#pragma once

#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <string>

namespace esphome {
namespace satellite1_radar {

static const char *const TAG_LD2450 = "satellite1_radar.ld2450";

using TargetUpdateCallback = std::function<void(int target, float x, float y)>;

class LD2450Handler {
 public:
  static const int NUM_TARGETS = 3;
  static const int NUM_ZONES = 3;
  static const int MAX_ZONE_POINTS = 8;
  static const int DATA_FRAME_SIZE = 30;

  // Per-target sensors
  sensor::Sensor *target_x[NUM_TARGETS]{};
  sensor::Sensor *target_y[NUM_TARGETS]{};
  sensor::Sensor *target_speed[NUM_TARGETS]{};
  sensor::Sensor *target_angle[NUM_TARGETS]{};
  sensor::Sensor *target_distance[NUM_TARGETS]{};
  sensor::Sensor *target_resolution[NUM_TARGETS]{};

  // Aggregate counts
  sensor::Sensor *target_count{nullptr};
  sensor::Sensor *still_target_count{nullptr};
  sensor::Sensor *moving_target_count{nullptr};

  // Per-zone state text sensors
  text_sensor::TextSensor *zone_state[NUM_ZONES]{};

  // Polygon zone coordinates: [zone][point][0=x,1=y]
  number::Number *zone_point_coords[NUM_ZONES][MAX_ZONE_POINTS][2]{};
  number::Number *zone_points_count[NUM_ZONES]{};

  // Exclusion zone (single polygon)
  number::Number *excl_zone_point_coords[MAX_ZONE_POINTS][2]{};
  number::Number *excl_zone_points_count{nullptr};

  // Detection range
  number::Number *detection_range{nullptr};

  // Target update callback (set by parent when zone editor is active)
  TargetUpdateCallback on_target_update{nullptr};

  // Config numbers
  number::Number *timeout_number{nullptr};
  number::Number *stability_number{nullptr};

  // Switches
  switch_::Switch *bluetooth_switch{nullptr};
  switch_::Switch *multi_target_switch{nullptr};

  // Selects
  select::Select *baud_rate_select{nullptr};

  // Text sensors
  text_sensor::TextSensor *version_text_sensor{nullptr};
  text_sensor::TextSensor *mac_text_sensor{nullptr};
  text_sensor::TextSensor *target_direction[NUM_TARGETS]{};

  // Common entities (published by parent, stored as references)
  binary_sensor::BinarySensor *presence_sensor{nullptr};
  binary_sensor::BinarySensor *moving_target_sensor{nullptr};
  binary_sensor::BinarySensor *still_target_sensor{nullptr};

  void setup(uart::UARTDevice *uart) {
    ESP_LOGI(TAG_LD2450, "Initializing LD2450 handler");
    uart_ = uart;
    fw_version_received_ = false;
    fw_retry_count_ = 0;
    fw_next_retry_ms_ = millis() + 2000;
  }

  void loop(uart::UARTDevice *uart) {
    uart_ = uart;

    if (!fw_version_received_ && fw_retry_count_ < FW_MAX_RETRIES && millis() >= fw_next_retry_ms_) {
      ESP_LOGI(TAG_LD2450, "Requesting firmware version (attempt %d/%d)", fw_retry_count_ + 1, FW_MAX_RETRIES);
      request_firmware_version_();
      fw_retry_count_++;
      fw_next_retry_ms_ = millis() + 3000;
    }

    while (uart_->available()) {
      uint8_t byte;
      if (!uart_->read_byte(&byte))
        break;

      if (buf_pos_ < MAX_BUF)
        buf_[buf_pos_++] = byte;
      else
        buf_pos_ = 0;

      // Check for command ACK frames (header 0xFDFCFBFA, footer 0x04030201)
      if (buf_pos_ >= 10 &&
          buf_[buf_pos_ - 4] == 0x04 && buf_[buf_pos_ - 3] == 0x03 &&
          buf_[buf_pos_ - 2] == 0x02 && buf_[buf_pos_ - 1] == 0x01) {
        handle_ack_frame_(buf_, buf_pos_);
        buf_pos_ = 0;
        continue;
      }

      // Check for data frames: header 0xAAFF0300 at bytes 0-3, footer 0x55CC at bytes 28-29
      if (buf_pos_ >= DATA_FRAME_SIZE &&
          buf_[0] == 0xAA && buf_[1] == 0xFF && buf_[2] == 0x03 && buf_[3] == 0x00 &&
          buf_[28] == 0x55 && buf_[29] == 0xCC) {
        parse_data_frame_(buf_);
        buf_pos_ = 0;
      }
    }
  }

  void factory_reset(uart::UARTDevice *uart) {
    uart_ = uart;
    static const uint8_t cmd[] = {0x02, 0x00, 0xA2, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_(cmd, sizeof(cmd));
    ESP_LOGI(TAG_LD2450, "Factory reset command sent");
  }

  void restart(uart::UARTDevice *uart) {
    uart_ = uart;
    static const uint8_t cmd[] = {0x02, 0x00, 0xA3, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_(cmd, sizeof(cmd));
    ESP_LOGI(TAG_LD2450, "Restart command sent");
  }

  void set_single_target() {
    static const uint8_t cmd[] = {0x02, 0x00, 0x80, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_(cmd, sizeof(cmd));
  }

  void set_multi_target() {
    static const uint8_t cmd[] = {0x02, 0x00, 0x90, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_(cmd, sizeof(cmd));
  }

  void turn_bluetooth_on() {
    static const uint8_t cmd[] = {0x04, 0x00, 0xA4, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_(cmd, sizeof(cmd));
  }

  void turn_bluetooth_off() {
    static const uint8_t cmd[] = {0x04, 0x00, 0xA4, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_(cmd, sizeof(cmd));
  }

 protected:
  static const int MAX_BUF = 160;
  static const int FW_MAX_RETRIES = 5;
  static const int DEFAULT_STABILITY = 5;
  uint8_t buf_[MAX_BUF]{};
  int buf_pos_{0};
  uart::UARTDevice *uart_{nullptr};
  bool fw_version_received_{false};
  int fw_retry_count_{0};
  uint32_t fw_next_retry_ms_{0};

  // Debounce state for aggregate count sensors
  int pub_total_count_{-1}, cand_total_count_{-1}, streak_total_{0};
  int pub_moving_count_{-1}, cand_moving_count_{-1}, streak_moving_{0};
  int pub_still_count_{-1}, cand_still_count_{-1}, streak_still_{0};

  // Debounce state for binary sensors
  int pub_presence_{-1}, cand_presence_{-1}, streak_presence_{0};
  int pub_has_moving_{-1}, cand_has_moving_{-1}, streak_has_moving_{0};
  int pub_has_still_{-1}, cand_has_still_{-1}, streak_has_still_{0};

  // Debounce state for zone text sensors
  std::string pub_zone_state_[NUM_ZONES];
  std::string cand_zone_state_[NUM_ZONES];
  int streak_zone_[NUM_ZONES]{};

  static uint16_t to_uint16(uint8_t lo, uint8_t hi) {
    return (static_cast<uint16_t>(hi) << 8) | lo;
  }

  static int16_t to_signed(uint16_t val) {
    return (val & 0x8000) ? static_cast<int16_t>(val & 0x7FFF) : -static_cast<int16_t>(val & 0x7FFF);
  }

  void parse_data_frame_(const uint8_t *buf) {
    int total_count = 0;
    int still_count = 0;
    int moving_count = 0;
    bool any_target = false;
    bool any_moving = false;
    bool any_still = false;

    for (int t = 0; t < NUM_TARGETS; t++) {
      int base = 4 + t * 8;
      uint16_t raw_x = to_uint16(buf[base], buf[base + 1]);
      uint16_t raw_y = to_uint16(buf[base + 2], buf[base + 3]);
      uint16_t raw_speed = to_uint16(buf[base + 4], buf[base + 5]);
      uint16_t raw_res = to_uint16(buf[base + 6], buf[base + 7]);

      float x_cm = -static_cast<float>(to_signed(raw_x)) / 10.0f;
      float y_cm = static_cast<float>(to_signed(raw_y)) / 10.0f;
      float speed = -static_cast<float>(to_signed(raw_speed));
      float dist = std::sqrt(x_cm * x_cm + y_cm * y_cm);
      float angle = (dist > 0.0f) ? (std::atan2(x_cm, y_cm) * 180.0f / M_PI) : 0.0f;

      bool valid = (dist > 0.0f);

      float pub_x = valid ? x_cm : 0.0f;
      float pub_y = valid ? y_cm : 0.0f;
      publish_sensor_(target_x[t], pub_x);
      publish_sensor_(target_y[t], pub_y);
      publish_sensor_(target_speed[t], valid ? speed : 0.0f);
      publish_sensor_(target_angle[t], valid ? angle : 0.0f);
      publish_sensor_(target_distance[t], valid ? dist : 0.0f);
      publish_sensor_(target_resolution[t], valid ? static_cast<float>(raw_res) : 0.0f);

      if (valid && !is_beyond_detection_range_(dist) && !is_in_exclusion_zone_(x_cm, y_cm)) {
        total_count++;
        any_target = true;

        if (std::fabs(speed) > 0.0f) {
          moving_count++;
          any_moving = true;
        } else {
          still_count++;
          any_still = true;
        }
      }

      if (on_target_update) {
        on_target_update(t, pub_x, pub_y);
      }

      if (target_direction[t] != nullptr) {
        const char *dir = "N/A";
        if (valid) {
          dir = (speed > 0) ? "Approaching" : (speed < 0) ? "Moving away" : "Stationary";
        }
        target_direction[t]->publish_state(dir);
      }
    }

    int threshold = get_stability_threshold_();

    debounce_sensor_(pub_total_count_, cand_total_count_, streak_total_,
                     total_count, threshold, target_count);
    debounce_sensor_(pub_still_count_, cand_still_count_, streak_still_,
                     still_count, threshold, still_target_count);
    debounce_sensor_(pub_moving_count_, cand_moving_count_, streak_moving_,
                     moving_count, threshold, moving_target_count);

    debounce_binary_(pub_presence_, cand_presence_, streak_presence_,
                     any_target, threshold, presence_sensor);
    debounce_binary_(pub_has_moving_, cand_has_moving_, streak_has_moving_,
                     any_moving, threshold, moving_target_sensor);
    debounce_binary_(pub_has_still_, cand_has_still_, streak_has_still_,
                     any_still, threshold, still_target_sensor);

    update_zone_states_(threshold);
  }

  bool point_in_polygon_(number::Number *points[][2], number::Number *count_num, float x, float y) {
    if (count_num == nullptr) return false;
    int n = static_cast<int>(get_number_val_(count_num, 0.0f));
    if (n < 3 || n > MAX_ZONE_POINTS) return false;

    float xs[MAX_ZONE_POINTS], ys[MAX_ZONE_POINTS];
    for (int i = 0; i < n; i++) {
      xs[i] = get_number_val_(points[i][0], 0.0f);
      ys[i] = get_number_val_(points[i][1], 0.0f);
    }

    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      if (((ys[i] > y) != (ys[j] > y)) &&
          (x < (xs[j] - xs[i]) * (y - ys[i]) / (ys[j] - ys[i]) + xs[i]))
        inside = !inside;
    }
    return inside;
  }

  bool is_in_exclusion_zone_(float x, float y) {
    return point_in_polygon_(excl_zone_point_coords, excl_zone_points_count, x, y);
  }

  bool is_beyond_detection_range_(float dist) {
    if (detection_range == nullptr) return false;
    float range = get_number_val_(detection_range, 0.0f);
    return (range > 0.0f && dist > range);
  }

  void update_zone_states_(int threshold) {
    for (int z = 0; z < NUM_ZONES; z++) {
      if (zone_state[z] == nullptr)
        continue;

      int n = static_cast<int>(get_number_val_(zone_points_count[z], 0.0f));
      if (n < 3) {
        debounce_zone_(z, "Undefined", threshold);
        continue;
      }

      // Priority: Approaching (2) > Moving Away (1) > Still (0)
      int best = -1;

      for (int t = 0; t < NUM_TARGETS; t++) {
        if (target_x[t] == nullptr || target_y[t] == nullptr)
          continue;

        float tx = target_x[t]->state;
        float ty = target_y[t]->state;
        float ts = target_speed[t] ? target_speed[t]->state : 0.0f;
        float td = target_distance[t] ? target_distance[t]->state : 0.0f;

        if (td <= 0.0f)
          continue;

        if (is_beyond_detection_range_(td))
          continue;

        if (is_in_exclusion_zone_(tx, ty))
          continue;

        if (point_in_polygon_(zone_point_coords[z], zone_points_count[z], tx, ty)) {
          if (ts > 0.0f) {
            best = 2;
            break;
          } else if (ts < 0.0f) {
            if (best < 1) best = 1;
          } else {
            if (best < 0) best = 0;
          }
        }
      }

      const char *state_str = (best == 2) ? "Approaching" :
                               (best == 1) ? "Moving Away" :
                               (best == 0) ? "Still" : "Clear";
      debounce_zone_(z, state_str, threshold);
    }
  }

  void handle_ack_frame_(const uint8_t *buf, int len) {
    if (len < 10)
      return;
    if (buf[0] != 0xFD || buf[1] != 0xFC || buf[2] != 0xFB || buf[3] != 0xFA)
      return;

    uint16_t cmd_word = to_uint16(buf[6], buf[7]);

    // Firmware version response (command 0x01A0)
    if (cmd_word == 0x01A0 && len >= 20) {
      char ver[32];
      snprintf(ver, sizeof(ver), "V%u.%02X.%02X%02X%02X%02X",
               buf[13], buf[12], buf[17], buf[16], buf[15], buf[14]);
      if (version_text_sensor != nullptr)
        version_text_sensor->publish_state(ver);
      fw_version_received_ = true;
      ESP_LOGI(TAG_LD2450, "Firmware version: %s", ver);
    }

    // MAC address response (command 0x01A5)
    if (cmd_word == 0x01A5 && len >= 16) {
      char mac[20];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
      if (mac_text_sensor != nullptr)
        mac_text_sensor->publish_state(mac);
      ESP_LOGI(TAG_LD2450, "BT MAC: %s", mac);
    }
  }

  void request_firmware_version_() {
    if (uart_ == nullptr)
      return;

    static const uint8_t enable[]  = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    static const uint8_t header[]  = {0xFD, 0xFC, 0xFB, 0xFA};
    static const uint8_t cmd[]     = {0x02, 0x00, 0xA0, 0x00, 0x04, 0x03, 0x02, 0x01};
    static const uint8_t disable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};

    // Drain any pending data frame bytes
    uint8_t dummy;
    int drained = 0;
    while (uart_->available()) { uart_->read_byte(&dummy); drained++; }
    buf_pos_ = 0;
    ESP_LOGI(TAG_LD2450, "Drained %d bytes before enable", drained);

    // Enter config mode and wait for LD2450 to process
    uart_->write_array(enable, sizeof(enable));
    uart_->flush();
    delay(50);

    // Drain enable ACK
    int ack_drained = 0;
    while (uart_->available()) { uart_->read_byte(&dummy); ack_drained++; }
    ESP_LOGI(TAG_LD2450, "Drained %d bytes after enable (ACK)", ack_drained);

    // Send firmware version query
    uart_->write_array(header, sizeof(header));
    uart_->write_array(cmd, sizeof(cmd));
    uart_->flush();

    // Read response with timeout
    uint8_t resp[64];
    int rpos = 0;
    uint32_t start = millis();
    while (millis() - start < 200 && rpos < 64) {
      if (uart_->available()) {
        uart_->read_byte(&resp[rpos++]);
      } else {
        delay(5);
      }
    }

    // Debug: log raw response
    ESP_LOGI(TAG_LD2450, "FW query got %d bytes", rpos);
    if (rpos > 0) {
      char hex[97];
      int hlen = 0;
      for (int i = 0; i < rpos && i < 32 && hlen < 94; i++)
        hlen += snprintf(hex + hlen, sizeof(hex) - hlen, "%02X ", resp[i]);
      ESP_LOGI(TAG_LD2450, "Response: %s", hex);
    }

    // Search for firmware version ACK (header FD FC FB FA, command word 0x01A0)
    for (int i = 0; i <= rpos - 18; i++) {
      if (resp[i] == 0xFD && resp[i + 1] == 0xFC && resp[i + 2] == 0xFB && resp[i + 3] == 0xFA) {
        uint16_t cw = resp[i + 6] | (static_cast<uint16_t>(resp[i + 7]) << 8);
        ESP_LOGI(TAG_LD2450, "Found ACK header at offset %d, cmd=0x%04X, rpos=%d", i, cw, rpos);
        if (cw == 0x01A0 && i + 18 <= rpos) {
          char ver[32];
          snprintf(ver, sizeof(ver), "V%u.%02X.%02X%02X%02X%02X",
                   resp[i + 13], resp[i + 12], resp[i + 17], resp[i + 16], resp[i + 15], resp[i + 14]);
          if (version_text_sensor != nullptr)
            version_text_sensor->publish_state(ver);
          fw_version_received_ = true;
          ESP_LOGI(TAG_LD2450, "Firmware version: %s", ver);
          break;
        }
      }
    }

    // Exit config mode
    uart_->write_array(disable, sizeof(disable));
    uart_->flush();

    // Drain any remaining bytes and reset parser buffer
    while (uart_->available()) uart_->read_byte(&dummy);
    buf_pos_ = 0;
  }

  void send_command_(const uint8_t *cmd, size_t len) {
    if (uart_ == nullptr)
      return;

    static const uint8_t enable[]  = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    static const uint8_t header[]  = {0xFD, 0xFC, 0xFB, 0xFA};
    static const uint8_t disable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};

    uart_->write_array(enable, sizeof(enable));
    uart_->flush();
    delay(50);

    uart_->write_array(header, sizeof(header));
    uart_->write_array(cmd, len);
    uart_->flush();
    delay(50);

    uart_->write_array(disable, sizeof(disable));
    uart_->flush();
  }

  int get_stability_threshold_() {
    int val = static_cast<int>(get_number_val_(stability_number, static_cast<float>(DEFAULT_STABILITY)));
    return (val < 0) ? 0 : val;
  }

  void debounce_sensor_(int &pub, int &cand, int &streak, int raw, int threshold, sensor::Sensor *s) {
    if (s == nullptr)
      return;
    if (threshold <= 0 || pub == -1) {
      pub = raw;
      s->publish_state(static_cast<float>(raw));
      return;
    }
    if (raw == pub) {
      streak = 0;
      return;
    }
    if (raw == cand) {
      streak++;
    } else {
      cand = raw;
      streak = 1;
    }
    if (streak >= threshold) {
      pub = raw;
      s->publish_state(static_cast<float>(raw));
      streak = 0;
    }
  }

  void debounce_binary_(int &pub, int &cand, int &streak, bool raw, int threshold,
                         binary_sensor::BinarySensor *s) {
    if (s == nullptr)
      return;
    int raw_int = raw ? 1 : 0;
    if (threshold <= 0 || pub == -1) {
      pub = raw_int;
      s->publish_state(raw);
      return;
    }
    if (raw_int == pub) {
      streak = 0;
      return;
    }
    if (raw_int == cand) {
      streak++;
    } else {
      cand = raw_int;
      streak = 1;
    }
    if (streak >= threshold) {
      pub = raw_int;
      s->publish_state(raw);
      streak = 0;
    }
  }

  void debounce_zone_(int z, const std::string &raw, int threshold) {
    if (zone_state[z] == nullptr)
      return;
    if (threshold <= 0 || pub_zone_state_[z].empty()) {
      pub_zone_state_[z] = raw;
      zone_state[z]->publish_state(raw);
      return;
    }
    if (raw == pub_zone_state_[z]) {
      streak_zone_[z] = 0;
      return;
    }
    if (raw == cand_zone_state_[z]) {
      streak_zone_[z]++;
    } else {
      cand_zone_state_[z] = raw;
      streak_zone_[z] = 1;
    }
    if (streak_zone_[z] >= threshold) {
      pub_zone_state_[z] = raw;
      zone_state[z]->publish_state(raw);
      streak_zone_[z] = 0;
    }
  }

  static void publish_sensor_(sensor::Sensor *s, float val) {
    if (s != nullptr)
      s->publish_state(val);
  }

  static float get_number_val_(number::Number *n, float def) {
    if (n == nullptr)
      return def;
    float v = n->state;
    return std::isnan(v) ? def : v;
  }
};

}  // namespace satellite1_radar
}  // namespace esphome
