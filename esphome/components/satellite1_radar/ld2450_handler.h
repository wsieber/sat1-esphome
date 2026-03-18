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

namespace esphome {
namespace satellite1_radar {

static const char *const TAG_LD2450 = "satellite1_radar.ld2450";

class LD2450Handler {
 public:
  static const int NUM_TARGETS = 3;
  static const int NUM_ZONES = 3;
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

  // Per-zone counts
  sensor::Sensor *zone_target_count[NUM_ZONES]{};
  sensor::Sensor *zone_still_target_count[NUM_ZONES]{};
  sensor::Sensor *zone_moving_target_count[NUM_ZONES]{};

  // Zone coordinate numbers: [zone][0=x1,1=y1,2=x2,3=y2]
  number::Number *zone_coords[NUM_ZONES][4]{};

  // Config numbers
  number::Number *timeout_number{nullptr};

  // Switches
  switch_::Switch *bluetooth_switch{nullptr};
  switch_::Switch *multi_target_switch{nullptr};

  // Selects
  select::Select *baud_rate_select{nullptr};
  select::Select *zone_type_select{nullptr};

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
    request_firmware_version_();
  }

  void loop(uart::UARTDevice *uart) {
    uart_ = uart;

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
  uint8_t buf_[MAX_BUF]{};
  int buf_pos_{0};
  uart::UARTDevice *uart_{nullptr};

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

      float x_cm = static_cast<float>(to_signed(raw_x)) / 10.0f;
      float y_cm = static_cast<float>(to_signed(raw_y)) / 10.0f;
      float speed = static_cast<float>(to_signed(raw_speed));
      float dist = std::sqrt(x_cm * x_cm + y_cm * y_cm);
      float angle = (dist > 0.0f) ? (std::atan2(x_cm, y_cm) * 180.0f / M_PI) : 0.0f;

      bool valid = (dist > 0.0f);

      if (valid) {
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

      publish_sensor_(target_x[t], valid ? x_cm : 0.0f);
      publish_sensor_(target_y[t], valid ? y_cm : 0.0f);
      publish_sensor_(target_speed[t], valid ? speed : 0.0f);
      publish_sensor_(target_angle[t], valid ? angle : 0.0f);
      publish_sensor_(target_distance[t], valid ? dist : 0.0f);
      publish_sensor_(target_resolution[t], valid ? static_cast<float>(raw_res) : 0.0f);

      if (target_direction[t] != nullptr) {
        const char *dir = "N/A";
        if (valid) {
          dir = (speed > 0) ? "Approaching" : (speed < 0) ? "Moving away" : "Stationary";
        }
        target_direction[t]->publish_state(dir);
      }
    }

    // Publish aggregate counts
    publish_sensor_(target_count, static_cast<float>(total_count));
    publish_sensor_(still_target_count, static_cast<float>(still_count));
    publish_sensor_(moving_target_count, static_cast<float>(moving_count));

    // Publish common binary sensors
    if (presence_sensor != nullptr) presence_sensor->publish_state(any_target);
    if (moving_target_sensor != nullptr) moving_target_sensor->publish_state(any_moving);
    if (still_target_sensor != nullptr) still_target_sensor->publish_state(any_still);

    update_zone_counts_();
  }

  void update_zone_counts_() {
    for (int z = 0; z < NUM_ZONES; z++) {
      if (zone_target_count[z] == nullptr)
        continue;

      float x1 = get_number_val_(zone_coords[z][0], 0.0f);
      float y1 = get_number_val_(zone_coords[z][1], 0.0f);
      float x2 = get_number_val_(zone_coords[z][2], 0.0f);
      float y2 = get_number_val_(zone_coords[z][3], 0.0f);

      if (x1 == 0.0f && y1 == 0.0f && x2 == 0.0f && y2 == 0.0f)
        continue;

      float min_x = std::min(x1, x2), max_x = std::max(x1, x2);
      float min_y = std::min(y1, y2), max_y = std::max(y1, y2);

      int zt = 0, zs = 0, zm = 0;

      for (int t = 0; t < NUM_TARGETS; t++) {
        if (target_x[t] == nullptr || target_y[t] == nullptr)
          continue;

        float tx = target_x[t]->state;
        float ty = target_y[t]->state;
        float ts = target_speed[t] ? target_speed[t]->state : 0.0f;
        float td = target_distance[t] ? target_distance[t]->state : 0.0f;

        if (td <= 0.0f)
          continue;

        if (tx >= min_x && tx <= max_x && ty >= min_y && ty <= max_y) {
          zt++;
          if (std::fabs(ts) > 0.0f)
            zm++;
          else
            zs++;
        }
      }

      publish_sensor_(zone_target_count[z], static_cast<float>(zt));
      publish_sensor_(zone_still_target_count[z], static_cast<float>(zs));
      publish_sensor_(zone_moving_target_count[z], static_cast<float>(zm));
    }
  }

  void handle_ack_frame_(const uint8_t *buf, int len) {
    if (len < 10)
      return;
    if (buf[0] != 0xFD || buf[1] != 0xFC || buf[2] != 0xFB || buf[3] != 0xFA)
      return;

    uint16_t cmd_word = to_uint16(buf[6], buf[7]);

    // Firmware version response (command 0x00A0)
    if (cmd_word == 0x00A0 && len >= 20) {
      char ver[32];
      snprintf(ver, sizeof(ver), "V%d.%02d.%02d%02d%02d%02d",
               buf[13], buf[12], buf[17], buf[16], buf[15], buf[14]);
      if (version_text_sensor != nullptr)
        version_text_sensor->publish_state(ver);
      ESP_LOGI(TAG_LD2450, "Firmware version: %s", ver);
    }

    // MAC address response (command 0x00A5)
    if (cmd_word == 0x00A5 && len >= 16) {
      char mac[20];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
      if (mac_text_sensor != nullptr)
        mac_text_sensor->publish_state(mac);
      ESP_LOGI(TAG_LD2450, "BT MAC: %s", mac);
    }
  }

  void request_firmware_version_() {
    static const uint8_t cmd[] = {0x02, 0x00, 0xA0, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_(cmd, sizeof(cmd));
  }

  void send_command_(const uint8_t *cmd, size_t len) {
    if (uart_ == nullptr)
      return;

    // Enter configuration mode
    static const uint8_t enable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    uart_->write_array(enable, sizeof(enable));

    // Send the actual command with header
    static const uint8_t header[] = {0xFD, 0xFC, 0xFB, 0xFA};
    uart_->write_array(header, sizeof(header));
    uart_->write_array(cmd, len);

    // Exit configuration mode
    static const uint8_t disable[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};
    uart_->write_array(disable, sizeof(disable));
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
