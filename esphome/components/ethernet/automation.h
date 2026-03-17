#pragma once

#include "esphome/core/automation.h"
#include "ethernet_component.h"

namespace esphome {
namespace ethernet {

class EthernetConnectedTrigger : public Trigger<> {
 public:
  explicit EthernetConnectedTrigger(EthernetComponent *eth) {
    eth->add_on_connected_callback([this]() { this->trigger(); });
  }
};

class EthernetDisconnectedTrigger : public Trigger<> {
 public:
  explicit EthernetDisconnectedTrigger(EthernetComponent *eth) {
    eth->add_on_disconnected_callback([this]() { this->trigger(); });
  }
};

}  // namespace ethernet
}  // namespace esphome
