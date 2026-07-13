#include <chrono>
#include <string>
#include <thread>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/library/transfer/transfer_runtime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "cockpit-gateway-service");
  const int samples = runtime.args().GetInt("samples", 0);
  const int max_hz = runtime.args().GetInt("max-hz", 10);
  cockpit::transfer::TransferRuntime transfer;
  if (!transfer.Start(runtime.config_path(), samples, max_hz)) {
    runtime.MarkStopped();
    return 1;
  }
  while (!runtime.ShouldStop() && transfer.Poll() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const int result = runtime.ShouldStop() ? 0 : transfer.Poll();
  transfer.Stop();
  runtime.MarkStopped();
  return result;
}
