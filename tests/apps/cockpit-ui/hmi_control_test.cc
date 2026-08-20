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
  std::filesystem::remove_all(root);
  return result && local_navigation_succeeded && invalid_navigation_rejected ? 0 : 1;
}
