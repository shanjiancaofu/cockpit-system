#include "cockpit/drivers/v4l2/v4l2_mmap_capture.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void Usage() {
  std::cout << "v4l2-mmap-capture [--device /dev/video0] [--width 1920] [--height 1080] "
               "[--fps 30] [--frames 10] [--output-dir DIR]\n";
}
}  // namespace

int main(int argc, char** argv) {
  cockpit::camera::V4l2MmapConfig config;
  std::string output_dir = "_output/runtime/v4l2-mmap";
  int frames = 10;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      Usage();
      return 0;
    }
    if (index + 1 >= argc) {
      std::cerr << arg << " requires a value\n";
      return 2;
    }
    const std::string value = argv[++index];
    try {
      if (arg == "--device")
        config.device = value;
      else if (arg == "--width")
        config.width = std::stoul(value);
      else if (arg == "--height")
        config.height = std::stoul(value);
      else if (arg == "--fps")
        config.fps = std::stoul(value);
      else if (arg == "--frames")
        frames = std::stoi(value);
      else if (arg == "--output-dir")
        output_dir = value;
      else {
        std::cerr << "unknown option: " << arg << '\n';
        return 2;
      }
    } catch (...) {
      std::cerr << "invalid value for " << arg << '\n';
      return 2;
    }
  }
  if (frames <= 0) {
    std::cerr << "frames must be positive\n";
    return 2;
  }
  cockpit::camera::V4l2MmapCapture capture;
  std::string error;
  if (!capture.Start(config, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  std::filesystem::create_directories(output_dir);
  std::ofstream metadata(std::filesystem::path(output_dir) / "metadata.tsv");
  metadata << "frame\tsequence\ttimestamp_ns\tbytesused\tbytesperline\n";
  std::cout << "driver=" << capture.driver() << " card=" << capture.card() << '\n'
            << "format=RG10 " << capture.width() << 'x' << capture.height()
            << " bytesperline=" << capture.bytes_per_line() << " sizeimage=" << capture.size_image()
            << '\n';
  for (int index = 0; index < frames; ++index) {
    cockpit::camera::V4l2RawFrame frame;
    if (!capture.WaitFrame(&frame, 5000, &error)) {
      std::cerr << error << '\n';
      return 1;
    }
    const auto path =
        std::filesystem::path(output_dir) / ("frame-" + std::to_string(index) + ".raw");
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(frame.data.data()),
                 static_cast<std::streamsize>(frame.data.size()));
    if (!output) {
      std::cerr << "failed to write " << path << '\n';
      return 1;
    }
    metadata << index << '\t' << frame.sequence << '\t' << frame.timestamp_ns << '\t'
             << frame.bytes_used << '\t' << frame.bytes_per_line << '\n';
    std::cout << "frame=" << index << " sequence=" << frame.sequence
              << " bytesused=" << frame.bytes_used << " timestamp_ns=" << frame.timestamp_ns
              << '\n';
  }
  capture.Stop();
  std::cout << "captured=" << frames << " output=" << output_dir << '\n';
  return 0;
}
