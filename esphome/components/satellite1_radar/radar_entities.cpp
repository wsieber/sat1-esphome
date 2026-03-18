#include "radar_entities.h"
#include "satellite1_radar.h"

namespace esphome {
namespace satellite1_radar {

void Satellite1RadarButton::press_action() {
  if (parent_ == nullptr)
    return;

  switch (button_type_) {
    case 0:
      parent_->factory_reset_radar();
      break;
    case 1:
      parent_->restart_radar();
      break;
    case 2:
      parent_->query_radar_params();
      break;
  }
}

}  // namespace satellite1_radar
}  // namespace esphome
