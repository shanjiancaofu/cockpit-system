#pragma once

#include <memory>
#include <optional>
#include <string>

#include "cockpit/core/base/macros.h"
#include "cockpit/core/runtime/module.h"
#include "cockpit/modules/audio/capture/audio_capture_stream.h"
#include "cockpit/modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace audio {

class AudioCaptureModule : public runtime::BasicModule {
 public:
  AudioCaptureModule();
  ~AudioCaptureModule() override = default;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioCaptureModule);

  void Configure(std::unique_ptr<AudioCaptureStream> capture_stream);

  AudioCaptureState capture_state() const;
  AudioCaptureMetrics metrics() const;
  std::string last_error() const;
  std::optional<AudioFrame> TryPop();

 protected:
  bool OnStart() override;
  void OnStop() override;

 private:
  std::unique_ptr<AudioCaptureStream> capture_stream_;
  std::string last_error_;
};

}  // namespace audio
}  // namespace cockpit
