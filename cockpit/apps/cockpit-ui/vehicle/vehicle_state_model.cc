#include "vehicle_state_model.h"

namespace cockpit {
namespace ui {

VehicleStateModel::VehicleStateModel(QObject* parent, int stale_timeout_ms) : QObject(parent) {
  stale_timer_.setSingleShot(true);
  stale_timer_.setInterval(stale_timeout_ms);
  connect(&stale_timer_, &QTimer::timeout, this, [this] {
    SetFresh(false);
  });
}

void VehicleStateModel::SetConnected(bool connected) {
  if (connected_ == connected) {
    return;
  }
  connected_ = connected;
  if (!connected_) {
    stale_timer_.stop();
    SetFresh(false);
  }
  emit connectedChanged();
}

void VehicleStateModel::Update(qint64 timestamp_ms, double speed_kph, int gear, int soc_percent,
                               bool cloud_enabled, const QString& source) {
  timestamp_ms_ = timestamp_ms;
  speed_kph_ = speed_kph;
  gear_ = gear;
  soc_percent_ = soc_percent;
  cloud_enabled_ = cloud_enabled;
  source_ = source;
  SetFresh(true);
  stale_timer_.start();
  emit vehicleStateChanged();
}

void VehicleStateModel::SetFresh(bool fresh) {
  if (fresh_ == fresh) {
    return;
  }
  fresh_ = fresh;
  emit freshnessChanged();
}

}  // namespace ui
}  // namespace cockpit
