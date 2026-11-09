#pragma once

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "camera.grpc.pb.h"

namespace cockpit::transfer {
// Single coalescing worker shared by all peers. Request() never performs RPC or
// waits for a camera/encoder; camera outages cannot stall RTP or UDP reception.
class CameraKeyframeClient {
 public:
  explicit CameraKeyframeClient(const std::string& address);
  ~CameraKeyframeClient();
  void Request();
  void Stop();

 private:
  void Run();
  std::unique_ptr<proto::camera::CameraControl::Stub> stub_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool pending_ = false, stopping_ = false;
  grpc::ClientContext* active_context_ = nullptr;
  std::thread worker_;
};
}  // namespace cockpit::transfer
