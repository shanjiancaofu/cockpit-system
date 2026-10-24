#include "cockpit/modules/camera/frames/frame_broker.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace cockpit::camera;
namespace {
FrameLease Frame(std::uint64_t session, std::uint32_t id, std::atomic<int>& returned) {
  NativeFrame frame;
  frame.session_id = session;
  frame.capture_id = id;
  frame.buffer_index = id % 6;
  frame.dmabuf_fd = 42;
  frame.width = 1920;
  frame.height = 1080;
  frame.sensor_timestamp_ns = id + 100;
  return FrameLease(frame, [&]() noexcept {
    ++returned;
  });
}
void Revoked(const SharedFrameLease& frame) {
  assert(!frame);
  try {
    frame.frame();
    assert(false);
  } catch (const std::logic_error&) {
  }
}
}  // namespace
int main() {
  try {
    NativeFrameBroker invalid({1, 1});
    assert(false);
  } catch (const std::invalid_argument&) {
  }
  std::atomic<int> returned{0};
  NativeFrameBroker broker(
      {6, 2, 1});  // Exercise the full five-frame budget independently of hardware.
  assert(!broker.BeginSession(0) && broker.BeginSession(1));
  assert(broker.Publish(Frame(1, 7, returned)) == FramePublishStatus::kPublished);
  auto preview = broker.TryPop(FrameConsumer::kPreview);
  auto transform = broker.TryPop(FrameConsumer::kTransform);
  auto copy = preview;
  assert(preview.frame().capture_id == 7 && transform.frame().capture_id == 7);
  assert(&preview.frame() == &transform.frame() && returned == 0);
  preview.Reset();
  transform.Reset();
  assert(returned == 0);
  copy.Reset();
  assert(returned == 1 && broker.stats().retained_frames == 0);

  for (int i = 1; i <= 3; ++i)
    assert(broker.Publish(Frame(1, i, returned)) == FramePublishStatus::kPublished);
  assert(returned == 2);  // Frame 1 left both bounded queues.
  preview = broker.TryPop(FrameConsumer::kPreview);
  auto second = broker.TryPop(FrameConsumer::kTransform);
  transform = broker.TryPop(FrameConsumer::kTransform);
  assert(preview.frame().capture_id == 3 && second.frame().capture_id == 2 &&
         transform.frame().capture_id == 3);
  assert(broker.stats().preview.high_watermark == 1 &&
         broker.stats().transform.high_watermark == 2);
  preview.Reset();
  transform.Reset();
  second.Reset();
  assert(returned == 4);

  std::vector<SharedFrameLease> held;
  for (int i = 0; i < 5; ++i) {
    assert(broker.Publish(Frame(1, i, returned)) == FramePublishStatus::kPublished);
    held.push_back(broker.TryPop(FrameConsumer::kPreview));
    broker.TryPop(FrameConsumer::kTransform).Reset();
  }
  const auto started = std::chrono::steady_clock::now();
  assert(broker.Publish(Frame(1, 100, returned)) == FramePublishStatus::kBudgetDropped);
  assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100));
  assert(broker.stats().retained_frames == 5 && broker.stats().budget_dropped == 1);
  broker.Close();
  for (const auto& frame : held) Revoked(frame);
  assert(!broker.BeginSession(1) && broker.BeginSession(2));
  assert(broker.Publish(Frame(1, 200, returned)) == FramePublishStatus::kWrongSession);
  assert(broker.Publish(Frame(2, 201, returned)) == FramePublishStatus::kPublished);
  auto fresh = broker.TryPop(FrameConsumer::kPreview);
  assert(fresh.frame().session_id == 2 && fresh.frame().capture_id == 201);
  held.clear();
  fresh.Reset();
  broker.Close();
  assert(returned == 12);

  assert(broker.BeginSession(3));
  std::promise<void> waiting;
  auto woke = std::async(std::launch::async, [&] {
    waiting.set_value();
    return static_cast<bool>(broker.WaitPop(FrameConsumer::kPreview, 5000));
  });
  waiting.get_future().wait();
  broker.Close();
  assert(woke.wait_for(std::chrono::seconds(1)) == std::future_status::ready && !woke.get());

  // Deferred native release callbacks may reenter stats; never call them under the broker mutex.
  assert(broker.BeginSession(4));
  NativeFrame metadata;
  metadata.session_id = 4;
  metadata.width = 2;
  metadata.height = 2;
  metadata.dmabuf_fd = 3;
  FrameLease reentrant(metadata, [&]() noexcept {
    broker.stats();
    ++returned;
  });
  assert(broker.Publish(std::move(reentrant)) == FramePublishStatus::kPublished);
  broker.Close();
  assert(returned == 13);

  assert(broker.BeginSession(5));
  std::atomic<bool> stop{false};
  auto consume = [&](FrameConsumer consumer) {
    std::uint32_t previous = 0;
    while (!stop) {
      auto frame = broker.WaitPop(consumer, 5);
      if (!frame) continue;
      try {
        const auto id = frame.frame().capture_id;
        assert(id > previous);
        previous = id;
      } catch (const std::logic_error&) {
      }  // Close revoked the generation between checks.
    }
  };
  std::thread fast(consume, FrameConsumer::kPreview), slow(consume, FrameConsumer::kTransform);
  for (int i = 1; i <= 1000; ++i) broker.Publish(Frame(5, i, returned));
  stop = true;
  broker.Close();
  fast.join();
  slow.join();
  assert(returned == 1013 && broker.stats().retained_frames == 0);
  assert(broker.stats().peak_retained_frames <= 5);
  return 0;
}
