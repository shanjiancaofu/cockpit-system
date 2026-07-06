#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace cockpit {
namespace audio {

template <typename T, std::size_t Capacity>
class SpscRingBuffer {
  static_assert(Capacity >= 2, "SPSC ring buffer capacity must be at least two");
  static_assert((Capacity & (Capacity - 1U)) == 0U,
                "SPSC ring buffer capacity must be a power of two");
  static_assert(std::atomic<std::size_t>::is_always_lock_free,
                "SPSC indices must be lock-free on the target platform");
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "SPSC drop metrics must be lock-free on the target platform");

 public:
  SpscRingBuffer() = default;

  SpscRingBuffer(const SpscRingBuffer&) = delete;
  SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

  bool TryPush(const T& value) {
    return Emplace(value);
  }
  bool TryPush(T&& value) {
    return Emplace(std::move(value));
  }

  std::optional<T> TryPop() {
    const std::size_t read = read_index_.load(std::memory_order_relaxed);
    const std::size_t write = write_index_.load(std::memory_order_acquire);
    if (read == write) {
      return std::nullopt;
    }

    auto& slot = slots_[read & kIndexMask];
    std::optional<T> result(std::move(*slot));
    slot.reset();
    read_index_.store(read + 1U, std::memory_order_release);
    return result;
  }

  std::size_t Available() const {
    const std::size_t read = read_index_.load(std::memory_order_acquire);
    const std::size_t write = write_index_.load(std::memory_order_acquire);
    return write - read;
  }

  std::uint64_t DropCount() const {
    return drop_count_.load(std::memory_order_relaxed);
  }

  static constexpr std::size_t capacity() {
    return Capacity;
  }

  // Reset is only valid while both producer and consumer are stopped.
  void Reset() {
    for (auto& slot : slots_) {
      slot.reset();
    }
    write_index_.store(0, std::memory_order_relaxed);
    read_index_.store(0, std::memory_order_relaxed);
    drop_count_.store(0, std::memory_order_relaxed);
  }

 private:
  template <typename U>
  bool Emplace(U&& value) {
    const std::size_t write = write_index_.load(std::memory_order_relaxed);
    const std::size_t read = read_index_.load(std::memory_order_acquire);
    if (write - read >= Capacity) {
      drop_count_.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }

    slots_[write & kIndexMask].emplace(std::forward<U>(value));
    write_index_.store(write + 1U, std::memory_order_release);
    return true;
  }

  static constexpr std::size_t kIndexMask = Capacity - 1U;

  std::array<std::optional<T>, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> write_index_{0};
  alignas(64) std::atomic<std::size_t> read_index_{0};
  alignas(64) std::atomic<std::uint64_t> drop_count_{0};
};

}  // namespace audio
}  // namespace cockpit
