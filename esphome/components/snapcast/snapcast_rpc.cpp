/*
 * This file is part of Snapcast integration for ESPHome.
 *
 * Copyright (C) 2025 Mischa Siekmann <FutureProofHomes Inc.>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "snapcast_rpc.h"
#include "esp_transport_tcp.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"




namespace esphome {
namespace snapcast {

static const char *const TAG = "snapcast_rpc";


enum RPCTaskBits : uint32_t {
  DISCONNECT_BIT = (1 << 0),
  RECONNECT_BIT = (1 << 1),
};

esp_err_t SnapcastControlSession::connect(std::string server, uint32_t port) {
  this->server_ = server;
  this->port_ = port;

  if (!rpc_queue_) {
    rpc_queue_ = xQueueCreate(4, sizeof(RpcRequest*));
  }
  
  // Start the notification handling task
  this->notification_task_should_run_ = true;
  if (this->notification_task_handle_ == nullptr) {
    xTaskCreate(
        [](void *param) {
          auto *session = static_cast<SnapcastControlSession *>(param);
          session->notification_loop();
          session->notification_task_handle_ = nullptr;
          vTaskDelete(nullptr);
        },
        "snapcast_notify", 4096, this, 5, &this->notification_task_handle_);
  } else {
    xTaskNotify(this->notification_task_handle_, RECONNECT_BIT, eSetBits);
  }
  return ESP_OK;
}

esp_err_t SnapcastControlSession::disconnect() {
  this->notification_task_should_run_ = false;
  if (this->notification_task_handle_) {
    xTaskNotify(this->notification_task_handle_, DISCONNECT_BIT, eSetBits);
  }
  return ESP_OK;
}

bool SnapcastControlSession::send_rpc_async(
    const std::string &method,
    std::function<void(JsonObject)> fill_params,
    RpcResponseCb on_response,
    uint32_t timeout_ms)
{
  if (!this->rpc_queue_) return false;

  auto *req = new RpcRequest{
      .method = method,
      .id = next_id_.fetch_add(1, std::memory_order_relaxed),
      .fill_params = std::move(fill_params),
      .on_response = std::move(on_response),
      .timeout_ms = timeout_ms,
  };

  if (xQueueSend(rpc_queue_, &req, 0) != pdTRUE) {
    delete req;
    return false;
  }
  return true;
}

void SnapcastControlSession::drain_rpc_queue_() {
  RpcRequest *req = nullptr;

  while (xQueueReceive(rpc_queue_, &req, 0) == pdTRUE) {
    if (!this->transport_) {  // not connected
      delete req;
      continue;
    }

    // Build JSON-RPC
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = req->id;
    doc["method"] = req->method;
    JsonObject params = doc["params"].to<JsonObject>();
    if (req->fill_params) req->fill_params(params);

    // Serialize
    static char buf[768];  // adjust to your message sizes
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n + 1 < sizeof(buf)) buf[n++] = '\n';

    esp_transport_write(transport_, buf, n, 1000);

    // Register callback
    if (req->on_response) {
      pending_[req->id] = PendingCb{
          .cb = std::move(req->on_response),
          .expires_ms = millis() + req->timeout_ms,
      };
    }

    delete req;
  }
}

void SnapcastControlSession::expire_pending_() {
  uint32_t now = millis();
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (now >= it->second.expires_ms) {
      // just drop
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

void SnapcastControlSession::update_from_server_obj_(const JsonObject &server_obj) {
  ClientState new_state;
  {
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    new_state = this->client_state_;
    xSemaphoreGive(state_mutex_);
  }
  bool found = new_state.from_groups_json(server_obj["groups"], this->client_id_);
  
#if 1 //SNAPCAST_DEBUG
  if (found) {
    printf("group_id: %s stream_id: %s\n", new_state.group_id.c_str(), new_state.stream_id.c_str());
    printf("Group members (%zu):\n", new_state.group_members.size());
    for (const auto& id : new_state.group_members) {
      printf("  member: %s\n", id.c_str());
    }
  }
#endif
  
ClientState &state = new_state;
  const char *curr_stream = state.stream_id.c_str();
  StreamInfo *curr_sInfo = nullptr;
  JsonArray streams = server_obj["streams"];
  known_streams_.clear();
  for (JsonObject stream_obj : streams) {
    const char *stream_id = stream_obj["id"].as<const char *>();
    if (!stream_id)
      continue;
    StreamInfo &sInfo = this->known_streams_[stream_id];
    sInfo.from_json(stream_obj);
    if (strcmp(curr_stream, stream_id) == 0) {
      curr_sInfo = &sInfo;
    }
  }
  
  {
    xSemaphoreTake(state_mutex_, portMAX_DELAY);
    this->client_state_ = std::move(new_state);
    xSemaphoreGive(state_mutex_);
  }

  if (!curr_sInfo || !this->on_stream_update_) {
    return;
  }
  StreamInfo &sInfo = *curr_sInfo;
  if (sInfo.status != StreamStatus::UNKNOWN) {
    this->on_stream_update_(sInfo.status, sInfo.id);
  }
}

void SnapcastControlSession::notification_loop() {
  this->line_buffer_.reserve(1024);
  while (this->notification_task_should_run_) {
    // Initialize transport
    if (this->transport_ != nullptr) {
      esp_transport_close(this->transport_);
      esp_transport_destroy(this->transport_);
    }
    this->transport_ = esp_transport_tcp_init();

    if (this->transport_ == nullptr) {
      ESP_LOGE(TAG, "Failed to initialize transport");
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }
    esp_transport_keep_alive_t keep_alive_config = {
        .keep_alive_enable = true,
        .keep_alive_idle = 10000,     // 10 seconds
        .keep_alive_interval = 5000,  // 5 seconds
        .keep_alive_count = 5,        // Number of keep-alive probes to send before considering the connection dead
    };
    esp_transport_tcp_set_keep_alive(this->transport_, &keep_alive_config);
    // Try to connect
    error_t err = esp_transport_connect(this->transport_, this->server_.c_str(), this->port_, -1);
    if (err != 0) {
      ESP_LOGE(TAG, "Connection failed with error: %d", errno);
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }

    this->request_server_info_();

    this->known_streams_.clear();
    this->recv_buffer_.clear();
    this->line_buffer_.clear();
    bool skipping_oversize_ = false;
    size_t dropped_bytes_ = 0;
    static constexpr size_t MAX_LINE = 16 * 1024;
    while (true) {
      uint32_t notify_value = 0;
      if (xTaskNotifyWait(0, RECONNECT_BIT, &notify_value, 0) > 0) {
        break;
      }

      drain_rpc_queue_();
      expire_pending_();
      
      char chunk[128];  // small read buffer
      int len = esp_transport_read(this->transport_, chunk, sizeof(chunk), 100);
      if (len < 0) {
        if (errno == EAGAIN || errno == ETIMEDOUT) {
          continue;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        break;
      }
      if (len > 0) {
        if (skipping_oversize_) {
          for (int i = 0; i < len; i++) {
            if (chunk[i] == '\n') {
              skipping_oversize_ = false;
              dropped_bytes_ = 0;
              // resume after newline: anything after it belongs to next message
              if (i + 1 < len) {
                recv_buffer_.append(&chunk[i + 1], len - (i + 1));
              }
              break;
            } else {
              dropped_bytes_++;
            }
          }
        } else {
          // Normal mode: append
          recv_buffer_.append(chunk, len);
        }

        if (!skipping_oversize_ && recv_buffer_.size() > MAX_LINE) {
          ESP_LOGW(TAG, "Oversized JSON line (%u+ bytes). Skipping until newline.", (unsigned) recv_buffer_.size());
          recv_buffer_.clear();
          skipping_oversize_ = true;
          dropped_bytes_ = 0;
          // don't parse anything this iteration
          continue;
        }
        if (!skipping_oversize_) {
          size_t pos;
          while ((pos = this->recv_buffer_.find('\n')) != std::string::npos) {
            this->line_buffer_.assign(this->recv_buffer_, 0, pos);

#if 1 //SNAPCAST_DEBUG
            printf("JSON: %s\n", this->line_buffer_.c_str());
#endif
            this->recv_buffer_.erase(0, pos + 1);

            json::parse_json(this->line_buffer_, [this](JsonObject root) -> bool {
              if (root["id"].is<uint32_t>()) {
                uint32_t id = root["id"].as<uint32_t>();
                auto it = pending_.find(id);
                if (it != pending_.end()) {
                  auto cb = std::move(it->second.cb);
                  pending_.erase(it);
                  if (root["result"].is<JsonObject>()){
                    cb(root);
                  } else if (root["error"].is<JsonObject>()) { 
                    JsonObject err = root["error"];
                    int code = err["code"] | 0;
                    const char* msg = err["message"] | "<no message>";
                    ESP_LOGW(TAG, "RPC error id=%u code=%d message=\"%s\"", id, code, msg);  
                  }
                }
              } else {
                const char *method = root["method"].as<const char *>();
                if (!method)
                  return false;
                if (strcmp(method, "Server.OnUpdate") == 0) {
                  this->update_from_server_obj_(root["params"]["server"].as<JsonObject>());
#if 0  // leave processing of stream properties for later              
                } else if (strcmp(method, "Stream.OnProperties") == 0) {
                  JsonObject params = root["params"];
                  const char *stream_id = params["id"].as<const char *>();
                  if (!stream_id)
                    return false;
                  if (strcmp(stream_id, this->client_state_.stream_id.c_str()) == 0) {
                    StreamInfo &sInfo = this->known_streams_[this->client_state_.stream_id];
                    sInfo.from_stream_properties(params["properties"]);
                    if (this->on_stream_update_ && sInfo.status != StreamStatus::UNKNOWN) {
                      this->on_stream_update_(sInfo.status, sInfo.id);
                    }
                  }
#endif
                } else if (strcmp(method, "Group.OnStreamChanged") == 0) {
                  JsonObject params = root["params"];
                  const char *group_id = params["id"].as<const char *>();
                  if (!group_id)
                    return false;
                  if (strcmp(group_id, this->client_state_.group_id.c_str()) == 0) {
                    const char *stream_id = params["stream_id"].as<const char *>();
                    if (!stream_id)
                      return false;
                    if (strcmp(stream_id, this->client_state_.stream_id.c_str()) != 0) {
                      this->client_state_.stream_id = stream_id;
                      StreamInfo &sInfo = this->known_streams_[this->client_state_.stream_id];
                      if (sInfo.id.empty()) {
                        sInfo.set_id(this->client_state_.stream_id);
                        request_server_info_();
                      } else if (this->on_stream_update_ && sInfo.status != StreamStatus::UNKNOWN) {
                        this->on_stream_update_(sInfo.status, sInfo.id);
                      }
                    }
                  }
                } else if (strcmp(method, "Stream.OnUpdate") == 0) {
                  JsonObject params = root["params"];
                  const char *stream_id = params["id"].as<const char *>();
                  if (!stream_id)
                    return false;
                  if (strcmp(stream_id, this->client_state_.stream_id.c_str()) == 0) {
                    StreamInfo &sInfo = this->known_streams_[stream_id];
                    sInfo.from_json(params["stream"]);
                    if (this->on_stream_update_ && sInfo.status != StreamStatus::UNKNOWN) {
                      this->on_stream_update_(sInfo.status, sInfo.id);
                    }
                  }
                }
              }
              return true;
            });
          }
        }
      }
    }
  }

  if (this->transport_ != nullptr) {
    esp_transport_close(this->transport_);
    esp_transport_destroy(this->transport_);
    this->transport_ = nullptr;
  }
}

void SnapcastControlSession::request_server_info_(std::function<void(const ClientState& state)> cb){
  this->send_rpc_async(
      "Server.GetStatus",
      [](JsonObject params) {
        // no params
      },
      [this, cb](JsonObject root){
        this->update_from_server_obj_(root["result"]["server"].as<JsonObject>());
        if (cb) {
          cb(this->client_state_);
        }
      },
      2000
    );
}

void SnapcastControlSession::set_group_stream_(const std::string &stream_id){
  this->send_rpc_async(
      "Group.SetStream",
      [this, stream_id](JsonObject params) {
        params["id"] = this->client_state_.group_id;
        params["stream_id"] = stream_id;
      },
      {},
      2000
  );
}

// void SnapcastControlSession::group_request_stop_streaming_(){
  
//   this->send_rpc_async(
//       "Group.SetStream",
//       [this, stream_id](JsonObject params) {
//         params["id"] = this->client_state_.group_id;
//         params["stream_id"] = stream_id;
//       },
//       {},
//       2000
//   );
// }

void SnapcastControlSession::isolate_client_(std::function<void(const ClientState& state)> cb){
  this->send_rpc_async(
      "Group.SetClients",
      [this](JsonObject params) {
        params["id"] = this->client_state_.group_id;
        JsonArray clients = params["clients"].to<JsonArray>();
        for (const auto& cli_id : this->client_state_.group_members) {
          if ( cli_id != this->client_id_ ){
            clients.add(cli_id.c_str());
          }            
        }
      },
      [this, cb](JsonObject root){
        this->request_server_info_(cb);
      },
      2000
  );
}


void SnapcastControlSession::request_stopping(){
  // if (this->client_state_.group_members.size() > 1){
  //   // is member of a group, isolate client first
  //   this->send_rpc_request_(
  //     "Group.SetClients",
  //       [this](JsonObject params) {
  //         params["id"] = this->client_state_.group_id;
  //         JsonArray clients = params["clients"].to<JsonArray>();
  //         for (const auto& cli_id : this->client_state_.group_members) {
  //           if ( cli_id != this->client_id_ ){
  //             clients.add(cli_id.c_str());
  //           }            
  //         }
  //       },
  //       static_cast<uint32_t>(10)
  //   );
  // }
  // StreamInfo &sInfo = this->known_streams_[this->client_state_.stream_id];
  // if (sInfo.id.empty()) return;

}


}  // namespace snapcast
}  // namespace esphome
