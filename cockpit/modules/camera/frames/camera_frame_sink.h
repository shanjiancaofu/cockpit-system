#pragma once

#include "cockpit/modules/camera/frames/camera_frame.h"

namespace cockpit {
namespace camera {

class CameraFrameSink {
 public:
  virtual ~CameraFrameSink() = default;

  virtual bool Publish(CameraFrame frame) = 0;
};

}  // namespace camera
}  // namespace cockpit
