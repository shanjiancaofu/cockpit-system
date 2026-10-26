#include "cockpit/driver/camera/tuning/tuning_profile.h"

#include <unistd.h>

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>

namespace cam = cockpit::camera;
namespace fs = std::filesystem;
namespace {
std::string Read(const fs::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
struct Directory {
  fs::path path;
  Directory() {
    std::string pattern = (fs::temp_directory_path() / "camera-profile-XXXXXX").string();
    assert(mkdtemp(pattern.data()));
    path = pattern;
  }
  ~Directory() {
    fs::remove_all(path);
  }
};
}  // namespace
int main() {
  Directory directory;
  const auto path = directory.path / "profile.yaml";
  cam::CameraTuningState state;
  state.device = {0, 1920, 1080, 30, 2, "imx219"};
  state.accepted.exposure_ns = {13000, 33333334};
  state.accepted.sensor_gain = {1, 10.625f};
  state.accepted.isp_gain = {1, 256};
  auto profile = cam::MakeTuningProfile(state, "profile-a");
  std::string error;
  assert(cam::ValidateTuningProfile(profile, &error));
  auto written = cam::SaveTuningProfile(path, profile);
  assert(written.committed && written.durable && written.error.empty());
  const auto original = Read(path);
  cam::TuningProfile loaded;
  assert(cam::LoadTuningProfile(path, &loaded, &error));
  assert(loaded.name == profile.name && loaded.target.sensor_model == "imx219" &&
         loaded.target.sensor_mode == 2);
  assert(loaded.settings.exposure_ns.max == 33333334 && loaded.settings.sensor_gain.max == 10.625f);
  const auto check_invalid = [&](std::string text) {
    auto output = loaded;
    assert(!cam::DecodeTuningProfile(text, &output, &error) && !error.empty());
    assert(output.name == loaded.name &&
           output.settings.sensor_gain.max == loaded.settings.sensor_gain.max);
  };
  check_invalid(original + "\nunknown: 1\n");
  check_invalid(original + "\nschema_version: 1\n");
  check_invalid(original + "\n---\nname: another\n");
  check_invalid(std::string(65537, 'x'));
  check_invalid("[invalid]");
  const auto replace = [&](std::string before, std::string after) {
    auto text = original;
    const auto at = text.find(before);
    assert(at != std::string::npos);
    text.replace(at, before.size(), after);
    check_invalid(text);
  };
  replace("schema_version: 1", "schema_version: 2");
  replace("schema_version: 1", "schema_version: -1");
  replace("schema_version: 1", "schema_version: 1.0");
  replace("device_index: 0", "device_index: 4294967296");
  replace("sensor_mode: 2", "sensor_mode: -1");
  replace("fps: 30", "fps: 0");
  replace("native_argus", "software_isp");
  replace("ae_mode: auto", "ae_mode: impossible");
  replace("ae_lock: false", "ae_lock: maybe");
  replace("ae_mode: auto", "ae_mode: auto\n  ae_mode: manual");
  replace("ae_mode: auto", "denoise: 0.5\n  ae_mode: auto");
  replace("[1, 10.625]", "[4, 2]");
  replace("[1, 10.625]", "[1, .nan]");
  replace("[1, 10.625]", "[1, .inf]");
  replace("[1, 10.625]", "[1, 1e100]");
  replace("[1, 1, 1, 1]", "[1, 1, 1]");
  replace("[1, 1, 1, 1]", "[1, 1, 0, 1]");
  auto invalid = profile;
  invalid.version = 2;
  written = cam::SaveTuningProfile(path, invalid);
  assert(!written.committed && !written.durable && !written.error.empty() &&
         Read(path) == original);
  written = cam::SaveTuningProfile(directory.path / "missing" / "profile.yaml", profile);
  assert(!written.committed && !written.durable && Read(path) == original);
  written = cam::SaveTuningProfile(directory.path, profile);
  assert(!written.committed && !written.durable && Read(path) == original);
  for (const auto& item : fs::directory_iterator(directory.path)) assert(item.path() == path);
  auto untouched = loaded;
  assert(!cam::LoadTuningProfile(directory.path / "missing.yaml", &untouched, &error));
  assert(untouched.name == loaded.name);
  cam::CameraTuningCapabilities caps;
  caps.frame_duration_ns = 33333334;
  caps.exposure_ns = {13000, 33333334};
  caps.sensor_gain = {1, 10.625f};
  caps.isp_gain = {1, 256};
  caps.exposure_compensation_ev = {-2, 2};
  assert(cam::ValidateTuningProfileForDevice(profile, state.device, caps, &error));
  invalid = profile;
  invalid.target.sensor_mode = 3;
  assert(!cam::ValidateTuningProfileForDevice(invalid, state.device, caps, &error));
  invalid = profile;
  invalid.target.sensor_model = "different";
  assert(!cam::ValidateTuningProfileForDevice(invalid, state.device, caps));
  invalid = profile;
  invalid.target.device_index = 1;
  assert(!cam::ValidateTuningProfileForDevice(invalid, state.device, caps));
  invalid = profile;
  invalid.target.fps = 25;
  assert(!cam::ValidateTuningProfileForDevice(invalid, state.device, caps));
  invalid = profile;
  invalid.settings.sensor_gain.max = 11;
  assert(cam::ValidateTuningProfile(invalid) &&
         !cam::ValidateTuningProfileForDevice(invalid, state.device, caps));
  invalid = profile;
  invalid.settings.wb_gains[0] = std::numeric_limits<float>::infinity();
  assert(!cam::ValidateTuningProfile(invalid));
  profile.settings.ae_mode = "manual";
  profile.settings.exposure_ns = {8000000, 8000000};
  profile.settings.sensor_gain = {2.125f, 2.125f};
  profile.settings.isp_gain = {1, 1};
  profile.settings.awb_mode = "manual";
  profile.settings.wb_gains = {1.1f, 1, 1, 1.3f};
  written = cam::SaveTuningProfile(path, profile);
  assert(written.durable && cam::LoadTuningProfile(path, &loaded, &error));
  assert(loaded.settings.wb_gains == profile.settings.wb_gains &&
         loaded.settings.sensor_gain.min == 2.125f);
  std::atomic<bool> done{false};
  std::atomic<int> reads{0};
  std::thread reader([&] {
    do {
      cam::TuningProfile value;
      assert(cam::LoadTuningProfile(path, &value));
      assert(value.name == "profile-a" || value.name == "profile-b");
      ++reads;
    } while (!done.load());
  });
  for (int i = 0; i < 20; ++i) {
    profile.name = i % 2 == 0 ? "profile-a" : "profile-b";
    assert(cam::SaveTuningProfile(path, profile).durable);
  }
  done = true;
  reader.join();
  assert(reads > 0);
  assert(!cam::DecodeTuningProfile(original, nullptr));
}
