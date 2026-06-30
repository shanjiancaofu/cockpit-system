#pragma once

#include "modules/camera/camera_frame.h"

namespace cockpit {
namespace camera {

class CameraFrameSink {
 public:
  virtual ~CameraFrameSink() = default;

  virtual bool Publish(CameraFrame frame) = 0;
};

}  // namespace camera
}  // namespace cockpit
