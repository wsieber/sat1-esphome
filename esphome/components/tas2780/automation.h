#pragma once

#include "esphome/core/automation.h"
#include "tas2780.h"

namespace esphome {

namespace tas2780 {

template<typename... Ts> class ResetAction : public Action<Ts...>, public Parented<TAS2780> {
 public:
  void play(const Ts &...x) override { this->parent_->reset(); }
};

// template<typename... Ts> class ActivateAction : public Action<Ts...> {
//  public:
//   ActivateAction(TAS2780 *parent) : parent_(parent) {}
//   TEMPLATABLE_VALUE(uint8_t, mode)

//   void play(const Ts &...x) override {
//     if (this->mode_.has_value()) {
//       this->parent_->activate(this->mode_.value(x...));
//     } else {
//       this->parent_->activate();
//     }
//   }

//  protected:
//   TAS2780 *parent_;
// };

template<typename... Ts> class UpdateConfigAction : public Action<Ts...> {
 public:
  UpdateConfigAction(TAS2780 *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint8_t, amp_level)
  TEMPLATABLE_VALUE(float, vol_range_min)
  TEMPLATABLE_VALUE(float, vol_range_max)
  TEMPLATABLE_VALUE(uint8_t, channel)

  void play(const Ts &...x) override {
    if (this->vol_range_min_.has_value()) {
      this->parent_->set_vol_range_min(this->vol_range_min_.value(x...));
    }
    if (this->vol_range_max_.has_value()) {
      this->parent_->set_vol_range_max(this->vol_range_max_.value(x...));
    }
    if (this->channel_.has_value()) {
      this->parent_->set_selected_channel((ChannelSelect) this->channel_.value(x...));
      this->parent_->update_settings();
    }
  }

 protected:
  TAS2780 *parent_;
};

template<typename... Ts> class UpdatePowerSupplyAction : public Action<Ts...> {
 public:
  UpdatePowerSupplyAction(TAS2780 *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint8_t, voltage)
  TEMPLATABLE_VALUE(float, max_current)

  void play(const Ts &...x) override {
    if (!this->voltage_.has_value() || !this->max_current_.has_value()) {
      return;
    }
    this->parent_->set_power_supply(
      static_cast<SupplyVoltage>(this->voltage_.value(x...)),
      this->max_current_.value(x...)
    );
    this->parent_->update_settings();
  }

 protected:
  TAS2780 *parent_;
};


// template<typename... Ts> class UpdateSpeakerAction : public Action<Ts...> {
//  public:
//   UpdateSpeakerAction(TAS2780 *parent) : parent_(parent) {}
//   TEMPLATABLE_VALUE(float, power)
//   TEMPLATABLE_VALUE(SpeakerImpedance, impedance)

//   void play(const Ts &...x) override {
//     if (!this->power_.has_value() || !this->impedance_.has_value()) {
//       return;
//     }
//     this->parent_->set_speaker_specs(
//       this->impedance_.value(x...),
//       this->power_.value(x...)
//     );
//     this->parent_->update_settings();
//   }

//  protected:
//   TAS2780 *parent_;
// };



template<typename... Ts> class DeactivateAction : public Action<Ts...>, public Parented<TAS2780> {
 public:
  void play(const Ts &...x) override { this->parent_->deactivate(); }
};

template<typename... Ts> class ActivateAction : public Action<Ts...>, public Parented<TAS2780> {
 public:
  void play(const Ts &...x) override { this->parent_->activate(); }
};

}  // namespace tas2780
}  // namespace esphome