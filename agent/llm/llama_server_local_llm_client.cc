#include "agent/llm/llama_server_local_llm_client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "cockpit/core/json/json.h"

namespace cockpit {
namespace voice {
namespace {

constexpr int kRequestPollStepMs = 50;

std::string EscapeJsonString(const std::string& input) {
  return cockpit::json::EscapeString(input);
}

std::string BuildRequestBody(const LocalLlmConfig& config, const SpeechTranscript& transcript) {
  std::ostringstream body;
  body << "{"
       << "\"model\":\"" << EscapeJsonString(config.model) << "\","
       << "\"stream\":false,"
       << "\"temperature\":" << config.temperature << ',' << "\"max_tokens\":" << config.max_tokens
       << ',' << "\"messages\":["
       << "{\"role\":\"system\",\"content\":\"" << EscapeJsonString(config.system_prompt) << "\"},"
       << "{\"role\":\"user\",\"content\":\"" << EscapeJsonString(transcript.text) << "\"}"
       << "]}";
  return body.str();
}

bool ParseHttpStatus(const std::string& response, int* status_code) {
  const auto first_line_end = response.find("\r\n");
  if (first_line_end == std::string::npos) {
    return false;
  }
  std::istringstream line(response.substr(0, first_line_end));
  std::string http_version;
  line >> http_version;
  line >> *status_code;
  return !line.fail();
}

std::string DecodeJsonString(std::string_view value) {
  const auto hex_value = [](char character) -> std::uint32_t {
    if (character >= '0' && character <= '9') {
      return static_cast<std::uint32_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
      return static_cast<std::uint32_t>(character - 'a' + 10);
    }
    return static_cast<std::uint32_t>(character - 'A' + 10);
  };
  const auto decode_code_unit = [&hex_value](std::string_view input,
                                             std::size_t offset) -> std::uint32_t {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
      value = (value << 4U) | hex_value(input[offset + index]);
    }
    return value;
  };
  const auto append_utf8 = [](std::uint32_t codepoint, std::string* output) {
    if (codepoint <= 0x7FU) {
      output->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
      output->push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
      output->push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
      output->push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
      output->push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      output->push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
      output->push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
      output->push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
      output->push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      output->push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
  };

  std::string output;
  output.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    const char ch = value[i];
    if (ch != '\\') {
      output.push_back(ch);
      continue;
    }
    if (++i >= value.size()) {
      break;
    }
    const char escaped = value[i];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        output.push_back(escaped);
        break;
      case 'b':
        output.push_back('\b');
        break;
      case 'f':
        output.push_back('\f');
        break;
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      case 'u':
        if (i + 4U < value.size()) {
          std::uint32_t codepoint = decode_code_unit(value, i + 1U);
          i += 4U;
          if (codepoint >= 0xD800U && codepoint <= 0xDBFFU && i + 6U < value.size() &&
              value[i + 1U] == '\\' && value[i + 2U] == 'u') {
            const std::uint32_t low = decode_code_unit(value, i + 3U);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
              codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
              i += 6U;
            }
          }
          append_utf8(codepoint, &output);
        }
        break;
      default:
        output.push_back(escaped);
        break;
    }
  }
  return output;
}

std::string ExtractContent(const std::string& body, std::string* error) {
  const std::string key = "\"content\"";
  const std::size_t key_pos = body.find(key);
  if (key_pos == std::string::npos) {
    if (error != nullptr) {
      *error = "llama-server response did not contain message content";
    }
    return {};
  }
  std::size_t start = key_pos + key.size();
  while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) {
    ++start;
  }
  if (start >= body.size() || body[start++] != ':') {
    if (error != nullptr) {
      *error = "llama-server response content field was invalid";
    }
    return {};
  }
  while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) {
    ++start;
  }
  if (start >= body.size() || body[start++] != '"') {
    if (error != nullptr) {
      *error = "llama-server response content was not a string";
    }
    return {};
  }
  std::string raw;
  bool escaped = false;
  for (std::size_t index = start; index < body.size(); ++index) {
    const char ch = body[index];
    if (!escaped && ch == '"') {
      return DecodeJsonString(raw);
    }
    if (!escaped && ch == '\\') {
      escaped = true;
      raw.push_back(ch);
      continue;
    }
    escaped = false;
    raw.push_back(ch);
  }
  if (error != nullptr) {
    *error = "llama-server response content was not terminated";
  }
  return {};
}

class ScopedSocket {
 public:
  ~ScopedSocket() {
    Close();
  }

  int get() const {
    return fd_;
  }

  void reset(int fd = -1) {
    Close();
    fd_ = fd;
  }

  void Close() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_{-1};
};

class ActiveSocketRegistration {
 public:
  ActiveSocketRegistration(std::mutex* mutex, int* active_socket, int socket)
      : mutex_(mutex), active_socket_(active_socket), socket_(socket) {
    std::lock_guard<std::mutex> lock(*mutex_);
    *active_socket_ = socket_;
  }

  ~ActiveSocketRegistration() {
    std::lock_guard<std::mutex> lock(*mutex_);
    if (*active_socket_ == socket_) {
      *active_socket_ = -1;
    }
  }

 private:
  std::mutex* mutex_;
  int* active_socket_;
  int socket_;
};

}  // namespace

LlamaServerLocalLlmClient::LlamaServerLocalLlmClient(LocalLlmConfig config)
    : config_(std::move(config)) {
}

LocalLlmResult LlamaServerLocalLlmClient::GenerateResponse(
    const SpeechTranscript& transcript, std::chrono::steady_clock::time_point deadline) {
  const std::uint64_t request_generation = cancel_generation_.load();
  if (std::chrono::steady_clock::now() >= deadline) {
    return {false, {}, "llama-server", "local LLM deadline exceeded"};
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG;
  addrinfo* result = nullptr;
  const std::string port = std::to_string(config_.port);
  if (getaddrinfo(config_.host.c_str(), port.c_str(), &hints, &result) != 0 || result == nullptr) {
    return {false, {}, "llama-server", "failed to resolve LLM host"};
  }

  ScopedSocket socket;
  for (addrinfo* entry = result; entry != nullptr; entry = entry->ai_next) {
    socket.reset(::socket(entry->ai_family, entry->ai_socktype | SOCK_CLOEXEC, entry->ai_protocol));
    if (socket.get() < 0) {
      continue;
    }
    if (::connect(socket.get(), entry->ai_addr, entry->ai_addrlen) == 0) {
      break;
    }
    socket.Close();
  }
  freeaddrinfo(result);
  if (socket.get() < 0) {
    return {false, {}, "llama-server", "failed to connect to local LLM server"};
  }
  ActiveSocketRegistration active_socket(&active_socket_mutex_, &active_socket_, socket.get());
  if (request_generation != cancel_generation_.load()) {
    return {false, {}, "llama-server", "local LLM request cancelled"};
  }

  const std::string body = BuildRequestBody(config_, transcript);
  std::ostringstream request;
  request << "POST " << config_.path << " HTTP/1.1\r\n"
          << "Host: " << config_.host << ':' << config_.port << "\r\n"
          << "Content-Type: application/json\r\n"
          << "Connection: close\r\n"
          << "Content-Length: " << body.size() << "\r\n\r\n"
          << body;
  const std::string request_text = request.str();
  std::size_t written = 0;
  while (written < request_text.size()) {
    if (request_generation != cancel_generation_.load()) {
      return {false, {}, "llama-server", "local LLM request cancelled"};
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return {false, {}, "llama-server", "local LLM deadline exceeded"};
    }
    const ssize_t bytes = ::send(socket.get(), request_text.data() + written,
                                 request_text.size() - written, MSG_NOSIGNAL);
    if (bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {false,
              {},
              "llama-server",
              std::string("failed to send request to local LLM server: ") + std::strerror(errno)};
    }
    written += static_cast<std::size_t>(bytes);
  }

  std::string response;
  char buffer[4096];
  while (true) {
    if (request_generation != cancel_generation_.load()) {
      return {false, {}, "llama-server", "local LLM request cancelled"};
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return {false, {}, "llama-server", "local LLM deadline exceeded"};
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    pollfd descriptor{socket.get(), POLLIN, 0};
    const int wait_ms = static_cast<int>(
        std::min(remaining, std::chrono::milliseconds(kRequestPollStepMs)).count());
    const int poll_result = ::poll(&descriptor, 1, wait_ms);
    if (poll_result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {false,
              {},
              "llama-server",
              std::string("failed while waiting for local LLM response: ") + std::strerror(errno)};
    }
    if (poll_result == 0) {
      continue;
    }
    const ssize_t bytes = ::recv(socket.get(), buffer, sizeof(buffer), 0);
    if (bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {false,
              {},
              "llama-server",
              std::string("failed to read local LLM response: ") + std::strerror(errno)};
    }
    if (bytes == 0) {
      if (request_generation != cancel_generation_.load()) {
        return {false, {}, "llama-server", "local LLM request cancelled"};
      }
      break;
    }
    response.append(buffer, buffer + bytes);
  }

  const std::size_t header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return {false, {}, "llama-server", "local LLM response missing HTTP headers"};
  }
  int status_code = 0;
  if (!ParseHttpStatus(response, &status_code)) {
    return {false, {}, "llama-server", "local LLM response had an invalid status line"};
  }
  const std::string body_text = response.substr(header_end + 4U);
  if (status_code < 200 || status_code >= 300) {
    return {false, {}, "llama-server", body_text.empty() ? "local LLM request failed" : body_text};
  }

  std::string parse_error;
  if (!cockpit::json::IsValidValue(body_text, &parse_error)) {
    return {false, {}, "llama-server", "invalid local LLM JSON response: " + parse_error};
  }
  const std::string content = ExtractContent(body_text, &parse_error);
  if (content.empty()) {
    return {false, {}, "llama-server", parse_error};
  }
  return {true, content, "llama-server", {}};
}

void LlamaServerLocalLlmClient::Cancel() {
  cancel_generation_.fetch_add(1U);
  std::lock_guard<std::mutex> lock(active_socket_mutex_);
  if (active_socket_ >= 0) {
    (void)::shutdown(active_socket_, SHUT_RDWR);
  }
}

}  // namespace voice
}  // namespace cockpit
