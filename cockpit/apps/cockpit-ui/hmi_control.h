#pragma once

#include <QObject>
#include <memory>
#include <string>

namespace cockpit {
namespace ui {

class HmiControl final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int currentView READ currentView WRITE setCurrentView NOTIFY currentViewChanged)

 public:
  enum View {
    kDashboardView = 0,
    kCameraView = 1,
    kDiagnosticsView = 2,
    kMediaView = 3,
    kVoiceView = 4,
    kSettingsView = 5,
  };
  Q_ENUM(View)

  explicit HmiControl(QObject* parent = nullptr);
  ~HmiControl() override;

  bool Start(const std::string& socket_path);
  void Stop();
  int currentView() const;

 public slots:
  void setCurrentView(int view);

 signals:
  void currentViewChanged();

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
  int current_view_{kDashboardView};
};

}  // namespace ui
}  // namespace cockpit
