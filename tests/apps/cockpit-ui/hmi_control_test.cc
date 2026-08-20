#include "cockpit/apps/cockpit-ui/hmi_control.h"

#include <unistd.h>

#include <QCoreApplication>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "agent/hmi/local_hmi_command_provider.h"
#include "cockpit/apps/cockpit-ui/media/grpc_media_player_backend.h"
#include "cockpit/apps/cockpit-ui/media/media_control_model.h"
#include "cockpit/library/media/media_grpc_service.h"
#include "cockpit/library/media/media_service.h"
#include "cockpit/modules/media/media_player.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto root = std::filesystem::temp_directory_path() /
                    ("cockpit_hmi_control_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  const auto socket_path = root / "hmi-control.sock";

  cockpit::ui::HmiControl control;
  if (!control.Start(socket_path.string())) {
    std::cerr << "HMI control server did not start\n";
    return 1;
  }

  cockpit::voice::LocalHmiCommandProvider provider("unix:" + socket_path.string());
  std::atomic_bool completed{false};
  bool camera_succeeded = false;
  std::string camera_response;
  std::string camera_error;
  std::thread camera_request([&] {
    const cockpit::voice::ActionExecutionContext action_context{
        std::chrono::steady_clock::now() + std::chrono::seconds(1),
        std::make_shared<cockpit::voice::ActionCancellation>()};
    camera_succeeded = provider.SendCommand(cockpit::voice::HmiCommand::kOpenCameraPreview,
                                            action_context, &camera_response, &camera_error);
    completed.store(true);
  });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!completed.load() && std::chrono::steady_clock::now() < deadline) {
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  camera_request.join();

  std::string music_response;
  std::string music_error;
  std::atomic_bool music_completed{false};
  bool music_succeeded = false;
  std::thread music_request([&] {
    const cockpit::voice::ActionExecutionContext music_context{
        std::chrono::steady_clock::now() + std::chrono::seconds(1),
        std::make_shared<cockpit::voice::ActionCancellation>()};
    music_succeeded = provider.SendCommand(cockpit::voice::HmiCommand::kPlayMusic, music_context,
                                           &music_response, &music_error);
    music_completed.store(true);
  });
  const auto music_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!music_completed.load() && std::chrono::steady_clock::now() < music_deadline) {
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  music_request.join();
  const bool result =
      camera_succeeded && camera_error.empty() && camera_response == "Camera view opened." &&
      control.currentView() == cockpit::ui::HmiControl::kCameraView && !music_succeeded &&
      music_error == "Media player is unavailable or cannot start playback.";
  control.setCurrentView(cockpit::ui::HmiControl::kSettingsView);
  const bool local_navigation_succeeded =
      control.currentView() == cockpit::ui::HmiControl::kSettingsView;
  control.setCurrentView(cockpit::ui::HmiControl::kSettingsView + 1);
  const bool invalid_navigation_rejected =
      control.currentView() == cockpit::ui::HmiControl::kSettingsView;
  if (!result || !local_navigation_succeeded || !invalid_navigation_rejected) {
    std::cerr << "HMI command result mismatch camera_error=" << camera_error
              << " music_error=" << music_error << '\n';
  }

  control.Stop();

  const auto media_socket_path = root / "media-control.sock";
  cockpit::media::MediaService media_service(cockpit::media::CreateMockMediaPlayer());
  cockpit::media::MediaGrpcService media_grpc(media_service);
  const std::string media_address = "unix:" + media_socket_path.string();
  if (!media_grpc.Start(media_address)) {
    std::cerr << "mock media service did not start\n";
    std::filesystem::remove_all(root);
    return 1;
  }
  cockpit::ui::MediaControlModel media_control(
      cockpit::ui::CreateGrpcMediaPlayerBackend(media_address));
  media_control.Start();
  const auto media_ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!media_control.available() && std::chrono::steady_clock::now() < media_ready_deadline) {
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto connected_hmi_socket = root / "connected-hmi-control.sock";
  cockpit::ui::HmiControl connected_control(&media_control);
  if (!connected_control.Start(connected_hmi_socket.string())) {
    std::cerr << "connected HMI control server did not start\n";
    media_control.Stop();
    media_grpc.Shutdown();
    std::filesystem::remove_all(root);
    return 1;
  }
  cockpit::voice::LocalHmiCommandProvider connected_provider("unix:" +
                                                             connected_hmi_socket.string());
  std::atomic_bool connected_completed{false};
  bool connected_music_succeeded = false;
  std::string connected_music_response;
  std::string connected_music_error;
  std::thread connected_music_request([&] {
    const cockpit::voice::ActionExecutionContext context{
        std::chrono::steady_clock::now() + std::chrono::seconds(2),
        std::make_shared<cockpit::voice::ActionCancellation>()};
    connected_music_succeeded =
        connected_provider.SendCommand(cockpit::voice::HmiCommand::kPlayMusic, context,
                                       &connected_music_response, &connected_music_error);
    connected_completed.store(true);
  });
  const auto connected_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!connected_completed.load() && std::chrono::steady_clock::now() < connected_deadline) {
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  connected_music_request.join();
  const auto playing_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (media_control.state() != QStringLiteral("PLAYING") &&
         std::chrono::steady_clock::now() < playing_deadline) {
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const bool connected_result = connected_music_succeeded && connected_music_error.empty() &&
                                connected_music_response == "Music playback requested." &&
                                media_control.state() == QStringLiteral("PLAYING");
  connected_control.Stop();
  media_control.Stop();
  media_grpc.Shutdown();
  std::filesystem::remove_all(root);
  return result && local_navigation_succeeded && invalid_navigation_rejected && connected_result
             ? 0
             : 1;
}
