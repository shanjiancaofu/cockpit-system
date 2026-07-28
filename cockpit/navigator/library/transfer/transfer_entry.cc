#include <memory>
#include <utility>

#include "cockpit/library/transfer/transfer_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::transfer::TransferRuntime> g_transfer;

int Start(const char* config_path) {
  if (config_path == nullptr || g_transfer != nullptr) {
    return 1;
  }
  auto transfer = std::make_unique<cockpit::transfer::TransferRuntime>();
  if (!transfer->Start(config_path)) {
    return 1;
  }
  g_transfer = std::move(transfer);
  return 0;
}

void Stop() {
  g_transfer.reset();
}

int Poll() {
  return g_transfer == nullptr ? 1 : g_transfer->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "transfer", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
