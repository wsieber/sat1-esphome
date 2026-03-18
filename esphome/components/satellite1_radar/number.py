import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, UNIT_SECOND

from . import satellite1_radar_ns, Satellite1Radar, CONF_SATELLITE1_RADAR

# LD2410 number keys
CONF_LD2410_TIMEOUT = "ld2410_timeout"
CONF_LD2410_MAX_MOVE_DISTANCE_GATE = "ld2410_max_move_distance_gate"
CONF_LD2410_MAX_STILL_DISTANCE_GATE = "ld2410_max_still_distance_gate"
CONF_LD2410_LIGHT_THRESHOLD = "ld2410_light_threshold"
CONF_LD2410_GATES = "ld2410_gates"
CONF_MOVE_THRESHOLD = "move_threshold"
CONF_STILL_THRESHOLD = "still_threshold"

# LD2450 number keys
CONF_LD2450_TIMEOUT = "ld2450_timeout"
CONF_LD2450_ZONES = "ld2450_zones"
CONF_X1 = "x1"
CONF_Y1 = "y1"
CONF_X2 = "x2"
CONF_Y2 = "y2"

# Simple template number for configuration values
Satellite1RadarNumber = satellite1_radar_ns.class_(
    "Satellite1RadarNumber", number.Number, cg.Component
)

_GATE_NUMBER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MOVE_THRESHOLD): number.number_schema(
            Satellite1RadarNumber,
            icon="mdi:arrow-right-bold",
        ),
        cv.Optional(CONF_STILL_THRESHOLD): number.number_schema(
            Satellite1RadarNumber,
            icon="mdi:arrow-right-bold-outline",
        ),
    }
)

_ZONE_COORD_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_X1): number.number_schema(
            Satellite1RadarNumber,
        ),
        cv.Optional(CONF_Y1): number.number_schema(
            Satellite1RadarNumber,
        ),
        cv.Optional(CONF_X2): number.number_schema(
            Satellite1RadarNumber,
        ),
        cv.Optional(CONF_Y2): number.number_schema(
            Satellite1RadarNumber,
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SATELLITE1_RADAR): cv.use_id(Satellite1Radar),
        # LD2410 numbers
        cv.Optional(CONF_LD2410_TIMEOUT): number.number_schema(
            Satellite1RadarNumber,
            unit_of_measurement=UNIT_SECOND,
            icon="mdi:timer",
        ),
        cv.Optional(CONF_LD2410_MAX_MOVE_DISTANCE_GATE): number.number_schema(
            Satellite1RadarNumber,
            icon="mdi:arrow-expand-horizontal",
        ),
        cv.Optional(CONF_LD2410_MAX_STILL_DISTANCE_GATE): number.number_schema(
            Satellite1RadarNumber,
            icon="mdi:arrow-expand-horizontal",
        ),
        cv.Optional(CONF_LD2410_LIGHT_THRESHOLD): number.number_schema(
            Satellite1RadarNumber,
            icon="mdi:brightness-6",
        ),
        cv.Optional(CONF_LD2410_GATES): cv.Schema(
            {cv.Optional(f"g{i}"): _GATE_NUMBER_SCHEMA for i in range(9)}
        ),
        # LD2450 numbers
        cv.Optional(CONF_LD2450_TIMEOUT): number.number_schema(
            Satellite1RadarNumber,
            unit_of_measurement=UNIT_SECOND,
            icon="mdi:timer",
        ),
        cv.Optional(CONF_LD2450_ZONES): cv.Schema(
            {cv.Optional(f"zone_{i+1}"): _ZONE_COORD_SCHEMA for i in range(3)}
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SATELLITE1_RADAR])

    # LD2410 numbers
    for conf_key, setter in [
        (CONF_LD2410_TIMEOUT, hub.set_ld2410_timeout_number),
        (CONF_LD2410_MAX_MOVE_DISTANCE_GATE, hub.set_ld2410_max_move_distance_gate_number),
        (CONF_LD2410_MAX_STILL_DISTANCE_GATE, hub.set_ld2410_max_still_distance_gate_number),
        (CONF_LD2410_LIGHT_THRESHOLD, hub.set_ld2410_light_threshold_number),
    ]:
        if conf_key in config:
            n = await number.new_number(
                config[conf_key],
                min_value=0,
                max_value=65535,
                step=1,
            )
            cg.add(setter(n))

    # LD2410 per-gate numbers
    if CONF_LD2410_GATES in config:
        gates_conf = config[CONF_LD2410_GATES]
        for i in range(9):
            gate_key = f"g{i}"
            if gate_key in gates_conf:
                gc = gates_conf[gate_key]
                if CONF_MOVE_THRESHOLD in gc:
                    n = await number.new_number(
                        gc[CONF_MOVE_THRESHOLD],
                        min_value=0,
                        max_value=100,
                        step=1,
                    )
                    cg.add(hub.set_ld2410_gate_move_threshold_number(i, n))
                if CONF_STILL_THRESHOLD in gc:
                    n = await number.new_number(
                        gc[CONF_STILL_THRESHOLD],
                        min_value=0,
                        max_value=100,
                        step=1,
                    )
                    cg.add(hub.set_ld2410_gate_still_threshold_number(i, n))

    # LD2450 timeout
    if CONF_LD2450_TIMEOUT in config:
        n = await number.new_number(
            config[CONF_LD2450_TIMEOUT],
            min_value=0,
            max_value=65535,
            step=1,
        )
        cg.add(hub.set_ld2450_timeout_number(n))

    # LD2450 zone coordinates
    if CONF_LD2450_ZONES in config:
        zones_conf = config[CONF_LD2450_ZONES]
        for i in range(3):
            zone_key = f"zone_{i+1}"
            if zone_key in zones_conf:
                zc = zones_conf[zone_key]
                for ci, coord_key in enumerate([CONF_X1, CONF_Y1, CONF_X2, CONF_Y2]):
                    if coord_key in zc:
                        n = await number.new_number(
                            zc[coord_key],
                            min_value=-6000,
                            max_value=6000,
                            step=1,
                        )
                        cg.add(hub.set_ld2450_zone_coordinate_number(i, ci, n))
