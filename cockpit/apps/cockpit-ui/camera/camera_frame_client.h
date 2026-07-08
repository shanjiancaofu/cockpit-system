#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace camera {
struct CameraFrame;
}
namespace ui {

class CameraFrameModel;

class CameraFrameClient {
 public:
  CameraFrameClient(std::string shared_memory_name, CameraFrameModel* model);
  ~CameraFrameClient();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(CameraFrameClient);

  void Start();
  void Stop();

 private:
  void Run();
  void PostConnected(bool connected);
  bool PostFrame(const camera::CameraFrame& frame, std::uint64_t generation);

  std::string shared_memory_name_;
  CameraFrameModel* model_;
  std::atomic_bool running_{false};
  std::thread worker_;
};

}  // namespace ui
}  // namespace cockpit
