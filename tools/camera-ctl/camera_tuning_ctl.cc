#include "tools/camera-ctl/camera_tuning_ctl.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "cockpit/exchange/foundation/json/json.h"

namespace cockpit::camera_ctl {
namespace {
namespace cam = cockpit::camera;
using Json = std::string;
Json Quote(const std::string& s) {
  return "\"" + json::EscapeString(s) + "\"";
}
template <class T>
Json Number(T n) {
  return std::isfinite(static_cast<double>(n)) ? std::to_string(n) : "null";
}
Json Bool(bool b) {
  return b ? "true" : "false";
}
Json Object(std::initializer_list<std::pair<std::string, Json>> fields) {
  std::string out = "{";
  for (const auto& field : fields) {
    if (out.size() > 1) out += ",";
    out += Quote(field.first) + ":" + field.second;
  }
  return out + "}";
}
Json Array(const std::vector<Json>& items) {
  std::string out = "[";
  for (const auto& item : items) {
    if (out.size() > 1) out += ",";
    out += item;
  }
  return out + "]";
}
template <class T>
Json Range(cam::TuningRange<T> r) {
  return Array({Number(r.min), Number(r.max)});
}
Json Gains(const std::array<float, 4>& g) {
  return Array({Number(g[0]), Number(g[1]), Number(g[2]), Number(g[3])});
}
Json Strings(const std::vector<std::string>& items) {
  std::vector<Json> values;
  for (const auto& s : items) values.push_back(Quote(s));
  return Array(values);
}
Json Settings(const cam::CameraTuning& s) {
  return Object({{"ae", Quote(s.ae_mode)},
                 {"awb", Quote(s.awb_mode)},
                 {"ae_lock", Bool(s.ae_lock)},
                 {"awb_lock", Bool(s.awb_lock)},
                 {"antibanding", Quote(s.antibanding)},
                 {"exposure_ns", Range(s.exposure_ns)},
                 {"sensor_gain", Range(s.sensor_gain)},
                 {"isp_gain", Range(s.isp_gain)},
                 {"ev", Number(s.exposure_compensation_ev)},
                 {"wb_gains", Gains(s.wb_gains)}});
}
Json Metadata(const cam::CameraTuningMetadata& m) {
  return Object({{"valid", Bool(m.valid)},
                 {"revision", Number(m.revision)},
                 {"exposure_ns", Number(m.exposure_ns)},
                 {"sensor_gain", Number(m.sensor_gain)},
                 {"isp_gain", Number(m.isp_gain)},
                 {"wb_gains", Gains(m.wb_gains)},
                 {"ae", Quote(m.ae_mode)},
                 {"awb", Quote(m.awb_mode)},
                 {"ae_locked", Bool(m.ae_locked)},
                 {"awb_locked", Bool(m.awb_locked)},
                 {"awb_cct", Number(m.awb_cct)}});
}
Json Capabilities(const cam::CameraTuningCapabilities& c) {
  return Object({{"exposure_ns", Range(c.exposure_ns)},
                 {"sensor_gain", Range(c.sensor_gain)},
                 {"isp_gain", Range(c.isp_gain)},
                 {"exposure_compensation_ev", Range(c.exposure_compensation_ev)},
                 {"fixed_frame_duration_ns", Number(c.frame_duration_ns)},
                 {"ae_modes", Strings(c.ae_modes)},
                 {"awb_modes", Strings(c.awb_modes)},
                 {"antibanding_modes", Strings(c.antibanding_modes)},
                 {"mode_lists", Quote("SDK candidates; device acceptance checked by SetTuning")}});
}
void Require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
double Scalar(const std::string& s) {
  std::size_t used = 0;
  const double n = std::stod(s, &used);
  if (used != s.size() || !std::isfinite(n))
    throw std::invalid_argument("invalid finite number: " + s);
  return n;
}
float Float(const std::string& value) {
  const double n = Scalar(value);
  if (std::abs(n) > std::numeric_limits<float>::max())
    throw std::invalid_argument("float outside limits: " + value);
  return static_cast<float>(n);
}
std::vector<double> Values(const std::string& s, std::size_t count) {
  std::vector<double> values;
  std::istringstream stream(s);
  std::string part;
  while (std::getline(stream, part, ',')) values.push_back(Float(part));
  if (values.size() != count || (!s.empty() && s.back() == ','))
    throw std::invalid_argument("invalid tuple: " + s);
  return values;
}
std::uint64_t Exposure(double us) {
  const long double ns = static_cast<long double>(us) * 1000;
  if (ns < 1 || ns >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
    throw std::invalid_argument("exposure is not a positive uint64 nanosecond value");
  return static_cast<std::uint64_t>(std::round(ns));
}
struct Options {
  std::string action, report;
  cam::NativeCaptureConfig config;
  int frames = 60, warmup = 30, cycles = 1;
  std::map<std::string, std::string> values;
};
int Integer(const std::string& s, int min, int max) {
  std::size_t used = 0;
  const auto n = std::stoll(s, &used);
  if (used != s.size() || n < min || n > max)
    throw std::invalid_argument("integer outside limits: " + s);
  return static_cast<int>(n);
}
void Patch(cam::CameraTuning* s, const std::map<std::string, std::string>& values) {
  for (const auto& [key, value] : values) {
    if (key == "ae")
      s->ae_mode = value;
    else if (key == "awb")
      s->awb_mode = value;
    else if (key == "ae-lock" || key == "awb-lock") {
      if (value != "on" && value != "off") throw std::invalid_argument("lock must be on/off");
      (key == "ae-lock" ? s->ae_lock : s->awb_lock) = value == "on";
    } else if (key == "antibanding")
      s->antibanding = value;
    else if (key == "exposure-us")
      s->exposure_ns = {Exposure(Scalar(value)), Exposure(Scalar(value))};
    else if (key == "exposure-range-us") {
      const auto v = Values(value, 2);
      s->exposure_ns = {Exposure(v[0]), Exposure(v[1])};
    } else if (key == "gain")
      s->sensor_gain = {Float(value), Float(value)};
    else if (key == "gain-range") {
      const auto v = Values(value, 2);
      s->sensor_gain = {static_cast<float>(v[0]), static_cast<float>(v[1])};
    } else if (key == "isp-gain")
      s->isp_gain = {Float(value), Float(value)};
    else if (key == "ev")
      s->exposure_compensation_ev = Float(value);
    else if (key == "wb-gains") {
      const auto v = Values(value, 4);
      for (std::size_t i = 0; i < 4; ++i) s->wb_gains[i] = static_cast<float>(v[i]);
    }
  }
  if (values.count("exposure-us") || values.count("gain")) {
    if (!values.count("ae")) s->ae_mode = "manual";
    if (s->ae_mode == "manual") {
      if (!values.count("exposure-us") || !values.count("gain"))
        throw std::invalid_argument("manual CLI requires both --exposure-us and --gain");
      if (!values.count("isp-gain")) s->isp_gain = {1, 1};
      if (!values.count("ev")) s->exposure_compensation_ev = 0;
      if (!values.count("ae-lock")) s->ae_lock = false;
    }
  }
  if (values.count("wb-gains") && !values.count("awb")) s->awb_mode = "manual";
}
Options Parse(int argc, char** argv) {
  if (argc < 2) throw std::invalid_argument("missing tuning action");
  Options options;
  options.action = argv[1];
  if (options.action != "get" && options.action != "set" && options.action != "reset" &&
      options.action != "verify")
    throw std::invalid_argument("tuning action must be get/set/reset/verify");
  bool native = false;
  const std::vector<std::string> controls{"ae",
                                          "awb",
                                          "ae-lock",
                                          "awb-lock",
                                          "antibanding",
                                          "exposure-us",
                                          "exposure-range-us",
                                          "gain",
                                          "gain-range",
                                          "isp-gain",
                                          "ev",
                                          "wb-gains"};
  for (int i = 2; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--native") {
      native = true;
      continue;
    }
    if (flag.rfind("--", 0) != 0 || ++i == argc)
      throw std::invalid_argument("missing option value: " + flag);
    const std::string key = flag.substr(2), value = argv[i];
    if (key == "device")
      options.config.device_index = Integer(value, 0, 100);
    else if (key == "width")
      options.config.width = Integer(value, 1, 16384);
    else if (key == "height")
      options.config.height = Integer(value, 1, 16384);
    else if (key == "fps")
      options.config.fps = Integer(value, 1, 240);
    else if (key == "frames")
      options.frames = Integer(value, 1, 100000);
    else if (key == "warmup")
      options.warmup = Integer(value, 1, 10000);
    else if (key == "cycles")
      options.cycles = Integer(value, 1, 100);
    else if (key == "report")
      options.report = value;
    else if (std::find(controls.begin(), controls.end(), key) != controls.end()) {
      if (!options.values.emplace(key, value).second)
        throw std::invalid_argument("duplicate tuning control: " + key);
    } else
      throw std::invalid_argument("unknown tuning option: " + flag);
  }
  if (!native)
    throw std::invalid_argument(
        "isolated capture requires --native; stop the Camera Service first");
  if ((options.action == "set") == options.values.empty())
    throw std::invalid_argument("set requires controls; get/reset/verify accept no controls");
  if (options.values.count("exposure-us") && options.values.count("exposure-range-us"))
    throw std::invalid_argument("conflicting exposure options");
  if (options.values.count("gain") && options.values.count("gain-range"))
    throw std::invalid_argument("conflicting gain options");
  cam::CameraTuning check;
  Patch(&check, options.values);  // Reject malformed values before opening hardware.
  const cam::CameraTuningCapabilities modes;
  const auto known = [](const auto& list, const auto& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
  };
  if (!known(modes.ae_modes, check.ae_mode) || !known(modes.awb_modes, check.awb_mode) ||
      !known(modes.antibanding_modes, check.antibanding))
    throw std::invalid_argument("unknown tuning mode");
  return options;
}
bool Near(float actual, float expected, float tolerance = 0.05f) {
  return std::isfinite(actual) &&
         std::abs(actual - expected) <= std::max(0.01f, std::abs(expected) * tolerance);
}
Json Phase(cam::NativeArgusCapture& capture, const std::string& name, int frames, bool verify,
           std::vector<Json>* observed) {
  cam::CameraTuningState state;
  std::string error;
  Require(capture.GetTuning(&state, &error), error);
  int matched = 0;
  cam::CameraTuningMetadata last;
  std::uint32_t capture_id = 0;
  std::uint64_t timestamp = 0;
  for (int i = 0; i < frames + 30; ++i) {
    cam::FrameLease lease;
    Require(capture.Acquire(&lease, 3000, &error) == cam::NativeAcquireStatus::kFrame, error);
    const auto& f = lease.frame();
    observed->push_back(Object({{"phase", Quote(name)},
                                {"session_id", Number(f.session_id)},
                                {"capture_id", Number(f.capture_id)},
                                {"sensor_timestamp_ns", Number(f.sensor_timestamp_ns)},
                                {"actual", Metadata(f.tuning)}}));
    Require(f.session_id == state.session_id && f.tuning.valid, "missing/foreign metadata");
    Require((capture_id == 0 || f.capture_id > capture_id) && f.sensor_timestamp_ns > timestamp,
            "non-monotonic frame metadata");
    capture_id = f.capture_id;
    timestamp = f.sensor_timestamp_ns;
    if (f.tuning.revision != state.revision) continue;
    last = f.tuning;
    if (++matched == frames) break;
  }
  Require(matched == frames, "new tuning revision did not reach metadata before frame budget");
  if (verify) {
    Require(
        state.accepted.antibanding == state.requested.antibanding &&
            Near(state.accepted.exposure_compensation_ev, state.requested.exposure_compensation_ev),
        "SDK antibanding/EV readback differs from request");
    Require(last.ae_mode == state.requested.ae_mode && last.awb_mode == state.requested.awb_mode,
            "metadata AE/AWB mode differs from request");
    if (state.requested.ae_lock) Require(last.ae_locked, "AE lock not observed");
    if (state.requested.awb_lock) Require(last.awb_locked, "AWB lock not observed");
    if (state.requested.ae_mode == "manual") {
      const auto requested = state.requested.exposure_ns.min;
      Require(std::abs(static_cast<double>(last.exposure_ns) - requested) <=
                  std::max(100000.0, requested * 0.05),
              "actual manual exposure exceeds sensor quantization tolerance");
      Require(Near(last.sensor_gain, state.requested.sensor_gain.min) &&
                  Near(last.isp_gain, state.requested.isp_gain.min),
              "actual gain differs from manual request");
    }
    if (state.requested.awb_mode == "manual")
      for (std::size_t i = 0; i < 4; ++i)
        Require(Near(last.wb_gains[i], state.requested.wb_gains[i]), "manual WB not observed");
  }
  return Object({{"phase", Quote(name)},
                 {"session_id", Number(state.session_id)},
                 {"requested_revision", Number(state.revision)},
                 {"matched_frames", Number(matched)},
                 {"requested", Settings(state.requested)},
                 {"accepted_request", Settings(state.accepted)},
                 {"actual_last", Metadata(last)}});
}
}  // namespace
int ControlNativeTuning(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "camera-ctl tuning get|set|reset|verify --native [--device 0] [--width 1920 "
                 "--height 1080 --fps 30]\n"
                 "  [--frames 60 --warmup 30 --cycles 1 --report file.json]\n"
                 "  set: --ae auto|manual --awb auto|manual|daylight|... --ae-lock on|off "
                 "--awb-lock on|off\n"
                 "       --exposure-us 8000 --gain 2 --isp-gain 1 --ev 0 --antibanding "
                 "auto|off|50hz|60hz\n"
                 "       --exposure-range-us 100,20000 --gain-range 1,8 --wb-gains R,Gr,Gb,B\n"
                 "ISOLATED native session; stop Camera Service first. Settings last only for this "
                 "invocation.\n";
    return 0;
  }
  Options options;
  try {
    options = Parse(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << Object({{"error", Quote(e.what())}}) << '\n';
    return 2;
  }
  cam::NativeArgusCapture capture;
  std::vector<Json> phases, observed;
  Json capabilities = "null", defaults = "null";
  std::string failure;
  bool balanced = true;
  try {
    for (int cycle = 0; cycle < options.cycles; ++cycle) {
      std::string error;
      Require(capture.Start(options.config, &error), error);
      cam::CameraTuningState state;
      Require(capture.GetTuning(&state, &error), error);
      capabilities = Capabilities(state.capabilities);
      defaults = Settings(state.defaults);
      phases.push_back(Phase(capture, "warmup", options.warmup, false, &observed));
      if (options.action == "verify") {
        auto settings = state.defaults;
        settings.ae_mode = "manual";
        settings.ae_lock = false;
        settings.exposure_compensation_ev = 0;
        const auto exposure = std::clamp<std::uint64_t>(8000000, state.capabilities.exposure_ns.min,
                                                        state.capabilities.exposure_ns.max);
        settings.exposure_ns = {exposure, exposure};
        const auto gain = std::clamp(2.0f, state.capabilities.sensor_gain.min,
                                     state.capabilities.sensor_gain.max);
        settings.sensor_gain = {gain, gain};
        settings.isp_gain = {state.capabilities.isp_gain.min, state.capabilities.isp_gain.min};
        settings.awb_mode = "manual";
        settings.awb_lock = false;
        settings.wb_gains = {1.5f, 1.0f, 1.0f, 1.25f};
        Require(capture.SetTuning(settings, nullptr, &error), error);
        phases.push_back(Phase(capture, "manual", options.frames, true, &observed));
        Require(capture.ResetTuning(nullptr, &error), error);
        phases.push_back(Phase(capture, "auto-restored", options.frames, true, &observed));
        settings = state.defaults;
        settings.exposure_compensation_ev =
            std::min(1.0f, state.capabilities.exposure_compensation_ev.max);
        settings.antibanding = "50hz";
        Require(capture.SetTuning(settings, nullptr, &error), error);
        phases.push_back(Phase(capture, "ev-50hz", options.frames, true, &observed));
        settings.exposure_compensation_ev =
            std::max(-1.0f, state.capabilities.exposure_compensation_ev.min);
        settings.antibanding = "60hz";
        Require(capture.SetTuning(settings, nullptr, &error), error);
        phases.push_back(Phase(capture, "ev-60hz", options.frames, true, &observed));
        settings = state.defaults;
        settings.ae_lock = settings.awb_lock = true;
        Require(capture.SetTuning(settings, nullptr, &error), error);
        phases.push_back(Phase(capture, "locked", options.frames, true, &observed));
        Require(capture.ResetTuning(nullptr, &error), error);
        phases.push_back(Phase(capture, "reset", options.frames, true, &observed));
        cam::CameraTuningState before;
        Require(capture.GetTuning(&before, &error), error);
        auto invalid = state.defaults;
        invalid.sensor_gain = {0, 0};
        const bool rejected = !capture.SetTuning(invalid, nullptr, &error);
        Require(rejected, "out-of-range gain accepted");
        cam::CameraTuningState after;
        Require(capture.GetTuning(&after, &error), error);
        Require(after.revision == before.revision, "rejected update changed revision");
      } else {
        if (options.action == "set") {
          auto settings = state.requested;
          Patch(&settings, options.values);
          Require(capture.SetTuning(settings, nullptr, &error), error);
        } else if (options.action == "reset")
          Require(capture.ResetTuning(nullptr, &error), error);
        phases.push_back(Phase(capture, options.action, options.frames, true, &observed));
      }
      Require(capture.Stop(&error) == cam::NativeStopStatus::kStopped, error);
      balanced = balanced && capture.stats().ResourcesBalanced();
    }
  } catch (const std::exception& e) {
    failure = e.what();
  }
  std::string stop_error;
  if (capture.Stop(&stop_error) != cam::NativeStopStatus::kStopped)
    failure += " stop: " + stop_error;
  const auto stats = capture.stats();
  balanced = balanced && stats.ResourcesBalanced();
  const bool passed = failure.empty() && balanced;
  const auto report = Object({{"schema_version", "1"},
                              {"isolated_native_session", "true"},
                              {"passed", Bool(passed)},
                              {"argus_version", Quote(capture.version())},
                              {"capabilities", capabilities},
                              {"defaults", defaults},
                              {"phases", Array(phases)},
                              {"frames", Array(observed)},
                              {"frames_acquired", Number(stats.frames_acquired)},
                              {"leases_returned", Number(stats.leases_returned)},
                              {"buffers_released", Number(stats.buffers_released)},
                              {"allocated", Number(stats.allocated)},
                              {"destroyed", Number(stats.destroyed)},
                              {"resources_balanced", Bool(balanced)},
                              {"error", Quote(failure)}});
  if (!options.report.empty()) {
    std::ofstream output(options.report);
    output << report << '\n';
    if (!output) {
      std::cerr << "cannot write tuning report\n";
      return 2;
    }
  }
  std::cout << report << '\n';
  return passed ? 0 : 1;
}
}  // namespace cockpit::camera_ctl
