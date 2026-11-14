#!/usr/bin/env python3
"""Update focused Tiled classes from the authoring schema."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "tools/behavior_schema.json"
PROJECT = ROOT / "OpenCorgiAction.tiled-project"
TEMPLATES = ROOT / "levels/templates"


def main():
    schema = json.loads(SCHEMA.read_text())
    project = json.loads(PROJECT.read_text())
    types = project["propertyTypes"]
    base = next(item for item in types if item["name"] == "Trap")
    members = {item["name"]: item for item in base["members"]}
    for field, enum, default in (("visual_metatile", "VisualMetatile", "inherit"),
                                 ("spent_metatile", "SpentMetatile", "default_spent")):
        members[field].update(type="string", propertyType=enum, value=default)
    for name in ("spawn_random_vx", "spawn_random_vy", "move_offset_x", "move_offset_y"):
        members[name] = {"name": name, "type": "float", "value": 0}
    generated = set(schema["classes"]) | {name + " Subtype" for name in schema["classes"]} | {"VisualMetatile", "SpentMetatile"}
    types[:] = [item for item in types if item["name"] not in generated]
    next_id = max(item["id"] for item in types) + 1
    seen = set()
    for item in types:
        if item["id"] in seen:
            item["id"] = next_id
            next_id += 1
        seen.add(item["id"])
    for name, special in (("VisualMetatile", "inherit"), ("SpentMetatile", "default_spent")):
        types.append({"id": next_id, "name": name, "type": "enum", "storageType": "string",
                      "valuesAsFlags": False, "values": [special] + list(schema["metatiles"])})
        next_id += 1
    enemy = next(item for item in types if item["name"] == "Enemy")
    for field in ("body_width", "body_height"):
        if not any(p["name"] == field for p in enemy["members"]):
            enemy["members"].append({"name": field, "type": "int", "value": 16})
    for name, spec in schema["classes"].items():
        subtype_name = name + " Subtype"
        types.append({"id": next_id, "name": subtype_name, "type": "enum",
                      "storageType": "string", "valuesAsFlags": False,
                      "values": spec["subtypes"]})
        next_id += 1
        fields = [{"name": "subtype", "type": "string",
                   "propertyType": subtype_name, "value": "none"}]
        fields.extend(members[field].copy() for field in spec["properties"])
        types.append({"id": next_id, "name": name, "type": "class",
                      "color": "#ffff7070", "drawFill": True,
                      "useAs": ["object"], "members": fields})
        next_id += 1
    PROJECT.write_text(json.dumps(project, indent=1, ensure_ascii=False) + "\n")
    TEMPLATES.mkdir(parents=True, exist_ok=True)
    for filename, preset in schema.get("presets", {}).items():
        class_name = preset["class"]
        spec = schema["classes"][class_name]
        properties = []
        for field, value in preset["properties"].items():
            if field == "subtype":
                if value not in spec["subtypes"]:
                    raise ValueError(f"{filename}: invalid subtype {value!r}")
                prop = {"name": field, "type": "string", "propertytype": class_name + " Subtype"}
            else:
                if field not in spec["properties"]:
                    raise ValueError(f"{filename}: unsupported property {field!r}")
                prop = {k: v for k, v in members[field].items() if k != "value"}
                if "propertyType" in prop:
                    prop["propertytype"] = prop.pop("propertyType")
            properties.append(dict(prop, value=value))
        obj = {"name": preset["name"], "type": class_name,
               "width": preset["width"], "height": preset["height"],
               "rotation": 0, "visible": True, "properties": properties}
        (TEMPLATES / (filename + ".tx")).write_text(
            json.dumps({"type": "template", "object": obj}, indent=1) + "\n")


if __name__ == "__main__":
    main()
