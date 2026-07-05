# ESPHome ITS-G5 Receiver

An [ESPHome](https://esphome.io/) component that turns an **ESP32-C5** into a
receiver for **ITS-G5 / 802.11p** Cooperative Intelligent Transport Systems
(C-ITS) messages broadcast by road traffic infrastructure (traffic lights, road
side units, ...). It captures the raw frames and hands them to your automations
via an `on_packet` trigger — for example to forward them to
[OpenTrafficMap](https://opentrafficmap.org/).

This project stands entirely on the shoulders of the excellent
[**opentrafficmap / its-g5-receiver-firmware**](https://codeberg.org/opentrafficmap/its-g5-receiver-firmware).
The opentrafficmap team did the hard part: they figured out how to coax the
ESP32-C5's WiFi radio into 802.11p mode at all — using the otherwise
undocumented PHY functions `phy_11p_set()` and `phy_change_channel()` hidden in
Espressif's binary PHY blob. Without their reverse-engineering, discovery, and
open publication of that firmware, this ESPHome port simply would not exist.

If you want a ready-made, purpose-built receiver, please look at their project
and hardware first. Huge thanks to the opentrafficmap community. See also their
[wiki](https://wiki.opentrafficmap.org/) and the
[map](https://opentrafficmap.org/).

This repository is just a re-implementation of that firmware's core as a
reusable ESPHome component, so it can be combined with the rest of the ESPHome
ecosystem (Home Assistant, any uplink, other sensors, ...).

Please do me a favor: :thumbsup: If you use any information or code you find here, please link back to this page.
:star: Also, please consider to star this project. I really like to keep track of who is using this to do creative things, especially if you are from other parts of the world.
:smiley: You are welcome to open an issue to report on your successful project and share it with others.

## Component configuration

Add the component via `external_components` and then configure the receiver:

```yaml
external_components:
  - source: github://hn/esphome-its-g5
    components: [ its_g5_receiver ]

its_g5_receiver:
  id: g5
  channel: 5900          # ITS-G5 channel in MHz (G5CC=5900, G5SC2=5890,
                         # G5SC1=5880, G5SC3=5870, G5SC4=5860)
  broadcast_only: true   # only forward broadcast frames
  on_packet:
    - lambda: |-
        // x       -> std::vector<uint8_t>  (raw frame bytes)
        // rssi    -> float                 (dBm)
        // channel -> int
        // rate    -> int
        ESP_LOGD("main", "got %d bytes, rssi=%.0f", x.size(), rssi);
```

### `on_packet` trigger

Fired for every captured (and, if `broadcast_only`, broadcast) frame. Lambda
variables:

| Variable | Type | Meaning |
| -------- | ---- | ------- |
| `x` | `std::vector<uint8_t>` | raw frame bytes |
| `rssi` | `float` | receive signal strength (dBm) |
| `channel` | `int` | reception channel |
| `rate` | `int` | reception rate field |

### Sensors

The `sensor` platform exposes optional cumulative counters:

```yaml
sensor:
  - platform: its_g5_receiver
    its_g5_receiver_id: g5
    packets_received:
      name: "Packets Received"
    packets_dropped:
      name: "Packets Dropped"
```

The `packets_received` sensor shows the frames received and dispatched to `on_packet`,
the `packets_dropped` sensors shows frames dropped in the RX callback because the internal work
queue was full or memory was exhausted (a load/back-pressure indicator; should stay 0 in normal operation).

The component intentionally leaves out some features of the reference firmware
to stay lightweight: the remote command topic, runtime (NVS) reconfiguration,
pcap capture, and the firmware's HW_VARIANT OTA logic (ESPHome has its own OTA).

## Using it with OpenTrafficMap

[OpenTrafficMap](https://opentrafficmap.org/) collects C-ITS messages from
receivers all over the place and visualises traffic infrastructure on a public
map. This repo ships a complete, ready-to-flash example config,
[`its-g5-opentrafficmap.yaml`](its-g5-opentrafficmap.yaml), that receives frames
and forwards them to the OpenTrafficMap MQTT server — reproducing the reference
firmware's behaviour.

> [!WARNING]
> Using `its-g5-opentrafficmap.yaml` as a replacement for the original
> firmware is highly experimental and risky, since the OpenTrafficMap
> hardware uses the KSZ8851SNL Ethernet chip, which is not (yet) included
> in the standard ESPHome distribution. A patched Ethernet component is
> shipped with this project, which adds support for the KSZ8851SNL.
> **This patched component is likely to compile only with ESPHome
> version 2026.6; expect it to break soon**.

> [!NOTE]
> If you are using your own hardware without the KSZ8851SNL chip
> (e.g. a W5500 or a ENC28J60), everything should generally work fine.
> Don't forget, in this case, to adjust the `ethernet:` section to match
> your hardware, and be sure to remove `ethernet` from the `components:`
> list in the `external_components:` section of the YAML file.

### Setup

Every receiver publishes under a node ID, used in all MQTT topics
(`its/<node_id>/...`). You need to set it once as a substitution at
the top of the config:

```yaml
substitutions:
  node_id: "aabbccddeeff"   # Ethernet MAC hex, or a friendly name (e.g. node789)
```

- **Ethernet MAC (default):** the reference firmware uses the device's Ethernet
  MAC as lowercase hex (e.g. `aabbccddeeff`). Using the same value makes this
  ESPHome build present the *same* identity as a device already known to
  OpenTrafficMap. If you don't know the MAC, flash once and read it from the
  device log, or from the `info` topic (field `emac`).
- **Friendly name:** OpenTrafficMap can assign your device a friendly name
  (e.g. `node789`); if so, put that in `node_id`.

Publishing to the OpenTrafficMap server is anonymous — no username or
password is required. For registering a device, assigning a friendly name,
showing it on the map, or getting credentials to *subscribe* to your own node's
messages, see the OpenTrafficMap
[node registration wiki](https://wiki.opentrafficmap.org/node-registration).

### Installation

Use `esptool --chip esp32c5 -p /dev/ttyACM0 -b 921600 --before=default-reset --after=hard-reset write-flash 0x0 its-g5-firmware.factory.bin`
to install the ESPHome factory image on the ESP32 (USB port on the left).
