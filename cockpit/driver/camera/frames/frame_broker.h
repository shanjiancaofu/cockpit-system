#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "cockpit/driver/camera/frames/native_frame.h"

namespace cockpit::camera {
namespace detail {
struct SharedNativeFrame;
}

// Copies share ONE move-only FrameLease. Resetting a copy never revokes another.
// Close/BeginSession invalidate metadata access, but storage stays leased until
// the last copy dies (including synchronous device work already in progress).
class SharedFrameLease final {
 public:
  SharedFrameLease() = default;
  explicit operator bool() const noexcept;
  const NativeFrame& frame() const;
  void Reset() noexcept;

 private:
  friend class NativeFrameBroker;
  explicit SharedFrameLease(std::shared_ptr<const detail::SharedNativeFrame> frame);
  std::shared_ptr<const detail::SharedNativeFrame> frame_;
};

enum class FrameConsumer { kPreview, kTransform };
enum class FramePublishStatus { kPublished, kInvalidFrame, kWrongSession, kBudgetDropped };
struct FrameBrokerConfig {
  // Must match NativeCaptureConfig. Reserve covers acquisition AND Argus pipeline depth.
  std::size_t pool_size = 6;
  std::size_t transform_capacity = 2;
  std::size_t capture_reserve = 4;
};
struct FrameQueueStats {
  std::uint64_t enqueued = 0, dequeued = 0, dropped = 0;
  std::size_t depth = 0, high_watermark = 0;
};
struct FrameBrokerStats {
  std::uint64_t session_id = 0, published = 0, rejected_session = 0, rejected_invalid = 0;
  std::uint64_t budget_dropped = 0, budget_evictions = 0;
  std::size_t retained_frames = 0, peak_retained_frames = 0;
  bool open = false;
  FrameQueueStats preview, transform;
};

// In-process fan-out only. No pixel access, allocator, worker or user callback.
// Preview is latest-only; raw Transform FIFO drops oldest when full. In-flight
// handles count against pool_size-capture_reserve; queued frames may be evicted for budget.
// Control and queue operations are thread-safe. A caller must finish its device
// access before resetting the last handle (SYNC_TYPE_NONE).
class NativeFrameBroker final {
 public:
  explicit NativeFrameBroker(FrameBrokerConfig config = {});
  ~NativeFrameBroker();
  NativeFrameBroker(const NativeFrameBroker&) = delete;
  NativeFrameBroker& operator=(const NativeFrameBroker&) = delete;

  // Session IDs must increase; never reactivate a closed generation.
  bool BeginSession(std::uint64_t session_id);
  FramePublishStatus Publish(FrameLease lease);
  SharedFrameLease TryPop(FrameConsumer consumer);
  // Returns empty on timeout, Close or Session change. No old-session delivery.
  SharedFrameLease WaitPop(FrameConsumer consumer, int timeout_ms);
  void Close();
  FrameBrokerStats stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace cockpit::camera
