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
#include <optional>
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
       << "\"stream\":true,"
       << "\"chat_template_kwargs\":{\"enable_thinking\":false},"
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

std::optional<std::size_t> FindJsonFieldValue(const std::string& body, const std::string& field) {
  for (std::size_t start = 0; start < body.size(); ++start) {
    if (body[start] != '"') {
      continue;
    }
    std::size_t end = start + 1U;
    bool escaped = false;
    for (; end < body.size(); ++end) {
      const char character = body[end];
      if (!escaped && character == '"') {
        break;
      }
      if (!escaped && character == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
    }
    if (end == body.size()) {
      return std::nullopt;
    }
    std::size_t value = end + 1U;
    while (value < body.size() && std::isspace(static_cast<unsigned char>(body[value]))) {
      ++value;
    }
    if (body.compare(start + 1U, end - start - 1U, field) == 0 && value < body.size() &&
        body[value] == ':') {
      return value + 1U;
    }
    start = end;
  }
  return std::nullopt;
}

std::string ExtractStringField(const std::string& body, const std::string& field, bool* found,
                               std::string* error) {
  const std::optional<std::size_t> value_start = FindJsonFieldValue(body, field);
  if (!value_start.has_value()) {
    return {};
  }
  *found = true;
  std::size_t start = *value_start;
  while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) {
    ++start;
  }
  if (body.compare(start, 4U, "null") == 0) {
    return {};
  }
  if (start >= body.size() || body[start++] != '"') {
    if (error != nullptr) {
      *error = "llama-server response " + field + " was not a string";
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
    *error = "llama-server response " + field + " was not terminated";
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

class HttpBodyDecoder {
 public:
  explicit HttpBodyDecoder(bool chunked) : chunked_(chunked) {
  }

  bool Feed(std::string_view input, std::string* decoded, std::string* error) {
    if (!chunked_) {
      decoded->append(input);
      return true;
    }
    pending_.append(input);
    while (!complete_) {
      if (!chunk_size_.has_value()) {
        const std::size_t line_end = pending_.find("\r\n");
        if (line_end == std::string::npos) {
          return true;
        }
        std::string size_text = pending_.substr(0, line_end);
        const std::size_t extension = size_text.find(';');
        if (extension != std::string::npos) {
          size_text.resize(extension);
        }
        try {
          std::size_t consumed = 0;
          chunk_size_ = std::stoull(size_text, &consumed, 16);
          if (consumed != size_text.size()) {
            throw std::invalid_argument("trailing chunk size data");
          }
        } catch (const std::exception&) {
          if (error != nullptr) {
            *error = "llama-server response had an invalid HTTP chunk size";
          }
          return false;
        }
        pending_.erase(0, line_end + 2U);
        if (*chunk_size_ == 0U) {
          complete_ = true;
          return true;
        }
      }
      if (pending_.size() < *chunk_size_ + 2U) {
        return true;
      }
      if (pending_.compare(*chunk_size_, 2U, "\r\n") != 0) {
        if (error != nullptr) {
          *error = "llama-server response had an invalid HTTP chunk terminator";
        }
        return false;
      }
      decoded->append(pending_, 0, *chunk_size_);
      pending_.erase(0, *chunk_size_ + 2U);
      chunk_size_.reset();
    }
    return true;
  }

 private:
  const bool chunked_;
  std::string pending_;
  std::optional<std::size_t> chunk_size_;
  bool complete_ = false;
};

bool UsesChunkedEncoding(std::string headers) {
  std::transform(headers.begin(), headers.end(), headers.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return headers.find("\r\ntransfer-encoding: chunked") != std::string::npos;
}

bool ConsumeSseLines(std::string* pending, std::string* output, bool* received_content,
                     bool* received_reasoning, bool* finished, bool* done, std::string* error) {
  while (true) {
    const std::size_t line_end = pending->find('\n');
    if (line_end == std::string::npos) {
      return true;
    }
    std::string line = pending->substr(0, line_end);
    pending->erase(0, line_end + 1U);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("data:", 0) != 0) {
      continue;
    }
    std::string payload = line.substr(5U);
    while (!payload.empty() && payload.front() == ' ') {
      payload.erase(payload.begin());
    }
    if (payload == "[DONE]") {
      *done = true;
      return true;
    }
    std::string parse_error;
    if (!cockpit::json::IsValidValue(payload, &parse_error)) {
      if (error != nullptr) {
        *error = "invalid streamed local LLM JSON response: " + parse_error;
      }
      return false;
    }
    bool content_found = false;
    const std::string content =
        ExtractStringField(payload, "content", &content_found, &parse_error);
    if (!parse_error.empty()) {
      if (error != nullptr) {
        *error = parse_error;
      }
      return false;
    }
    if (!content.empty()) {
      output->append(content);
      *received_content = true;
    }

    bool reasoning_found = false;
    const std::string reasoning =
        ExtractStringField(payload, "reasoning_content", &reasoning_found, &parse_error);
    if (!parse_error.empty()) {
      if (error != nullptr) {
        *error = parse_error;
      }
      return false;
    }
    if (reasoning_found && !reasoning.empty()) {
      *received_reasoning = true;
    }

    bool finish_reason_found = false;
    const std::string finish_reason =
        ExtractStringField(payload, "finish_reason", &finish_reason_found, &parse_error);
    if (!parse_error.empty()) {
      if (error != nullptr) {
        *error = parse_error;
      }
      return false;
    }
    if (finish_reason_found && !finish_reason.empty()) {
      *finished = true;
    }
  }
}

}  // namespace

LlamaServerLocalLlmClient::LlamaServerLocalLlmClient(LocalLlmConfig config)
    : config_(std::move(config)) {
}

LocalLlmResult LlamaServerLocalLlmClient::GenerateResponse(
    const SpeechTranscript& transcript, std::chrono::steady_clock::time_point deadline) {
  const std::uint64_t request_generation = cancel_generation_.load();
  const auto request_started = std::chrono::steady_clock::now();
  if (request_started >= deadline) {
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

  const auto first_token_deadline =
      std::min(deadline, request_started + config_.first_token_timeout);
  std::string headers;
  std::string undecoded_response;
  std::string decoded_body;
  std::string sse_pending;
  std::string response_text;
  std::optional<HttpBodyDecoder> body_decoder;
  bool received_content = false;
  bool received_reasoning = false;
  bool stream_finished = false;
  bool stream_done = false;
  int status_code = 0;
  char buffer[4096];
  while (true) {
    if (request_generation != cancel_generation_.load()) {
      return {false, {}, "llama-server", "local LLM request cancelled"};
    }
    const auto now = std::chrono::steady_clock::now();
    const auto active_deadline = received_content ? deadline : first_token_deadline;
    if (now >= active_deadline) {
      return {false,
              {},
              "llama-server",
              received_content ? "local LLM deadline exceeded"
                               : "local LLM first-token deadline exceeded"};
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(active_deadline - now);
    pollfd descriptor{socket.get(), POLLIN, 0};
    const int wait_ms = static_cast<int>(
        std::max(std::chrono::milliseconds(1),
                 std::min(remaining, std::chrono::milliseconds(kRequestPollStepMs)))
            .count());
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
    const auto received_at = std::chrono::steady_clock::now();
    if ((!received_content && received_at >= first_token_deadline) || received_at >= deadline) {
      return {false,
              {},
              "llama-server",
              received_content ? "local LLM deadline exceeded"
                               : "local LLM first-token deadline exceeded"};
    }
    std::string_view received(buffer, static_cast<std::size_t>(bytes));
    if (!body_decoder.has_value()) {
      undecoded_response.append(received);
      const std::size_t header_end = undecoded_response.find("\r\n\r\n");
      if (header_end == std::string::npos) {
        continue;
      }
      headers = undecoded_response.substr(0, header_end + 2U);
      if (!ParseHttpStatus(headers, &status_code)) {
        return {false, {}, "llama-server", "local LLM response had an invalid status line"};
      }
      body_decoder.emplace(UsesChunkedEncoding(headers));
      received = std::string_view(undecoded_response).substr(header_end + 4U);
    }

    std::string decoded;
    std::string decode_error;
    if (!body_decoder->Feed(received, &decoded, &decode_error)) {
      return {false, {}, "llama-server", decode_error};
    }
    decoded_body.append(decoded);
    if (status_code >= 200 && status_code < 300) {
      sse_pending.append(decoded);
      if (!ConsumeSseLines(&sse_pending, &response_text, &received_content, &received_reasoning,
                           &stream_finished, &stream_done, &decode_error)) {
        return {false, {}, "llama-server", decode_error};
      }
      if (stream_done) {
        break;
      }
    }
  }

  if (!body_decoder.has_value()) {
    return {false, {}, "llama-server", "local LLM response missing HTTP headers"};
  }
  if (status_code < 200 || status_code >= 300) {
    return {false,
            {},
            "llama-server",
            decoded_body.empty() ? "local LLM request failed" : decoded_body};
  }
  if (stream_done || stream_finished) {
    if (response_text.empty()) {
      return {false,
              {},
              "llama-server",
              received_reasoning
                  ? "local LLM stream completed with reasoning but without user-visible content"
                  : "local LLM stream completed without content"};
    }
    return {true, response_text, "llama-server", {}};
  }

  std::string parse_error;
  if (!cockpit::json::IsValidValue(decoded_body, &parse_error)) {
    return {false, {}, "llama-server", "invalid local LLM JSON response: " + parse_error};
  }
  bool content_found = false;
  const std::string content =
      ExtractStringField(decoded_body, "content", &content_found, &parse_error);
  if (content.empty()) {
    return {false,
            {},
            "llama-server",
            parse_error.empty() ? "llama-server response did not contain message content"
                                : parse_error};
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
