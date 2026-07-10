#include "camera_frame_client.h"

#include <QImage>
#include <QMetaObject>
#include <chrono>
#include <limits>
#include <utility>

#include "camera_frame_model.h"

#include "cockpit/modules/camera/frames/camera_frame.h"
#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"

namespace cockpit {
namespace ui {

CameraFrameClient::CameraFrameClient(std::string shared_memory_name, CameraFrameModel* model)
    : shared_memory_name_(std::move(shared_memory_name)), model_(model) {
}

CameraFrameClient::~CameraFrameClient() {
  Stop();
}

void CameraFrameClient::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }
  worker_ = std::thread(&CameraFrameClient::Run, this);
}

void CameraFrameClient::Stop() {
  running_.store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void CameraFrameClient::Run() {
  while (running_.load()) {
    std::string error;
    auto reader = camera::SharedFrameReader::Open(shared_memory_name_, &error);
    if (reader == nullptr) {
      PostConnected(false);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    PostConnected(true);
    std::uint64_t last_generation = 0;
    while (running_.load()) {
      if (!reader->IsAvailable()) {
        PostConnected(false);
        break;
      }
      camera::CameraFrame frame;
      std::uint64_t generation = 0;
      if (reader->ReadLatest(&frame, &generation, &error) && generation != last_generation) {
        if (PostFrame(frame, generation)) {
          last_generation = generation;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  PostConnected(false);
}

void CameraFrameClient::PostConnected(bool connected) {
  QMetaObject::invokeMethod(
      model_,
      [model = model_, connected] {
        model->SetConnected(connected);
      },
      Qt::QueuedConnection);
}

bool CameraFrameClient::PostFrame(const camera::CameraFrame& frame, std::uint64_t generation) {
  if (!frame.IsValid() ||
      frame.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      frame.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      frame.stride_bytes > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  QImage image;
  if (frame.format == camera::CameraPixelFormat::kBgrx) {
    image = QImage(frame.data.data(), static_cast<int>(frame.width), static_cast<int>(frame.height),
                   static_cast<int>(frame.stride_bytes), QImage::Format_RGB32)
                .copy();
  } else if (frame.format == camera::CameraPixelFormat::kRgb) {
    image = QImage(frame.data.data(), static_cast<int>(frame.width), static_cast<int>(frame.height),
                   static_cast<int>(frame.stride_bytes), QImage::Format_RGB888)
                .copy();
  } else {
    return false;
  }
  if (image.isNull()) {
    return false;
  }

  const quint64 sequence = frame.sequence;
  QMetaObject::invokeMethod(
      model_,
      [model = model_, image = std::move(image), sequence, generation]() mutable {
        model->UpdateFrame(std::move(image), sequence, generation);
      },
      Qt::QueuedConnection);
  return true;
}

}  // namespace ui
}  // namespace cockpit
