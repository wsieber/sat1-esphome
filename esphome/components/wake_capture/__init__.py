"""Sends the audio around each wake word firing to a microWakeWord trainer.

A passive microphone source keeps the last few seconds of what micro_wake_word hears in a PSRAM
ring. On a firing, capture() waits out a short post-roll, snapshots the ring, and a background task
POSTs it as raw 16 kHz mono PCM to the trainer's /api/upload_captured_audio_raw. The trainer
review queue holds each clip until it is marked negative (a false trigger), approved or discarded.
capture(..., hold=true) keeps the clip back until the session shows whether the firing was likely
false - release_for_transcript() / release_for_stt_error() send or drop it - so only those reach
the queue. Nothing here can delay or gate the wake itself: a busy upload drops the new clip.
"""

import esphome.codegen as cg
from esphome.components import microphone
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MICROPHONE, CONF_URL

DEPENDENCIES = ["microphone", "network"]

CONF_PRE_ROLL = "pre_roll"
CONF_POST_ROLL = "post_roll"

wake_capture_ns = cg.esphome_ns.namespace("wake_capture")
WakeCapture = wake_capture_ns.class_("WakeCapture", cg.Component)

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
