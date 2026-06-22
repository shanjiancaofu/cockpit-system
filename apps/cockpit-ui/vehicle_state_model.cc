#include "vehicle_state_model.h"

namespace cockpit {
namespace ui {

VehicleStateModel::VehicleStateModel(QObject* parent) : QObject(parent) {}

void VehicleStateModel::SetConnected(bool connected) {
  if (connected_ == connected) {
    return;
  }
  connected_ = connected;
  emit connectedChanged();
}

void VehicleStateModel::Update(qint64 timestamp_ms, double speed_kph, int gear,
                               int soc_percent, bool cloud_enabled,
                               const QString& source) {
  timestamp_ms_ = timestamp_ms;
  speed_kph_ = speed_kph;
  gear_ = gear;
  soc_percent_ = soc_percent;
  cloud_enabled_ = cloud_enabled;
  source_ = source;
  emit vehicleStateChanged();
}

}  // namespace ui
}  // namespace cockpit
