#include "wake_capture.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <esp_http_client.h>

#include <algorithm>
#include <cstring>

namespace esphome {
namespace wake_capture {

static const char *const TAG = "wake_capture";

static constexpr uint32_t SAMPLE_RATE = 16000;
// One clip in flight at a time: a firing that lands while the last one is still uploading is
// dropped rather than queued, so a burst of false triggers cannot pile up PSRAM.
static constexpr UBaseType_t JOB_QUEUE_LENGTH = 1;
static constexpr uint32_t UPLOAD_TASK_STACK = 6144;
static constexpr UBaseType_t UPLOAD_TASK_PRIORITY = 2;
static constexpr int HTTP_TIMEOUT_MS = 10000;
// If the microphone stops before the post-roll arrives, cut the clip with what there is.
static constexpr uint32_t POST_ROLL_GRACE_MS = 1000;

void WakeCapture::setup() {
  // The ring holds the pre-roll plus the post-roll, so the clip can be cut once the post-roll is in.
  this->ring_samples_ = (size_t) (this->pre_roll_ms_ + this->post_roll_ms_) * SAMPLE_RATE / 1000;
  RAMAllocator<int16_t> allocator;
  this->ring_ = allocator.allocate(this->ring_samples_);
  if (this->ring_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate %u-sample ring", (unsigned) this->ring_samples_);
    this->mark_failed();
    return;
  }
  memset(this->ring_, 0, this->ring_samples_ * sizeof(int16_t));

  this->jobs_ = xQueueCreate(JOB_QUEUE_LENGTH, sizeof(Job *));
  if (this->jobs_ == nullptr ||
      xTaskCreate(WakeCapture::upload_task, "wake_capture", UPLOAD_TASK_STACK, this, UPLOAD_TASK_PRIORITY,
                  nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Could not start the upload task");
    this->mark_failed();
    return;
  }

  this->mic_source_->add_data_callback([this](const std::vector<uint8_t> &data) { this->on_audio_(data); });

  if (this->enable_switch_ != nullptr) {
    auto restored = this->enable_switch_->get_initial_state_with_restore_mode();
    this->enabled_ = restored.value_or(true);
    this->enable_switch_->publish_state(this->enabled_);
  }
}

void WakeCapture::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Wake Capture:\n"
                "  URL: %s\n"
                "  Pre-roll: %ums, post-roll: %ums",
                this->url_.c_str(), (unsigned) this->pre_roll_ms_, (unsigned) this->post_roll_ms_);
}

void WakeCapture::on_audio_(const std::vector<uint8_t> &data) {
  const int16_t *samples = reinterpret_cast<const int16_t *>(data.data());
  size_t count = data.size() / sizeof(int16_t);
  LockGuard guard{this->ring_lock_};
  // Only the newest ring_samples_ of an oversized chunk can survive anyway.
  if (count > this->ring_samples_) {
    samples += count - this->ring_samples_;
    this->total_written_ += count - this->ring_samples_;
    count = this->ring_samples_;
  }
  while (count > 0) {
    const size_t run = std::min(count, this->ring_samples_ - this->write_pos_);
    memcpy(this->ring_ + this->write_pos_, samples, run * sizeof(int16_t));
    this->write_pos_ = (this->write_pos_ + run) % this->ring_samples_;
    this->total_written_ += run;
    samples += run;
    count -= run;
  }
}

void WakeCapture::capture(const std::string &wake_word, const char *event_type) {
  if (!this->enabled_ || this->is_failed() || this->url_.empty())
    return;
  if (this->pending_) {
    this->dropped_++;
    ESP_LOGD(TAG, "Dropped '%s' firing: previous clip still pending", wake_word.c_str());
    return;
  }
  {
    LockGuard guard{this->ring_lock_};
    this->due_at_sample_ = this->total_written_ + (uint64_t) this->post_roll_ms_ * SAMPLE_RATE / 1000;
  }
  this->pending_ = true;
  this->pending_since_ms_ = millis();
  this->pending_wake_word_ = wake_word;
  this->pending_event_type_ = event_type;
}

void WakeCapture::loop() {
  if (!this->pending_)
    return;

  RAMAllocator<int16_t> allocator;
  Job *job = nullptr;
  {
    LockGuard guard{this->ring_lock_};
    const bool post_roll_in = this->total_written_ >= this->due_at_sample_;
    if (!post_roll_in && millis() - this->pending_since_ms_ < this->post_roll_ms_ + POST_ROLL_GRACE_MS)
      return;
    // Oldest sample first. Right after boot the ring may not be full yet; send only what is real.
    const size_t samples = (size_t) std::min<uint64_t>(this->total_written_, this->ring_samples_);
    if (samples < SAMPLE_RATE / 2) {
      this->pending_ = false;
      return;
    }
    int16_t *pcm = allocator.allocate(samples);
    if (pcm == nullptr) {
      this->pending_ = false;
      this->failed_++;
      ESP_LOGW(TAG, "No memory for a %u-sample clip", (unsigned) samples);
      return;
    }
    const size_t start = (this->write_pos_ + this->ring_samples_ - samples) % this->ring_samples_;
    const size_t first = std::min(samples, this->ring_samples_ - start);
    memcpy(pcm, this->ring_ + start, first * sizeof(int16_t));
    memcpy(pcm + first, this->ring_, (samples - first) * sizeof(int16_t));
    job = new Job{pcm, samples, this->pending_wake_word_, this->pending_event_type_};
  }
  this->pending_ = false;

  if (xQueueSend(this->jobs_, &job, 0) != pdTRUE) {
    this->dropped_++;
    ESP_LOGD(TAG, "Dropped '%s' clip: previous upload still running", job->wake_word.c_str());
    allocator.deallocate(job->pcm, job->samples);
    delete job;
  }
}

void WakeCapture::upload_task(void *arg) {
  auto *self = static_cast<WakeCapture *>(arg);
  RAMAllocator<int16_t> allocator;
  while (true) {
    Job *job = nullptr;
    if (xQueueReceive(self->jobs_, &job, portMAX_DELAY) != pdTRUE || job == nullptr)
      continue;
    if (self->upload_(*job)) {
      self->sent_++;
    } else {
      self->failed_++;
    }
    allocator.deallocate(job->pcm, job->samples);
    delete job;
  }
}

bool WakeCapture::upload_(const Job &job) {
  esp_http_client_config_t config{};
  config.url = this->url_.c_str();
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGW(TAG, "Could not create HTTP client");
    return false;
  }

  const std::string device = App.get_friendly_name().str();
  const std::string original_name = App.get_name().str() + "_wake_capture.raw";
  esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
  esp_http_client_set_header(client, "X-Audio-Format", "pcm_s16le");
  esp_http_client_set_header(client, "X-Original-Name", original_name.c_str());
  esp_http_client_set_header(client, "X-Source-Device", device.c_str());
  esp_http_client_set_header(client, "X-Wake-Word", job.wake_word.c_str());
  esp_http_client_set_header(client, "X-Event-Type", job.event_type.c_str());
  esp_http_client_set_header(client, "X-Notes", "Satellite1 wake_capture");
  esp_http_client_set_post_field(client, reinterpret_cast<const char *>(job.pcm),
                                 (int) (job.samples * sizeof(int16_t)));

  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGW(TAG, "Upload of '%s' clip failed: %s, HTTP %d", job.wake_word.c_str(), esp_err_to_name(err), status);
    return false;
  }
  ESP_LOGI(TAG, "Sent %.2fs '%s' clip (%u sent, %u dropped, %u failed)", job.samples / (float) SAMPLE_RATE,
           job.wake_word.c_str(), (unsigned) this->sent_ + 1, (unsigned) this->dropped_, (unsigned) this->failed_);
  return true;
}

}  // namespace wake_capture
}  // namespace esphome
