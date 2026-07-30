#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "cockpit/modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace audio {

constexpr std::uint16_t kAudioStreamProtocolVersion = 1;
constexpr std::size_t kAudioStreamHeaderBytes = 48;
constexpr std::size_t kAudioStreamCapturePacketBytes =
    kAudioStreamHeaderBytes + AudioFrame::kSampleCount * sizeof(std::int16_t);

using AudioStreamCapturePacket = std::array<std::uint8_t, kAudioStreamCapturePacketBytes>;

AudioStreamCapturePacket EncodeAudioStreamCaptureFrame(const AudioFrame& frame);

std::optional<AudioFrame> DecodeAudioStreamCaptureFrame(const void* packet, std::size_t packet_size,
                                                        std::string* error = nullptr);

}  // namespace audio
}  // namespace cockpit
