#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "cockpit/driver/camera/tuning/camera_tuning.h"

namespace cockpit::camera {
struct TuningProfile {
  static constexpr std::uint32_t kVersion = 1;
  std::uint32_t version = kVersion;
  std::string name;
  NativeCameraDescriptor target;
  CameraTuning settings;
};

// A profile stores the SDK Request readback, not one frame's automatic values.
TuningProfile MakeTuningProfile(const CameraTuningState& state, const std::string& name);
bool ValidateTuningProfile(const TuningProfile& profile, std::string* error = nullptr);
bool ValidateTuningProfileForDevice(const TuningProfile& profile,
                                    const NativeCameraDescriptor& target,
                                    const CameraTuningCapabilities& capabilities,
                                    std::string* error = nullptr);
// Strict single-document YAML: unknown/duplicate/missing fields are rejected.
// Outputs are replaced only after complete parsing and validation.
bool DecodeTuningProfile(std::string_view yaml, TuningProfile* profile,
                         std::string* error = nullptr);
bool LoadTuningProfile(const std::filesystem::path& path, TuningProfile* profile,
                       std::string* error = nullptr);

struct TuningProfileSaveResult {
  bool committed = false;  // Atomic rename completed; the new file is visible.
  bool durable = false;    // Both file and containing directory were synchronized.
  std::string error;
};
// Validate first, write a unique sibling temporary file, sync, rename, sync dir.
// Errors before commit preserve an existing profile. A post-rename sync error
// reports committed=true/durable=false rather than claiming no replacement.
TuningProfileSaveResult SaveTuningProfile(const std::filesystem::path& path,
                                          const TuningProfile& profile);
}  // namespace cockpit::camera
