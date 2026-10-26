#include "cockpit/driver/camera/tuning/camera_tuning.h"

#include <algorithm>
#include <cmath>

namespace cockpit::camera {
namespace {
template <class T>
bool Within(TuningRange<T> value, TuningRange<T> limits) {
  return std::isfinite(static_cast<double>(value.min)) &&
         std::isfinite(static_cast<double>(value.max)) && value.min <= value.max &&
         value.min >= limits.min && value.max <= limits.max;
}
bool Contains(const std::vector<std::string>& choices, const std::string& value) {
  return std::find(choices.begin(), choices.end(), value) != choices.end();
}
}  // namespace
bool ValidateCameraTuning(const CameraTuning& s, const CameraTuningCapabilities& c,
                          std::string* error) {
  std::string why;
  if (!Contains(c.ae_modes, s.ae_mode) || !Contains(c.awb_modes, s.awb_mode) ||
      !Contains(c.antibanding_modes, s.antibanding))
    why = "unknown or unsupported AE/AWB/antibanding mode";
  else if (!Within(s.exposure_ns, c.exposure_ns) || s.exposure_ns.min == 0 ||
           s.exposure_ns.max > c.frame_duration_ns)
    why = "exposure range exceeds sensor mode or fixed frame duration";
  else if (!Within(s.sensor_gain, c.sensor_gain) || !Within(s.isp_gain, c.isp_gain))
    why = "analog/ISP gain range exceeds device limits";
  else if (!std::isfinite(s.exposure_compensation_ev) ||
           s.exposure_compensation_ev < c.exposure_compensation_ev.min ||
           s.exposure_compensation_ev > c.exposure_compensation_ev.max)
    why = "exposure compensation exceeds device limits";
  else if (s.ae_mode == "manual" &&
           (s.exposure_ns.min != s.exposure_ns.max || s.sensor_gain.min != s.sensor_gain.max ||
            s.isp_gain.min != s.isp_gain.max || s.ae_lock || s.exposure_compensation_ev != 0))
    why = "manual AE requires fixed exposure/gains, unlocked AE and zero compensation";
  else if (s.awb_mode == "manual" &&
           (s.awb_lock || std::any_of(s.wb_gains.begin(), s.wb_gains.end(), [](float gain) {
              return !std::isfinite(gain) || gain <= 0;
            })))
    why = "manual WB requires unlocked AWB and finite positive R/Gr/Gb/B gains";
  if (error) *error = why;
  return why.empty();
}
}  // namespace cockpit::camera
