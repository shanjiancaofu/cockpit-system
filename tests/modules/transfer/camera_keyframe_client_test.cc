#include "cockpit/transfer/runtime/camera_keyframe_client.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
void Check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
class Camera final : public cockpit::proto::camera::CameraControl::Service {
 public:
  std::atomic_uint calls{0};
  std::atomic_bool delay{false};
  std::mutex mutex;
  std::vector<std::chrono::steady_clock::time_point> received;
  grpc::Status RequestKeyframe(grpc::ServerContext* context, const cockpit::proto::common::Empty*,
                               cockpit::proto::camera::RequestKeyframeResponse* response) override {
    {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(std::chrono::steady_clock::now());
    }
    ++calls;
    if (delay)
      while (!context->IsCancelled()) std::this_thread::sleep_for(1ms);
    response->set_accepted(true);
    return grpc::Status::OK;
  }
};
template <class F>
bool Wait(F predicate) {
  auto until = std::chrono::steady_clock::now() + 2s;
  while (!predicate() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(5ms);
  return predicate();
}
}  // namespace
int main() {
  try {
    Camera camera;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&camera);
    auto server = builder.BuildAndStart();
    Check(server != nullptr, "test server bind failed");
    cockpit::transfer::CameraKeyframeClient client("127.0.0.1:" + std::to_string(port));
    auto began = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000; ++i) client.Request();
    Check(std::chrono::steady_clock::now() - began < 100ms, "Request blocked on RPC");
    Check(Wait([&] {
            return camera.calls > 0;
          }),
          "no IDR RPC");
    std::this_thread::sleep_for(300ms);
    Check(camera.calls <= 2, "burst not coalesced");
    camera.delay = true;
    const auto previous = camera.calls.load();
    client.Request();
    Check(Wait([&] {
            return camera.calls > previous;
          }),
          "delayed RPC not started");
    began = std::chrono::steady_clock::now();
    client.Stop();
    Check(std::chrono::steady_clock::now() - began < 300ms, "Stop did not cancel RPC");
    const auto stopped = camera.calls.load();
    client.Request();
    std::this_thread::sleep_for(300ms);
    Check(camera.calls == stopped, "requests accepted after Stop");
    {
      std::lock_guard<std::mutex> lock(camera.mutex);
      for (std::size_t i = 1; i < camera.received.size(); ++i)
        Check(camera.received[i] - camera.received[i - 1] >= 200ms,
              "RPC requests are not rate-limited");
    }
    server->Shutdown();
    std::cout << "async coalescing IDR RPC, rate limit and cancellation passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
