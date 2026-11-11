#include "cockpit/transfer/rtsp/rtsp_server.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "cockpit/exchange/foundation/logging/logger.h"

namespace cockpit::transfer {
struct RtspServer::Impl {
  RtspServerConfig config;
  std::atomic<bool> stopping{false};
  std::atomic<bool> active{false};
  std::shared_ptr<VideoSubscription> subscription;
  bool waiting_keyframe = true;
  GMainContext* context = nullptr;
  GMainLoop* loop = nullptr;
  GstRTSPServer* server = nullptr;
  GstRTSPMediaFactory* factory = nullptr;
  GstAppSrc* appsrc = nullptr;
  std::thread main_thread;
  std::mutex mutex;
  RtspPushStats stats;
  std::vector<std::uint8_t> pending_packet;
  std::int64_t pending_pts = 0;
  bool pending_keyframe = false;
  bool have_input_pts = false;
  std::int64_t input_pts_origin = 0;
  std::int64_t last_input_pts = 0;
  GstClockTime output_origin = 0;
  bool need_output_origin = true;
  std::vector<std::uint8_t> sps;
  std::vector<std::uint8_t> pps;

  static std::vector<std::uint8_t> Avcc(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> output;
    for (const auto& nal : H264Nals(data)) {
      const auto length = static_cast<std::uint32_t>(nal.size());
      output.push_back(static_cast<std::uint8_t>(length >> 24));
      output.push_back(static_cast<std::uint8_t>(length >> 16));
      output.push_back(static_cast<std::uint8_t>(length >> 8));
      output.push_back(static_cast<std::uint8_t>(length));
      output.insert(output.end(), nal.begin(), nal.end());
    }
    return output;
  }
  void SetSourceCaps() {
    if (!appsrc || sps.size() < 4 || pps.empty()) return;

    std::vector<std::uint8_t> codec_data{1,
                                         sps[1],
                                         sps[2],
                                         sps[3],
                                         0xff,
                                         0xe1,
                                         static_cast<std::uint8_t>(sps.size() >> 8),
                                         static_cast<std::uint8_t>(sps.size())};
    codec_data.insert(codec_data.end(), sps.begin(), sps.end());
    codec_data.push_back(1);
    codec_data.push_back(static_cast<std::uint8_t>(pps.size() >> 8));
    codec_data.push_back(static_cast<std::uint8_t>(pps.size()));
    codec_data.insert(codec_data.end(), pps.begin(), pps.end());
    auto* codec = gst_buffer_new_allocate(nullptr, codec_data.size(), nullptr);
    gst_buffer_fill(codec, 0, codec_data.data(), codec_data.size());
    auto* caps =
        gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "avc", "alignment",
                            G_TYPE_STRING, "au", "framerate", GST_TYPE_FRACTION, config.fps, 1,
                            "codec_data", GST_TYPE_BUFFER, codec, nullptr);
    gst_app_src_set_caps(appsrc, caps);
    gst_caps_unref(caps);
    gst_buffer_unref(codec);
  }
  void UpdateParameterSets(const std::vector<std::uint8_t>& data) {
    const auto old_sps = sps, old_pps = pps;
    for (const auto& nal : H264Nals(data)) {
      if (nal.empty()) continue;
      if ((nal[0] & 0x1f) == 7) sps = nal;
      if ((nal[0] & 0x1f) == 8) pps = nal;
    }
    if (old_sps == sps && old_pps == pps) return;
    SetSourceCaps();
    if (factory && sps.size() >= 4 && !pps.empty()) {
      std::vector<std::uint8_t> codec{1,
                                      sps[1],
                                      sps[2],
                                      sps[3],
                                      0xff,
                                      0xe1,
                                      static_cast<std::uint8_t>(sps.size() >> 8),
                                      static_cast<std::uint8_t>(sps.size())};
      codec.insert(codec.end(), sps.begin(), sps.end());
      codec.push_back(1);
      codec.push_back(static_cast<std::uint8_t>(pps.size() >> 8));
      codec.push_back(static_cast<std::uint8_t>(pps.size()));
      codec.insert(codec.end(), pps.begin(), pps.end());
      static constexpr char hex[] = "0123456789abcdef";
      std::string encoded;
      encoded.reserve(codec.size() * 2);
      for (const auto byte : codec) {
        encoded.push_back(hex[byte >> 4]);
        encoded.push_back(hex[byte & 0x0f]);
      }
      const auto launch =
          "( appsrc name=src is-live=true do-timestamp=true format=time block=false caps=video/"
          "x-h264,stream-format=(string)avc,alignment=(string)au,framerate=(fraction)" +
          std::to_string(config.fps) + "/1,codec_data=(buffer)" + encoded +
          " ! h264parse ! rtph264pay name=pay0 pt=96 config-interval=1 )";
      gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    }
  }

  ~Impl() {
    Stop();
  }
  void Stop() {
    active = false;
    stopping = true;
    if (subscription) {
      subscription->Stop();
      subscription.reset();
    }
    if (main_thread.joinable()) {
      g_main_context_invoke(
          context,
          +[](gpointer data) -> gboolean {
            auto* self = static_cast<Impl*>(data);
            gst_rtsp_server_client_filter(
                self->server,
                +[](GstRTSPServer*, GstRTSPClient* client, gpointer) {
                  gst_rtsp_client_close(client);
                  return GST_RTSP_FILTER_REMOVE;
                },
                nullptr);
            g_main_loop_quit(self->loop);
            return G_SOURCE_REMOVE;
          },
          this);
    }
    if (main_thread.joinable()) main_thread.join();
    std::lock_guard<std::mutex> lock(mutex);
    if (appsrc) {
      gst_object_unref(appsrc);
      appsrc = nullptr;
    }
    if (server) {
      g_object_unref(server);
      server = nullptr;
    }
    if (factory) {
      g_object_unref(factory);
      factory = nullptr;
    }
    if (loop) {
      g_main_loop_unref(loop);
      loop = nullptr;
    }
    if (context) {
      g_main_context_unref(context);
      context = nullptr;
    }
  }
  // Caller holds mutex. gst_app_src_push_buffer takes ownership even on error.
  bool PushBuffer(GstBuffer* buffer) {
    const auto flow = buffer ? gst_app_src_push_buffer(appsrc, buffer) : GST_FLOW_ERROR;
    stats.last_flow_return = flow;
    if (flow == GST_FLOW_OK) {
      ++stats.pushed;
      return true;
    }
    ++stats.failures;
    if (stats.failures == 1 || stats.failures % 100 == 0)
      LOG_WARN("RTSP H264 push failed: " + std::string(gst_flow_get_name(flow)) +
               "; failures=" + std::to_string(stats.failures));
    waiting_keyframe = true;
    need_output_origin = true;
    pending_packet.clear();
    pending_keyframe = false;
    return false;
  }

  static void MediaConfigure(GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    g_signal_connect(media, "unprepared", G_CALLBACK(Impl::MediaUnprepared), user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* source = element ? gst_bin_get_by_name(GST_BIN(element), "src") : nullptr;
    if (element) gst_object_unref(element);
    std::lock_guard<std::mutex> lock(self->mutex);
    if (self->appsrc) gst_object_unref(self->appsrc);
    self->appsrc = source ? GST_APP_SRC(source) : nullptr;
    self->waiting_keyframe = true;
    self->need_output_origin = true;
    if (self->appsrc) {
      gst_app_src_set_stream_type(self->appsrc, GST_APP_STREAM_TYPE_STREAM);
      gst_app_src_set_latency(self->appsrc, 0, 2000);
      g_object_set(self->appsrc, "max-bytes", guint64(2 * 1024 * 1024), nullptr);
      self->SetSourceCaps();
      if (!self->pending_packet.empty() && self->pending_keyframe) {
        const auto avcc = Avcc(self->pending_packet);
        auto* buffer = gst_buffer_new_allocate(nullptr, avcc.size(), nullptr);
        if (!buffer) {
          self->PushBuffer(nullptr);
          return;
        }
        gst_buffer_fill(buffer, 0, avcc.data(), avcc.size());
        GST_BUFFER_PTS(buffer) = 0;
        GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(GST_SECOND, 1, self->config.fps);
        self->PushBuffer(buffer);
        self->pending_packet.clear();
        self->pending_keyframe = false;
        self->waiting_keyframe = true;  // Cached IDR is for preroll; resume at a fresh IDR.
      }
    }
  }
  static void MediaUnprepared(GstRTSPMedia* media, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* source = element ? gst_bin_get_by_name(GST_BIN(element), "src") : nullptr;
    std::lock_guard<std::mutex> lock(self->mutex);
    if (self->appsrc && (!source || GST_OBJECT(self->appsrc) == GST_OBJECT(source))) {
      gst_object_unref(self->appsrc);
      self->appsrc = nullptr;
    }
    if (source) gst_object_unref(source);
    if (element) gst_object_unref(element);
  }
  void Push(std::vector<std::uint8_t> data, std::int64_t packet_pts, bool keyframe) {
    std::lock_guard<std::mutex> lock(mutex);
    if (data.empty()) return;
    if (!have_input_pts || (keyframe && packet_pts < last_input_pts)) {
      input_pts_origin = packet_pts;
      have_input_pts = true;
      need_output_origin = true;  // Encoder restart must not rewind an existing RTSP clock.
    }
    last_input_pts = packet_pts;
    if (appsrc && need_output_origin && keyframe) {
      input_pts_origin = packet_pts;
      auto* clock = gst_element_get_clock(GST_ELEMENT(appsrc));
      const auto base = gst_element_get_base_time(GST_ELEMENT(appsrc));
      const auto now = clock ? gst_clock_get_time(clock) : 0;
      output_origin = now > base ? now - base : 0;
      if (clock) gst_object_unref(clock);
      need_output_origin = false;
    }
    packet_pts = std::max<std::int64_t>(0, packet_pts - input_pts_origin);
    UpdateParameterSets(data);
    if (!appsrc) {
      if (keyframe) {
        pending_packet = std::move(data);
        pending_pts = packet_pts;
        pending_keyframe = true;
      }
      return;
    }
    if (gst_app_src_get_current_level_bytes(appsrc) > 2 * 1024 * 1024) {
      ++stats.backlog_drops;
      waiting_keyframe = true;
      return;
    }
    if (waiting_keyframe && !keyframe) return;
    waiting_keyframe = false;
    const auto avcc = Avcc(data);
    auto* buffer = gst_buffer_new_allocate(nullptr, avcc.size(), nullptr);
    if (!buffer) {
      PushBuffer(nullptr);
      return;
    }
    gst_buffer_fill(buffer, 0, avcc.data(), avcc.size());
    GST_BUFFER_PTS(buffer) =
        output_origin + gst_util_uint64_scale(packet_pts, GST_SECOND, config.fps);
    GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(GST_SECOND, 1, config.fps);
    PushBuffer(buffer);
  }
};

RtspServer::RtspServer() : impl_(std::make_unique<Impl>()) {
}
RtspServer::~RtspServer() = default;
bool RtspServer::Start(const RtspServerConfig& config, H264Receiver& receiver, std::string* error) {
  if (impl_->active) return false;
  if (config.port == 0 || config.udp_port == 0 || config.path.empty() || config.fps == 0) {
    if (error) *error = "invalid RTSP server configuration";
    return false;
  }
  gst_init(nullptr, nullptr);
  impl_->config = config;
  impl_->stats = {};
  impl_->have_input_pts = false;
  impl_->input_pts_origin = 0;
  impl_->last_input_pts = 0;
  impl_->sps.clear();
  impl_->pps.clear();
  impl_->need_output_origin = true;
  impl_->waiting_keyframe = true;
  impl_->pending_packet.clear();
  impl_->pending_keyframe = false;
  impl_->context = g_main_context_new();
  impl_->loop = g_main_loop_new(impl_->context, FALSE);
  impl_->server = gst_rtsp_server_new();
  gst_rtsp_server_set_service(impl_->server, std::to_string(config.port).c_str());
  gst_rtsp_server_set_address(impl_->server, config.address.c_str());
  auto* mounts = gst_rtsp_server_get_mount_points(impl_->server);
  auto* factory = gst_rtsp_media_factory_new();
  impl_->factory = GST_RTSP_MEDIA_FACTORY(g_object_ref(factory));
  const auto launch =
      "( appsrc name=src is-live=true do-timestamp=true format=time block=false caps=video/"
      "x-h264,stream-format=(string)avc,alignment=(string)au,framerate=(fraction)" +
      std::to_string(config.fps) + "/1 ! rtph264pay name=pay0 pt=96 config-interval=1 )";
  gst_rtsp_media_factory_set_launch(factory, launch.c_str());
  const auto codec = receiver.Status();
  if (!codec.sps.empty() && !codec.pps.empty()) {
    std::vector<std::uint8_t> parameters{0, 0, 0, 1};
    parameters.insert(parameters.end(), codec.sps.begin(), codec.sps.end());
    parameters.insert(parameters.end(), {0, 0, 0, 1});
    parameters.insert(parameters.end(), codec.pps.begin(), codec.pps.end());
    impl_->UpdateParameterSets(parameters);
  }
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  g_signal_connect(factory, "media-configure", G_CALLBACK(Impl::MediaConfigure), impl_.get());
  gst_rtsp_mount_points_add_factory(mounts, config.path.c_str(), factory);
  g_object_unref(mounts);
  g_main_context_push_thread_default(impl_->context);
  if (gst_rtsp_server_attach(impl_->server, impl_->context) == 0) {
    g_main_context_pop_thread_default(impl_->context);
    if (error) *error = "attach RTSP server failed";
    impl_->Stop();
    return false;
  }
  g_main_context_pop_thread_default(impl_->context);
  impl_->stopping = false;
  impl_->active = true;
  std::promise<void> ready;
  auto started = ready.get_future();
  auto* startup = g_idle_source_new();
  g_source_set_callback(
      startup,
      +[](gpointer data) -> gboolean {
        static_cast<std::promise<void>*>(data)->set_value();
        return G_SOURCE_REMOVE;
      },
      &ready, nullptr);
  g_source_attach(startup, impl_->context);
  g_source_unref(startup);
  impl_->main_thread = std::thread([this] {
    g_main_context_push_thread_default(impl_->context);
    g_main_loop_run(impl_->loop);
    g_main_context_pop_thread_default(impl_->context);
  });
  started.get();
  impl_->subscription = receiver.Subscribe([this](VideoFramePtr frame) {
    if (frame->discontinuity) {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      impl_->waiting_keyframe = true;
      impl_->need_output_origin = true;
      impl_->pending_packet.clear();
    }
    impl_->Push(frame->packet.data, frame->packet.pts, frame->packet.keyframe);
  });
  return true;
}
void RtspServer::Stop() {
  impl_->Stop();
}
RtspPushStats RtspServer::Stats() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->stats;
}
bool RtspServer::running() const {
  return impl_->active;
}
}  // namespace cockpit::transfer
