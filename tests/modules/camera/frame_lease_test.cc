#include <atomic>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "cockpit/driver/camera/capture/native_argus_capture.h"

int main() {
  using namespace cockpit::camera;
  static_assert(!std::is_copy_constructible_v<FrameLease>);
  static_assert(std::is_nothrow_move_constructible_v<FrameLease>);
  static_assert(std::is_nothrow_move_assignable_v<FrameLease>);
  std::atomic<int> returned{0};
  NativeFrame metadata;
  metadata.dmabuf_fd = 42;
  metadata.capture_id = 7;
  metadata.sensor_timestamp_ns = 1234;
  FrameLease empty;
  assert(!empty);
  try {
    empty.frame();
    assert(false);
  } catch (const std::logic_error&) {
  }
  try {
    FrameLease invalid(metadata, {});
    assert(false);
  } catch (const std::invalid_argument&) {
  }
  {
    FrameLease first(metadata, [&] {
      ++returned;
    });
    assert(first.frame().capture_id == 7 && first.frame().sensor_timestamp_ns == 1234);
    assert(first.frame().source_clock == CameraTimestampClock::kUnknown);
    FrameLease moved(std::move(first));
    assert(!first && moved && returned == 0);
    FrameLease replaced(metadata, [&] {
      ++returned;
    });
    replaced = std::move(moved);
    assert(!moved && replaced && returned == 1);
    replaced.Reset();
    replaced.Reset();
    assert(returned == 2);
  }
  assert(returned == 2);
  {
    std::vector<FrameLease> leases;
    for (int i = 0; i < 1000; ++i)
      leases.emplace_back(metadata, [&]() noexcept {
        ++returned;
      });
    assert(returned == 2);
    std::thread consumer([owned = std::move(leases)]() mutable {
      owned.clear();
    });
    consumer.join();
    assert(returned == 1002);
  }
  struct Resource {
    explicit Resource(int& destroyed) : destroyed(destroyed) {
    }
    ~Resource() {
      ++destroyed;
    }
    int& destroyed;
  };
  int destroyed = 0;
  auto resource = std::make_shared<Resource>(destroyed);
  FrameLease survivor(metadata, [owner = resource]() noexcept {
  });
  resource.reset();
  assert(destroyed == 0);
  survivor.Reset();
  assert(destroyed == 1);

  NativeArgusCapture capture;  // Lazy; these tests never open hardware.
  assert(capture.stats().ResourcesBalanced() && !capture.running());
  assert(capture.Stop() == NativeStopStatus::kStopped);
  std::string error;
  assert(capture.Acquire(&empty, 10, &error) == NativeAcquireStatus::kStopped);
  assert(capture.Acquire(nullptr, 10, &error) == NativeAcquireStatus::kError);
  assert(capture.Acquire(&empty, 0, &error) == NativeAcquireStatus::kError);
  NativeCaptureConfig invalid;
  invalid.width = 0;
  assert(!capture.Start(invalid, &error) && !error.empty());
  if (!NativeArgusCapture::IsSupported()) {
    assert(!capture.Start({}, &error) && error.find("unavailable") != std::string::npos);
  }
  assert(capture.stats().ResourcesBalanced());
  return 0;
}
