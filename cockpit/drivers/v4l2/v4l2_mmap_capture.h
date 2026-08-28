#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cockpit::camera {

struct V4l2MmapConfig {
  std::string device = "/dev/video0";
  std::uint32_t width = 1920;
  std::uint32_t height = 1080;
  std::uint32_t fps = 30;
  std::uint32_t buffer_count = 4;
  bool set_tegra_bypass_mode = true;
};

struct V4l2RawFrame {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t bytes_per_line = 0;
  std::uint32_t bytes_used = 0;
  std::uint32_t sequence = 0;
  std::int64_t timestamp_ns = 0;
  std::uint32_t timestamp_flags = 0;
  std::vector<std::uint8_t> data;
};

class V4l2MmapCapture final {
 public:
  V4l2MmapCapture() = default;
  ~V4l2MmapCapture();
  V4l2MmapCapture(const V4l2MmapCapture&) = delete;
  V4l2MmapCapture& operator=(const V4l2MmapCapture&) = delete;

  bool Start(const V4l2MmapConfig& config, std::string* error);
  bool WaitFrame(V4l2RawFrame* frame, int timeout_ms, std::string* error);
  void Stop();
  bool running() const;
  const V4l2MmapConfig& config() const {
    return config_;
  }
  std::uint32_t width() const {
    return width_;
  }
  std::uint32_t height() const {
    return height_;
  }
  std::uint32_t bytes_per_line() const {
    return bytes_per_line_;
  }
  std::uint32_t size_image() const {
    return size_image_;
  }
  std::string driver() const {
    return driver_;
  }
  std::string card() const {
    return card_;
  }

 private:
  struct Buffer {
    void* start = nullptr;
    std::size_t length = 0;
  };

  int fd_ = -1;
  V4l2MmapConfig config_;
  std::vector<Buffer> buffers_;
  std::string driver_;
  std::string card_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::uint32_t bytes_per_line_ = 0;
  std::uint32_t size_image_ = 0;
  bool streaming_ = false;
};

}  // namespace cockpit::camera
