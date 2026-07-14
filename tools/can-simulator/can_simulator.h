#pragma once

namespace cockpit {
namespace runtime {
class ProcessRuntime;
}
namespace can_simulator {

int SimulateCan(const runtime::ProcessRuntime& runtime);

}  // namespace can_simulator
}  // namespace cockpit
