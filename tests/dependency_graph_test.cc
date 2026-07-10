#include <iostream>
#include <string>

#include "cockpit/core/runtime/DependencyGraph.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  cockpit::runtime::DependencyGraph graph;
  graph.AddRequired("voice-interaction-service", "audio-service");
  graph.AddRequired("voice-interaction-service", "cockpit-gateway-service");
  graph.AddOptional("voice-interaction-service", "recording-service");
  graph.AddRequired("cockpit-gateway-service", "vehicle-data-service");

  const auto voice_required = graph.RequiredDependenciesOf("voice-interaction-service");
  const auto voice_optional = graph.OptionalDependenciesOf("voice-interaction-service");
  if (!Check(voice_required.size() == 2, "required dependency count mismatch") ||
      !Check(voice_optional.size() == 1 && voice_optional[0] == "recording-service",
             "optional dependency mismatch") ||
      !Check(!graph.HasCycle(nullptr), "acyclic dependency graph reported a cycle")) {
    return 1;
  }

  cockpit::runtime::DependencyGraph cyclic;
  cyclic.AddRequired("a", "b");
  cyclic.AddRequired("b", "c");
  cyclic.AddRequired("c", "a");
  std::string cycle;
  if (!Check(cyclic.HasCycle(&cycle), "cycle was not detected") ||
      !Check(cycle.find('a') != std::string::npos, "cycle path did not include service name")) {
    return 1;
  }

  std::cout << "dependency graph tests passed\n";
  return 0;
}
