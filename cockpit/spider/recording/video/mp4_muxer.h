#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "cockpit/spider/recording/video/h264_gop_ring.h"

namespace cockpit::recording {

class Mp4Muxer final {
 public:
  Mp4Muxer();
  ~Mp4Muxer();
  Mp4Muxer(const Mp4Muxer&) = delete;
  Mp4Muxer& operator=(const Mp4Muxer&) = delete;

  bool Open(const std::string& path, std::uint32_t fps, std::string* error = nullptr);
  bool Write(const EncodedVideoPacket& packet, std::string* error = nullptr);
  bool Close(std::string* error = nullptr);
  bool open() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cockpit::recording
