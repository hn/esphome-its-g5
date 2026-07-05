"""ITS-G5 (802.11p) receiver component for ESPHome.

Puts the ESP32-C5 WiFi PHY into 802.11p / ITS-G5 mode and captures C-ITS frames
in promiscuous mode. Each captured frame fires an ``on_packet`` trigger with the
raw frame bytes and reception metadata, so the user decides what to do with it
(publish to MQTT, forward over HTTP, log, ...).

The component is transport agnostic: it only depends on ``network`` (any uplink:
ethernet, an LTE modem, ...) and the ``esp32`` platform. It claims the WiFi PHY
exclusively for 802.11p, hence it conflicts with the ``wifi`` component.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.const import CONF_CHANNEL, CONF_ID
from esphome.core import CoroPriority, coroutine_with_priority

CODEOWNERS = ["@hn"]

# Any uplink (network) is enough; MQTT is not required by the component itself.
# The esp32 platform is required because we call into the ESP-IDF WiFi/PHY APIs.
DEPENDENCIES = ["esp32", "network"]

# We drive the WiFi PHY directly (WIFI_MODE_NULL + promiscuous + 802.11p), so
# the regular wifi component (STA/AP connection stack) must not be present.
CONFLICTS_WITH = ["wifi"]

CONF_BROADCAST_ONLY = "broadcast_only"
CONF_ON_PACKET = "on_packet"

# Exported for the sensor platform (sensor.py).
CONF_ITS_G5_RECEIVER_ID = "its_g5_receiver_id"

its_g5_receiver_ns = cg.esphome_ns.namespace("its_g5_receiver")
ITSG5Receiver = its_g5_receiver_ns.class_("ITSG5Receiver", cg.Component)
ITSG5ReceiverPacketTrigger = its_g5_receiver_ns.class_(
    "ITSG5ReceiverPacketTrigger",
    automation.Trigger.template(
        cg.std_vector.template(cg.uint8), cg.float_, cg.int_, cg.int_
    ),
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ITSG5Receiver),
            # ITS-G5 channel frequency in MHz. Reference firmware:
            # G5CC = 5900, G5SC2 = 5890, G5SC1 = 5880, G5SC3 = 5870, G5SC4 = 5860.
            cv.Optional(CONF_CHANNEL, default=5900): cv.int_range(min=5800, max=5900),
            # Only forward broadcast frames (destination FF:FF:FF:FF:FF:FF).
            cv.Optional(CONF_BROADCAST_ONLY, default=True): cv.boolean,
            cv.Optional(CONF_ON_PACKET): automation.validate_automation(single=True),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


# Runs at a lower priority than the ethernet component
# (CoroPriority.COMMUNICATION) so that our CONFIG_ESP_WIFI_ENABLED=True overrides
# the value ethernet writes (it disables WiFi to save memory).
@coroutine_with_priority(CoroPriority.LATE)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_broadcast_only(config[CONF_BROADCAST_ONLY]))

    if CONF_ON_PACKET in config:
        await automation.build_automation(
            var.get_packet_trigger(),
            [
                (cg.std_vector.template(cg.uint8), "x"),
                (cg.float_, "rssi"),
                (cg.int_, "channel"),
                (cg.int_, "rate"),
            ],
            config[CONF_ON_PACKET],
        )

    # Keep the ESP-IDF WiFi stack compiled in. Without any wifi component and
    # with ethernet present, ESPHome sets CONFIG_ESP_WIFI_ENABLED=n, which would
    # make esp_wifi_init() and the PHY symbols (phy_11p_set / phy_change_channel)
    # fail to link.
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_WIFI_ENABLED", True)
