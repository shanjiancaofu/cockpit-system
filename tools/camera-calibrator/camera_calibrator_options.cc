#include <cmath>
#include <iostream>
#include <string>

#include "camera_calibrator_internal.h"

namespace cockpit::camera_calibrator {

void Usage() {
  std::cout << R"(camera-calibrator [options]
  --device nvargus://0       Capture device (default: nvargus://0)
  --board-profile NAME       Board profile: q12-70-5 (12x9 squares, 11x8 corners, 5 mm)
  --input-dir DIR            Calibrate existing image files instead of capturing
  --output-dir DIR           Output directory
  --width N --height N       Capture dimensions (default: 1920x1080)
  --fps N                    Capture frame rate (default: 30)
  --frames N                 Accepted samples to collect (default: 30)
  --max-candidates N         Hard candidate-pool limit (default: 50)
  --timeout-seconds N        Capture timeout (default: 120)
  --corners-x N --corners-y N Chessboard inner corners (default: 9x6)
  --square-size M            Square size in metres (default: 0.025)
  --blur-min N               Laplacian variance threshold
  --mean-min N --mean-max N  Mean grayscale range
  --area-min N --area-max N  Chessboard image-area range
  --grid-required N          Minimum occupied 3x3 cells
  --duplicate-threshold N    Mean absolute image difference threshold
  --near-distance N          Near-distance upper bound in metres
  --far-distance N           Far-distance lower bound in metres
  --tilt-threshold N         Horizontal/vertical tilt threshold in degrees
  --help
)";
}

bool TakeValue(int& index, int argc, char** argv, std::string* value) {
  if (index + 1 >= argc) return false;
  *value = argv[++index];
  return true;
}

bool ParseInt(const std::string& text, int* value) {
  try {
    std::size_t consumed = 0;
    const int parsed = std::stoi(text, &consumed);
    if (consumed != text.size()) return false;
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseDouble(const std::string& text, double* value) {
  try {
    std::size_t consumed = 0;
    const double parsed = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

ParseResult ParseOptions(int argc, char** argv, Options* options) {
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      Usage();
      return ParseResult::kHelp;
    }
    std::string value;
    auto require_value = [&](const char* name) {
      if (!TakeValue(index, argc, argv, &value)) {
        std::cerr << name << " requires a value\n";
        return false;
      }
      return true;
    };
    if (arg == "--device") {
      if (!require_value("--device")) return ParseResult::kError;
      options->device = value;
    } else if (arg == "--board-profile") {
      if (!require_value("--board-profile")) return ParseResult::kError;
      options->board_profile = value;
    } else if (arg == "--input-dir") {
      if (!require_value("--input-dir")) return ParseResult::kError;
      options->input_dir = value;
    } else if (arg == "--output-dir") {
      if (!require_value("--output-dir")) return ParseResult::kError;
      options->output_dir = value;
    } else if (arg == "--width") {
      if (!require_value("--width") || !ParseInt(value, &options->width))
        return ParseResult::kError;
    } else if (arg == "--height") {
      if (!require_value("--height") || !ParseInt(value, &options->height))
        return ParseResult::kError;
    } else if (arg == "--fps") {
      if (!require_value("--fps") || !ParseInt(value, &options->fps)) return ParseResult::kError;
    } else if (arg == "--frames") {
      if (!require_value("--frames") || !ParseInt(value, &options->frames))
        return ParseResult::kError;
    } else if (arg == "--max-candidates") {
      if (!require_value("--max-candidates") || !ParseInt(value, &options->max_candidates))
        return ParseResult::kError;
    } else if (arg == "--timeout-seconds") {
      if (!require_value("--timeout-seconds") || !ParseInt(value, &options->timeout_seconds))
        return ParseResult::kError;
    } else if (arg == "--corners-x") {
      if (!require_value("--corners-x") || !ParseInt(value, &options->corners_x))
        return ParseResult::kError;
      options->corners_x_explicit = true;
    } else if (arg == "--corners-y") {
      if (!require_value("--corners-y") || !ParseInt(value, &options->corners_y))
        return ParseResult::kError;
      options->corners_y_explicit = true;
    } else if (arg == "--square-size") {
      if (!require_value("--square-size") || !ParseDouble(value, &options->square_size))
        return ParseResult::kError;
      options->square_size_explicit = true;
    } else if (arg == "--blur-min") {
      if (!require_value("--blur-min") || !ParseDouble(value, &options->blur_min))
        return ParseResult::kError;
      options->blur_min_explicit = true;
    } else if (arg == "--mean-min") {
      if (!require_value("--mean-min") || !ParseDouble(value, &options->mean_min))
        return ParseResult::kError;
    } else if (arg == "--mean-max") {
      if (!require_value("--mean-max") || !ParseDouble(value, &options->mean_max))
        return ParseResult::kError;
    } else if (arg == "--area-min") {
      if (!require_value("--area-min") || !ParseDouble(value, &options->area_min))
        return ParseResult::kError;
      options->area_min_explicit = true;
    } else if (arg == "--area-max") {
      if (!require_value("--area-max") || !ParseDouble(value, &options->area_max))
        return ParseResult::kError;
      options->area_max_explicit = true;
    } else if (arg == "--grid-required") {
      if (!require_value("--grid-required") || !ParseInt(value, &options->grid_required))
        return ParseResult::kError;
      options->grid_required_explicit = true;
    } else if (arg == "--duplicate-threshold") {
      if (!require_value("--duplicate-threshold") ||
          !ParseDouble(value, &options->duplicate_threshold))
        return ParseResult::kError;
      options->duplicate_threshold_explicit = true;
    } else if (arg == "--near-distance") {
      if (!require_value("--near-distance") || !ParseDouble(value, &options->near_distance_m))
        return ParseResult::kError;
      options->near_distance_explicit = true;
    } else if (arg == "--far-distance") {
      if (!require_value("--far-distance") || !ParseDouble(value, &options->far_distance_m))
        return ParseResult::kError;
      options->far_distance_explicit = true;
    } else if (arg == "--tilt-threshold") {
      if (!require_value("--tilt-threshold") || !ParseDouble(value, &options->tilt_threshold_deg))
        return ParseResult::kError;
      options->tilt_threshold_explicit = true;
    } else {
      std::cerr << "unknown option: " << arg << "\n";
      Usage();
      return ParseResult::kError;
    }
  }
  if (!options->board_profile.empty()) {
    if (options->board_profile != "q12-70-5") {
      std::cerr << "unknown board profile: " << options->board_profile << "\n";
      return ParseResult::kError;
    }
    if ((options->corners_x_explicit && options->corners_x != 11) ||
        (options->corners_y_explicit && options->corners_y != 8) ||
        (options->square_size_explicit && options->square_size != 0.005)) {
      std::cerr << "board profile q12-70-5 conflicts with explicit board geometry\n";
      return ParseResult::kError;
    }
    options->corners_x = 11;
    options->corners_y = 8;
    options->square_size = 0.005;
    if (!options->area_min_explicit) options->area_min = 0.0005;
    if (!options->grid_required_explicit) options->grid_required = 1;
    if (!options->near_distance_explicit) options->near_distance_m = 0.25;
    if (!options->far_distance_explicit) options->far_distance_m = 0.55;
    if (!options->tilt_threshold_explicit) options->tilt_threshold_deg = 12.0;
  }
  if (options->width <= 0 || options->height <= 0 || options->fps <= 0 || options->frames <= 0 ||
      options->max_candidates < options->frames || options->max_candidates > 100 ||
      options->timeout_seconds <= 0 || options->corners_x <= 1 || options->corners_y <= 1 ||
      options->square_size <= 0.0 || options->area_min < 0.0 || options->area_max > 1.0 ||
      options->area_min >= options->area_max || options->grid_required < 1 ||
      options->grid_required > 9 || options->duplicate_threshold < 0.0 ||
      options->near_distance_m <= 0.0 || options->far_distance_m <= options->near_distance_m ||
      options->tilt_threshold_deg <= 0.0) {
    std::cerr << "invalid calibration options\n";
    return ParseResult::kError;
  }
  return ParseResult::kOk;
}

}  // namespace cockpit::camera_calibrator
