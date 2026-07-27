#include "cockpit/modules/camera/photo/jpeg_encoder.h"

#include <fcntl.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <vector>

namespace cockpit {
namespace camera {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::uint32_t BytesPerPixel(CameraPixelFormat format) {
  if (format == CameraPixelFormat::kRgb) {
    return 3;
  }
  if (format == CameraPixelFormat::kBgrx) {
    return 4;
  }
  return 0;
}

const char* GstreamerFormat(CameraPixelFormat format) {
  return format == CameraPixelFormat::kRgb ? "RGB" : "BGRx";
}

void UnrefElement(GstElement* element) {
  if (element != nullptr) {
    gst_object_unref(element);
  }
}

void EnsureGstreamerInitialized() {
  static std::once_flag once;
  std::call_once(once, [] {
    gst_init(nullptr, nullptr);
  });
}

std::string PipelineError(GstMessage* message) {
  GError* gst_error = nullptr;
  gchar* debug = nullptr;
  gst_message_parse_error(message, &gst_error, &debug);
  std::string result = gst_error == nullptr ? "JPEG pipeline failed" : gst_error->message;
  if (gst_error != nullptr) {
    g_error_free(gst_error);
  }
  if (debug != nullptr) {
    g_free(debug);
  }
  return result;
}

}  // namespace

bool JpegEncoder::IsAvailable() {
  return true;
}

bool JpegEncoder::Encode(const CameraFrame& frame, const std::filesystem::path& output_path,
                         int quality, std::string* error) {
  if (!frame.IsValid()) {
    AssignError(error, "camera frame is invalid");
    return false;
  }
  const std::uint32_t bytes_per_pixel = BytesPerPixel(frame.format);
  if (bytes_per_pixel == 0) {
    AssignError(error, "JPEG encoder only supports RGB and BGRx frames");
    return false;
  }
  const std::size_t tight_stride = static_cast<std::size_t>(frame.width) * bytes_per_pixel;
  if (frame.stride_bytes < tight_stride ||
      frame.data.size() < static_cast<std::size_t>(frame.stride_bytes) * frame.height) {
    AssignError(error, "camera frame stride or payload is invalid");
    return false;
  }

  const std::filesystem::path parent_path =
      output_path.parent_path().empty() ? std::filesystem::path(".") : output_path.parent_path();
  try {
    if (!parent_path.empty()) {
      std::filesystem::create_directories(parent_path);
    }
  } catch (const std::exception& exception) {
    AssignError(error, exception.what());
    return false;
  }

  std::string temporary_template =
      (parent_path / ("." + output_path.filename().string() + ".tmp.XXXXXX")).string();
  std::vector<char> temporary_name(temporary_template.begin(), temporary_template.end());
  temporary_name.push_back('\0');
  const int temporary_fd = mkstemp(temporary_name.data());
  if (temporary_fd < 0) {
    AssignError(error, "create temporary JPEG failed: " + std::string(std::strerror(errno)));
    return false;
  }
  close(temporary_fd);
  const std::filesystem::path temporary_path(temporary_name.data());
  const auto remove_temporary = [&temporary_path]() {
    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
  };

  std::vector<std::uint8_t> packed(tight_stride * frame.height);
  for (std::uint32_t row = 0; row < frame.height; ++row) {
    const auto* source = frame.data.data() + static_cast<std::size_t>(row) * frame.stride_bytes;
    std::copy_n(source, tight_stride, packed.data() + static_cast<std::size_t>(row) * tight_stride);
  }

  EnsureGstreamerInitialized();
  GstElement* pipeline = gst_pipeline_new("camera-photo-pipeline");
  GstElement* source = gst_element_factory_make("appsrc", "photo-source");
  GstElement* convert = gst_element_factory_make("videoconvert", "photo-convert");
  GstElement* encoder = gst_element_factory_make("jpegenc", "photo-encoder");
  GstElement* sink = gst_element_factory_make("filesink", "photo-sink");
  if (pipeline == nullptr || source == nullptr || convert == nullptr || encoder == nullptr ||
      sink == nullptr) {
    UnrefElement(sink);
    UnrefElement(encoder);
    UnrefElement(convert);
    UnrefElement(source);
    UnrefElement(pipeline);
    remove_temporary();
    AssignError(error, "create GStreamer JPEG elements failed; install jpegenc plugin");
    return false;
  }

  const int normalized_quality = std::clamp(quality, 1, 100);
  g_object_set(encoder, "quality", normalized_quality, nullptr);
  g_object_set(sink, "location", temporary_path.c_str(), nullptr);
  GstCaps* caps = gst_caps_new_simple(
      "video/x-raw", "format", G_TYPE_STRING, GstreamerFormat(frame.format), "width", G_TYPE_INT,
      static_cast<int>(frame.width), "height", G_TYPE_INT, static_cast<int>(frame.height),
      "framerate", GST_TYPE_FRACTION, 1, 1, nullptr);
  g_object_set(source, "caps", caps, "format", GST_FORMAT_TIME, nullptr);
  gst_caps_unref(caps);

  gst_bin_add_many(GST_BIN(pipeline), source, convert, encoder, sink, nullptr);
  if (!gst_element_link_many(source, convert, encoder, sink, nullptr) ||
      gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    remove_temporary();
    AssignError(error, "start GStreamer JPEG pipeline failed");
    return false;
  }

  GstBuffer* buffer = gst_buffer_new_allocate(nullptr, packed.size(), nullptr);
  gst_buffer_fill(buffer, 0, packed.data(), packed.size());
  GST_BUFFER_PTS(buffer) = 0;
  GST_BUFFER_DURATION(buffer) = GST_SECOND;
  const GstFlowReturn push_result = gst_app_src_push_buffer(GST_APP_SRC(source), buffer);
  const GstFlowReturn end_result = gst_app_src_end_of_stream(GST_APP_SRC(source));
  GstBus* bus = gst_element_get_bus(pipeline);
  GstMessage* message = gst_bus_timed_pop_filtered(
      bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

  bool success = push_result == GST_FLOW_OK && end_result == GST_FLOW_OK && message != nullptr &&
                 GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
  if (!success) {
    AssignError(error, message != nullptr && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR
                           ? PipelineError(message)
                           : "GStreamer JPEG pipeline timed out");
  }
  if (message != nullptr) {
    gst_message_unref(message);
  }
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  if (!success) {
    remove_temporary();
    return false;
  }

  const int sync_fd = open(temporary_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (sync_fd < 0) {
    remove_temporary();
    AssignError(error, "open temporary JPEG for sync failed: " + std::string(std::strerror(errno)));
    return false;
  }
  if (fsync(sync_fd) != 0) {
    const int sync_error = errno;
    close(sync_fd);
    remove_temporary();
    AssignError(error, "sync temporary JPEG failed: " + std::string(std::strerror(sync_error)));
    return false;
  }
  if (close(sync_fd) != 0) {
    const int close_error = errno;
    remove_temporary();
    AssignError(error, "close temporary JPEG failed: " + std::string(std::strerror(close_error)));
    return false;
  }
  if (link(temporary_path.c_str(), output_path.c_str()) != 0) {
    const int publish_error = errno;
    remove_temporary();
    AssignError(error, publish_error == EEXIST
                           ? "photo destination already exists: " + output_path.string()
                           : "publish JPEG failed: " + std::string(std::strerror(publish_error)));
    return false;
  }
  remove_temporary();
  const int directory_fd = open(parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory_fd < 0) {
    AssignError(error, "open JPEG directory for sync failed: " + std::string(std::strerror(errno)));
    return false;
  }
  if (fsync(directory_fd) != 0) {
    const int sync_error = errno;
    close(directory_fd);
    AssignError(error, "sync JPEG directory failed: " + std::string(std::strerror(sync_error)));
    return false;
  }
  if (close(directory_fd) != 0) {
    AssignError(error, "close JPEG directory failed: " + std::string(std::strerror(errno)));
    return false;
  }
  return true;
}

}  // namespace camera
}  // namespace cockpit
