"""Sends the audio around each wake word firing to a microWakeWord trainer.

A passive microphone source keeps the last few seconds of what micro_wake_word hears in a PSRAM
ring. On a firing, capture() waits out a short post-roll, snapshots the ring, and a background task
POSTs it as raw 16 kHz mono PCM to the trainer's /api/upload_captured_audio_raw. The trainer
transcribes each clip and files the ones that do not contain the wake phrase as negatives for the
next retrain. Nothing here can delay or gate the wake itself: a busy upload drops the new clip.
"""

import esphome.codegen as cg
from esphome.components import microphone, switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MICROPHONE, CONF_URL, ENTITY_CATEGORY_CONFIG

DEPENDENCIES = ["microphone", "network"]
AUTO_LOAD = ["switch"]

CONF_PRE_ROLL = "pre_roll"
CONF_POST_ROLL = "post_roll"
CONF_ENABLE_SWITCH = "enable_switch"

wake_capture_ns = cg.esphome_ns.namespace("wake_capture")
WakeCapture = wake_capture_ns.class_("WakeCapture", cg.Component)
WakeCaptureSwitch = wake_capture_ns.class_("WakeCaptureSwitch", switch.Switch, cg.Parented.template(WakeCapture))

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WakeCapture),
        cv.Optional(CONF_MICROPHONE, default={}): microphone.microphone_source_schema(
            min_bits_per_sample=16,
            max_bits_per_sample=16,
            min_channels=1,
            max_channels=1,
        ),
        cv.Required(CONF_URL): cv.url,
        # 3 s in all, the clip length the trainer's own satellites send.
        cv.Optional(CONF_PRE_ROLL, default="2750ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(milliseconds=500), max=cv.TimePeriod(seconds=5)),
        ),
        cv.Optional(CONF_POST_ROLL, default="250ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(max=cv.TimePeriod(seconds=1)),
        ),
        cv.Optional(CONF_ENABLE_SWITCH, default={"name": "Wake word capture"}): switch.switch_schema(
            WakeCaptureSwitch,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:waveform",
            default_restore_mode="RESTORE_DEFAULT_ON",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

FINAL_VALIDATE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MICROPHONE): microphone.final_validate_microphone_source_schema(
            "wake_capture", sample_rate=16000
        ),
    },
    extra=cv.ALLOW_EXTRA,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Passive: rides along whenever micro_wake_word has the microphone running, never starts it.
    mic_source = await microphone.microphone_source_to_code(config[CONF_MICROPHONE], passive=True)
    cg.add(var.set_microphone_source(mic_source))
    cg.add(var.set_url(config[CONF_URL]))
    cg.add(var.set_pre_roll_ms(config[CONF_PRE_ROLL].total_milliseconds))
    cg.add(var.set_post_roll_ms(config[CONF_POST_ROLL].total_milliseconds))

    sw = await switch.new_switch(config[CONF_ENABLE_SWITCH])
    await cg.register_parented(sw, config[CONF_ID])
    cg.add(var.set_enable_switch(sw))
