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
};

Mp4Muxer::Mp4Muxer() : impl_(std::make_unique<Impl>()) {}
Mp4Muxer::~Mp4Muxer() { Close(); }

bool Mp4Muxer::Open(const std::string& path, std::uint32_t fps, std::string* error) {
  Close();
  if (path.empty() || fps == 0) {
    SetError(error, "invalid MP4 muxer configuration");
    return false;
  }
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
  gst_init(nullptr, nullptr);
  const auto launch =
      "appsrc name=src is-live=false format=time block=true caps=video/x-h264,stream-format=(string)byte-stream,alignment=(string)au,framerate=(fraction)" +
      std::to_string(fps) + "/1 ! h264parse ! mp4mux faststart=true ! filesink location=\"" + path + "\"";
  GError* parse_error = nullptr;
  impl_->pipeline = gst_parse_launch(launch.c_str(), &parse_error);
  if (!impl_->pipeline) {
    SetError(error, parse_error ? parse_error->message : "create MP4 pipeline failed");
    if (parse_error) g_error_free(parse_error);
    return false;
  }
  auto* source = gst_bin_get_by_name(GST_BIN(impl_->pipeline), "src");
  impl_->source = source ? GST_APP_SRC(source) : nullptr;
  impl_->fps = fps;
  if (!impl_->source || gst_element_set_state(impl_->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    SetError(error, "start MP4 pipeline failed");
    Close();
    return false;
  }
  return true;
}

bool Mp4Muxer::Write(const EncodedVideoPacket& packet, std::string* error) {
  if (!impl_->source || packet.data.empty()) {
    SetError(error, "MP4 muxer is not open or packet is empty");
    return false;
  }
  auto* buffer = gst_buffer_new_allocate(nullptr, packet.data.size(), nullptr);
  gst_buffer_fill(buffer, 0, packet.data.data(), packet.data.size());
  GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(packet.pts, GST_SECOND, impl_->fps);
  GST_BUFFER_DTS(buffer) = gst_util_uint64_scale(packet.dts, GST_SECOND, impl_->fps);
  GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(GST_SECOND, 1, impl_->fps);
  const auto result = gst_app_src_push_buffer(impl_->source, buffer);
  if (result != GST_FLOW_OK) {
    SetError(error, "write MP4 packet failed");
    return false;
  }
  return true;
}

bool Mp4Muxer::Close(std::string* error) {
  if (!impl_->pipeline) return true;
  if (impl_->source) {
    const auto result = gst_app_src_end_of_stream(impl_->source);
    if (result != GST_FLOW_OK) SetError(error, "finish MP4 stream failed");
  }
  GstBus* bus = gst_element_get_bus(impl_->pipeline);
  GstMessage* message = gst_bus_timed_pop_filtered(
      bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
  if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
    if (error && error->empty()) *error = "MP4 pipeline reported an error";
  }
  if (message) gst_message_unref(message);
  gst_object_unref(bus);
  gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
  if (impl_->source) gst_object_unref(impl_->source);
  gst_object_unref(impl_->pipeline);
  impl_->source = nullptr;
  impl_->pipeline = nullptr;
  impl_->fps = 0;
  return error == nullptr || error->empty();
}

bool Mp4Muxer::open() const { return impl_->source != nullptr; }

}  // namespace cockpit::recording
