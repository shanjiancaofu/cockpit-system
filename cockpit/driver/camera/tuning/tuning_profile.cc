#include "cockpit/driver/camera/tuning/tuning_profile.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace cockpit::camera {
namespace {
constexpr std::size_t kMaxProfileBytes = 65536;
void Require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
void Error(std::string* error, const std::string& message) {
  if (error) *error = message;
}
void Keys(const YAML::Node& node, std::initializer_list<const char*> fields,
          const std::string& label) {
  Require(node.IsMap(), label + " must be a map");
  std::set<std::string> remaining(fields.begin(), fields.end());
  for (const auto& item : node) {
    Require(item.first.IsScalar(), label + " has a non-scalar key");
    const auto key = item.first.Scalar();
    Require(remaining.erase(key) == 1, label + " has an unknown or duplicate field: " + key);
  }
  Require(remaining.empty(), label + " is missing required fields");
}
std::string Text(const YAML::Node& node) {
  Require(node.IsScalar(), "expected a scalar string");
  return node.Scalar();
}
std::uint64_t Unsigned(const YAML::Node& node, std::uint64_t max) {
  const auto text = Text(node);
  std::uint64_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  Require(!text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
              value <= max,
          "expected an unsigned decimal integer within limits: " + text);
  return value;
}
float Float(const YAML::Node& node) {
  Require(node.IsScalar(), "expected a finite scalar number");
  const auto value = node.as<double>();
  Require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
          "number is not a finite float");
  return static_cast<float>(value);
}
bool Boolean(const YAML::Node& node) {
  const auto value = Text(node);
  Require(value == "true" || value == "false", "boolean must be true or false");
  return value == "true";
}
void Length(const YAML::Node& node, std::size_t count) {
  Require(node.IsSequence() && node.size() == count, "wrong tuple size");
}
TuningRange<std::uint64_t> Exposure(const YAML::Node& node) {
  Length(node, 2);
  return {Unsigned(node[0], UINT64_MAX), Unsigned(node[1], UINT64_MAX)};
}
TuningRange<float> Gain(const YAML::Node& node) {
  Length(node, 2);
  return {Float(node[0]), Float(node[1])};
}
std::string Encode(const TuningProfile& p) {
  YAML::Emitter out;
  out.SetFloatPrecision(std::numeric_limits<float>::max_digits10);
  out << YAML::BeginMap << YAML::Key << "schema_version" << YAML::Value << p.version << YAML::Key
      << "name" << YAML::Value << YAML::DoubleQuoted << p.name << YAML::Key << "target"
      << YAML::Value << YAML::BeginMap << YAML::Key << "backend" << YAML::Value << "native_argus"
      << YAML::Key << "device_index" << YAML::Value << p.target.device_index << YAML::Key
      << "sensor_model" << YAML::Value << YAML::DoubleQuoted << p.target.sensor_model << YAML::Key
      << "sensor_mode" << YAML::Value << p.target.sensor_mode << YAML::Key << "width" << YAML::Value
      << p.target.width << YAML::Key << "height" << YAML::Value << p.target.height << YAML::Key
      << "fps" << YAML::Value << p.target.fps << YAML::EndMap << YAML::Key << "settings"
      << YAML::Value << YAML::BeginMap;
  const auto& s = p.settings;
  out << YAML::Key << "ae_mode" << YAML::Value << s.ae_mode << YAML::Key << "awb_mode"
      << YAML::Value << s.awb_mode << YAML::Key << "antibanding" << YAML::Value << s.antibanding
      << YAML::Key << "ae_lock" << YAML::Value << s.ae_lock << YAML::Key << "awb_lock"
      << YAML::Value << s.awb_lock << YAML::Key << "exposure_ns" << YAML::Value << YAML::Flow
      << YAML::BeginSeq << s.exposure_ns.min << s.exposure_ns.max << YAML::EndSeq << YAML::Key
      << "sensor_gain" << YAML::Value << YAML::Flow << YAML::BeginSeq << s.sensor_gain.min
      << s.sensor_gain.max << YAML::EndSeq << YAML::Key << "isp_gain" << YAML::Value << YAML::Flow
      << YAML::BeginSeq << s.isp_gain.min << s.isp_gain.max << YAML::EndSeq << YAML::Key
      << "exposure_compensation_ev" << YAML::Value << s.exposure_compensation_ev << YAML::Key
      << "wb_gains" << YAML::Value << YAML::Flow << YAML::BeginSeq;
  for (auto gain : s.wb_gains) out << gain;
  out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
  Require(out.good(), "cannot encode tuning profile");
  return std::string(out.c_str()) + "\n";
}
#ifndef _WIN32
struct File {
  int fd = -1;
  ~File() {
    if (fd >= 0) close(fd);
  }
};
void System(bool ok, const char* action) {
  if (!ok) throw std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}
#endif
}  // namespace

TuningProfile MakeTuningProfile(const CameraTuningState& state, const std::string& name) {
  return {TuningProfile::kVersion, name, state.device, state.accepted};
}
bool ValidateTuningProfile(const TuningProfile& p, std::string* error) {
  if (p.version != TuningProfile::kVersion || p.name.empty() || p.name.size() > 128 ||
      p.target.sensor_model.empty() || p.target.sensor_model.size() > 128 ||
      p.target.sensor_mode < 0 || !p.target.width || !p.target.height || !p.target.fps) {
    Error(error, "unsupported profile version or invalid name/target");
    return false;
  }
  CameraTuningCapabilities broad;
  broad.frame_duration_ns = (1000000000ULL + p.target.fps - 1) / p.target.fps;
  broad.exposure_ns = {1, broad.frame_duration_ns};
  broad.sensor_gain =
      broad.isp_gain = {std::numeric_limits<float>::min(), std::numeric_limits<float>::max()};
  broad.exposure_compensation_ev = {-std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max()};
  if (!ValidateCameraTuning(p.settings, broad, error)) return false;
  if (std::any_of(p.settings.wb_gains.begin(), p.settings.wb_gains.end(), [](float g) {
        return !std::isfinite(g) || g <= 0;
      })) {
    Error(error, "profile WB gains must all be finite and positive");
    return false;
  }
  Error(error, {});
  return true;
}
bool ValidateTuningProfileForDevice(const TuningProfile& p, const NativeCameraDescriptor& d,
                                    const CameraTuningCapabilities& capabilities,
                                    std::string* error) {
  if (!ValidateTuningProfile(p, error)) return false;
  if (p.target.device_index != d.device_index || p.target.sensor_model != d.sensor_model ||
      p.target.sensor_mode != d.sensor_mode || p.target.width != d.width ||
      p.target.height != d.height || p.target.fps != d.fps) {
    Error(error, "profile target does not match active native device/sensor mode/format");
    return false;
  }
  return ValidateCameraTuning(p.settings, capabilities, error);
}
bool DecodeTuningProfile(std::string_view text, TuningProfile* profile, std::string* error) {
  try {
    Require(profile && !text.empty() && text.size() <= kMaxProfileBytes,
            "invalid profile output/size");
    const auto documents = YAML::LoadAll(std::string(text));
    Require(documents.size() == 1, "profile must contain exactly one YAML document");
    const auto root = documents[0];
    Keys(root, {"schema_version", "name", "target", "settings"}, "profile");
    TuningProfile next;
    next.version = Unsigned(root["schema_version"], UINT32_MAX);
    next.name = Text(root["name"]);
    const auto target = root["target"];
    Keys(target,
         {"backend", "device_index", "sensor_model", "sensor_mode", "width", "height", "fps"},
         "target");
    Require(Text(target["backend"]) == "native_argus", "profile is not for Native Hardware ISP");
    next.target.device_index = Unsigned(target["device_index"], UINT32_MAX);
    next.target.sensor_model = Text(target["sensor_model"]);
    next.target.sensor_mode = Unsigned(target["sensor_mode"], std::numeric_limits<int>::max());
    next.target.width = Unsigned(target["width"], UINT32_MAX);
    next.target.height = Unsigned(target["height"], UINT32_MAX);
    next.target.fps = Unsigned(target["fps"], UINT32_MAX);
    const auto settings = root["settings"];
    Keys(settings,
         {"ae_mode", "awb_mode", "ae_lock", "awb_lock", "antibanding", "exposure_ns", "sensor_gain",
          "isp_gain", "exposure_compensation_ev", "wb_gains"},
         "settings");
    auto& s = next.settings;
    s.ae_mode = Text(settings["ae_mode"]);
    s.awb_mode = Text(settings["awb_mode"]);
    s.ae_lock = Boolean(settings["ae_lock"]);
    s.awb_lock = Boolean(settings["awb_lock"]);
    s.antibanding = Text(settings["antibanding"]);
    s.exposure_ns = Exposure(settings["exposure_ns"]);
    s.sensor_gain = Gain(settings["sensor_gain"]);
    s.isp_gain = Gain(settings["isp_gain"]);
    s.exposure_compensation_ev = Float(settings["exposure_compensation_ev"]);
    Length(settings["wb_gains"], 4);
    for (std::size_t i = 0; i < 4; ++i) s.wb_gains[i] = Float(settings["wb_gains"][i]);
    std::string why;
    Require(ValidateTuningProfile(next, &why), why);
    *profile = std::move(next);
    Error(error, {});
    return true;
  } catch (const std::exception& e) {
    Error(error, e.what());
    return false;
  }
}
bool LoadTuningProfile(const std::filesystem::path& path, TuningProfile* profile,
                       std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Error(error, "cannot open tuning profile: " + path.string());
    return false;
  }
  std::string text(kMaxProfileBytes + 1, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  const auto count = input.gcount();
  if (input.bad() || count > static_cast<std::streamsize>(kMaxProfileBytes)) {
    Error(error, "profile read failed or size exceeds 64 KiB");
    return false;
  }
  text.resize(static_cast<std::size_t>(count));
  return DecodeTuningProfile(text, profile, error);
}
TuningProfileSaveResult SaveTuningProfile(const std::filesystem::path& path,
                                          const TuningProfile& profile) {
  TuningProfileSaveResult result;
  if (!ValidateTuningProfile(profile, &result.error)) return result;
#ifdef _WIN32
  result.error = "atomic profile persistence requires POSIX";
#else
  std::string temporary;
  try {
    Require(!path.empty() && !path.filename().empty(), "profile destination is empty");
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    const auto text = Encode(profile);
    Require(text.size() <= kMaxProfileBytes, "encoded profile exceeds 64 KiB");
    File directory;
    directory.fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    System(directory.fd >= 0, "open profile directory");
    auto pattern = (parent / (path.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    File file;
    file.fd = mkstemp(name.data());
    System(file.fd >= 0, "create profile temporary file");
    temporary = name.data();
    System(fcntl(file.fd, F_SETFD, FD_CLOEXEC) != -1, "set profile close-on-exec");
    std::size_t offset = 0;
    while (offset < text.size()) {
      const auto count = write(file.fd, text.data() + offset, text.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      System(count > 0, "write profile");
      offset += static_cast<std::size_t>(count);
    }
    System(fsync(file.fd) == 0, "sync profile");
    const auto fd = file.fd;
    file.fd = -1;
    System(close(fd) == 0, "close profile");
    System(rename(temporary.c_str(), path.c_str()) == 0, "replace profile");
    result.committed = true;
    temporary.clear();
    System(fsync(directory.fd) == 0, "profile replaced but directory sync failed");
    result.durable = true;
  } catch (const std::exception& e) {
    result.error = e.what();
  }
  if (!temporary.empty()) unlink(temporary.c_str());
#endif
  return result;
}
}  // namespace cockpit::camera
