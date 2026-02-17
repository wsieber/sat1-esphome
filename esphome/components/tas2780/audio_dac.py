import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome import automation
from esphome.components.audio_dac import AudioDac
from esphome.const import (
    CONF_CHANNEL,
    CONF_ID,
    CONF_IMPEDANCE, 
    CONF_MAX_CURRENT,
    CONF_POWER,
    CONF_VOLTAGE,     
)

CODEOWNERS = ["@gnumpi"]
DEPENDENCIES = ["i2c"]

tas2780_ns = cg.esphome_ns.namespace("tas2780")
tas2780 = tas2780_ns.class_("TAS2780", AudioDac, cg.Component, i2c.I2CDevice)


RestAction = tas2780_ns.class_(
    "ResetAction", automation.Action, cg.Parented.template(tas2780)
)

ActivateAction = tas2780_ns.class_(
    "ActivateAction", automation.Action, cg.Parented.template(tas2780)
)

DeactivateAction = tas2780_ns.class_(
    "DeactivateAction", automation.Action, cg.Parented.template(tas2780)
)

UpdateSpeakerAction = tas2780_ns.class_(
    "UpdateSpeakerAction", automation.Action
)

UpdatePowerSupplyAction = tas2780_ns.class_(
    "UpdatePowerSupplyAction", automation.Action
)

UpdateConfigAction = tas2780_ns.class_(
    "UpdateConfigAction", automation.Action
)


CONF_VOL_RNG_MIN = "vol_range_min"
CONF_VOL_RNG_MAX = "vol_range_max"
CONF_MONO_DWN_MIX = "mono"
CONF_LEFT = "left"
CONF_RIGHT = "right"


tas2780_channels_t = tas2780_ns.enum("ChannelSelect")
TAS2780_CHANNELS = {
    CONF_MONO_DWN_MIX: tas2780_channels_t.MONO_DWN_MIX,
    CONF_LEFT: tas2780_channels_t.LEFT_CHANNEL,
    CONF_RIGHT: tas2780_channels_t.RIGHT_CHANNEL,
}

SpeakerImpedance = tas2780_ns.enum("SpeakerImpedance")
SPEAKER_IMPEDANCE = {
   4 : SpeakerImpedance.Ohm4,
   8 : SpeakerImpedance.Ohm8 
}

SupplyVoltage = tas2780_ns.enum("SupplyVoltage")
SUPPLY_VOLTAGE = {
    5 : SupplyVoltage.V5,
    9 : SupplyVoltage.V9,
    12 : SupplyVoltage.V12,
    15 : SupplyVoltage.V15,
    20 : SupplyVoltage.V20
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(tas2780),
            cv.Optional(CONF_IMPEDANCE, default=8) : cv.one_of(*SPEAKER_IMPEDANCE),
            cv.Optional(CONF_POWER, default=5) : cv.positive_float,
            cv.Optional(CONF_CHANNEL, default="mono") : cv.one_of(*TAS2780_CHANNELS),
            cv.Optional(CONF_VOL_RNG_MIN, default=.3) : cv.float_range(0.,1.),
            cv.Optional(CONF_VOL_RNG_MAX, default=1.) : cv.float_range(0.,1.),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x18))
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_speaker_specs(SPEAKER_IMPEDANCE[config[CONF_IMPEDANCE]], config[CONF_POWER]))
    cg.add(var.set_selected_channel(TAS2780_CHANNELS[config[CONF_CHANNEL]]))
    cg.add(var.set_vol_range_min(config[CONF_VOL_RNG_MIN]))
    cg.add(var.set_vol_range_max(config[CONF_VOL_RNG_MAX]))




TAS2780_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(tas2780),
    }
)
@automation.register_action("tas2780.activate", ActivateAction, TAS2780_ACTION_SCHEMA)
@automation.register_action("tas2780.deactivate", DeactivateAction, TAS2780_ACTION_SCHEMA)
@automation.register_action("tas2780.reset", RestAction, TAS2780_ACTION_SCHEMA)
async def tas2780_action(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var



TAS2780_UPDATE_CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(tas2780),
        cv.Optional(CONF_VOL_RNG_MIN, default=.3) : cv.templatable(cv.float_range(0.,1.)),
        cv.Optional(CONF_VOL_RNG_MAX, default=1.) : cv.templatable(cv.float_range(0.,1.)),
        cv.Optional(CONF_CHANNEL) : cv.templatable(cv.int_range(0,2)),
    }
)

@automation.register_action("tas2780.update_config", UpdateConfigAction, TAS2780_UPDATE_CONFIG_SCHEMA)
async def tas2780_action(config, action_id, template_arg, args):
    tas2780 = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, tas2780)
    vol_min_ = config.get(CONF_VOL_RNG_MIN)
    template_ = await cg.templatable(vol_min_, args, float)
    cg.add(var.set_vol_range_min(template_))
    vol_max_ = config.get(CONF_VOL_RNG_MAX)
    template_ = await cg.templatable(vol_max_, args, float)
    cg.add(var.set_vol_range_max(template_))
    if CONF_CHANNEL in config:
        channel = config.get(CONF_CHANNEL)
        template_ = await cg.templatable(channel, args, cg.uint8)
        cg.add(var.set_channel(template_))    
    return var



TAS2780_POWER_SUPPLY_CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(tas2780),
        cv.Required(CONF_VOLTAGE): cv.templatable(cv.one_of(*SUPPLY_VOLTAGE)),
        cv.Required(CONF_MAX_CURRENT): cv.templatable(cv.positive_float)
    }
)

@automation.register_action("tas2780.update_power_supply", UpdatePowerSupplyAction, TAS2780_POWER_SUPPLY_CONFIG_SCHEMA)
async def tas2780_action(config, action_id, template_arg, args):
    tas2780 = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, tas2780)
    voltage_ = config.get(CONF_VOLTAGE)
    tmpl_v = await cg.templatable(voltage_, args, cg.uint8)
    cg.add(var.set_voltage(tmpl_v))
    
    max_curr_ = config.get(CONF_MAX_CURRENT)
    tmpl_i = await cg.templatable(max_curr_, args, float)
    cg.add(var.set_max_current(tmpl_i))    
    return var




    