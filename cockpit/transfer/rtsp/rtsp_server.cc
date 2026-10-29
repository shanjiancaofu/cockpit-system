#include "cockpit/transfer/rtsp/rtsp_server.h"

#include <arpa/inet.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace cockpit::transfer {
namespace {
constexpr std::uint32_t kMagic = 0x43525031;
constexpr std::size_t kHeaderSize = 24;
std::uint16_t Get16(const std::uint8_t* p) {
  std::uint16_t value;
  std::memcpy(&value, p, sizeof(value));
  return ntohs(value);
}
std::uint32_t Get32(const std::uint8_t* p) {
  std::uint32_t value;
  std::memcpy(&value, p, sizeof(value));
  return ntohl(value);
}
std::int64_t Get64(const std::uint8_t* p) {
  std::uint32_t high_network = 0;
  std::uint32_t low_network = 0;
  // Datagram payloads are not required to be naturally aligned.  Copy before
  // converting so this remains defined on ARM as well as x86.
  std::memcpy(&high_network, p, sizeof(high_network));
  std::memcpy(&low_network, p + 4, sizeof(low_network));
  const auto high = ntohl(high_network);
  const auto low = ntohl(low_network);
  return static_cast<std::int64_t>((static_cast<std::uint64_t>(high) << 32) | low);
}
}  // namespace

struct RtspServer::Impl {
  RtspServerConfig config;
  std::atomic<bool> stopping{false};
  std::atomic<bool> active{false};
  int socket = -1;
  GMainContext* context = nullptr;
  GMainLoop* loop = nullptr;
  GstRTSPServer* server = nullptr;
  GstRTSPMediaFactory* factory = nullptr;
  GstAppSrc* appsrc = nullptr;
  std::thread main_thread;
  std::thread receiver_thread;
  std::mutex mutex;
  std::uint32_t frame_id = 0;
  std::uint16_t expected_fragments = 0;
  std::vector<std::vector<std::uint8_t>> fragments;
  std::int64_t pts = 0;
  std::vector<std::uint8_t> pending_packet;
  std::int64_t pending_pts = 0;
  std::vector<std::uint8_t> sps;
  std::vector<std::uint8_t> pps;

  static std::vector<std::vector<std::uint8_t>> Nals(const std::vector<std::uint8_t>& data) {
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i + 3 < data.size();) {
      const std::size_t size = data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1
                                   ? 3
                                   : (i + 4 <= data.size() && data[i] == 0 && data[i + 1] == 0 &&
                                              data[i + 2] == 0 && data[i + 3] == 1
                                          ? 4
                                          : 0);
      if (size) {
        starts.push_back(i + size);
        i += size;
      } else {
        ++i;
      }
    }
    std::vector<std::vector<std::uint8_t>> result;
    for (std::size_t i = 0; i < starts.size(); ++i) {
      const auto end = i + 1 < starts.size() ? starts[i + 1] : data.size();
      if (starts[i] < end) result.emplace_back(data.begin() + starts[i], data.begin() + end);
    }
    return result;
  }
  static std::vector<std::uint8_t> Avcc(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> output;
    for (const auto& nal : Nals(data)) {
      const auto length = static_cast<std::uint32_t>(nal.size());
      output.push_back(static_cast<std::uint8_t>(length >> 24));
      output.push_back(static_cast<std::uint8_t>(length >> 16));
      output.push_back(static_cast<std::uint8_t>(length >> 8));
      output.push_back(static_cast<std::uint8_t>(length));
      output.insert(output.end(), nal.begin(), nal.end());
    }
    return output;
  }
  void UpdateParameterSets(const std::vector<std::uint8_t>& data) {
    for (const auto& nal : Nals(data)) {
      if (nal.empty()) continue;
      if ((nal[0] & 0x1f) == 7) sps = nal;
      if ((nal[0] & 0x1f) == 8) pps = nal;
    }
    if (factory && sps.size() >= 4 && !pps.empty()) {
      std::vector<std::uint8_t> codec{1, sps[1], sps[2], sps[3], 0xff, 0xe1,
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
    if (!active.exchange(false) && socket < 0 && !server && !loop && !context) return;
    stopping = true;
    if (socket >= 0) {
      shutdown(socket, SHUT_RDWR);
      close(socket);
      socket = -1;
    }
    if (loop) g_main_loop_quit(loop);
    if (receiver_thread.joinable()) receiver_thread.join();
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
  static void MediaConfigure(GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    g_signal_connect(media, "unprepared", G_CALLBACK(Impl::MediaUnprepared), user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* source = gst_bin_get_by_name(GST_BIN(element), "src");
    gst_object_unref(element);
    std::lock_guard<std::mutex> lock(self->mutex);
    if (self->appsrc) gst_object_unref(self->appsrc);
    self->appsrc = source ? GST_APP_SRC(source) : nullptr;
    if (self->appsrc) {
      gst_app_src_set_stream_type(self->appsrc, GST_APP_STREAM_TYPE_STREAM);
      gst_app_src_set_latency(self->appsrc, 0, 2000);
      if (self->sps.size() >= 4 && !self->pps.empty()) {
        std::vector<std::uint8_t> codec_data{1, self->sps[1], self->sps[2], self->sps[3], 0xff, 0xe1,
                                             static_cast<std::uint8_t>(self->sps.size() >> 8),
                                             static_cast<std::uint8_t>(self->sps.size()),
                                             0};
        codec_data.insert(codec_data.end(), self->sps.begin(), self->sps.end());
        codec_data.push_back(1);
        codec_data.push_back(static_cast<std::uint8_t>(self->pps.size() >> 8));
        codec_data.push_back(static_cast<std::uint8_t>(self->pps.size()));
        codec_data.insert(codec_data.end(), self->pps.begin(), self->pps.end());
        auto* codec = gst_buffer_new_allocate(nullptr, codec_data.size(), nullptr);
        gst_buffer_fill(codec, 0, codec_data.data(), codec_data.size());
        auto* caps = gst_caps_new_simple(
            "video/x-h264", "stream-format", G_TYPE_STRING, "avc", "alignment", G_TYPE_STRING,
            "au", "framerate", GST_TYPE_FRACTION, self->config.fps, 1, "codec_data",
            GST_TYPE_BUFFER, codec, nullptr);
        gst_app_src_set_caps(self->appsrc, caps);
        gst_caps_unref(caps);
        gst_buffer_unref(codec);
      }
      if (!self->pending_packet.empty()) {
        const auto avcc = Avcc(self->pending_packet);
        auto* buffer = gst_buffer_new_allocate(nullptr, avcc.size(), nullptr);
        gst_buffer_fill(buffer, 0, avcc.data(), avcc.size());
        GST_BUFFER_PTS(buffer) =
            gst_util_uint64_scale(self->pending_pts, GST_SECOND, self->config.fps);
        GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(GST_SECOND, 1, self->config.fps);
        gst_app_src_push_buffer(self->appsrc, buffer);
        self->pending_packet.clear();
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
  void Push(std::vector<std::uint8_t> data, std::int64_t packet_pts) {
    std::lock_guard<std::mutex> lock(mutex);
    if (data.empty()) return;
    UpdateParameterSets(data);
    if (!appsrc) {
      pending_packet = std::move(data);
      pending_pts = packet_pts;
      return;
    }
    const auto avcc = Avcc(data);
    auto* buffer = gst_buffer_new_allocate(nullptr, avcc.size(), nullptr);
    gst_buffer_fill(buffer, 0, avcc.data(), avcc.size());
    GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(packet_pts, GST_SECOND, config.fps);
    GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(GST_SECOND, 1, config.fps);
    gst_app_src_push_buffer(appsrc, buffer);
  }
  void Receive() {
    std::vector<std::uint8_t> datagram(65536);
    while (!stopping) {
      const auto count = recv(socket, datagram.data(), datagram.size(), 0);
      if (count < static_cast<ssize_t>(kHeaderSize)) continue;
      if (Get32(datagram.data()) != kMagic) continue;
      const auto current = Get32(datagram.data() + 4);
      const auto index = Get16(datagram.data() + 8);
      const auto total = Get16(datagram.data() + 10);
      if (!total || index >= total) continue;
      if (current != frame_id || total != expected_fragments) {
        frame_id = current;
        expected_fragments = total;
        pts = Get64(datagram.data() + 12);
        fragments.assign(total, {});
      }
      fragments[index].assign(datagram.begin() + kHeaderSize, datagram.begin() + count);
      bool complete = true;
      for (const auto& fragment : fragments)
        if (fragment.empty()) complete = false;
      if (!complete) continue;
      std::vector<std::uint8_t> packet;
      for (const auto& fragment : fragments)
        packet.insert(packet.end(), fragment.begin(), fragment.end());
      Push(std::move(packet), pts);
      expected_fragments = 0;
    }
  }
};

RtspServer::RtspServer() : impl_(std::make_unique<Impl>()) {
}
RtspServer::~RtspServer() = default;
bool RtspServer::Start(const RtspServerConfig& config, std::string* error) {
  if (impl_->active) return false;
  if (config.port == 0 || config.udp_port == 0 || config.path.empty() || config.fps == 0) {
    if (error) *error = "invalid RTSP server configuration";
    return false;
  }
  gst_init(nullptr, nullptr);
  impl_->socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (impl_->socket < 0) {
    if (error) *error = "create RTSP UDP input socket failed";
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(config.udp_port);
  if (bind(impl_->socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    if (error) *error = "bind RTSP UDP input port failed";
    close(impl_->socket);
    impl_->socket = -1;
    return false;
  }
  impl_->config = config;
  impl_->context = g_main_context_new();
  impl_->loop = g_main_loop_new(impl_->context, FALSE);
  impl_->server = gst_rtsp_server_new();
  gst_rtsp_server_set_service(impl_->server, std::to_string(config.port).c_str());
  gst_rtsp_server_set_address(impl_->server, config.address.c_str());
  auto* mounts = gst_rtsp_server_get_mount_points(impl_->server);
  auto* factory = gst_rtsp_media_factory_new();
  const auto launch =
      "( appsrc name=src is-live=true do-timestamp=true format=time block=false caps=video/"
      "x-h264,stream-format=(string)avc,alignment=(string)au,framerate=(fraction)" +
      std::to_string(config.fps) + "/1 ! rtph264pay name=pay0 pt=96 config-interval=1 )";
  gst_rtsp_media_factory_set_launch(factory, launch.c_str());
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
  impl_->main_thread = std::thread([this] {
    g_main_loop_run(impl_->loop);
  });
  impl_->receiver_thread = std::thread([this] {
    impl_->Receive();
  });
  return true;
}
void RtspServer::Stop() {
  impl_->Stop();
}
bool RtspServer::running() const {
  return impl_->active;
}
}  // namespace cockpit::transfer
