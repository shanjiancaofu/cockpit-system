#include "cockpit/transfer/webrtc/webrtc_server.h"

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <chrono>
#include <cstring>
#include <future>
#include <map>
#include <rtc/rtc.hpp>
#include <stdexcept>
#include <thread>
#include <utility>

#include "webrtc_web_page.h"

#include "cockpit/transfer/webrtc/h264_track.h"

namespace cockpit::transfer {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kMaxBody = 64 * 1024;
constexpr char kSessions[] = "/api/v1/webrtc/sessions";
std::string JsonString(const std::string& value) {
  JsonNode* node = json_node_new(JSON_NODE_VALUE);
  json_node_set_string(node, value.c_str());
  char* text = json_to_string(node, FALSE);
  std::string result(text);
  g_free(text);
  json_node_free(node);
  return result;
}
void Reply(SoupMessage* message, unsigned code, const std::string& body,
           const char* type = "application/json") {
  soup_message_set_status(message, code);
  soup_message_headers_replace(message->response_headers, "Cache-Control", "no-store");
  soup_message_headers_replace(message->response_headers, "X-Content-Type-Options", "nosniff");
  soup_message_set_response(message, type, SOUP_MEMORY_COPY, body.data(), body.size());
}
void Error(SoupMessage* message, unsigned code, const std::string& error) {
  Reply(message, code, "{\"error\":" + JsonString(error) + "}");
}
std::string Offer(SoupMessage* message) {
  auto* parser = json_parser_new();
  GError* error = nullptr;
  const auto* body = message->request_body;
  const bool parsed = body->length > 0 && body->length <= static_cast<goffset>(kMaxBody) &&
                      json_parser_load_from_data(parser, body->data, body->length, &error);
  auto* root = parsed ? json_parser_get_root(parser) : nullptr;
  std::string result;
  if (root && JSON_NODE_HOLDS_OBJECT(root)) {
    auto* object = json_node_get_object(root);
    auto* type = json_object_get_member(object, "type");
    auto* sdp = json_object_get_member(object, "sdp");
    if (type && sdp && json_node_get_value_type(type) == G_TYPE_STRING &&
        json_node_get_value_type(sdp) == G_TYPE_STRING &&
        std::strcmp(json_node_get_string(type), "offer") == 0)
      result = json_node_get_string(sdp);
  }
  g_clear_error(&error);
  g_object_unref(parser);
  if (result.empty() || result.find("a=candidate:") == std::string::npos ||
      result.find("a=ice-ufrag:") == std::string::npos ||
      result.find("a=ice-pwd:") == std::string::npos)
    throw std::invalid_argument(
        "expected JSON {type: offer, sdp} with fully gathered ICE candidates");
  return result;
}
// Reject large declared bodies before accumulation, and bound chunked bodies.
void RequestStarted(SoupServer*, SoupMessage* message, SoupClientContext*, gpointer) {
  g_signal_connect(message, "got-headers", G_CALLBACK(+[](SoupMessage* msg, gpointer) {
                     if (soup_message_headers_get_content_length(msg->request_headers) >
                         static_cast<goffset>(kMaxBody)) {
                       soup_message_body_set_accumulate(msg->request_body, FALSE);
                       Error(msg, SOUP_STATUS_REQUEST_ENTITY_TOO_LARGE,
                             "request body exceeds 64 KiB");
                     }
                   }),
                   nullptr);
  g_signal_connect(
      message, "got-chunk", G_CALLBACK(+[](SoupMessage* msg, SoupBuffer* chunk, gpointer) {
        const auto previous = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(msg), "body-bytes"));
        const auto bytes = previous + chunk->length;
        g_object_set_data(G_OBJECT(msg), "body-bytes", GSIZE_TO_POINTER(bytes));
        if (bytes > kMaxBody) {
          soup_message_body_set_accumulate(msg->request_body, FALSE);
          soup_message_body_truncate(msg->request_body);
          Error(msg, SOUP_STATUS_REQUEST_ENTITY_TOO_LARGE, "request body exceeds 64 KiB");
        }
      }),
      nullptr);
}
std::string StreamState(const VideoStatus& status) {
  if (status.generation == 0) return "no_input";
  if (status.input_age_ms < 0) return "stopped";
  if (!status.available) return "stalled";
  if (status.sps.size() < 4 || status.pps.empty()) return "waiting_parameter_sets";
  if (!status.has_idr) return "waiting_idr";
  return "ready";
}
std::string State(rtc::PeerConnection::State state) {
  switch (state) {
    case rtc::PeerConnection::State::New:
      return "new";
    case rtc::PeerConnection::State::Connecting:
      return "connecting";
    case rtc::PeerConnection::State::Connected:
      return "connected";
    case rtc::PeerConnection::State::Disconnected:
      return "disconnected";
    case rtc::PeerConnection::State::Failed:
      return "failed";
    case rtc::PeerConnection::State::Closed:
      return "closed";
  }
  return "unknown";
}
}  // namespace

struct WebRtcServer::Impl {
  struct Peer {
    std::string id;
    std::uint64_t generation = 0;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<H264Track> track;
    std::shared_ptr<VideoSubscription> subscription;
    SoupMessage* pending = nullptr;
    Clock::time_point created = Clock::now(), disconnected{};
    ~Peer() {
      if (subscription) subscription->Stop();
      if (pc) {
        pc->resetCallbacks();
        pc->close();
      }
      if (pending) g_object_unref(pending);
    }
  };
  WebRtcServerConfig config;
  std::function<void()> request_idr;
  H264Receiver* receiver = nullptr;
  GMainContext* context = nullptr;
  GMainLoop* loop = nullptr;
  SoupServer* http = nullptr;
  GSource* timer = nullptr;
  std::thread worker;
  // Sessions and SoupMessage objects are accessed only on the HTTP context.
  std::map<std::string, std::unique_ptr<Peer>> peers;

  void Finish(Peer& peer, unsigned code, const std::string& body) {
    if (!peer.pending) return;
    Reply(peer.pending, code, body);
    soup_server_unpause_message(http, peer.pending);
    g_object_unref(std::exchange(peer.pending, nullptr));
  }
  void Prune() {
    const auto status = receiver->Status();
    const auto now = Clock::now();
    for (auto it = peers.begin(); it != peers.end();) {
      auto& peer = *it->second;
      const auto state = peer.pc->state();
      if (state == rtc::PeerConnection::State::Disconnected) {
        if (peer.disconnected == Clock::time_point{}) peer.disconnected = now;
      } else
        peer.disconnected = {};
      const bool expired = (state != rtc::PeerConnection::State::Connected &&
                            now - peer.created > std::chrono::seconds(15)) ||
                           (peer.disconnected != Clock::time_point{} &&
                            now - peer.disconnected > std::chrono::seconds(3));
      if (!status.available || peer.generation != status.generation || expired ||
          state == rtc::PeerConnection::State::Closed ||
          state == rtc::PeerConnection::State::Failed) {
        Finish(peer, SOUP_STATUS_GONE, "{\"error\":\"video generation or peer connection ended\"}");
        it = peers.erase(it);
        continue;
      }
      if (peer.pending) {
        if (peer.pc->gatheringState() == rtc::PeerConnection::GatheringState::Complete) {
          const auto description = peer.pc->localDescription();
          if (description) {
            Finish(peer, SOUP_STATUS_CREATED,
                   "{\"id\":" + JsonString(peer.id) + ",\"type\":\"answer\",\"sdp\":" +
                       JsonString(std::string(*description)) + "}");
          }
        } else if (now - peer.created > std::chrono::seconds(10)) {
          Finish(peer, SOUP_STATUS_GATEWAY_TIMEOUT, "{\"error\":\"ICE gathering timeout\"}");
          it = peers.erase(it);
          continue;
        }
      }
      ++it;
    }
  }
  void Handle(SoupMessage* message, const std::string& path) {
    if (message->status_code == SOUP_STATUS_REQUEST_ENTITY_TOO_LARGE) return;
    // The page and API share one origin; no permissive cross-origin signaling.
    if (const char* origin = soup_message_headers_get_one(message->request_headers, "Origin")) {
      auto* uri = soup_uri_new(origin);
      const auto* request = soup_message_get_uri(message);
      const bool same = uri && uri->host && request->host &&
                        g_ascii_strcasecmp(uri->host, request->host) == 0 &&
                        uri->port == request->port && g_strcmp0(uri->scheme, request->scheme) == 0;
      if (uri) soup_uri_free(uri);
      if (!same) {
        Error(message, SOUP_STATUS_FORBIDDEN, "signaling requires the same origin");
        return;
      }
    }
    const std::string method = message->method;
    Prune();
    if (path == "/" && method == "GET") {
      Reply(message, SOUP_STATUS_OK, kWebRtcPage, "text/html; charset=utf-8");
    } else if (path == "/api/v1/webrtc/status" && method == "GET") {
      const auto status = receiver->Status();
      std::string sessions = "[";
      for (const auto& item : peers) {
        const auto& peer = *item.second;
        if (sessions.size() > 1) sessions += ',';
        sessions += "{\"id\":" + JsonString(peer.id) +
                    ",\"generation\":" + std::to_string(peer.generation) +
                    ",\"state\":" + JsonString(State(peer.pc->state())) +
                    ",\"waiting_keyframe\":" + (peer.track->waiting_keyframe() ? "true" : "false") +
                    "}";
      }
      sessions += ']';
      Reply(message, SOUP_STATUS_OK,
            "{\"enabled\":true,\"listening\":true,\"video_available\":" +
                std::string(status.available && status.has_idr && status.sps.size() >= 4 &&
                                    !status.pps.empty()
                                ? "true"
                                : "false") +
                ",\"stream_state\":" + JsonString(StreamState(status)) +
                ",\"input_age_ms\":" + std::to_string(status.input_age_ms) +
                ",\"idr_received\":" + (status.has_idr ? "true" : "false") +
                ",\"generation\":" + std::to_string(status.generation) +
                ",\"profile_level_id\":" + JsonString(H264ProfileLevelId(status.sps)) +
                ",\"max_clients\":" + std::to_string(config.max_clients) +
                ",\"sessions\":" + sessions + "}");
    } else if (path == kSessions && method == "POST") {
      const char* content_type =
          soup_message_headers_get_content_type(message->request_headers, nullptr);
      if (!content_type || g_ascii_strcasecmp(content_type, "application/json")) {
        Error(message, SOUP_STATUS_UNSUPPORTED_MEDIA_TYPE, "use application/json");
        return;
      }
      const auto status = receiver->Status();
      if (!status.available || !status.has_idr || status.sps.size() < 4 || status.pps.empty()) {
        if (request_idr) request_idr();
        Error(message, SOUP_STATUS_SERVICE_UNAVAILABLE, "H264 stream/SPS/PPS not ready");
        return;
      }
      if (peers.size() >= config.max_clients) {
        Error(message, SOUP_STATUS_CONFLICT, "WebRTC client limit reached");
        return;
      }
      rtc::Description offer(Offer(message), "offer");
      auto video = SelectH264Video(offer, status.sps);
      auto peer = std::make_unique<Peer>();
      gchar* uuid = g_uuid_string_random();
      peer->id = uuid;
      g_free(uuid);
      peer->generation = status.generation;
      rtc::Configuration rtc_config;
      rtc_config.disableAutoNegotiation = true;
      rtc_config.forceMediaTransport = true;
      rtc_config.portRangeBegin = config.udp_port_begin;
      rtc_config.portRangeEnd = config.udp_port_end;
      rtc_config.mtu = 1200;
      // No STUN/TURN: gather only host candidates on the trusted LAN.
      peer->pc = std::make_shared<rtc::PeerConnection>(rtc_config);
      peer->track = std::make_shared<H264Track>(peer->pc, std::move(video), status.generation,
                                                config.fps, request_idr);
      peer->pc->setRemoteDescription(std::move(offer));
      peer->pc->setLocalDescription(rtc::Description::Type::Answer);
      peer->subscription = receiver->Subscribe(
          [track = peer->track](VideoFramePtr frame) {
            track->Send(std::move(frame));
          },
          request_idr);
      peer->pending = SOUP_MESSAGE(g_object_ref(message));
      soup_server_pause_message(http, message);
      peers.emplace(peer->id, std::move(peer));
    } else if (path.rfind(std::string(kSessions) + '/', 0) == 0 && method == "DELETE") {
      const auto id = path.substr(std::strlen(kSessions) + 1);
      auto peer = peers.find(id);
      if (peer == peers.end()) {
        Error(message, SOUP_STATUS_NOT_FOUND, "session not found");
        return;
      }
      Finish(*peer->second, SOUP_STATUS_GONE, "{\"error\":\"session deleted\"}");
      peers.erase(peer);
      Reply(message, SOUP_STATUS_NO_CONTENT, "");
    } else
      Error(message, SOUP_STATUS_NOT_FOUND, "route not found");
  }
  void Run(std::promise<std::string> ready) {
    g_main_context_push_thread_default(context);
    http = soup_server_new(SOUP_SERVER_SERVER_HEADER, "cockpit-transfer", nullptr);
    g_signal_connect(http, "request-started", G_CALLBACK(RequestStarted), nullptr);
    soup_server_add_handler(
        http, nullptr,
        +[](SoupServer*, SoupMessage* message, const char* path, GHashTable*, SoupClientContext*,
            gpointer data) {
          try {
            static_cast<Impl*>(data)->Handle(message, path);
          } catch (const std::invalid_argument& e) {
            Error(message, SOUP_STATUS_BAD_REQUEST, e.what());
          } catch (const std::exception& e) {
            Error(message, SOUP_STATUS_BAD_REQUEST, e.what());
          }
        },
        this, nullptr);
    auto* address = g_inet_address_new_from_string(config.address.c_str());
    auto* socket_address = address ? g_inet_socket_address_new(address, config.http_port) : nullptr;
    GError* error = nullptr;
    const bool listening = socket_address && soup_server_listen(http, socket_address,
                                                                SoupServerListenOptions(0), &error);
    if (socket_address) g_object_unref(socket_address);
    if (address) g_object_unref(address);
    if (!listening) {
      ready.set_value(error ? error->message : "invalid WebRTC HTTP bind address");
      g_clear_error(&error);
    } else {
      timer = g_timeout_source_new(50);
      g_source_set_callback(
          timer,
          +[](gpointer data) -> gboolean {
            static_cast<Impl*>(data)->Prune();
            return G_SOURCE_CONTINUE;
          },
          this, nullptr);
      g_source_attach(timer, context);
      // Resolve startup only after the loop starts; Stop cannot quit before Run.
      auto* startup = g_idle_source_new();
      g_source_set_callback(
          startup,
          +[](gpointer data) -> gboolean {
            static_cast<std::promise<std::string>*>(data)->set_value("");
            return G_SOURCE_REMOVE;
          },
          &ready, nullptr);
      g_source_attach(startup, context);
      g_source_unref(startup);
      g_main_loop_run(loop);
    }
    for (auto& item : peers)
      Finish(*item.second, SOUP_STATUS_SERVICE_UNAVAILABLE, "{\"error\":\"server stopping\"}");
    peers.clear();
    if (timer) {
      g_source_destroy(timer);
      g_source_unref(timer);
      timer = nullptr;
    }
    soup_server_disconnect(http);
    g_object_unref(http);
    http = nullptr;
    g_main_context_pop_thread_default(context);
  }
};
WebRtcServer::WebRtcServer() : impl_(std::make_unique<Impl>()) {
}
WebRtcServer::~WebRtcServer() {
  Stop();
}
bool WebRtcServer::Start(const WebRtcServerConfig& config, H264Receiver& receiver,
                         std::string* error, std::function<void()> request_idr) {
  if (impl_->worker.joinable() || !config.http_port || !config.udp_port_begin ||
      config.udp_port_end < config.udp_port_begin || !config.fps || !config.max_clients ||
      config.max_clients > 4) {
    if (error) *error = "invalid WebRTC configuration or server already started";
    return false;
  }
  impl_->config = config;
  impl_->request_idr = std::move(request_idr);
  impl_->receiver = &receiver;
  impl_->context = g_main_context_new();
  impl_->loop = g_main_loop_new(impl_->context, FALSE);
  std::promise<std::string> ready;
  auto result = ready.get_future();
  impl_->worker = std::thread([this, ready = std::move(ready)]() mutable {
    impl_->Run(std::move(ready));
  });
  const auto failure = result.get();
  if (!failure.empty()) {
    if (error) *error = failure;
    Stop();
    return false;
  }
  return true;
}
void WebRtcServer::Stop() {
  if (impl_->worker.joinable()) {
    g_main_context_invoke(
        impl_->context,
        +[](gpointer data) -> gboolean {
          g_main_loop_quit(static_cast<Impl*>(data)->loop);
          return G_SOURCE_REMOVE;
        },
        impl_.get());
    impl_->worker.join();
  }
  if (impl_->loop) {
    g_main_loop_unref(impl_->loop);
    impl_->loop = nullptr;
  }
  if (impl_->context) {
    g_main_context_unref(impl_->context);
    impl_->context = nullptr;
  }
}
}  // namespace cockpit::transfer
