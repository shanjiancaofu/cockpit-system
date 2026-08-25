#include "cockpit/modules/hawkeye/camera_calibration.h"

namespace cockpit::hawkeye {

const char* CameraDistortionModelName(CameraDistortionModel model) {
  switch (model) {
    case CameraDistortionModel::kPlumbBob:
      return "plumb_bob";
  }
  return "unknown";
}

}  // namespace cockpit::hawkeye
