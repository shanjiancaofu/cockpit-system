#include "modules/camera/capture/gstreamer_preview_pipeline.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video-info.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>

#include "core/utils/Time.h"

namespace cockpit {
namespace camera {
namespace {

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::string GErrorMessage(const std::string& prefix, GError* error) {
  if (error == nullptr) {
    return prefix;
  }
  return prefix + ": " + error->message;
}

const char* GstFormat(CameraPixelFormat format) {
  if (format == CameraPixelFormat::kRgb) {
    return "RGB";
  }
  return "BGRx";
}

CameraPixelFormat NormalizedOutputFormat(CameraPixelFormat format) {
  if (format == CameraPixelFormat::kRgb) {
    return CameraPixelFormat::kRgb;
  }
  return CameraPixelFormat::kBgrx;
}

std::uint32_t BytesPerPixel(CameraPixelFormat format) {
  if (format == CameraPixelFormat::kRgb) {
    return 3;
  }
  return 4;
}

std::string ReadBusError(GstElement* pipeline, const std::string& fallback) {
  if (pipeline == nullptr) {
    return fallback;
  }
  GstBus* bus = gst_element_get_bus(pipeline);
  if (bus == nullptr) {
    return fallback;
  }
  GstMessage* message = gst_bus_pop_filtered(
      bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));
  std::string result = fallback;
  if (message != nullptr) {
    GError* error = nullptr;
    gchar* debug = nullptr;
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
      gst_message_parse_error(message, &error, &debug);
    } else {
      gst_message_parse_warning(message, &error, &debug);
    }
    if (error != nullptr) {
      result += ": ";
      result += error->message;
    }
    if (debug != nullptr) {
      result += " (";
      result += debug;
      result += ")";
    }
    if (error != nullptr) {
      g_error_free(error);
    }
    if (debug != nullptr) {
      g_free(debug);
    }
    gst_message_unref(message);
  }
  gst_object_unref(bus);
  return result;
}

}  // namespace

GstreamerPreviewPipeline::GstreamerPreviewPipeline() {
  EnsureGstreamerInitialized();
}

GstreamerPreviewPipeline::~GstreamerPreviewPipeline() {
  ReleasePipeline();
}

bool GstreamerPreviewPipeline::Start(const CameraPreviewConfig& config, FrameCallback callback,
                                     std::string* error) {
  if (!callback) {
    SetError(error, "camera preview callback is empty");
    return false;
  }
  Stop();

  CameraPreviewConfig normalized = config;
  normalized.output_format = NormalizedOutputFormat(config.output_format);
  if (normalized.device.empty() || normalized.width == 0 || normalized.height == 0 ||
      normalized.fps == 0) {
    SetError(error, "invalid camera preview config");
    return false;
  }

  GError* gst_error = nullptr;
  GstElement* pipeline = gst_parse_launch(BuildPipelineDescription(normalized).c_str(), &gst_error);
  if (gst_error != nullptr || pipeline == nullptr) {
    SetError(error, GErrorMessage("create camera preview pipeline failed", gst_error));
    if (gst_error != nullptr) {
      g_error_free(gst_error);
    }
    if (pipeline != nullptr) {
      gst_object_unref(pipeline);
    }
    return false;
  }

  GstElement* appsink = gst_bin_get_by_name(GST_BIN(pipeline), "preview_sink");
  if (appsink == nullptr) {
    SetError(error, "camera preview pipeline missing appsink");
    gst_object_unref(pipeline);
    return false;
  }

  GstAppSinkCallbacks callbacks{};
  callbacks.new_sample = [](GstAppSink* appsink, gpointer user_data) -> GstFlowReturn {
    return static_cast<GstFlowReturn>(OnNewSample(appsink, user_data));
  };
  gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, this, nullptr);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_ = pipeline;
    appsink_ = appsink;
    callback_ = std::move(callback);
    config_ = normalized;
    sequence_ = 0;
  }

  const GstStateChangeReturn state_result = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  if (state_result == GST_STATE_CHANGE_FAILURE) {
    SetError(error, ReadBusError(pipeline_, "start camera preview pipeline failed"));
    Stop();
    return false;
  }
  if (state_result == GST_STATE_CHANGE_ASYNC) {
    GstState state = GST_STATE_NULL;
    const GstStateChangeReturn wait_result =
        gst_element_get_state(pipeline_, &state, nullptr, 3 * GST_SECOND);
    if (wait_result == GST_STATE_CHANGE_FAILURE) {
      SetError(error, ReadBusError(pipeline_, "start camera preview pipeline failed"));
      Stop();
      return false;
    }
  }

  running_.store(true);
  return true;
}

void GstreamerPreviewPipeline::Stop() {
  ReleasePipeline();
}

void GstreamerPreviewPipeline::ReleasePipeline() {
  GstElement* pipeline = nullptr;
  GstElement* appsink = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline = pipeline_;
    appsink = appsink_;
    pipeline_ = nullptr;
    appsink_ = nullptr;
    callback_ = nullptr;
  }

  running_.store(false);
  if (pipeline != nullptr) {
    gst_element_set_state(pipeline, GST_STATE_NULL);
  }
  if (appsink != nullptr) {
    gst_object_unref(appsink);
  }
  if (pipeline != nullptr) {
    gst_object_unref(pipeline);
  }
}

void GstreamerPreviewPipeline::EnsureGstreamerInitialized() {
  static std::once_flag init_once;
  std::call_once(init_once, []() {
    gst_init(nullptr, nullptr);
  });
}

int GstreamerPreviewPipeline::OnNewSample(void* appsink, void* user_data) {
  auto* self = static_cast<GstreamerPreviewPipeline*>(user_data);
  GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
  if (sample == nullptr || self == nullptr) {
    return GST_FLOW_OK;
  }
  const int result = self->HandleNewSample(sample);
  gst_sample_unref(sample);
  return result;
}

int GstreamerPreviewPipeline::HandleNewSample(GstSample* sample) {
  FrameCallback callback;
  CameraPreviewConfig config;
  std::uint64_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = callback_;
    config = config_;
    sequence = ++sequence_;
  }
  if (!callback) {
    return GST_FLOW_OK;
  }

  GstCaps* caps = gst_sample_get_caps(sample);
  GstVideoInfo video_info{};
  if (caps == nullptr || !gst_video_info_from_caps(&video_info, caps)) {
    return GST_FLOW_OK;
  }

  GstBuffer* buffer = gst_sample_get_buffer(sample);
  GstMapInfo map{};
  if (buffer == nullptr || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    return GST_FLOW_OK;
  }

  CameraFrame frame;
  frame.sequence = sequence;
  frame.timestamp_ms = static_cast<std::uint64_t>(cockpit::utils::NowMs());
  frame.width = GST_VIDEO_INFO_WIDTH(&video_info);
  frame.height = GST_VIDEO_INFO_HEIGHT(&video_info);
  frame.stride_bytes = static_cast<std::uint32_t>(GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0));
  frame.format = config.output_format;

  const std::uint32_t min_stride = frame.width * BytesPerPixel(frame.format);
  const std::uint32_t copied_stride = frame.stride_bytes == 0 ? min_stride : frame.stride_bytes;
  const std::size_t expected_size = static_cast<std::size_t>(copied_stride) * frame.height;
  const std::size_t copy_size = std::min(expected_size, static_cast<std::size_t>(map.size));
  frame.data.resize(copy_size);
  std::memcpy(frame.data.data(), map.data, copy_size);

  gst_buffer_unmap(buffer, &map);
  callback(std::move(frame));
  return GST_FLOW_OK;
}

std::string GstreamerPreviewPipeline::BuildPipelineDescription(
    const CameraPreviewConfig& config) const {
  std::ostringstream stream;
  stream << "v4l2src device=" << config.device << " ! "
         << "videoconvert ! videoscale ! videorate ! "
         << "video/x-raw,format=" << GstFormat(config.output_format) << ",width=" << config.width
         << ",height=" << config.height << ",framerate=" << config.fps << "/1 ! "
         << "appsink name=preview_sink emit-signals=true sync=false max-buffers=2 drop=true";
  return stream.str();
}

}  // namespace camera
}  // namespace cockpit
