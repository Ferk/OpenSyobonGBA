#!/usr/bin/env python3
"""Print tileset and image dependencies referenced by Tiled maps for make."""
import json
import sys
from pathlib import Path


def dependencies(paths):
    result = set()
    visited = set()

    def tileset_files(entry, base):
        tileset = entry
        if "source" in entry:
            source = base / entry["source"]
            result.add(source)
            base = source.parent
            try:
                tileset = json.loads(source.read_text())
            except (OSError, ValueError):
                return
        if tileset.get("image"):
            result.add(base / tileset["image"])
        for tile in tileset.get("tiles", []):
            if tile.get("image"):
                result.add(base / tile["image"])

    def templates(obj, base):
        if "template" not in obj:
            return
        source = base / obj["template"]
        result.add(source)
        key = source.resolve()
        if key in visited:
            return
        visited.add(key)
        try:
            data = json.loads(source.read_text())
            if data.get("tileset"):
                tileset_files(data["tileset"], source.parent)
            templates(data.get("object", {}), source.parent)
        except (OSError, TypeError, ValueError):
            pass

    for path in paths:
        path = Path(path)
        try:
            map_data = json.loads(path.read_text())
            for layer in map_data.get("layers", []):
                for obj in layer.get("objects", []):
                    templates(obj, path.parent)
            for entry in map_data.get("tilesets", []):
                tileset_files(entry, path.parent)
        except (OSError, ValueError):
            continue
    return sorted(str(path) for path in result)


if __name__ == "__main__":
    print(" ".join(path.replace("$", "$$").replace("#", "\\#").replace(" ", "\\ ")
                   for path in dependencies(sys.argv[1:])))
