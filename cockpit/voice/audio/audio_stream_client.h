#pragma once

#include <optional>
#include <string>

#include "cockpit/driver/audio/frames/audio_frame.h"
#include "cockpit/exchange/foundation/base/macros.h"

namespace cockpit {
namespace agent {

enum class AudioStreamReceiveStatus {
  kFrame,
  kTimeout,
  kDisconnected,
  kError,
};

struct AudioStreamReceiveResult {
  AudioStreamReceiveStatus status = AudioStreamReceiveStatus::kError;
  std::optional<audio::AudioFrame> frame;
  std::string error;
};

class AudioStreamClient {
 public:
  AudioStreamClient() = default;
  ~AudioStreamClient();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioStreamClient);

  bool Connect(const std::string& socket_path, std::string* error = nullptr);
  AudioStreamReceiveResult ReceiveFrame(int timeout_ms);
  void Close();
  bool connected() const {
    return socket_fd_ >= 0;
  }

 private:
  int socket_fd_{-1};
};

}  // namespace agent
}  // namespace cockpit
