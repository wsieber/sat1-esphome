import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@FutureProofHomes"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "number", "switch", "select", "button"]
MULTI_CONF = False

CONF_SATELLITE1_RADAR = "satellite1_radar"

satellite1_radar_ns = cg.esphome_ns.namespace("satellite1_radar")
Satellite1Radar = satellite1_radar_ns.class_(
    "Satellite1Radar", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(): cv.declare_id(Satellite1Radar),
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    return var
