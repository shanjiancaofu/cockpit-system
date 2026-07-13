#include <chrono>
#include <thread>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/library/agent/agent_runtime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "voice-interaction-service");
  cockpit::agent::AgentRuntime agent;
  if (!agent.Start(runtime.config_path(), runtime.args().HasFlag("enable"))) {
    runtime.MarkStopped();
    return 1;
  }
  while (!runtime.ShouldStop() && agent.Poll() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const int result = runtime.ShouldStop() ? 0 : agent.Poll();
  agent.Stop();
  runtime.MarkStopped();
  return result;
}
