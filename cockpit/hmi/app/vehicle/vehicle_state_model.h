#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace cockpit {
namespace ui {

class VehicleStateModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
  Q_PROPERTY(bool fresh READ fresh NOTIFY freshnessChanged)
  Q_PROPERTY(qint64 timestampMs READ timestampMs NOTIFY vehicleStateChanged)
  Q_PROPERTY(double speedKph READ speedKph NOTIFY vehicleStateChanged)
  Q_PROPERTY(int gear READ gear NOTIFY vehicleStateChanged)
  Q_PROPERTY(int socPercent READ socPercent NOTIFY vehicleStateChanged)
  Q_PROPERTY(bool cloudEnabled READ cloudEnabled NOTIFY vehicleStateChanged)
  Q_PROPERTY(QString source READ source NOTIFY vehicleStateChanged)

 public:
  explicit VehicleStateModel(QObject* parent = nullptr, int stale_timeout_ms = 1500);

  bool connected() const {
    return connected_;
  }
  bool fresh() const {
    return fresh_;
  }
  qint64 timestampMs() const {
    return timestamp_ms_;
  }
  double speedKph() const {
    return speed_kph_;
  }
  int gear() const {
    return gear_;
  }
  int socPercent() const {
    return soc_percent_;
  }
  bool cloudEnabled() const {
    return cloud_enabled_;
  }
  const QString& source() const {
    return source_;
  }

  void SetConnected(bool connected);
  void Update(qint64 timestamp_ms, double speed_kph, int gear, int soc_percent, bool cloud_enabled,
              const QString& source);

 signals:
  void connectedChanged();
  void freshnessChanged();
  void vehicleStateChanged();

 private:
  void SetFresh(bool fresh);

  bool connected_ = false;
  bool fresh_ = false;
  qint64 timestamp_ms_ = 0;
  double speed_kph_ = 0.0;
  int gear_ = 0;
  int soc_percent_ = 0;
  bool cloud_enabled_ = false;
  QString source_ = QStringLiteral("waiting");
  QTimer stale_timer_;
};

}  // namespace ui
}  // namespace cockpit
