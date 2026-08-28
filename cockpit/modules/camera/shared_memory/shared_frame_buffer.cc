#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"

#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "cockpit/core/ipc/shared_memory_region.h"

namespace cockpit {
namespace camera {
namespace {

constexpr std::uint32_t kMagic = 0x43414D46U;
constexpr std::uint32_t kVersion = 3;
constexpr std::size_t kSlotCount = 2;
constexpr std::size_t kAlignment = 64;

struct alignas(kAlignment) SharedHeader {
  std::uint32_t magic = kMagic;
  std::uint32_t version = kVersion;
  std::uint64_t mapping_size = 0;
  std::uint64_t slot_capacity = 0;
  std::atomic<std::uint32_t> initialized{0};
  std::atomic<std::uint32_t> active_slot{0};
  std::atomic<std::uint64_t> generation{0};
  std::uint64_t writer_pid = 0;
};

struct alignas(kAlignment) SharedSlot {
  pthread_mutex_t lock{};
  std::uint64_t generation = 0;
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ms = 0;
  std::int64_t source_timestamp_ns = 0;
  std::int64_t received_at_ns = 0;
  std::uint64_t payload_size = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride_bytes = 0;
  std::uint32_t format = 0;
  std::uint32_t source_clock = 0;
  std::uint32_t source_timestamp_flags = 0;
  std::uint32_t source_timestamp_valid = 0;
};

std::size_t AlignUp(std::size_t value) {
  return (value + kAlignment - 1U) & ~(kAlignment - 1U);
}

bool AlignUp(std::size_t value, std::size_t* result) {
  if (value > std::numeric_limits<std::size_t>::max() - (kAlignment - 1U)) {
    return false;
  }
  *result = AlignUp(value);
  return true;
}

bool SlotStride(std::size_t capacity, std::size_t* result) {
  std::size_t payload_size = 0;
  if (!AlignUp(capacity, &payload_size)) {
    return false;
  }
  const std::size_t header_size = AlignUp(sizeof(SharedSlot));
  if (payload_size > std::numeric_limits<std::size_t>::max() - header_size) {
    return false;
  }
  *result = header_size + payload_size;
  return true;
}

bool MappingSize(std::size_t capacity, std::size_t* result) {
  std::size_t slot_stride = 0;
  if (!SlotStride(capacity, &slot_stride) ||
      slot_stride >
          (std::numeric_limits<std::size_t>::max() - AlignUp(sizeof(SharedHeader))) / kSlotCount) {
    return false;
  }
  *result = AlignUp(sizeof(SharedHeader)) + kSlotCount * slot_stride;
  return true;
}

SharedHeader* Header(void* mapping) {
  return static_cast<SharedHeader*>(mapping);
}

const SharedHeader* Header(const void* mapping) {
  return static_cast<const SharedHeader*>(mapping);
}

SharedSlot* Slot(void* mapping, std::size_t capacity, std::uint32_t index) {
  std::size_t slot_stride = 0;
  static_cast<void>(SlotStride(capacity, &slot_stride));
  auto* bytes = static_cast<std::uint8_t*>(mapping);
  return reinterpret_cast<SharedSlot*>(bytes + AlignUp(sizeof(SharedHeader)) + index * slot_stride);
}

std::uint8_t* Payload(SharedSlot* slot) {
  return reinterpret_cast<std::uint8_t*>(slot) + AlignUp(sizeof(SharedSlot));
}

const std::uint8_t* Payload(const SharedSlot* slot) {
  return reinterpret_cast<const std::uint8_t*>(slot) + AlignUp(sizeof(SharedSlot));
}

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

void ClearSlot(SharedSlot* slot) {
  slot->generation = 0;
  slot->sequence = 0;
  slot->timestamp_ms = 0;
  slot->source_timestamp_ns = 0;
  slot->received_at_ns = 0;
  slot->payload_size = 0;
  slot->width = 0;
  slot->height = 0;
  slot->stride_bytes = 0;
  slot->format = static_cast<std::uint32_t>(CameraPixelFormat::kUnknown);
  slot->source_clock = static_cast<std::uint32_t>(CameraTimestampClock::kUnknown);
  slot->source_timestamp_flags = 0;
  slot->source_timestamp_valid = 0;
}

bool InitializeSlot(SharedSlot* slot, std::string* error) {
  new (slot) SharedSlot();
  pthread_mutexattr_t attributes{};
  if (pthread_mutexattr_init(&attributes) != 0) {
    AssignError(error, "initialize shared frame lock attributes failed");
    return false;
  }
  const int shared_result = pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
  const int robust_result = shared_result == 0
                                ? pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST)
                                : shared_result;
  const int lock_result =
      robust_result == 0 ? pthread_mutex_init(&slot->lock, &attributes) : robust_result;
  pthread_mutexattr_destroy(&attributes);
  if (lock_result != 0) {
    AssignError(error, "initialize robust process-shared frame lock failed");
    return false;
  }
  ClearSlot(slot);
  return true;
}

bool LockSlot(SharedSlot* slot, bool* owner_dead, std::string* error) {
  *owner_dead = false;
  const int result = pthread_mutex_lock(&slot->lock);
  if (result == 0) {
    return true;
  }
  if (result == EOWNERDEAD) {
    ClearSlot(slot);
    if (pthread_mutex_consistent(&slot->lock) != 0) {
      pthread_mutex_unlock(&slot->lock);
      AssignError(error, "recover interrupted shared camera frame write failed");
      return false;
    }
    *owner_dead = true;
    return true;
  }
  AssignError(error, "lock shared camera frame failed");
  return false;
}

void InvalidateStaleMapping(ipc::SharedMemoryRegion* region) {
  if (region == nullptr || region->size() < AlignUp(sizeof(SharedHeader))) {
    return;
  }
  auto* header = Header(region->data());
  if (header->magic == kMagic) {
    header->initialized.store(0U, std::memory_order_release);
  }
}

bool ValidateMapping(const void* mapping, std::size_t mapping_size, std::size_t* capacity,
                     std::string* error) {
  if (mapping_size < AlignUp(sizeof(SharedHeader))) {
    AssignError(error, "shared frame mapping is too small");
    return false;
  }
  const auto* header = Header(mapping);
  if (header->initialized.load(std::memory_order_acquire) != 1U || header->magic != kMagic ||
      header->version != kVersion) {
    AssignError(error, "shared frame mapping is not initialized or has an unsupported version");
    return false;
  }
  std::size_t expected_mapping_size = 0;
  if (header->mapping_size != mapping_size || header->slot_capacity == 0 ||
      header->slot_capacity > std::numeric_limits<std::size_t>::max() ||
      !MappingSize(static_cast<std::size_t>(header->slot_capacity), &expected_mapping_size) ||
      expected_mapping_size != mapping_size) {
    AssignError(error, "shared frame mapping layout is invalid");
    return false;
  }
  *capacity = static_cast<std::size_t>(header->slot_capacity);
  return true;
}

}  // namespace

std::unique_ptr<SharedFrameWriter> SharedFrameWriter::Create(const SharedFrameBufferConfig& config,
                                                             std::string* error) {
  if (config.max_frame_bytes == 0) {
    AssignError(error, "shared frame capacity must be positive");
    return nullptr;
  }

  std::size_t mapping_size = 0;
  if (!MappingSize(config.max_frame_bytes, &mapping_size)) {
    AssignError(error, "shared frame capacity is too large");
    return nullptr;
  }
  auto region = ipc::SharedMemoryRegion::Create(config.name, mapping_size, error);
  if (region == nullptr) {
    std::string create_error = error == nullptr ? std::string() : *error;
    std::string recovery_error;
    auto stale_region = ipc::SharedMemoryRegion::Open(config.name, &recovery_error);
    if (stale_region == nullptr) {
      AssignError(error, create_error);
      return nullptr;
    }
    if (!stale_region->TryLockExclusive(&recovery_error)) {
      AssignError(error, "shared camera frame writer is already active: " + recovery_error);
      return nullptr;
    }
    InvalidateStaleMapping(stale_region.get());
    if (!ipc::SharedMemoryRegion::Unlink(config.name, &recovery_error)) {
      AssignError(error, recovery_error);
      return nullptr;
    }
    stale_region.reset();
    region = ipc::SharedMemoryRegion::Create(config.name, mapping_size, error);
    if (region == nullptr) {
      return nullptr;
    }
  }
  if (!region->TryLockExclusive(error)) {
    return nullptr;
  }
  void* mapping = region->data();

  std::memset(mapping, 0, mapping_size);
  auto* header = new (mapping) SharedHeader();
  header->mapping_size = mapping_size;
  header->slot_capacity = config.max_frame_bytes;
  header->writer_pid = static_cast<std::uint64_t>(getpid());
  for (std::uint32_t index = 0; index < kSlotCount; ++index) {
    if (!InitializeSlot(Slot(mapping, config.max_frame_bytes, index), error)) {
      return nullptr;
    }
  }
  header->initialized.store(1U, std::memory_order_release);
  return std::unique_ptr<SharedFrameWriter>(
      new SharedFrameWriter(std::move(region), config.max_frame_bytes));
}

SharedFrameWriter::SharedFrameWriter(std::unique_ptr<ipc::SharedMemoryRegion> region,
                                     std::size_t slot_capacity)
    : region_(std::move(region)), slot_capacity_(slot_capacity) {
}

SharedFrameWriter::~SharedFrameWriter() {
  if (region_ != nullptr) {
    auto* header = Header(region_->data());
    header->initialized.store(0U, std::memory_order_release);
    header->writer_pid = 0;
  }
}

bool SharedFrameWriter::Publish(CameraFrame frame) {
  if (!frame.IsValid() || frame.data.size() > slot_capacity_) {
    frames_rejected_.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }

  auto* header = Header(region_->data());
  const std::uint32_t active = header->active_slot.load(std::memory_order_acquire);
  const std::uint32_t target = (active + 1U) % kSlotCount;
  auto* slot = Slot(region_->data(), slot_capacity_, target);
  bool owner_dead = false;
  if (!LockSlot(slot, &owner_dead, nullptr)) {
    frames_rejected_.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }
  const std::uint64_t next_generation = header->generation.load(std::memory_order_relaxed) + 1U;
  slot->generation = next_generation;
  slot->sequence = frame.sequence;
  slot->timestamp_ms = frame.timestamp_ms;
  slot->source_timestamp_ns = frame.source_timestamp_ns;
  slot->received_at_ns = frame.received_at_ns;
  slot->payload_size = frame.data.size();
  slot->width = frame.width;
  slot->height = frame.height;
  slot->stride_bytes = frame.stride_bytes;
  slot->format = static_cast<std::uint32_t>(frame.format);
  slot->source_clock = static_cast<std::uint32_t>(frame.source_clock);
  slot->source_timestamp_flags = frame.source_timestamp_flags;
  slot->source_timestamp_valid = frame.source_timestamp_valid ? 1U : 0U;
  std::memcpy(Payload(slot), frame.data.data(), frame.data.size());
  pthread_mutex_unlock(&slot->lock);

  header->active_slot.store(target, std::memory_order_release);
  header->generation.store(next_generation, std::memory_order_release);
  frames_published_.fetch_add(1U, std::memory_order_relaxed);
  return true;
}

SharedFrameBufferStatus SharedFrameWriter::status() const {
  SharedFrameBufferStatus result;
  result.frames_published = frames_published_.load(std::memory_order_relaxed);
  result.frames_rejected = frames_rejected_.load(std::memory_order_relaxed);
  result.generation = Header(region_->data())->generation.load(std::memory_order_acquire);
  return result;
}

std::unique_ptr<SharedFrameReader> SharedFrameReader::Open(const std::string& name,
                                                           std::string* error) {
  auto region = ipc::SharedMemoryRegion::Open(name, error);
  if (region == nullptr) {
    return nullptr;
  }
  std::size_t capacity = 0;
  if (!ValidateMapping(region->data(), region->size(), &capacity, error)) {
    return nullptr;
  }
  return std::unique_ptr<SharedFrameReader>(new SharedFrameReader(std::move(region), capacity));
}

SharedFrameReader::SharedFrameReader(std::unique_ptr<ipc::SharedMemoryRegion> region,
                                     std::size_t slot_capacity)
    : region_(std::move(region)), slot_capacity_(slot_capacity) {
}

SharedFrameReader::~SharedFrameReader() = default;

bool SharedFrameReader::IsAvailable() const {
  return region_ != nullptr &&
         Header(region_->data())->initialized.load(std::memory_order_acquire) == 1U;
}

bool SharedFrameReader::ReadLatest(CameraFrame* frame, std::uint64_t* generation,
                                   std::string* error) const {
  if (frame == nullptr) {
    AssignError(error, "camera frame output must not be null");
    return false;
  }
  if (!IsAvailable()) {
    AssignError(error, "shared frame writer is not available");
    return false;
  }
  const auto* header = Header(region_->data());
  if (header->generation.load(std::memory_order_acquire) == 0) {
    AssignError(error, "shared frame buffer has no frame");
    return false;
  }
  const std::uint32_t active = header->active_slot.load(std::memory_order_acquire);
  const auto* slot = Slot(region_->data(), slot_capacity_, active);
  auto* mutable_slot = const_cast<SharedSlot*>(slot);  // NOLINT
  bool owner_dead = false;
  if (!LockSlot(mutable_slot, &owner_dead, error)) {
    return false;
  }
  if (owner_dead) {
    pthread_mutex_unlock(&mutable_slot->lock);
    AssignError(error, "recovered interrupted shared camera frame write");
    return false;
  }
  if (slot->payload_size > slot_capacity_) {
    pthread_mutex_unlock(&mutable_slot->lock);
    AssignError(error, "shared camera frame payload exceeds slot capacity");
    return false;
  }
  frame->sequence = slot->sequence;
  frame->timestamp_ms = slot->timestamp_ms;
  frame->source_timestamp_ns = slot->source_timestamp_ns;
  frame->received_at_ns = slot->received_at_ns;
  frame->width = slot->width;
  frame->height = slot->height;
  frame->stride_bytes = slot->stride_bytes;
  frame->format = static_cast<CameraPixelFormat>(slot->format);
  frame->source_clock = static_cast<CameraTimestampClock>(slot->source_clock);
  frame->source_timestamp_flags = slot->source_timestamp_flags;
  frame->source_timestamp_valid = slot->source_timestamp_valid != 0U;
  frame->data.assign(Payload(slot), Payload(slot) + slot->payload_size);
  const std::uint64_t frame_generation = slot->generation;
  pthread_mutex_unlock(&mutable_slot->lock);
  if (generation != nullptr) {
    *generation = frame_generation;
  }
  if (!frame->IsValid()) {
    AssignError(error, "shared camera frame layout or payload is invalid");
    return false;
  }
  return true;
}

}  // namespace camera
}  // namespace cockpit
