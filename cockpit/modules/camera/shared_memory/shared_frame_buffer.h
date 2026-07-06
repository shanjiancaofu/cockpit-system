#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "cockpit/modules/camera/frames/camera_frame_sink.h"

namespace cockpit {
namespace ipc {
class SharedMemoryRegion;
}
}  // namespace cockpit

namespace cockpit {
namespace camera {

struct SharedFrameBufferConfig {
  std::string name = "/cockpit_camera_preview";
  std::size_t max_frame_bytes = std::size_t{8} * 1024U * 1024U;
};

struct SharedFrameBufferStatus {
  std::uint64_t frames_published = 0;
  std::uint64_t frames_rejected = 0;
  std::uint64_t generation = 0;
};

class SharedFrameWriter final : public CameraFrameSink {
 public:
  static std::unique_ptr<SharedFrameWriter> Create(const SharedFrameBufferConfig& config,
                                                   std::string* error);
  ~SharedFrameWriter() override;

  SharedFrameWriter(const SharedFrameWriter&) = delete;
  SharedFrameWriter& operator=(const SharedFrameWriter&) = delete;

  bool Publish(CameraFrame frame) override;
  SharedFrameBufferStatus status() const;

 private:
  SharedFrameWriter(std::unique_ptr<ipc::SharedMemoryRegion> region, std::size_t slot_capacity);

  std::unique_ptr<ipc::SharedMemoryRegion> region_;
  std::size_t slot_capacity_ = 0;
  std::atomic<std::uint64_t> frames_published_{0};
  std::atomic<std::uint64_t> frames_rejected_{0};
};

class SharedFrameReader {
 public:
  static std::unique_ptr<SharedFrameReader> Open(const std::string& name, std::string* error);
  ~SharedFrameReader();

  SharedFrameReader(const SharedFrameReader&) = delete;
  SharedFrameReader& operator=(const SharedFrameReader&) = delete;

  bool IsAvailable() const;
  bool ReadLatest(CameraFrame* frame, std::uint64_t* generation, std::string* error) const;
  std::size_t max_frame_bytes() const {
    return slot_capacity_;
  }

 private:
  SharedFrameReader(std::unique_ptr<ipc::SharedMemoryRegion> region, std::size_t slot_capacity);

  std::unique_ptr<ipc::SharedMemoryRegion> region_;
  std::size_t slot_capacity_ = 0;
};

}  // namespace camera
}  // namespace cockpit
