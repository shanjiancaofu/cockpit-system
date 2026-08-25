#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "cockpit/modules/vehicle/chassis_event.h"
#include "vehicle_state.grpc.pb.h"

namespace cockpit {
namespace sentinel {

class ChassisEventClient final {
 public:
  explicit ChassisEventClient(const std::string& address);
  int Stream(const std::function<bool(vehicle::ChassisEvent)>& callback,
             const std::function<bool()>& keep_running);
  void Stop();

 private:
  std::unique_ptr<proto::vehicle::VehicleDataService::Stub> stub_;
  std::mutex mutex_;
  grpc::ClientContext* active_context_ = nullptr;
};

}  // namespace sentinel
}  // namespace cockpit
