#include "agent/llm/llama_server_local_llm_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

class FakeHttpServer {
 public:
  FakeHttpServer(std::string response_body, int delay_ms = 0, bool chunked = false)
      : response_body_(std::move(response_body)), delay_ms_(delay_ms), chunked_(chunked) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
      throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
      throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
    }
    socklen_t length = sizeof(address);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
      throw std::runtime_error(std::string("getsockname failed: ") + std::strerror(errno));
    }
    port_ = ntohs(address.sin_port);
    if (listen(listen_fd_, 1) < 0) {
      throw std::runtime_error(std::string("listen failed: ") + std::strerror(errno));
    }
    worker_ = std::thread([this] {
      Run();
    });
  }

  ~FakeHttpServer() {
    stop_.store(true);
    if (listen_fd_ >= 0) {
      ::shutdown(listen_fd_, SHUT_RDWR);
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  int port() const {
    return port_;
  }

  std::string request() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return request_;
  }

  bool WaitForRequest(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return request_changed_.wait_for(lock, timeout, [this] {
      return !request_.empty();
    });
  }

 private:
  static std::size_t ContentLength(const std::string& request) {
    const std::string key = "Content-Length:";
    const std::size_t pos = request.find(key);
    if (pos == std::string::npos) {
      return 0;
    }
    const std::size_t line_end = request.find("\r\n", pos);
    return static_cast<std::size_t>(std::stoul(request.substr(pos + key.size(), line_end - pos)));
  }

  void Run() {
    const int client_fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (client_fd < 0) {
      return;
    }
    std::string request;
    char buffer[4096];
    while (true) {
      const ssize_t bytes = ::recv(client_fd, buffer, sizeof(buffer), 0);
      if (bytes <= 0) {
        break;
      }
      request.append(buffer, buffer + bytes);
      if (request.find("\r\n\r\n") != std::string::npos) {
        const std::size_t header_end = request.find("\r\n\r\n");
        const std::size_t body_length = ContentLength(request);
        if (request.size() >= header_end + 4U + body_length) {
          break;
        }
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      request_ = request;
    }
    request_changed_.notify_all();

    if (delay_ms_ > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    }
    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n";
    if (chunked_) {
      std::ostringstream chunk_size;
      chunk_size << std::hex << response_body_.size();
      response += "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n" + chunk_size.str() +
                  "\r\n" + response_body_ + "\r\n0\r\n\r\n";
    } else {
      response += "Content-Length: " + std::to_string(response_body_.size()) +
                  "\r\nConnection: close\r\n\r\n" + response_body_;
    }
    (void)::send(client_fd, response.data(), response.size(), MSG_NOSIGNAL);
    ::shutdown(client_fd, SHUT_RDWR);
    ::close(client_fd);
  }

  std::string response_body_;
  int delay_ms_ = 0;
  bool chunked_ = false;
  int listen_fd_ = -1;
  int port_ = 0;
  std::atomic_bool stop_{false};
  std::thread worker_;
  mutable std::mutex mutex_;
  mutable std::condition_variable request_changed_;
  std::string request_;
};

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  FakeHttpServer server(
      "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"content\":\"local \"}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"content\":\"llama response\"}}]}\n\n"
      "data: [DONE]\n\n",
      0, true);
  cockpit::voice::LocalLlmConfig config;
  config.provider = "llama-server";
  config.host = "127.0.0.1";
  config.port = static_cast<std::uint16_t>(server.port());
  config.model = "Qwen3.5-2B";
  cockpit::voice::LlamaServerLocalLlmClient client(config);

  cockpit::voice::SpeechTranscript transcript;
  transcript.text = "tell me a joke";
  const auto result = client.GenerateResponse(
      transcript, std::chrono::steady_clock::now() + std::chrono::seconds(1));
  if (!Check(result.success, "llama-server client did not succeed") ||
      !Check(result.response_text == "local llama response",
             "llama-server client did not parse response content") ||
      !Check(result.provider == "llama-server", "llama-server provider name mismatch") ||
      !Check(result.total_latency >= result.first_content_latency,
             "llama-server timing order was invalid")) {
    return 1;
  }

  const std::string request = server.request();
  if (!Check(request.find("POST /v1/chat/completions HTTP/1.1") != std::string::npos,
             "llama-server client used the wrong endpoint") ||
      !Check(request.find("\"model\":\"Qwen3.5-2B\"") != std::string::npos,
             "llama-server client did not send model name") ||
      !Check(request.find("\"stream\":true") != std::string::npos,
             "llama-server client did not request token streaming") ||
      !Check(
          request.find("\"chat_template_kwargs\":{\"enable_thinking\":false}") != std::string::npos,
          "llama-server client did not disable thinking") ||
      !Check(request.find("\"role\":\"user\",\"content\":\"tell me a joke\"") != std::string::npos,
             "llama-server client did not send transcript text")) {
    return 1;
  }

  FakeHttpServer unicode_server(
      "data: {\"choices\":[{\"delta\":{\"content\":\"\\u4f60\"}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"content\":\"\\u597d\"}}]}\n\n"
      "data: [DONE]\n\n");
  config.port = static_cast<std::uint16_t>(unicode_server.port());
  cockpit::voice::LlamaServerLocalLlmClient unicode_client(config);
  const auto unicode_result = unicode_client.GenerateResponse(
      transcript, std::chrono::steady_clock::now() + std::chrono::seconds(1));
  if (!Check(unicode_result.success, "escaped Unicode llama-server response did not succeed") ||
      !Check(unicode_result.response_text == "你好",
             "escaped Unicode llama-server response was not decoded")) {
    return 1;
  }

  FakeHttpServer reasoning_only_server(
      "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"hidden reasoning\"},"
      "\"finish_reason\":null}]}\n\n"
      "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n\n"
      "data: [DONE]\n\n");
  config.port = static_cast<std::uint16_t>(reasoning_only_server.port());
  cockpit::voice::LlamaServerLocalLlmClient reasoning_only_client(config);
  const auto reasoning_only_result = reasoning_only_client.GenerateResponse(
      transcript, std::chrono::steady_clock::now() + std::chrono::seconds(1));
  if (!Check(!reasoning_only_result.success, "reasoning-only llama-server response succeeded") ||
      !Check(reasoning_only_result.response_text.empty(),
             "reasoning-only content was exposed to the user") ||
      !Check(reasoning_only_result.error ==
                 "local LLM stream completed with reasoning but without user-visible content",
             "reasoning-only llama-server response returned the wrong error")) {
    return 1;
  }

  FakeHttpServer finish_reason_server(
      "data: {\"choices\":[{\"delta\":{\"content\":\"finished response\"},"
      "\"finish_reason\":null}]}\n\n"
      "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n");
  config.port = static_cast<std::uint16_t>(finish_reason_server.port());
  cockpit::voice::LlamaServerLocalLlmClient finish_reason_client(config);
  const auto finish_reason_result = finish_reason_client.GenerateResponse(
      transcript, std::chrono::steady_clock::now() + std::chrono::seconds(1));
  if (!Check(finish_reason_result.success, "finish_reason response did not succeed") ||
      !Check(finish_reason_result.response_text == "finished response",
             "finish_reason response content mismatch")) {
    return 1;
  }

  FakeHttpServer fallback_server(R"({"choices":[{"message":{"content":"non-stream fallback"}}]})");
  config.port = static_cast<std::uint16_t>(fallback_server.port());
  cockpit::voice::LlamaServerLocalLlmClient fallback_client(config);
  const auto fallback_result = fallback_client.GenerateResponse(
      transcript, std::chrono::steady_clock::now() + std::chrono::seconds(1));
  if (!Check(fallback_result.success, "non-stream llama-server fallback did not succeed") ||
      !Check(fallback_result.response_text == "non-stream fallback",
             "non-stream llama-server fallback response mismatch") ||
      !Check(fallback_result.first_content_latency == fallback_result.total_latency,
             "non-stream fallback timing did not use completion latency")) {
    return 1;
  }

  FakeHttpServer slow_server(
      "data: {\"choices\":[{\"delta\":{\"content\":\"too late\"}}]}\n\n"
      "data: [DONE]\n\n",
      300);
  config.port = static_cast<std::uint16_t>(slow_server.port());
  config.first_token_timeout = std::chrono::milliseconds(50);
  cockpit::voice::LlamaServerLocalLlmClient slow_client(config);
  const auto failed = slow_client.GenerateResponse(
      transcript, std::chrono::steady_clock::now() + std::chrono::seconds(2));
  if (!Check(!failed.success, "llama-server first-token deadline was not enforced") ||
      !Check(failed.error == "local LLM first-token deadline exceeded",
             "llama-server returned the wrong first-token timeout error")) {
    return 1;
  }

  FakeHttpServer cancelled_server(
      "data: {\"choices\":[{\"delta\":{\"content\":\"too late\"}}]}\n\n"
      "data: [DONE]\n\n",
      300);
  config.port = static_cast<std::uint16_t>(cancelled_server.port());
  config.first_token_timeout = std::chrono::seconds(1);
  cockpit::voice::LlamaServerLocalLlmClient cancelled_client(config);
  cockpit::voice::LocalLlmResult cancelled_result;
  std::thread request_thread([&] {
    cancelled_result = cancelled_client.GenerateResponse(
        transcript, std::chrono::steady_clock::now() + std::chrono::seconds(2));
  });
  if (!Check(cancelled_server.WaitForRequest(std::chrono::seconds(1)),
             "llama-server cancellation test did not receive a request")) {
    cancelled_client.Cancel();
    request_thread.join();
    return 1;
  }
  const auto cancel_started = std::chrono::steady_clock::now();
  cancelled_client.Cancel();
  request_thread.join();
  const auto cancel_elapsed = std::chrono::steady_clock::now() - cancel_started;
  if (!Check(!cancelled_result.success, "cancelled llama-server request succeeded") ||
      !Check(cancel_elapsed < std::chrono::milliseconds(200),
             "llama-server cancellation did not unblock the request promptly")) {
    return 1;
  }

  std::cout << "llama-server local LLM client tests passed\n";
  return 0;
}
