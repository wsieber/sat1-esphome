#pragma once

#ifdef USE_ESP_IDF

#include <esp_http_server.h>
#include <string>
#include <map>
#include <functional>
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace satellite1_radar {

static const int RT_NUM_TARGETS = 3;
static const int RT_NUM_GATES = 9;

class RadarTunerServer {
 public:
  void start();
  void stop();
  bool is_running() const { return server_ != nullptr; }

  void set_html_content(const uint8_t *data, unsigned int len) {
    html_gz_ = data;
    html_gz_len_ = len;
  }

  void register_number(const std::string &object_id, number::Number *n);
  void register_switch(const std::string &object_id, switch_::Switch *s);
  void register_select(const std::string &object_id, select::Select *s);

  void set_gate_sensors(sensor::Sensor **move, sensor::Sensor **still);
  void set_write_config_callback(std::function<void()> cb) { on_write_config_ = std::move(cb); }

  void update_target(int index, float x, float y);

  void clear_registrations();

 private:
  httpd_handle_t server_{nullptr};
  std::map<std::string, number::Number *> numbers_;
  std::map<std::string, switch_::Switch *> switches_;
  std::map<std::string, select::Select *> selects_;

  const uint8_t *html_gz_{nullptr};
  unsigned int html_gz_len_{0};
  std::function<void()> on_write_config_;

  struct TargetData {
    float x{0};
    float y{0};
  };
  TargetData targets_[RT_NUM_TARGETS]{};

  sensor::Sensor *gate_move_energy_[RT_NUM_GATES]{};
  sensor::Sensor *gate_still_energy_[RT_NUM_GATES]{};

  static esp_err_t handle_root_(httpd_req_t *req);
  static esp_err_t handle_number_get_(httpd_req_t *req);
  static esp_err_t handle_number_set_(httpd_req_t *req);
  static esp_err_t handle_switch_get_(httpd_req_t *req);
  static esp_err_t handle_switch_set_(httpd_req_t *req);
  static esp_err_t handle_targets_(httpd_req_t *req);
  static esp_err_t handle_gates_(httpd_req_t *req);
  static esp_err_t handle_select_get_(httpd_req_t *req);
  static esp_err_t handle_select_set_(httpd_req_t *req);
  static esp_err_t handle_write_config_(httpd_req_t *req);
  static esp_err_t handle_save_(httpd_req_t *req);

  number::Number *find_number_(const std::string &id);
  switch_::Switch *find_switch_(const std::string &id);
  select::Select *find_select_(const std::string &id);
};

}  // namespace satellite1_radar
}  // namespace esphome

#endif  // USE_ESP_IDF
