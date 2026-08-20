#include "cockpit/modules/voice/actions/hmi_command_provider.h"

namespace cockpit {
namespace voice {

const char* ToString(HmiCommand command) {
  switch (command) {
    case HmiCommand::kOpenCameraPreview:
      return "open_camera_preview";
    case HmiCommand::kPlayMusic:
      return "play_music";
    case HmiCommand::kPauseMusic:
      return "pause_music";
    case HmiCommand::kResumeMusic:
      return "resume_music";
  }
  return "unknown";
}

}  // namespace voice
}  // namespace cockpit
