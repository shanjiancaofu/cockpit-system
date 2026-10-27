#include "cockpit/driver/camera/encode/h264_encoder.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

int main() {
  using namespace cockpit::camera;
  if (!H264Encoder::IsSupported()) return 0;
  H264Encoder encoder;
  H264EncoderConfig config;
  config.width = 128;
  config.height = 72;
  config.bitrate = 200000;
  std::string error;
  assert(encoder.Start(config, &error));
  std::vector<H264Packet> packets;
  for (std::uint32_t frame_id = 0; frame_id < 30; ++frame_id) {
    CpuVideoFrame frame;
    frame.format = VicOutputFormat::kI420;
    frame.width = config.width;
    frame.height = config.height;
    frame.capture_id = frame_id;
    frame.planes = {{0, 128, 128, 72}, {128 * 72, 64, 64, 36}, {128 * 72 + 64 * 36, 64, 64, 36}};
    frame.data.resize(128 * 72 + 64 * 36 * 2, 0);
    for (std::uint32_t row = 0; row < frame.height; ++row)
      for (std::uint32_t col = 0; col < frame.width; ++col)
        frame.data[row * 128 + col] = static_cast<std::uint8_t>((row + col + frame_id) & 0xff);
    std::fill(frame.data.begin() + 128 * 72, frame.data.end(), 128);
    assert(encoder.Encode(frame, &packets, &error));
  }
  assert(encoder.Flush(&packets, &error));
  encoder.Stop();
  assert(!packets.empty());
  bool keyframe = false;
  std::size_t bytes = 0;
  for (const auto& packet : packets) {
    keyframe = keyframe || packet.keyframe;
    bytes += packet.data.size();
  }
  assert(keyframe && bytes > 0);

  const auto path = std::filesystem::temp_directory_path() / "cockpit-h264-encoder-test.h264";
  H264FileWriter writer;
  assert(writer.Open(path.string(), &error));
  for (const auto& packet : packets) assert(writer.Write(packet, &error));
  assert(writer.Close(&error) && writer.bytes_written() == bytes);
  assert(std::filesystem::file_size(path) == bytes);
  std::filesystem::remove(path);
  return 0;
}
