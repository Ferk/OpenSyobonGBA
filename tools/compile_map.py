#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


TRAP_KIND_NAMES = {
    "invisible_block": "TRAP_INVISIBLE_BLOCK",
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
}

TRAP_SUBTYPE_NAMES = {
    "none": 0,
    "default": 0,
    "invisible_reveal_brick": 1,
    "invisible_reveal": 1,
    "falling_on_pass_under": 51,
    "falling_when_below": 51,
    "falling_brick_group": 51,
    "falling_when_approached": 52,
    "stage_pipe_shot": 100,
    "stage_flying_enemy": 101,
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
    "question_poison_generator": 110,
    "question_coin_generator": 112,
    "question_hidden_poison": 114,
    "question_generator_active": 200,
    "question_generator_active_internal": 200,
    "evasive_vertical": 0,
    "evasive_horizontal": 1,
    "pipe_fake_kill": 0,
    "pipe_to_underground": 1,
    "hint_stage_1": 1,
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
}

ENEMY_KIND_NAMES = {
    "walker": "ENEMY_WALKER",
    "jumper": "ENEMY_JUMPER",
    "static_hazard": "ENEMY_STATIC_HAZARD",
    "ceiling_faller": "ENEMY_CEILING_FALLER",
    "pipe_shot": "ENEMY_PIPE_SHOT",
    "firebar": "ENEMY_FIREBAR",
}

ENEMY_SPRITE_NAMES = {
    "ghost": "ENEMY_SPRITE_GHOST",
    "tall": "ENEMY_SPRITE_TALL",
    "hazard": "ENEMY_SPRITE_HAZARD",
    "atype3": "ENEMY_SPRITE_ATYPE3",
    "face_hidden": "ENEMY_SPRITE_FACE_HIDDEN",
    "face_grin": "ENEMY_SPRITE_FACE_GRIN",
    "cloud": "ENEMY_SPRITE_FACE_HIDDEN",
    "cloud_face": "ENEMY_SPRITE_FACE_GRIN",
    "nyassun": "ENEMY_SPRITE_NYASSUN",
    "nyassun_alert": "ENEMY_SPRITE_NYASSUN_ALERT",
    "tall32": "ENEMY_SPRITE_TALL32",
    "cuckoo32": "ENEMY_SPRITE_CUCKOO32",
    "spiky_soldier": "ENEMY_SPRITE_SPIKY_SOLDIER",
}

FLIP_MASK = 0xE0000000
GBA_METATILE_SIZE = 16
SOURCE_ROW_OFFSET = 5
WORLD_Y_OFFSET_PX = SOURCE_ROW_OFFSET * GBA_METATILE_SIZE
FIX_SCALE = 1 << 16


def c_ident(name):
    stem = re.sub(r"[^0-9A-Za-z_]", "_", Path(name).stem)
    stem = re.sub(r"_+", "_", stem).strip("_").lower()
    if not stem or stem[0].isdigit():
        stem = "level_" + stem
    return stem


def fixed(value):
    return int(round(float(value) * FIX_SCALE))


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
    return TRAP_KIND_NAMES.get(text.lower())


def enum_name(value, names, default):
    if value is None:
        return default
    text = str(value)
    if text in names.values():
        return text
    return names.get(text.lower(), default)


def enum_int(value, names, default):
    if value is None:
        return default

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

    match = re.match(r"^(-?\d+)", text)
    if match:
        return int(match.group(1), 0)

    raise ValueError(f"unknown enum integer value: {text}")


def object_name(obj):
    return obj.get("class") or obj.get("type") or obj.get("name") or ""


def parse_object_layers(map_data, tile_props):
    objects = []
    enemy_spawns = []
    traps = []

    for layer in map_data.get("layers", []):
        if layer.get("type") != "objectgroup":
            continue

        layer_name = layer.get("name", "")
        for obj in layer.get("objects", []):
            props = obj.get("properties", [])
            gid = decode_gid(obj.get("gid", 0))
            tile_prop = tile_props.get(gid, {})
            name = object_name(obj)
            trap = trap_kind(prop_value(props, "trap_type",
                            tile_prop.get("trap_type")))

            tiled_x = float(obj.get("x", 0))
            tiled_y = float(obj.get("y", 0))
            world_y = tiled_y - WORLD_Y_OFFSET_PX
            x = fixed(tiled_x)
            y = fixed(world_y)
            w = fixed(obj.get("width", GBA_METATILE_SIZE))
            h = fixed(obj.get("height", GBA_METATILE_SIZE))
            subtype = enum_int(prop_value(props, "subtype",
                                          tile_prop.get("subtype")),
                               TRAP_SUBTYPE_NAMES, 0)
            spawn_dir = enum_int(prop_value(props, "spawn_dir",
                                            tile_prop.get("spawn_dir")),
                                 TRAP_SPAWN_DIRECTION_NAMES, 0)
            spawn_interval = int(prop_value(
                props, "spawn_interval",
                tile_prop.get("spawn_interval",
                              default_question_spawn_interval(subtype))))
            spawn_limit = int(prop_value(
                props, "spawn_limit",
                tile_prop.get("spawn_limit",
                              default_question_spawn_limit(subtype))))
            goal_walk_frames = int(prop_value(
                props, "goal_walk_frames",
                tile_prop.get("goal_walk_frames",
                              default_goal_walk_frames(trap))))
            hint_text = prop_value(props, "hint_text",
                                   tile_prop.get("hint_text"))
            source_x = int(float(prop_value(props, "source_x",
                           tiled_x / GBA_METATILE_SIZE)))
            source_y = int(float(prop_value(props, "source_y",
                           tiled_y / GBA_METATILE_SIZE)))
            trigger_x = fixed(prop_value(props, "trigger_x", tiled_x))
            trigger_y = fixed(float(prop_value(props, "trigger_y", tiled_y)) -
                              WORLD_Y_OFFSET_PX)

            if layer_name.lower().startswith("enem") or name.lower() == "enemy":
                enemy_spawns.append({
                    "kind": enum_name(prop_value(props, "enemy_kind",
                                      tile_prop.get("enemy_kind")),
                                      ENEMY_KIND_NAMES, "ENEMY_WALKER"),
                    "sprite": enum_name(prop_value(props, "enemy_sprite",
                                        tile_prop.get("enemy_sprite")),
                                        ENEMY_SPRITE_NAMES, "ENEMY_SPRITE_GHOST"),
                    "dir": int(prop_value(props, "dir", tile_prop.get("dir", -1))),
                    "w": max(1, min(255, int(round(float(obj.get("width", 16)))))),
                    "h": max(1, min(255, int(round(float(obj.get("height", 16)))))),
                    "palette": max(0, min(15, int(prop_value(
                        props, "enemy_palette",
                        tile_prop.get("enemy_palette", 0))))),
                    "param": max(0, min(255, int(prop_value(
                        props, "enemy_param",
                        tile_prop.get("enemy_param", 0))))),
                    "x": x,
                    "y": y,
                })
                continue

            if trap or layer_name.lower().startswith("trap"):
                traps.append({
                    "kind": trap or "TRAP_INVISIBLE_BLOCK",
                    "subtype": subtype,
                    "spawn_dir": spawn_dir,
                    "visual_palette": enum_int(prop_value(props, "visual_palette",
                                                tile_prop.get("visual_palette",
                                                              tile_prop.get("palette"))),
                                               BG_PALETTE_NAMES, 0),
                    "spawn_interval": max(0, min(255, spawn_interval)),
                    "spawn_limit": max(0, min(255, spawn_limit)),
                    "goal_walk_frames": max(0, min(65535, goal_walk_frames)),
                    "hint_text": c_string_literal(hint_text),
                    "visual_metatile": int(prop_value(props, "visual_metatile",
                                           tile_prop.get("visual_metatile", 255))),
                    "spent_metatile": int(prop_value(props, "spent_metatile",
                                          tile_prop.get("spent_metatile", 255))),
                    "collision": int(prop_value(props, "collision",
                                     tile_prop.get("collision", 255))),
                    "hidden": int(prop_value(props, "hidden",
                                  tile_prop.get("hidden", 0))),
                    "source_x": source_x,
                    "source_y": source_y,
                    "x": x,
                    "y": y,
                    "w": w,
                    "h": h,
                    "trigger_x": trigger_x,
                    "trigger_y": trigger_y,
                })
                continue

            entity_kind = int(prop_value(props, "entity_kind",
                              ENTITY_KIND_NAMES.get(name, 0)))
            if entity_kind:
                objects.append({
                    "kind": entity_kind,
                    "subtype": subtype,
                    "x": x,
                    "y": y,
                    "w": w,
                    "h": h,
                })

    return objects, enemy_spawns, traps


def format_u8_array(name, rows):
    out = [f"const uint8_t {name}[{len(rows)}][{len(rows[0])}] = {{"]
    for row in rows:
        out.append("    { " + ", ".join(str(v) for v in row) + " },")
    out.append("};")
    return "\n".join(out)


def write_outputs(tmj_path, header_path, source_path, symbol_prefix=None):
    map_data = json.loads(tmj_path.read_text())
    width = int(map_data["width"])
    height = int(map_data["height"])
    map_props = map_properties(map_data)
    camera_margin_top = max(0, min(65535, int(map_props.get("camera_margin_top", 0))))
    camera_margin_bottom = max(0, min(65535, int(map_props.get("camera_margin_bottom", 0))))
    props = tile_properties(map_data)

    metatiles = [[0 for _ in range(width)] for _ in range(height)]
    palettes = [[0 for _ in range(width)] for _ in range(height)]
    source = [[0 for _ in range(width)] for _ in range(height)]

    for layer in map_data.get("layers", []):
        if layer.get("type") != "tilelayer":
            continue
        layer_name = layer.get("name", "").lower()
        is_source_layer = layer_name in ("source", "reference", "originalsource",
                                         "original_source")
        if layer.get("visible", True) is False and not is_source_layer:
            continue

        for x, y, raw_gid in iter_tile_layer_cells(layer, width, height):
            gid = decode_gid(raw_gid)
            if gid == 0:
                continue
            if is_source_layer:
                source[y][x] = source_from_gid(gid, props) or (gid - 1)
            else:
                layer_palette = enum_int(layer_properties(layer).get("palette"),
                                         BG_PALETTE_NAMES, 0)
                metatiles[y][x] = metatile_from_gid(gid, props)
                palettes[y][x] = palette_from_gid(gid, props, layer_palette)

    objects, enemy_spawns, traps = parse_object_layers(map_data, props)
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
}};

typedef struct GeneratedEnemySpawn {{
    EnemyKind kind;
    uint8_t sprite;
    int8_t dir;
    uint8_t w;
    uint8_t h;
    uint8_t palette;
    uint8_t param;
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
        f"const GeneratedMapObject {prefix}_objects[{max(1, len(objects))}] = {{",
    ]
    if objects:
        for obj in objects:
            lines.append("    { %d, %d, %d, %d, %d, %d }," %
                         (obj["kind"], obj["subtype"], obj["x"], obj["y"],
                          obj["w"], obj["h"]))
    else:
        lines.append("    { 0, 0, 0, 0, 0, 0 },")
    lines.extend([
        "};",
        f"const uint16_t {prefix}_object_count = {len(objects)};",
        "",
        f"const GeneratedEnemySpawn {prefix}_enemy_spawns[{max(1, len(enemy_spawns))}] = {{",
    ])
    if enemy_spawns:
        for spawn in enemy_spawns:
            lines.append("    { %s, %s, %d, %d, %d, %d, %d, %d, %d }," %
                         (spawn["kind"], spawn["sprite"], spawn["dir"],
                          spawn["w"], spawn["h"], spawn["palette"],
                          spawn["param"], spawn["x"], spawn["y"]))
    else:
        lines.append("    { ENEMY_NONE, 0, 0, 16, 16, 0, 0, 0, 0 },")
    lines.extend([
        "};",
        f"const uint16_t {prefix}_enemy_spawn_count = {len(enemy_spawns)};",
        "",
        f"const TrapTrigger {prefix}_traps[{max(1, len(traps))}] = {{",
    ])
    if traps:
        for trap in traps:
            lines.append("    { %s, %d, %d, %d, %d, %d, %d, %s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d }," %
                         (trap["kind"], trap["subtype"], trap["spawn_dir"],
                          trap["visual_palette"], trap["spawn_interval"],
                          trap["spawn_limit"], trap["goal_walk_frames"],
                          trap["hint_text"],
                          trap["visual_metatile"], trap["spent_metatile"],
                          trap["collision"], trap["hidden"], trap["source_x"],
                          trap["source_y"], trap["x"], trap["y"], trap["w"],
                          trap["h"], trap["trigger_x"], trap["trigger_y"]))
    else:
        lines.append("    { TRAP_INVISIBLE_BLOCK, 0, 0, 0, 0, 1, 0, 0, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0 },")
    lines.extend([
        "};",
        f"const uint16_t {prefix}_trap_count = {len(traps)};",
        "",
    ]) 

    source_path.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description="Compile Tiled JSON maps to GBA C data.")
    parser.add_argument("map", type=Path)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol-prefix")
    args = parser.parse_args()

    write_outputs(args.map, args.header, args.source, args.symbol_prefix)


if __name__ == "__main__":
    main()
