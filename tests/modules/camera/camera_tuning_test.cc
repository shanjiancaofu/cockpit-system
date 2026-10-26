#include <cassert>
#include <limits>
#include <string>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "cockpit/driver/camera/tuning/tuning_profile.h"

int main() {
  using namespace cockpit::camera;
  CameraTuningCapabilities caps;
  caps.exposure_ns = {10000, 100000000};
  caps.frame_duration_ns = 33333334;
  caps.sensor_gain = {1, 16};
  caps.isp_gain = {1, 8};
  caps.exposure_compensation_ev = {-2, 2};
  CameraTuning settings;
  settings.exposure_ns = {10000, 30000000};
  settings.sensor_gain = {1, 16};
  settings.isp_gain = {1, 8};
  std::string error;
  assert(ValidateCameraTuning(settings, caps, &error) && error.empty());
  auto bad = settings;
  bad.exposure_ns.max = 40000000;
  assert(!ValidateCameraTuning(bad, caps, &error));  // sensor permits it, fixed FPS does not.
  bad = settings;
  bad.sensor_gain = {4, 2};
  assert(!ValidateCameraTuning(bad, caps));
  bad = settings;
  bad.sensor_gain.min = std::numeric_limits<float>::quiet_NaN();
  assert(!ValidateCameraTuning(bad, caps));
  bad = settings;
  bad.exposure_compensation_ev = std::numeric_limits<float>::infinity();
  assert(!ValidateCameraTuning(bad, caps));
  bad = settings;
  bad.exposure_compensation_ev = -3;
  assert(!ValidateCameraTuning(bad, caps));
  bad = settings;
  bad.antibanding = "120hz";
  assert(!ValidateCameraTuning(bad, caps));
  bad = settings;
  bad.awb_mode = "unknown";
  assert(!ValidateCameraTuning(bad, caps));
  settings.ae_mode = "manual";
  assert(!ValidateCameraTuning(settings, caps));
  settings.exposure_ns = {8000000, 8000000};
  settings.sensor_gain = {2, 2};
  settings.isp_gain = {1, 1};
  assert(ValidateCameraTuning(settings, caps));
  settings.ae_lock = true;
  assert(!ValidateCameraTuning(settings, caps));
  settings.ae_lock = false;
  settings.exposure_compensation_ev = 1;
  assert(!ValidateCameraTuning(settings, caps));
  settings.exposure_compensation_ev = 0;
  settings.awb_mode = "manual";
  settings.wb_gains = {1.5f, 1, 1, 1.25f};
  assert(ValidateCameraTuning(settings, caps));
  settings.wb_gains[2] = 0;
  assert(!ValidateCameraTuning(settings, caps));
  settings.wb_gains[2] = 1;
  settings.awb_lock = true;
  assert(!ValidateCameraTuning(settings, caps));
  NativeArgusCapture stopped;
#ifdef COCKPIT_TEST_NATIVE_STUB
  assert(!NativeArgusCapture::IsSupported());
  assert(!stopped.Start({}, &error) && !error.empty());
#endif
  CameraTuningState state;
  std::uint32_t revision = 99;
  assert(!stopped.GetTuning(&state, &error) && !error.empty());
  assert(!stopped.SetTuning(settings, &revision, &error) && revision == 99);
  assert(!stopped.ResetTuning(&revision, &error) && revision == 99);
  assert(!stopped.ApplyTuningProfile(TuningProfile{}, &revision, &error) && revision == 99);
  assert(stopped.Stop() == NativeStopStatus::kStopped && stopped.stats().ResourcesBalanced());
}
