#pragma once

#include "esphome/core/automation.h"
#include "ethernet_component.h"

namespace esphome {
namespace ethernet {


template<typename... Ts> class EnableAction : public Action<Ts...>, public Parented<EthernetComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->enable(); }
};

template<typename... Ts> class DisableAction : public Action<Ts...>, public Parented<EthernetComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->disable(); }
};

}  // namespace ethernet
}  // namespace esphome
