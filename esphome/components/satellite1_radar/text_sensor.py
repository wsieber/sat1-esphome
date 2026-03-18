import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

from . import satellite1_radar_ns, Satellite1Radar, CONF_SATELLITE1_RADAR

CONF_RADAR_TYPE = "radar_type"
CONF_LD2450_VERSION = "ld2450_version"
CONF_LD2450_MAC = "ld2450_mac"
CONF_TARGET_1 = "target_1"
CONF_TARGET_2 = "target_2"
CONF_TARGET_3 = "target_3"
CONF_DIRECTION = "direction"

_TARGET_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_DIRECTION): text_sensor.text_sensor_schema(),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SATELLITE1_RADAR): cv.use_id(Satellite1Radar),
        cv.Optional(CONF_RADAR_TYPE): text_sensor.text_sensor_schema(
            icon="mdi:radar",
        ),
        cv.Optional(CONF_LD2450_VERSION): text_sensor.text_sensor_schema(
            icon="mdi:chip",
            entity_category="diagnostic",
        ),
        cv.Optional(CONF_LD2450_MAC): text_sensor.text_sensor_schema(
            icon="mdi:bluetooth",
            entity_category="diagnostic",
        ),
        cv.Optional(CONF_TARGET_1): _TARGET_SCHEMA,
        cv.Optional(CONF_TARGET_2): _TARGET_SCHEMA,
        cv.Optional(CONF_TARGET_3): _TARGET_SCHEMA,
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SATELLITE1_RADAR])

    if CONF_RADAR_TYPE in config:
        s = await text_sensor.new_text_sensor(config[CONF_RADAR_TYPE])
        cg.add(hub.set_radar_type_text_sensor(s))

    if CONF_LD2450_VERSION in config:
        s = await text_sensor.new_text_sensor(config[CONF_LD2450_VERSION])
        cg.add(hub.set_ld2450_version_text_sensor(s))

    if CONF_LD2450_MAC in config:
        s = await text_sensor.new_text_sensor(config[CONF_LD2450_MAC])
        cg.add(hub.set_ld2450_mac_text_sensor(s))

    for idx, conf_key in enumerate([CONF_TARGET_1, CONF_TARGET_2, CONF_TARGET_3]):
        if conf_key in config:
            target_conf = config[conf_key]
            if CONF_DIRECTION in target_conf:
                s = await text_sensor.new_text_sensor(target_conf[CONF_DIRECTION])
                cg.add(hub.set_ld2450_target_direction_text_sensor(idx, s))
