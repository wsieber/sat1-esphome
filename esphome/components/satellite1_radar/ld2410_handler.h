#pragma once

#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include <cstring>

namespace esphome {
namespace satellite1_radar {

static const char *const TAG_LD2410 = "satellite1_radar.ld2410";

/*
 * LD2410 protocol reference:
 *   Data frames:    header 0xF4F3F2F1 ... footer 0xF8F7F6F5
 *   Command frames: header 0xFDFCFBFA ... footer 0x04030201
 *
 * Data frame types (byte 6 = target state):
 *   0x00 = no target
 *   0x01 = moving target only
 *   0x02 = still target only
 *   0x03 = both moving and still targets
 *
 * Engineering mode adds per-gate energy readings after the basic data.
 */

class LD2410Handler {
 public:
  static const int NUM_GATES = 9;  // g0 through g8

  // Sensors
  sensor::Sensor *moving_distance{nullptr};
  sensor::Sensor *still_distance{nullptr};
  sensor::Sensor *moving_energy{nullptr};
  sensor::Sensor *still_energy{nullptr};
  sensor::Sensor *detection_distance{nullptr};
  sensor::Sensor *light_sensor{nullptr};

  sensor::Sensor *gate_move_energy[NUM_GATES]{};
  sensor::Sensor *gate_still_energy[NUM_GATES]{};

  // Numbers
  number::Number *timeout_number{nullptr};
  number::Number *max_move_distance_gate{nullptr};
  number::Number *max_still_distance_gate{nullptr};
  number::Number *light_threshold{nullptr};

  number::Number *gate_move_threshold[NUM_GATES]{};
  number::Number *gate_still_threshold[NUM_GATES]{};

  // Switches
  switch_::Switch *engineering_mode_switch{nullptr};
  switch_::Switch *bluetooth_switch{nullptr};

  // Selects
  select::Select *distance_resolution_select{nullptr};
  select::Select *light_function_select{nullptr};

  // Common entities (references from parent)
  binary_sensor::BinarySensor *presence_sensor{nullptr};
  binary_sensor::BinarySensor *moving_target_sensor{nullptr};
  binary_sensor::BinarySensor *still_target_sensor{nullptr};

  void setup(uart::UARTDevice *uart) {
    ESP_LOGI(TAG_LD2410, "Initializing LD2410 handler");
    uart_ = uart;
    // Query initial parameters from the sensor
    query_params(uart);
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

      if (buf_pos_ < 4)
        continue;

      bool is_data = (buf_[0] == 0xF4 && buf_[1] == 0xF3 && buf_[2] == 0xF2 && buf_[3] == 0xF1);
      bool is_cmd = (buf_[0] == 0xFD && buf_[1] == 0xFC && buf_[2] == 0xFB && buf_[3] == 0xFA);

      if (!is_data && !is_cmd) {
        memmove(buf_, buf_ + 1, buf_pos_ - 1);
        buf_pos_--;
        continue;
      }

      // Data frame footer check
      if (is_data && buf_pos_ >= 8) {
        uint16_t data_len = to_uint16_(buf_[4], buf_[5]);
        int frame_len = 4 + 2 + data_len + 4;  // header(4) + len(2) + data + footer(4)

        if (buf_pos_ >= frame_len) {
          if (buf_[frame_len - 4] == 0xF8 && buf_[frame_len - 3] == 0xF7 &&
              buf_[frame_len - 2] == 0xF6 && buf_[frame_len - 1] == 0xF5) {
            parse_data_frame_(buf_, frame_len);
          }
          buf_pos_ = 0;
          continue;
        }
      }

      // Command frame footer check
      if (is_cmd && buf_pos_ >= 8) {
        uint16_t data_len = to_uint16_(buf_[4], buf_[5]);
        int frame_len = 4 + 2 + data_len + 4;

        if (buf_pos_ >= frame_len) {
          if (buf_[frame_len - 4] == 0x04 && buf_[frame_len - 3] == 0x03 &&
              buf_[frame_len - 2] == 0x02 && buf_[frame_len - 1] == 0x01) {
            handle_ack_frame_(buf_, frame_len);
          }
          buf_pos_ = 0;
          continue;
        }
      }

      // Buffer overflow protection
      if (buf_pos_ >= MAX_BUF - 1) {
        buf_pos_ = 0;
      }
    }
  }

  void factory_reset(uart::UARTDevice *uart) {
    uart_ = uart;
    // Command 0x00A2: factory reset
    uint8_t cmd[] = {0x04, 0x00, 0x02, 0x00, 0xA2, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_raw_(cmd, sizeof(cmd));
    ESP_LOGI(TAG_LD2410, "Factory reset command sent");
  }

  void restart(uart::UARTDevice *uart) {
    uart_ = uart;
    // Command 0x00A3: restart
    uint8_t cmd[] = {0x04, 0x00, 0x02, 0x00, 0xA3, 0x00, 0x04, 0x03, 0x02, 0x01};
    send_command_raw_(cmd, sizeof(cmd));
    ESP_LOGI(TAG_LD2410, "Restart command sent");
  }

  void query_params(uart::UARTDevice *uart) {
    uart_ = uart;
    enter_config_mode_();
    send_config_command_(0x0000, nullptr, 0);
    send_config_command_(0x0061, nullptr, 0);
    exit_config_mode_();
    ESP_LOGI(TAG_LD2410, "Parameter query sent");
  }

 protected:
  static const int MAX_BUF = 256;
  uint8_t buf_[MAX_BUF]{};
  int buf_pos_{0};
  uart::UARTDevice *uart_{nullptr};

  static uint16_t to_uint16_(uint8_t lo, uint8_t hi) {
    return (static_cast<uint16_t>(hi) << 8) | lo;
  }

  void parse_data_frame_(const uint8_t *buf, int len) {
    // Minimum valid data frame: header(4) + len(2) + type(1) + head(1) + state(1) + ... + footer(4)
    if (len < 13)
      return;

    uint8_t data_type = buf[6];
    uint8_t head_byte = buf[7];

    // data_type 0x02 = engineering mode, 0x01 = basic mode
    // head_byte 0xAA = valid target data

    if (head_byte != 0xAA)
      return;

    uint8_t target_state = buf[8];

    bool has_moving = (target_state == 0x01 || target_state == 0x03);
    bool has_still = (target_state == 0x02 || target_state == 0x03);
    bool has_target = (target_state != 0x00);

    // Moving target data: distance at buf[9-10], energy at buf[11]
    uint16_t move_dist = to_uint16_(buf[9], buf[10]);
    uint8_t move_energy_val = buf[11];

    // Still target data: distance at buf[12-13], energy at buf[14]
    uint16_t still_dist = to_uint16_(buf[12], buf[13]);
    uint8_t still_energy_val = buf[14];

    // Detection distance at buf[15-16]
    uint16_t detect_dist = to_uint16_(buf[15], buf[16]);

    // Publish common binary sensors
    if (presence_sensor != nullptr) presence_sensor->publish_state(has_target);
    if (moving_target_sensor != nullptr) moving_target_sensor->publish_state(has_moving);
    if (still_target_sensor != nullptr) still_target_sensor->publish_state(has_still);

    // Publish distance/energy sensors
    publish_sensor_(moving_distance, static_cast<float>(move_dist));
    publish_sensor_(still_distance, static_cast<float>(still_dist));
    publish_sensor_(moving_energy, static_cast<float>(move_energy_val));
    publish_sensor_(still_energy, static_cast<float>(still_energy_val));
    publish_sensor_(detection_distance, static_cast<float>(detect_dist));

    // Engineering mode data (data_type == 0x02)
    if (data_type == 0x02 && len >= 17 + 2 * NUM_GATES + 2 * NUM_GATES) {
      // After detection distance (byte 16), engineering data starts:
      // max_move_gate(1), max_still_gate(1),
      // per-gate move energy (NUM_GATES bytes), per-gate still energy (NUM_GATES bytes),
      // light value(1)
      int offset = 17;
      // max_move_gate = buf[offset], max_still_gate = buf[offset+1]
      offset += 2;

      // Per-gate moving energy
      for (int g = 0; g < NUM_GATES && (offset + g) < len - 4; g++) {
        publish_sensor_(gate_move_energy[g], static_cast<float>(buf[offset + g]));
      }
      offset += NUM_GATES;

      // Per-gate still energy
      for (int g = 0; g < NUM_GATES && (offset + g) < len - 4; g++) {
        publish_sensor_(gate_still_energy[g], static_cast<float>(buf[offset + g]));
      }
      offset += NUM_GATES;

      // Light sensor value
      if (offset < len - 4) {
        publish_sensor_(light_sensor, static_cast<float>(buf[offset]));
      }
    }
  }

  void handle_ack_frame_(const uint8_t *buf, int len) {
    if (len < 10)
      return;

    uint16_t cmd_word = to_uint16_(buf[6], buf[7]);
    uint8_t status = buf[8];

    // Firmware version response (cmd 0x0000)
    if (cmd_word == 0x0100 && len >= 18) {
      ESP_LOGI(TAG_LD2410, "Firmware version ACK received (status: %d)", status);
    }

    // Read parameter response (cmd 0x0061)
    if (cmd_word == 0x0161 && len >= 28) {
      ESP_LOGI(TAG_LD2410, "Parameter query ACK received");
    }
  }

  void enter_config_mode_() {
    static const uint8_t cmd[] = {
      0xFD, 0xFC, 0xFB, 0xFA,
      0x04, 0x00, 0xFF, 0x00, 0x01, 0x00,
      0x04, 0x03, 0x02, 0x01
    };
    if (uart_ != nullptr)
      uart_->write_array(cmd, sizeof(cmd));
  }

  void exit_config_mode_() {
    static const uint8_t cmd[] = {
      0xFD, 0xFC, 0xFB, 0xFA,
      0x02, 0x00, 0xFE, 0x00,
      0x04, 0x03, 0x02, 0x01
    };
    if (uart_ != nullptr)
      uart_->write_array(cmd, sizeof(cmd));
  }

  void send_config_command_(uint16_t command, const uint8_t *data, size_t data_len) {
    if (uart_ == nullptr)
      return;

    uint16_t payload_len = 2 + data_len;

    uint8_t frame[64];
    int pos = 0;

    frame[pos++] = 0xFD;
    frame[pos++] = 0xFC;
    frame[pos++] = 0xFB;
    frame[pos++] = 0xFA;

    frame[pos++] = payload_len & 0xFF;
    frame[pos++] = (payload_len >> 8) & 0xFF;

    frame[pos++] = command & 0xFF;
    frame[pos++] = (command >> 8) & 0xFF;

    if (data != nullptr && data_len > 0) {
      memcpy(frame + pos, data, data_len);
      pos += data_len;
    }

    frame[pos++] = 0x04;
    frame[pos++] = 0x03;
    frame[pos++] = 0x02;
    frame[pos++] = 0x01;

    uart_->write_array(frame, pos);
  }

  void send_command_raw_(const uint8_t *cmd, size_t len) {
    if (uart_ == nullptr)
      return;

    static const uint8_t header[] = {0xFD, 0xFC, 0xFB, 0xFA};
    uart_->write_array(header, sizeof(header));
    uart_->write_array(cmd, len);
  }

  static void publish_sensor_(sensor::Sensor *s, float val) {
    if (s != nullptr)
      s->publish_state(val);
  }
};

}  // namespace satellite1_radar
}  // namespace esphome
