"""Sensor platform for the its_g5_receiver hub.

Exposes optional cumulative counters, e.g. the total number of received
(and accepted) C-ITS frames, and the number of frames dropped under load.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import STATE_CLASS_TOTAL_INCREASING

from . import CONF_ITS_G5_RECEIVER_ID, ITSG5Receiver

DEPENDENCIES = ["its_g5_receiver"]

CONF_PACKETS_RECEIVED = "packets_received"
CONF_PACKETS_DROPPED = "packets_dropped"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ITS_G5_RECEIVER_ID): cv.use_id(ITSG5Receiver),
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


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ITS_G5_RECEIVER_ID])

    if CONF_PACKETS_RECEIVED in config:
        sens = await sensor.new_sensor(config[CONF_PACKETS_RECEIVED])
        cg.add(hub.set_packets_received_sensor(sens))

    if CONF_PACKETS_DROPPED in config:
        sens = await sensor.new_sensor(config[CONF_PACKETS_DROPPED])
        cg.add(hub.set_packets_dropped_sensor(sens))
