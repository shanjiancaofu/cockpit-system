#include "cockpit/apps/cockpit-ui/hmi_control.h"

#include <unistd.h>

#include <QCoreApplication>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
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
    camera_succeeded = provider.SendCommand(cockpit::voice::HmiCommand::kOpenCameraPreview,
                                            &camera_response, &camera_error);
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
  const bool music_succeeded =
      provider.SendCommand(cockpit::voice::HmiCommand::kPlayMusic, &music_response, &music_error);
  const bool result = camera_succeeded && camera_error.empty() &&
                      camera_response == "Camera view opened." &&
                      control.currentView() == cockpit::ui::HmiControl::kCameraView &&
                      !music_succeeded && music_error == "Media player is not connected.";
  if (!result) {
    std::cerr << "HMI command result mismatch camera_error=" << camera_error
              << " music_error=" << music_error << '\n';
  }

  control.Stop();
  std::filesystem::remove_all(root);
  return result ? 0 : 1;
}
