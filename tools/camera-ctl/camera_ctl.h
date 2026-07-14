#pragma once

namespace cockpit {
namespace runtime {
class ProcessRuntime;
}
namespace camera_ctl {

int ControlCamera(const runtime::ProcessRuntime& runtime);

}  // namespace camera_ctl
}  // namespace cockpit
