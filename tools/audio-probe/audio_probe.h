#pragma once

namespace cockpit {
namespace runtime {
class ProcessRuntime;
}
namespace audio_probe {

int ProbeAudio(const runtime::ProcessRuntime& runtime);

}  // namespace audio_probe
}  // namespace cockpit
