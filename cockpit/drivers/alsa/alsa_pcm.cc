#include "cockpit/drivers/alsa/alsa_pcm.h"

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace cockpit {
namespace audio {
namespace {

bool Fail(const std::string& message, std::string* error) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

std::string AlsaError(const std::string& operation, int code) {
  return operation + ": " + snd_strerror(code);
}

std::string HintString(void* hint, const char* key) {
  char* value = snd_device_name_get_hint(hint, key);
  if (value == nullptr) {
    return {};
  }
  std::string result(value);
  std::free(value);
  return result;
}

AlsaDeviceIo ParseIo(const std::string& io) {
  if (io == "Input") {
    return AlsaDeviceIo::kInput;
  }
  if (io == "Output") {
    return AlsaDeviceIo::kOutput;
  }
  if (io.empty()) {
    return AlsaDeviceIo::kDuplex;
  }
  return AlsaDeviceIo::kUnknown;
}

}  // namespace

AlsaPcm::~AlsaPcm() {
  Close();
}

AlsaPcm::AlsaPcm(AlsaPcm&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      format_(other.format_),
      direction_(other.direction_) {
}

AlsaPcm& AlsaPcm::operator=(AlsaPcm&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = std::exchange(other.handle_, nullptr);
    format_ = other.format_;
    direction_ = other.direction_;
  }
  return *this;
}

std::vector<AlsaDeviceInfo> AlsaPcm::ListDevices(std::string* error) {
  void** hints = nullptr;
  const int result = snd_device_name_hint(-1, "pcm", &hints);
  if (result < 0) {
    Fail(AlsaError("failed to enumerate ALSA PCM devices", result), error);
    return {};
  }

  std::vector<AlsaDeviceInfo> devices;
  for (void** current = hints; current != nullptr && *current != nullptr; ++current) {
    AlsaDeviceInfo device;
    device.name = HintString(*current, "NAME");
    device.description = HintString(*current, "DESC");
    device.io = ParseIo(HintString(*current, "IOID"));
    if (!device.name.empty()) {
      devices.push_back(std::move(device));
    }
  }
  snd_device_name_free_hint(hints);
  std::sort(devices.begin(), devices.end(), [](const auto& left, const auto& right) {
    if (left.name == right.name) {
      return static_cast<int>(left.io) < static_cast<int>(right.io);
    }
    return left.name < right.name;
  });
  return devices;
}

bool AlsaPcm::Open(const std::string& device, PcmDirection direction, const PcmFormat& format,
                   std::string* error) {
  Close();
  std::string format_error;
  if (!format.IsValid(&format_error)) {
    return Fail("invalid ALSA PCM format: " + format_error, error);
  }

  const snd_pcm_stream_t stream =
      direction == PcmDirection::kCapture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
  const int open_mode = direction == PcmDirection::kCapture ? SND_PCM_NONBLOCK : 0;
  int result = snd_pcm_open(&handle_, device.c_str(), stream, open_mode);
  if (result < 0) {
    handle_ = nullptr;
    return Fail(AlsaError("failed to open ALSA device " + device, result), error);
  }

  snd_pcm_hw_params_t* params = nullptr;
  snd_pcm_hw_params_alloca(&params);
  if ((result = snd_pcm_hw_params_any(handle_, params)) < 0 ||
      (result = snd_pcm_hw_params_set_access(handle_, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
      (result = snd_pcm_hw_params_set_format(handle_, params, SND_PCM_FORMAT_S16_LE)) < 0 ||
      (result = snd_pcm_hw_params_set_channels(handle_, params,
                                               static_cast<unsigned int>(format.channels))) < 0) {
    const std::string message = AlsaError("failed to configure ALSA hardware parameters", result);
    Close();
    return Fail(message, error);
  }

  unsigned int sample_rate = static_cast<unsigned int>(format.sample_rate_hz);
  int direction_hint = 0;
  result = snd_pcm_hw_params_set_rate_near(handle_, params, &sample_rate, &direction_hint);
  if (result < 0 || sample_rate != static_cast<unsigned int>(format.sample_rate_hz)) {
    const std::string message = result < 0
                                    ? AlsaError("failed to configure ALSA sample rate", result)
                                    : "ALSA device does not support the requested sample rate";
    Close();
    return Fail(message, error);
  }

  snd_pcm_uframes_t period_size = static_cast<snd_pcm_uframes_t>(format.FramesPerPeriod());
  snd_pcm_uframes_t buffer_size = period_size * 4U;
  direction_hint = 0;
  if ((result = snd_pcm_hw_params_set_period_size_near(handle_, params, &period_size,
                                                       &direction_hint)) < 0 ||
      (result = snd_pcm_hw_params_set_buffer_size_near(handle_, params, &buffer_size)) < 0 ||
      (result = snd_pcm_hw_params(handle_, params)) < 0 ||
      (result = snd_pcm_prepare(handle_)) < 0) {
    const std::string message = AlsaError("failed to activate ALSA hardware parameters", result);
    Close();
    return Fail(message, error);
  }

  format_ = format;
  direction_ = direction;
  return true;
}

CaptureResult AlsaPcm::PollReadFrames(std::int16_t* samples, std::size_t frame_capacity,
                                      int timeout_ms, const std::atomic_bool& stop_requested) {
  if (handle_ == nullptr || direction_ != PcmDirection::kCapture) {
    return {CaptureStatus::kDeviceError, 0, EBADF, "ALSA capture device is not open"};
  }
  if (samples == nullptr || frame_capacity == 0) {
    return {CaptureStatus::kDeviceError, 0, EINVAL, "ALSA capture buffer is invalid"};
  }
  if (stop_requested.load()) {
    return {CaptureStatus::kStopped, 0, 0, {}};
  }

  const int descriptor_count = snd_pcm_poll_descriptors_count(handle_);
  if (descriptor_count <= 0) {
    return {CaptureStatus::kDeviceError, 0, descriptor_count,
            "ALSA capture has no poll descriptors"};
  }
  std::vector<pollfd> descriptors(static_cast<std::size_t>(descriptor_count));
  int result = snd_pcm_poll_descriptors(handle_, descriptors.data(), descriptor_count);
  if (result < 0) {
    return {CaptureStatus::kDeviceError, 0, result,
            AlsaError("failed to get ALSA poll descriptors", result)};
  }

  result = poll(descriptors.data(), descriptors.size(), timeout_ms);
  if (stop_requested.load()) {
    return {CaptureStatus::kStopped, 0, 0, {}};
  }
  if (result == 0 || (result < 0 && errno == EINTR)) {
    return {CaptureStatus::kTimeout, 0, 0, {}};
  }
  if (result < 0) {
    return {CaptureStatus::kDeviceError, 0, errno,
            "ALSA capture poll failed: " + std::string(std::strerror(errno))};
  }

  unsigned short revents = 0;
  result =
      snd_pcm_poll_descriptors_revents(handle_, descriptors.data(), descriptor_count, &revents);
  if (result < 0) {
    return {CaptureStatus::kDeviceError, 0, result,
            AlsaError("failed to read ALSA poll events", result)};
  }
  if ((revents & POLLERR) != 0U) {
    const int state_error = snd_pcm_state(handle_) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
    result = snd_pcm_recover(handle_, state_error, 1);
    if (result >= 0) {
      return {CaptureStatus::kXrunRecovered, 0, 0, {}};
    }
    return {CaptureStatus::kDeviceError, 0, result,
            AlsaError("failed to recover ALSA capture", result)};
  }
  if ((revents & POLLIN) == 0U) {
    return {CaptureStatus::kTimeout, 0, 0, {}};
  }

  const snd_pcm_sframes_t frames =
      snd_pcm_readi(handle_, samples, static_cast<snd_pcm_uframes_t>(frame_capacity));
  if (frames > 0) {
    return {CaptureStatus::kOk, static_cast<std::size_t>(frames), 0, {}};
  }
  if (frames == -EAGAIN || frames == 0) {
    return {CaptureStatus::kTimeout, 0, 0, {}};
  }
  if (frames == -EPIPE || frames == -ESTRPIPE) {
    result = snd_pcm_recover(handle_, static_cast<int>(frames), 1);
    if (result >= 0) {
      return {CaptureStatus::kXrunRecovered, 0, 0, {}};
    }
    return {CaptureStatus::kDeviceError, 0, result,
            AlsaError("failed to recover ALSA capture", result)};
  }
  return {CaptureStatus::kDeviceError, 0, static_cast<int>(frames),
          AlsaError("ALSA capture failed", static_cast<int>(frames))};
}

bool AlsaPcm::WriteFrames(const std::int16_t* samples, std::size_t frame_count,
                          std::string* error) {
  if (handle_ == nullptr || direction_ != PcmDirection::kPlayback) {
    return Fail("ALSA playback device is not open", error);
  }
  if (samples == nullptr && frame_count > 0) {
    return Fail("ALSA playback buffer is null", error);
  }

  std::size_t completed = 0;
  while (completed < frame_count) {
    const snd_pcm_sframes_t result =
        snd_pcm_writei(handle_, samples + completed * static_cast<std::size_t>(format_.channels),
                       static_cast<snd_pcm_uframes_t>(frame_count - completed));
    if (result < 0) {
      if (!Recover(static_cast<int>(result), "ALSA playback failed", error)) {
        return false;
      }
      continue;
    }
    if (result == 0) {
      return Fail("ALSA playback made no progress", error);
    }
    completed += static_cast<std::size_t>(result);
  }
  return true;
}

bool AlsaPcm::Drain(std::string* error) {
  if (handle_ == nullptr || direction_ != PcmDirection::kPlayback) {
    return Fail("ALSA playback device is not open", error);
  }
  const int result = snd_pcm_drain(handle_);
  return result < 0 ? Fail(AlsaError("failed to drain ALSA playback", result), error) : true;
}

void AlsaPcm::Close() {
  if (handle_ != nullptr) {
    snd_pcm_drop(handle_);
    snd_pcm_close(handle_);
    handle_ = nullptr;
  }
}

bool AlsaPcm::Recover(int alsa_error, const std::string& operation, std::string* error) {
  const int result = snd_pcm_recover(handle_, alsa_error, 1);
  return result < 0 ? Fail(AlsaError(operation, result), error) : true;
}

const char* ToString(AlsaDeviceIo io) {
  switch (io) {
    case AlsaDeviceIo::kInput:
      return "input";
    case AlsaDeviceIo::kOutput:
      return "output";
    case AlsaDeviceIo::kDuplex:
      return "duplex";
    case AlsaDeviceIo::kUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace audio
}  // namespace cockpit
