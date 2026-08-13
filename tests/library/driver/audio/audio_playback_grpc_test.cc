#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "audio.grpc.pb.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/library/driver/audio/capture/audio_capture_controller.h"
#include "cockpit/library/driver/audio/grpc/audio_grpc_server.h"
#include "cockpit/library/driver/audio/playback/audio_playback.h"
#include "cockpit/library/driver/audio/transport/audio_stream_publisher.h"
#include "cockpit/modules/audio/playback/audio_player.h"

namespace {

class ControllablePlayer final : public cockpit::audio::AudioPlayer {
 public:
  bool Play(const std::string&, const cockpit::audio::PcmBuffer&, const std::atomic_bool& cancelled,
            std::string* error) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      entered_ = true;
    }
    changed_.notify_all();
    while (!released_.load() && !cancelled.load()) {
      std::this_thread::yield();
    }
    if (cancelled.load()) {
      if (error != nullptr) {
        *error = "playback cancelled";
      }
      return false;
    }
    return true;
  }

  bool WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this] {
                                 return entered_;
                               });
  }

  void Release() {
    released_.store(true);
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  std::atomic_bool released_{false};
};

cockpit::proto::audio::PlayPcmRequest MakeRequest(std::uint64_t playback_id) {
  cockpit::proto::audio::PlayPcmRequest request;
  request.set_playback_id(playback_id);
  request.set_sample_rate_hz(16000U);
  request.set_channels(1U);
  std::int16_t samples[320]{};
  request.set_samples_s16le(samples, sizeof(samples));
  return request;
}

}  // namespace

int main() {
  auto player = std::make_unique<ControllablePlayer>();
  auto* player_observer = player.get();
  cockpit::audio::AudioPlayback playback("fake-output", std::move(player));
  std::string error;
  if (!playback.Start(&error)) {
    std::cerr << "failed to start playback: " << error << '\n';
    return 1;
  }
  cockpit::audio::AudioStreamPublisher publisher;
  cockpit::config::AudioConfig config;
  cockpit::audio::AudioCaptureController capture(config, publisher);
  cockpit::audio::AudioGrpcServer server(capture, playback);
  const std::string socket_path =
      "/tmp/cockpit-audio-playback-grpc-" + std::to_string(getpid()) + ".sock";
  if (!server.Start("unix:" + socket_path)) {
    std::cerr << "failed to start audio gRPC server\n";
    return 1;
  }

  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel("unix:" + socket_path,
                                           grpc::InsecureChannelCredentials(), arguments);
  auto stub = cockpit::proto::audio::AudioControl::NewStub(channel);
  auto cancel_stub = cockpit::proto::audio::AudioControl::NewStub(channel);

  cockpit::proto::audio::PlayPcmResponse first_response;
  grpc::ClientContext first_context;
  first_context.set_initial_metadata_corked(false);
  const grpc::Status first_status =
      stub->PlayPcm(&first_context, MakeRequest(101U), &first_response);
  if (!first_status.ok() || !first_response.accepted() || first_response.playback_id() != 101U ||
      !player_observer->WaitUntilEntered()) {
    std::cerr << "PlayPcm did not accept and correlate the first request\n";
    return 1;
  }

  cockpit::proto::audio::PlaybackRequest pending_request;
  pending_request.set_playback_id(101U);
  pending_request.set_wait_timeout_ms(1U);
  cockpit::proto::audio::PlaybackResult pending_result;
  grpc::ClientContext pending_context;
  pending_context.set_initial_metadata_corked(false);
  if (!stub->WaitPlayback(&pending_context, pending_request, &pending_result).ok() ||
      pending_result.status() != cockpit::proto::audio::PLAYBACK_STATUS_PENDING) {
    std::cerr << "accepted playback was incorrectly reported as completed\n";
    return 1;
  }

  cockpit::proto::audio::PlayPcmResponse queued_response;
  grpc::ClientContext queued_context;
  queued_context.set_initial_metadata_corked(false);
  if (!stub->PlayPcm(&queued_context, MakeRequest(102U), &queued_response).ok() ||
      !queued_response.accepted()) {
    std::cerr << "second playback was not queued\n";
    return 1;
  }
  cockpit::proto::audio::CancelPlaybackRequest cancel_request;
  cancel_request.set_playback_id(102U);
  cockpit::proto::audio::CancelPlaybackResponse cancel_response;
  grpc::ClientContext cancel_context;
  cancel_context.set_initial_metadata_corked(false);
  if (!cancel_stub->CancelPlayback(&cancel_context, cancel_request, &cancel_response).ok() ||
      !cancel_response.accepted()) {
    std::cerr << "queued playback cancellation was rejected\n";
    return 1;
  }
  cockpit::proto::audio::PlaybackRequest cancelled_request;
  cancelled_request.set_playback_id(102U);
  cancelled_request.set_wait_timeout_ms(100U);
  cockpit::proto::audio::PlaybackResult cancelled_result;
  grpc::ClientContext cancelled_context;
  cancelled_context.set_initial_metadata_corked(false);
  if (!stub->WaitPlayback(&cancelled_context, cancelled_request, &cancelled_result).ok() ||
      cancelled_result.status() != cockpit::proto::audio::PLAYBACK_STATUS_CANCELLED) {
    std::cerr << "cancelled playback did not retain its final result\n";
    return 1;
  }

  player_observer->Release();
  cockpit::proto::audio::PlaybackRequest completed_request;
  completed_request.set_playback_id(101U);
  completed_request.set_wait_timeout_ms(1000U);
  cockpit::proto::audio::PlaybackResult completed_result;
  grpc::ClientContext completed_context;
  completed_context.set_initial_metadata_corked(false);
  if (!stub->WaitPlayback(&completed_context, completed_request, &completed_result).ok() ||
      completed_result.status() != cockpit::proto::audio::PLAYBACK_STATUS_COMPLETED) {
    std::cerr << "audio worker completion was not returned by gRPC\n";
    return 1;
  }

  playback.Stop();
  server.Shutdown();
  unlink(socket_path.c_str());
  std::cout << "audio playback gRPC tests passed\n";
  return 0;
}
