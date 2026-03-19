import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID

from . import satellite1_radar_ns, Satellite1Radar, CONF_SATELLITE1_RADAR

Satellite1RadarSelect = satellite1_radar_ns.class_(
    "Satellite1RadarSelect", select.Select, cg.Component
)

CONF_LD2410_DISTANCE_RESOLUTION = "ld2410_distance_resolution"
CONF_LD2410_LIGHT_FUNCTION = "ld2410_light_function"
CONF_LD2450_BAUD_RATE = "ld2450_baud_rate"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SATELLITE1_RADAR): cv.use_id(Satellite1Radar),
        cv.Optional(CONF_LD2410_DISTANCE_RESOLUTION): select.select_schema(
            Satellite1RadarSelect,
            icon="mdi:ruler",
            entity_category="diagnostic",
        ),
        cv.Optional(CONF_LD2410_LIGHT_FUNCTION): select.select_schema(
            Satellite1RadarSelect,
            icon="mdi:brightness-6",
            entity_category="diagnostic",
        ),
        cv.Optional(CONF_LD2450_BAUD_RATE): select.select_schema(
            Satellite1RadarSelect,
            icon="mdi:serial-port",
            entity_category="diagnostic",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SATELLITE1_RADAR])

    if CONF_LD2410_DISTANCE_RESOLUTION in config:
        s = await select.new_select(config[CONF_LD2410_DISTANCE_RESOLUTION], options=["0.75m", "0.2m"])
        cg.add(hub.set_ld2410_distance_resolution_select(s))

    if CONF_LD2410_LIGHT_FUNCTION in config:
        s = await select.new_select(config[CONF_LD2410_LIGHT_FUNCTION], options=[])
        cg.add(hub.set_ld2410_light_function_select(s))

    if CONF_LD2450_BAUD_RATE in config:
        s = await select.new_select(config[CONF_LD2450_BAUD_RATE], options=[])
        cg.add(hub.set_ld2450_baud_rate_select(s))
