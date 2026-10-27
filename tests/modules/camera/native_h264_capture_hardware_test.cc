#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "cockpit/driver/camera/encode/h264_encoder.h"
#include "cockpit/driver/camera/frames/frame_broker.h"
#include "cockpit/driver/camera/transform/vic_transform.h"

int main(int argc, char** argv) {
  using namespace cockpit::camera;
  const std::uint32_t frames = argc > 1 ? static_cast<std::uint32_t>(std::stoul(argv[1])) : 300;
  const std::string output = argc > 2 ? argv[2] : "/tmp/cockpit-native.h264";
  const std::uint32_t width = argc > 3 ? static_cast<std::uint32_t>(std::stoul(argv[3])) : 1280;
  const std::uint32_t height = argc > 4 ? static_cast<std::uint32_t>(std::stoul(argv[4])) : 720;
  if (!frames || !NativeArgusCapture::IsSupported() || !VicTransform::IsSupported() ||
      !H264Encoder::IsSupported()) {
    std::cerr << "Native Argus, VIC and H264 dependencies are required\n";
    return 2;
  }

  NativeArgusCapture capture;
  NativeFrameBroker broker;
  VicTransform vic;
  H264Encoder encoder;
  H264FileWriter writer;
  std::string error;
  NativeCaptureConfig capture_config;
  capture_config.device_index = 0;
  capture_config.width = 1920;
  capture_config.height = 1080;
  capture_config.fps = 30;
  if (!capture.Start(capture_config, &error)) {
    std::cerr << "capture start failed: " << error << '\n';
    return 1;
  }
  CameraTuningState tuning;
  if (!capture.GetTuning(&tuning, &error) || !broker.BeginSession(tuning.session_id)) {
    std::cerr << "native session setup failed: " << error << '\n';
    return 1;
  }
  H264EncoderConfig encoder_config;
  encoder_config.width = width;
  encoder_config.height = height;
  encoder_config.bitrate = width == 1920 && height == 1080 ? 4'000'000 : 2'000'000;
  if (!encoder.Start(encoder_config, &error) || !writer.Open(output, &error)) {
    std::cerr << "encoder setup failed: " << error << '\n';
    return 1;
  }
  for (std::uint32_t index = 0; index < frames; ++index) {
    FrameLease lease;
    if (capture.Acquire(&lease, 3000, &error) != NativeAcquireStatus::kFrame ||
        broker.Publish(std::move(lease)) != FramePublishStatus::kPublished) {
      std::cerr << "capture/broker failed at frame " << index << ": " << error << '\n';
      return 1;
    }
    auto input = broker.TryPop(FrameConsumer::kTransform);
    CpuVideoFrame cpu;
    if (!input || !vic.Convert(input, {width, height, VicOutputFormat::kI420}, &cpu, &error)) {
      std::cerr << "VIC failed at frame " << index << ": " << error << '\n';
      return 1;
    }
    input.Reset();
    std::vector<H264Packet> packets;
    if (!encoder.Encode(cpu, &packets, &error)) {
      std::cerr << "H264 encode failed at frame " << index << ": " << error << '\n';
      return 1;
    }
    for (const auto& packet : packets)
      if (!writer.Write(packet, &error)) {
        std::cerr << "H264 write failed: " << error << '\n';
        return 1;
      }
    while (auto preview = broker.TryPop(FrameConsumer::kPreview)) preview.Reset();
  }
  std::vector<H264Packet> packets;
  if (!encoder.Flush(&packets, &error)) {
    std::cerr << "H264 flush failed: " << error << '\n';
    return 1;
  }
  for (const auto& packet : packets)
    if (!writer.Write(packet, &error)) {
      std::cerr << "H264 flush write failed: " << error << '\n';
      return 1;
    }
  if (!writer.Close(&error)) {
    std::cerr << error << '\n';
    return 1;
  }
  broker.Close();
  if (capture.Stop(&error) != NativeStopStatus::kStopped || !capture.stats().ResourcesBalanced() ||
      !vic.Reset(&error)) {
    std::cerr << "resource cleanup failed: " << error << '\n';
    return 1;
  }
  const auto stats = encoder.stats();
  if (stats.frames_submitted != frames || stats.bytes_emitted == 0 || stats.keyframes == 0) {
    std::cerr << "unexpected encoder stats\n";
    return 1;
  }
  std::cout << "frames=" << stats.frames_submitted << " packets=" << stats.packets_emitted
            << " bytes=" << stats.bytes_emitted << " keyframes=" << stats.keyframes << '\n';
  return 0;
}
