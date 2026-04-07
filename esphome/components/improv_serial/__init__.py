import esphome.codegen as cg
from esphome.components import improv_base
from esphome.components.esp32 import get_esp32_variant
from esphome.components.esp32.const import VARIANT_ESP32S3
from esphome.components.logger import USB_CDC
import esphome.config_validation as cv
from esphome.const import CONF_BAUD_RATE, CONF_HARDWARE_UART, CONF_ID, CONF_LOGGER
from esphome.core import CORE
import esphome.final_validate as fv
from esphome import automation

AUTO_LOAD = ["improv_base"]
CODEOWNERS = ["@esphome/core"]
DEPENDENCIES = ["logger", "wifi"]

improv_serial_ns = cg.esphome_ns.namespace("improv_serial")

ImprovSerialComponent = improv_serial_ns.class_("ImprovSerialComponent", cg.Component)
ExtAction = improv_serial_ns.class_("ExtAction")
SendActionStatusAction = improv_serial_ns.class_(
    "ImprovSendActionStatusAction", 
    automation.Action
)


CONF_ON_ACTION = "on_action"
CONF_ACTION_ID  = "action"
CONF_STATUS = "status"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ImprovSerialComponent),
            cv.Optional(CONF_ON_ACTION): automation.validate_automation(single=True)
        })
    .extend(improv_base.IMPROV_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


def validate_logger(config):
    logger_conf = fv.full_config.get()[CONF_LOGGER]
    if logger_conf[CONF_BAUD_RATE] == 0:
        raise cv.Invalid("improv_serial requires the logger baud_rate to be not 0")
    if CORE.using_esp_idf:
        if (
            logger_conf[CONF_HARDWARE_UART] == USB_CDC
            and get_esp32_variant() == VARIANT_ESP32S3
        ):
            raise cv.Invalid(
                "improv_serial does not support the selected logger hardware_uart"
            )
    return config


FINAL_VALIDATE_SCHEMA = validate_logger


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await improv_base.setup_improv_core(var, config, "improv_serial")

    if on_action := config.get(CONF_ON_ACTION):
        await automation.build_automation(
            var.get_action_request_trigger(), 
            [(ExtAction, "x")],
            on_action
        )
    var.get_action_request_trigger()


SEND_ACTION_STATUS_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.use_id(ImprovSerialComponent),
        cv.Required(CONF_ACTION_ID): cv.string,
        cv.Required(CONF_STATUS): cv.positive_int
    }
)


@automation.register_action(
    "improv_serial.send_action_status",
    SendActionStatusAction,
    SEND_ACTION_STATUS_SCHEMA)
async def send_action_status_to_code(config, action_id, template_args, args):
    improv_comp = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_args, improv_comp)
    
    action_id = config[CONF_ACTION_ID]
    action_tmpl_ = await cg.templatable(action_id, args, cg.std_string)
    cg.add( var.set_action(action_tmpl_))

    status = config[CONF_STATUS]
    status_tmpl_ = await cg.templatable(status, args, cg.int16)
    cg.add( var.set_status(status_tmpl_))
    
    return var
    