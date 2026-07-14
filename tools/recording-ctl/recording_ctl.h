#pragma once

namespace cockpit {
namespace runtime {
class ProcessRuntime;
}
namespace recording_ctl {

int ControlRecording(const runtime::ProcessRuntime& runtime);

}  // namespace recording_ctl
}  // namespace cockpit
