#include "cockpit/apps/cockpit-ui/hmi_control.h"

#include <grpcpp/grpcpp.h>

#include <QMetaObject>
#include <filesystem>
#include <string>
#include <utility>

#include "cockpit/core/logging/logger.h"
#include "hmi.grpc.pb.h"

namespace cockpit {
namespace ui {

class HmiControl::Impl final : public proto::hmi::HmiControl::Service {
 public:
  Impl(HmiControl* owner, std::string socket_path)
      : owner_(owner), socket_path_(std::move(socket_path)) {
  }

  ~Impl() override {
    Stop();
  }

  bool Start() {
    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + socket_path_, grpc::InsecureServerCredentials());
    builder.RegisterService(this);
    server_ = builder.BuildAndStart();
    return server_ != nullptr;
  }

  void Stop() {
    if (server_ != nullptr) {
      server_->Shutdown();
      server_.reset();
    }
    std::error_code error;
    std::filesystem::remove(socket_path_, error);
  }

 private:
  grpc::Status Execute(grpc::ServerContext*, const proto::hmi::ExecuteHmiCommandRequest* request,
                       proto::hmi::ExecuteHmiCommandResponse* response) override {
    switch (request->command()) {
      case proto::hmi::HMI_COMMAND_OPEN_CAMERA_PREVIEW: {
        bool camera_selected = false;
        const bool invoked = QMetaObject::invokeMethod(
            owner_,
            [this, &camera_selected] {
              owner_->setCurrentView(HmiControl::kCameraView);
              camera_selected = owner_->currentView() == HmiControl::kCameraView;
            },
            Qt::BlockingQueuedConnection);
        if (!invoked || !camera_selected) {
          return grpc::Status(grpc::StatusCode::INTERNAL, "failed to select the camera view");
        }
        response->set_executed(true);
        response->set_message("Camera view opened.");
        return grpc::Status::OK;
      }
      case proto::hmi::HMI_COMMAND_PLAY_MUSIC:
        response->set_executed(false);
        response->set_message("Media player is not connected.");
        return grpc::Status::OK;
      case proto::hmi::HMI_COMMAND_UNSPECIFIED:
      default:
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "HMI command is invalid");
    }
  }

  HmiControl* owner_;
  const std::string socket_path_;
  std::unique_ptr<grpc::Server> server_;
};

HmiControl::HmiControl(QObject* parent) : QObject(parent) {
}

HmiControl::~HmiControl() {
  Stop();
}

bool HmiControl::Start(const std::string& socket_path) {
  if (impl_ != nullptr || socket_path.empty()) {
    return false;
  }
  const std::filesystem::path absolute_path = std::filesystem::absolute(socket_path);
  std::error_code error;
  std::filesystem::create_directories(absolute_path.parent_path(), error);
  if (error) {
    LOG_ERROR("failed to create HMI runtime directory: " + error.message());
    return false;
  }
  std::filesystem::remove(absolute_path, error);
  if (error) {
    LOG_ERROR("failed to remove stale HMI control socket: " + error.message());
    return false;
  }

  auto impl = std::make_unique<Impl>(this, absolute_path.string());
  if (!impl->Start()) {
    LOG_ERROR("failed to start HMI control server socket=" + absolute_path.string());
    return false;
  }
  impl_ = std::move(impl);
  LOG_INFO("HMI control server listening socket=" + absolute_path.string());
  return true;
}

void HmiControl::Stop() {
  impl_.reset();
}

int HmiControl::currentView() const {
  return current_view_;
}

void HmiControl::setCurrentView(int view) {
  if (view < kDashboardView || view > kDiagnosticsView || current_view_ == view) {
    return;
  }
  current_view_ = view;
  emit currentViewChanged();
}

}  // namespace ui
}  // namespace cockpit
