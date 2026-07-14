#pragma once

namespace cockpit {
namespace runtime {
class ProcessRuntime;
}
namespace camera_probe {

int ProbeCamera(const runtime::ProcessRuntime& runtime);

}  // namespace camera_probe
}  // namespace cockpit
