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
CONF_LD2450_STABILITY = "ld2450_stability"
CONF_LD2450_TIMEOUT = "ld2450_timeout"
CONF_LD2450_ZONES = "ld2450_zones"
CONF_LD2450_EXCLUSION_ZONE = "ld2450_exclusion_zone"
CONF_LD2450_DETECTION_RANGE = "ld2450_detection_range"
CONF_POINTS_COUNT = "points_count"
CONF_X = "x"
CONF_Y = "y"

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

_ZONE_POINT_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_X): number.number_schema(Satellite1RadarNumber),
        cv.Optional(CONF_Y): number.number_schema(Satellite1RadarNumber),
    }
)

_ZONE_POLYGON_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_POINTS_COUNT): number.number_schema(Satellite1RadarNumber),
        **{cv.Optional(f"p{i}"): _ZONE_POINT_SCHEMA for i in range(1, 9)},
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
        cv.Optional(CONF_LD2450_STABILITY): number.number_schema(
            Satellite1RadarNumber,
            icon="mdi:tune-vertical",
        ),
        cv.Optional(CONF_LD2450_TIMEOUT): number.number_schema(
            Satellite1RadarNumber,
            unit_of_measurement=UNIT_SECOND,
            icon="mdi:timer",
        ),
        cv.Optional(CONF_LD2450_ZONES): cv.Schema(
            {cv.Optional(f"zone_{i+1}"): _ZONE_POLYGON_SCHEMA for i in range(3)}
        ),
        cv.Optional(CONF_LD2450_EXCLUSION_ZONE): _ZONE_POLYGON_SCHEMA,
        cv.Optional(CONF_LD2450_DETECTION_RANGE): number.number_schema(
            Satellite1RadarNumber,
            unit_of_measurement="cm",
            icon="mdi:signal-distance-variant",
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
            await cg.register_component(n, config[conf_key])
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
                    await cg.register_component(n, gc[CONF_MOVE_THRESHOLD])
                    cg.add(hub.set_ld2410_gate_move_threshold_number(i, n))
                if CONF_STILL_THRESHOLD in gc:
                    n = await number.new_number(
                        gc[CONF_STILL_THRESHOLD],
                        min_value=0,
                        max_value=100,
                        step=1,
                    )
                    await cg.register_component(n, gc[CONF_STILL_THRESHOLD])
                    cg.add(hub.set_ld2410_gate_still_threshold_number(i, n))

    # LD2450 stability (debounce strength)
    if CONF_LD2450_STABILITY in config:
        n = await number.new_number(
            config[CONF_LD2450_STABILITY],
            min_value=0,
            max_value=10,
            step=1,
        )
        await cg.register_component(n, config[CONF_LD2450_STABILITY])
        cg.add(hub.set_ld2450_stability_number(n))

    # LD2450 timeout
    if CONF_LD2450_TIMEOUT in config:
        n = await number.new_number(
            config[CONF_LD2450_TIMEOUT],
            min_value=0,
            max_value=65535,
            step=1,
        )
        await cg.register_component(n, config[CONF_LD2450_TIMEOUT])
        cg.add(hub.set_ld2450_timeout_number(n))

    # LD2450 polygon zone coordinates
    if CONF_LD2450_ZONES in config:
        zones_conf = config[CONF_LD2450_ZONES]
        for i in range(3):
            zone_key = f"zone_{i+1}"
            if zone_key in zones_conf:
                zc = zones_conf[zone_key]
                if CONF_POINTS_COUNT in zc:
                    n = await number.new_number(
                        zc[CONF_POINTS_COUNT],
                        min_value=0, max_value=8, step=1,
                    )
                    await cg.register_component(n, zc[CONF_POINTS_COUNT])
                    cg.add(hub.set_ld2450_zone_points_count_number(i, n))
                for pi in range(1, 9):
                    point_key = f"p{pi}"
                    if point_key in zc:
                        pc = zc[point_key]
                        if CONF_X in pc:
                            n = await number.new_number(
                                pc[CONF_X],
                                min_value=-6000, max_value=6000, step=1,
                            )
                            await cg.register_component(n, pc[CONF_X])
                            cg.add(hub.set_ld2450_zone_point_coord_number(i, pi - 1, 0, n))
                        if CONF_Y in pc:
                            n = await number.new_number(
                                pc[CONF_Y],
                                min_value=-6000, max_value=6000, step=1,
                            )
                            await cg.register_component(n, pc[CONF_Y])
                            cg.add(hub.set_ld2450_zone_point_coord_number(i, pi - 1, 1, n))

    # LD2450 exclusion zone
    if CONF_LD2450_EXCLUSION_ZONE in config:
        ec = config[CONF_LD2450_EXCLUSION_ZONE]
        if CONF_POINTS_COUNT in ec:
            n = await number.new_number(
                ec[CONF_POINTS_COUNT],
                min_value=0, max_value=8, step=1,
            )
            await cg.register_component(n, ec[CONF_POINTS_COUNT])
            cg.add(hub.set_ld2450_excl_zone_points_count_number(n))
        for pi in range(1, 9):
            point_key = f"p{pi}"
            if point_key in ec:
                pc = ec[point_key]
                if CONF_X in pc:
                    n = await number.new_number(
                        pc[CONF_X],
                        min_value=-6000, max_value=6000, step=1,
                    )
                    await cg.register_component(n, pc[CONF_X])
                    cg.add(hub.set_ld2450_excl_zone_point_coord_number(pi - 1, 0, n))
                if CONF_Y in pc:
                    n = await number.new_number(
                        pc[CONF_Y],
                        min_value=-6000, max_value=6000, step=1,
                    )
                    await cg.register_component(n, pc[CONF_Y])
                    cg.add(hub.set_ld2450_excl_zone_point_coord_number(pi - 1, 1, n))

    # LD2450 detection range
    if CONF_LD2450_DETECTION_RANGE in config:
        n = await number.new_number(
            config[CONF_LD2450_DETECTION_RANGE],
            min_value=0, max_value=600, step=1,
        )
        await cg.register_component(n, config[CONF_LD2450_DETECTION_RANGE])
        cg.add(hub.set_ld2450_detection_range_number(n))
