#include "audio_grpc_service.h"

#include <chrono>

#include "core/logging/Logger.h"

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

proto::audio::VoiceActivityState ToProtoVoiceActivityState(const AudioServiceStatus& status) {
  if (!status.vad_enabled) {
    return proto::audio::VOICE_ACTIVITY_STATE_DISABLED;
  }
  return status.voice_activity_state == VoiceActivityState::kSpeech
             ? proto::audio::VOICE_ACTIVITY_STATE_SPEECH
             : proto::audio::VOICE_ACTIVITY_STATE_SILENCE;
}

}  // namespace

AudioGrpcService::AudioGrpcService(AudioService& audio_service,
                                   voice::VoiceResponseSink& speech_output)
    : audio_service_(audio_service), speech_output_(speech_output) {
}

AudioGrpcService::~AudioGrpcService() {
  Shutdown();
}

bool AudioGrpcService::Start(const std::string& address) {
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

void AudioGrpcService::Shutdown() {
  if (server_) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status AudioGrpcService::StartCapture(grpc::ServerContext*,
                                            const proto::audio::StartCaptureRequest* request,
                                            proto::audio::AudioStatus* response) {
  std::string error;
  if (!audio_service_.StartCapture(request->input_device(), &error)) {
    FillStatus(audio_service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(audio_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status AudioGrpcService::StopCapture(grpc::ServerContext*, const proto::common::Empty*,
                                           proto::audio::AudioStatus* response) {
  audio_service_.StopCapture();
  FillStatus(audio_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status AudioGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                         proto::audio::AudioStatus* response) {
  FillStatus(audio_service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status AudioGrpcService::Speak(grpc::ServerContext*,
                                     const proto::audio::SpeakRequest* request,
                                     proto::audio::SpeakResponse* response) {
  if (request->text().empty()) {
    response->set_accepted(false);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "speech text must not be empty");
  }
  const bool accepted = speech_output_.Submit(request->text());
  response->set_accepted(accepted);
  return accepted ? grpc::Status::OK
                  : grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                 "speech output queue rejected the request");
}

grpc::Status AudioGrpcService::SubscribeTranscripts(
    grpc::ServerContext* context, const proto::audio::SubscribeTranscriptsRequest* request,
    grpc::ServerWriter<proto::audio::TranscriptEvent>* writer) {
  std::uint64_t observed_id = request->after_id();
  std::uint32_t emitted = 0;
  LOG_INFO("transcript subscriber connected client_id=" + request->client_id());
  while (!context->IsCancelled() &&
         (request->max_events() == 0 || emitted < request->max_events())) {
    voice::SpeechTranscript transcript;
    if (!audio_service_.WaitForTranscript(observed_id, std::chrono::milliseconds(100),
                                          &transcript)) {
      continue;
    }
    proto::audio::TranscriptEvent event;
    FillTranscript(transcript, &event);
    if (!writer->Write(event)) {
      break;
    }
    observed_id = transcript.id;
    ++emitted;
  }
  LOG_INFO("transcript subscriber disconnected client_id=" + request->client_id());
  return grpc::Status::OK;
}

void AudioGrpcService::FillStatus(const AudioServiceStatus& status,
                                  proto::audio::AudioStatus* response) const {
  response->set_capture_state(ToProtoState(status.capture_state));
  response->set_input_device(status.input_device);
  response->set_sample_rate_hz(status.sample_rate_hz);
  response->set_channels(status.channels);
  response->set_frame_ms(status.frame_ms);
  response->set_last_error(status.last_error);
  response->set_voice_activity_state(ToProtoVoiceActivityState(status));
  response->set_input_level_dbfs(status.input_level_dbfs);
  response->set_asr_enabled(status.asr_enabled);
  auto* metrics = response->mutable_metrics();
  metrics->set_pcm_frames_read(status.metrics.pcm_frames_read);
  metrics->set_audio_frames_published(status.metrics.audio_frames_published);
  metrics->set_audio_frames_dropped(status.metrics.audio_frames_dropped);
  metrics->set_timeouts(status.metrics.timeouts);
  metrics->set_xruns(status.metrics.xruns);
  metrics->set_device_errors(status.metrics.device_errors);
  metrics->set_vad_frames_processed(status.vad_frames_processed);
  metrics->set_vad_speech_frames(status.vad_speech_frames);
  metrics->set_vad_speech_events(status.vad_speech_events);
  metrics->set_vad_silence_events(status.vad_silence_events);
  metrics->set_speech_segments_completed(status.speech_segments_completed);
  metrics->set_speech_segments_truncated(status.speech_segments_truncated);
  metrics->set_speech_segments_dropped(status.speech_segments_dropped);
  metrics->set_last_segment_duration_ms(status.last_segment_duration_ms);
  metrics->set_asr_segments_processed(status.asr_segments_processed);
  metrics->set_transcripts_published(status.transcripts_published);
  metrics->set_asr_errors(status.asr_errors);
  const voice::VoiceOutputMetrics output = speech_output_.metrics();
  metrics->set_tts_queued(output.queued);
  metrics->set_tts_played(output.played);
  metrics->set_tts_failed(output.failed);
  metrics->set_tts_dropped(output.dropped);
}

void AudioGrpcService::FillTranscript(const voice::SpeechTranscript& transcript,
                                      proto::audio::TranscriptEvent* response) {
  response->set_id(transcript.id);
  response->set_timestamp_ms(transcript.timestamp_ms);
  response->set_start_sequence(transcript.start_sequence);
  response->set_end_sequence(transcript.end_sequence);
  response->set_duration_ms(transcript.duration_ms);
  response->set_truncated(transcript.truncated);
  response->set_discontinuous(transcript.discontinuous);
  response->set_text(transcript.text);
  response->set_provider(transcript.provider);
  response->set_confidence(transcript.confidence);
}

}  // namespace audio
}  // namespace cockpit
