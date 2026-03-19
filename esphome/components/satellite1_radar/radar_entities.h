#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "esphome/components/button/button.h"

namespace esphome {
namespace satellite1_radar {

class Satellite1Radar;

class Satellite1RadarNumber : public number::Number, public Component {
 public:
  void setup() override {
    float val;
    this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
    if (this->pref_.load(&val)) {
      this->publish_state(val);
    }
  }
  void dump_config() override {}

 protected:
  void control(float value) override {
    this->publish_state(value);
    this->pref_.save(&value);
  }
  ESPPreferenceObject pref_;
};

class Satellite1RadarSwitch : public switch_::Switch, public Component {
 public:
  void setup() override {}
  void dump_config() override {}

 protected:
  void write_state(bool state) override { this->publish_state(state); }
};

class Satellite1RadarSelect : public select::Select, public Component {
 public:
  void setup() override {}
  void dump_config() override {}

 protected:
  void control(const std::string &value) override { this->publish_state(value); }
};

class Satellite1RadarTunerSwitch : public switch_::Switch, public Component {
 public:
  void setup() override {}
  void dump_config() override {}
  void set_parent(Satellite1Radar *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override;
  Satellite1Radar *parent_{nullptr};
};

class Satellite1RadarButton : public button::Button, public Component {
 public:
  void setup() override {}
  void dump_config() override {}

  void set_parent(Satellite1Radar *parent) { parent_ = parent; }
  void set_button_type(uint8_t type) { button_type_ = type; }

 protected:
  void press_action() override;

  Satellite1Radar *parent_{nullptr};
  uint8_t button_type_{0};  // 0=factory_reset, 1=restart, 2=query_params
};

}  // namespace satellite1_radar
}  // namespace esphome
