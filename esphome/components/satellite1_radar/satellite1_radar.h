#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "esphome/components/button/button.h"

#include "radar_entities.h"
#include "ld2410_handler.h"
#include "ld2450_handler.h"
#ifdef USE_ESP_IDF
#include "radar_tuner_server.h"
#endif

namespace esphome {
namespace satellite1_radar {

enum class RadarType : uint8_t {
  UNKNOWN = 0,
  NONE,
  LD2410,
  LD2450,
};

static const uint32_t DETECT_TIMEOUT_MS = 3000;

class Satellite1Radar : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  RadarType get_detected_type() const { return detected_type_; }
  bool is_detection_complete() const { return detection_complete_; }

  // --- Common entity setters (shared by both sensor types) ---
  void set_presence_binary_sensor(binary_sensor::BinarySensor *s) { presence_binary_sensor_ = s; }
  void set_moving_target_binary_sensor(binary_sensor::BinarySensor *s) { moving_target_binary_sensor_ = s; }
  void set_still_target_binary_sensor(binary_sensor::BinarySensor *s) { still_target_binary_sensor_ = s; }
  void set_radar_type_text_sensor(text_sensor::TextSensor *s) { radar_type_text_sensor_ = s; }
  void set_radar_firmware_text_sensor(text_sensor::TextSensor *s) {
    ld2410_.version_text_sensor = s;
    ld2450_.version_text_sensor = s;
  }
  // --- LD2410 entity setters ---
  void set_ld2410_moving_distance_sensor(sensor::Sensor *s) { ld2410_.moving_distance = s; }
  void set_ld2410_still_distance_sensor(sensor::Sensor *s) { ld2410_.still_distance = s; }
  void set_ld2410_moving_energy_sensor(sensor::Sensor *s) { ld2410_.moving_energy = s; }
  void set_ld2410_still_energy_sensor(sensor::Sensor *s) { ld2410_.still_energy = s; }
  void set_ld2410_detection_distance_sensor(sensor::Sensor *s) { ld2410_.detection_distance = s; }
  void set_ld2410_light_sensor(sensor::Sensor *s) { ld2410_.light_sensor = s; }

  void set_ld2410_gate_move_energy_sensor(int gate, sensor::Sensor *s) {
    if (gate < LD2410Handler::NUM_GATES) ld2410_.gate_move_energy[gate] = s;
  }
  void set_ld2410_gate_still_energy_sensor(int gate, sensor::Sensor *s) {
    if (gate < LD2410Handler::NUM_GATES) ld2410_.gate_still_energy[gate] = s;
  }

  void set_ld2410_timeout_number(number::Number *n) { ld2410_.timeout_number = n; }
  void set_ld2410_max_move_distance_gate_number(number::Number *n) { ld2410_.max_move_distance_gate = n; }
  void set_ld2410_max_still_distance_gate_number(number::Number *n) { ld2410_.max_still_distance_gate = n; }
  void set_ld2410_light_threshold_number(number::Number *n) { ld2410_.light_threshold = n; }

  void set_ld2410_gate_move_threshold_number(int gate, number::Number *n) {
    if (gate < LD2410Handler::NUM_GATES) ld2410_.gate_move_threshold[gate] = n;
  }
  void set_ld2410_gate_still_threshold_number(int gate, number::Number *n) {
    if (gate < LD2410Handler::NUM_GATES) ld2410_.gate_still_threshold[gate] = n;
  }

  void set_ld2410_engineering_mode_switch(switch_::Switch *s) { ld2410_.engineering_mode_switch = s; }
  void set_ld2410_bluetooth_switch(switch_::Switch *s) { ld2410_.bluetooth_switch = s; }

  void set_ld2410_distance_resolution_select(select::Select *s) { ld2410_.distance_resolution_select = s; }
  void set_ld2410_light_function_select(select::Select *s) { ld2410_.light_function_select = s; }

  // --- LD2450 entity setters ---
  void set_ld2450_target_count_sensor(sensor::Sensor *s) { ld2450_.target_count = s; }
  void set_ld2450_still_target_count_sensor(sensor::Sensor *s) { ld2450_.still_target_count = s; }
  void set_ld2450_moving_target_count_sensor(sensor::Sensor *s) { ld2450_.moving_target_count = s; }

  void set_ld2450_target_x_sensor(int target, sensor::Sensor *s) {
    if (target < LD2450Handler::NUM_TARGETS) ld2450_.target_x[target] = s;
  }
  void set_ld2450_target_y_sensor(int target, sensor::Sensor *s) {
    if (target < LD2450Handler::NUM_TARGETS) ld2450_.target_y[target] = s;
  }
  void set_ld2450_target_speed_sensor(int target, sensor::Sensor *s) {
    if (target < LD2450Handler::NUM_TARGETS) ld2450_.target_speed[target] = s;
  }
  void set_ld2450_target_angle_sensor(int target, sensor::Sensor *s) {
    if (target < LD2450Handler::NUM_TARGETS) ld2450_.target_angle[target] = s;
  }
  void set_ld2450_target_distance_sensor(int target, sensor::Sensor *s) {
    if (target < LD2450Handler::NUM_TARGETS) ld2450_.target_distance[target] = s;
  }
  void set_ld2450_target_resolution_sensor(int target, sensor::Sensor *s) {
    if (target < LD2450Handler::NUM_TARGETS) ld2450_.target_resolution[target] = s;
  }

  void set_ld2450_zone_state_text_sensor(int zone, text_sensor::TextSensor *s) {
    if (zone < LD2450Handler::NUM_ZONES) ld2450_.zone_state[zone] = s;
  }

  void set_ld2450_stability_number(number::Number *n) { ld2450_.stability_number = n; }
  void set_ld2450_timeout_number(number::Number *n) { ld2450_.timeout_number = n; }
  void set_ld2450_zone_point_coord_number(int zone, int point, int axis, number::Number *n) {
    if (zone < LD2450Handler::NUM_ZONES && point < LD2450Handler::MAX_ZONE_POINTS && axis < 2)
      ld2450_.zone_point_coords[zone][point][axis] = n;
  }
  void set_ld2450_zone_points_count_number(int zone, number::Number *n) {
    if (zone < LD2450Handler::NUM_ZONES) ld2450_.zone_points_count[zone] = n;
  }
  void set_ld2450_excl_zone_point_coord_number(int point, int axis, number::Number *n) {
    if (point < LD2450Handler::MAX_ZONE_POINTS && axis < 2)
      ld2450_.excl_zone_point_coords[point][axis] = n;
  }
  void set_ld2450_excl_zone_points_count_number(number::Number *n) { ld2450_.excl_zone_points_count = n; }
  void set_ld2450_detection_range_number(number::Number *n) { ld2450_.detection_range = n; }

  void set_ld2450_bluetooth_switch(switch_::Switch *s) { ld2450_.bluetooth_switch = s; }
  void set_ld2450_multi_target_switch(switch_::Switch *s) { ld2450_.multi_target_switch = s; }
  void set_radar_tuner_switch(switch_::Switch *s) { tuner_switch_ = s; }

  void set_ld2450_baud_rate_select(select::Select *s) { ld2450_.baud_rate_select = s; }

  void set_ld2410_version_text_sensor(text_sensor::TextSensor *s) { ld2410_.version_text_sensor = s; }
  void set_ld2450_version_text_sensor(text_sensor::TextSensor *s) { ld2450_.version_text_sensor = s; }
  void set_ld2450_mac_text_sensor(text_sensor::TextSensor *s) { ld2450_.mac_text_sensor = s; }
  void set_ld2450_target_direction_text_sensor(int target, text_sensor::TextSensor *s) {
    if (target < LD2450Handler::NUM_TARGETS) ld2450_.target_direction[target] = s;
  }

  // --- Button entity setters ---
  void set_factory_reset_button(button::Button *b) { factory_reset_button_ = b; }
  void set_restart_button(button::Button *b) { restart_button_ = b; }
  void set_query_params_button(button::Button *b) { query_params_button_ = b; }

  // --- Button action methods ---
  void factory_reset_radar();
  void restart_radar();
  void query_radar_params();

  // --- Radar tuner server ---
#ifdef USE_ESP_IDF
  RadarTunerServer &get_tuner_server() { return tuner_server_; }
  void start_tuner();
  void stop_tuner();
#endif

 protected:
  void finalize_detection_(RadarType type);
  void apply_entity_visibility_();

  RadarType detected_type_{RadarType::UNKNOWN};
  bool detection_complete_{false};
  uint32_t detect_start_ms_{0};
  uint8_t ring_buf_[4]{};
  int ring_pos_{0};

  // Common entities
  binary_sensor::BinarySensor *presence_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *moving_target_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *still_target_binary_sensor_{nullptr};
  text_sensor::TextSensor *radar_type_text_sensor_{nullptr};
  // Button entities
  button::Button *factory_reset_button_{nullptr};
  button::Button *restart_button_{nullptr};
  button::Button *query_params_button_{nullptr};

  // Tuner switch (shared, dispatches based on detected radar type)
  switch_::Switch *tuner_switch_{nullptr};

  // Protocol handlers
  LD2410Handler ld2410_{};
  LD2450Handler ld2450_{};

#ifdef USE_ESP_IDF
  RadarTunerServer tuner_server_{};
  volatile bool write_config_pending_{false};
#endif
};

}  // namespace satellite1_radar
}  // namespace esphome
