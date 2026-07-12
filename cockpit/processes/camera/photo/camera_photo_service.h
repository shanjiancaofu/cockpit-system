#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace cockpit {
namespace camera {

struct CameraPhotoResult {
  std::string path;
  std::uint64_t frame_sequence = 0;
  std::uint64_t frame_timestamp_ms = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t size_bytes = 0;
};

class CameraPhotoService {
 public:
  CameraPhotoService(std::string shared_memory_name, std::filesystem::path output_directory,
                     int jpeg_quality, int max_frame_age_ms);

  bool TakePhoto(const std::string& filename, CameraPhotoResult* result, std::string* error) const;

 private:
  static bool IsSafeFilename(const std::string& filename);

  const std::string shared_memory_name_;
  const std::filesystem::path output_directory_;
  const int jpeg_quality_;
  const int max_frame_age_ms_;
};

}  // namespace camera
}  // namespace cockpit
