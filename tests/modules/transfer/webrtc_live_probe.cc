// Opt-in acceptance client for a *running* Transfer/Native Camera. Not a CTest.
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <rtc/rtc.hpp>
extern "C" {
#include <libavcodec/avcodec.h>
}

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
std::string Quote(const std::string& value) {
  auto* node = json_node_new(JSON_NODE_VALUE);
  json_node_set_string(node, value.c_str());
  char* text = json_to_string(node, FALSE);
  std::string result(text);
  g_free(text);
  json_node_free(node);
  return result;
}
std::string Member(const std::string& value, const char* key) {
  auto* parser = json_parser_new();
  GError* error = nullptr;
  const bool parsed = json_parser_load_from_data(parser, value.data(), value.size(), &error);
  auto* node = parsed ? json_parser_get_root(parser) : nullptr;
  auto* object = node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : nullptr;
  auto* field = object ? json_object_get_member(object, key) : nullptr;
  const bool valid = field && json_node_get_value_type(field) == G_TYPE_STRING;
  std::string result = valid ? json_node_get_string(field) : "";
  g_clear_error(&error);
  g_object_unref(parser);
  if (!valid)
    throw std::runtime_error(std::string("response missing string ") + key + ": " + value);
  return result;
}
std::string Http(const std::string& base, const char* method, const std::string& path,
                 unsigned expected, const std::string& body = "") {
  auto* client = soup_session_new_with_options(SOUP_SESSION_TIMEOUT, 15u, nullptr);
  auto* message = soup_message_new(method, (base + path).c_str());
  if (!message) {
    g_object_unref(client);
    throw std::runtime_error("invalid HTTP URL");
  }
  if (!body.empty())
    soup_message_set_request(message, "application/json", SOUP_MEMORY_COPY, body.data(),
                             body.size());
  const unsigned code = soup_session_send_message(client, message);
  const std::string response(message->response_body->data ? message->response_body->data : "",
                             message->response_body->length);
  g_object_unref(message);
  g_object_unref(client);
  if (code != expected)
    throw std::runtime_error(std::string(method) + " " + path + " HTTP " + std::to_string(code) +
                             ": " + response);
  return response;
}
struct Decoder {
  AVCodecContext* context = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
  std::mutex mutex;
  std::atomic_uint frames{0}, failures{0}, width{0}, height{0};
  ~Decoder() {
    avcodec_free_context(&context);
  }
  void Feed(rtc::binary bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* packet = av_packet_alloc();
    if (!packet || av_new_packet(packet, bytes.size()) < 0) {
      av_packet_free(&packet);
      ++failures;
      return;
    }
    std::memcpy(packet->data, bytes.data(), bytes.size());
    const int result = avcodec_send_packet(context, packet);
    av_packet_free(&packet);
    if (result < 0) {
      ++failures;
      return;
    }
    auto* frame = av_frame_alloc();
    if (!frame) {
      ++failures;
      return;
    }
    while (avcodec_receive_frame(context, frame) == 0) {
      width = frame->width;
      height = frame->height;
      ++frames;
    }
    av_frame_free(&frame);
  }
};
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "webrtc_live_probe --base-url http://JETSON:8080 --width 1280 --height 720 "
                 "--seconds 10 --output evidence.json\n"
                 "Consumes an existing H264 stream; does not start Camera or validate a browser.\n";
    return 0;
  }
  std::map<std::string, std::string> args;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 >= argc) {
      std::cerr << "expected --key value pairs\n";
      return 2;
    }
    args[argv[i]] = argv[i + 1];
  }
  std::string endpoint, id, output, failure;
  std::shared_ptr<rtc::PeerConnection> pc;
  auto decoder = std::make_shared<Decoder>();
  unsigned expected_width = 0, expected_height = 0;
  double duration = 0;
  unsigned pli_requests = 0;
  try {
    endpoint = args.at("--base-url");
    output = args.at("--output");
    expected_width = std::stoul(args.at("--width"));
    expected_height = std::stoul(args.at("--height"));
    duration = args.count("--seconds") ? std::stod(args.at("--seconds")) : 10;
    if (!expected_width || !expected_height || duration < 3 || duration > 120)
      throw std::runtime_error("invalid dimensions/duration");
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    const auto status = Http(endpoint, "GET", "/api/v1/webrtc/status", 200);
    const auto profile = Member(status, "profile_level_id");
    if (profile.size() != 6) throw std::runtime_error("H264 stream profile not ready");
    if (!decoder->context ||
        avcodec_open2(decoder->context, avcodec_find_decoder(AV_CODEC_ID_H264), nullptr))
      throw std::runtime_error("H264 decoder unavailable");
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    pc = std::make_shared<rtc::PeerConnection>(config);
    rtc::Description::Video video("0", rtc::Description::Direction::RecvOnly);
    video.addH264Codec(
        102, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=" + profile);
    auto track = pc->addTrack(video);
    auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
    depacketizer->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
    track->setMediaHandler(depacketizer);
    track->onFrame([decoder](rtc::binary bytes, rtc::FrameInfo) {
      decoder->Feed(std::move(bytes));
    });
    pc->setLocalDescription(rtc::Description::Type::Offer);
    const auto ice_deadline = Clock::now() + std::chrono::seconds(10);
    while (pc->gatheringState() != rtc::PeerConnection::GatheringState::Complete) {
      if (Clock::now() > ice_deadline) throw std::runtime_error("client ICE timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto answer =
        Http(endpoint, "POST", "/api/v1/webrtc/sessions", 201,
             "{\"type\":\"offer\",\"sdp\":" + Quote(std::string(*pc->localDescription())) + "}");
    id = Member(answer, "id");
    pc->setRemoteDescription(rtc::Description(Member(answer, "sdp"), "answer"));
    const double pli_after =
        args.count("--pli-after-seconds") ? std::stod(args.at("--pli-after-seconds")) : -1;
    if (pli_after >= duration) throw std::runtime_error("PLI time must precede probe duration");
    const auto started = Clock::now();
    auto last_frame_at = started;
    unsigned previous = 0;
    while (Clock::now() - started < std::chrono::duration<double>(duration)) {
      if (pli_after >= 0 && !pli_requests &&
          std::chrono::duration<double>(Clock::now() - started).count() >= pli_after) {
        if (!track->requestKeyframe()) throw std::runtime_error("RTCP PLI request failed");
        ++pli_requests;
      }
      const auto frames = decoder->frames.load();
      if (frames != previous) {
        previous = frames;
        last_frame_at = Clock::now();
      }
      if (Clock::now() - last_frame_at > std::chrono::seconds(frames ? 3 : 10))
        throw std::runtime_error("no decoded-frame progress (ICE/codec/IDR/stall)");
      if (pc->state() == rtc::PeerConnection::State::Failed ||
          pc->state() == rtc::PeerConnection::State::Closed)
        throw std::runtime_error("peer connection ended");
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (decoder->frames < 5 || decoder->width != expected_width ||
        decoder->height != expected_height)
      throw std::runtime_error("decoded frame count or resolution mismatch");
    if (decoder->failures) throw std::runtime_error("H264 decoder reported errors");
    track->resetCallbacks();
  } catch (const std::exception& error) {
    failure = error.what();
  }
  if (!id.empty()) {
    try {
      Http(endpoint, "DELETE", "/api/v1/webrtc/sessions/" + id, 204);
    } catch (const std::exception& error) {
      if (failure.empty()) failure = error.what();
    }
  }
  if (pc) {
    pc->resetCallbacks();
    pc->close();
  }
  const std::string report = "{\"result\":" + Quote(failure.empty() ? "passed" : "failed") +
                             ",\"endpoint\":" + Quote(endpoint) + ",\"session_id\":" + Quote(id) +
                             ",\"decoded_frames\":" + std::to_string(decoder->frames.load()) +
                             ",\"width\":" + std::to_string(decoder->width.load()) +
                             ",\"height\":" + std::to_string(decoder->height.load()) +
                             ",\"pli_requests\":" + std::to_string(pli_requests) +
                             ",\"decoder_errors\":" + std::to_string(decoder->failures.load()) +
                             ",\"error\":" + Quote(failure) + ",\"browser_acceptance\":false}";
  std::cout << report << '\n';
  if (!output.empty()) {
    std::ofstream file(output);
    file << report << '\n';
    if (!file) {
      std::cerr << "cannot save evidence " << output << '\n';
      return 1;
    }
  }
  return failure.empty() ? 0 : 1;
}
