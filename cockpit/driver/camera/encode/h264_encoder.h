#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cockpit/driver/camera/transform/vic_transform.h"

namespace cockpit::camera {

struct H264EncoderConfig {
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  std::uint32_t fps = 30;
  std::uint32_t bitrate = 2'000'000;
  std::uint32_t gop = 30;
  std::uint32_t b_frames = 0;
  std::string preset = "ultrafast";
  std::string tune = "zerolatency";
};

struct H264Packet {
  std::vector<std::uint8_t> data;
  std::int64_t pts = 0;
  std::int64_t dts = 0;
  bool keyframe = false;
};

struct H264EncoderStats {
  std::uint64_t frames_submitted = 0;
  std::uint64_t packets_emitted = 0;
  std::uint64_t keyframes = 0;
  std::uint64_t bytes_emitted = 0;
};

class H264Encoder final {
 public:
  H264Encoder();
  ~H264Encoder();
  H264Encoder(const H264Encoder&) = delete;
  H264Encoder& operator=(const H264Encoder&) = delete;

  static bool IsSupported();
  bool Start(const H264EncoderConfig& config, std::string* error = nullptr);
  bool Encode(const CpuVideoFrame& frame, std::vector<H264Packet>* packets,
              std::string* error = nullptr);
  bool Flush(std::vector<H264Packet>* packets, std::string* error = nullptr);
  void Stop();
  bool running() const;
  H264EncoderStats stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class H264FileWriter final {
 public:
  H264FileWriter();
  ~H264FileWriter();
  H264FileWriter(const H264FileWriter&) = delete;
  H264FileWriter& operator=(const H264FileWriter&) = delete;

  bool Open(const std::string& path, std::string* error = nullptr);
  bool Write(const H264Packet& packet, std::string* error = nullptr);
  bool Close(std::string* error = nullptr);
  std::uint64_t bytes_written() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cockpit::camera
