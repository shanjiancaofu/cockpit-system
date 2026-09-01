#include "cockpit/modules/camera/capture/software_isp_preview_source.h"

#include <cassert>
#include <string>

int main() {
  cockpit::camera::SoftwareIspPreviewSource source;
  std::string error;
  assert(!source.Start(cockpit::camera::CameraPreviewConfig{}, {}, &error));
  const auto stats = source.stats();
  assert(stats.fatal_capture_errors == 0);
  assert(stats.last_error.empty());
  return 0;
}
