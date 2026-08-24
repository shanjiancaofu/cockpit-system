#include "agent/audio/audio_focus_controller.h"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cockpit/library/media/media_grpc_service.h"
#include "cockpit/library/media/media_service.h"
#include "cockpit/modules/media/media_player.h"

namespace {

class FocusMediaPlayer final : public cockpit::media::MediaPlayer {
 public:
  FocusMediaPlayer(cockpit::media::MediaPlaybackState state, bool fail_pause)
      : fail_pause_(fail_pause) {
    status_.state = state;
  }

  std::vector<cockpit::media::MediaTrack> ListTracks() const override {
    return {};
  }
  cockpit::media::MediaPlaybackStatus status() const override {
    return status_;
  }
  bool Play(const std::string&, std::string*) override {
    return false;
  }

  bool Pause(std::string* error) override {
    ++pause_calls;
    if (fail_pause_ || status_.state != cockpit::media::MediaPlaybackState::kPlaying) {
      if (error != nullptr) {
        *error = "fixture pause failed";
      }
      return false;
    }
    status_.state = cockpit::media::MediaPlaybackState::kPaused;
    return true;
  }

  bool Resume(std::string* error) override {
    ++resume_calls;
    if (status_.state != cockpit::media::MediaPlaybackState::kPaused) {
      if (error != nullptr) {
        *error = "fixture resume failed";
      }
      return false;
    }
    status_.state = cockpit::media::MediaPlaybackState::kPlaying;
    return true;
  }

  bool Stop(std::string*) override {
    return false;
  }
  bool Next(std::string*) override {
    return false;
  }

  int pause_calls = 0;
  int resume_calls = 0;

 private:
  const bool fail_pause_;
  cockpit::media::MediaPlaybackStatus status_;
};

bool RunCase(const std::string& name, cockpit::media::MediaPlaybackState initial_state,
             bool fail_pause, bool expected_acquire,
             cockpit::media::MediaPlaybackState expected_final_state, int expected_pause_calls,
             int expected_resume_calls) {
  const std::filesystem::path socket =
      std::filesystem::temp_directory_path() /
      ("cockpit-audio-focus-" + std::to_string(getpid()) + "-" + name + ".sock");
  std::error_code filesystem_error;
  std::filesystem::remove(socket, filesystem_error);
  auto player = std::make_unique<FocusMediaPlayer>(initial_state, fail_pause);
  FocusMediaPlayer* observer = player.get();
  cockpit::media::MediaService service(std::move(player));
  cockpit::media::MediaGrpcService grpc(service);
  const std::string address = "unix:" + socket.string();
  if (!grpc.Start(address)) {
    std::cerr << name << ": media fixture server did not start\n";
    return false;
  }
  auto focus =
      cockpit::voice::CreateMediaAudioFocusController(address, std::chrono::milliseconds(300));
  const bool acquired = focus != nullptr && focus->AcquireTts();
  if (focus != nullptr) {
    focus->ReleaseTts();
  }
  const auto final_state = service.status().state;
  grpc.Shutdown();
  std::filesystem::remove(socket, filesystem_error);
  if (acquired != expected_acquire || final_state != expected_final_state ||
      observer->pause_calls != expected_pause_calls ||
      observer->resume_calls != expected_resume_calls) {
    std::cerr << name << ": focus result mismatch acquired=" << acquired
              << " pause_calls=" << observer->pause_calls
              << " resume_calls=" << observer->resume_calls << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  using cockpit::media::MediaPlaybackState;
  if (!RunCase("playing", MediaPlaybackState::kPlaying, false, true, MediaPlaybackState::kPlaying,
               1, 1) ||
      !RunCase("already-paused", MediaPlaybackState::kPaused, false, true,
               MediaPlaybackState::kPaused, 0, 0) ||
      !RunCase("stopped", MediaPlaybackState::kStopped, false, true, MediaPlaybackState::kStopped,
               0, 0) ||
      !RunCase("pause-failure", MediaPlaybackState::kPlaying, true, false,
               MediaPlaybackState::kPlaying, 1, 0)) {
    return 1;
  }

  const auto started = std::chrono::steady_clock::now();
  auto unavailable = cockpit::voice::CreateMediaAudioFocusController(
      "unix:/tmp/cockpit-audio-focus-missing.sock", std::chrono::milliseconds(100));
  if (unavailable == nullptr || unavailable->AcquireTts() ||
      std::chrono::steady_clock::now() - started > std::chrono::seconds(1)) {
    std::cerr << "unavailable media focus was not rejected within the deadline\n";
    return 1;
  }
  return 0;
}
