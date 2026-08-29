#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "camera.grpc.pb.h"
#include "cockpit/core/time/time.h"
#include "cockpit/library/driver/vehicle/vehicle_grpc_service.h"
#include "cockpit/library/sentinel/chassis_event_client.h"
#include "cockpit/library/sentinel/sentinel_actions.h"
#include "cockpit/modules/sentinel/sentinel_service.h"
#include "recording.grpc.pb.h"

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

bool WaitFor(const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::yield();
  }
  return predicate();
}

class OrderedCalls {
 public:
  void Add(std::string call) {
    std::lock_guard<std::mutex> lock(mutex_);
    calls_.push_back(std::move(call));
  }
  std::vector<std::string> Get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> calls_;
};

class FakeRecording final : public cockpit::proto::recording::RecordingControl::Service {
 public:
  explicit FakeRecording(OrderedCalls& calls) : calls_(calls) {
  }
  grpc::Status GetStatus(grpc::ServerContext*, const cockpit::proto::common::Empty*,
                         cockpit::proto::recording::RecordingStatus* response) override {
    response->set_state(recording_ ? cockpit::proto::recording::RECORDING_STATE_RECORDING
                                   : cockpit::proto::recording::RECORDING_STATE_IDLE);
    return grpc::Status::OK;
  }
  grpc::Status Start(grpc::ServerContext*, const cockpit::proto::recording::StartRecordingRequest*,
                     cockpit::proto::recording::RecordingStatus* response) override {
    calls_.Add("recording-start");
    recording_ = true;
    response->set_state(cockpit::proto::recording::RECORDING_STATE_RECORDING);
    return grpc::Status::OK;
  }
  grpc::Status AppendEvent(grpc::ServerContext*,
                           const cockpit::proto::recording::AppendRecordingEventRequest* request,
                           cockpit::proto::recording::RecordingStatus* response) override {
    if (!recording_ || request->topic() != "/sentinel/motion_detected") {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "invalid recording event");
    }
    calls_.Add("recording-event");
    response->set_state(cockpit::proto::recording::RECORDING_STATE_RECORDING);
    return grpc::Status::OK;
  }

 private:
  OrderedCalls& calls_;
  bool recording_ = false;
};

class FakeCamera final : public cockpit::proto::camera::CameraControl::Service {
 public:
  explicit FakeCamera(OrderedCalls& calls) : calls_(calls) {
  }
  grpc::Status TakePhoto(grpc::ServerContext*,
                         const cockpit::proto::camera::TakePhotoRequest* request,
                         cockpit::proto::camera::TakePhotoResponse* response) override {
    if (request->filename() != "sentinel-motion-1.jpg") {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "unexpected filename");
    }
    calls_.Add("camera-photo");
    response->set_path("/tmp/sentinel-motion-1.jpg");
    response->set_size_bytes(123);
    return grpc::Status::OK;
  }

 private:
  OrderedCalls& calls_;
};

std::unique_ptr<grpc::Server> StartServer(grpc::Service* service, int* port) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), port);
  builder.RegisterService(service);
  return builder.BuildAndStart();
}

}  // namespace

int main() {
  OrderedCalls calls;
  FakeRecording recording(calls);
  FakeCamera camera(calls);
  int recording_port = 0;
  int camera_port = 0;
  auto recording_server = StartServer(&recording, &recording_port);
  auto camera_server = StartServer(&camera, &camera_port);
  Require(recording_server != nullptr && camera_server != nullptr, "fake services failed to start");

  cockpit::vehicle::VehicleGrpcService vehicle_service;
  // Use a unique Unix socket so no fixed TCP port is required.
  const std::string vehicle_address =
      "unix:/tmp/cockpit-sentinel-fake-" + std::to_string(::getpid()) + ".sock";
  Require(vehicle_service.Start(vehicle_address), "vehicle service failed to start");

  cockpit::sentinel::SentinelPolicy policy;
  policy.cooldown = std::chrono::milliseconds(100);
  policy.max_event_age = std::chrono::seconds(1);
  auto actions = std::make_unique<cockpit::sentinel::GrpcSentinelActions>(
      "127.0.0.1:" + std::to_string(camera_port), "127.0.0.1:" + std::to_string(recording_port),
      500);
  cockpit::sentinel::SentinelService sentinel(policy, std::move(actions));
  Require(sentinel.Start(true), "sentinel failed to start");
  cockpit::sentinel::ChassisEventClient event_client(vehicle_address);
  std::atomic_bool running{true};
  std::thread stream([&] {
    event_client.Stream(
        [&](cockpit::vehicle::ChassisEvent event) {
          return sentinel.Submit(std::move(event));
        },
        [&] {
          return running.load();
        });
  });
  Require(vehicle_service.WaitForEventSubscriber(std::chrono::seconds(1)),
          "sentinel did not subscribe to typed chassis events");

  cockpit::vehicle::ChassisEvent event;
  event.sequence = 1;
  event.timestamp_ms = cockpit::time::WallTime::Now().ToMilliseconds();
  event.source = "fake-stm32";
  event.sensor_id = 1;
  event.motion_detected = true;
  vehicle_service.PublishEvent(event);
  Require(WaitFor([&] {
            return sentinel.status().accepted_events == 1;
          }),
          "fake sentinel closure did not complete");
  const auto status = sentinel.status();
  Require(status.last_snapshot_path == "/tmp/sentinel-motion-1.jpg",
          "snapshot result missing from sentinel status");
  Require(calls.Get() ==
              std::vector<std::string>({"recording-start", "camera-photo", "recording-event"}),
          "recording/camera action order is incorrect");

  running.store(false);
  event_client.Stop();
  vehicle_service.Shutdown();
  stream.join();
  sentinel.Stop();
  camera_server->Shutdown();
  recording_server->Shutdown();
  std::cout << "fake STM32 sentinel closure tests passed\n";
  return 0;
}
