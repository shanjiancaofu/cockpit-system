#pragma once

namespace cockpit {
namespace runtime {
class ProcessRuntime;
}
namespace voice_ctl {

int ControlVoice(const runtime::ProcessRuntime& runtime);

}  // namespace voice_ctl
}  // namespace cockpit
