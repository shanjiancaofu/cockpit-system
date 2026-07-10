#include "cockpit/core/ipc/shared_memory_region.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace cockpit {
namespace ipc {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::string SystemError(const std::string& operation) {
  return operation + " failed: " + std::strerror(errno);
}

bool ValidName(const std::string& name) {
  return name.size() > 1 && name.front() == '/' && name.find('/', 1) == std::string::npos;
}

}  // namespace

std::unique_ptr<SharedMemoryRegion> SharedMemoryRegion::Create(const std::string& name,
                                                               std::size_t size,
                                                               std::string* error) {
  if (!ValidName(name)) {
    AssignError(error, "shared memory name must be a single POSIX name beginning with '/'");
    return nullptr;
  }
  if (size == 0) {
    AssignError(error, "shared memory size must be positive");
    return nullptr;
  }

  const int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0660);
  if (fd < 0) {
    AssignError(error, SystemError("create shared memory"));
    return nullptr;
  }
  if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
    AssignError(error, SystemError("resize shared memory"));
    close(fd);
    shm_unlink(name.c_str());
    return nullptr;
  }
  void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    AssignError(error, SystemError("map shared memory"));
    close(fd);
    shm_unlink(name.c_str());
    return nullptr;
  }
  return std::unique_ptr<SharedMemoryRegion>(new SharedMemoryRegion(name, fd, mapping, size, true));
}

std::unique_ptr<SharedMemoryRegion> SharedMemoryRegion::Open(const std::string& name,
                                                             std::string* error) {
  if (!ValidName(name)) {
    AssignError(error, "shared memory name must be a single POSIX name beginning with '/'");
    return nullptr;
  }
  const int fd = shm_open(name.c_str(), O_RDWR, 0);
  if (fd < 0) {
    AssignError(error, SystemError("open shared memory"));
    return nullptr;
  }
  struct stat file_status {};
  if (fstat(fd, &file_status) != 0) {
    AssignError(error, SystemError("inspect shared memory"));
    close(fd);
    return nullptr;
  }
  if (file_status.st_size <= 0) {
    AssignError(error, "shared memory size must be positive");
    close(fd);
    return nullptr;
  }
  const auto size = static_cast<std::size_t>(file_status.st_size);
  void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    AssignError(error, SystemError("map shared memory"));
    close(fd);
    return nullptr;
  }
  return std::unique_ptr<SharedMemoryRegion>(
      new SharedMemoryRegion(name, fd, mapping, size, false));
}

bool SharedMemoryRegion::Unlink(const std::string& name, std::string* error) {
  if (!ValidName(name)) {
    AssignError(error, "shared memory name must be a single POSIX name beginning with '/'");
    return false;
  }
  if (shm_unlink(name.c_str()) != 0 && errno != ENOENT) {
    AssignError(error, SystemError("unlink shared memory"));
    return false;
  }
  return true;
}

SharedMemoryRegion::SharedMemoryRegion(std::string name, int fd, void* mapping, std::size_t size,
                                       bool owner)
    : name_(std::move(name)), fd_(fd), mapping_(mapping), size_(size), owner_(owner) {
}

SharedMemoryRegion::~SharedMemoryRegion() {
  if (mapping_ != nullptr) {
    munmap(mapping_, size_);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
  if (owner_ && !name_.empty()) {
    shm_unlink(name_.c_str());
  }
}

bool SharedMemoryRegion::TryLockExclusive(std::string* error) const {
  if (fd_ < 0) {
    AssignError(error, "shared memory region is not open");
    return false;
  }
  if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
    AssignError(error, SystemError("lock shared memory writer"));
    return false;
  }
  return true;
}

}  // namespace ipc
}  // namespace cockpit
