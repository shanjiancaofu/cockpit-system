#pragma once

#include <memory>
#include <string>

#include "cockpit/apps/cockpit-ui/media/media_control_model.h"

namespace cockpit {
namespace ui {

// Bounded client for the media-service control plane. Only fixed track IDs are accepted by the
// MediaControlModel; this class never exposes paths, URLs, argv, or shell commands.
std::unique_ptr<MediaPlayerBackend> CreateGrpcMediaPlayerBackend(std::string address);

}  // namespace ui
}  // namespace cockpit
