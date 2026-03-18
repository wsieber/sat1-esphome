import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from . import satellite1_radar_ns, Satellite1Radar, CONF_SATELLITE1_RADAR

Satellite1RadarButton = satellite1_radar_ns.class_(
    "Satellite1RadarButton", button.Button, cg.Component
)

CONF_FACTORY_RESET = "factory_reset"
CONF_RESTART = "restart"
CONF_QUERY_PARAMS = "query_params"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SATELLITE1_RADAR): cv.use_id(Satellite1Radar),
        cv.Optional(CONF_FACTORY_RESET): button.button_schema(
            Satellite1RadarButton,
            icon="mdi:factory",
            entity_category="config",
        ),
        cv.Optional(CONF_RESTART): button.button_schema(
            Satellite1RadarButton,
            icon="mdi:restart",
            entity_category="config",
        ),
        cv.Optional(CONF_QUERY_PARAMS): button.button_schema(
            Satellite1RadarButton,
            icon="mdi:database-refresh",
            entity_category="config",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SATELLITE1_RADAR])

    if CONF_FACTORY_RESET in config:
        b = await button.new_button(config[CONF_FACTORY_RESET])
        cg.add(b.set_parent(hub))
        cg.add(b.set_button_type(0))

    if CONF_RESTART in config:
        b = await button.new_button(config[CONF_RESTART])
        cg.add(b.set_parent(hub))
        cg.add(b.set_button_type(1))

    if CONF_QUERY_PARAMS in config:
        b = await button.new_button(config[CONF_QUERY_PARAMS])
        cg.add(b.set_parent(hub))
        cg.add(b.set_button_type(2))
