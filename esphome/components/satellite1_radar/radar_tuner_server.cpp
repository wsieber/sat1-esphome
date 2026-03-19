#ifdef USE_ESP_IDF

#include "radar_tuner_server.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <esp_http_server.h>

namespace esphome {
namespace satellite1_radar {

static const char *const TAG_RT = "radar_tuner_server";

void RadarTunerServer::register_number(const std::string &object_id, number::Number *n) {
  if (n != nullptr)
    numbers_[object_id] = n;
}

number::Number *RadarTunerServer::find_number_(const std::string &id) {
  auto it = numbers_.find(id);
  return (it != numbers_.end()) ? it->second : nullptr;
}

void RadarTunerServer::register_switch(const std::string &object_id, switch_::Switch *s) {
  if (s != nullptr)
    switches_[object_id] = s;
}

switch_::Switch *RadarTunerServer::find_switch_(const std::string &id) {
  auto it = switches_.find(id);
  return (it != switches_.end()) ? it->second : nullptr;
}

void RadarTunerServer::register_select(const std::string &object_id, select::Select *s) {
  if (s != nullptr)
    selects_[object_id] = s;
}

select::Select *RadarTunerServer::find_select_(const std::string &id) {
  auto it = selects_.find(id);
  return (it != selects_.end()) ? it->second : nullptr;
}

void RadarTunerServer::update_target(int index, float x, float y) {
  if (index >= 0 && index < RT_NUM_TARGETS) {
    targets_[index].x = x;
    targets_[index].y = y;
  }
}

void RadarTunerServer::set_gate_sensors(sensor::Sensor **move, sensor::Sensor **still) {
  for (int i = 0; i < RT_NUM_GATES; i++) {
    gate_move_energy_[i] = move ? move[i] : nullptr;
    gate_still_energy_[i] = still ? still[i] : nullptr;
  }
}

void RadarTunerServer::clear_registrations() {
  numbers_.clear();
  switches_.clear();
  selects_.clear();
  on_write_config_ = nullptr;
  for (int i = 0; i < RT_NUM_GATES; i++) {
    gate_move_energy_[i] = nullptr;
    gate_still_energy_[i] = nullptr;
  }
}

static void set_common_headers_(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");
}

esp_err_t RadarTunerServer::handle_root_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  if (self->html_gz_ == nullptr || self->html_gz_len_ == 0) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No HTML content");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, reinterpret_cast<const char *>(self->html_gz_),
                         self->html_gz_len_);
}

esp_err_t RadarTunerServer::handle_number_get_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  std::string uri(req->uri);

  const char *prefix = "/number/";
  size_t prefix_len = strlen(prefix);
  if (uri.length() <= prefix_len) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Missing entity id");
    return ESP_FAIL;
  }
  std::string object_id = uri.substr(prefix_len);
  auto qpos = object_id.find('?');
  if (qpos != std::string::npos)
    object_id = object_id.substr(0, qpos);

  auto *n = self->find_number_(object_id);
  if (n == nullptr) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Entity not found");
    return ESP_FAIL;
  }

  char buf[128];
  float val = n->state;
  if (std::isnan(val)) val = 0.0f;
  snprintf(buf, sizeof(buf), "{\"id\":\"number-%s\",\"value\":%.1f,\"state\":\"%.1f\"}",
           object_id.c_str(), val, val);

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_send(req, buf, strlen(buf));
}

esp_err_t RadarTunerServer::handle_number_set_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  std::string uri(req->uri);

  const char *prefix = "/number/";
  size_t prefix_len = strlen(prefix);
  if (uri.length() <= prefix_len) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Missing entity id");
    return ESP_FAIL;
  }

  std::string tail = uri.substr(prefix_len);
  auto slash = tail.find("/set");
  if (slash == std::string::npos) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
    return ESP_FAIL;
  }
  std::string object_id = tail.substr(0, slash);

  auto *n = self->find_number_(object_id);
  if (n == nullptr) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Entity not found");
    return ESP_FAIL;
  }

  auto qpos = uri.find("value=");
  if (qpos == std::string::npos) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing value parameter");
    return ESP_FAIL;
  }
  float value = strtof(uri.c_str() + qpos + 6, nullptr);

  auto call = n->make_call();
  call.set_value(value);
  call.perform();

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

esp_err_t RadarTunerServer::handle_switch_get_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  std::string uri(req->uri);

  const char *prefix = "/switch/";
  size_t prefix_len = strlen(prefix);
  if (uri.length() <= prefix_len) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Missing entity id");
    return ESP_FAIL;
  }
  std::string object_id = uri.substr(prefix_len);
  auto qpos = object_id.find('?');
  if (qpos != std::string::npos)
    object_id = object_id.substr(0, qpos);

  auto *s = self->find_switch_(object_id);
  if (s == nullptr) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Entity not found");
    return ESP_FAIL;
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "{\"id\":\"switch-%s\",\"value\":%s,\"state\":\"%s\"}",
           object_id.c_str(), s->state ? "true" : "false", s->state ? "ON" : "OFF");

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_send(req, buf, strlen(buf));
}

esp_err_t RadarTunerServer::handle_switch_set_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  std::string uri(req->uri);

  const char *prefix = "/switch/";
  size_t prefix_len = strlen(prefix);
  if (uri.length() <= prefix_len) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Missing entity id");
    return ESP_FAIL;
  }

  std::string tail = uri.substr(prefix_len);
  auto slash = tail.find("/set");
  if (slash == std::string::npos) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
    return ESP_FAIL;
  }
  std::string object_id = tail.substr(0, slash);

  auto *s = self->find_switch_(object_id);
  if (s == nullptr) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Entity not found");
    return ESP_FAIL;
  }

  auto qpos = uri.find("state=");
  if (qpos == std::string::npos) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing state parameter");
    return ESP_FAIL;
  }
  std::string state_str = uri.substr(qpos + 6);
  bool new_state = (state_str == "true" || state_str == "ON" || state_str == "1");

  if (new_state) {
    s->turn_on();
  } else {
    s->turn_off();
  }

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

esp_err_t RadarTunerServer::handle_select_get_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  std::string uri(req->uri);

  const char *prefix = "/select/";
  size_t prefix_len = strlen(prefix);
  if (uri.length() <= prefix_len) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Missing entity id");
    return ESP_FAIL;
  }
  std::string object_id = uri.substr(prefix_len);

  auto *s = self->find_select_(object_id);
  if (s == nullptr) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Entity not found");
    return ESP_FAIL;
  }

  const auto &options = s->traits.get_options();
  char buf[512];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"id\":\"select-%s\",\"value\":\"%s\",\"options\":[",
                  object_id.c_str(), s->current_option().c_str());
  for (size_t i = 0; i < options.size(); i++) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s\"%s\"", i ? "," : "", options[i]);
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_send(req, buf, pos);
}

esp_err_t RadarTunerServer::handle_select_set_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  std::string uri(req->uri);

  const char *prefix = "/select/";
  size_t prefix_len = strlen(prefix);
  if (uri.length() <= prefix_len) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Missing entity id");
    return ESP_FAIL;
  }

  std::string tail = uri.substr(prefix_len);
  auto slash = tail.find("/set");
  if (slash == std::string::npos) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
    return ESP_FAIL;
  }
  std::string object_id = tail.substr(0, slash);

  auto *sel = self->find_select_(object_id);
  if (sel == nullptr) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Entity not found");
    return ESP_FAIL;
  }

  auto qpos = uri.find("option=");
  if (qpos == std::string::npos) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing option parameter");
    return ESP_FAIL;
  }
  std::string option = uri.substr(qpos + 7);

  auto call = sel->make_call();
  call.set_option(option);
  call.perform();

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

esp_err_t RadarTunerServer::handle_gates_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);

  char buf[512];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"move\":[");
  for (int g = 0; g < RT_NUM_GATES; g++) {
    float val = 0;
    if (self->gate_move_energy_[g] != nullptr) {
      val = self->gate_move_energy_[g]->state;
      if (std::isnan(val)) val = 0;
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%.0f", g ? "," : "", val);
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "],\"still\":[");
  for (int g = 0; g < RT_NUM_GATES; g++) {
    float val = 0;
    if (self->gate_still_energy_[g] != nullptr) {
      val = self->gate_still_energy_[g]->state;
      if (std::isnan(val)) val = 0;
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%.0f", g ? "," : "", val);
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_send(req, buf, pos);
}

esp_err_t RadarTunerServer::handle_write_config_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);
  if (self->on_write_config_) {
    self->on_write_config_();
  }
  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

esp_err_t RadarTunerServer::handle_save_(httpd_req_t *req) {
  global_preferences->sync();
  ESP_LOGI(TAG_RT, "Preferences flushed to NVS");
  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

esp_err_t RadarTunerServer::handle_targets_(httpd_req_t *req) {
  auto *self = static_cast<RadarTunerServer *>(req->user_ctx);

  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"targets\":["
           "{\"x\":%.1f,\"y\":%.1f},"
           "{\"x\":%.1f,\"y\":%.1f},"
           "{\"x\":%.1f,\"y\":%.1f}]}",
           self->targets_[0].x, self->targets_[0].y,
           self->targets_[1].x, self->targets_[1].y,
           self->targets_[2].x, self->targets_[2].y);

  httpd_resp_set_type(req, "application/json");
  set_common_headers_(req);
  return httpd_resp_send(req, buf, strlen(buf));
}

void RadarTunerServer::start() {
  if (server_ != nullptr) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 16;
  config.max_open_sockets = 2;
  config.backlog_conn = 5;
  config.stack_size = 8192;
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&server_, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG_RT, "Failed to start httpd: %s", esp_err_to_name(err));
    server_ = nullptr;
    return;
  }

  httpd_uri_t root = {};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = handle_root_;
  root.user_ctx = this;
  httpd_register_uri_handler(server_, &root);

  httpd_uri_t targets = {};
  targets.uri = "/targets";
  targets.method = HTTP_GET;
  targets.handler = handle_targets_;
  targets.user_ctx = this;
  httpd_register_uri_handler(server_, &targets);

  httpd_uri_t gates = {};
  gates.uri = "/gates";
  gates.method = HTTP_GET;
  gates.handler = handle_gates_;
  gates.user_ctx = this;
  httpd_register_uri_handler(server_, &gates);

  httpd_uri_t num_get = {};
  num_get.uri = "/number/*";
  num_get.method = HTTP_GET;
  num_get.handler = handle_number_get_;
  num_get.user_ctx = this;
  httpd_register_uri_handler(server_, &num_get);

  httpd_uri_t num_set = {};
  num_set.uri = "/number/*";
  num_set.method = HTTP_POST;
  num_set.handler = handle_number_set_;
  num_set.user_ctx = this;
  httpd_register_uri_handler(server_, &num_set);

  httpd_uri_t sw_get = {};
  sw_get.uri = "/switch/*";
  sw_get.method = HTTP_GET;
  sw_get.handler = handle_switch_get_;
  sw_get.user_ctx = this;
  httpd_register_uri_handler(server_, &sw_get);

  httpd_uri_t sw_set = {};
  sw_set.uri = "/switch/*";
  sw_set.method = HTTP_POST;
  sw_set.handler = handle_switch_set_;
  sw_set.user_ctx = this;
  httpd_register_uri_handler(server_, &sw_set);

  httpd_uri_t sel_get = {};
  sel_get.uri = "/select/*";
  sel_get.method = HTTP_GET;
  sel_get.handler = handle_select_get_;
  sel_get.user_ctx = this;
  httpd_register_uri_handler(server_, &sel_get);

  httpd_uri_t sel_set = {};
  sel_set.uri = "/select/*";
  sel_set.method = HTTP_POST;
  sel_set.handler = handle_select_set_;
  sel_set.user_ctx = this;
  httpd_register_uri_handler(server_, &sel_set);

  httpd_uri_t write_cfg = {};
  write_cfg.uri = "/write_config";
  write_cfg.method = HTTP_POST;
  write_cfg.handler = handle_write_config_;
  write_cfg.user_ctx = this;
  httpd_register_uri_handler(server_, &write_cfg);

  httpd_uri_t save = {};
  save.uri = "/save";
  save.method = HTTP_POST;
  save.handler = handle_save_;
  save.user_ctx = this;
  httpd_register_uri_handler(server_, &save);

  ESP_LOGI(TAG_RT, "Radar tuner server started on port 80");
}

void RadarTunerServer::stop() {
  if (server_ == nullptr) return;

  httpd_stop(server_);
  server_ = nullptr;
  ESP_LOGI(TAG_RT, "Radar tuner server stopped");
}

}  // namespace satellite1_radar
}  // namespace esphome

#endif  // USE_ESP_IDF
