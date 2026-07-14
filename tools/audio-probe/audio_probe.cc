#include "audio_probe.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "audio_control_client.h"

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/drivers/alsa/alsa_pcm.h"
#include "cockpit/modules/audio/wav/wav_file.h"
#include "tools/diagnostics/cli_output.h"

namespace {

int ListDevices(const cockpit::runtime::ProcessRuntime& runtime) {
  std::string error;
  const auto devices = cockpit::audio::AlsaPcm::ListDevices(&error);
  if (!error.empty()) {
    LOG_ERROR(error);
    return 1;
  }
  if (devices.empty()) {
    std::cout << "no ALSA PCM devices found\n";
    return 0;
  }
  for (const auto& device : devices) {
    std::cout << device.name << " [" << cockpit::audio::ToString(device.io) << ']';
    if (!device.description.empty()) {
      std::string description = device.description;
      std::replace(description.begin(), description.end(), '\n', ' ');
      std::replace(description.begin(), description.end(), '\r', ' ');
      std::cout << " - " << description;
    }
    std::cout << '\n';
  }
  return 0;
}

int Capture(const cockpit::runtime::ProcessRuntime& runtime, const std::string& output_path) {
  const auto& audio_config = runtime.config().hardware().audio;
  cockpit::audio::PcmFormat format;
  format.sample_rate_hz = audio_config.sample_rate_hz;
  format.channels = audio_config.channels;
  format.frame_ms = audio_config.frame_ms;
  const std::string device = runtime.args().GetString("device", audio_config.input_device);
  const int seconds = std::clamp(runtime.args().GetInt("seconds", 3), 1, 60);
  const std::size_t total_frames =
      static_cast<std::size_t>(format.sample_rate_hz) * static_cast<std::size_t>(seconds);
  if (total_frames >
      std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(format.channels)) {
    LOG_ERROR("audio capture size overflow");
    return 2;
  }

  cockpit::audio::AlsaPcm pcm;
  std::string error;
  if (!pcm.Open(device, cockpit::audio::PcmDirection::kCapture, format, &error)) {
    LOG_ERROR(error);
    return 1;
  }

  std::vector<std::int16_t> samples;
  samples.reserve(total_frames * static_cast<std::size_t>(format.channels));
  std::vector<std::int16_t> period(format.FramesPerPeriod() *
                                   static_cast<std::size_t>(format.channels));
  std::size_t captured_frames = 0;
  const std::atomic_bool stop_requested{false};
  while (captured_frames < total_frames && !runtime.ShouldStop()) {
    const std::size_t frame_capacity =
        std::min(format.FramesPerPeriod(), total_frames - captured_frames);
    const auto result = pcm.PollReadFrames(period.data(), frame_capacity, 100, stop_requested);
    if (result.status == cockpit::audio::CaptureStatus::kTimeout ||
        result.status == cockpit::audio::CaptureStatus::kXrunRecovered) {
      continue;
    }
    if (result.status == cockpit::audio::CaptureStatus::kStopped) {
      break;
    }
    if (result.status == cockpit::audio::CaptureStatus::kDeviceError) {
      LOG_ERROR(result.message);
      return 1;
    }
    samples.insert(
        samples.end(), period.begin(),
        period.begin() + static_cast<std::ptrdiff_t>(result.frames_read *
                                                     static_cast<std::size_t>(format.channels)));
    captured_frames += result.frames_read;
  }
  pcm.Close();

  if (samples.empty()) {
    LOG_ERROR("audio capture stopped before any samples were received");
    return 1;
  }
  if (!cockpit::audio::WritePcm16Wav(output_path, format, samples, &error)) {
    LOG_ERROR(error);
    return 1;
  }
  std::cout << "captured " << captured_frames << " frames from " << device << " to " << output_path
            << '\n';
  return 0;
}

int Play(const cockpit::runtime::ProcessRuntime& runtime, const std::string& input_path) {
  cockpit::audio::PcmBuffer buffer;
  std::string error;
  if (!cockpit::audio::ReadPcm16Wav(input_path, &buffer, &error)) {
    LOG_ERROR(error);
    return 1;
  }

  const auto& audio_config = runtime.config().hardware().audio;
  buffer.format.frame_ms = audio_config.frame_ms;
  const std::string device = runtime.args().GetString("device", audio_config.output_device);
  cockpit::audio::AlsaPcm pcm;
  if (!pcm.Open(device, cockpit::audio::PcmDirection::kPlayback, buffer.format, &error)) {
    LOG_ERROR(error);
    return 1;
  }

  std::size_t played_frames = 0;
  while (played_frames < buffer.FrameCount() && !runtime.ShouldStop()) {
    const std::size_t frames =
        std::min(buffer.format.FramesPerPeriod(), buffer.FrameCount() - played_frames);
    const auto* samples =
        buffer.samples.data() + played_frames * static_cast<std::size_t>(buffer.format.channels);
    if (!pcm.WriteFrames(samples, frames, &error)) {
      LOG_ERROR(error);
      return 1;
    }
    played_frames += frames;
  }
  if (!runtime.ShouldStop() && !pcm.Drain(&error)) {
    LOG_ERROR(error);
    return 1;
  }
  pcm.Close();
  std::cout << "played " << played_frames << " frames from " << input_path << " through " << device
            << '\n';
  return 0;
}

const char* CaptureStateName(cockpit::proto::audio::CaptureState state) {
  switch (state) {
    case cockpit::proto::audio::CAPTURE_STATE_STOPPED:
      return "stopped";
    case cockpit::proto::audio::CAPTURE_STATE_STARTING:
      return "starting";
    case cockpit::proto::audio::CAPTURE_STATE_RUNNING:
      return "running";
    case cockpit::proto::audio::CAPTURE_STATE_RECOVERING:
      return "recovering";
    case cockpit::proto::audio::CAPTURE_STATE_FAULTED:
      return "faulted";
    case cockpit::proto::audio::CAPTURE_STATE_UNSPECIFIED:
      return "unspecified";
    default:
      return "unknown";
  }
}

const char* VoiceActivityStateName(cockpit::proto::audio::VoiceActivityState state) {
  switch (state) {
    case cockpit::proto::audio::VOICE_ACTIVITY_STATE_DISABLED:
      return "disabled";
    case cockpit::proto::audio::VOICE_ACTIVITY_STATE_SILENCE:
      return "silence";
    case cockpit::proto::audio::VOICE_ACTIVITY_STATE_SPEECH:
      return "speech";
    case cockpit::proto::audio::VOICE_ACTIVITY_STATE_UNSPECIFIED:
      return "unspecified";
    default:
      return "unknown";
  }
}

void PrintStatusText(const cockpit::proto::audio::AudioStatus& status) {
  const auto& metrics = status.metrics();
  std::cout << "state: " << CaptureStateName(status.capture_state()) << '\n'
            << "device: " << status.input_device() << '\n'
            << "format: " << status.sample_rate_hz() << " Hz, " << status.channels()
            << " channel(s), " << status.frame_ms() << " ms\n"
            << "frames read: " << metrics.pcm_frames_read() << '\n'
            << "frames published: " << metrics.audio_frames_published() << '\n'
            << "frames dropped: " << metrics.audio_frames_dropped() << '\n'
            << "voice activity: " << VoiceActivityStateName(status.voice_activity_state()) << '\n'
            << "input level: " << status.input_level_dbfs() << " dBFS\n"
            << "VAD frames: " << metrics.vad_frames_processed() << '\n'
            << "VAD speech frames: " << metrics.vad_speech_frames() << '\n'
            << "VAD speech events: " << metrics.vad_speech_events() << '\n'
            << "VAD silence events: " << metrics.vad_silence_events() << '\n'
            << "speech segments: " << metrics.speech_segments_completed() << '\n'
            << "segments truncated: " << metrics.speech_segments_truncated() << '\n'
            << "segments dropped: " << metrics.speech_segments_dropped() << '\n'
            << "last segment: " << metrics.last_segment_duration_ms() << " ms\n"
            << "ASR enabled: " << (status.asr_enabled() ? "true" : "false") << '\n'
            << "ASR segments: " << metrics.asr_segments_processed() << '\n'
            << "transcripts: " << metrics.transcripts_published() << '\n'
            << "ASR errors: " << metrics.asr_errors() << '\n'
            << "TTS queued: " << metrics.tts_queued() << '\n'
            << "TTS played: " << metrics.tts_played() << '\n'
            << "TTS failed: " << metrics.tts_failed() << '\n'
            << "TTS dropped: " << metrics.tts_dropped() << '\n'
            << "timeouts: " << metrics.timeouts() << '\n'
            << "xruns: " << metrics.xruns() << '\n'
            << "device errors: " << metrics.device_errors() << '\n';
  if (!status.last_error().empty()) {
    std::cout << "last error: " << status.last_error() << '\n';
  }
}

int Control(const cockpit::runtime::ProcessRuntime& runtime, const std::string& command,
            cockpit::diagnostics::OutputFormat output_format) {
  const std::string address =
      runtime.args().GetString("address", runtime.config().services().audio.grpc.listen_address);
  cockpit::audio::AudioControlClient client(address);
  cockpit::proto::audio::AudioStatus status;
  std::string error;
  bool success = false;
  if (command == "start") {
    success = client.StartCapture(runtime.args().GetString("device", ""), &status, &error);
  } else if (command == "stop") {
    success = client.StopCapture(&status, &error);
  } else {
    success = client.GetStatus(&status, &error);
  }
  if (!success) {
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      cockpit::diagnostics::WriteJsonError("operation_failed", error, &std::cerr);
    } else {
      LOG_ERROR("audio control RPC failed address=" + address + " error=" + error);
    }
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed);
  }
  if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
    if (!cockpit::diagnostics::WriteJson(status, &std::cout, &error)) {
      cockpit::diagnostics::WriteJsonError("serialization_failed", error, &std::cerr);
      return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed);
    }
  } else {
    PrintStatusText(status);
  }
  return 0;
}

int Transcripts(const cockpit::runtime::ProcessRuntime& runtime,
                cockpit::diagnostics::OutputFormat output_format) {
  const std::string address =
      runtime.args().GetString("address", runtime.config().services().audio.grpc.listen_address);
  const int count = std::clamp(runtime.args().GetInt("count", 1), 1, 100);
  const int timeout_ms = std::clamp(runtime.args().GetInt("timeout-ms", 10000), 100, 60000);
  cockpit::audio::AudioControlClient client(address);
  std::string error;
  const bool success = client.SubscribeTranscripts(
      static_cast<std::uint32_t>(count), timeout_ms,
      [output_format](const cockpit::proto::audio::TranscriptEvent& event) {
        if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
          std::string serialization_error;
          if (!cockpit::diagnostics::WriteJson(event, &std::cout, &serialization_error)) {
            cockpit::diagnostics::WriteJsonError("serialization_failed", serialization_error,
                                                 &std::cerr);
          }
        } else {
          std::cout << "transcript id=" << event.id() << " provider=" << event.provider()
                    << " confidence=" << event.confidence()
                    << " duration_ms=" << event.duration_ms()
                    << " truncated=" << (event.truncated() ? "true" : "false")
                    << " discontinuous=" << (event.discontinuous() ? "true" : "false") << " text=\""
                    << event.text() << "\"\n";
        }
      },
      &error);
  if (!success) {
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      cockpit::diagnostics::WriteJsonError("operation_failed", error, &std::cerr);
    } else {
      LOG_ERROR("transcript stream failed address=" + address + " error=" + error);
    }
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed);
  }
  return 0;
}

int Speak(const cockpit::runtime::ProcessRuntime& runtime, const std::string& text) {
  const std::string address =
      runtime.args().GetString("address", runtime.config().services().audio.grpc.listen_address);
  cockpit::audio::AudioControlClient client(address);
  std::string error;
  if (!client.Speak(text, &error)) {
    LOG_ERROR("audio speak RPC failed address=" + address + " error=" + error);
    return 1;
  }
  std::cout << "speech queued\n";
  return 0;
}

void PrintUsage() {
  std::cout << "usage:\n"
            << "  audio-probe --list [--config configs/config.yaml]\n"
            << "  audio-probe --capture output.wav [--seconds N] [--device NAME]\n"
            << "  audio-probe --play input.wav [--device NAME]\n"
            << "  audio-probe --start [--device NAME] [--address HOST:PORT]\n"
            << "  audio-probe --stop [--address HOST:PORT]\n"
            << "  audio-probe --status [--address HOST:PORT]\n"
            << "  audio-probe --speak TEXT [--address HOST:PORT]\n"
            << "  audio-probe --transcripts [--count N] [--timeout-ms N]\n";
  std::cout << "  control and transcript commands accept [--output text|json]\n";
}

}  // namespace

int cockpit::audio_probe::ProbeAudio(const cockpit::runtime::ProcessRuntime& runtime) {
  cockpit::diagnostics::OutputFormat output_format;
  std::string output_error;
  if (!cockpit::diagnostics::ParseOutputFormat(runtime.args().GetString("output", "text"),
                                               &output_format, &output_error)) {
    std::cerr << output_error << '\n';
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }
  const std::string capture_path = runtime.args().GetString("capture", "");
  const std::string play_path = runtime.args().GetString("play", "");
  const bool start = runtime.args().HasFlag("start");
  const bool stop = runtime.args().HasFlag("stop");
  const bool status = runtime.args().HasFlag("status");
  const bool transcripts = runtime.args().HasFlag("transcripts");
  const std::string speak_text = runtime.args().GetString("speak", "");
  const bool no_command = capture_path.empty() && play_path.empty() && !start && !stop && !status &&
                          !transcripts && speak_text.empty();
  const bool list = runtime.args().HasFlag("list") || no_command;
  const int command_count = static_cast<int>(list) + static_cast<int>(!capture_path.empty()) +
                            static_cast<int>(!play_path.empty()) + static_cast<int>(start) +
                            static_cast<int>(stop) + static_cast<int>(status) +
                            static_cast<int>(transcripts) + static_cast<int>(!speak_text.empty());
  if (command_count != 1) {
    PrintUsage();
    return 2;
  }
  if (output_format == cockpit::diagnostics::OutputFormat::kJson &&
      (list || !capture_path.empty() || !play_path.empty() || !speak_text.empty())) {
    cockpit::diagnostics::WriteJsonError(
        "invalid_arguments", "JSON output is supported for control status and transcripts",
        &std::cerr);
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }
  if (list) {
    return ListDevices(runtime);
  }
  if (!capture_path.empty()) {
    return Capture(runtime, capture_path);
  }
  if (!play_path.empty()) {
    return Play(runtime, play_path);
  }
  if (transcripts) {
    return Transcripts(runtime, output_format);
  }
  if (!speak_text.empty()) {
    return Speak(runtime, speak_text);
  }
  return Control(runtime, start ? "start" : (stop ? "stop" : "status"), output_format);
}
