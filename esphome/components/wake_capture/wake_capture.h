#pragma once

#include "esphome/components/microphone/microphone_source.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <string>

namespace esphome {
namespace wake_capture {

/// Keeps the last few seconds of the wake word engine's audio and, on a firing, sends a clip of it
/// to a microWakeWord trainer for review. See __init__.py for the whole flow.
class WakeCapture : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_microphone_source(microphone::MicrophoneSource *source) { this->mic_source_ = source; }
  void set_url(const std::string &url) { this->url_ = url; }
  void set_pre_roll_ms(uint32_t ms) { this->pre_roll_ms_ = ms; }
  void set_post_roll_ms(uint32_t ms) { this->post_roll_ms_ = ms; }
  void set_enable_switch(switch_::Switch *sw) { this->enable_switch_ = sw; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }

  /// Called from on_wake_word_detected. Returns at once; the clip is cut after the post-roll and
  /// uploaded by a background task. A firing while the previous clip is still pending is dropped.
  void capture(const std::string &wake_word, const char *event_type = "wake_detected");

 protected:
  struct Job {
    int16_t *pcm;
    size_t samples;
    std::string wake_word;
    std::string event_type;
  };

  void on_audio_(const std::vector<uint8_t> &data);
  static void upload_task(void *arg);
  bool upload_(const Job &job);

  microphone::MicrophoneSource *mic_source_{nullptr};
  switch_::Switch *enable_switch_{nullptr};
  std::string url_;
  uint32_t pre_roll_ms_{2750};
  uint32_t post_roll_ms_{250};
  bool enabled_{true};

  // Ring of the most recent samples, in PSRAM. Written from the microphone's task, read from
  // loop(), so both sides go through ring_lock_.
  Mutex ring_lock_;
  int16_t *ring_{nullptr};
  size_t ring_samples_{0};
  size_t write_pos_{0};
  uint64_t total_written_{0};

  // A firing waiting for its post-roll.
  bool pending_{false};
  uint64_t due_at_sample_{0};
  uint32_t pending_since_ms_{0};
  std::string pending_wake_word_;
  const char *pending_event_type_{""};

  QueueHandle_t jobs_{nullptr};
  uint32_t sent_{0};
  uint32_t dropped_{0};
  uint32_t failed_{0};
};

class WakeCaptureSwitch : public switch_::Switch, public Parented<WakeCapture> {
 protected:
  void write_state(bool state) override {
    this->parent_->set_enabled(state);
    this->publish_state(state);
  }
};

}  // namespace wake_capture
}  // namespace esphome
