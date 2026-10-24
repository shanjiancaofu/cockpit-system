#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#include "diagnostics.h"

namespace cockpit::camera_diag {
bool ParseOptions(int argc, char** argv, Options* options, std::string* error) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      options->help = true;
      return true;
    }
    if (arg == "--inspect-only") {
      options->inspect_only = true;
      continue;
    }
    if (i + 1 == argc) {
      *error = "missing value for " + arg;
      return false;
    }
    const std::string value = argv[++i];
    if (arg == "--device")
      options->device = value;
    else if (arg == "--output")
      options->output = value;
    else {
      int number = 0;
      try {
        std::size_t end = 0;
        number = std::stoi(value, &end);
        if (end != value.size() || number <= 0) throw std::invalid_argument("range");
      } catch (...) {
        *error = "invalid positive integer for " + arg;
        return false;
      }
      if (arg == "--width")
        options->width = number;
      else if (arg == "--height")
        options->height = number;
      else if (arg == "--fps")
        options->fps = number;
      else if (arg == "--frames")
        options->frames = number;
      else if (arg == "--cycles")
        options->cycles = number;
      else if (arg == "--timeout-ms")
        options->timeout_ms = number;
      else {
        *error = "unknown option " + arg;
        return false;
      }
    }
  }
  if (options->device.empty() || options->output.empty()) {
    *error = "device/output cannot be empty";
    return false;
  }
  return true;
}

int RunDiagnostics(int argc, char** argv, bool stress_only) {
  Options options;
  if (stress_only) {
    options.cycles = 10;
    options.output = "camera_stress.json";
  }
  std::string error;
  if (!ParseOptions(argc, argv, &options, &error)) {
    std::cerr << error << '\n';
    return 2;
  }
  if (options.help) {
    std::cout << (stress_only ? "camera-stress" : "camera-diag")
              << " [--device /dev/video0] [--width 1920] [--height 1080] [--fps 30]"
                 " [--frames 300] [--cycles 1] [--timeout-ms 5000] [--output report.json] "
                 "[--inspect-only]\n";
    return 0;
  }
  std::ofstream report_check(options.output);
  std::ofstream error_log(options.output + ".errors.log");
  if (!report_check || !error_log) {
    std::cerr << "cannot open report/error log\n";
    return 2;
  }
  report_check.close();
  const double started =
      std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::vector<std::string> errors;
  const Json devices = stress_only ? "null" : InspectDevices(&errors);
  const Json media = stress_only ? "null" : InspectMedia(&errors);
  const Json sensor = stress_only ? "null" : InspectSensor(options.device, &errors);
  const Json controls = stress_only ? "null" : InspectControls(options.device, &errors);
  std::vector<Json> sub_controls;
  if (!stress_only)
    for (const auto& path : DevicePaths("v4l-subdev"))
      sub_controls.push_back(
          Object({{"device", Quote(path)}, {"controls", InspectControls(path, &errors)}}));
  bool capture_ok = false;
  const auto capture = options.inspect_only ? "null" : CaptureStress(options, &capture_ok, &errors);
  std::vector<Json> logs;
  bool logs_available = true;
  const std::regex camera("camera|tegra.*(vi|csi|camrtc)|imx219|argus", std::regex::icase);
  const std::regex anomaly(
      "error|timeout|timed out|uncorr|no reply|CHANSEL_(NOMATCH|SHORT|FAULT)|overflow|failed",
      std::regex::icase);
  for (const auto& source : {std::string("kernel"), std::string("argus")}) {
    std::vector<std::string> args{"journalctl", "--no-pager", "-o",
                                  "short-iso",  "--since",    "@" + Number(started)};
    if (source == "kernel")
      args.push_back("-k");
    else {
      args.push_back("-u");
      args.push_back("nvargus-daemon");
    }
    const auto result = RunCommand(args);
    const bool available =
        result.code == 0 && !result.truncated &&
        !std::regex_search(result.output,
                           std::regex("permission denied|not seeing messages|No journal files",
                                      std::regex::icase));
    logs_available = logs_available && available;
    std::vector<std::string> anomalies;
    std::istringstream lines(result.output);
    std::string line;
    while (std::getline(lines, line))
      if ((source == "argus" || std::regex_search(line, camera)) &&
          std::regex_search(line, anomaly)) {
        anomalies.push_back(line);
        errors.push_back(source + ": " + line);
      }
    logs.push_back(Object({{"source", Quote(source)},
                           {"available", Boolean(available)},
                           {"returncode", Number(result.code)},
                           {"truncated", Boolean(result.truncated)},
                           {"output", Quote(result.output)},
                           {"anomalies", Strings(anomalies)}}));
  }
  if (!logs_available)
    errors.push_back("kernel/Argus logs unavailable; driver health is unverified");
  const bool healthy = errors.empty() && capture_ok;
  const std::string status = !errors.empty()        ? "failed"
                             : options.inspect_only ? "unverified"
                             : healthy              ? "healthy"
                                                    : "failed";
  const auto report =
      Object({{"schema_version", "1"},
              {"status", Quote(status)},
              {"healthy", Boolean(healthy)},
              {"health_scope", Quote("V4L2 RAW sensor capture and driver logs; Hardware ISP "
                                     "capture is validated separately by libargus-capture-probe")},
              {"started_unix_s", Number(started)},
              {"device", Quote(options.device)},
              {"environment", InspectEnvironment()},
              {"devices", devices},
              {"media", media},
              {"sensor", sensor},
              {"controls", controls},
              {"subdevice_controls", Array(sub_controls)},
              {"capture", capture},
              {"logs", Array(logs)},
              {"errors", Strings(errors)}});
  for (const auto& item : errors) error_log << item << '\n';
  error_log.close();
  if (!WriteReport(options.output, report) || !error_log) {
    std::cerr << "cannot write report/error log\n";
    return 2;
  }
  std::cout << "status=" << status << " report=" << options.output << " errors=" << errors.size()
            << '\n';
  return healthy ? 0 : errors.empty() && options.inspect_only ? 3 : 1;
}
}  // namespace cockpit::camera_diag
