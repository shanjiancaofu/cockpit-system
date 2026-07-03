#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <type_traits>

#include "modules/audio/frames/audio_frame.h"
#include "modules/audio/frames/spsc_ring_buffer.h"

namespace {

using cockpit::audio::AudioFrame;
using cockpit::audio::AudioFrameFlag;

AudioFrame MakeFrame(std::uint64_t sequence, AudioFrameFlag flags = AudioFrameFlag::kNone) {
  AudioFrame::Samples samples{};
  samples[0] = static_cast<std::int16_t>(sequence & 0x7fffU);
  return AudioFrame(sequence, static_cast<std::int64_t>(sequence * 20'000'000U), flags, samples);
}

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool TestFrameContract() {
  static_assert(AudioFrame::kSampleCount == 320);
  static_assert(!std::is_copy_assignable<AudioFrame>::value);
  static_assert(!std::is_move_assignable<AudioFrame>::value);

  const AudioFrame frame =
      MakeFrame(7, AudioFrameFlag::kDiscontinuity | AudioFrameFlag::kRecoveredFromXrun);
  return Check(frame.sequence() == 7, "frame sequence changed") &&
         Check(frame.capture_time_ns() == 140'000'000, "frame timestamp changed") &&
         Check(frame.samples()[0] == 7, "frame samples changed") &&
         Check(frame.HasFlag(AudioFrameFlag::kDiscontinuity), "discontinuity flag missing") &&
         Check(frame.HasFlag(AudioFrameFlag::kRecoveredFromXrun), "xrun flag missing") &&
         Check(!frame.HasFlag(AudioFrameFlag::kDroppedBefore), "unexpected dropped flag");
}

bool TestOrderWrapAndOverflow() {
  cockpit::audio::SpscRingBuffer<AudioFrame, 4> buffer;
  if (!Check(buffer.capacity() == 4, "ring capacity changed")) {
    return false;
  }
  for (std::uint64_t sequence = 0; sequence < 4; ++sequence) {
    if (!Check(buffer.TryPush(MakeFrame(sequence)), "ring rejected available slot")) {
      return false;
    }
  }
  if (!Check(!buffer.TryPush(MakeFrame(4)), "full ring accepted a frame") ||
      !Check(buffer.DropCount() == 1, "ring drop count did not increase") ||
      !Check(buffer.Available() == 4, "full ring size is incorrect")) {
    return false;
  }

  for (std::uint64_t sequence = 0; sequence < 2; ++sequence) {
    auto frame = buffer.TryPop();
    if (!Check(frame.has_value(), "ring lost a frame") ||
        !Check(frame->sequence() == sequence, "ring order changed before wrap")) {
      return false;
    }
  }
  if (!Check(buffer.TryPush(MakeFrame(4)), "ring failed first wrapped push") ||
      !Check(buffer.TryPush(MakeFrame(5)), "ring failed second wrapped push")) {
    return false;
  }
  for (std::uint64_t sequence = 2; sequence < 6; ++sequence) {
    auto frame = buffer.TryPop();
    if (!Check(frame.has_value(), "ring lost a wrapped frame") ||
        !Check(frame->sequence() == sequence, "ring order changed after wrap")) {
      return false;
    }
  }
  return Check(!buffer.TryPop().has_value(), "empty ring returned a frame") &&
         Check(buffer.Available() == 0, "empty ring size is incorrect");
}

bool TestConcurrentProducerConsumer() {
  constexpr std::uint64_t kFrameCount = 50'000;
  cockpit::audio::SpscRingBuffer<AudioFrame, 64> buffer;
  std::atomic_bool failed{false};

  std::thread producer([&] {
    for (std::uint64_t sequence = 0; sequence < kFrameCount; ++sequence) {
      while (!buffer.TryPush(MakeFrame(sequence))) {
        std::this_thread::yield();
      }
    }
  });
  std::thread consumer([&] {
    std::uint64_t expected = 0;
    while (expected < kFrameCount) {
      auto frame = buffer.TryPop();
      if (!frame.has_value()) {
        std::this_thread::yield();
        continue;
      }
      if (frame->sequence() != expected ||
          frame->samples()[0] != static_cast<std::int16_t>(expected & 0x7fffU)) {
        failed.store(true);
      }
      ++expected;
    }
  });

  producer.join();
  consumer.join();
  return Check(!failed.load(), "concurrent ring order or data changed") &&
         Check(buffer.Available() == 0, "concurrent ring did not drain");
}

}  // namespace

int main() {
  return TestFrameContract() && TestOrderWrapAndOverflow() && TestConcurrentProducerConsumer() ? 0
                                                                                               : 1;
}
