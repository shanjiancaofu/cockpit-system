#include "modules/camera/shared_memory/shared_frame_buffer.h"

#include <pthread.h>

#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "core/ipc/shared_memory_region.h"

namespace cockpit {
namespace camera {
namespace {

constexpr std::uint32_t kMagic = 0x43414D46U;
constexpr std::uint32_t kVersion = 1;
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
};

struct alignas(kAlignment) SharedSlot {
  pthread_rwlock_t lock{};
  std::uint64_t generation = 0;
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ms = 0;
  std::uint64_t payload_size = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride_bytes = 0;
  std::uint32_t format = 0;
};

std::size_t AlignUp(std::size_t value) {
  return (value + kAlignment - 1U) & ~(kAlignment - 1U);
}

std::size_t SlotStride(std::size_t capacity) {
  return AlignUp(sizeof(SharedSlot)) + AlignUp(capacity);
}

std::size_t MappingSize(std::size_t capacity) {
  return AlignUp(sizeof(SharedHeader)) + kSlotCount * SlotStride(capacity);
}

SharedHeader* Header(void* mapping) {
  return static_cast<SharedHeader*>(mapping);
}

const SharedHeader* Header(const void* mapping) {
  return static_cast<const SharedHeader*>(mapping);
}

SharedSlot* Slot(void* mapping, std::size_t capacity, std::uint32_t index) {
  auto* bytes = static_cast<std::uint8_t*>(mapping);
  return reinterpret_cast<SharedSlot*>(bytes + AlignUp(sizeof(SharedHeader)) +
                                       index * SlotStride(capacity));
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

bool InitializeSlot(SharedSlot* slot, std::string* error) {
  new (slot) SharedSlot();
  pthread_rwlockattr_t attributes{};
  if (pthread_rwlockattr_init(&attributes) != 0) {
    AssignError(error, "initialize shared frame lock attributes failed");
    return false;
  }
  const int shared_result = pthread_rwlockattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
  const int lock_result =
      shared_result == 0 ? pthread_rwlock_init(&slot->lock, &attributes) : shared_result;
  pthread_rwlockattr_destroy(&attributes);
  if (lock_result != 0) {
    AssignError(error, "initialize process-shared frame lock failed");
    return false;
  }
  return true;
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
  if (header->mapping_size != mapping_size || header->slot_capacity == 0 ||
      header->slot_capacity > std::numeric_limits<std::size_t>::max() ||
      MappingSize(static_cast<std::size_t>(header->slot_capacity)) != mapping_size) {
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

  const std::size_t mapping_size = MappingSize(config.max_frame_bytes);
  auto region = ipc::SharedMemoryRegion::Create(config.name, mapping_size, error);
  if (region == nullptr) {
    return nullptr;
  }
  void* mapping = region->data();

  std::memset(mapping, 0, mapping_size);
  auto* header = new (mapping) SharedHeader();
  header->mapping_size = mapping_size;
  header->slot_capacity = config.max_frame_bytes;
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
    Header(region_->data())->initialized.store(0U, std::memory_order_release);
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
  if (pthread_rwlock_wrlock(&slot->lock) != 0) {
    frames_rejected_.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }
  const std::uint64_t next_generation = header->generation.load(std::memory_order_relaxed) + 1U;
  slot->generation = next_generation;
  slot->sequence = frame.sequence;
  slot->timestamp_ms = frame.timestamp_ms;
  slot->payload_size = frame.data.size();
  slot->width = frame.width;
  slot->height = frame.height;
  slot->stride_bytes = frame.stride_bytes;
  slot->format = static_cast<std::uint32_t>(frame.format);
  std::memcpy(Payload(slot), frame.data.data(), frame.data.size());
  pthread_rwlock_unlock(&slot->lock);

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

bool SharedFrameReader::ReadLatest(CameraFrame* frame, std::uint64_t* generation,
                                   std::string* error) const {
  if (frame == nullptr) {
    AssignError(error, "camera frame output must not be null");
    return false;
  }
  const auto* header = Header(region_->data());
  if (header->generation.load(std::memory_order_acquire) == 0) {
    AssignError(error, "shared frame buffer has no frame");
    return false;
  }
  const std::uint32_t active = header->active_slot.load(std::memory_order_acquire);
  const auto* slot = Slot(region_->data(), slot_capacity_, active);
  auto* mutable_slot = const_cast<SharedSlot*>(slot);
  if (pthread_rwlock_rdlock(&mutable_slot->lock) != 0) {
    AssignError(error, "lock shared camera frame for reading failed");
    return false;
  }
  if (slot->payload_size > slot_capacity_) {
    pthread_rwlock_unlock(&mutable_slot->lock);
    AssignError(error, "shared camera frame payload exceeds slot capacity");
    return false;
  }
  frame->sequence = slot->sequence;
  frame->timestamp_ms = slot->timestamp_ms;
  frame->width = slot->width;
  frame->height = slot->height;
  frame->stride_bytes = slot->stride_bytes;
  frame->format = static_cast<CameraPixelFormat>(slot->format);
  frame->data.assign(Payload(slot), Payload(slot) + slot->payload_size);
  const std::uint64_t frame_generation = slot->generation;
  pthread_rwlock_unlock(&mutable_slot->lock);
  if (generation != nullptr) {
    *generation = frame_generation;
  }
  return frame->IsValid();
}

}  // namespace camera
}  // namespace cockpit
