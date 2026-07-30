#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "agent/audio/audio_stream_client.h"
#include "cockpit/library/driver/audio/transport/audio_stream_publisher.h"
#include "cockpit/modules/audio/transport/audio_stream_protocol.h"

namespace {

using cockpit::agent::AudioStreamClient;
using cockpit::agent::AudioStreamReceiveStatus;
using cockpit::audio::AudioFrame;
using cockpit::audio::AudioFrameFlag;
using cockpit::audio::AudioStreamPublisher;

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

AudioFrame MakeFrame(std::uint64_t sequence, AudioFrameFlag flags = AudioFrameFlag::kNone) {
  AudioFrame::Samples samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = static_cast<std::int16_t>((sequence + index) & 0x7fffU);
  }
  samples.back() = -1234;
  return AudioFrame(sequence, static_cast<std::int64_t>(sequence * 20'000'000U), flags, samples);
}

template <typename Predicate>
bool WaitUntil(const Predicate& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

bool TestProtocolRoundTrip() {
  const AudioFrame original =
      MakeFrame(42, AudioFrameFlag::kDiscontinuity | AudioFrameFlag::kRecoveredFromXrun);
  auto packet = cockpit::audio::EncodeAudioStreamCaptureFrame(original);
  std::string error;
  auto decoded =
      cockpit::audio::DecodeAudioStreamCaptureFrame(packet.data(), packet.size(), &error);
  if (!Check(decoded.has_value(), error.c_str())) {
    return false;
  }
  if (!Check(decoded->sequence() == original.sequence(), "audio stream sequence changed") ||
      !Check(decoded->capture_time_ns() == original.capture_time_ns(),
             "audio stream timestamp changed") ||
      !Check(decoded->flags() == original.flags(), "audio stream flags changed") ||
      !Check(decoded->samples() == original.samples(), "audio stream samples changed")) {
    return false;
  }

  packet[4] = 0xffU;
  auto invalid =
      cockpit::audio::DecodeAudioStreamCaptureFrame(packet.data(), packet.size(), &error);
  return Check(!invalid.has_value(), "incompatible audio stream version was accepted") &&
         Check(!error.empty(), "invalid audio stream packet did not report an error");
}

bool TestPublisherClient() {
  std::array<char, 64> directory_template{};
  const std::string pattern = "/tmp/cockpit-audio-stream-test-XXXXXX";
  std::copy(pattern.begin(), pattern.end(), directory_template.begin());
  char* directory = mkdtemp(directory_template.data());
  if (!Check(directory != nullptr, "failed to create audio stream test directory")) {
    return false;
  }
  const std::string socket_path = std::string(directory) + "/capture.sock";

  AudioStreamPublisher publisher(4);
  std::string error;
  const bool started = publisher.Start(socket_path, &error);
  if (!Check(started, error.c_str())) {
    rmdir(directory);
    return false;
  }
  AudioStreamClient client;
  const bool connected = client.Connect(socket_path, &error);
  if (!Check(connected, error.c_str())) {
    publisher.Stop();
    rmdir(directory);
    return false;
  }
  const auto timeout = client.ReceiveFrame(10);
  if (!Check(timeout.status == AudioStreamReceiveStatus::kTimeout,
             "idle audio stream did not report a timeout")) {
    client.Close();
    publisher.Stop();
    rmdir(directory);
    return false;
  }

  const AudioFrame frame = MakeFrame(7);
  if (!Check(publisher.Publish(frame), "audio stream publisher rejected a frame")) {
    client.Close();
    publisher.Stop();
    rmdir(directory);
    return false;
  }
  auto received = client.ReceiveFrame(1000);
  const bool frame_ok =
      Check(received.status == AudioStreamReceiveStatus::kFrame, received.error.c_str()) &&
      Check(received.frame.has_value(), "audio stream client returned no frame") &&
      Check(received.frame->sequence() == frame.sequence(), "published sequence changed") &&
      Check(received.frame->samples() == frame.samples(), "published samples changed");
  const bool metrics_ready = WaitUntil([&publisher] {
    return publisher.metrics().frames_sent == 1;
  });
  const auto metrics = publisher.metrics();

  client.Close();
  publisher.Stop();

  AudioStreamPublisher dropping_publisher(2);
  const bool dropping_started = dropping_publisher.Start(socket_path, &error);
  if (!Check(dropping_started, error.c_str()) ||
      !Check(dropping_publisher.Publish(MakeFrame(8)), "failed to queue first overflow frame") ||
      !Check(dropping_publisher.Publish(MakeFrame(9)), "failed to queue second overflow frame") ||
      !Check(dropping_publisher.Publish(MakeFrame(10)), "failed to queue third overflow frame")) {
    dropping_publisher.Stop();
    rmdir(directory);
    return false;
  }
  AudioStreamClient dropping_client;
  const bool dropping_connected = dropping_client.Connect(socket_path, &error);
  if (!Check(dropping_connected, error.c_str())) {
    dropping_publisher.Stop();
    rmdir(directory);
    return false;
  }
  const auto first_after_drop = dropping_client.ReceiveFrame(1000);
  const bool drop_ok =
      Check(first_after_drop.status == AudioStreamReceiveStatus::kFrame,
            first_after_drop.error.c_str()) &&
      Check(first_after_drop.frame.has_value(), "overflow test returned no audio frame") &&
      Check(first_after_drop.frame->sequence() == 9,
            "publisher did not discard the oldest frame") &&
      Check(first_after_drop.frame->HasFlag(AudioFrameFlag::kDiscontinuity),
            "overflow frame did not report discontinuity") &&
      Check(first_after_drop.frame->HasFlag(AudioFrameFlag::kDroppedBefore),
            "overflow frame did not report a preceding drop") &&
      Check(dropping_publisher.metrics().frames_dropped == 1, "publisher drop metric is incorrect");
  dropping_client.Close();
  dropping_publisher.Stop();

  struct stat socket_status {};
  const bool socket_removed = lstat(socket_path.c_str(), &socket_status) < 0 && errno == ENOENT;
  rmdir(directory);
  return frame_ok && drop_ok && Check(metrics_ready, "sent frame metric did not settle") &&
         Check(metrics.clients_accepted == 1, "client acceptance was not counted") &&
         Check(metrics.frames_sent == 1, "sent frame was not counted") &&
         Check(socket_removed, "publisher left its Unix socket behind");
}

}  // namespace

int main() {
  return TestProtocolRoundTrip() && TestPublisherClient() ? 0 : 1;
}
