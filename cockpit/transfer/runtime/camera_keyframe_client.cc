#include "cockpit/transfer/runtime/camera_keyframe_client.h"

#include "cockpit/exchange/foundation/logging/logger.h"

namespace cockpit::transfer {
CameraKeyframeClient::CameraKeyframeClient(const std::string& address)
    : stub_(proto::camera::CameraControl::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))),
      worker_([this] {
        Run();
      }) {
}
CameraKeyframeClient::~CameraKeyframeClient() {
  Stop();
}
void CameraKeyframeClient::Request() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    pending_ = true;
  }
  cv_.notify_one();
}
void CameraKeyframeClient::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    pending_ = false;
    if (active_context_) active_context_->TryCancel();
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}
void CameraKeyframeClient::Run() {
  using Clock = std::chrono::steady_clock;
  auto next = Clock::time_point{};
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stopping_) {
    cv_.wait(lock, [this] {
      return stopping_ || pending_;
    });
    if (stopping_) break;
    if (Clock::now() < next && cv_.wait_until(lock, next, [this] {
          return stopping_;
        }))
      break;
    if (stopping_) break;
    pending_ = false;
    next = Clock::now() + std::chrono::milliseconds(250);
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(150));
    active_context_ = &context;
    lock.unlock();
    proto::common::Empty request;
    proto::camera::RequestKeyframeResponse response;
    const auto result = stub_->RequestKeyframe(&context, request, &response);
    if (!result.ok()) LOG_WARN("Camera IDR request failed: " + result.error_message());
    lock.lock();
    active_context_ = nullptr;
  }
}
}  // namespace cockpit::transfer
