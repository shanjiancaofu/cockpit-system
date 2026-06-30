#include "core/ipc/shared_memory_region.h"

#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  const std::string name = "/cockpit_ipc_test_" + std::to_string(getpid());
  std::string error;
  auto owner = cockpit::ipc::SharedMemoryRegion::Create(name, 64, &error);
  if (!Check(owner != nullptr, "create shared memory region failed")) {
    std::cerr << error << '\n';
    return 1;
  }
  auto peer = cockpit::ipc::SharedMemoryRegion::Open(name, &error);
  if (!Check(peer != nullptr, "open shared memory region failed")) {
    std::cerr << error << '\n';
    return 1;
  }

  auto* owner_bytes = static_cast<std::uint8_t*>(owner->data());
  auto* peer_bytes = static_cast<std::uint8_t*>(peer->data());
  owner_bytes[17] = 0x5a;
  if (!Check(peer_bytes[17] == 0x5a, "shared memory mappings do not share data") ||
      !Check(owner->size() == 64 && peer->size() == 64, "shared memory size mismatch") ||
      !Check(owner->name() == name && peer->name() == name, "shared memory name mismatch")) {
    return 1;
  }

  return Check(cockpit::ipc::SharedMemoryRegion::Create("invalid", 64, &error) == nullptr,
               "invalid shared memory name was accepted")
             ? 0
             : 1;
}
