#include "cockpit/library/media/media_service.h"

#include <utility>

namespace cockpit {
namespace media {

MediaService::MediaService(std::unique_ptr<MediaPlayer> player) : player_(std::move(player)) {
}

std::vector<MediaTrack> MediaService::ListTracks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_ == nullptr ? std::vector<MediaTrack>{} : player_->ListTracks();
}

MediaPlaybackStatus MediaService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_ == nullptr ? MediaPlaybackStatus{MediaPlaybackState::kFaulted, {}, {}, {}, 0, 0,
                                                  "media player is unavailable"}
                            : player_->status();
}

bool MediaService::Play(const std::string& track_id, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_ != nullptr && player_->Play(track_id, error);
}

bool MediaService::Pause(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_ != nullptr && player_->Pause(error);
}

bool MediaService::Resume(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_ != nullptr && player_->Resume(error);
}

bool MediaService::Stop(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_ != nullptr && player_->Stop(error);
}

bool MediaService::Next(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_ != nullptr && player_->Next(error);
}

}  // namespace media
}  // namespace cockpit
