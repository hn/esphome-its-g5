#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_wifi_types.h"

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace its_g5_receiver {

// One captured C-ITS frame handed from the promiscuous RX callback to loop()
// via a FreeRTOS queue.
struct CapturedFrame {
  uint8_t *payload;
  uint16_t length;
  int8_t rssi;
  uint8_t channel;
  uint8_t rate;
};

class ITSG5Receiver : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_channel(uint32_t channel) { this->channel_ = channel; }
  void set_broadcast_only(bool broadcast_only) { this->broadcast_only_ = broadcast_only; }
  void set_packets_received_sensor(sensor::Sensor *s) { this->packets_received_sensor_ = s; }
  void set_packets_dropped_sensor(sensor::Sensor *s) { this->packets_dropped_sensor_ = s; }

  Trigger<std::vector<uint8_t>, float, int, int> *get_packet_trigger() { return &this->packet_trigger_; }

 protected:
  // Enable promiscuous mode and switch the PHY into 802.11p on the configured
  // channel. Safe to call once connectivity is available.
  void start_sniffer_();

  // Static trampoline registered with esp_wifi_set_promiscuous_rx_cb.
  static void promiscuous_rx_cb_(void *recv_buf, wifi_promiscuous_pkt_type_t type);
  // Instance handler invoked by the trampoline (runs in the WiFi task context).
  void handle_rx_(void *recv_buf, wifi_promiscuous_pkt_type_t type);

  // Publish the packet counters, throttled to once per interval.
  void publish_counters_();

  uint32_t channel_{5900};
  bool broadcast_only_{true};

  bool sniffer_started_{false};

  QueueHandle_t work_queue_{nullptr};

  Trigger<std::vector<uint8_t>, float, int, int> packet_trigger_;

  // Received (and accepted) frames. Only touched from loop().
  sensor::Sensor *packets_received_sensor_{nullptr};
  uint32_t packets_received_count_{0};
  uint32_t last_published_received_{0};

  // Frames dropped in the RX callback (out of memory or work queue full).
  // Incremented from the WiFi task context, read from loop(), hence atomic.
  sensor::Sensor *packets_dropped_sensor_{nullptr};
  std::atomic<uint32_t> packets_dropped_count_{0};
  uint32_t last_published_dropped_{0};

  uint32_t last_publish_ms_{0};

  // The promiscuous callback is a plain C callback with no user argument, so we
  // reach the (single) instance through this static pointer.
  static ITSG5Receiver *instance_;
};

}  // namespace its_g5_receiver
}  // namespace esphome
