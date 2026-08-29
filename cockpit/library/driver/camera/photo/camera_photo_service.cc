#include "cockpit/library/driver/camera/photo/camera_photo_service.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <utility>

#include "cockpit/core/time/time.h"
#include "cockpit/modules/camera/photo/jpeg_encoder.h"
#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"

namespace cockpit {
namespace camera {
namespace {

std::atomic<std::uint64_t> g_photo_sequence{0};

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

CameraPhotoService::CameraPhotoService(std::string shared_memory_name,
                                       std::filesystem::path output_directory, int jpeg_quality,
                                       int max_frame_age_ms)
    : shared_memory_name_(std::move(shared_memory_name)),
      output_directory_(std::move(output_directory)),
      jpeg_quality_(jpeg_quality),
      max_frame_age_ms_(max_frame_age_ms) {
}

bool CameraPhotoService::TakePhoto(const std::string& filename, CameraPhotoResult* result,
                                   std::string* error) const {
  if (result == nullptr) {
    AssignError(error, "camera photo result must not be null");
    return false;
  }
  const std::string resolved_filename =
      filename.empty() ? "photo_" + std::to_string(time::WallTime::Now().ToMilliseconds()) + "_" +
                             std::to_string(getpid()) + "_" +
                             std::to_string(g_photo_sequence.fetch_add(1U)) + ".jpg"
                       : filename;
  if (!IsSafeFilename(resolved_filename)) {
    AssignError(error,
                "photo filename must contain only letters, digits, '-', '_' and end in .jpg");
    return false;
  }
  if (!JpegEncoder::IsAvailable()) {
    AssignError(error, "GStreamer JPEG backend is not available");
    return false;
  }

  auto reader = SharedFrameReader::Open(shared_memory_name_, error);
  if (reader == nullptr) {
    return false;
  }
  CameraFrame frame;
  if (!reader->ReadLatest(&frame, nullptr, error)) {
    return false;
  }
  const std::int64_t now_ms = time::WallTime::Now().ToMilliseconds();
  if (frame.timestamp_ms == 0 || now_ms < static_cast<std::int64_t>(frame.timestamp_ms) ||
      now_ms - static_cast<std::int64_t>(frame.timestamp_ms) > max_frame_age_ms_) {
    AssignError(error, "latest camera frame is stale");
    return false;
  }

  const std::filesystem::path output_path = output_directory_ / resolved_filename;
  if (!JpegEncoder::Encode(frame, output_path, jpeg_quality_, error)) {
    return false;
  }
  try {
    result->path = output_path.string();
    result->frame_sequence = frame.sequence;
    result->frame_timestamp_ms = frame.timestamp_ms;
    result->width = frame.width;
    result->height = frame.height;
    result->size_bytes = std::filesystem::file_size(output_path);
    return true;
  } catch (const std::exception& exception) {
    AssignError(error, exception.what());
    return false;
  }
}

bool CameraPhotoService::IsSafeFilename(const std::string& filename) {
  if (filename.size() < 5 || filename.size() > 128 ||
      filename.substr(filename.size() - 4) != ".jpg") {
    return false;
  }
  return std::all_of(filename.begin(), filename.end() - 4, [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' || character == '_';
  });
}

}  // namespace camera
}  // namespace cockpit
