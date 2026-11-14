import copy
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("compile_map", ROOT / "tools/compile_map.py")
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


class CompilerValidationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        shutil.copy(ROOT / "gfx/tiles_16.png", self.root / "tiles.png")
        self.obj = {"id": 1, "type": "Trap", "name": "example", "x": 0,
                    "y": 0, "width": 16, "height": 16,
                    "properties": [{"name": "trap_type", "value": "question_block"}]}
        self.map = {"width": 1, "height": 1, "nextobjectid": 2,
                    "orientation": "orthogonal", "tilewidth": 16, "tileheight": 16,
                    "tilesets": [{"firstgid": 1, "image": "tiles.png", "imagewidth": 16,
                                  "imageheight": 1360, "tilecount": 85}],
                    "layers": [{"name": "Background", "type": "tilelayer", "data": [1]},
                               {"name": "Traps", "type": "objectgroup", "objects": [self.obj]}]}

    def tearDown(self):
        self.directory.cleanup()

    def compile(self):
        path = self.root / "map.tmj"
        path.write_text(json.dumps(self.map))
        compiler.write_outputs(path, self.root / "map.h", self.root / "map.c")
        return (self.root / "map.c").read_text()

    def set_property(self, name, value):
        props = self.obj["properties"]
        props[:] = [p for p in props if p["name"] != name]
        props.append({"name": name, "value": value})

    def test_unknown_trap_has_object_and_field(self):
        self.set_property("trap_type", "typo")
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*trap_type.*unknown"):
            self.compile()

    def test_wrong_kind_subtype_and_out_of_range_interval(self):
        self.set_property("subtype", "platform_bounce")
        with self.assertRaisesRegex(compiler.MapValidationError, "subtype.*not valid"):
            self.compile()
        self.set_property("subtype", "none")
        self.set_property("spawn_interval", 300)
        with self.assertRaisesRegex(compiler.MapValidationError, "spawn_interval.*255"):
            self.compile()

    def test_unset_destination_and_valid_first_stage_are_distinct(self):
        self.set_property("trap_type", "transition_zone")
        self.set_property("stage_destination", "none")
        with self.assertRaisesRegex(compiler.MapValidationError, "stage_destination"):
            self.compile()
        self.set_property("stage_destination", "1-1")
        target = copy.deepcopy(self.map)
        target["layers"] = [{"name": "Objects", "type": "objectgroup", "objects": [
            {"id": 1, "type": "Player Start", "x": 0, "y": 0}]}]
        (self.root / "level1.tmj").write_text(json.dumps(target))
        self.assertIn(".destination = 0", self.compile())

    def test_limits_ids_and_tileset_metadata(self):
        self.map["width"] = 161
        with self.assertRaisesRegex(compiler.MapValidationError, "runtime limit"):
            self.compile()
        self.map["width"] = 1
        self.map["nextobjectid"] = 1
        with self.assertRaisesRegex(compiler.MapValidationError, "nextobjectid"):
            self.compile()
        self.map["nextobjectid"] = 2
        self.map["tilesets"][0]["tilecount"] = 80
        with self.assertRaisesRegex(compiler.MapValidationError, "metadata"):
            self.compile()

    def test_hidden_switch_layer_is_compiled_as_mask(self):
        self.map["layers"].append({"name": "P-Switch Blocks", "type": "tilelayer",
                                   "visible": False, "data": [1]})
        self.assertIn("const uint8_t map_switch_cells[1][1] = {\n    { 1 }", self.compile())

    def test_unsupported_tiled_features_are_reported(self):
        layer = self.map["layers"][0]
        layer["data"] = [0x80000001]
        with self.assertRaisesRegex(compiler.MapValidationError, r"Background.*tile \(0, 0\).*flip"):
            self.compile()
        layer["data"] = [1]
        layer["offsetx"] = 2
        with self.assertRaisesRegex(compiler.MapValidationError, "Background.*offset"):
            self.compile()
        del layer["offsetx"]
        self.obj["rotation"] = 90
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*rotation"):
            self.compile()
        del self.obj["rotation"]
        self.map["layers"].append({"name": "Group", "type": "group"})
        with self.assertRaisesRegex(compiler.MapValidationError, "Group.*group"):
            self.compile()

    def test_map_shape_and_external_tileset_validation(self):
        self.map["orientation"] = "isometric"
        with self.assertRaisesRegex(compiler.MapValidationError, "orientation"):
            self.compile()
        self.map["orientation"] = "orthogonal"
        self.map["layers"][0]["data"] = []
        with self.assertRaisesRegex(compiler.MapValidationError, "exactly 1 cells"):
            self.compile()
        self.map["layers"][0]["data"] = [1]
        self.map["tilesets"][0]["source"] = "shared.tsj"
        with self.assertRaisesRegex(compiler.MapValidationError, "external tilesets"):
            self.compile()

    def test_check_command_writes_no_generated_outputs(self):
        path = self.root / "map.tmj"
        path.write_text(json.dumps(self.map))
        result = subprocess.run([sys.executable, str(ROOT / "tools/compile_map.py"),
                                 str(path), "--check"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "map.c").exists())
        self.assertFalse((self.root / "map.h").exists())

    def test_spawns_use_offsets_and_reject_absolute_coordinates(self):
        self.set_property("trap_type", "stage_spawner")
        self.set_property("spawn_offset_x", 7.5)
        self.set_property("spawn_offset_y", -12.25)
        self.obj["x"] = 20
        self.assertIn(".spawn_offset_x = 491520, .spawn_offset_y = -802816", self.compile())
        self.obj["x"] = 40
        self.assertIn(".spawn_offset_x = 491520, .spawn_offset_y = -802816", self.compile())
        self.set_property("spawn_x", 100)
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*spawn_x.*spawn_offset_x"):
            self.compile()

    def test_channel_listener_kind_and_cycles(self):
        self.set_property("trap_type", "spike_block")
        self.set_property("listen_channel", 1)
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*listen_channel.*cannot"):
            self.compile()
        self.set_property("trap_type", "falling_floor")
        self.set_property("trigger_channel", 2)
        other = copy.deepcopy(self.obj)
        other["id"] = 2
        other["name"] = "other"
        other["properties"] = [{"name": "trap_type", "value": "falling_floor"},
                               {"name": "listen_channel", "value": 2},
                               {"name": "trigger_channel", "value": 3}]
        self.map["layers"][1]["objects"].append(other)
        self.map["nextobjectid"] = 3
        self.compile()
        other["properties"][-1]["value"] = 1
        with self.assertRaisesRegex(compiler.MapValidationError, "other.*trigger_channel.*cycle"):
            self.compile()

    def test_runtime_only_question_subtype_is_not_authorable(self):
        self.set_property("subtype", "question_generator_active_internal")
        with self.assertRaisesRegex(compiler.MapValidationError, "subtype.*unknown"):
            self.compile()


if __name__ == "__main__":
    unittest.main()
