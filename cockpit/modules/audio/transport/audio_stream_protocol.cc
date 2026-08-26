#include "cockpit/modules/audio/transport/audio_stream_protocol.h"

#include <cstring>
#include <utility>

namespace cockpit {
namespace audio {
namespace {

constexpr std::uint32_t kMagic = 0x41554443U;
constexpr std::uint16_t kCaptureFrameMessage = 1;
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kMessageOffset = 6;
constexpr std::size_t kPacketSizeOffset = 8;
constexpr std::size_t kFlagsOffset = 12;
constexpr std::size_t kSequenceOffset = 16;
constexpr std::size_t kCaptureTimeOffset = 24;
constexpr std::size_t kSampleRateOffset = 32;
constexpr std::size_t kChannelsOffset = 36;
constexpr std::size_t kSampleCountOffset = 40;
constexpr std::size_t kReservedOffset = 44;
constexpr std::size_t kSamplesOffset = kAudioStreamHeaderBytes;

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

void PutUint16(std::uint8_t* output, std::size_t offset, std::uint16_t value) {
  output[offset] = static_cast<std::uint8_t>(value & 0xffU);
  output[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void PutUint32(std::uint8_t* output, std::size_t offset, std::uint32_t value) {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    output[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU);
  }
}

void PutUint64(std::uint8_t* output, std::size_t offset, std::uint64_t value) {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    output[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU);
  }
}

std::uint16_t GetUint16(const std::uint8_t* input, std::size_t offset) {
  return static_cast<std::uint16_t>(input[offset]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(input[offset + 1U]) << 8U);
}

std::uint32_t GetUint32(const std::uint8_t* input, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    value |= static_cast<std::uint32_t>(input[offset + byte]) << (byte * 8U);
  }
  return value;
}

std::uint64_t GetUint64(const std::uint8_t* input, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    value |= static_cast<std::uint64_t>(input[offset + byte]) << (byte * 8U);
  }
  return value;
}

std::uint64_t SignedBits(std::int64_t value) {
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

std::int64_t SignedValue(std::uint64_t bits) {
  std::int64_t value = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::int16_t SignedSample(std::uint16_t bits) {
  std::int16_t value = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

}  // namespace

AudioStreamCapturePacket EncodeAudioStreamCaptureFrame(const AudioFrame& frame) {
  AudioStreamCapturePacket packet{};
  PutUint32(packet.data(), kMagicOffset, kMagic);
  PutUint16(packet.data(), kVersionOffset, kAudioStreamProtocolVersion);
  PutUint16(packet.data(), kMessageOffset, kCaptureFrameMessage);
  PutUint32(packet.data(), kPacketSizeOffset,
            static_cast<std::uint32_t>(kAudioStreamCapturePacketBytes));
  PutUint32(packet.data(), kFlagsOffset, static_cast<std::uint32_t>(frame.flags()));
  PutUint64(packet.data(), kSequenceOffset, frame.sequence());
  PutUint64(packet.data(), kCaptureTimeOffset, SignedBits(frame.capture_time_ns()));
  PutUint32(packet.data(), kSampleRateOffset, AudioFrame::kSampleRateHz);
  PutUint32(packet.data(), kChannelsOffset, AudioFrame::kChannels);
  PutUint32(packet.data(), kSampleCountOffset,
            static_cast<std::uint32_t>(AudioFrame::kSampleCount));
  PutUint32(packet.data(), kReservedOffset, 0);
  for (std::size_t index = 0; index < frame.samples().size(); ++index) {
    PutUint16(packet.data(), kSamplesOffset + index * sizeof(std::int16_t),
              static_cast<std::uint16_t>(frame.samples()[index]));
  }
  return packet;
}

std::optional<AudioFrame> DecodeAudioStreamCaptureFrame(const void* packet, std::size_t packet_size,
                                                        std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (packet == nullptr) {
    SetError(error, "audio stream packet is null");
    return std::nullopt;
  }
  if (packet_size != kAudioStreamCapturePacketBytes) {
    SetError(error, "audio stream packet size is invalid");
    return std::nullopt;
  }
  const auto* bytes = static_cast<const std::uint8_t*>(packet);
  if (GetUint32(bytes, kMagicOffset) != kMagic) {
    SetError(error, "audio stream packet magic is invalid");
    return std::nullopt;
  }
  if (GetUint16(bytes, kVersionOffset) != kAudioStreamProtocolVersion) {
    SetError(error, "audio stream protocol version is incompatible");
    return std::nullopt;
  }
  if (GetUint16(bytes, kMessageOffset) != kCaptureFrameMessage) {
    SetError(error, "audio stream message type is unsupported");
    return std::nullopt;
  }
  if (GetUint32(bytes, kPacketSizeOffset) != kAudioStreamCapturePacketBytes) {
    SetError(error, "audio stream packet declares an invalid size");
    return std::nullopt;
  }
  if (GetUint32(bytes, kSampleRateOffset) != AudioFrame::kSampleRateHz ||
      GetUint32(bytes, kChannelsOffset) != AudioFrame::kChannels ||
      GetUint32(bytes, kSampleCountOffset) != AudioFrame::kSampleCount) {
    SetError(error, "audio stream PCM format is incompatible");
    return std::nullopt;
  }

  constexpr std::uint32_t kKnownFlags =
      static_cast<std::uint32_t>(AudioFrameFlag::kDiscontinuity) |
      static_cast<std::uint32_t>(AudioFrameFlag::kRecoveredFromXrun) |
      static_cast<std::uint32_t>(AudioFrameFlag::kDroppedBefore);
  const std::uint32_t flags = GetUint32(bytes, kFlagsOffset);
  if ((flags & ~kKnownFlags) != 0U) {
    SetError(error, "audio stream frame flags are invalid");
    return std::nullopt;
  }

  AudioFrame::Samples samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = SignedSample(GetUint16(bytes, kSamplesOffset + index * sizeof(std::int16_t)));
  }
  return std::optional<AudioFrame>(std::in_place, GetUint64(bytes, kSequenceOffset),
                                   SignedValue(GetUint64(bytes, kCaptureTimeOffset)),
                                   static_cast<AudioFrameFlag>(flags), samples);
}

}  // namespace audio
}  // namespace cockpit
