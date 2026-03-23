import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

from . import satellite1_radar_ns, Satellite1Radar, CONF_SATELLITE1_RADAR

CONF_HAS_TARGET = "has_target"
CONF_HAS_MOVING_TARGET = "has_moving_target"
CONF_HAS_STILL_TARGET = "has_still_target"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SATELLITE1_RADAR): cv.use_id(Satellite1Radar),
        cv.Optional(CONF_HAS_TARGET): binary_sensor.binary_sensor_schema(
            device_class="occupancy",
        ),
        cv.Optional(CONF_HAS_MOVING_TARGET): binary_sensor.binary_sensor_schema(
            device_class="motion",
        ),
        cv.Optional(CONF_HAS_STILL_TARGET): binary_sensor.binary_sensor_schema(
            device_class="occupancy",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SATELLITE1_RADAR])

    if CONF_HAS_TARGET in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_HAS_TARGET])
        cg.add(hub.set_presence_binary_sensor(s))

    if CONF_HAS_MOVING_TARGET in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_HAS_MOVING_TARGET])
        cg.add(hub.set_moving_target_binary_sensor(s))

    if CONF_HAS_STILL_TARGET in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_HAS_STILL_TARGET])
        cg.add(hub.set_still_target_binary_sensor(s))
