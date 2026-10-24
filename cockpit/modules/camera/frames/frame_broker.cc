#include "cockpit/modules/camera/frames/frame_broker.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cockpit::camera {
namespace detail {
struct FrameEpoch {
  explicit FrameEpoch(std::uint64_t id) : id(id) {
  }
  const std::uint64_t id;
  std::atomic<bool> open{true};
  std::atomic<std::size_t> retained{0};
};
struct SharedNativeFrame {
  SharedNativeFrame(FrameLease lease, std::shared_ptr<FrameEpoch> epoch)
      : lease(std::move(lease)), epoch(std::move(epoch)) {
  }
  ~SharedNativeFrame() {
    lease.Reset();
    --epoch->retained;
  }
  FrameLease lease;
  std::shared_ptr<FrameEpoch> epoch;
};
}  // namespace detail

SharedFrameLease::SharedFrameLease(std::shared_ptr<const detail::SharedNativeFrame> frame)
    : frame_(std::move(frame)) {
}
SharedFrameLease::operator bool() const noexcept {
  return frame_ && frame_->epoch->open.load();
}
const NativeFrame& SharedFrameLease::frame() const {
  if (!*this) throw std::logic_error("empty or revoked shared native frame");
  return frame_->lease.frame();
}
void SharedFrameLease::Reset() noexcept {
  frame_.reset();
}

struct NativeFrameBroker::Impl {
  explicit Impl(FrameBrokerConfig config) : config(config) {
  }
  const FrameBrokerConfig config;
  mutable std::mutex mutex;
  std::condition_variable ready;
  std::shared_ptr<detail::FrameEpoch> epoch;
  std::uint64_t last_session = 0;
  FrameBrokerStats counters;
  std::deque<SharedFrameLease> preview, transform;
  using Retired = std::vector<SharedFrameLease>;
  void Evict(std::deque<SharedFrameLease>& queue, FrameQueueStats& stats, Retired& retired) {
    retired.push_back(std::move(queue.front()));
    queue.pop_front();
    ++stats.dropped;
  }
  void EvictAll(Retired& retired) {
    while (!preview.empty()) Evict(preview, counters.preview, retired);
    while (!transform.empty()) Evict(transform, counters.transform, retired);
  }
  SharedFrameLease Pop(FrameConsumer consumer) {
    auto& queue = consumer == FrameConsumer::kPreview ? preview : transform;
    auto& stats = consumer == FrameConsumer::kPreview ? counters.preview : counters.transform;
    if (queue.empty()) return {};
    auto frame = std::move(queue.front());
    queue.pop_front();
    ++stats.dequeued;
    return frame;
  }
};

NativeFrameBroker::NativeFrameBroker(FrameBrokerConfig config) {
  if (config.pool_size < 2 || config.pool_size > 32 || config.transform_capacity == 0 ||
      config.transform_capacity >= config.pool_size || config.capture_reserve == 0 ||
      config.capture_reserve >= config.pool_size)
    throw std::invalid_argument("invalid broker pool/queue capacity");
  impl_ = std::make_unique<Impl>(config);
}
NativeFrameBroker::~NativeFrameBroker() {
  Close();
}
bool NativeFrameBroker::BeginSession(std::uint64_t session_id) {
  auto next = std::make_shared<detail::FrameEpoch>(session_id);
  Impl::Retired retired;
  retired.reserve(impl_->config.transform_capacity + 1);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!session_id || session_id <= impl_->last_session) return false;
    if (impl_->epoch) impl_->epoch->open = false;
    impl_->EvictAll(retired);
    impl_->last_session = session_id;
    impl_->epoch = std::move(next);
  }
  impl_->ready.notify_all();
  // Retire outside the mutex: the last lease may run deferred Runtime shutdown.
  return true;
}
FramePublishStatus NativeFrameBroker::Publish(FrameLease lease) {
  if (!lease || lease.frame().dmabuf_fd < 0 || !lease.frame().session_id || !lease.frame().width ||
      !lease.frame().height || lease.frame().format != CameraPixelFormat::kNv12) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->counters.rejected_invalid;
    return FramePublishStatus::kInvalidFrame;
  }
  const auto session = lease.frame().session_id;
  std::shared_ptr<detail::FrameEpoch> epoch;
  SharedFrameLease shared;  // Outlives the lock, including allocation-failure unwinding.
  Impl::Retired retired;
  retired.reserve(impl_->config.transform_capacity + 1);
  std::unique_lock<std::mutex> lock(impl_->mutex);
  if (!impl_->epoch || !impl_->epoch->open || impl_->epoch->id != session) {
    ++impl_->counters.rejected_session;
    return FramePublishStatus::kWrongSession;
  }
  epoch = impl_->epoch;
  // Release obsolete queue entries before reserving budget for this frame.
  if (!impl_->preview.empty()) impl_->Evict(impl_->preview, impl_->counters.preview, retired);
  if (impl_->transform.size() == impl_->config.transform_capacity)
    impl_->Evict(impl_->transform, impl_->counters.transform, retired);
  if (epoch->retained >= impl_->config.pool_size - impl_->config.capture_reserve) {
    const auto before = retired.size();
    impl_->EvictAll(retired);
    impl_->counters.budget_evictions += retired.size() - before;
  }
  lock.unlock();
  retired.clear();
  lock.lock();
  if (impl_->epoch != epoch || !epoch->open) {
    ++impl_->counters.rejected_session;
    return FramePublishStatus::kWrongSession;
  }
  if (epoch->retained >= impl_->config.pool_size - impl_->config.capture_reserve) {
    ++impl_->counters.budget_dropped;
    return FramePublishStatus::kBudgetDropped;
  }
  ++epoch->retained;  // Include reservations in budget, including concurrent publishers.
  impl_->counters.peak_retained_frames =
      std::max(impl_->counters.peak_retained_frames, epoch->retained.load());
  lock.unlock();
  try {
    shared = SharedFrameLease(std::make_shared<detail::SharedNativeFrame>(std::move(lease), epoch));
  } catch (...) {
    --epoch->retained;
    throw;
  }
  lock.lock();
  if (impl_->epoch != epoch || !epoch->open) {
    ++impl_->counters.rejected_session;
    lock.unlock();
    return FramePublishStatus::kWrongSession;
  }
  // Another producer may have filled queues while the lease was being wrapped.
  if (!impl_->preview.empty()) impl_->Evict(impl_->preview, impl_->counters.preview, retired);
  if (impl_->transform.size() == impl_->config.transform_capacity)
    impl_->Evict(impl_->transform, impl_->counters.transform, retired);
  impl_->preview.push_back(shared);
  impl_->transform.push_back(shared);
  ++impl_->counters.published;
  ++impl_->counters.preview.enqueued;
  ++impl_->counters.transform.enqueued;
  impl_->counters.preview.high_watermark =
      std::max(impl_->counters.preview.high_watermark, impl_->preview.size());
  impl_->counters.transform.high_watermark =
      std::max(impl_->counters.transform.high_watermark, impl_->transform.size());
  lock.unlock();
  impl_->ready.notify_all();
  return FramePublishStatus::kPublished;
}
SharedFrameLease NativeFrameBroker::TryPop(FrameConsumer consumer) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->Pop(consumer);
}
SharedFrameLease NativeFrameBroker::WaitPop(FrameConsumer consumer, int timeout_ms) {
  if (timeout_ms <= 0) return TryPop(consumer);
  std::unique_lock<std::mutex> lock(impl_->mutex);
  const auto epoch = impl_->epoch;
  impl_->ready.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
    return impl_->epoch != epoch || !epoch || !epoch->open ||
           !(consumer == FrameConsumer::kPreview ? impl_->preview : impl_->transform).empty();
  });
  if (!epoch || impl_->epoch != epoch || !epoch->open) return {};
  return impl_->Pop(consumer);
}
void NativeFrameBroker::Close() {
  Impl::Retired retired;
  retired.reserve(impl_->config.transform_capacity + 1);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->epoch) impl_->epoch->open = false;
    impl_->EvictAll(retired);
  }
  impl_->ready.notify_all();
}
FrameBrokerStats NativeFrameBroker::stats() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  auto stats = impl_->counters;
  stats.session_id = impl_->epoch ? impl_->epoch->id : 0;
  stats.open = impl_->epoch && impl_->epoch->open;
  stats.retained_frames = impl_->epoch ? impl_->epoch->retained.load() : 0;
  stats.preview.depth = impl_->preview.size();
  stats.transform.depth = impl_->transform.size();
  return stats;
}
}  // namespace cockpit::camera
