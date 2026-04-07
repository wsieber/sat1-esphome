#pragma once

#include "esphome/components/improv_base/improv_base.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#ifdef USE_WIFI
#include "improv_ext.h"
#include <vector>

#ifdef USE_ARDUINO
#include <HardwareSerial.h>
#endif
#ifdef USE_ESP_IDF
#include <driver/uart.h>
#if defined(USE_ESP32_VARIANT_ESP32C3) || defined(USE_ESP32_VARIANT_ESP32C6) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32H2)
#include <driver/usb_serial_jtag.h>
#include <hal/usb_serial_jtag_ll.h>
#endif
#if defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3)
#include <esp_private/usb_console.h>
#endif
#endif



namespace esphome {
namespace improv_serial {

enum ImprovSerialType : uint8_t {
  TYPE_CURRENT_STATE = 0x01,
  TYPE_ERROR_STATE = 0x02,
  TYPE_RPC = 0x03,
  TYPE_RPC_RESPONSE = 0x04
};

static const uint16_t IMPROV_SERIAL_TIMEOUT = 100;
static const uint8_t IMPROV_SERIAL_VERSION = 1;


class ExtAction {
  public:
    ExtAction() = default;
    ExtAction(const std::string &action, const std::string &url) : action_(action), url_(url) {}
    ExtAction(const improv_ext::ImprovCommand &command) {
      this->action_ = command.ssid;
      this->url_ = command.password;
    }
    const std::string &get_action() const { return action_; }
    const optional<std::string> &get_url() const { return url_; }
  
  private:
    std::string action_;
    optional<std::string> url_;
};



class ImprovSerialComponent : public Component, public improv_base::ImprovBase {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void send_action_status(const std::string action, int status);
  
  Trigger<ExtAction> *get_action_request_trigger() {
    return this->action_request_trigger_;
  }

 protected:
  bool parse_improv_serial_byte_(uint8_t byte);
  bool parse_improv_payload_(improv_ext::ImprovCommand &command);

  void set_state_(improv_ext::State state);
  void set_error_(improv_ext::Error error);
  void send_response_(std::vector<uint8_t> &response);
  void on_wifi_connect_timeout_();

  std::vector<uint8_t> build_rpc_settings_response_(improv_ext::Command command);
  std::vector<uint8_t> build_version_info_();

  optional<uint8_t> read_byte_();
  void write_data_(std::vector<uint8_t> &data);

#ifdef USE_ARDUINO
  Stream *hw_serial_{nullptr};
#endif
#ifdef USE_ESP_IDF
  uart_port_t uart_num_;
#endif

  std::vector<uint8_t> rx_buffer_;
  uint32_t last_read_byte_{0};
  wifi::WiFiAP connecting_sta_;
  improv_ext::State state_{improv_ext::STATE_AUTHORIZED};

  Trigger<ExtAction> *action_request_trigger_ = new Trigger<ExtAction>();
};


template<typename... Ts> 
class ImprovSendActionStatusAction : public Action<Ts...> {
public:
  ImprovSendActionStatusAction(ImprovSerialComponent *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, action)
  TEMPLATABLE_VALUE(int, status )
  
  void play(Ts... x) override {
    this->parent_->send_action_status(action_.value(x...), status_.value(x...));
  }
protected:
  ImprovSerialComponent *parent_;
};



extern ImprovSerialComponent
    *global_improv_serial_component;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace improv_serial
}  // namespace esphome
#endif
