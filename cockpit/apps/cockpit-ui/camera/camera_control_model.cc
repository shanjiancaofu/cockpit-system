#include "camera_control_model.h"

#include <grpcpp/grpcpp.h>

#include <QMetaObject>
#include <chrono>
#include <utility>

#include "camera.grpc.pb.h"
#include "common.pb.h"

namespace cockpit {
namespace ui {
namespace {

void SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

QString RpcError(const grpc::Status& status) {
  return status.ok() ? QString() : QString::fromStdString(status.error_message());
}

}  // namespace

CameraControlModel::CameraControlModel(std::string address, QObject* parent)
    : QObject(parent), address_(std::move(address)) {
}

CameraControlModel::~CameraControlModel() {
  Stop();
}

void CameraControlModel::Start() {
  bool expected = false;
  if (!worker_running_.compare_exchange_strong(expected, true)) {
    return;
  }
  worker_ = std::thread(&CameraControlModel::Run, this);
  refreshDevices();
}

void CameraControlModel::Stop() {
  worker_running_.store(false);
  condition_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void CameraControlModel::refreshDevices() {
  Enqueue(Command{});
}

void CameraControlModel::startPreview(const QString& device, int width, int height, int fps) {
  if (device.isEmpty() || width <= 0 || height <= 0 || fps <= 0) {
    PostStatus(running_, active_device_, QStringLiteral("Invalid camera preview settings"));
    return;
  }
  Command command;
  command.type = CommandType::kStartPreview;
  command.device = device.toStdString();
  command.width = static_cast<std::uint32_t>(width);
  command.height = static_cast<std::uint32_t>(height);
  command.fps = static_cast<std::uint32_t>(fps);
  Enqueue(std::move(command));
}

void CameraControlModel::stopPreview() {
  Command command;
  command.type = CommandType::kStopPreview;
  Enqueue(std::move(command));
}

void CameraControlModel::takePhoto() {
  Command command;
  command.type = CommandType::kTakePhoto;
  Enqueue(std::move(command));
}

void CameraControlModel::Enqueue(Command command) {
  if (!worker_running_.load()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push_back(std::move(command));
  }
  if (!busy_) {
    busy_ = true;
    emit statusChanged();
  }
  condition_.notify_one();
}

void CameraControlModel::Run() {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel(address_, grpc::InsecureChannelCredentials(), arguments);
  auto stub = proto::camera::CameraControl::NewStub(channel);

  while (worker_running_.load()) {
    Command command;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [this] {
        return !worker_running_.load() || !commands_.empty();
      });
      if (!worker_running_.load()) {
        break;
      }
      command = std::move(commands_.front());
      commands_.pop_front();
    }

    if (command.type == CommandType::kRefreshDevices) {
      proto::common::Empty request;
      proto::camera::ListCameraDevicesResponse response;
      grpc::ClientContext context;
      SetDeadline(&context);
      const grpc::Status status = stub->ListDevices(&context, request, &response);
      QStringList devices;
      if (status.ok()) {
        for (const auto& device : response.devices()) {
          if (device.query_ok() && device.supports_capture() && device.supports_streaming()) {
            devices.push_back(QString::fromStdString(device.path()));
          }
        }
      }
      PostDevices(std::move(devices), RpcError(status));

      proto::common::Empty status_request;
      proto::camera::CameraStatus camera_status;
      grpc::ClientContext status_context;
      SetDeadline(&status_context);
      const grpc::Status status_result =
          stub->GetStatus(&status_context, status_request, &camera_status);
      PostStatus(camera_status.state() == proto::camera::CAMERA_PREVIEW_STATE_RUNNING,
                 QString::fromStdString(camera_status.device()), RpcError(status_result));
      continue;
    }

    if (command.type == CommandType::kTakePhoto) {
      proto::camera::TakePhotoRequest request;
      proto::camera::TakePhotoResponse response;
      grpc::ClientContext context;
      SetDeadline(&context);
      const grpc::Status status = stub->TakePhoto(&context, request, &response);
      PostPhoto(QString::fromStdString(response.path()), RpcError(status));
      continue;
    }

    proto::camera::CameraStatus response;
    grpc::ClientContext context;
    SetDeadline(&context);
    grpc::Status status;
    if (command.type == CommandType::kStartPreview) {
      proto::camera::StartPreviewRequest request;
      request.set_device(command.device);
      request.set_width(command.width);
      request.set_height(command.height);
      request.set_fps(command.fps);
      status = stub->StartPreview(&context, request, &response);
    } else {
      proto::common::Empty request;
      status = stub->StopPreview(&context, request, &response);
    }
    const bool running = response.state() == proto::camera::CAMERA_PREVIEW_STATE_RUNNING;
    PostStatus(running, QString::fromStdString(response.device()), RpcError(status));
  }
}

void CameraControlModel::PostPhoto(QString path, QString error) {
  QMetaObject::invokeMethod(
      this,
      [this, path = std::move(path), error = std::move(error)]() mutable {
        last_photo_path_ = std::move(path);
        last_error_ = std::move(error);
        busy_ = false;
        emit statusChanged();
      },
      Qt::QueuedConnection);
}

void CameraControlModel::PostDevices(QStringList devices, QString error) {
  QMetaObject::invokeMethod(
      this,
      [this, devices = std::move(devices), error = std::move(error)]() mutable {
        devices_ = std::move(devices);
        last_error_ = std::move(error);
        busy_ = false;
        emit devicesChanged();
        emit statusChanged();
      },
      Qt::QueuedConnection);
}

void CameraControlModel::PostStatus(bool running, QString active_device, QString error) {
  QMetaObject::invokeMethod(
      this,
      [this, running, active_device = std::move(active_device),
       error = std::move(error)]() mutable {
        running_ = running;
        active_device_ = std::move(active_device);
        last_error_ = std::move(error);
        busy_ = false;
        emit statusChanged();
      },
      Qt::QueuedConnection);
}

}  // namespace ui
}  // namespace cockpit
