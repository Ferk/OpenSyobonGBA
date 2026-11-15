"""Shared focused-class and recipe definitions for Tiled and the map compiler."""
import copy
import json
from pathlib import Path

SCHEMA = json.loads(Path(__file__).with_name("behavior_schema.json").read_text())


def authoring_classes(schema=SCHEMA):
    classes = copy.deepcopy(schema["classes"])
    for name, recipe in schema.get("recipes", {}).items():
        family = classes[recipe["family"]]
        if recipe["subtype"] not in family["subtypes"]:
            raise ValueError(f"{name}: subtype is not supported by {recipe['family']}")
        if not set(recipe["properties"]) <= set(family["properties"]):
            raise ValueError(f"{name}: recipe exposes unsupported family properties")
        if not set(recipe.get("defaults", {})) <= set(recipe["properties"]):
            raise ValueError(f"{name}: default is not an editable recipe property")
        classes[name] = dict(recipe, kind=family["kind"],
                             defaults=dict(recipe.get("defaults", {}), subtype=recipe["subtype"]))
    for name, spec in classes.items():
        for field in spec["properties"]:
            if field not in schema["properties"]:
                raise ValueError(f"{name}: unknown property {field!r}")
    return classes
