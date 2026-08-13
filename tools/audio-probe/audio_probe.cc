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
  const cockpit::audio::AlsaPcmFormat driver_format{format.sample_rate_hz, format.channels,
                                                    format.frame_ms};
  std::string error;
  if (!pcm.Open(device, cockpit::audio::PcmDirection::kCapture, driver_format, &error)) {
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
    if (result.status == cockpit::audio::AlsaReadStatus::kTimeout ||
        result.status == cockpit::audio::AlsaReadStatus::kXrunRecovered) {
      continue;
    }
    if (result.status == cockpit::audio::AlsaReadStatus::kStopped) {
      break;
    }
    if (result.status == cockpit::audio::AlsaReadStatus::kDeviceError) {
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
  const cockpit::audio::AlsaPcmFormat driver_format{buffer.format.sample_rate_hz,
                                                    buffer.format.channels, buffer.format.frame_ms};
  if (!pcm.Open(device, cockpit::audio::PcmDirection::kPlayback, driver_format, &error)) {
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

void PrintStatusText(const cockpit::proto::audio::AudioStatus& status) {
  const auto& metrics = status.metrics();
  std::cout << "state: " << CaptureStateName(status.capture_state()) << '\n'
            << "device: " << status.input_device() << '\n'
            << "format: " << status.sample_rate_hz() << " Hz, " << status.channels()
            << " channel(s), " << status.frame_ms() << " ms\n"
            << "frames read: " << metrics.pcm_frames_read() << '\n'
            << "frames published: " << metrics.audio_frames_published() << '\n'
            << "frames dropped: " << metrics.audio_frames_dropped() << '\n'
            << "input level: " << status.input_level_dbfs() << " dBFS\n"
            << "stream clients: " << metrics.stream_clients_accepted() << '\n'
            << "stream frames queued: " << metrics.stream_frames_queued() << '\n'
            << "stream frames sent: " << metrics.stream_frames_sent() << '\n'
            << "stream frames dropped: " << metrics.stream_frames_dropped() << '\n'
            << "stream disconnects: " << metrics.stream_client_disconnects() << '\n'
            << "playback queued: " << metrics.playback_queued() << '\n'
            << "playback played: " << metrics.playback_played() << '\n'
            << "playback failed: " << metrics.playback_failed() << '\n'
            << "playback dropped: " << metrics.playback_dropped() << '\n'
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

void PrintUsage() {
  std::cout << "usage:\n"
            << "  audio-probe --list [--config configs/development.yaml]\n"
            << "  audio-probe --capture output.wav [--seconds N] [--device NAME]\n"
            << "  audio-probe --play input.wav [--device NAME]\n"
            << "  audio-probe --start [--device NAME] [--address HOST:PORT]\n"
            << "  audio-probe --stop [--address HOST:PORT]\n"
            << "  audio-probe --status [--address HOST:PORT]\n";
  std::cout << "  control commands accept [--output text|json]\n";
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
  const bool no_command = capture_path.empty() && play_path.empty() && !start && !stop && !status;
  const bool list = runtime.args().HasFlag("list") || no_command;
  const int command_count = static_cast<int>(list) + static_cast<int>(!capture_path.empty()) +
                            static_cast<int>(!play_path.empty()) + static_cast<int>(start) +
                            static_cast<int>(stop) + static_cast<int>(status);
  if (command_count != 1) {
    PrintUsage();
    return 2;
  }
  if (output_format == cockpit::diagnostics::OutputFormat::kJson &&
      (list || !capture_path.empty() || !play_path.empty())) {
    cockpit::diagnostics::WriteJsonError("invalid_arguments",
                                         "JSON output is supported for control status", &std::cerr);
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
  return Control(runtime, start ? "start" : (stop ? "stop" : "status"), output_format);
}
