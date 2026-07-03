#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "modules/audio/frames/pcm_format.h"
#include "modules/audio/wav/wav_file.h"

namespace {

bool Check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  cockpit::audio::PcmFormat format;
  format.sample_rate_hz = 16000;
  format.channels = 2;
  format.frame_ms = 20;

  std::string error;
  if (!Check(format.IsValid(&error), error) ||
      !Check(format.BytesPerSample() == 2, "unexpected bytes per sample") ||
      !Check(format.BytesPerFrame() == 4, "unexpected bytes per frame") ||
      !Check(format.FramesPerPeriod() == 320, "unexpected frames per period")) {
    return 1;
  }

  cockpit::audio::PcmFormat invalid = format;
  invalid.sample_rate_hz = 1000;
  if (!Check(!invalid.IsValid(&error), "invalid sample rate was accepted") ||
      !Check(error.find("sample_rate_hz") != std::string::npos,
             "invalid sample rate error lacks field name")) {
    return 1;
  }

  const std::filesystem::path wav_path = "audio_wav_test.wav";
  const std::filesystem::path invalid_path = "audio_wav_invalid.bin";
  const std::vector<std::int16_t> samples{-32768, 32767, -1000, 1000, 0, 42};
  if (!Check(cockpit::audio::WritePcm16Wav(wav_path.string(), format, samples, &error), error)) {
    return 1;
  }

  cockpit::audio::PcmBuffer decoded;
  if (!Check(cockpit::audio::ReadPcm16Wav(wav_path.string(), &decoded, &error), error) ||
      !Check(decoded.format.sample_rate_hz == format.sample_rate_hz, "WAV sample rate changed") ||
      !Check(decoded.format.channels == format.channels, "WAV channel count changed") ||
      !Check(decoded.samples == samples, "WAV samples changed") ||
      !Check(decoded.FrameCount() == 3, "unexpected WAV frame count")) {
    std::filesystem::remove(wav_path);
    return 1;
  }

  const std::vector<std::int16_t> incomplete_frame{1, 2, 3};
  if (!Check(!cockpit::audio::WritePcm16Wav(wav_path.string(), format, incomplete_frame, &error),
             "incomplete PCM frame was accepted")) {
    std::filesystem::remove(wav_path);
    return 1;
  }

  {
    std::ofstream invalid_file(invalid_path, std::ios::binary | std::ios::trunc);
    invalid_file << "not-a-wave";
  }
  if (!Check(!cockpit::audio::ReadPcm16Wav(invalid_path.string(), &decoded, &error),
             "invalid WAV file was accepted") ||
      !Check(!cockpit::audio::ReadPcm16Wav("missing-audio-file.wav", &decoded, &error),
             "missing WAV file was accepted")) {
    std::filesystem::remove(wav_path);
    std::filesystem::remove(invalid_path);
    return 1;
  }

  std::filesystem::remove(wav_path);
  std::filesystem::remove(invalid_path);
  return 0;
}
