#include "its_g5_receiver.h"

#include <cinttypes>
#include <cstring>

#include "esp_err.h"
#include "esp_wifi.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "esphome/components/network/util.h"

namespace esphome {
namespace its_g5_receiver {

static const char *const TAG = "its_g5_receiver";

// Number of frames buffered between the RX callback and loop().
static const uint32_t WORK_QUEUE_LEN = 128;

// Maximum number of frames processed per loop() iteration, to bound the time
// spent in a single loop when frames arrive in bursts.
static const uint32_t MAX_FRAMES_PER_LOOP = 16;

// Throttle interval for publishing the counters.
static const uint32_t COUNTER_PUBLISH_INTERVAL_MS = 1000;

// Undocumented PHY helpers provided by the Espressif PHY blob (libphy.a).
// These are what actually enable 802.11p / ITS-G5 reception on the ESP32-C5.
extern "C" void phy_11p_set(int enable, int unknown);
extern "C" void phy_change_channel(int channel, int a, int b, int c);

ITSG5Receiver *ITSG5Receiver::instance_ = nullptr;

float ITSG5Receiver::get_setup_priority() const {
  // Set up after WiFi/network so the PHY is ready; the actual sniffer start is
  // additionally gated on network connectivity in loop().
  return setup_priority::AFTER_WIFI;
}

void ITSG5Receiver::setup() {
  ITSG5Receiver::instance_ = this;

  // Initialize WiFi in NULL mode: we never associate, we only use the PHY in
  // promiscuous mode (mirrors the reference firmware's initialize_wifi()).
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

  this->work_queue_ = xQueueCreate(WORK_QUEUE_LEN, sizeof(CapturedFrame));
  if (this->work_queue_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create work queue");
    this->mark_failed();
    return;
  }
}

void ITSG5Receiver::loop() {
  // Start the sniffer once, as soon as the network is connected.
  if (!this->sniffer_started_ && network::is_connected()) {
    this->start_sniffer_();
  }

  // Drain queued frames (filled by the promiscuous RX callback) and fire the
  // on_packet trigger for each. Doing this in loop() keeps the RX callback
  // lightweight and runs the user's automation in the main loop context.
  CapturedFrame frame;
  for (uint32_t i = 0; i < MAX_FRAMES_PER_LOOP; i++) {
    if (xQueueReceive(this->work_queue_, &frame, 0) != pdTRUE) {
      break;
    }

    std::vector<uint8_t> data(frame.payload, frame.payload + frame.length);
    free(frame.payload);

    this->bytes_received_count_ += frame.length;
    this->packets_received_count_++;
    this->packet_trigger_.trigger(std::move(data), frame.rssi, frame.channel, frame.rate);
  }

  this->publish_counters_();
}

void ITSG5Receiver::publish_counters_() {
  const uint32_t dropped = this->packets_dropped_count_.load(std::memory_order_relaxed);

  const bool bytes_changed =
      this->bytes_received_sensor_ != nullptr && this->bytes_received_count_ != this->last_published_bytes_;
  const bool received_changed =
      this->packets_received_sensor_ != nullptr && this->packets_received_count_ != this->last_published_received_;
  const bool dropped_changed =
      this->packets_dropped_sensor_ != nullptr && dropped != this->last_published_dropped_;

  if (!bytes_changed && !received_changed && !dropped_changed) {
    return;
  }

  // Throttle to at most once per interval (shared by both counters).
  const uint32_t now = millis();
  if (now - this->last_publish_ms_ < COUNTER_PUBLISH_INTERVAL_MS) {
    return;
  }
  this->last_publish_ms_ = now;

  if (bytes_changed) {
    this->last_published_bytes_ = this->bytes_received_count_;
    // A sensor state is a float, so the value stays exact only up to 2^24
    // bytes (16 MiB). Beyond that it rounds to the float grid; the counter
    // itself keeps counting exactly.
    this->bytes_received_sensor_->publish_state(static_cast<float>(this->bytes_received_count_));
  }
  if (received_changed) {
    this->last_published_received_ = this->packets_received_count_;
    this->packets_received_sensor_->publish_state(this->packets_received_count_);
  }
  if (dropped_changed) {
    this->last_published_dropped_ = dropped;
    this->packets_dropped_sensor_->publish_state(dropped);
  }
}

void ITSG5Receiver::start_sniffer_() {
  ESP_LOGI(TAG, "Starting ITS-G5 sniffer on channel %" PRIu32 " MHz", this->channel_);

  // Capture all frame types except those failing the frame check sequence.
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL & ~WIFI_PROMIS_FILTER_MASK_FCSFAIL;
  esp_wifi_set_promiscuous_filter(&filter);

  esp_wifi_set_promiscuous_rx_cb(ITSG5Receiver::promiscuous_rx_cb_);

  esp_err_t err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_set_promiscuous failed: %s", esp_err_to_name(err));
    return;
  }

  // Enable 802.11p mode (enable, unknown - must be 0).
  phy_11p_set(1, 0);

  // Select a channel with a frequency close to the desired one first (this
  // mirrors the reference firmware; not certain it is strictly required).
  err = esp_wifi_set_channel(140, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_set_channel(140) failed: %s", esp_err_to_name(err));
  }

  // Switch to the exact ITS-G5 channel (channel, ignored, ignored, ht_mode?).
  phy_change_channel(static_cast<int>(this->channel_), 1, 0, 0);

  this->sniffer_started_ = true;
  ESP_LOGI(TAG, "ITS-G5 sniffer started");
}

void ITSG5Receiver::promiscuous_rx_cb_(void *recv_buf, wifi_promiscuous_pkt_type_t type) {
  ITSG5Receiver *self = ITSG5Receiver::instance_;
  if (self != nullptr) {
    self->handle_rx_(recv_buf, type);
  }
}

void ITSG5Receiver::handle_rx_(void *recv_buf, wifi_promiscuous_pkt_type_t type) {
  auto *packet = static_cast<wifi_promiscuous_pkt_t *>(recv_buf);

  // The reference firmware ignores MISC frames and frames with rx_state set.
  if (type == WIFI_PKT_MISC || packet->rx_ctrl.rx_state != 0) {
    return;
  }

  // On the ESP32-C5 (HE-capable), the usable length is dump_len (excludes FCS).
  uint32_t length = packet->rx_ctrl.dump_len;

  // Optionally only forward broadcast frames (destination address at offset 4
  // in an 802.11 QoS data frame is FF:FF:FF:FF:FF:FF).
  if (this->broadcast_only_) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (length < (4 + 6) || memcmp(&packet->payload[4], broadcast, 6) != 0) {
      return;
    }
  }

  if (this->work_queue_ == nullptr) {
    return;
  }

  // Copy the frame and hand it to loop(); the receive buffer is only valid for
  // the duration of this callback.
  auto *copy = static_cast<uint8_t *>(malloc(length));
  if (copy == nullptr) {
    // Out of memory: drop the frame (avoid logging here, this is a hot path).
    this->packets_dropped_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  memcpy(copy, packet->payload, length);

  CapturedFrame frame{};
  frame.payload = copy;
  frame.length = static_cast<uint16_t>(length);
  frame.rssi = packet->rx_ctrl.rssi;
  frame.channel = packet->rx_ctrl.channel;
  frame.rate = packet->rx_ctrl.rate;

  if (xQueueSend(this->work_queue_, &frame, 0) != pdTRUE) {
    // Queue full: drop the frame to keep the RX callback non-blocking.
    free(copy);
    this->packets_dropped_count_.fetch_add(1, std::memory_order_relaxed);
  }
}

void ITSG5Receiver::dump_config() {
  ESP_LOGCONFIG(TAG, "ITS-G5 Receiver:");
  ESP_LOGCONFIG(TAG, "  Channel: %" PRIu32 " MHz", this->channel_);
  ESP_LOGCONFIG(TAG, "  Broadcast only: %s", YESNO(this->broadcast_only_));
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup failed");
  }
}

}  // namespace its_g5_receiver
}  // namespace esphome
