#include "satellite1_radar.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace satellite1_radar {

static const char *const TAG = "satellite1_radar";

static const uint8_t LD2410_FRAME_HEADER[] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t LD2450_FRAME_HEADER[] = {0xAA, 0xFF, 0x03, 0x00};

void Satellite1Radar::setup() {
  this->detect_start_ms_ = millis();
  ESP_LOGI(TAG, "Starting non-blocking mmWave radar auto-detection (%.1fs timeout)...",
           DETECT_TIMEOUT_MS / 1000.0f);
}

void Satellite1Radar::loop() {
  if (!this->detection_complete_) {
    // Non-blocking detection: consume available UART bytes each loop iteration
    while (this->available()) {
      uint8_t byte;
      if (!this->read_byte(&byte))
        break;

      this->ring_buf_[this->ring_pos_ % 4] = byte;
      this->ring_pos_++;

      if (this->ring_pos_ < 4)
        continue;

      uint8_t seq[4];
      for (int i = 0; i < 4; i++)
        seq[i] = this->ring_buf_[(this->ring_pos_ - 4 + i) % 4];

      if (memcmp(seq, LD2410_FRAME_HEADER, 4) == 0) {
        ESP_LOGI(TAG, "Detected LD2410 radar (frame header 0xF4F3F2F1)");
        while (this->available()) { uint8_t discard; this->read_byte(&discard); }
        this->finalize_detection_(RadarType::LD2410);
        return;
      }

      if (memcmp(seq, LD2450_FRAME_HEADER, 4) == 0) {
        ESP_LOGI(TAG, "Detected LD2450 radar (frame header 0xAAFF0300)");
        while (this->available()) { uint8_t discard; this->read_byte(&discard); }
        this->finalize_detection_(RadarType::LD2450);
        return;
      }
    }

    if (millis() - this->detect_start_ms_ >= DETECT_TIMEOUT_MS) {
      ESP_LOGI(TAG, "No mmWave radar detected within timeout");
      this->finalize_detection_(RadarType::NONE);
    }
    return;
  }

  // Normal operation after detection
  if (detected_type_ == RadarType::LD2410) {
    ld2410_.loop(this);
  } else if (detected_type_ == RadarType::LD2450) {
    ld2450_.loop(this);
  }
}

void Satellite1Radar::finalize_detection_(RadarType type) {
  this->detected_type_ = type;
  this->detection_complete_ = true;

  if (radar_type_text_sensor_ != nullptr) {
    switch (type) {
      case RadarType::LD2410:
        radar_type_text_sensor_->publish_state("LD2410");
        break;
      case RadarType::LD2450:
        radar_type_text_sensor_->publish_state("LD2450");
        break;
      default:
        radar_type_text_sensor_->publish_state("None");
        break;
    }
  }

  apply_entity_visibility_();

  if (type == RadarType::LD2410) {
    ld2410_.presence_sensor = presence_binary_sensor_;
    ld2410_.moving_target_sensor = moving_target_binary_sensor_;
    ld2410_.still_target_sensor = still_target_binary_sensor_;
    ld2410_.setup(this);
  } else if (type == RadarType::LD2450) {
    ld2450_.presence_sensor = presence_binary_sensor_;
    ld2450_.moving_target_sensor = moving_target_binary_sensor_;
    ld2450_.still_target_sensor = still_target_binary_sensor_;
    ld2450_.setup(this);
  }
}

void Satellite1Radar::dump_config() {
  ESP_LOGCONFIG(TAG, "Satellite1 Radar:");
  if (!detection_complete_) {
    ESP_LOGCONFIG(TAG, "  Detection: in progress...");
    return;
  }
  switch (detected_type_) {
    case RadarType::LD2410:
      ESP_LOGCONFIG(TAG, "  Detected sensor: LD2410");
      break;
    case RadarType::LD2450:
      ESP_LOGCONFIG(TAG, "  Detected sensor: LD2450");
      break;
    case RadarType::NONE:
      ESP_LOGCONFIG(TAG, "  Detected sensor: None");
      break;
    default:
      ESP_LOGCONFIG(TAG, "  Detected sensor: Unknown");
      break;
  }
}

void Satellite1Radar::factory_reset_radar() {
  if (detected_type_ == RadarType::LD2410)
    ld2410_.factory_reset(this);
  else if (detected_type_ == RadarType::LD2450)
    ld2450_.factory_reset(this);
}

void Satellite1Radar::restart_radar() {
  if (detected_type_ == RadarType::LD2410)
    ld2410_.restart(this);
  else if (detected_type_ == RadarType::LD2450)
    ld2450_.restart(this);
}

void Satellite1Radar::query_radar_params() {
  if (detected_type_ == RadarType::LD2410)
    ld2410_.query_params(this);
}

// Helper to set internal flag on a non-null entity
template<typename T>
static void set_internal_if(T *entity, bool internal) {
  if (entity != nullptr)
    entity->set_internal(internal);
}

void Satellite1Radar::apply_entity_visibility_() {
  bool no_radar = (detected_type_ == RadarType::NONE || detected_type_ == RadarType::UNKNOWN);
  bool is_2410 = (detected_type_ == RadarType::LD2410);
  bool is_2450 = (detected_type_ == RadarType::LD2450);

  // Common entities: visible when any radar detected
  set_internal_if(presence_binary_sensor_, no_radar);
  set_internal_if(moving_target_binary_sensor_, no_radar);
  set_internal_if(still_target_binary_sensor_, no_radar);
  // Radar type text sensor is always visible
  set_internal_if(radar_type_text_sensor_, false);

  // LD2410-specific entities: hide when not LD2410
  set_internal_if(ld2410_.moving_distance, !is_2410);
  set_internal_if(ld2410_.still_distance, !is_2410);
  set_internal_if(ld2410_.moving_energy, !is_2410);
  set_internal_if(ld2410_.still_energy, !is_2410);
  set_internal_if(ld2410_.detection_distance, !is_2410);
  set_internal_if(ld2410_.light_sensor, !is_2410);

  for (int i = 0; i < LD2410Handler::NUM_GATES; i++) {
    set_internal_if(ld2410_.gate_move_energy[i], !is_2410);
    set_internal_if(ld2410_.gate_still_energy[i], !is_2410);
    set_internal_if(ld2410_.gate_move_threshold[i], !is_2410);
    set_internal_if(ld2410_.gate_still_threshold[i], !is_2410);
  }

  set_internal_if(ld2410_.timeout_number, !is_2410);
  set_internal_if(ld2410_.max_move_distance_gate, !is_2410);
  set_internal_if(ld2410_.max_still_distance_gate, !is_2410);
  set_internal_if(ld2410_.light_threshold, !is_2410);
  set_internal_if(ld2410_.engineering_mode_switch, !is_2410);
  set_internal_if(ld2410_.bluetooth_switch, !is_2410);
  set_internal_if(ld2410_.distance_resolution_select, !is_2410);
  set_internal_if(ld2410_.light_function_select, !is_2410);

  // LD2450-specific entities: hide when not LD2450
  set_internal_if(ld2450_.target_count, !is_2450);
  set_internal_if(ld2450_.still_target_count, !is_2450);
  set_internal_if(ld2450_.moving_target_count, !is_2450);

  for (int i = 0; i < LD2450Handler::NUM_TARGETS; i++) {
    set_internal_if(ld2450_.target_x[i], !is_2450);
    set_internal_if(ld2450_.target_y[i], !is_2450);
    set_internal_if(ld2450_.target_speed[i], !is_2450);
    set_internal_if(ld2450_.target_angle[i], !is_2450);
    set_internal_if(ld2450_.target_distance[i], !is_2450);
    set_internal_if(ld2450_.target_resolution[i], !is_2450);
    set_internal_if(ld2450_.target_direction[i], !is_2450);
  }

  for (int i = 0; i < LD2450Handler::NUM_ZONES; i++) {
    set_internal_if(ld2450_.zone_target_count[i], !is_2450);
    set_internal_if(ld2450_.zone_still_target_count[i], !is_2450);
    set_internal_if(ld2450_.zone_moving_target_count[i], !is_2450);
    for (int c = 0; c < 4; c++)
      set_internal_if(ld2450_.zone_coords[i][c], !is_2450);
  }

  set_internal_if(ld2450_.timeout_number, !is_2450);
  set_internal_if(ld2450_.bluetooth_switch, !is_2450);
  set_internal_if(ld2450_.multi_target_switch, !is_2450);
  set_internal_if(ld2450_.baud_rate_select, !is_2450);
  set_internal_if(ld2450_.zone_type_select, !is_2450);
  set_internal_if(ld2450_.version_text_sensor, !is_2450);
  set_internal_if(ld2450_.mac_text_sensor, !is_2450);
}

}  // namespace satellite1_radar
}  // namespace esphome
