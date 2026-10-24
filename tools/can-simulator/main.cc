#include "can_simulator.h"

#include "cockpit/exchange/foundation/runtime/process_runtime.h"

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "can-simulator");
  const int result = cockpit::can_simulator::SimulateCan(runtime);
  runtime.MarkStopped();
  return result;
}
