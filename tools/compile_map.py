#!/usr/bin/env python3
import argparse
import json
import re
import struct
import tempfile
from pathlib import Path


TRAP_KIND_NAMES = {
    "falling_floor": "TRAP_FALLING_FLOOR",
    "bump_shooter": "TRAP_BUMP_SHOOTER",
    "question_block": "TRAP_QUESTION_BLOCK",
    "evasive_block": "TRAP_EVASIVE_BLOCK",
    "stage_spawner": "TRAP_STAGE_SPAWNER",
    "enter_pipe": "TRAP_ENTER_PIPE",
    "side_pipe": "TRAP_SIDE_PIPE",
    "checkpoint": "TRAP_CHECKPOINT",
    "goal": "TRAP_GOAL",
    "hint_block": "TRAP_HINT_BLOCK",
    "spike_block": "TRAP_SPIKE_BLOCK",
    "moving_platform": "TRAP_MOVING_PLATFORM",
    "pickup_item": "TRAP_PICKUP_ITEM",
    "fake_goal": "TRAP_FAKE_GOAL",
    "moving_block": "TRAP_MOVING_BLOCK",
    "breaking_platform": "TRAP_BREAKING_PLATFORM",
    "transition_zone": "TRAP_TRANSITION_ZONE",
    "coin": "TRAP_COIN",
    "jump_pad": "TRAP_JUMP_PAD",
    "breakable_block": "TRAP_BREAKABLE_BLOCK",
    "kill_zone": "TRAP_KILL_ZONE",
    "p_switch_block": "TRAP_P_SWITCH_BLOCK",
}

TRAP_SUBTYPE_NAMES = {
    "none": 0,
    "default": 0,
    "falling_on_pass_under": 51,
    "falling_when_below": 51,
    "falling_brick_group": 51,
    "falling_when_approached": 52,
    "stage_pipe_shot": 100,
    "stage_flying_enemy": 101,
    "stage_walker_swarm": 102,
    "stage_ghost_swarm": 102,
    "stage_upward_hazard": 181,
    "stage_upward_hazard_toggle": 182,
    "stage_upward_hazard_left": 188,
    "stage_hint_message": 187,
    "stage_pipe_hazard": 180,
    "question_enemy": 101,
    "question_good_mushroom": 102,
    "question_bad_mushroom": 103,
    "question_star": 104,
    "question_bad_flower": 105,
    "question_poison_generator": 110,
    "question_coin_generator": 112,
    "question_hidden_poison": 114,
    "evasive_vertical": 0,
    "evasive_horizontal": 1,
    "pipe_fake_kill": 0,
    "pipe_to_underground": 1,
    "hint_stage_1": 1,
    "platform_conveyor": 2,
    "platform_vertical_wrap": 5,
    "platform_bounce": 12,
    "warp_pad": 1,
    "fatal_launch": 2,
}

STAGE_DESTINATION_NAMES = {
    "none": 255,
    "1-1": 0,
    "1-2": 1,
    "1-2u": 2,
    "1-2b": 3,
    "1-3": 4,
    "1-3u": 5,
    "1-3s": 6,
}

TRAP_SPAWN_DIRECTION_NAMES = {
    "auto": 0,
    "default": 0,
    "left": -1,
    "right": 1,
}

BG_PALETTE_NAMES = {
    "normal": 0,
    "default": 0,
    "palette0": 0,
    "palette1": 1,
    "underground": 1,
    "palette2": 2,
    "palette3": 3,
    "palette4": 4,
    "palette5": 5,
    "palette6": 6,
    "palette7": 7,
    "palette8": 8,
    "palette9": 9,
    "palette10": 10,
    "palette11": 11,
    "palette12": 12,
    "palette13": 13,
    "palette14": 14,
    "palette15": 15,
}

ENTITY_KIND_NAMES = {
    "Player Start": 1,
    "PlayerStart": 1,
    "player_start": 1,
    "Enemy": 2,
    "enemy": 2,
    "Goal Flag": 3,
    "GoalFlag": 3,
    "goal_flag": 3,
    "Pipe Exit": 4,
    "PipeExit": 4,
    "pipe_exit": 4,
}

ENEMY_KIND_NAMES = {
    "none": "ENEMY_NONE",
    "walker": "ENEMY_WALKER",
    "shell_walker": "ENEMY_SHELL_WALKER",
    "jumper": "ENEMY_SHELL_WALKER",
    "shell": "ENEMY_SHELL",
    "static_hazard": "ENEMY_STATIC_HAZARD",
    "ceiling_faller": "ENEMY_CEILING_FALLER",
    "pipe_shot": "ENEMY_PIPE_SHOT",
    "firebar": "ENEMY_FIREBAR",
    "superjien": "ENEMY_SUPERJIEN",
    "super_jien": "ENEMY_SUPERJIEN",
    "jien_cannon": "ENEMY_JIEN_CANNON",
    "kuma": "ENEMY_KUMA",
    "giant": "ENEMY_GIANT",
    "defrag": "ENEMY_DEFRAG",
    "aramaki": "ENEMY_ARAMAKI",
    "laser": "ENEMY_LASER",
    "super_boon": "ENEMY_SUPER_BOON",
}

ENEMY_SPRITE_NAMES = {
    "walker": "ENEMY_SPRITE_WALKER",
    "ghost": "ENEMY_SPRITE_WALKER",
    "shell_walker": "ENEMY_SPRITE_SHELL_WALKER",
    "tall": "ENEMY_SPRITE_SHELL_WALKER",
    "shell": "ENEMY_SPRITE_SHELL",
    "hazard": "ENEMY_SPRITE_HAZARD",
    "atype3": "ENEMY_SPRITE_ATYPE3",
    "face_hidden": "ENEMY_SPRITE_FACE_HIDDEN",
    "face_grin": "ENEMY_SPRITE_FACE_GRIN",
    "cloud": "ENEMY_SPRITE_FACE_HIDDEN",
    "cloud_face": "ENEMY_SPRITE_FACE_GRIN",
    "nyassun": "ENEMY_SPRITE_NYASSUN",
    "nyassun_alert": "ENEMY_SPRITE_NYASSUN_ALERT",
    "kuma": "ENEMY_SPRITE_KUMA",
    "cuckoo32": "ENEMY_SPRITE_CUCKOO32",
    "fire_projectile": "ENEMY_SPRITE_FIRE_PROJECTILE",
    "superjien": "ENEMY_SPRITE_SUPERJIEN",
    "spiky_soldier": "ENEMY_SPRITE_SUPERJIEN",
    "jien_cannon": "ENEMY_SPRITE_JIEN_CANNON",
    "giant": "ENEMY_SPRITE_GIANT",
    "defrag": "ENEMY_SPRITE_DEFRAG",
    "defrag_stomped": "ENEMY_SPRITE_DEFRAG_STOMPED",
    "laser": "ENEMY_SPRITE_LASER",
    "super_boon": "ENEMY_SPRITE_SUPER_BOON",
}

FLIP_MASK = 0xF0000000
GBA_METATILE_SIZE = 16
SOURCE_ROW_OFFSET = 5
WORLD_Y_OFFSET_PX = SOURCE_ROW_OFFSET * GBA_METATILE_SIZE
FIX_SCALE = 1 << 16
MAX_MAP_WIDTH = 160
MAX_MAP_HEIGHT = 17
MAX_TRAPS = 48
MAX_ENEMY_SPAWNS = 64
MAX_DYNAMIC_COLLIDERS = 16
STAGE_MAPS = ("level1.tmj", "level1_2.tmj", "level1_2u.tmj",
              "level1_2b.tmj", "level1_3.tmj", "level1_3u.tmj", "level1_3s.tmj")

TRAP_SUBTYPES = {
    "TRAP_FALLING_FLOOR": {0, 51, 52},
    "TRAP_QUESTION_BLOCK": {0, 101, 102, 103, 104, 105, 110, 112, 114},
    "TRAP_EVASIVE_BLOCK": {0, 1},
    "TRAP_STAGE_SPAWNER": {0, 100, 101, 102, 180, 181, 182, 187, 188},
    "TRAP_ENTER_PIPE": {0, 1}, "TRAP_SIDE_PIPE": {0, 1},
    "TRAP_HINT_BLOCK": {0, 1}, "TRAP_MOVING_PLATFORM": {0, 2, 5, 12},
    "TRAP_JUMP_PAD": {0, 1, 2},
}
TRAP_SUBTYPE_NAMES_BY_KIND = {
    "TRAP_FALLING_FLOOR": {"falling_on_pass_under", "falling_when_below", "falling_brick_group", "falling_when_approached"},
    "TRAP_QUESTION_BLOCK": {"question_enemy", "question_good_mushroom", "question_bad_mushroom", "question_star", "question_bad_flower", "question_poison_generator", "question_coin_generator", "question_hidden_poison"},
    "TRAP_EVASIVE_BLOCK": {"evasive_vertical", "evasive_horizontal"},
    "TRAP_STAGE_SPAWNER": {"stage_pipe_shot", "stage_flying_enemy", "stage_walker_swarm", "stage_ghost_swarm", "stage_upward_hazard", "stage_upward_hazard_toggle", "stage_upward_hazard_left", "stage_hint_message", "stage_pipe_hazard"},
    "TRAP_ENTER_PIPE": {"pipe_fake_kill", "pipe_to_underground"},
    "TRAP_SIDE_PIPE": {"pipe_fake_kill", "pipe_to_underground"},
    "TRAP_HINT_BLOCK": {"hint_stage_1"},
    "TRAP_MOVING_PLATFORM": {"platform_conveyor", "platform_vertical_wrap", "platform_bounce"},
    "TRAP_JUMP_PAD": {"warp_pad", "fatal_launch"},
}

CHANNEL_LISTENER_KINDS = {
    "TRAP_FALLING_FLOOR", "TRAP_BUMP_SHOOTER", "TRAP_QUESTION_BLOCK",
    "TRAP_STAGE_SPAWNER", "TRAP_CHECKPOINT", "TRAP_GOAL", "TRAP_FAKE_GOAL",
    "TRAP_HINT_BLOCK", "TRAP_PICKUP_ITEM", "TRAP_MOVING_BLOCK",
}


class MapValidationError(ValueError):
    """An authoring error with enough context to fix it in Tiled."""


_validation_context = {
    "map": None,
    "layer": None,
    "object": None,
}


def set_validation_context(map_path=None, layer=None, obj=None):
    if map_path is not None:
        _validation_context["map"] = map_path
    _validation_context["layer"] = layer
    _validation_context["object"] = obj


def map_error(message, field=None):
    parts = []
    map_path = _validation_context["map"]
    if map_path is not None:
        parts.append(str(map_path))

    layer = _validation_context["layer"]
    if layer:
        parts.append(f"layer {layer!r}")

    obj = _validation_context["object"]
    if obj is not None:
        name = obj.get("name") or obj.get("class") or obj.get("type") or "unnamed object"
        object_id = obj.get("id")
        suffix = f" (id {object_id})" if object_id is not None else ""
        parts.append(f"object {name!r}{suffix}")

    if field:
        parts.append(f"property {field!r}")

    prefix = ": ".join(parts)
    return MapValidationError(f"{prefix}: {message}" if prefix else message)


def fixed(value, field=None):
    try:
        result = int(round(float(value) * FIX_SCALE))
        if not -(1 << 31) <= result < (1 << 31):
            raise ValueError("outside 16.16 fixed-point range")
        return result
    except (TypeError, ValueError, OverflowError) as exc:
        raise map_error(
            f"expected a number of pixels, got {value!r}", field) from exc


def integer(value, field, default=None):
    if value is None and default is not None:
        return default
    if isinstance(value, (dict, list)):
        raise map_error(f"expected an integer, got {value!r}", field)
    try:
        result = int(value)
        if isinstance(value, float) and value != result:
            raise ValueError("fractional integer")
        return result
    except (TypeError, ValueError) as exc:
        raise map_error(f"expected an integer, got {value!r}", field) from exc


def bounded(value, field, low, high):
    value = integer(value, field)
    if not low <= value <= high:
        raise map_error(f"must be between {low} and {high}, got {value}", field)
    return value


def palette_value(value, field):
    return bounded(enum_int(value, BG_PALETTE_NAMES, 0, field), field, 0, 3)


def enemy_palette_value(value, field):
    value = bounded(value, field, 0, 15)
    if value not in (0, 4):
        raise map_error(f"enemy palette bank {value} is not loaded; use 0 or 4", field)
    return value


def c_ident(name):
    stem = re.sub(r"[^0-9A-Za-z_]", "_", Path(name).stem)
    stem = re.sub(r"_+", "_", stem).strip("_").lower()
    if not stem or stem[0].isdigit():
        stem = "level_" + stem
    return stem


def throw_velocity_fixed(value):
    """Convert the original Defrag throw parameter to port fixed-point speed."""
    try:
        return int(round(float(value) * 16 * FIX_SCALE / (29 * 100 * 2)))
    except (TypeError, ValueError) as exc:
        raise map_error(
            f"expected a numeric Defrag throw speed, got {value!r}",
            "throw_vx") from exc


def prop_value(props, name, default=None):
    for prop in props or []:
        if prop.get("name") == name:
            return prop.get("value")
    return default


def c_string_literal(value):
    if value is None:
        return "0"

    text = str(value).replace("\\n", "\n")
    out = '"'
    for ch in text:
        if ch == "\\":
            out += "\\\\"
        elif ch == '"':
            out += '\\"'
        elif ch == "\n":
            out += "\\n"
        elif ch == "\r":
            continue
        elif ch == "\t":
            out += "\\t"
        else:
            out += ch
    out += '"'
    return out


def default_question_spawn_interval(subtype):
    if subtype == TRAP_SUBTYPE_NAMES["stage_pipe_hazard"]:
        return 48
    if subtype == TRAP_SUBTYPE_NAMES["question_coin_generator"]:
        return 3
    if subtype == TRAP_SUBTYPE_NAMES["question_poison_generator"]:
        return 16
    return 0


def default_question_spawn_limit(subtype):
    if subtype == TRAP_SUBTYPE_NAMES["question_coin_generator"]:
        return 20
    if subtype == TRAP_SUBTYPE_NAMES["question_poison_generator"]:
        return 0
    return 1


def default_goal_walk_frames(kind):
    return 207 if kind == "TRAP_GOAL" else 0


def tile_properties(map_data):
    props = {}
    for tileset in map_data.get("tilesets", []):
        firstgid = int(tileset.get("firstgid", 1))
        for tile in tileset.get("tiles", []):
            gid = firstgid + int(tile.get("id", 0))
            props[gid] = {p.get("name"): p.get("value")
                          for p in tile.get("properties", [])}
    return props


def iter_tile_layer_cells(layer, width, height):
    if "chunks" in layer:
        for chunk in layer["chunks"]:
            chunk_width = int(chunk["width"])
            chunk_height = int(chunk["height"])
            chunk_x = int(chunk["x"])
            chunk_y = int(chunk["y"])
            data = chunk.get("data", [])
            for y in range(chunk_height):
                for x in range(chunk_width):
                    map_x = chunk_x + x
                    map_y = chunk_y + y
                    if 0 <= map_x < width and 0 <= map_y < height:
                        yield map_x, map_y, int(data[y * chunk_width + x])
        return

    data = layer.get("data", [])
    for y in range(height):
        for x in range(width):
            yield x, y, int(data[y * width + x])


def decode_gid(raw_gid):
    return int(raw_gid) & ~FLIP_MASK


def metatile_from_gid(gid, props):
    if gid == 0:
        return 0

    tile_props = props.get(gid, {})
    if "metatile" in tile_props:
        return int(tile_props["metatile"])
    if "metatile_id" in tile_props:
        return int(tile_props["metatile_id"])

    return gid - 1


def palette_from_gid(gid, props, default=0):
    if gid == 0:
        return 0
    return enum_int(props.get(gid, {}).get("palette"), BG_PALETTE_NAMES, default)


def layer_properties(layer):
    return {p.get("name"): p.get("value") for p in layer.get("properties", [])}


def map_properties(map_data):
    return {p.get("name"): p.get("value") for p in map_data.get("properties", [])}


def source_from_gid(gid, props):
    if gid == 0:
        return 0
    return int(props.get(gid, {}).get("source", 0))


def trap_kind(value):
    if value is None:
        return None
    text = str(value)
    if text in TRAP_KIND_NAMES.values():
        return text
    if text.lower() not in TRAP_KIND_NAMES:
        raise map_error(f"unknown value {text!r}; expected one of {', '.join(sorted(TRAP_KIND_NAMES))}", "trap_type")
    return TRAP_KIND_NAMES[text.lower()]


def enum_name(value, names, default, field=None):
    if value is None:
        return default
    if isinstance(value, (dict, list)):
        raise map_error(f"expected one of {', '.join(sorted(names))}, got {value!r}", field)
    text = str(value).strip()
    if not text:
        return default
    if text in names.values():
        return text
    key = text.lower()
    if key in names:
        return names[key]
    raise map_error(f"unknown value {text!r}; expected one of {', '.join(sorted(names))}", field)


def enum_int(value, names, default, field=None):
    if value is None:
        return default
    if isinstance(value, (dict, list)):
        raise map_error(
            f"expected one of {', '.join(sorted(names))}, got structured value {value!r}",
            field)

    if isinstance(value, bool):
        return int(value)

    if isinstance(value, (int, float)):
        return int(value)

    text = str(value).strip()
    if not text:
        return default

    try:
        return int(text, 0)
    except ValueError:
        pass

    key = text.lower()
    if key in names:
        return names[key]

    key = re.sub(r"[^0-9a-zA-Z_]+", "_", text).strip("_").lower()
    if key in names:
        return names[key]

    raise map_error(f"unknown value {text!r}; expected one of {', '.join(sorted(names))}", field)


def object_name(obj):
    return obj.get("class") or obj.get("type") or obj.get("name") or ""


def validate_map_format(map_data, width, height):
    if map_data.get("orientation") != "orthogonal":
        raise map_error("only orthogonal maps are supported", "orientation")
    if (map_data.get("tilewidth"), map_data.get("tileheight")) != (16, 16):
        raise map_error("map tiles must be 16 x 16 pixels")
    if map_data.get("infinite", False):
        raise map_error("infinite maps are not supported", "infinite")
    tilesets = map_data.get("tilesets", [])
    if len(tilesets) != 1 or tilesets[0].get("firstgid", 1) != 1:
        raise map_error("exactly one embedded tileset with firstgid 1 is supported", "tilesets")
    for layer in map_data.get("layers", []):
        set_validation_context(layer=layer.get("name"), obj=None)
        kind = layer.get("type")
        if kind not in ("tilelayer", "objectgroup"):
            raise map_error(f"layer type {kind!r} is not supported; ungroup layers before export")
        if any(layer.get(key, 0) != 0 for key in ("x", "y", "offsetx", "offsety")):
            raise map_error("layer position and offsets must be zero; move its contents instead")
        if layer.get("opacity", 1) != 1 or any(layer.get(key, 1) != 1 for key in ("parallaxx", "parallaxy")):
            raise map_error("layer opacity and parallax are not supported")
        if kind == "tilelayer":
            if "chunks" in layer or layer.get("encoding") or layer.get("compression"):
                raise map_error("chunked, encoded, or compressed tile layers are not supported")
            if (layer.get("width", width), layer.get("height", height)) != (width, height):
                raise map_error(f"tile layer dimensions must match the map ({width} x {height})")
            data = layer.get("data")
            if not isinstance(data, list) or len(data) != width * height:
                raise map_error(f"tile layer data must contain exactly {width * height} cells")
            for index, gid in enumerate(data):
                if not isinstance(gid, int) or isinstance(gid, bool) or not 0 <= gid <= 0xffffffff:
                    raise map_error(f"tile ({index % width}, {index // width}) has an invalid GID")
                if gid & FLIP_MASK:
                    raise map_error(f"tile ({index % width}, {index // width}) uses unsupported flip/rotation flags")
        else:
            for obj in layer.get("objects", []):
                set_validation_context(layer=layer.get("name"), obj=obj)
                if obj.get("rotation", 0) != 0:
                    raise map_error("object rotation is not supported", "rotation")
                if any(obj.get(shape) for shape in ("ellipse", "point", "polygon", "polyline", "text")):
                    raise map_error("only rectangular objects are supported")
                if int(obj.get("gid", 0)) & FLIP_MASK:
                    raise map_error("tile object flip/rotation flags are not supported", "gid")
    set_validation_context(layer=None, obj=None)


def parse_object_layers(map_data, tile_props, metatile_count=85, map_path=None):
    objects = []
    enemy_spawns = []
    traps = []
    trap_origins = []
    dynamic_colliders = 0

    for layer in map_data.get("layers", []):
        if layer.get("type") != "objectgroup":
            continue

        layer_name = layer.get("name", "")
        for obj in layer.get("objects", []):
            set_validation_context(layer=layer_name, obj=obj)
            props = obj.get("properties", [])
            gid = decode_gid(obj.get("gid", 0))
            tile_prop = tile_props.get(gid, {})
            name = object_name(obj)
            trap = trap_kind(prop_value(props, "trap_type",
                            tile_prop.get("trap_type")))

            try:
                tiled_x = float(obj.get("x", 0))
                tiled_y = float(obj.get("y", 0))
            except (TypeError, ValueError) as exc:
                raise map_error("object x and y must be numeric pixel coordinates") from exc
            world_y = tiled_y - WORLD_Y_OFFSET_PX
            x = fixed(tiled_x)
            y = fixed(world_y)
            w = fixed(obj.get("width", GBA_METATILE_SIZE))
            h = fixed(obj.get("height", GBA_METATILE_SIZE))
            subtype_value = prop_value(props, "subtype", tile_prop.get("subtype"))
            subtype = enum_int(subtype_value,
                               TRAP_SUBTYPE_NAMES, 0, "subtype")
            if trap:
                if subtype not in TRAP_SUBTYPES.get(trap, {0}):
                    raise map_error(f"subtype {subtype_value!r} is not valid for {trap}", "subtype")
                if isinstance(subtype_value, str) and subtype_value.lower() in TRAP_SUBTYPE_NAMES:
                    if subtype_value.lower() not in {"none", "default"} | TRAP_SUBTYPE_NAMES_BY_KIND.get(trap, set()):
                        raise map_error(f"subtype {subtype_value!r} is not valid for {trap}", "subtype")
            spawn_dir = enum_int(prop_value(props, "spawn_dir",
                                            tile_prop.get("spawn_dir")),
                                 TRAP_SPAWN_DIRECTION_NAMES, 0, "spawn_dir")
            bounded(spawn_dir, "spawn_dir", -1, 1)
            destination_value = prop_value(props, "stage_destination",
                                           tile_prop.get("stage_destination"))
            destination = enum_int(destination_value, STAGE_DESTINATION_NAMES, 255,
                                   "stage_destination")
            entry_id = bounded(prop_value(props, "entry_id", tile_prop.get("entry_id", 0)), "entry_id", 0, 255)
            spawn_interval = integer(prop_value(
                props, "spawn_interval",
                tile_prop.get("spawn_interval",
                              default_question_spawn_interval(subtype))), "spawn_interval")
            spawn_limit = integer(prop_value(
                props, "spawn_limit",
                tile_prop.get("spawn_limit",
                              default_question_spawn_limit(subtype))), "spawn_limit")
            item_variant = integer(prop_value(
                props, "item_variant",
                tile_prop.get("item_variant", 0)), "item_variant")
            trigger_channel = integer(prop_value(
                props, "trigger_channel",
                tile_prop.get("trigger_channel", 0)), "trigger_channel")
            listen_channel = integer(prop_value(
                props, "listen_channel",
                tile_prop.get("listen_channel", 0)), "listen_channel")
            channel_spawn_interval = integer(prop_value(
                props, "channel_spawn_interval",
                tile_prop.get("channel_spawn_interval", 0)), "channel_spawn_interval")
            trigger_delay = integer(prop_value(
                props, "trigger_delay",
                tile_prop.get("trigger_delay", 0)), "trigger_delay")
            goal_walk_frames = integer(prop_value(
                props, "goal_walk_frames",
                tile_prop.get("goal_walk_frames",
                              default_goal_walk_frames(trap))), "goal_walk_frames")
            hint_text = prop_value(props, "hint_text",
                                   tile_prop.get("hint_text"))
            source_x = int(round(tiled_x / GBA_METATILE_SIZE))
            source_y = int(round(tiled_y / GBA_METATILE_SIZE))
            for field in ("trigger_x", "trigger_y"):
                if prop_value(props, field, tile_prop.get(field)) is not None:
                    raise map_error("move the object rectangle to set the trigger position", field)
            vx = fixed(prop_value(props, "vx", tile_prop.get("vx", 0)) or 0)
            vy = fixed(prop_value(props, "vy", tile_prop.get("vy", 0)) or 0)
            gravity = fixed(prop_value(props, "gravity", tile_prop.get("gravity", 0)) or 0)
            move_offset_x = fixed(prop_value(
                props, "move_offset_x", tile_prop.get("move_offset_x", 0)) or 0)
            move_offset_y = fixed(prop_value(
                props, "move_offset_y", tile_prop.get("move_offset_y", 0)) or 0)
            spawn_enemy_kind = enum_name(prop_value(
                props, "spawn_enemy_kind", tile_prop.get("spawn_enemy_kind")),
                ENEMY_KIND_NAMES, "ENEMY_NONE", "spawn_enemy_kind")
            spawn_enemy_sprite = enum_name(prop_value(
                props, "spawn_enemy_sprite", tile_prop.get("spawn_enemy_sprite")),
                ENEMY_SPRITE_NAMES, "ENEMY_SPRITE_WALKER", "spawn_enemy_sprite")
            spawn_enemy_palette = integer(prop_value(
                props, "spawn_enemy_palette",
                tile_prop.get("spawn_enemy_palette",
                              tile_prop.get("enemy_palette", 0))), "spawn_enemy_palette")
            spawn_enemy_param = integer(prop_value(
                props, "spawn_enemy_param",
                tile_prop.get("spawn_enemy_param",
                              tile_prop.get("enemy_param", 0))), "spawn_enemy_param")
            spawn_enemy_count = integer(prop_value(
                props, "spawn_enemy_count",
                tile_prop.get("spawn_enemy_count", 1)), "spawn_enemy_count")
            spawn_sound = integer(prop_value(
                props, "spawn_sound", tile_prop.get("spawn_sound", 1)), "spawn_sound")
            spawn_offset_x = fixed(prop_value(
                props, "spawn_offset_x", tile_prop.get("spawn_offset_x", 0)) or 0)
            spawn_offset_y = fixed(prop_value(
                props, "spawn_offset_y", tile_prop.get("spawn_offset_y", 0)) or 0)
            spawn_vx = fixed(prop_value(
                props, "spawn_vx", tile_prop.get("spawn_vx", 0)) or 0)
            spawn_vy = fixed(prop_value(
                props, "spawn_vy", tile_prop.get("spawn_vy", 0)) or 0)
            spawn_random_vx = fixed(prop_value(
                props, "spawn_random_vx", tile_prop.get("spawn_random_vx", 0)) or 0)
            spawn_random_vy = fixed(prop_value(
                props, "spawn_random_vy", tile_prop.get("spawn_random_vy", 0)) or 0)
            spawn_spacing_x = fixed(prop_value(
                props, "spawn_spacing_x", tile_prop.get("spawn_spacing_x", 0)) or 0)
            spawn_spacing_y = fixed(prop_value(
                props, "spawn_spacing_y", tile_prop.get("spawn_spacing_y", 0)) or 0)
            repeat_rearm_offset_x = fixed(prop_value(
                props, "repeat_rearm_offset_x",
                tile_prop.get("repeat_rearm_offset_x", 0)) or 0)
            repeat_rearm_offset_y = fixed(prop_value(
                props, "repeat_rearm_offset_y",
                tile_prop.get("repeat_rearm_offset_y", 0)) or 0)

            for axis in ("x", "y"):
                field = "spawn_" + axis
                if prop_value(props, field, tile_prop.get(field)) is not None:
                    raise map_error(f"absolute spawn positions are not supported; use spawn_offset_{axis} relative to the object", field)

            if layer_name.lower().startswith("enem") or name.lower() == "enemy":
                if len(enemy_spawns) >= MAX_ENEMY_SPAWNS:
                    raise map_error(f"exceeds the limit of {MAX_ENEMY_SPAWNS} enemy spawns")
                enemy_kind = enum_name(prop_value(props, "enemy_kind",
                                       tile_prop.get("enemy_kind")),
                                       ENEMY_KIND_NAMES, "ENEMY_WALKER", "enemy_kind")
                enemy_sprite = enum_name(prop_value(props, "enemy_sprite",
                                        tile_prop.get("enemy_sprite")),
                                         ENEMY_SPRITE_NAMES, "ENEMY_SPRITE_WALKER",
                                         "enemy_sprite")

                if (enemy_kind == "ENEMY_WALKER" and
                        enemy_sprite == "ENEMY_SPRITE_SHELL_WALKER"):
                    enemy_kind = "ENEMY_SHELL_WALKER"

                enemy_spawns.append({
                    "kind": enemy_kind,
                    "sprite": enemy_sprite,
                    "dir": bounded(prop_value(props, "dir", tile_prop.get("dir", -1)), "dir", -1, 1),
                    "w": bounded(round(float(obj.get("width", 16))), "width", 1, 255),
                    "h": bounded(round(float(obj.get("height", 16))), "height", 1, 255),
                    "palette": enemy_palette_value(prop_value(
                        props, "enemy_palette",
                        tile_prop.get("enemy_palette", 0)), "enemy_palette"),
                    "param": bounded(prop_value(
                        props, "enemy_param",
                        tile_prop.get("enemy_param", 0)), "enemy_param", 0, 255),
                    "throw_vx": throw_velocity_fixed(prop_value(
                        props, "throw_vx", tile_prop.get("throw_vx", 700))),
                    "x": x,
                    "y": y,
                })
                continue

            if trap or layer_name.lower().startswith("trap"):
                if trap is None:
                    raise map_error("trap objects require a trap_type", "trap_type")
                if len(traps) >= MAX_TRAPS:
                    raise map_error(f"exceeds the limit of {MAX_TRAPS} traps")
                if (trap in {"TRAP_ENTER_PIPE", "TRAP_SIDE_PIPE", "TRAP_EVASIVE_BLOCK", "TRAP_MOVING_PLATFORM", "TRAP_BREAKING_PLATFORM", "TRAP_MOVING_BLOCK"} or
                    (trap == "TRAP_JUMP_PAD" and subtype == 0)):
                    dynamic_colliders += 1
                    if dynamic_colliders > MAX_DYNAMIC_COLLIDERS:
                        raise map_error(f"exceeds the limit of {MAX_DYNAMIC_COLLIDERS} dynamic colliders")
                needs_destination = (
                    trap == "TRAP_TRANSITION_ZONE" or
                    (trap in ("TRAP_ENTER_PIPE", "TRAP_SIDE_PIPE") and subtype == 1)
                )
                if needs_destination and destination == 255:
                    raise map_error(
                        "must be a StageDestination value (for example '1-3u'), not an "
                        "empty value or an object/class reference",
                        "stage_destination")
                if destination != 255:
                    bounded(destination, "stage_destination", 0, len(STAGE_MAPS) - 1)
                if needs_destination and map_path is not None:
                    destination_path = map_path.parent / STAGE_MAPS[destination]
                    try:
                        target_map = json.loads(destination_path.read_text())
                    except (OSError, ValueError) as exc:
                        raise map_error(f"cannot read destination {destination_path}: {exc}", "stage_destination") from exc
                    exits = [o for layer in target_map.get("layers", [])
                             for o in layer.get("objects", [])
                             if object_name(o) in ("Pipe Exit", "PipeExit", "pipe_exit")]
                    if entry_id and not any(integer(prop_value(o.get("properties", []), "entry_id", 0), "entry_id") == entry_id for o in exits):
                        raise map_error(f"destination {destination_path.name} has no Pipe Exit with entry_id {entry_id}", "entry_id")
                    has_default_exit = any(integer(prop_value(o.get("properties", []), "entry_id", 0), "entry_id") == 0 for o in exits)
                    if not entry_id and not has_default_exit and not any(object_name(o) in ("Player Start", "PlayerStart", "player_start") for layer in target_map.get("layers", []) for o in layer.get("objects", [])):
                        raise map_error(f"destination {destination_path.name} has no default entry", "stage_destination")
                visual_metatile = integer(prop_value(props, "visual_metatile", tile_prop.get("visual_metatile", 255)), "visual_metatile")
                spent_metatile = integer(prop_value(props, "spent_metatile", tile_prop.get("spent_metatile", 255)), "spent_metatile")
                for field, value in (("visual_metatile", visual_metatile), ("spent_metatile", spent_metatile)):
                    if value != 255:
                        bounded(value, field, 0, metatile_count - 1)
                collision = integer(prop_value(props, "collision", tile_prop.get("collision", 255)), "collision")
                if collision not in (0, 1, 2, 3, 255):
                    raise map_error(f"invalid collision {collision}", "collision")
                if listen_channel and trap not in CHANNEL_LISTENER_KINDS:
                    raise map_error(f"{trap} cannot be activated by a channel", "listen_channel")
                trap_origins.append((layer_name, obj))
                traps.append({
                    "kind": trap or "TRAP_QUESTION_BLOCK",
                    "subtype": subtype,
                    "destination": destination,
                    "entry_id": entry_id,
                    "spawn_dir": spawn_dir,
                    "visual_palette": palette_value(prop_value(props, "visual_palette",
                                                tile_prop.get("visual_palette",
                                                              tile_prop.get("palette"))),
                                               "visual_palette"),
                    "spawn_interval": bounded(spawn_interval, "spawn_interval", 0, 255),
                    "spawn_limit": bounded(spawn_limit, "spawn_limit", 0, 255),
                    "item_variant": bounded(item_variant, "item_variant", 0, 255),
                    "spawn_enemy_kind": spawn_enemy_kind,
                    "spawn_enemy_sprite": spawn_enemy_sprite,
                    "spawn_enemy_palette": enemy_palette_value(spawn_enemy_palette, "spawn_enemy_palette"),
                    "spawn_enemy_param": bounded(spawn_enemy_param, "spawn_enemy_param", 0, 255),
                    "spawn_enemy_count": bounded(spawn_enemy_count, "spawn_enemy_count", 0, 255),
                    "spawn_sound": bounded(spawn_sound, "spawn_sound", 0, 1),
                    "trigger_channel": bounded(trigger_channel, "trigger_channel", 0, 255),
                    "listen_channel": bounded(listen_channel, "listen_channel", 0, 255),
                    "channel_spawn_interval": bounded(channel_spawn_interval, "channel_spawn_interval", 0, 255),
                    "trigger_delay": bounded(trigger_delay, "trigger_delay", 0, 255),
                    "spawn_offset_x": spawn_offset_x,
                    "spawn_offset_y": spawn_offset_y,
                    "spawn_vx": spawn_vx,
                    "spawn_vy": spawn_vy,
                    "spawn_random_vx": spawn_random_vx,
                    "spawn_random_vy": spawn_random_vy,
                    "spawn_spacing_x": spawn_spacing_x,
                    "spawn_spacing_y": spawn_spacing_y,
                    "repeat_rearm_offset_x": repeat_rearm_offset_x,
                    "repeat_rearm_offset_y": repeat_rearm_offset_y,
                    "move_offset_x": move_offset_x,
                    "move_offset_y": move_offset_y,
                    "goal_walk_frames": bounded(goal_walk_frames, "goal_walk_frames", 0, 65535),
                    "hint_text": c_string_literal(hint_text),
                    "visual_metatile": visual_metatile,
                    "spent_metatile": spent_metatile,
                    "collision": collision,
                    "hidden": bounded(prop_value(props, "hidden", tile_prop.get("hidden", 0)), "hidden", 0, 1),
                    "source_x": source_x,
                    "source_y": source_y,
                    "x": x,
                    "y": y,
                    "w": w,
                    "h": h,
                    "vx": vx,
                    "vy": vy,
                    "gravity": gravity,
                })
                continue

            entity_kind = int(prop_value(props, "entity_kind",
                              ENTITY_KIND_NAMES.get(name, 0)))
            if entity_kind:
                entity_subtype = subtype
                if entity_kind == ENTITY_KIND_NAMES["Player Start"]:
                    checkpoint = prop_value(props, "checkpoint", True)
                    entity_subtype = int(str(checkpoint).lower() not in
                                         ("0", "false", "no", "off"))
                objects.append({
                    "kind": entity_kind,
                    "subtype": entity_subtype,
                    "entry_id": entry_id,
                    "x": x,
                    "y": y,
                    "w": w,
                    "h": h,
                })

    edges = {}
    for trap, origin in zip(traps, trap_origins):
        if trap["listen_channel"] and trap["trigger_channel"]:
            edges.setdefault(trap["listen_channel"], []).append((trap["trigger_channel"], origin))
    visiting = set()
    visited = set()

    def check_channel(channel):
        visiting.add(channel)
        for target, (layer_name, obj) in edges.get(channel, []):
            if target in visiting:
                set_validation_context(layer=layer_name, obj=obj)
                raise map_error(f"channel link {channel} -> {target} creates a cycle", "trigger_channel")
            if target not in visited:
                check_channel(target)
        visiting.remove(channel)
        visited.add(channel)

    for channel in edges:
        if channel not in visited:
            check_channel(channel)
    set_validation_context(layer=None, obj=None)
    return objects, enemy_spawns, traps


def format_u8_array(name, rows):
    out = [f"const uint8_t {name}[{len(rows)}][{len(rows[0])}] = {{"]
    for row in rows:
        out.append("    { " + ", ".join(str(v) for v in row) + " },")
    out.append("};")
    return "\n".join(out)


def write_outputs(tmj_path, header_path, source_path, symbol_prefix=None):
    set_validation_context(map_path=tmj_path, layer=None, obj=None)
    try:
        map_data = json.loads(tmj_path.read_text())
    except OSError as exc:
        raise map_error(f"could not read map: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise map_error(
            f"invalid JSON at line {exc.lineno}, column {exc.colno}: {exc.msg}") from exc

    if not isinstance(map_data, dict):
        raise map_error("map root must be a JSON object")
    try:
        width = int(map_data["width"])
        height = int(map_data["height"])
    except (KeyError, TypeError, ValueError) as exc:
        raise map_error("map width and height must be integer properties") from exc
    if width <= 0 or height <= 0:
        raise map_error(f"map dimensions must be positive, got {width} x {height}")
    if width > MAX_MAP_WIDTH or height > MAX_MAP_HEIGHT:
        raise map_error(f"map dimensions {width} x {height} exceed runtime limit {MAX_MAP_WIDTH} x {MAX_MAP_HEIGHT}")
    validate_map_format(map_data, width, height)
    ids = set()
    for layer in map_data.get("layers", []):
        for obj in layer.get("objects", []):
            set_validation_context(layer=layer.get("name"), obj=obj)
            object_id = integer(obj.get("id"), "id")
            if object_id in ids:
                raise map_error(f"duplicate object id {object_id}", "id")
            ids.add(object_id)
    set_validation_context(layer=None, obj=None)
    if ids and integer(map_data.get("nextobjectid"), "nextobjectid") <= max(ids):
        raise map_error(f"must be greater than existing object id {max(ids)}", "nextobjectid")
    map_props = map_properties(map_data)
    camera_margin_top = bounded(map_props.get("camera_margin_top", 0), "camera_margin_top", 0, 65535)
    camera_margin_bottom = bounded(map_props.get("camera_margin_bottom", 0), "camera_margin_bottom", 0, 65535)
    tileset_ranges = []
    for tileset in map_data.get("tilesets", []):
        if tileset.get("source"):
            raise map_error("external tilesets are not supported; embed the tileset in the map")
        image = tileset.get("image")
        if not image:
            raise map_error("tileset must specify an image")
        image_path = tmj_path.parent / image
        try:
            with image_path.open("rb") as png:
                header = png.read(24)
            if header[:8] != b"\x89PNG\r\n\x1a\n" or len(header) < 24:
                raise ValueError("not a PNG image")
            image_width, image_height = struct.unpack(">II", header[16:24])
        except (OSError, ValueError) as exc:
            raise map_error(f"cannot read tileset image {image!r}: {exc}") from exc
        count = (image_width // 16) * (image_height // 16)
        if (tileset.get("imagewidth"), tileset.get("imageheight"), tileset.get("tilecount")) != (image_width, image_height, count):
            raise map_error(f"tileset {tileset.get('name')!r} metadata does not match image {image_width}x{image_height}, {count} tiles")
        firstgid = int(tileset.get("firstgid", 1))
        tileset_ranges.append((firstgid, firstgid + count))
    props = tile_properties(map_data)

    metatiles = [[0 for _ in range(width)] for _ in range(height)]
    palettes = [[0 for _ in range(width)] for _ in range(height)]
    source = [[0 for _ in range(width)] for _ in range(height)]
    switch_cells = [[0 for _ in range(width)] for _ in range(height)]

    for layer in map_data.get("layers", []):
        if layer.get("type") != "tilelayer":
            continue
        layer_name = layer.get("name", "").lower()
        set_validation_context(layer=layer.get("name"), obj=None)
        is_switch_layer = layer_name == "p-switch blocks"
        is_source_layer = layer_name in ("source", "reference", "originalsource",
                                         "original_source")
        if layer.get("visible", True) is False and not is_source_layer and not is_switch_layer:
            continue

        for x, y, raw_gid in iter_tile_layer_cells(layer, width, height):
            gid = decode_gid(raw_gid)
            if gid == 0:
                continue
            if not any(first <= gid < end for first, end in tileset_ranges):
                raise map_error(f"tile ({x}, {y}) uses unknown GID {gid}")
            if is_switch_layer:
                switch_cells[y][x] = 1
            elif is_source_layer:
                source[y][x] = source_from_gid(gid, props) or (gid - 1)
            else:
                layer_palette = palette_value(layer_properties(layer).get("palette"), "palette")
                metatiles[y][x] = bounded(metatile_from_gid(gid, props), "metatile", 0, count - 1)
                palettes[y][x] = bounded(palette_from_gid(gid, props, layer_palette), "palette", 0, 3)

    if not tileset_ranges:
        raise map_error("map must contain an embedded tileset")
    objects, enemy_spawns, traps = parse_object_layers(map_data, props, count, tmj_path)
    prefix = symbol_prefix or c_ident(tmj_path.name)
    guard = f"GENERATED_{prefix.upper()}_DATA_H"

    header_path.parent.mkdir(parents=True, exist_ok=True)
    source_path.parent.mkdir(parents=True, exist_ok=True)

    header_path.write_text(f"""#ifndef {guard}
#define {guard}

#include <stdint.h>

#include \"enemy.h\"
#include \"fixed.h\"
#include \"traps.h\"

#ifndef GENERATED_MAP_TYPES_H
#define GENERATED_MAP_TYPES_H

typedef struct GeneratedMapObject {{
    uint8_t kind;
    uint8_t subtype;
    uint8_t entry_id;
    fix16_t x;
    fix16_t y;
    fix16_t w;
    fix16_t h;
}} GeneratedMapObject;

enum {{
    GENERATED_OBJECT_NONE = 0,
    GENERATED_OBJECT_PLAYER_START = 1,
    GENERATED_OBJECT_ENEMY = 2,
    GENERATED_OBJECT_GOAL_FLAG = 3,
    GENERATED_OBJECT_PIPE_EXIT = 4,
}};

typedef struct GeneratedEnemySpawn {{
    EnemyKind kind;
    uint8_t sprite;
    int8_t dir;
    uint8_t w;
    uint8_t h;
    uint8_t palette;
    uint8_t param;
    fix16_t throw_vx;
    fix16_t x;
    fix16_t y;
}} GeneratedEnemySpawn;

#endif

#define {prefix.upper()}_WIDTH {width}
#define {prefix.upper()}_HEIGHT {height}
#define {prefix.upper()}_CAMERA_MARGIN_TOP {camera_margin_top}
#define {prefix.upper()}_CAMERA_MARGIN_BOTTOM {camera_margin_bottom}

extern const uint8_t {prefix}_metatiles[{height}][{width}];
extern const uint8_t {prefix}_palettes[{height}][{width}];
extern const uint8_t {prefix}_source[{height}][{width}];
extern const uint8_t {prefix}_switch_cells[{height}][{width}];
extern const GeneratedMapObject {prefix}_objects[{max(1, len(objects))}];
extern const uint16_t {prefix}_object_count;
extern const GeneratedEnemySpawn {prefix}_enemy_spawns[{max(1, len(enemy_spawns))}];
extern const uint16_t {prefix}_enemy_spawn_count;
extern const TrapTrigger {prefix}_traps[{max(1, len(traps))}];
extern const uint16_t {prefix}_trap_count;

#endif
""")

    lines = [
        f'#include \"generated/{prefix}_data.h\"',
        "",
        format_u8_array(f"{prefix}_metatiles", metatiles),
        "",
        format_u8_array(f"{prefix}_palettes", palettes),
        "",
        format_u8_array(f"{prefix}_source", source),
        "",
        format_u8_array(f"{prefix}_switch_cells", switch_cells),
        "",
        f"const GeneratedMapObject {prefix}_objects[{max(1, len(objects))}] = {{",
    ]
    if objects:
        for obj in objects:
            lines.append("    { %d, %d, %d, %d, %d, %d, %d }," %
                         (obj["kind"], obj["subtype"], obj["entry_id"], obj["x"],
                          obj["y"], obj["w"], obj["h"]))
    else:
        lines.append("    { 0, 0, 0, 0, 0, 0, 0 },")
    lines.extend([
        "};",
        f"const uint16_t {prefix}_object_count = {len(objects)};",
        "",
        f"const GeneratedEnemySpawn {prefix}_enemy_spawns[{max(1, len(enemy_spawns))}] = {{",
    ])
    if enemy_spawns:
        for spawn in enemy_spawns:
            lines.append("    { %s, %s, %d, %d, %d, %d, %d, %d, %d, %d }," %
                         (spawn["kind"], spawn["sprite"], spawn["dir"],
                          spawn["w"], spawn["h"], spawn["palette"],
                          spawn["param"], spawn["throw_vx"], spawn["x"], spawn["y"]))
    else:
        lines.append("    { ENEMY_NONE, 0, 0, 16, 16, 0, 0, 0, 0, 0 },")
    lines.extend([
        "};",
        f"const uint16_t {prefix}_enemy_spawn_count = {len(enemy_spawns)};",
        "",
        f"const TrapTrigger {prefix}_traps[{max(1, len(traps))}] = {{",
    ])
    if traps:
        for trap in traps:
            lines.extend([
                "    {",
                f"        .kind = {trap['kind']}, .subtype = {trap['subtype']}, .destination = {trap['destination']}, .entry_id = {trap['entry_id']}, .spawn_dir = {trap['spawn_dir']},",
                f"        .visual_palette = {trap['visual_palette']}, .spawn_interval = {trap['spawn_interval']},",
                f"        .spawn_limit = {trap['spawn_limit']}, .item_variant = {trap['item_variant']},",
                f"        .spawn_enemy_kind = {trap['spawn_enemy_kind']}, .spawn_enemy_sprite = {trap['spawn_enemy_sprite']},",
                f"        .spawn_enemy_palette = {trap['spawn_enemy_palette']}, .spawn_enemy_param = {trap['spawn_enemy_param']},",
                f"        .spawn_enemy_count = {trap['spawn_enemy_count']}, .spawn_sound = {trap['spawn_sound']},",
                f"        .trigger_channel = {trap['trigger_channel']}, .listen_channel = {trap['listen_channel']},",
                f"        .channel_spawn_interval = {trap['channel_spawn_interval']}, .trigger_delay = {trap['trigger_delay']},",
                f"        .goal_walk_frames = {trap['goal_walk_frames']},",
                f"        .hint_text = {trap['hint_text']}, .visual_metatile = {trap['visual_metatile']},",
                f"        .spent_metatile = {trap['spent_metatile']}, .collision = {trap['collision']}, .hidden = {trap['hidden']},",
                f"        .source_x = {trap['source_x']}, .source_y = {trap['source_y']},",
                f"        .x = {trap['x']}, .y = {trap['y']}, .w = {trap['w']}, .h = {trap['h']},",
                f"        .vx = {trap['vx']}, .vy = {trap['vy']}, .gravity = {trap['gravity']},",
                f"        .spawn_offset_x = {trap['spawn_offset_x']}, .spawn_offset_y = {trap['spawn_offset_y']},",
                f"        .spawn_vx = {trap['spawn_vx']}, .spawn_vy = {trap['spawn_vy']},",
                f"        .spawn_random_vx = {trap['spawn_random_vx']}, .spawn_random_vy = {trap['spawn_random_vy']},",
                f"        .spawn_spacing_x = {trap['spawn_spacing_x']}, .spawn_spacing_y = {trap['spawn_spacing_y']},",
                f"        .repeat_rearm_offset_x = {trap['repeat_rearm_offset_x']}, .repeat_rearm_offset_y = {trap['repeat_rearm_offset_y']},",
                f"        .move_offset_x = {trap['move_offset_x']}, .move_offset_y = {trap['move_offset_y']},",
                "    },",
            ])
    else:
        lines.append("    { .kind = TRAP_QUESTION_BLOCK },")
    lines.extend([
        "};",
        f"const uint16_t {prefix}_trap_count = {len(traps)};",
        "",
    ]) 

    source_path.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description="Compile Tiled JSON maps to GBA C data.")
    parser.add_argument("map", type=Path)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--check", action="store_true", help="validate without writing generated files")
    parser.add_argument("--symbol-prefix")
    args = parser.parse_args()
    if args.check and (args.header or args.source):
        parser.error("--check cannot be combined with --header or --source")
    if not args.check and (not args.header or not args.source):
        parser.error("--header and --source are required unless --check is used")

    try:
        if args.check:
            with tempfile.TemporaryDirectory() as temp:
                write_outputs(args.map, Path(temp) / "map.h", Path(temp) / "map.c", args.symbol_prefix)
        else:
            write_outputs(args.map, args.header, args.source, args.symbol_prefix)
    except MapValidationError as exc:
        parser.exit(2, f"compile_map.py: error: {exc}\n")
    except (KeyError, TypeError, ValueError) as exc:
        parser.exit(2, f"compile_map.py: error: {args.map}: invalid map data: {exc}\n")


if __name__ == "__main__":
    main()
