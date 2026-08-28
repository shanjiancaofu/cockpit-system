#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "cockpit/modules/camera/isp/software_isp.h"

namespace {
double CpuMs(const rusage& usage) {
  return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
         static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 5 || argc > 7) {
    std::cerr
        << "usage: camera-isp-cpu-benchmark RAW WIDTH HEIGHT STRIDE [ITERATIONS] [OUTPUT_BGRX]\n";
    return 2;
  }
  const std::string raw_path = argv[1];
  const std::uint32_t width = static_cast<std::uint32_t>(std::stoul(argv[2]));
  const std::uint32_t height = static_cast<std::uint32_t>(std::stoul(argv[3]));
  const std::size_t bytes_per_line = std::stoul(argv[4]);
  const int iterations = argc >= 6 ? std::stoi(argv[5]) : 300;
  const std::string output_path = argc == 7 ? argv[6] : "";
  if (width == 0 || height == 0 || iterations <= 0) return 2;

  if (bytes_per_line < static_cast<std::size_t>(width) * 2U) return 2;
  const std::size_t raw_size = bytes_per_line * height;
  std::vector<std::uint8_t> raw_data(raw_size);
  std::ifstream input(raw_path, std::ios::binary);
  input.read(reinterpret_cast<char*>(raw_data.data()), static_cast<std::streamsize>(raw_size));
  if (input.gcount() != static_cast<std::streamsize>(raw_size)) {
    std::cerr << "RAW file is shorter than width*height*2\n";
    return 1;
  }

  cockpit::camera::SoftwareIsp isp;
  cockpit::camera::RawBayerFrame raw;
  raw.width = width;
  raw.height = height;
  raw.bytes_per_line = bytes_per_line;
  raw.bytes_used = static_cast<std::uint32_t>(raw_size);
  raw.data = raw_data;
  cockpit::camera::CameraFrame output;
  for (int i = 0; i < 10; ++i) {
    std::string error;
    if (!isp.Process(raw, &output, &error)) {
      std::cerr << error << '\n';
      return 1;
    }
  }

  rusage usage_started{};
  getrusage(RUSAGE_SELF, &usage_started);
  const auto wall_started = std::chrono::steady_clock::now();
  double total_isp_ms = 0.0;
  cockpit::camera::SoftwareIspTimingMs timing;
  for (int i = 0; i < iterations; ++i) {
    std::string error;
    const auto started = std::chrono::steady_clock::now();
    if (!isp.Process(raw, &output, &error, &timing)) {
      std::cerr << error << '\n';
      return 1;
    }
    total_isp_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
  }
  const auto wall_elapsed = std::chrono::steady_clock::now() - wall_started;
  rusage usage_finished{};
  getrusage(RUSAGE_SELF, &usage_finished);
  const double wall_ms = std::chrono::duration<double, std::milli>(wall_elapsed).count();
  const double cpu_ms = CpuMs(usage_finished) - CpuMs(usage_started);
  if (!output_path.empty()) {
    std::ofstream output_file(output_path, std::ios::binary);
    output_file.write(reinterpret_cast<const char*>(output.data.data()),
                      static_cast<std::streamsize>(output.data.size()));
    if (!output_file) {
      std::cerr << "failed to write CPU output: " << output_path << '\n';
      return 1;
    }
    std::cout << "output_path=" << output_path << '\n';
  }
  std::cout << std::fixed << std::setprecision(3) << "iterations=" << iterations
            << " pixels=" << width * height << '\n'
            << "cpu_isp_mean_ms=" << total_isp_ms / iterations << '\n'
            << "cpu_process_wall_mean_ms=" << wall_ms / iterations << '\n'
            << "process_cpu_percent=" << (wall_ms > 0.0 ? cpu_ms / wall_ms * 100.0 : 0.0) << '\n'
            << "last_raw_unpack_ms=" << timing.raw_unpack
            << " last_normalize_ms=" << timing.normalize << " last_demosaic_ms=" << timing.demosaic
            << " last_color_correction_ms=" << timing.color_correction
            << " last_output_ms=" << timing.output << '\n';
  return 0;
}
