#include "cockpit/services/camera-service/photo/camera_photo_service.h"

#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "cockpit/core/utils/time.h"
#include "cockpit/modules/camera/photo/jpeg_encoder.h"
#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  if (!cockpit::camera::JpegEncoder::IsAvailable()) {
    std::cout << "GStreamer JPEG backend unavailable; photo encoding test skipped\n";
    return 0;
  }

  const std::string suffix = std::to_string(getpid());
  const std::string shared_memory_name = "/cockpit_camera_photo_test_" + suffix;
  const auto output_directory =
      std::filesystem::temp_directory_path() / ("cockpit_camera_photo_test_" + suffix);
  std::filesystem::remove_all(output_directory);

  cockpit::camera::SharedFrameBufferConfig buffer_config;
  buffer_config.name = shared_memory_name;
  buffer_config.max_frame_bytes = 4096;
  std::string error;
  auto writer = cockpit::camera::SharedFrameWriter::Create(buffer_config, &error);
  if (!Check(writer != nullptr, "create shared camera frame writer failed")) {
    std::cerr << error << '\n';
    return 1;
  }

  cockpit::camera::CameraFrame frame;
  frame.sequence = 7;
  frame.timestamp_ms = static_cast<std::uint64_t>(cockpit::utils::NowMs());
  frame.width = 16;
  frame.height = 16;
  frame.stride_bytes = frame.width * 4;
  frame.format = cockpit::camera::CameraPixelFormat::kBgrx;
  frame.data.resize(static_cast<std::size_t>(frame.stride_bytes) * frame.height);
  for (std::size_t index = 0; index < frame.data.size(); index += 4) {
    frame.data[index] = 32;
    frame.data[index + 1] = 128;
    frame.data[index + 2] = 224;
    frame.data[index + 3] = 255;
  }
  if (!Check(writer->Publish(std::move(frame)), "publish shared camera frame failed")) {
    return 1;
  }

  cockpit::camera::CameraPhotoService service(shared_memory_name, output_directory, 90, 2000);
  cockpit::camera::CameraPhotoResult result;
  if (!Check(service.TakePhoto("snapshot.jpg", &result, &error), "take photo failed")) {
    std::cerr << error << '\n';
    return 1;
  }
  std::ifstream jpeg(result.path, std::ios::binary);
  std::array<unsigned char, 2> signature{};
  jpeg.read(reinterpret_cast<char*>(signature.data()), signature.size());
  cockpit::camera::CameraPhotoResult invalid_result;
  const bool test_result =
      Check(signature[0] == 0xFF && signature[1] == 0xD8, "JPEG signature mismatch") &&
      Check(result.frame_sequence == 7, "photo frame sequence mismatch") &&
      Check(result.width == 16 && result.height == 16, "photo dimensions mismatch") &&
      Check(result.size_bytes > 2, "photo file is empty") &&
      Check(!service.TakePhoto("../escape.jpg", &invalid_result, &error),
            "unsafe photo filename was accepted");
  std::filesystem::remove_all(output_directory);
  return test_result ? 0 : 1;
}
