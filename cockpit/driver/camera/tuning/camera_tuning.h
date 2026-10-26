#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cockpit::camera {
template <class T>
struct TuningRange {
  T min{}, max{};
};

// SDK candidate modes are not a per-device support guarantee. SetTuning reports
// SDK rejections; numeric limits come from the selected sensor mode/device.
struct CameraTuningCapabilities {
  TuningRange<std::uint64_t> exposure_ns;
  TuningRange<float> sensor_gain, isp_gain, exposure_compensation_ev;
  std::uint64_t frame_duration_ns = 0;
  std::vector<std::string> ae_modes{"auto", "manual"};
  std::vector<std::string> awb_modes{
      "off",      "auto",   "incandescent", "fluorescent", "warm-fluorescent",
      "daylight", "cloudy", "twilight",     "shade",       "manual"};
  std::vector<std::string> antibanding_modes{"off", "auto", "50hz", "60hz"};
};

// Complete requested settings. Manual AE requires fixed exposure, analog gain
// and ISP gain, unlocked AE and zero EV compensation. Gains use R/Gr/Gb/B order.
struct CameraTuning {
  std::string ae_mode = "auto", awb_mode = "auto", antibanding = "auto";
  bool ae_lock = false, awb_lock = false;
  TuningRange<std::uint64_t> exposure_ns;
  TuningRange<float> sensor_gain, isp_gain;
  float exposure_compensation_ev = 0;
  std::array<float, 4> wb_gains{1, 1, 1, 1};
};

// Actual values copied from each frame's ICaptureMetadata, never from the latest
// requested settings. revision comes from IRequest::clientData, including frames
// queued before an update. Pair revision with session_id from NativeFrame/State.
struct CameraTuningMetadata {
  bool valid = false;
  std::uint32_t revision = 0;
  std::uint64_t exposure_ns = 0;
  float sensor_gain = 0, isp_gain = 0;
  std::array<float, 4> wb_gains{};
  std::string ae_mode, awb_mode;
  bool ae_locked = false, awb_locked = false;
  std::uint32_t awb_cct = 0;
};
struct CameraTuningState {
  std::uint64_t session_id = 0;
  std::uint32_t revision = 0;
  CameraTuningCapabilities capabilities;
  CameraTuning requested, accepted, defaults;
  CameraTuningMetadata last_observed;
};
bool ValidateCameraTuning(const CameraTuning& settings, const CameraTuningCapabilities& limits,
                          std::string* error = nullptr);
}  // namespace cockpit::camera
