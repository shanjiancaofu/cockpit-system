#include "modules/audio/wav_file.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace cockpit {
namespace audio {
namespace {

constexpr std::uint32_t kMaxPcmDataBytes = 512U * 1024U * 1024U;

bool Fail(const std::string& message, std::string* error) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

void WriteU16(std::ostream* output, std::uint16_t value) {
  const std::array<char, 2> bytes{
      static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU)};
  output->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void WriteU32(std::ostream* output, std::uint32_t value) {
  const std::array<char, 4> bytes{
      static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
      static_cast<char>((value >> 16U) & 0xffU), static_cast<char>((value >> 24U) & 0xffU)};
  output->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool ReadExact(std::istream* input, char* data, std::size_t size) {
  input->read(data, static_cast<std::streamsize>(size));
  return input->gcount() == static_cast<std::streamsize>(size);
}

bool ReadU16(std::istream* input, std::uint16_t* value) {
  std::array<unsigned char, 2> bytes{};
  if (!ReadExact(input, reinterpret_cast<char*>(bytes.data()), bytes.size())) {
    return false;
  }
  *value = static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
  return true;
}

bool ReadU32(std::istream* input, std::uint32_t* value) {
  std::array<unsigned char, 4> bytes{};
  if (!ReadExact(input, reinterpret_cast<char*>(bytes.data()), bytes.size())) {
    return false;
  }
  *value = static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
  return true;
}

bool IsTag(const std::array<char, 4>& value, const char* expected) {
  return std::memcmp(value.data(), expected, value.size()) == 0;
}

}  // namespace

std::size_t PcmBuffer::FrameCount() const {
  if (format.channels <= 0) {
    return 0;
  }
  return samples.size() / static_cast<std::size_t>(format.channels);
}

bool WritePcm16Wav(const std::string& path, const PcmFormat& format,
                   const std::vector<std::int16_t>& samples, std::string* error) {
  std::string format_error;
  if (!format.IsValid(&format_error)) {
    return Fail("invalid WAV format: " + format_error, error);
  }
  if (samples.size() % static_cast<std::size_t>(format.channels) != 0U) {
    return Fail("sample count must contain complete PCM frames", error);
  }
  const std::size_t data_bytes = samples.size() * sizeof(std::int16_t);
  if (data_bytes > std::numeric_limits<std::uint32_t>::max() - 36U) {
    return Fail("PCM data is too large for a RIFF WAV file", error);
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return Fail("failed to open WAV output: " + path, error);
  }

  output.write("RIFF", 4);
  WriteU32(&output, static_cast<std::uint32_t>(36U + data_bytes));
  output.write("WAVE", 4);
  output.write("fmt ", 4);
  WriteU32(&output, 16);
  WriteU16(&output, 1);
  WriteU16(&output, static_cast<std::uint16_t>(format.channels));
  WriteU32(&output, static_cast<std::uint32_t>(format.sample_rate_hz));
  WriteU32(&output, static_cast<std::uint32_t>(format.sample_rate_hz) *
                        static_cast<std::uint32_t>(format.BytesPerFrame()));
  WriteU16(&output, static_cast<std::uint16_t>(format.BytesPerFrame()));
  WriteU16(&output, 16);
  output.write("data", 4);
  WriteU32(&output, static_cast<std::uint32_t>(data_bytes));
  for (const std::int16_t sample : samples) {
    WriteU16(&output, static_cast<std::uint16_t>(sample));
  }

  if (!output) {
    return Fail("failed to write WAV output: " + path, error);
  }
  return true;
}

bool ReadPcm16Wav(const std::string& path, PcmBuffer* buffer, std::string* error) {
  if (buffer == nullptr) {
    return Fail("WAV output buffer is null", error);
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Fail("failed to open WAV input: " + path, error);
  }

  std::array<char, 4> tag{};
  std::uint32_t riff_size = 0;
  if (!ReadExact(&input, tag.data(), tag.size()) || !IsTag(tag, "RIFF") ||
      !ReadU32(&input, &riff_size) || !ReadExact(&input, tag.data(), tag.size()) ||
      !IsTag(tag, "WAVE")) {
    return Fail("input is not a RIFF WAVE file", error);
  }
  (void)riff_size;

  bool found_format = false;
  bool found_data = false;
  std::uint16_t audio_format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t bits_per_sample = 0;
  std::vector<std::int16_t> samples;

  while (input && (!found_format || !found_data)) {
    std::uint32_t chunk_size = 0;
    if (!ReadExact(&input, tag.data(), tag.size()) || !ReadU32(&input, &chunk_size)) {
      break;
    }
    const std::streampos chunk_start = input.tellg();
    if (IsTag(tag, "fmt ")) {
      std::uint32_t byte_rate = 0;
      std::uint16_t block_align = 0;
      if (chunk_size < 16 || !ReadU16(&input, &audio_format) ||
          !ReadU16(&input, &channels) || !ReadU32(&input, &sample_rate) ||
          !ReadU32(&input, &byte_rate) || !ReadU16(&input, &block_align) ||
          !ReadU16(&input, &bits_per_sample)) {
        return Fail("invalid WAV format chunk", error);
      }
      (void)byte_rate;
      (void)block_align;
      found_format = true;
    } else if (IsTag(tag, "data")) {
      if (chunk_size > kMaxPcmDataBytes) {
        return Fail("WAV data chunk exceeds the supported size limit", error);
      }
      if (chunk_size % sizeof(std::int16_t) != 0U) {
        return Fail("WAV PCM16 data size is not sample aligned", error);
      }
      samples.resize(chunk_size / sizeof(std::int16_t));
      for (auto& sample : samples) {
        std::uint16_t value = 0;
        if (!ReadU16(&input, &value)) {
          return Fail("truncated WAV data chunk", error);
        }
        sample = static_cast<std::int16_t>(value);
      }
      found_data = true;
    }

    const std::streamoff padded_size = static_cast<std::streamoff>(chunk_size + (chunk_size & 1U));
    input.clear();
    input.seekg(chunk_start + padded_size);
  }

  if (!found_format || !found_data) {
    return Fail("WAV file must contain format and data chunks", error);
  }
  if (audio_format != 1 || bits_per_sample != 16) {
    return Fail("only uncompressed PCM16 WAV files are supported", error);
  }

  PcmFormat format;
  format.sample_rate_hz = static_cast<int>(sample_rate);
  format.channels = static_cast<int>(channels);
  std::string format_error;
  if (!format.IsValid(&format_error)) {
    return Fail("invalid WAV audio format: " + format_error, error);
  }
  if (samples.size() % static_cast<std::size_t>(format.channels) != 0U) {
    return Fail("WAV data does not contain complete PCM frames", error);
  }

  buffer->format = format;
  buffer->samples = std::move(samples);
  return true;
}

}  // namespace audio
}  // namespace cockpit
