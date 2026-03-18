import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

from . import satellite1_radar_ns, Satellite1Radar, CONF_SATELLITE1_RADAR

Satellite1RadarSwitch = satellite1_radar_ns.class_(
    "Satellite1RadarSwitch", switch.Switch, cg.Component
)

CONF_LD2410_ENGINEERING_MODE = "ld2410_engineering_mode"
CONF_LD2410_BLUETOOTH = "ld2410_bluetooth"
CONF_LD2450_BLUETOOTH = "ld2450_bluetooth"
CONF_LD2450_MULTI_TARGET = "ld2450_multi_target"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SATELLITE1_RADAR): cv.use_id(Satellite1Radar),
        cv.Optional(CONF_LD2410_ENGINEERING_MODE): switch.switch_schema(
            Satellite1RadarSwitch,
            icon="mdi:cog",
            entity_category="config",
        ),
        cv.Optional(CONF_LD2410_BLUETOOTH): switch.switch_schema(
            Satellite1RadarSwitch,
            icon="mdi:bluetooth",
            entity_category="config",
        ),
        cv.Optional(CONF_LD2450_BLUETOOTH): switch.switch_schema(
            Satellite1RadarSwitch,
            icon="mdi:bluetooth",
            entity_category="config",
        ),
        cv.Optional(CONF_LD2450_MULTI_TARGET): switch.switch_schema(
            Satellite1RadarSwitch,
            icon="mdi:account-multiple",
            entity_category="config",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SATELLITE1_RADAR])

    if CONF_LD2410_ENGINEERING_MODE in config:
        s = await switch.new_switch(config[CONF_LD2410_ENGINEERING_MODE])
        cg.add(hub.set_ld2410_engineering_mode_switch(s))

    if CONF_LD2410_BLUETOOTH in config:
        s = await switch.new_switch(config[CONF_LD2410_BLUETOOTH])
        cg.add(hub.set_ld2410_bluetooth_switch(s))

    if CONF_LD2450_BLUETOOTH in config:
        s = await switch.new_switch(config[CONF_LD2450_BLUETOOTH])
        cg.add(hub.set_ld2450_bluetooth_switch(s))

    if CONF_LD2450_MULTI_TARGET in config:
        s = await switch.new_switch(config[CONF_LD2450_MULTI_TARGET])
        cg.add(hub.set_ld2450_multi_target_switch(s))
