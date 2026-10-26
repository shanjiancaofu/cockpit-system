#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "cockpit/driver/camera/tuning/tuning_profile.h"
#include "tools/camera/camera-diag/diagnostics.h"

namespace cam = cockpit::camera;
namespace diag = cockpit::camera_diag;
namespace {
void Require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
std::string Read(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
bool Same(const cam::CameraTuning& a, const cam::CameraTuning& b) {
  const auto fields = [](const auto& s) {
    return std::tie(s.ae_mode, s.awb_mode, s.ae_lock, s.awb_lock, s.antibanding, s.exposure_ns.min,
                    s.exposure_ns.max, s.sensor_gain.min, s.sensor_gain.max, s.isp_gain.min,
                    s.isp_gain.max, s.exposure_compensation_ev, s.wb_gains);
  };
  return fields(a) == fields(b);
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    auto name = (std::filesystem::temp_directory_path() / "native-profile-XXXXXX").string();
    Require(mkdtemp(name.data()), "create test profile directory");
    path = name;
  }
  ~Directory() {
    std::filesystem::remove_all(path);
  }
};
cam::CameraTuningMetadata Observe(cam::NativeArgusCapture& capture, int frames,
                                  std::vector<diag::Json>* metadata) {
  std::string error;
  cam::CameraTuningState state;
  Require(capture.GetTuning(&state, &error), error);
  cam::CameraTuningMetadata last;
  int matched = 0;
  for (int i = 0; i < frames + 30 && matched < frames; ++i) {
    cam::FrameLease lease;
    Require(capture.Acquire(&lease, 3000, &error) == cam::NativeAcquireStatus::kFrame, error);
    const auto& frame = lease.frame();
    const auto& m = frame.tuning;
    metadata->push_back(diag::Object(
        {{"session_id", diag::Number(frame.session_id)},
         {"capture_id", diag::Number(frame.capture_id)},
         {"sensor_timestamp_ns", diag::Number(frame.sensor_timestamp_ns)},
         {"revision", diag::Number(m.revision)},
         {"exposure_ns", diag::Number(m.exposure_ns)},
         {"sensor_gain", diag::Number(m.sensor_gain)},
         {"isp_gain", diag::Number(m.isp_gain)},
         {"ae", diag::Quote(m.ae_mode)},
         {"awb", diag::Quote(m.awb_mode)},
         {"wb_gains", diag::Array({diag::Number(m.wb_gains[0]), diag::Number(m.wb_gains[1]),
                                   diag::Number(m.wb_gains[2]), diag::Number(m.wb_gains[3])})}}));
    Require(m.valid && frame.session_id == state.session_id, "invalid metadata identity");
    if (m.revision == state.revision) {
      ++matched;
      last = m;
    }
  }
  Require(matched == frames, "profile revision not observed");
  Require(last.ae_mode == state.accepted.ae_mode && last.awb_mode == state.accepted.awb_mode,
          "profile mode not observed");
  if (state.accepted.ae_mode == "manual") {
    Require(std::abs(static_cast<double>(last.exposure_ns) - state.accepted.exposure_ns.min) <=
                    100000 &&
                std::abs(last.sensor_gain - state.accepted.sensor_gain.min) < 0.1 &&
                std::abs(last.isp_gain - state.accepted.isp_gain.min) < 0.05,
            "manual profile values not observed");
    for (std::size_t i = 0; i < 4; ++i)
      Require(std::abs(last.wb_gains[i] - state.accepted.wb_gains[i]) < 0.05,
              "profile WB not observed");
  }
  return last;
}
}  // namespace
// Explicit hardware gate; no physical camera is opened by automatic CTest.
int main(int argc, char** argv) {
  if (argc != 3 || std::string(argv[1]) != "--output") return 2;
  cam::NativeArgusCapture capture;
  std::string error;
  std::vector<diag::Json> runs, metadata;
  int rejections = 0;
  try {
    Directory directory;
    const auto path = directory.path / "profile.yaml";
    for (int cycle = 0; cycle < 10; ++cycle) {
      Require(capture.Start({}, &error), error);
      cam::CameraTuningState before;
      Require(capture.GetTuning(&before, &error), error);
      auto profile = cam::MakeTuningProfile(before, "hardware-profile");
      if (cycle == 0) {
        auto& settings = profile.settings;
        settings.ae_mode = "manual";
        settings.ae_lock = false;
        settings.exposure_compensation_ev = 0;
        settings.exposure_ns = {8000000, 8000000};
        settings.sensor_gain = {2, 2};
        settings.isp_gain = {1, 1};
        settings.awb_mode = "manual";
        settings.awb_lock = false;
        settings.wb_gains = {1.5f, 1, 1, 1.25f};
      } else
        Require(cam::LoadTuningProfile(path, &profile, &error), error);
      Require(capture.ApplyTuningProfile(profile, nullptr, &error), error);
      const auto manual = Observe(capture, 30, &metadata);
      cam::CameraTuningState active;
      Require(capture.GetTuning(&active, &error), error);
      if (cycle == 0) {
        profile = cam::MakeTuningProfile(active, "hardware-profile");
        const auto saved = cam::SaveTuningProfile(path, profile);
        Require(saved.durable, saved.error);
      }
      const auto original = Read(path);
      for (int invalid = 0; invalid < 7; ++invalid) {
        auto bad = profile;
        if (invalid == 0) bad.version = 2;
        if (invalid == 1) ++bad.target.sensor_mode;
        if (invalid == 2) bad.target.sensor_model = "mismatched-sensor";
        if (invalid == 3) ++bad.target.device_index;
        if (invalid == 4) bad.target.width = 1280;
        if (invalid == 5) bad.settings.sensor_gain = {0, 0};
        if (invalid == 6)
          bad.settings.sensor_gain = {active.capabilities.sensor_gain.max + 1,
                                      active.capabilities.sensor_gain.max + 1};
        std::uint32_t revision = 999;
        Require(!capture.ApplyTuningProfile(bad, &revision, &error) && !error.empty() &&
                    revision == 999,
                "invalid profile accepted");
        cam::CameraTuningState unchanged;
        Require(capture.GetTuning(&unchanged, &error), error);
        Require(unchanged.revision == active.revision && Same(unchanged.accepted, active.accepted),
                "invalid profile changed active settings");
        ++rejections;
      }
      auto bad = profile;
      bad.version = 2;
      const auto failed = cam::SaveTuningProfile(path, bad);
      Require(!failed.committed && !failed.durable && Read(path) == original,
              "invalid save replaced valid file");
      Observe(capture, 5, &metadata);
      Require(capture.ResetTuning(nullptr, &error), error);
      Observe(capture, 30, &metadata);
      cam::TuningProfile restored;
      Require(cam::LoadTuningProfile(path, &restored, &error), error);
      Require(capture.ApplyTuningProfile(restored, nullptr, &error), error);
      const auto reloaded = Observe(capture, 30, &metadata);
      Require(manual.exposure_ns == reloaded.exposure_ns &&
                  std::abs(manual.sensor_gain - reloaded.sensor_gain) < 0.01,
              "profile restoration differs");
      Require(capture.Stop(&error) == cam::NativeStopStatus::kStopped, error);
      Require(capture.stats().ResourcesBalanced(), "profile lifecycle leaked resources");
      runs.push_back(diag::Object({{"cycle", diag::Number(cycle)},
                                   {"session_id", diag::Number(active.session_id)},
                                   {"passed", "true"},
                                   {"sensor_model", diag::Quote(active.device.sensor_model)},
                                   {"sensor_mode", diag::Number(active.device.sensor_mode)},
                                   {"exposure_ns", diag::Number(reloaded.exposure_ns)},
                                   {"sensor_gain", diag::Number(reloaded.sensor_gain)}}));
    }
    error.clear();
  } catch (const std::exception& e) {
    error = e.what();
  }
  std::string stop;
  if (capture.Stop(&stop) != cam::NativeStopStatus::kStopped) error += " stop: " + stop;
  const auto stats = capture.stats();
  const bool passed =
      error.empty() && runs.size() == 10 && rejections == 70 && stats.ResourcesBalanced();
  const auto report =
      diag::Object({{"schema_version", "1"},
                    {"passed", diag::Boolean(passed)},
                    {"rejections", diag::Number(rejections)},
                    {"runs", diag::Array(runs)},
                    {"frames", diag::Array(metadata)},
                    {"frames_acquired", diag::Number(stats.frames_acquired)},
                    {"leases_returned", diag::Number(stats.leases_returned)},
                    {"buffers_released", diag::Number(stats.buffers_released)},
                    {"allocated", diag::Number(stats.allocated)},
                    {"destroyed", diag::Number(stats.destroyed)},
                    {"resources_balanced", diag::Boolean(stats.ResourcesBalanced())},
                    {"error", diag::Quote(error)}});
  if (!diag::WriteReport(argv[2], report)) return 2;
  std::cout << "passed=" << passed << " error=" << error << '\n';
  return passed ? 0 : 1;
}
