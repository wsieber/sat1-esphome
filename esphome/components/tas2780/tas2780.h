#pragma once

#include "esphome/components/audio_dac/audio_dac.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace tas2780 {

enum ChannelSelect : uint8_t { MONO_DWN_MIX, LEFT_CHANNEL, RIGHT_CHANNEL };

enum SpeakerImpedance : uint8_t { Ohm4 = 4, Ohm8 = 8 };

enum SupplyVoltage : uint8_t { V5 = 5, V9 = 9, V12 = 12, V15 = 15, V20 = 20 };

class TAS2780 : public audio_dac::AudioDac, public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void loop() override;

  void init();
  void reset();
  void activate(uint8_t power_mode = 2);
  void deactivate();
  void update_settings();
  void log_error_states();
  
  bool set_mute_off() override;
  bool set_mute_on() override;
  bool set_volume(float volume) override;

  bool is_muted() override;
  float volume() override;

  void set_vol_range_min(float min_val) { this->vol_range_min_ = min_val; }
  void set_vol_range_max(float max_val) { this->vol_range_max_ = max_val; }
  void set_selected_channel(ChannelSelect channel) { this->selected_channel_ = channel; }
  void set_speaker_specs(SpeakerImpedance impedance, uint8_t power) {
    this->speaker_impedance_ = impedance;
    this->speaker_power_ = power;
  }
  void set_power_supply(SupplyVoltage voltage, float max_current) {
    this->supply_voltage_ = voltage;
    this->supply_max_current_ = max_current;
  }

 protected:
  float calc_vcap_dBV_() const;
  
  float calc_and_write_amp_level_(float v_cap_dBV);
  void set_and_write_power_mode_();
  void set_and_write_limiter_(float v_cap_dBV);
  void update_dvc_volume_range_(float dvc_max_db);

  /**
   * Configure Supply Tracking Limiter (STL) thresholds + basic dynamics.
   *
   * @param th_max_vpk  Limiter maximum threshold in VOLTS PEAK (required).
   * @param enable      true enables limiter (LIM_EN), false disables it.
   * @param max_attn_db Limiter max attenuation in dB (1..15). (LIM_MAX_ATTN upper nibble)
   * @param attack_code LIM_ATK_RT code (0..15), see datasheet table.
   * @param hold_code   LIM_HLD_TM code (0..7), see datasheet table.
   * @param release_code LIM_RLS_RT code (0..15), see datasheet table.
   * @param pause_during_bop If true sets LIM_PDB=1 (pauses limiter during BOP).
   * @param headroom_enable If true sets LIM_HR_EN=1.
   *
   * Optional tracking parameters (Mode 2 use-case):
   * @param th_min_vpk  If >=0, programs LIM_TH_MIN (VOLTS PEAK).
   * @param inf_pt_vpk  If >=0, programs LIM_INF_PT (VOLTS PEAK).
   */
  void configure_STL_(
      float th_max_vpk,
      bool enable,
      uint8_t max_attn_db,
      uint8_t attack_code,
      uint8_t hold_code,
      uint8_t release_code,
      bool pause_during_bop = true,
      bool headroom_enable = true,
      float th_min_vpk = -1.0f,
      float inf_pt_vpk = -1.0f
  );
  
  void set_limiter_mode0_fixed_(float vpk_cap);
  void set_limiter_mode2_tracking_(float vpk_max, float vpk_min, float vpk_inflect);
  
  bool write_mute_();
  bool write_volume_();
  void write_u32_be_(uint8_t reg_msb, uint32_t v);

  
  float vol_range_max_{1.};
  float vol_range_min_{.3};  
  float volume_{0};
  float min_attenuation_db_{0};
  
  uint8_t power_mode_{2};
  SupplyVoltage supply_voltage_{SupplyVoltage::V5};
  float supply_max_current_{0.5f};
  
  uint8_t speaker_power_{15};
  SpeakerImpedance speaker_impedance_{SpeakerImpedance::Ohm4};
  
  ChannelSelect selected_channel_{MONO_DWN_MIX};
};

}  // namespace tas2780
}  // namespace esphome