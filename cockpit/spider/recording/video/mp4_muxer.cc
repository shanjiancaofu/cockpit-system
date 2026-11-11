#include "cockpit/spider/recording/video/mp4_muxer.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <filesystem>

namespace cockpit::recording {
namespace {
void SetError(std::string* error, const std::string& value) {
  if (error) *error = value;
}
}  // namespace

struct Mp4Muxer::Impl {
  GstElement* pipeline = nullptr;
  GstAppSrc* source = nullptr;
  std::uint32_t fps = 0;
  std::string failure;
};

Mp4Muxer::Mp4Muxer() : impl_(std::make_unique<Impl>()) {
}
Mp4Muxer::~Mp4Muxer() {
  Close();
}

bool Mp4Muxer::Open(const std::string& path, std::uint32_t fps, std::string* error) {
  Close();
  impl_->failure.clear();
  if (path.empty() || fps == 0) {
    SetError(error, "invalid MP4 muxer configuration");
    return false;
  }
  const auto parent = std::filesystem::path(path).parent_path();
  std::error_code filesystem_error;
  if (!parent.empty()) std::filesystem::create_directories(parent, filesystem_error);
  if (filesystem_error) {
    SetError(error, filesystem_error.message());
    return false;
  }
  gst_init(nullptr, nullptr);
  const auto launch =
      "appsrc name=src is-live=false format=time block=true "
      "caps=video/"
      "x-h264,stream-format=(string)byte-stream,alignment=(string)au,framerate=(fraction)" +
      std::to_string(fps) + "/1 ! h264parse ! mp4mux faststart=true ! filesink name=sink";
  GError* parse_error = nullptr;
  impl_->pipeline = gst_parse_launch(launch.c_str(), &parse_error);
  if (!impl_->pipeline || parse_error) {
    SetError(error, parse_error ? parse_error->message : "create MP4 pipeline failed");
    if (parse_error) g_error_free(parse_error);
    if (impl_->pipeline) gst_object_unref(impl_->pipeline);
    impl_->pipeline = nullptr;
    return false;
  }
  auto* sink = gst_bin_get_by_name(GST_BIN(impl_->pipeline), "sink");
  if (sink) {
    g_object_set(sink, "location", path.c_str(), nullptr);
    gst_object_unref(sink);
  }
  auto* source = gst_bin_get_by_name(GST_BIN(impl_->pipeline), "src");
  impl_->source = source ? GST_APP_SRC(source) : nullptr;
  impl_->fps = fps;
  if (!impl_->source ||
      gst_element_set_state(impl_->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    impl_->failure = "start MP4 pipeline failed";
    SetError(error, impl_->failure);
    Close();
    return false;
  }
  return true;
}

bool Mp4Muxer::Write(const EncodedVideoPacket& packet, std::string* error) {
  if (!impl_->source || packet.data.empty() || !impl_->failure.empty()) {
    if (impl_->failure.empty()) impl_->failure = "MP4 muxer is not open or packet is empty";
    SetError(error, impl_->failure);
    return false;
  }
  auto* buffer = gst_buffer_new_allocate(nullptr, packet.data.size(), nullptr);
  if (!buffer) {
    impl_->failure = "allocate MP4 packet failed";
    SetError(error, impl_->failure);
    return false;
  }
  gst_buffer_fill(buffer, 0, packet.data.data(), packet.data.size());
  GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(packet.pts, GST_SECOND, impl_->fps);
  GST_BUFFER_DTS(buffer) = gst_util_uint64_scale(packet.dts, GST_SECOND, impl_->fps);
  GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(GST_SECOND, 1, impl_->fps);
  const auto result = gst_app_src_push_buffer(impl_->source, buffer);
  if (result != GST_FLOW_OK) {
    impl_->failure = "write MP4 packet failed";
    SetError(error, impl_->failure);
    return false;
  }
  return true;
}

bool Mp4Muxer::Close(std::string* error) {
  // A previous Write/Close failure remains a failure even with no error output,
  // and even if Close is called again by a later service cleanup.
  if (!impl_->pipeline) {
    if (!impl_->failure.empty()) SetError(error, impl_->failure);
    return impl_->failure.empty();
  }
  auto fail = [&](const std::string& message) {
    if (impl_->failure.empty()) impl_->failure = message;
  };
  if (!impl_->source || gst_app_src_end_of_stream(impl_->source) != GST_FLOW_OK)
    fail("finish MP4 stream failed");
  GstBus* bus = gst_element_get_bus(impl_->pipeline);
  if (impl_->failure.empty()) {
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (!message) {
      fail("timed out waiting for MP4 EOS");
    } else {
      if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* pipeline_error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &pipeline_error, &debug);
        fail(pipeline_error ? pipeline_error->message : "MP4 pipeline reported an error");
        g_clear_error(&pipeline_error);
        g_free(debug);
      }
      gst_message_unref(message);
    }
  }
  gst_object_unref(bus);
  if (gst_element_set_state(impl_->pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE)
    fail("stop MP4 pipeline failed");
  if (impl_->source) gst_object_unref(impl_->source);
  gst_object_unref(impl_->pipeline);
  impl_->source = nullptr;
  impl_->pipeline = nullptr;
  impl_->fps = 0;
  if (!impl_->failure.empty()) SetError(error, impl_->failure);
  return impl_->failure.empty();
}

bool Mp4Muxer::open() const {
  return impl_->source != nullptr;
}

}  // namespace cockpit::recording
