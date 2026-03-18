import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    UNIT_CENTIMETER,
    UNIT_PERCENT,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_ILLUMINANCE,
    STATE_CLASS_MEASUREMENT,
    ICON_SIGNAL,
)

from . import satellite1_radar_ns, Satellite1Radar, CONF_SATELLITE1_RADAR

# LD2410 sensor keys
CONF_LD2410_MOVING_DISTANCE = "ld2410_moving_distance"
CONF_LD2410_STILL_DISTANCE = "ld2410_still_distance"
CONF_LD2410_MOVING_ENERGY = "ld2410_moving_energy"
CONF_LD2410_STILL_ENERGY = "ld2410_still_energy"
CONF_LD2410_DETECTION_DISTANCE = "ld2410_detection_distance"
CONF_LD2410_LIGHT = "ld2410_light"

# LD2410 per-gate
CONF_LD2410_GATES = "ld2410_gates"
CONF_MOVE_ENERGY = "move_energy"
CONF_STILL_ENERGY = "still_energy"

# LD2450 sensor keys
CONF_LD2450_TARGET_COUNT = "ld2450_target_count"
CONF_LD2450_STILL_TARGET_COUNT = "ld2450_still_target_count"
CONF_LD2450_MOVING_TARGET_COUNT = "ld2450_moving_target_count"

CONF_LD2450_TARGETS = "ld2450_targets"
CONF_X = "x"
CONF_Y = "y"
CONF_SPEED = "speed"
CONF_ANGLE = "angle"
CONF_DISTANCE = "distance"
CONF_RESOLUTION = "resolution"

CONF_LD2450_ZONES = "ld2450_zones"
CONF_TARGET_COUNT = "target_count"
CONF_STILL_TARGET_COUNT = "still_target_count"
CONF_MOVING_TARGET_COUNT = "moving_target_count"

_TARGET_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_X): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_Y): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_SPEED): sensor.sensor_schema(
            unit_of_measurement="cm/s",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:speedometer",
        ),
        cv.Optional(CONF_ANGLE): sensor.sensor_schema(
            unit_of_measurement="°",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:angle-acute",
        ),
        cv.Optional(CONF_DISTANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_RESOLUTION): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)

_ZONE_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_TARGET_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:account-multiple",
        ),
        cv.Optional(CONF_STILL_TARGET_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:account",
        ),
        cv.Optional(CONF_MOVING_TARGET_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:account-arrow-right",
        ),
    }
)

_GATE_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MOVE_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_STILL_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_SIGNAL,
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SATELLITE1_RADAR): cv.use_id(Satellite1Radar),
        # LD2410 sensors
        cv.Optional(CONF_LD2410_MOVING_DISTANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_LD2410_STILL_DISTANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_LD2410_MOVING_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_LD2410_STILL_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_LD2410_DETECTION_DISTANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_LD2410_LIGHT): sensor.sensor_schema(
            device_class=DEVICE_CLASS_ILLUMINANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_LD2410_GATES): cv.Schema(
            {cv.Optional(f"g{i}"): _GATE_SENSOR_SCHEMA for i in range(9)}
        ),
        # LD2450 count sensors
        cv.Optional(CONF_LD2450_TARGET_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:account-multiple",
        ),
        cv.Optional(CONF_LD2450_STILL_TARGET_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:account",
        ),
        cv.Optional(CONF_LD2450_MOVING_TARGET_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:account-arrow-right",
        ),
        # LD2450 per-target and per-zone sensors
        cv.Optional(CONF_LD2450_TARGETS): cv.Schema(
            {cv.Optional(f"target_{i+1}"): _TARGET_SENSOR_SCHEMA for i in range(3)}
        ),
        cv.Optional(CONF_LD2450_ZONES): cv.Schema(
            {cv.Optional(f"zone_{i+1}"): _ZONE_SENSOR_SCHEMA for i in range(3)}
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SATELLITE1_RADAR])

    # LD2410 sensors
    for conf_key, setter in [
        (CONF_LD2410_MOVING_DISTANCE, hub.set_ld2410_moving_distance_sensor),
        (CONF_LD2410_STILL_DISTANCE, hub.set_ld2410_still_distance_sensor),
        (CONF_LD2410_MOVING_ENERGY, hub.set_ld2410_moving_energy_sensor),
        (CONF_LD2410_STILL_ENERGY, hub.set_ld2410_still_energy_sensor),
        (CONF_LD2410_DETECTION_DISTANCE, hub.set_ld2410_detection_distance_sensor),
        (CONF_LD2410_LIGHT, hub.set_ld2410_light_sensor),
    ]:
        if conf_key in config:
            s = await sensor.new_sensor(config[conf_key])
            cg.add(setter(s))

    # LD2410 per-gate sensors
    if CONF_LD2410_GATES in config:
        gates_conf = config[CONF_LD2410_GATES]
        for i in range(9):
            gate_key = f"g{i}"
            if gate_key in gates_conf:
                gate_conf = gates_conf[gate_key]
                if CONF_MOVE_ENERGY in gate_conf:
                    s = await sensor.new_sensor(gate_conf[CONF_MOVE_ENERGY])
                    cg.add(hub.set_ld2410_gate_move_energy_sensor(i, s))
                if CONF_STILL_ENERGY in gate_conf:
                    s = await sensor.new_sensor(gate_conf[CONF_STILL_ENERGY])
                    cg.add(hub.set_ld2410_gate_still_energy_sensor(i, s))

    # LD2450 count sensors
    for conf_key, setter in [
        (CONF_LD2450_TARGET_COUNT, hub.set_ld2450_target_count_sensor),
        (CONF_LD2450_STILL_TARGET_COUNT, hub.set_ld2450_still_target_count_sensor),
        (CONF_LD2450_MOVING_TARGET_COUNT, hub.set_ld2450_moving_target_count_sensor),
    ]:
        if conf_key in config:
            s = await sensor.new_sensor(config[conf_key])
            cg.add(setter(s))

    # LD2450 per-target sensors
    if CONF_LD2450_TARGETS in config:
        targets_conf = config[CONF_LD2450_TARGETS]
        for i in range(3):
            target_key = f"target_{i+1}"
            if target_key in targets_conf:
                tc = targets_conf[target_key]
                for field, setter_name in [
                    (CONF_X, "set_ld2450_target_x_sensor"),
                    (CONF_Y, "set_ld2450_target_y_sensor"),
                    (CONF_SPEED, "set_ld2450_target_speed_sensor"),
                    (CONF_ANGLE, "set_ld2450_target_angle_sensor"),
                    (CONF_DISTANCE, "set_ld2450_target_distance_sensor"),
                    (CONF_RESOLUTION, "set_ld2450_target_resolution_sensor"),
                ]:
                    if field in tc:
                        s = await sensor.new_sensor(tc[field])
                        cg.add(getattr(hub, setter_name)(i, s))

    # LD2450 per-zone sensors
    if CONF_LD2450_ZONES in config:
        zones_conf = config[CONF_LD2450_ZONES]
        for i in range(3):
            zone_key = f"zone_{i+1}"
            if zone_key in zones_conf:
                zc = zones_conf[zone_key]
                for field, setter_name in [
                    (CONF_TARGET_COUNT, "set_ld2450_zone_target_count_sensor"),
                    (CONF_STILL_TARGET_COUNT, "set_ld2450_zone_still_target_count_sensor"),
                    (CONF_MOVING_TARGET_COUNT, "set_ld2450_zone_moving_target_count_sensor"),
                ]:
                    if field in zc:
                        s = await sensor.new_sensor(zc[field])
                        cg.add(getattr(hub, setter_name)(i, s))
