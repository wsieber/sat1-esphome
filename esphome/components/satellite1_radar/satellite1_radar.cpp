#include "satellite1_radar.h"
#include "esphome/core/log.h"
#include <cstring>
#ifdef USE_ESP_IDF
#include "radar_tuner_ld2450_html.h"
#include "radar_tuner_ld2410_html.h"
#endif

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
#ifdef USE_ESP_IDF
    if (write_config_pending_) {
      write_config_pending_ = false;
      ld2410_.write_gate_config(this);
      if (ld2410_.distance_resolution_select != nullptr) {
        bool fine = (ld2410_.distance_resolution_select->current_option() == "0.2m");
        ld2410_.set_distance_resolution(this, fine);
      }
    }
#endif
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

#ifdef USE_ESP_IDF
void Satellite1Radar::start_tuner() {
  tuner_server_.clear_registrations();

  if (detected_type_ == RadarType::LD2450) {
    tuner_server_.set_html_content(RADAR_TUNER_LD2450_HTML_GZ, RADAR_TUNER_LD2450_HTML_GZ_LEN);

    for (int z = 0; z < LD2450Handler::NUM_ZONES; z++) {
      char id[32];
      snprintf(id, sizeof(id), "zone_%d_points_count", z + 1);
      tuner_server_.register_number(id, ld2450_.zone_points_count[z]);
      for (int p = 0; p < LD2450Handler::MAX_ZONE_POINTS; p++) {
        snprintf(id, sizeof(id), "zone_%d_p%d_x", z + 1, p + 1);
        tuner_server_.register_number(id, ld2450_.zone_point_coords[z][p][0]);
        snprintf(id, sizeof(id), "zone_%d_p%d_y", z + 1, p + 1);
        tuner_server_.register_number(id, ld2450_.zone_point_coords[z][p][1]);
      }
    }
    tuner_server_.register_number("exclusion_zone_points_count", ld2450_.excl_zone_points_count);
    for (int p = 0; p < LD2450Handler::MAX_ZONE_POINTS; p++) {
      char id[32];
      snprintf(id, sizeof(id), "exclusion_zone_p%d_x", p + 1);
      tuner_server_.register_number(id, ld2450_.excl_zone_point_coords[p][0]);
      snprintf(id, sizeof(id), "exclusion_zone_p%d_y", p + 1);
      tuner_server_.register_number(id, ld2450_.excl_zone_point_coords[p][1]);
    }
    tuner_server_.register_number("detection_range", ld2450_.detection_range);
    tuner_server_.register_number("ld2450_timeout", ld2450_.timeout_number);
    tuner_server_.register_number("ld2450_stability", ld2450_.stability_number);
    tuner_server_.register_switch("ld2450_multi_target", ld2450_.multi_target_switch);
    tuner_server_.register_switch("ld2450_bluetooth", ld2450_.bluetooth_switch);

    ld2450_.on_target_update = [this](int target, float x, float y) {
      tuner_server_.update_target(target, x, y);
    };

  } else if (detected_type_ == RadarType::LD2410) {
    tuner_server_.set_html_content(RADAR_TUNER_LD2410_HTML_GZ, RADAR_TUNER_LD2410_HTML_GZ_LEN);

    tuner_server_.register_number("ld2410_timeout", ld2410_.timeout_number);
    tuner_server_.register_number("ld2410_max_move_gate", ld2410_.max_move_distance_gate);
    tuner_server_.register_number("ld2410_max_still_gate", ld2410_.max_still_distance_gate);

    for (int g = 0; g < LD2410Handler::NUM_GATES; g++) {
      char id[32];
      snprintf(id, sizeof(id), "g%d_move_threshold", g);
      tuner_server_.register_number(id, ld2410_.gate_move_threshold[g]);
      snprintf(id, sizeof(id), "g%d_still_threshold", g);
      tuner_server_.register_number(id, ld2410_.gate_still_threshold[g]);
    }

    tuner_server_.register_switch("ld2410_bluetooth", ld2410_.bluetooth_switch);
    tuner_server_.register_select("ld2410_distance_resolution", ld2410_.distance_resolution_select);
    tuner_server_.set_gate_sensors(ld2410_.gate_move_energy, ld2410_.gate_still_energy);

    tuner_server_.set_write_config_callback([this]() {
      write_config_pending_ = true;
    });

    ld2410_.enable_engineering_mode(this);
  }

  tuner_server_.start();
}

void Satellite1Radar::stop_tuner() {
  if (detected_type_ == RadarType::LD2450) {
    ld2450_.on_target_update = nullptr;
  } else if (detected_type_ == RadarType::LD2410) {
    ld2410_.disable_engineering_mode(this);
  }
  tuner_server_.stop();
}
#endif

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

  // Presence binary sensor: visible when any radar detected
  set_internal_if(presence_binary_sensor_, no_radar);
  // Moving/still binary sensors: visible only for LD2410 (LD2450 uses count sensors instead)
  set_internal_if(moving_target_binary_sensor_, !is_2410);
  set_internal_if(still_target_binary_sensor_, !is_2410);
  // Radar type text sensor always visible (diagnostic)
  set_internal_if(radar_type_text_sensor_, false);

  // Buttons: visible when a radar is detected; query_params only for LD2410
  set_internal_if(factory_reset_button_, no_radar);
  set_internal_if(restart_button_, no_radar);
  set_internal_if(query_params_button_, true);

  // LD2410-specific entities: hide when not LD2410
  set_internal_if(ld2410_.moving_distance, !is_2410);
  set_internal_if(ld2410_.still_distance, !is_2410);
  set_internal_if(ld2410_.moving_energy, !is_2410);
  set_internal_if(ld2410_.still_energy, !is_2410);
  set_internal_if(ld2410_.detection_distance, !is_2410);
  set_internal_if(ld2410_.light_sensor, !is_2410);

  for (int i = 0; i < LD2410Handler::NUM_GATES; i++) {
    // Gate energy sensors always hidden (engineering debug data only)
    set_internal_if(ld2410_.gate_move_energy[i], true);
    set_internal_if(ld2410_.gate_still_energy[i], true);
    set_internal_if(ld2410_.gate_move_threshold[i], !is_2410);
    set_internal_if(ld2410_.gate_still_threshold[i], !is_2410);
  }

  set_internal_if(ld2410_.timeout_number, true);
  set_internal_if(ld2410_.max_move_distance_gate, true);
  set_internal_if(ld2410_.max_still_distance_gate, true);
  set_internal_if(ld2410_.light_threshold, !is_2410);
  // Engineering mode switch always hidden (gate sensors are hidden, no HA feedback)
  set_internal_if(ld2410_.engineering_mode_switch, true);
  set_internal_if(ld2410_.bluetooth_switch, true);
  set_internal_if(ld2410_.distance_resolution_select, true);
  set_internal_if(ld2410_.light_function_select, !is_2410);
  if (ld2410_.version_text_sensor != nullptr &&
      ld2410_.version_text_sensor == ld2450_.version_text_sensor) {
    set_internal_if(ld2410_.version_text_sensor, no_radar);
  } else {
    set_internal_if(ld2410_.version_text_sensor, !is_2410);
  }

  // LD2450: count sensors visible when LD2450
  set_internal_if(ld2450_.target_count, !is_2450);
  set_internal_if(ld2450_.still_target_count, !is_2450);
  set_internal_if(ld2450_.moving_target_count, !is_2450);

  // LD2450: per-target detail sensors always hidden (internal use only for zone editor)
  for (int i = 0; i < LD2450Handler::NUM_TARGETS; i++) {
    set_internal_if(ld2450_.target_x[i], true);
    set_internal_if(ld2450_.target_y[i], true);
    set_internal_if(ld2450_.target_speed[i], true);
    set_internal_if(ld2450_.target_angle[i], true);
    set_internal_if(ld2450_.target_distance[i], true);
    set_internal_if(ld2450_.target_resolution[i], true);
    set_internal_if(ld2450_.target_direction[i], true);
  }

  // LD2450: zone state text sensors visible when LD2450
  for (int i = 0; i < LD2450Handler::NUM_ZONES; i++) {
    set_internal_if(ld2450_.zone_state[i], !is_2450);
  }

  // LD2450: polygon zone config numbers always hidden (configured via zone editor only)
  for (int i = 0; i < LD2450Handler::NUM_ZONES; i++) {
    set_internal_if(ld2450_.zone_points_count[i], true);
    for (int p = 0; p < LD2450Handler::MAX_ZONE_POINTS; p++) {
      set_internal_if(ld2450_.zone_point_coords[i][p][0], true);
      set_internal_if(ld2450_.zone_point_coords[i][p][1], true);
    }
  }
  set_internal_if(ld2450_.excl_zone_points_count, true);
  for (int p = 0; p < LD2450Handler::MAX_ZONE_POINTS; p++) {
    set_internal_if(ld2450_.excl_zone_point_coords[p][0], true);
    set_internal_if(ld2450_.excl_zone_point_coords[p][1], true);
  }
  set_internal_if(ld2450_.detection_range, true);

  // Tuner switch: visible whenever any radar is detected
  set_internal_if(tuner_switch_, no_radar);

  // LD2450: stability and timeout hidden (configured via zone editor only)
  set_internal_if(ld2450_.stability_number, true);
  set_internal_if(ld2450_.timeout_number, true);
  set_internal_if(ld2450_.bluetooth_switch, true);
  set_internal_if(ld2450_.multi_target_switch, true);
  set_internal_if(ld2450_.baud_rate_select, true);
  if (ld2450_.version_text_sensor != ld2410_.version_text_sensor) {
    set_internal_if(ld2450_.version_text_sensor, !is_2450);
  }
  // BT MAC always hidden (query not implemented, always shows unknown)
  set_internal_if(ld2450_.mac_text_sensor, true);
}

}  // namespace satellite1_radar
}  // namespace esphome
