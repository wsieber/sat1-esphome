#pragma once

#include "esphome/components/microphone/microphone_source.h"
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

  /// Called from on_wake_word_detected. Returns at once; the clip is cut after the post-roll and
  /// uploaded by a background task. With hold, the cut clip waits for one of the release calls
  /// below and is dropped if none comes within HOLD_TIMEOUT. A new firing replaces a held clip.
  void capture(const std::string &wake_word, bool hold);

  /// The session's first transcript decides a held clip: nothing heard, or only a dismissal
  /// ("stop", "never mind"), sends it as a likely false trigger; anything else drops it. No-op
  /// when nothing is held, so later turns of the same conversation are ignored.
  void release_for_transcript(const std::string &text);
  /// The first speech-to-text stage failed (for example, no text recognized): send a held clip.
  void release_for_stt_error(const std::string &code);

 protected:
  struct Job {
    int16_t *pcm;
    size_t samples;
    std::string wake_word;
    std::string event_type;
    std::string notes;
  };

  enum class Decision : uint8_t { NONE, SEND, DROP };

  void on_audio_(const std::vector<uint8_t> &data);
  void decide_(Decision decision, const std::string &why);
  void send_(Job *job);
  void free_(Job *job);
  static void upload_task(void *arg);
  bool upload_(const Job &job);

  microphone::MicrophoneSource *mic_source_{nullptr};
  std::string url_;
  uint32_t pre_roll_ms_{2750};
  uint32_t post_roll_ms_{250};

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
  bool pending_hold_{false};
  std::string pending_wake_word_;

  // A cut clip waiting for the session to decide it, and that decision if it came first.
  Job *held_{nullptr};
  uint32_t held_since_ms_{0};
  Decision decision_{Decision::NONE};
  std::string decision_why_;

  QueueHandle_t jobs_{nullptr};
  uint32_t sent_{0};
  uint32_t dropped_{0};
  uint32_t failed_{0};
};

}  // namespace wake_capture
}  // namespace esphome
