#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace cockpit {
namespace event {

enum class EventQueuePushResult {
  kAccepted,
  kClosed,
  kFull,
};

template <typename T>
class EventQueue {
 public:
  explicit EventQueue(std::size_t capacity) : capacity_(capacity) {
  }

  EventQueue(const EventQueue&) = delete;
  EventQueue& operator=(const EventQueue&) = delete;

  EventQueuePushResult Push(const T& event) {
    return Emplace(event);
  }
  EventQueuePushResult Push(T&& event) {
    return Emplace(std::move(event));
  }

  std::optional<T> TryPop() {
    std::lock_guard<std::mutex> lock(mutex_);
    return PopLocked();
  }

  std::optional<T> WaitPop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] {
      return closed_ || !queue_.empty();
    });
    return PopLocked();
  }

  template <typename Rep, typename Period>
  std::optional<T> WaitPopFor(const std::chrono::duration<Rep, Period>& timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [this] {
      return closed_ || !queue_.empty();
    });
    return PopLocked();
  }

  void Close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    closed_ = false;
    drop_count_ = 0;
  }

  std::size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  std::size_t capacity() const {
    return capacity_;
  }

  std::uint64_t DropCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return drop_count_;
  }

  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

 private:
  template <typename U>
  EventQueuePushResult Emplace(U&& event) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return EventQueuePushResult::kClosed;
      }
      if (capacity_ == 0 || queue_.size() >= capacity_) {
        ++drop_count_;
        return EventQueuePushResult::kFull;
      }
      queue_.emplace_back(std::forward<U>(event));
    }
    cv_.notify_one();
    return EventQueuePushResult::kAccepted;
  }

  std::optional<T> PopLocked() {
    if (queue_.empty()) {
      return std::nullopt;
    }
    T event = std::move(queue_.front());
    queue_.pop_front();
    return event;
  }

  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<T> queue_;
  bool closed_ = false;
  std::uint64_t drop_count_ = 0;
};

}  // namespace event
}  // namespace cockpit
