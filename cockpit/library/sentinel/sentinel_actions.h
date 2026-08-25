#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <mutex>
#include <string>

#include "camera.grpc.pb.h"
#include "cockpit/modules/sentinel/sentinel_service.h"
#include "recording.grpc.pb.h"

namespace cockpit {
namespace sentinel {

class GrpcSentinelActions final : public SentinelActions {
 public:
  GrpcSentinelActions(const std::string& camera_address, const std::string& recording_address,
                      int rpc_timeout_ms);

  bool PrepareRecording(std::uint64_t event_sequence, std::string* error) override;
  bool TakeSnapshot(std::uint64_t event_sequence, SentinelSnapshot* snapshot,
                    std::string* error) override;
  bool RecordMotion(const vehicle::ChassisEvent& event, const SentinelSnapshot& snapshot,
                    std::string* error) override;
  void Cancel() override;

 private:
  std::unique_ptr<proto::camera::CameraControl::Stub> camera_;
  std::unique_ptr<proto::recording::RecordingControl::Stub> recording_;
  const int rpc_timeout_ms_;
  std::mutex mutex_;
  grpc::ClientContext* active_context_ = nullptr;
};

}  // namespace sentinel
}  // namespace cockpit
