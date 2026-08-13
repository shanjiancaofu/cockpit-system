#include "agent/audio/audio_playback_client.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

#include "agent/speech/tts/mock_speech_synthesizer.h"

namespace cockpit {
namespace voice {

AudioPlaybackClient::AudioPlaybackClient(const std::string& address)
    : AudioPlaybackClient(address, std::make_unique<MockSpeechSynthesizer>(),
                          std::chrono::seconds(5)) {
}

AudioPlaybackClient::AudioPlaybackClient(const std::string& address,
                                         std::unique_ptr<SpeechSynthesizer> synthesizer,
                                         std::chrono::milliseconds synthesis_timeout)
    : stub_([&address] {
        grpc::ChannelArguments arguments;
        arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
        return proto::audio::AudioControl::NewStub(
            grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments));
      }()),
      synthesizer_(std::move(synthesizer)),
      synthesis_timeout_(synthesis_timeout) {
  if (synthesis_timeout_ <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("speech synthesis timeout must be positive");
  }
}

bool AudioPlaybackClient::Submit(std::string text) {
  if (synthesizer_ == nullptr) {
    failed_.fetch_add(1U);
    return false;
  }
  const auto synthesis_deadline = std::chrono::steady_clock::now() + synthesis_timeout_;
  const auto watchdog_deadline = std::chrono::system_clock::now() + synthesis_timeout_;
  std::mutex synthesis_mutex;
  std::condition_variable synthesis_changed;
  bool synthesis_finished = false;
  std::atomic_bool synthesis_timed_out{false};
  std::thread synthesis_watchdog([&] {
    std::unique_lock<std::mutex> lock(synthesis_mutex);
    if (!synthesis_changed.wait_until(lock, watchdog_deadline, [&] {
          return synthesis_finished;
        })) {
      synthesis_timed_out.store(true);
      synthesizer_->Cancel();
    }
  });
  SpeechSynthesisResult synthesis;
  try {
    synthesis = synthesizer_->Synthesize(text, synthesis_deadline);
  } catch (const std::exception& exception) {
    synthesis.error = exception.what();
  } catch (...) {
    synthesis.error = "speech synthesis failed";
  }
  {
    std::lock_guard<std::mutex> lock(synthesis_mutex);
    synthesis_finished = true;
  }
  synthesis_changed.notify_all();
  synthesis_watchdog.join();
  if (synthesis_timed_out.load()) {
    synthesis.success = false;
    synthesis.error = "speech synthesis deadline exceeded";
  }
  if (!synthesis.success || synthesis.audio.samples.empty()) {
    failed_.fetch_add(1U);
    return false;
  }
  proto::audio::PlayPcmRequest request;
  request.set_sample_rate_hz(static_cast<std::uint32_t>(synthesis.audio.format.sample_rate_hz));
  request.set_channels(static_cast<std::uint32_t>(synthesis.audio.format.channels));
  std::string samples;
  samples.resize(synthesis.audio.samples.size() * sizeof(std::int16_t));
  for (std::size_t index = 0; index < synthesis.audio.samples.size(); ++index) {
    std::uint16_t raw = 0;
    std::memcpy(&raw, &synthesis.audio.samples[index], sizeof(raw));
    samples[index * 2U] = static_cast<char>(raw & 0xFFU);
    samples[index * 2U + 1U] = static_cast<char>((raw >> 8U) & 0xFFU);
  }
  request.set_samples_s16le(std::move(samples));
  proto::audio::PlayPcmResponse response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
  {
    std::lock_guard<std::mutex> lock(context_mutex_);
    if (stopping_) {
      dropped_.fetch_add(1U);
      return false;
    }
    active_context_ = &context;
  }
  const grpc::Status status = stub_->PlayPcm(&context, request, &response);
  {
    std::lock_guard<std::mutex> lock(context_mutex_);
    active_context_ = nullptr;
  }
  if (status.ok() && response.accepted()) {
    MarkReachable();
    last_success_timestamp_ms_.store(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count()));
    queued_.fetch_add(1U);
    return true;
  }
  if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED) {
    MarkReachable();
    dropped_.fetch_add(1U);
  } else {
    available_.store(false);
    consecutive_failures_.fetch_add(1U);
    failed_.fetch_add(1U);
  }
  return false;
}

VoiceOutputMetrics AudioPlaybackClient::metrics() const {
  VoiceOutputMetrics result;
  result.queued = queued_.load();
  result.failed = failed_.load();
  result.dropped = dropped_.load();
  result.reconnects = reconnects_.load();
  result.consecutive_failures = consecutive_failures_.load();
  result.last_success_timestamp_ms = last_success_timestamp_ms_.load();
  result.available = available_.load();
  return result;
}

void AudioPlaybackClient::MarkReachable() {
  if (!available_.exchange(true) && connected_once_.exchange(true)) {
    reconnects_.fetch_add(1U);
  }
  consecutive_failures_.store(0);
}

void AudioPlaybackClient::Stop() {
  std::lock_guard<std::mutex> lock(context_mutex_);
  stopping_ = true;
  if (synthesizer_ != nullptr) {
    synthesizer_->Cancel();
  }
  available_.store(false);
  if (active_context_ != nullptr) {
    active_context_->TryCancel();
  }
}

}  // namespace voice
}  // namespace cockpit
