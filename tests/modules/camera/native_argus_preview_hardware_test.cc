#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "cockpit/driver/camera/capture/native_argus_preview_source.h"

int main(int argc, char** argv) {
  using namespace cockpit::camera;
  std::uint32_t frames = 3000;
  std::uint32_t cycles = 10;
  const std::string h264_output = argc > 3 ? argv[3] : std::string();
  if (argc > 1) frames = static_cast<std::uint32_t>(std::stoul(argv[1]));
  if (argc > 2) cycles = static_cast<std::uint32_t>(std::stoul(argv[2]));
  if (!frames || !cycles || !NativeArgusCapture::IsSupported()) {
    std::cerr << "native Argus preview hardware test requires the Native SDK\n";
    return 2;
  }

  for (std::uint32_t cycle = 0; cycle < cycles; ++cycle) {
    NativeArgusPreviewSource source;
    std::atomic<std::uint32_t> delivered{0};
    CameraPreviewConfig config;
    config.device = "nvargus://0";
    config.width = 1920;
    config.height = 1080;
    config.fps = 30;
    if (!h264_output.empty()) {
      config.h264_output_path = h264_output + "." + std::to_string(cycle) + ".h264";
      config.h264_width = 1280;
      config.h264_height = 720;
      config.h264_bitrate = 2'000'000;
    }
    std::string error;
    if (!source.Start(
            config,
            [&delivered](CameraFrame frame) {
              if (frame.IsValid()) delivered.fetch_add(1, std::memory_order_relaxed);
            },
            &error)) {
      std::cerr << "start failed at cycle " << cycle << ": " << error << '\n';
      return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(130);
    while (delivered.load(std::memory_order_relaxed) < frames &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (delivered.load(std::memory_order_relaxed) < frames) {
      std::cerr << "preview stalled at cycle " << cycle
                << " frames=" << delivered.load(std::memory_order_relaxed) << '\n';
      source.Stop();
      return 1;
    }
    source.Stop();
    NativePreviewStatus status;
    assert(source.GetNativeStatus(&status));
    const auto& capture = status.capture;
    if (!capture.ResourcesBalanced() || !status.vic.ResourcesBalanced() || status.broker.open ||
        capture.frames_acquired != capture.leases_returned ||
        capture.leases_returned != capture.buffers_released) {
      std::cerr << "resource imbalance at cycle " << cycle << '\n';
      return 1;
    }
    if (h264_output.empty() && status.encoder.frames_submitted != 0) {
      std::cerr << "unexpected encoder activity without an output path\n";
      return 1;
    }
    if (!h264_output.empty() && status.encoder.frames_submitted == 0) {
      std::cerr << "encoder produced no frames at cycle " << cycle << '\n';
      return 1;
    }
  }
  return 0;
}
