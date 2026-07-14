#pragma once

namespace cockpit {
namespace runtime {
class ProcessRuntime;
}
namespace camera_preview_probe {

int ProbeCameraPreview(const runtime::ProcessRuntime& runtime);

}  // namespace camera_preview_probe
}  // namespace cockpit
