#include "cockpit/library/driver/audio/grpc/audio_grpc_server.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"

namespace cockpit {
namespace audio {
namespace {

proto::audio::CaptureState ToProtoState(AudioCaptureState state) {
  switch (state) {
    case AudioCaptureState::kStopped:
      return proto::audio::CAPTURE_STATE_STOPPED;
    case AudioCaptureState::kStarting:
      return proto::audio::CAPTURE_STATE_STARTING;
    case AudioCaptureState::kRunning:
      return proto::audio::CAPTURE_STATE_RUNNING;
    case AudioCaptureState::kRecovering:
      return proto::audio::CAPTURE_STATE_RECOVERING;
    case AudioCaptureState::kFaulted:
      return proto::audio::CAPTURE_STATE_FAULTED;
  }
  return proto::audio::CAPTURE_STATE_UNSPECIFIED;
}

proto::common::RuntimeModuleState ToProtoModuleState(runtime::ModuleState state) {
  switch (state) {
    case runtime::ModuleState::kCreated:
      return proto::common::RUNTIME_MODULE_STATE_CREATED;
    case runtime::ModuleState::kStarting:
      return proto::common::RUNTIME_MODULE_STATE_STARTING;
    case runtime::ModuleState::kRunning:
      return proto::common::RUNTIME_MODULE_STATE_RUNNING;
    case runtime::ModuleState::kStopping:
      return proto::common::RUNTIME_MODULE_STATE_STOPPING;
    case runtime::ModuleState::kStopped:
      return proto::common::RUNTIME_MODULE_STATE_STOPPED;
    case runtime::ModuleState::kFailed:
      return proto::common::RUNTIME_MODULE_STATE_FAILED;
  }
  return proto::common::RUNTIME_MODULE_STATE_UNSPECIFIED;
}

proto::audio::PlaybackStatus ToProtoPlaybackStatus(AudioPlaybackStatus status) {
  switch (status) {
    case AudioPlaybackStatus::kPending:
      return proto::audio::PLAYBACK_STATUS_PENDING;
    case AudioPlaybackStatus::kCompleted:
      return proto::audio::PLAYBACK_STATUS_COMPLETED;
    case AudioPlaybackStatus::kFailed:
      return proto::audio::PLAYBACK_STATUS_FAILED;
    case AudioPlaybackStatus::kCancelled:
      return proto::audio::PLAYBACK_STATUS_CANCELLED;
    case AudioPlaybackStatus::kDropped:
      return proto::audio::PLAYBACK_STATUS_DROPPED;
  }
  return proto::audio::PLAYBACK_STATUS_UNSPECIFIED;
}

bool IsSupportedPlaybackRate(std::uint32_t sample_rate_hz) {
  // Capture remains fixed at 16 kHz. Playback also accepts Kokoro's native
  // 24 kHz output without permitting arbitrary device reconfiguration.
  return sample_rate_hz == 16000U || sample_rate_hz == 24000U;
}

void FillHealth(const AudioCaptureControllerStatus& status, proto::common::ServiceHealth* health) {
  health->set_service_name("audio-driver");
  health->set_checked_at_ms(time::WallTime::Now().ToMilliseconds());
  health->set_last_error(status.last_error);
  if (status.capture_state == AudioCaptureState::kFaulted) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "audio capture faulted" : status.last_error);
    return;
  }
  if (status.capture_state == AudioCaptureState::kStopped) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DISABLED);
    health->set_message("audio capture stopped");
    return;
  }
  health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
  health->set_message("audio driver online");
}

}  // namespace

AudioGrpcServer::AudioGrpcServer(AudioCaptureController& capture, AudioPlayback& playback)
    : capture_(capture), playback_(playback) {
}

AudioGrpcServer::~AudioGrpcServer() {
  Shutdown();
}

bool AudioGrpcServer::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (!server_) {
    LOG_ERROR("failed to start audio gRPC server address=" + address);
    return false;
  }
  LOG_INFO("audio gRPC server listening address=" + address);
  return true;
}

void AudioGrpcServer::Shutdown() {
  if (server_) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status AudioGrpcServer::StartCapture(grpc::ServerContext*,
                                           const proto::audio::StartCaptureRequest* request,
                                           proto::audio::AudioStatus* response) {
  std::string error;
  if (!capture_.Start(request->input_device(), &error)) {
    FillStatus(capture_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(capture_.status(), response);
  return grpc::Status::OK;
}

grpc::Status AudioGrpcServer::StopCapture(grpc::ServerContext*, const proto::common::Empty*,
                                          proto::audio::AudioStatus* response) {
  capture_.Stop();
  FillStatus(capture_.status(), response);
  return grpc::Status::OK;
}

grpc::Status AudioGrpcServer::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                        proto::audio::AudioStatus* response) {
  FillStatus(capture_.status(), response);
  return grpc::Status::OK;
}

grpc::Status AudioGrpcServer::PlayPcm(grpc::ServerContext*,
                                      const proto::audio::PlayPcmRequest* request,
                                      proto::audio::PlayPcmResponse* response) {
  constexpr std::size_t kMaxPcmBytes = 2ULL * 1024ULL * 1024ULL;
  if (!IsSupportedPlaybackRate(request->sample_rate_hz()) || request->channels() != 1U) {
    response->set_accepted(false);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "PCM playback requires signed 16-bit 16 or 24 kHz mono audio");
  }
  const std::string& bytes = request->samples_s16le();
  if (bytes.empty() || bytes.size() > kMaxPcmBytes || bytes.size() % 2U != 0U) {
    response->set_accepted(false);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "invalid PCM payload size");
  }
  PcmBuffer buffer;
  buffer.format.sample_rate_hz = static_cast<int>(request->sample_rate_hz());
  buffer.format.channels = 1;
  buffer.format.frame_ms = 20;
  buffer.samples.resize(bytes.size() / 2U);
  for (std::size_t index = 0; index < buffer.samples.size(); ++index) {
    const auto low = static_cast<std::uint8_t>(bytes[index * 2U]);
    const auto high = static_cast<std::uint8_t>(bytes[index * 2U + 1U]);
    const std::uint16_t raw =
        static_cast<std::uint16_t>(low) | (static_cast<std::uint16_t>(high) << 8U);
    std::memcpy(&buffer.samples[index], &raw, sizeof(raw));
  }
  if (request->playback_id() == 0U) {
    response->set_accepted(false);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "playback_id must be non-zero");
  }
  const auto playback_id = playback_.Submit(std::move(buffer), request->playback_id());
  response->set_accepted(playback_id.has_value());
  response->set_playback_id(playback_id.value_or(request->playback_id()));
  return playback_id.has_value() ? grpc::Status::OK
                                 : grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                                "audio playback queue rejected the request");
}

grpc::Status AudioGrpcServer::WaitPlayback(grpc::ServerContext*,
                                           const proto::audio::PlaybackRequest* request,
                                           proto::audio::PlaybackResult* response) {
  constexpr std::uint32_t kMaxWaitTimeoutMs = 120000U;
  if (request->playback_id() == 0U || request->wait_timeout_ms() == 0U ||
      request->wait_timeout_ms() > kMaxWaitTimeoutMs) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "playback wait requires an id and timeout up to 120000 ms");
  }
  AudioPlaybackResult result;
  const AudioPlaybackWaitStatus wait = playback_.WaitForResult(
      request->playback_id(), std::chrono::milliseconds(request->wait_timeout_ms()), &result);
  response->set_playback_id(request->playback_id());
  if (wait == AudioPlaybackWaitStatus::kNotFound) {
    response->set_status(proto::audio::PLAYBACK_STATUS_NOT_FOUND);
    return grpc::Status::OK;
  }
  if (wait == AudioPlaybackWaitStatus::kTimeout) {
    response->set_status(proto::audio::PLAYBACK_STATUS_PENDING);
    return grpc::Status::OK;
  }
  response->set_status(ToProtoPlaybackStatus(result.status));
  response->set_error(result.error);
  return grpc::Status::OK;
}

grpc::Status AudioGrpcServer::CancelPlayback(grpc::ServerContext*,
                                             const proto::audio::CancelPlaybackRequest* request,
                                             proto::audio::CancelPlaybackResponse* response) {
  if (request->playback_id() == 0U) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "playback_id must be non-zero");
  }
  response->set_accepted(playback_.Cancel(request->playback_id()));
  return grpc::Status::OK;
}

void AudioGrpcServer::FillStatus(const AudioCaptureControllerStatus& status,
                                 proto::audio::AudioStatus* response) const {
  response->set_capture_state(ToProtoState(status.capture_state));
  response->set_input_device(status.input_device);
  response->set_sample_rate_hz(status.sample_rate_hz);
  response->set_channels(status.channels);
  response->set_frame_ms(status.frame_ms);
  response->set_last_error(status.last_error);
  response->set_input_level_dbfs(status.input_level_dbfs);
  FillHealth(status, response->mutable_health());
  auto* metrics = response->mutable_metrics();
  metrics->set_pcm_frames_read(status.capture_metrics.pcm_frames_read);
  metrics->set_audio_frames_published(status.capture_metrics.audio_frames_published);
  metrics->set_audio_frames_dropped(status.capture_metrics.audio_frames_dropped);
  metrics->set_timeouts(status.capture_metrics.timeouts);
  metrics->set_xruns(status.capture_metrics.xruns);
  metrics->set_device_errors(status.capture_metrics.device_errors);
  metrics->set_stream_clients_accepted(status.stream_metrics.clients_accepted);
  metrics->set_stream_frames_queued(status.stream_metrics.frames_queued);
  metrics->set_stream_frames_sent(status.stream_metrics.frames_sent);
  metrics->set_stream_frames_dropped(status.stream_metrics.frames_dropped);
  metrics->set_stream_client_disconnects(status.stream_metrics.client_disconnects);
  const AudioPlaybackMetrics playback = playback_.metrics();
  metrics->set_playback_queued(playback.queued);
  metrics->set_playback_played(playback.played);
  metrics->set_playback_failed(playback.failed);
  metrics->set_playback_dropped(playback.dropped);
  for (const auto& module : status.modules) {
    auto* module_status = response->add_modules();
    module_status->set_name(module.name);
    module_status->set_state(ToProtoModuleState(module.state));
  }
}

}  // namespace audio
}  // namespace cockpit
