#include "agent/audio/audio_playback_client.h"

#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

#include "agent/speech/tts/mock_speech_synthesizer.h"
#include "audio.grpc.pb.h"

namespace cockpit {
namespace voice {
namespace {

std::uint64_t InitialPlaybackId() {
  static std::atomic<std::uint64_t> client_instance{1U};
  const auto now =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
  const auto process = static_cast<std::uint64_t>(getpid());
  const std::uint64_t id = now ^ (process << 32U) ^ client_instance.fetch_add(1U);
  return id == 0U ? 1U : id;
}

std::shared_ptr<grpc::ChannelInterface> CreateAudioChannel(const std::string& address) {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  return grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments);
}

class GrpcAudioPlaybackTransport final : public AudioPlaybackTransport {
 public:
  explicit GrpcAudioPlaybackTransport(const std::string& address)
      : channel_(CreateAudioChannel(address)),
        stub_(proto::audio::AudioControl::NewStub(channel_)),
        cancel_stub_(proto::audio::AudioControl::NewStub(channel_)) {
  }

  AudioPlaybackSubmitResult Submit(std::uint64_t playback_id,
                                   const audio::PcmBuffer& audio) override {
    proto::audio::PlayPcmRequest request;
    request.set_sample_rate_hz(static_cast<std::uint32_t>(audio.format.sample_rate_hz));
    request.set_channels(static_cast<std::uint32_t>(audio.format.channels));
    std::string samples;
    samples.resize(audio.samples.size() * sizeof(std::int16_t));
    for (std::size_t index = 0; index < audio.samples.size(); ++index) {
      std::uint16_t raw = 0;
      std::memcpy(&raw, &audio.samples[index], sizeof(raw));
      samples[index * 2U] = static_cast<char>(raw & 0xFFU);
      samples[index * 2U + 1U] = static_cast<char>((raw >> 8U) & 0xFFU);
    }
    request.set_samples_s16le(std::move(samples));
    request.set_playback_id(playback_id);
    proto::audio::PlayPcmResponse response;
    grpc::ClientContext context;
    context.set_initial_metadata_corked(false);
    context.set_wait_for_ready(true);
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
    const grpc::Status status = stub_->PlayPcm(&context, request, &response);
    if (status.ok() && response.accepted()) {
      return {AudioPlaybackSubmitStatus::kAccepted, {}};
    }
    if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED) {
      return {AudioPlaybackSubmitStatus::kRejected, status.error_message()};
    }
    if (status.error_code() == grpc::StatusCode::CANCELLED) {
      return {AudioPlaybackSubmitStatus::kCancelled, status.error_message()};
    }
    return {AudioPlaybackSubmitStatus::kFailed, status.error_message()};
  }

  VoiceOutputResult Wait(std::uint64_t request_id, std::uint64_t playback_id,
                         std::chrono::milliseconds timeout) override {
    proto::audio::PlaybackRequest request;
    request.set_playback_id(playback_id);
    request.set_wait_timeout_ms(static_cast<std::uint32_t>(timeout.count()));
    proto::audio::PlaybackResult response;
    grpc::ClientContext context;
    context.set_initial_metadata_corked(false);
    context.set_wait_for_ready(true);
    context.set_deadline(std::chrono::system_clock::now() + timeout + std::chrono::seconds(1));
    const grpc::Status status = stub_->WaitPlayback(&context, request, &response);
    VoiceOutputResult result;
    result.request_id = request_id;
    result.error = status.ok() ? response.error() : status.error_message();
    if (!status.ok()) {
      result.status = status.error_code() == grpc::StatusCode::CANCELLED
                          ? VoiceOutputStatus::kCancelled
                          : VoiceOutputStatus::kFailed;
      return result;
    }
    switch (response.status()) {
      case proto::audio::PLAYBACK_STATUS_COMPLETED:
        result.status = VoiceOutputStatus::kCompleted;
        break;
      case proto::audio::PLAYBACK_STATUS_CANCELLED:
        result.status = VoiceOutputStatus::kCancelled;
        break;
      case proto::audio::PLAYBACK_STATUS_DROPPED:
        result.status = VoiceOutputStatus::kDropped;
        break;
      case proto::audio::PLAYBACK_STATUS_UNSPECIFIED:
      case proto::audio::PLAYBACK_STATUS_PENDING:
      case proto::audio::PLAYBACK_STATUS_FAILED:
      case proto::audio::PLAYBACK_STATUS_NOT_FOUND:
        result.status = VoiceOutputStatus::kFailed;
        break;
      default:
        result.status = VoiceOutputStatus::kFailed;
        break;
    }
    return result;
  }

  void Cancel(std::uint64_t playback_id) override {
    proto::audio::CancelPlaybackRequest request;
    request.set_playback_id(playback_id);
    proto::audio::CancelPlaybackResponse response;
    grpc::ClientContext context;
    context.set_initial_metadata_corked(false);
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(1));
    cancel_stub_->CancelPlayback(&context, request, &response);
  }

 private:
  const std::shared_ptr<grpc::ChannelInterface> channel_;
  const std::unique_ptr<proto::audio::AudioControl::Stub> stub_;
  const std::unique_ptr<proto::audio::AudioControl::Stub> cancel_stub_;
};

}  // namespace

AudioPlaybackClient::AudioPlaybackClient(const std::string& address)
    : AudioPlaybackClient(address, std::make_unique<MockSpeechSynthesizer>(),
                          std::chrono::seconds(5)) {
}

AudioPlaybackClient::AudioPlaybackClient(const std::string& address,
                                         std::unique_ptr<SpeechSynthesizer> synthesizer,
                                         std::chrono::milliseconds synthesis_timeout)
    : AudioPlaybackClient(std::make_unique<GrpcAudioPlaybackTransport>(address),
                          std::move(synthesizer), synthesis_timeout) {
}

AudioPlaybackClient::AudioPlaybackClient(std::unique_ptr<AudioPlaybackTransport> transport,
                                         std::unique_ptr<SpeechSynthesizer> synthesizer,
                                         std::chrono::milliseconds synthesis_timeout)
    : transport_(std::move(transport)),
      synthesizer_(std::move(synthesizer)),
      synthesis_timeout_(synthesis_timeout) {
  if (transport_ == nullptr || synthesis_timeout_ <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("audio playback requires a transport and positive timeout");
  }
  next_playback_id_.store(InitialPlaybackId());
}

bool AudioPlaybackClient::Submit(std::uint64_t request_id, std::string text,
                                 VoiceOutputCompletion completion) {
  if (request_id == 0U || !completion) {
    dropped_.fetch_add(1U);
    return false;
  }
  const std::uint64_t request_generation = interrupt_generation_.load();
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (stopping_ || active_request_id_ != 0U) {
      dropped_.fetch_add(1U);
      return false;
    }
    active_request_id_ = request_id;
  }
  if (synthesizer_ == nullptr) {
    failed_.fetch_add(1U);
    ClearActiveRequest(request_id);
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
    ClearActiveRequest(request_id);
    return false;
  }
  if (request_generation != interrupt_generation_.load()) {
    ClearActiveRequest(request_id);
    return false;
  }
  const std::uint64_t playback_id = next_playback_id_.fetch_add(1U);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (stopping_) {
      dropped_.fetch_add(1U);
      active_request_id_ = 0U;
      return false;
    }
    active_playback_id_ = playback_id;
  }
  const AudioPlaybackSubmitResult submission = transport_->Submit(playback_id, synthesis.audio);
  if (submission.status == AudioPlaybackSubmitStatus::kAccepted) {
    MarkReachable();
    queued_.fetch_add(1U);
  } else {
    ClearActiveRequest(request_id);
    if (submission.status == AudioPlaybackSubmitStatus::kRejected) {
      MarkReachable();
      dropped_.fetch_add(1U);
    } else if (submission.status == AudioPlaybackSubmitStatus::kCancelled) {
      completion({request_id, VoiceOutputStatus::kCancelled,
                  submission.error.empty() ? "voice output interrupted" : submission.error});
      return true;
    } else {
      available_.store(false);
      consecutive_failures_.fetch_add(1U);
      failed_.fetch_add(1U);
    }
    return false;
  }

  const auto playback_duration =
      std::chrono::milliseconds(synthesis.audio.samples.size() * 1000U /
                                static_cast<std::size_t>(synthesis.audio.format.sample_rate_hz));
  const auto wait_timeout =
      std::min(playback_duration + std::chrono::seconds(5),
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(120)));
  bool cancel_before_wait = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (stopping_ || request_generation != interrupt_generation_.load()) {
      cancel_before_wait = true;
    }
  }
  if (cancel_before_wait) {
    transport_->Cancel(playback_id);
    ClearActiveRequest(request_id);
    completion({request_id, VoiceOutputStatus::kCancelled, "voice output interrupted"});
    return true;
  }
  VoiceOutputResult result = transport_->Wait(request_id, playback_id, wait_timeout);
  ClearActiveRequest(request_id);

  if (request_generation != interrupt_generation_.load()) {
    result.status = VoiceOutputStatus::kCancelled;
  }
  if (result.status == VoiceOutputStatus::kCompleted) {
    played_.fetch_add(1U);
    last_success_timestamp_ms_.store(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count()));
  } else if (result.status == VoiceOutputStatus::kDropped) {
    dropped_.fetch_add(1U);
  } else if (result.status == VoiceOutputStatus::kFailed) {
    if (result.error.empty()) {
      result.error = "audio playback did not complete";
    }
    available_.store(false);
    consecutive_failures_.fetch_add(1U);
    failed_.fetch_add(1U);
  }
  completion(std::move(result));
  return true;
}

VoiceOutputMetrics AudioPlaybackClient::metrics() const {
  VoiceOutputMetrics result;
  result.queued = queued_.load();
  result.played = played_.load();
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

void AudioPlaybackClient::Interrupt() {
  interrupt_generation_.fetch_add(1U);
  std::uint64_t playback_id = 0U;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    playback_id = active_playback_id_;
  }
  if (synthesizer_ != nullptr) {
    synthesizer_->Cancel();
  }
  if (playback_id != 0U) {
    transport_->Cancel(playback_id);
  }
}

void AudioPlaybackClient::Stop() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    stopping_ = true;
  }
  Interrupt();
  available_.store(false);
}

void AudioPlaybackClient::ClearActiveRequest(std::uint64_t request_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (active_request_id_ == request_id) {
    active_request_id_ = 0U;
    active_playback_id_ = 0U;
  }
}

}  // namespace voice
}  // namespace cockpit
