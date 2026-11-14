#!/usr/bin/env python3
"""Generate Tiled previews from the port's current graphics, not reference art."""
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    schema = json.loads((ROOT / "tools/behavior_schema.json").read_text())
    directory = ROOT / "levels/editor"
    directory.mkdir(parents=True, exist_ok=True)
    tiles = []
    for index, spec in enumerate(schema["previews"]):
        with Image.open(ROOT / "gfx" / spec["sheet"]) as source:
            rgba = source.convert("RGBA")
            if source.mode == "P":
                rgba.putalpha(source.point(lambda value: 0 if value == 0 else 255, mode="L"))
            columns = spec["columns"]
            cells = spec["cells"]
            image = Image.new("RGBA", (16 * columns, 16 * ((len(cells) + columns - 1) // columns)))
            for part, cell in enumerate(cells):
                if cell < 0 or (cell + 1) * 16 > source.height or source.width != 16:
                    raise ValueError(f"{spec['name']}: cell {cell} outside {spec['sheet']}")
                image.paste(rgba.crop((0, cell * 16, 16, (cell + 1) * 16)),
                            ((part % columns) * 16, (part // columns) * 16))
        image.save(directory / (spec["name"] + ".png"))
        properties = [{"name": "preview_name", "type": "string", "value": spec["name"]}]
        for name, value in spec["properties"].items():
            prop = {"name": name, "type": "int" if isinstance(value, int) else "string", "value": value}
            enum = {"enemy_kind": "EnemyKind", "enemy_sprite": "EnemySprite", "trap_type": "TrapKind"}.get(name)
            if enum:
                prop["propertytype"] = enum
            properties.append(prop)
        tiles.append({"id": index, "type": spec["class"], "image": "editor/" + spec["name"] + ".png",
                      "imagewidth": image.width, "imageheight": image.height,
                      "properties": properties,
                      "objectgroup": {"type": "objectgroup", "draworder": "index", "objects": [
                          {"id": 1, "name": "Gameplay Body", "x": 0, "y": 0,
                           "width": spec["properties"]["body_width"], "height": spec["properties"]["body_height"]}]}})
        template_dir = ROOT / "levels/templates" / ("enemies" if spec["class"] == "Enemy" else "items")
        template_dir.mkdir(parents=True, exist_ok=True)
        template = {"type": "template", "tileset": {"firstgid": 1, "source": "../../objects.tsj"},
                    "object": {"name": spec["name"], "type": spec["class"], "gid": index + 1,
                               "width": image.width, "height": image.height, "rotation": 0, "visible": True}}
        (template_dir / (spec["name"] + ".tx")).write_text(json.dumps(template, indent=1) + "\n")
    tileset = {"type": "tileset", "version": "1.10", "name": "Object Previews",
               "tilewidth": max(t["imagewidth"] for t in tiles), "tileheight": max(t["imageheight"] for t in tiles),
               "tilecount": len(tiles), "columns": 0, "objectalignment": "topleft",
               "grid": {"orientation": "orthogonal", "width": 16, "height": 16},
               "properties": [{"name": "object_tileset", "type": "bool", "value": True}], "tiles": tiles}
    (ROOT / "levels/objects.tsj").write_text(json.dumps(tileset, indent=1) + "\n")
    terrain_path = ROOT / "levels/tiles_16.tsj"
    terrain = json.loads(terrain_path.read_text())
    existing = {tile["id"]: tile for tile in terrain.get("tiles", [])}
    for name, index in schema["metatiles"].items():
        tile = existing.setdefault(index, {"id": index})
        props = [p for p in tile.get("properties", []) if p["name"] != "metatile_name"]
        props.append({"name": "metatile_name", "type": "string", "value": name})
        tile["properties"] = props
    terrain["tiles"] = [existing[index] for index in sorted(existing)]
    terrain_path.write_text(json.dumps(terrain, indent=1) + "\n")


if __name__ == "__main__":
    main()
