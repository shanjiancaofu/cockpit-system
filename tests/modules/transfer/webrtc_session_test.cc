#include <arpa/inet.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <rtc/rtc.hpp>
#include <stdexcept>
#include <thread>

#include "cockpit/transfer/webrtc/h264_track.h"
#include "cockpit/transfer/webrtc/webrtc_server.h"

#ifdef COCKPIT_TEST_VIDEO_DECODE
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "cockpit/driver/camera/encode/h264_encoder.h"
#include "cockpit/transfer/rtsp/rtsp_server.h"
#endif

using namespace cockpit::transfer;
using namespace std::chrono_literals;
namespace {
void Check(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
bool Wait(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 5000ms) {
  const auto until = std::chrono::steady_clock::now() + timeout;
  do {
    if (predicate()) return true;
    std::this_thread::sleep_for(20ms);
  } while (std::chrono::steady_clock::now() < until);
  return predicate();
}
std::string Quote(const std::string& value) {
  auto* node = json_node_new(JSON_NODE_VALUE);
  json_node_set_string(node, value.c_str());
  auto* text = json_to_string(node, FALSE);
  std::string result(text);
  g_free(text);
  json_node_free(node);
  return result;
}
std::string Member(const std::string& json, const char* name) {
  auto* parser = json_parser_new();
  GError* error = nullptr;
  const bool parsed = json_parser_load_from_data(parser, json.c_str(), json.size(), &error);
  if (!parsed) {
    g_clear_error(&error);
    g_object_unref(parser);
    throw std::runtime_error("invalid JSON response");
  }
  auto* object = json_node_get_object(json_parser_get_root(parser));
  const std::string result = json_object_get_string_member(object, name);
  g_object_unref(parser);
  return result;
}
struct Response {
  unsigned code;
  std::string body;
};
Response Http(unsigned port, const char* method, const std::string& path,
              const std::string& body = "", const char* content_type = "application/json",
              const char* origin = nullptr) {
  auto* client = soup_session_new_with_options(SOUP_SESSION_TIMEOUT, 12u, nullptr);
  const auto url = "http://127.0.0.1:" + std::to_string(port) + path;
  auto* message = soup_message_new(method, url.c_str());
  if (origin) soup_message_headers_replace(message->request_headers, "Origin", origin);
  if (!body.empty())
    soup_message_set_request(message, content_type, SOUP_MEMORY_COPY, body.data(), body.size());
  const auto code = soup_session_send_message(client, message);
  Response result{code,
                  std::string(message->response_body->data ? message->response_body->data : "",
                              message->response_body->length)};
  g_object_unref(message);
  g_object_unref(client);
  return result;
}
unsigned FreePort() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Check(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
        "test port bind failed");
  socklen_t size = sizeof(address);
  getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size);
  close(fd);
  return ntohs(address.sin_port);
}
H264Packet Packet(std::int64_t pts, bool idr) {
  H264Packet packet;
  packet.pts = packet.dts = pts;
  packet.keyframe = idr;
  if (idr) packet.data = {0, 0, 0, 1, 0x67, 0x42, 0xe0, 0x1f, 0, 0, 1, 0x68, 0xce, 0x3c, 0x80};
  packet.data.insert(packet.data.end(),
                     {0, 0, 0, 1, static_cast<std::uint8_t>(idr ? 0x65 : 0x41), 0x88, 0x80});
  return packet;
}
void NackRetransmission() {
  auto responder = std::make_shared<rtc::RtcpNackResponder>(4);
  auto rtp = rtc::make_message(13);
  (*rtp)[0] = std::byte{0x80};
  (*rtp)[1] = std::byte{96};
  (*rtp)[2] = std::byte{0x12};
  (*rtp)[3] = std::byte{0x34};
  (*rtp)[12] = std::byte{0x65};
  rtc::message_vector outgoing{rtp};
  responder->outgoing(outgoing, [](rtc::message_ptr) {
  });
  auto nack = rtc::make_message(16, rtc::Message::Control);
  (*nack)[0] = std::byte{0x81};
  (*nack)[1] = std::byte{205};
  (*nack)[3] = std::byte{3};
  (*nack)[12] = std::byte{0x12};
  (*nack)[13] = std::byte{0x34};
  rtc::message_vector incoming{nack};
  bool retransmitted = false;
  responder->incoming(incoming, [&](rtc::message_ptr packet) {
    retransmitted = *packet == *rtp;
  });
  // Test before SRTP replay protection: requesting a packet already delivered to
  // a real peer legitimately causes the retransmitted duplicate to be discarded.
  Check(retransmitted, "NACK cache did not retransmit the requested RTP packet");
}
#ifdef COCKPIT_TEST_VIDEO_DECODE
void EncodedMedia(unsigned width, unsigned height) {
  cockpit::camera::H264Encoder encoder;
  cockpit::camera::H264EncoderConfig encoding;
  encoding.width = width;
  encoding.height = height;
  encoding.gop = 5;
  std::string error;
  Check(encoder.Start(encoding, &error), error);
  std::vector<cockpit::camera::H264Packet> samples;
  for (unsigned i = 0; i < 10; ++i) {
    cockpit::camera::CpuVideoFrame frame;
    frame.format = cockpit::camera::VicOutputFormat::kI420;
    frame.width = width;
    frame.height = height;
    frame.capture_id = i;
    const auto y = width * height;
    frame.planes = {{0, width, width, height},
                    {y, width / 2, width / 2, height / 2},
                    {y + y / 4, width / 2, width / 2, height / 2}};
    frame.data.resize(y + y / 2, 128);
    for (unsigned n = 0; n < y; ++n)
      frame.data[n] = static_cast<std::uint8_t>((n / width + n % width + i * 3) % 220 + 16);
    Check(encoder.Encode(frame, &samples, &error), error);
  }
  Check(encoder.Flush(&samples, &error), error);
  encoder.Stop();
  Check(samples.size() == 10, "encoder fixture count wrong");
  H264Receiver receiver;
  const auto udp = FreePort();
  Check(receiver.Start(udp, &error), error);
  cockpit::camera::H264UdpPublisher publisher;
  cockpit::camera::H264UdpPublisherConfig publisher_config;
  publisher_config.port = udp;
  publisher_config.mtu = 1200;
  Check(publisher.Open(publisher_config, &error), error);
  std::atomic_bool stop{false};
  struct Feed {
    std::atomic_bool& stop;
    std::thread worker;
    ~Feed() {
      stop = true;
      if (worker.joinable()) worker.join();
    }
  } feed{stop, std::thread([&] {
           std::int64_t tick = 0;
           while (!stop) {
             auto packet = samples[tick % samples.size()];
             packet.pts = packet.dts = tick++;
             publisher.Publish(packet);
             std::this_thread::sleep_for(33ms);
           }
         })};
  Check(Wait([&] {
          return !receiver.Status().pps.empty();
        }),
        "UDP CRP1 input unavailable");
  RtspServer rtsp;
  RtspServerConfig rtsp_config;
  rtsp_config.address = "127.0.0.1";
  rtsp_config.port = FreePort();
  Check(rtsp.Start(rtsp_config, receiver, &error), error);
  WebRtcServer server;
  WebRtcServerConfig config;
  config.http_port = FreePort();
  Check(server.Start(config, receiver, &error), error);
  rtc::Configuration client_config;
  client_config.disableAutoNegotiation = true;
  auto pc = std::make_shared<rtc::PeerConnection>(client_config);
  rtc::Description::Video video("0", rtc::Description::Direction::RecvOnly);
  video.addH264Codec(
      102, "packetization-mode=1;profile-level-id=" + H264ProfileLevelId(receiver.Status().sps));
  auto track = pc->addTrack(video);
  auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
  depacketizer->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
  track->setMediaHandler(depacketizer);
  struct Decoder {
    AVCodecContext* context = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
    std::atomic_bool decoded{false};
    std::atomic_int decoded_width{0}, decoded_height{0};
    ~Decoder() {
      avcodec_free_context(&context);
    }
  };
  auto decoder = std::make_shared<Decoder>();
  Check(decoder->context &&
            avcodec_open2(decoder->context, avcodec_find_decoder(AV_CODEC_ID_H264), nullptr) == 0,
        "decoder startup failed");
  track->onFrame([decoder](rtc::binary bytes, rtc::FrameInfo) {
    auto* packet = av_packet_alloc();
    av_new_packet(packet, bytes.size());
    std::memcpy(packet->data, bytes.data(), bytes.size());
    if (avcodec_send_packet(decoder->context, packet) >= 0) {
      auto* frame = av_frame_alloc();
      while (avcodec_receive_frame(decoder->context, frame) == 0) {
        decoder->decoded_width = frame->width;
        decoder->decoded_height = frame->height;
        decoder->decoded = true;
      }
      av_frame_free(&frame);
    }
    av_packet_free(&packet);
  });
  pc->setLocalDescription(rtc::Description::Type::Offer);
  Check(Wait([&] {
          return pc->gatheringState() == rtc::PeerConnection::GatheringState::Complete;
        }),
        "media ICE timeout");
  const auto response =
      Http(config.http_port, "POST", "/api/v1/webrtc/sessions",
           "{\"type\":\"offer\",\"sdp\":" + Quote(std::string(*pc->localDescription())) + "}");
  Check(response.code == 201, "media session failed: " + response.body);
  pc->setRemoteDescription(rtc::Description(Member(response.body, "sdp"), "answer"));
  const auto launch = "rtspsrc location=rtsp://127.0.0.1:" + std::to_string(rtsp_config.port) +
                      "/camera protocols=tcp latency=0 ! rtph264depay ! appsink name=sink "
                      "sync=false max-buffers=2 drop=true";
  GError* gst_error = nullptr;
  auto* pipeline = gst_parse_launch(launch.c_str(), &gst_error);
  const std::string failure = gst_error ? gst_error->message : "RTSP client parse failed";
  g_clear_error(&gst_error);
  Check(pipeline, failure);
  auto* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
  const bool rtsp_ok = sample != nullptr;
  if (sample) gst_sample_unref(sample);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(sink);
  gst_object_unref(pipeline);
  Check(rtsp_ok, "parallel RTSP did not receive encoded video");
  Check(Wait([&] {
          return decoder->decoded.load();
        }),
        "WebRTC RTP received but H264 did not decode");
  Check(decoder->decoded_width == static_cast<int>(width) &&
            decoder->decoded_height == static_cast<int>(height),
        "decoded resolution wrong");
  track->resetCallbacks();
  pc->close();
  server.Stop();
  rtsp.Stop();
  // Exercise clean start/stop after a live RTSP client, including socket reuse.
  Check(rtsp.Start(rtsp_config, receiver, &error), error);
  rtsp.Stop();
  std::cout << "libx264 -> CRP1 UDP -> shared RTSP/WebRTC -> decoded " << width << "x" << height
            << " passed\n";
}
#endif
}  // namespace
int main() {
  try {
    NackRetransmission();
    H264Receiver receiver;
    WebRtcServer server;
    WebRtcServerConfig config;
    config.http_port = FreePort();
    std::string error;
    Check(server.Start(config, receiver, &error), error);
    const std::string api = "/api/v1/webrtc";
    Check(Http(config.http_port, "GET", "/").body.find("RTCPeerConnection") != std::string::npos,
          "page missing");
    Check(Http(config.http_port, "GET", api + "/status").body.find("\"video_available\":false") !=
              std::string::npos,
          "idle status wrong");
    Check(Http(config.http_port, "POST", api + "/sessions", "{}").code == 503,
          "unavailable stream accepted");
    std::atomic_bool stop{false};
    std::atomic<std::int64_t> pts{0};
    // std::thread is always joined, including assertion failures.
    struct Feed {
      std::atomic_bool& stop;
      std::thread worker;
      ~Feed() {
        stop = true;
        if (worker.joinable()) worker.join();
      }
    } feed{stop, std::thread([&] {
             while (!stop) {
               auto p = pts.fetch_add(1);
               receiver.Publish(Packet(p, p % 10 == 0), false);
               std::this_thread::sleep_for(33ms);
             }
           })};
    Check(Wait([&] {
            return receiver.Status().sps.size() >= 4;
          }),
          "stream startup failed");
    Check(Http(config.http_port, "POST", api + "/sessions", "{}").code == 400,
          "malformed JSON accepted");
    Check(Http(config.http_port, "POST", api + "/sessions", "{}", "text/plain").code == 415,
          "wrong content-type accepted");
    Check(Http(config.http_port, "POST", api + "/sessions", "{}", "application/json",
               "http://evil.invalid")
                  .code == 403,
          "cross-origin request accepted");
    Check(Http(config.http_port, "POST", api + "/sessions", std::string(65537, 'x')).code == 413,
          "oversized request accepted");
    Check(Http(config.http_port, "DELETE", api + "/sessions/missing").code == 404,
          "missing DELETE accepted");

    std::shared_ptr<rtc::PeerConnection> retained_pc;
    auto connect = [&](bool receive, bool keep = false) {
      rtc::Configuration client_config;
      client_config.disableAutoNegotiation = true;
      auto pc = std::make_shared<rtc::PeerConnection>(client_config);
      rtc::Description::Video video("0", rtc::Description::Direction::RecvOnly);
      video.addH264Codec(102,
                         "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
      auto track = pc->addTrack(video);
      pc->setLocalDescription(rtc::Description::Type::Offer);
      Check(Wait([&] {
              return pc->gatheringState() == rtc::PeerConnection::GatheringState::Complete;
            }),
            "client ICE timeout");
      const auto offer = std::string(*pc->localDescription());
      auto incompatible = offer;
      const auto profile_at = incompatible.find("profile-level-id=42e01f");
      Check(profile_at != std::string::npos, "test profile missing");
      incompatible.replace(profile_at, std::strlen("profile-level-id=42e01f"),
                           "profile-level-id=64001f");
      Check(Http(config.http_port, "POST", api + "/sessions",
                 "{\"type\":\"offer\",\"sdp\":" + Quote(incompatible) + "}")
                    .code == 400,
            "incompatible H264 profile accepted");
      const auto response = Http(config.http_port, "POST", api + "/sessions",
                                 "{\"type\":\"offer\",\"sdp\":" + Quote(offer) + "}");
      Check(response.code == 201,
            "create failed " + std::to_string(response.code) + ": " + response.body);
      const auto id = Member(response.body, "id");
      Check(Http(config.http_port, "POST", api + "/sessions", "{}").code == 409,
            "client limit not enforced");
      if (receive) {
        struct Metrics {
          std::mutex mutex;
          unsigned packets = 0, sps = 0, pps = 0, idr = 0;
          std::vector<std::uint32_t> timestamps;
          bool oversized = false;
        };
        auto metrics = std::make_shared<Metrics>();
        track->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
        track->onMessage(
            [metrics](rtc::binary data) {
              if (data.size() < 13) return;
              const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
              if ((p[1] & 127) != 102) return;
              std::lock_guard<std::mutex> lock(metrics->mutex);
              ++metrics->packets;
              metrics->oversized = metrics->oversized || data.size() > 1200;
              if ((p[12] & 31) == 7) ++metrics->sps;
              if ((p[12] & 31) == 8) ++metrics->pps;
              if ((p[12] & 31) == 5) ++metrics->idr;
              std::uint32_t ts;
              std::memcpy(&ts, p + 4, 4);
              ts = ntohl(ts);
              if (metrics->timestamps.empty() || metrics->timestamps.back() != ts)
                metrics->timestamps.push_back(ts);
            },
            nullptr);
        pc->setRemoteDescription(rtc::Description(Member(response.body, "sdp"), "answer"));
        Check(Wait([&] {
                return pc->state() == rtc::PeerConnection::State::Connected;
              }),
              "local ICE/DTLS failed");
        Check(Wait([&] {
                std::lock_guard<std::mutex> lock(metrics->mutex);
                return metrics->packets > 12 && metrics->sps && metrics->pps && metrics->idr &&
                       metrics->timestamps.size() > 4;
              }),
              "RTP SPS/PPS/IDR missing");
        {
          std::lock_guard<std::mutex> lock(metrics->mutex);
          bool spacing = false;
          for (std::size_t i = 1; i < metrics->timestamps.size(); ++i)
            if (metrics->timestamps[i] - metrics->timestamps[i - 1] == 3000) spacing = true;
          Check(spacing, "90 kHz frame timestamp mapping failed");
        }
        {
          std::lock_guard<std::mutex> lock(metrics->mutex);
          Check(!metrics->oversized, "RTP exceeds configured MTU");
        }
        Check(track->requestKeyframe(), "RTCP PLI failed");
        track->resetCallbacks();
        if (keep)
          retained_pc = pc;
        else {
          const auto deletion = Http(config.http_port, "DELETE", api + "/sessions/" + id);
          Check(deletion.code == 204, "session deletion failed");
          pc->close();
        }
      } else {
        // Camera restart must destroy an old peer even if the browser never applied the answer.
        receiver.Publish(Packet(0, true), false);
        Check(Wait([&] {
                return Http(config.http_port, "GET", api + "/status").body.find(id) ==
                       std::string::npos;
              }),
              "generation change left old peer alive");

        pc->close();
      }
    };
    std::cout << "first peer\n" << std::flush;
    connect(true);
    std::cout << "reconnect\n" << std::flush;
    connect(true);  // reconnect, including a fresh RTP timestamp origin
    connect(false);
    Check(Wait([&] {
            return receiver.Status().sps.size() >= 4;
          }),
          "restart codec not restored");
    connect(true);
    connect(true, true);
    stop = true;
    feed.worker.join();
    Check(Wait([&] {
            return Http(config.http_port, "GET", api + "/status").body.find("\"sessions\":[]") !=
                   std::string::npos;
          }),
          "stopped Camera did not clear its connected peer");
    retained_pc->close();
    server.Stop();
    Check(server.Start(config, receiver, &error), "server restart bind failed: " + error);
    server.Stop();
#ifdef COCKPIT_TEST_VIDEO_DECODE
    EncodedMedia(1280, 720);
    EncodedMedia(1920, 1080);
#endif
    std::cout << "HTTP, host ICE/DTLS, H264 RTP, timestamps, PLI, reconnect and generation cleanup "
                 "passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
