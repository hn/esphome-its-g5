"""Sensor platform for the its_g5_receiver hub.

Exposes optional cumulative counters: the total size in bytes of the received
(and accepted) C-ITS frames, their number, and the number of frames dropped
under load.
"""

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_DATA_SIZE,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_BYTES,
)
from esphome.types import ConfigType

from . import CONF_ITS_G5_RECEIVER_ID, ITSG5Receiver

DEPENDENCIES = ["its_g5_receiver"]

CONF_BYTES_RECEIVED = "bytes_received"
CONF_PACKETS_RECEIVED = "packets_received"
CONF_PACKETS_DROPPED = "packets_dropped"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ITS_G5_RECEIVER_ID): cv.use_id(ITSG5Receiver),
        cv.Optional(CONF_BYTES_RECEIVED): sensor.sensor_schema(
            unit_of_measurement=UNIT_BYTES,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DATA_SIZE,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_PACKETS_RECEIVED): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_PACKETS_DROPPED): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_ITS_G5_RECEIVER_ID])
    sensors = sensor.sub_sensors(config)
    await sensors(CONF_BYTES_RECEIVED, hub.set_bytes_received_sensor)
    await sensors(CONF_PACKETS_RECEIVED, hub.set_packets_received_sensor)
    await sensors(CONF_PACKETS_DROPPED, hub.set_packets_dropped_sensor)
