#pragma once

#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <cstring>
#include <cmath>
#include <vector>

namespace esphome {
namespace satellite1_radar {

static const char *const TAG_LD2410 = "satellite1_radar.ld2410";

/*
 * LD2410 protocol reference:
 *   Data frames:    header 0xF4F3F2F1 ... footer 0xF8F7F6F5
 *   Command frames: header 0xFDFCFBFA ... footer 0x04030201
 *
 * Data frame report type (byte 6):
 *   0x01 = engineering mode (includes per-gate energy after basic data)
 *   0x02 = basic/normal target detection
 *
 * Target state (byte 8):
 *   0x00 = no target, 0x01 = moving, 0x02 = still, 0x03 = both
 *
 * Commands must be sent one at a time, waiting for the ACK response
 * before sending the next. This handler uses a command queue processed
 * in loop() to enforce this sequencing.
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

  // Text sensors
  text_sensor::TextSensor *version_text_sensor{nullptr};

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

      if (is_data && buf_pos_ >= 8) {
        uint16_t data_len = to_uint16_(buf_[4], buf_[5]);
        int frame_len = 4 + 2 + data_len + 4;

        if (buf_pos_ >= frame_len) {
          if (buf_[frame_len - 4] == 0xF8 && buf_[frame_len - 3] == 0xF7 &&
              buf_[frame_len - 2] == 0xF6 && buf_[frame_len - 1] == 0xF5) {
            parse_data_frame_(buf_, frame_len);
          }
          buf_pos_ = 0;
          continue;
        }
      }

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

      if (buf_pos_ >= MAX_BUF - 1) {
        buf_pos_ = 0;
      }
    }

    process_queue_();
  }

  void factory_reset(uart::UARTDevice *uart) {
    uart_ = uart;
    queue_enter_config_();
    queue_config_command_(0x00A2, nullptr, 0);
    queue_exit_config_();
    ESP_LOGI(TAG_LD2410, "Factory reset queued");
  }

  void restart(uart::UARTDevice *uart) {
    uart_ = uart;
    queue_enter_config_();
    queue_config_command_(0x00A3, nullptr, 0);
    queue_exit_config_();
    ESP_LOGI(TAG_LD2410, "Restart queued");
  }

  void query_params(uart::UARTDevice *uart) {
    uart_ = uart;
    queue_enter_config_();
    queue_config_command_(0x00A0, nullptr, 0);
    queue_config_command_(0x0061, nullptr, 0);
    queue_config_command_(0x00AB, nullptr, 0);
    queue_exit_config_();
    ESP_LOGI(TAG_LD2410, "Parameter query queued");
  }

  void write_gate_config(uart::UARTDevice *uart) {
    uart_ = uart;
    queue_enter_config_();

    uint32_t max_move = static_cast<uint32_t>(get_number_val_(max_move_distance_gate, 8.0f));
    uint32_t max_still = static_cast<uint32_t>(get_number_val_(max_still_distance_gate, 8.0f));
    uint32_t tout = static_cast<uint32_t>(get_number_val_(timeout_number, 0.0f));

    uint8_t d60[18];
    d60[0] = 0x00; d60[1] = 0x00;
    put_uint32_le_(d60 + 2, max_move);
    d60[6] = 0x01; d60[7] = 0x00;
    put_uint32_le_(d60 + 8, max_still);
    d60[12] = 0x02; d60[13] = 0x00;
    put_uint32_le_(d60 + 14, tout);
    queue_config_command_(0x0060, d60, 18);

    for (int g = 0; g < NUM_GATES; g++) {
      uint32_t mv = static_cast<uint32_t>(get_number_val_(gate_move_threshold[g], 50.0f));
      uint32_t sv = static_cast<uint32_t>(get_number_val_(gate_still_threshold[g], 50.0f));
      uint8_t d64[10];
      put_uint16_le_(d64, static_cast<uint16_t>(g));
      put_uint32_le_(d64 + 2, mv);
      put_uint32_le_(d64 + 6, sv);
      queue_config_command_(0x0064, d64, 10);
    }

    queue_exit_config_();
    ESP_LOGI(TAG_LD2410, "Gate configuration queued");
  }

  void set_distance_resolution(uart::UARTDevice *uart, bool fine) {
    uart_ = uart;
    queue_enter_config_();
    uint8_t data[2];
    put_uint16_le_(data, fine ? 0x0001 : 0x0000);
    queue_config_command_(0x00AA, data, 2);
    queue_exit_config_();
    if (distance_resolution_select != nullptr)
      distance_resolution_select->publish_state(fine ? "0.2m" : "0.75m");
    ESP_LOGI(TAG_LD2410, "Distance resolution change queued (%s)", fine ? "0.2m" : "0.75m");
  }

  void enable_engineering_mode(uart::UARTDevice *uart) {
    uart_ = uart;
    queue_enter_config_();
    queue_config_command_(0x0062, nullptr, 0);
    queue_exit_config_();
    ESP_LOGI(TAG_LD2410, "Engineering mode enable queued");
  }

  void disable_engineering_mode(uart::UARTDevice *uart) {
    uart_ = uart;
    queue_enter_config_();
    queue_config_command_(0x0063, nullptr, 0);
    queue_exit_config_();
    ESP_LOGI(TAG_LD2410, "Engineering mode disable queued");
  }

 protected:
  static const int MAX_BUF = 256;
  uint8_t buf_[MAX_BUF]{};
  int buf_pos_{0};
  uart::UARTDevice *uart_{nullptr};

  static const int MAX_CMD_FRAME = 64;
  struct QueuedCmd {
    uint8_t frame[MAX_CMD_FRAME];
    int len;
  };
  std::vector<QueuedCmd> cmd_queue_;
  bool waiting_for_ack_{false};
  uint32_t ack_wait_start_{0};
  static const uint32_t ACK_TIMEOUT_MS = 1000;

  static uint16_t to_uint16_(uint8_t lo, uint8_t hi) {
    return (static_cast<uint16_t>(hi) << 8) | lo;
  }

  void process_queue_() {
    if (cmd_queue_.empty())
      return;

    if (waiting_for_ack_) {
      if (millis() - ack_wait_start_ > ACK_TIMEOUT_MS) {
        ESP_LOGW(TAG_LD2410, "Command ACK timeout, skipping");
        waiting_for_ack_ = false;
        cmd_queue_.erase(cmd_queue_.begin());
      }
      return;
    }

    auto &cmd = cmd_queue_.front();
    if (uart_ != nullptr)
      uart_->write_array(cmd.frame, cmd.len);
    waiting_for_ack_ = true;
    ack_wait_start_ = millis();
  }

  void on_ack_received_() {
    if (!waiting_for_ack_)
      return;
    waiting_for_ack_ = false;
    if (!cmd_queue_.empty())
      cmd_queue_.erase(cmd_queue_.begin());
  }

  void queue_enter_config_() {
    QueuedCmd cmd{};
    static const uint8_t frame[] = {
      0xFD, 0xFC, 0xFB, 0xFA,
      0x04, 0x00, 0xFF, 0x00, 0x01, 0x00,
      0x04, 0x03, 0x02, 0x01
    };
    memcpy(cmd.frame, frame, sizeof(frame));
    cmd.len = sizeof(frame);
    cmd_queue_.push_back(cmd);
  }

  void queue_exit_config_() {
    QueuedCmd cmd{};
    static const uint8_t frame[] = {
      0xFD, 0xFC, 0xFB, 0xFA,
      0x02, 0x00, 0xFE, 0x00,
      0x04, 0x03, 0x02, 0x01
    };
    memcpy(cmd.frame, frame, sizeof(frame));
    cmd.len = sizeof(frame);
    cmd_queue_.push_back(cmd);
  }

  void queue_config_command_(uint16_t command, const uint8_t *data, size_t data_len) {
    QueuedCmd cmd{};
    int pos = 0;

    cmd.frame[pos++] = 0xFD;
    cmd.frame[pos++] = 0xFC;
    cmd.frame[pos++] = 0xFB;
    cmd.frame[pos++] = 0xFA;

    uint16_t payload_len = 2 + data_len;
    cmd.frame[pos++] = payload_len & 0xFF;
    cmd.frame[pos++] = (payload_len >> 8) & 0xFF;

    cmd.frame[pos++] = command & 0xFF;
    cmd.frame[pos++] = (command >> 8) & 0xFF;

    if (data != nullptr && data_len > 0 && (pos + data_len) < MAX_CMD_FRAME - 4) {
      memcpy(cmd.frame + pos, data, data_len);
      pos += data_len;
    }

    cmd.frame[pos++] = 0x04;
    cmd.frame[pos++] = 0x03;
    cmd.frame[pos++] = 0x02;
    cmd.frame[pos++] = 0x01;

    cmd.len = pos;
    cmd_queue_.push_back(cmd);
  }

  void parse_data_frame_(const uint8_t *buf, int len) {
    if (len < 13)
      return;

    uint8_t data_type = buf[6];
    uint8_t head_byte = buf[7];

    if (head_byte != 0xAA)
      return;

    uint8_t target_state = buf[8];

    bool has_moving = (target_state == 0x01 || target_state == 0x03);
    bool has_still = (target_state == 0x02 || target_state == 0x03);
    bool has_target = (target_state != 0x00);

    uint16_t move_dist = to_uint16_(buf[9], buf[10]);
    uint8_t move_energy_val = buf[11];

    uint16_t still_dist = to_uint16_(buf[12], buf[13]);
    uint8_t still_energy_val = buf[14];

    uint16_t detect_dist = to_uint16_(buf[15], buf[16]);

    if (presence_sensor != nullptr) presence_sensor->publish_state(has_target);
    if (moving_target_sensor != nullptr) moving_target_sensor->publish_state(has_moving);
    if (still_target_sensor != nullptr) still_target_sensor->publish_state(has_still);

    publish_sensor_(moving_distance, static_cast<float>(move_dist));
    publish_sensor_(still_distance, static_cast<float>(still_dist));
    publish_sensor_(moving_energy, static_cast<float>(move_energy_val));
    publish_sensor_(still_energy, static_cast<float>(still_energy_val));
    publish_sensor_(detection_distance, static_cast<float>(detect_dist));

    // Engineering mode: per-gate energy starts at buf[17]
    //   buf[17] = max_move_gate, buf[18] = max_still_gate,
    //   buf[19..27] = gate move energy (9 bytes),
    //   buf[28..36] = gate still energy (9 bytes),
    //   buf[37]     = light value
    if (data_type == 0x01 && len >= 19 + 2 * NUM_GATES + 4) {
      int offset = 19;

      for (int g = 0; g < NUM_GATES && (offset + g) < len - 4; g++) {
        publish_sensor_(gate_move_energy[g], static_cast<float>(buf[offset + g]));
      }
      offset += NUM_GATES;

      for (int g = 0; g < NUM_GATES && (offset + g) < len - 4; g++) {
        publish_sensor_(gate_still_energy[g], static_cast<float>(buf[offset + g]));
      }
      offset += NUM_GATES;

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

    if (status != 0) {
      ESP_LOGW(TAG_LD2410, "Command 0x%04X failed (status=%u)", cmd_word, status);
    }

    // Firmware version response (cmd 0x00A0 -> ACK 0x01A0)
    if (cmd_word == 0x01A0 && status == 0 && len >= 18) {
      char ver[32];
      snprintf(ver, sizeof(ver), "V%u.%02X.%02X%02X%02X%02X",
               buf[13], buf[12], buf[17], buf[16], buf[15], buf[14]);
      if (version_text_sensor != nullptr)
        version_text_sensor->publish_state(ver);
      ESP_LOGI(TAG_LD2410, "Firmware version: %s", ver);
    }

    // Read parameter response (cmd 0x0061 -> ACK 0x0161)
    if (cmd_word == 0x0161 && status == 0 && len >= 32) {
      uint8_t max_move = buf[10];
      uint8_t max_still = buf[11];

      publish_number_(max_move_distance_gate, static_cast<float>(max_move));
      publish_number_(max_still_distance_gate, static_cast<float>(max_still));

      for (int g = 0; g < NUM_GATES && (12 + g) < len - 4; g++) {
        publish_number_(gate_move_threshold[g], static_cast<float>(buf[12 + g]));
      }
      for (int g = 0; g < NUM_GATES && (21 + g) < len - 4; g++) {
        publish_number_(gate_still_threshold[g], static_cast<float>(buf[21 + g]));
      }

      if (30 < len - 5) {
        uint16_t timeout = to_uint16_(buf[30], buf[31]);
        publish_number_(timeout_number, static_cast<float>(timeout));
      }

      ESP_LOGI(TAG_LD2410, "Parameters: max_move=%u max_still=%u", max_move, max_still);
    }

    // Engineering mode ON ACK (cmd 0x0062 -> ACK 0x0162)
    if (cmd_word == 0x0162) {
      if (status == 0) {
        if (engineering_mode_switch != nullptr)
          engineering_mode_switch->publish_state(true);
        ESP_LOGI(TAG_LD2410, "Engineering mode enabled");
      } else {
        ESP_LOGW(TAG_LD2410, "Failed to enable engineering mode");
      }
    }

    // Engineering mode OFF ACK (cmd 0x0063 -> ACK 0x0163)
    if (cmd_word == 0x0163) {
      if (status == 0) {
        if (engineering_mode_switch != nullptr)
          engineering_mode_switch->publish_state(false);
        ESP_LOGI(TAG_LD2410, "Engineering mode disabled");
      }
    }

    // Distance resolution query response (cmd 0x00AB -> ACK 0x01AB)
    if (cmd_word == 0x01AB && status == 0 && len >= 12) {
      uint16_t res = to_uint16_(buf[10], buf[11]);
      bool fine = (res == 0x0001);
      if (distance_resolution_select != nullptr)
        distance_resolution_select->publish_state(fine ? "0.2m" : "0.75m");
      ESP_LOGI(TAG_LD2410, "Distance resolution: %s", fine ? "0.2m" : "0.75m");
    }

    on_ack_received_();
  }

  static void publish_sensor_(sensor::Sensor *s, float val) {
    if (s != nullptr)
      s->publish_state(val);
  }

  static void publish_number_(number::Number *n, float val) {
    if (n == nullptr)
      return;
    auto call = n->make_call();
    call.set_value(val);
    call.perform();
  }

  static float get_number_val_(number::Number *n, float fallback) {
    if (n == nullptr) return fallback;
    float v = n->state;
    return std::isnan(v) ? fallback : v;
  }

  static void put_uint16_le_(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
  }

  static void put_uint32_le_(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
  }
};

}  // namespace satellite1_radar
}  // namespace esphome
