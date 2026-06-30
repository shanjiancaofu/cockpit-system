#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace cockpit {
namespace ipc {

class SharedMemoryRegion {
 public:
  static std::unique_ptr<SharedMemoryRegion> Create(const std::string& name, std::size_t size,
                                                    std::string* error);
  static std::unique_ptr<SharedMemoryRegion> Open(const std::string& name, std::string* error);
  ~SharedMemoryRegion();

  SharedMemoryRegion(const SharedMemoryRegion&) = delete;
  SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;

  void* data() {
    return mapping_;
  }
  const void* data() const {
    return mapping_;
  }
  std::size_t size() const {
    return size_;
  }
  const std::string& name() const {
    return name_;
  }

 private:
  SharedMemoryRegion(std::string name, int fd, void* mapping, std::size_t size, bool owner);

  std::string name_;
  int fd_ = -1;
  void* mapping_ = nullptr;
  std::size_t size_ = 0;
  bool owner_ = false;
};

}  // namespace ipc
}  // namespace cockpit
