import copy
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("compile_map", ROOT / "tools/compile_map.py")
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


class CompilerValidationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        shutil.copy(ROOT / "gfx/tiles_16.png", self.root / "tiles.png")
        self.obj = {"id": 1, "type": "Question Block", "name": "example", "x": 0,
                    "y": 0, "width": 16, "height": 16,
                    "properties": []}
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
        self.obj["type"] = "Question Blcok"
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*class.*unknown trap class"):
            self.compile()

    def test_generic_trap_and_obsolete_kind_property_are_rejected(self):
        for layer_name in ("Traps", "Objects"):
            self.map["layers"][1]["name"] = layer_name
            self.obj["type"] = "Trap"
            with self.assertRaisesRegex(compiler.MapValidationError, "example.*class.*generic Trap"):
                self.compile()
        self.obj["type"] = "Question Block"
        for value in ("question_block", "goal", None):
            self.set_property("trap_type", value)
            with self.assertRaisesRegex(compiler.MapValidationError, "example.*trap_type.*no longer supported"):
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
        self.obj["type"] = "Transition Zone"
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
        self.map["tilesets"][0]["source"] = "missing.tsj"
        with self.assertRaisesRegex(compiler.MapValidationError, "external tileset.*missing.tsj"):
            self.compile()

    def test_external_tileset_resolves_image_relative_to_tileset(self):
        expected = self.compile()
        tileset = copy.deepcopy(self.map["tilesets"][0])
        del tileset["firstgid"]
        tileset["image"] = "../tiles.png"
        (self.root / "sets").mkdir()
        (self.root / "sets/shared.tsj").write_text(json.dumps(tileset))
        self.map["tilesets"] = [{"firstgid": 1, "source": "sets/shared.tsj"}]
        self.assertEqual(self.compile(), expected)
        deps = subprocess.check_output([sys.executable, str(ROOT / "tools/map_dependencies.py"),
                                        str(self.root / "map.tmj")], text=True)
        self.assertIn("sets/shared.tsj", deps)
        self.assertIn("sets/../tiles.png", deps)

    def test_focused_class_selects_kind_and_rejects_obsolete_property(self):
        self.obj["type"] = "Question Block"
        self.obj["properties"] = [{"name": "subtype", "value": "question_star"}]
        output = self.compile()
        self.assertIn(".kind = TRAP_QUESTION_BLOCK", output)
        self.assertIn(".subtype = 104", output)
        self.set_property("trap_type", "goal")
        with self.assertRaisesRegex(compiler.MapValidationError, "trap_type.*no longer supported"):
            self.compile()

    def test_focused_class_subtypes_match_compiler(self):
        for name, spec in compiler.BEHAVIOR_SCHEMA["classes"].items():
            kind = compiler.TRAP_KIND_NAMES[spec["kind"]]
            for subtype in spec["subtypes"]:
                with self.subTest(name=name, subtype=subtype):
                    self.assertIn(compiler.TRAP_SUBTYPE_NAMES[subtype],
                                  compiler.TRAP_SUBTYPES.get(kind, {0}))
                    if subtype != "none":
                        self.assertIn(subtype, compiler.TRAP_SUBTYPE_NAMES_BY_KIND.get(kind, set()))

    def test_tiled_focused_classes_follow_schema(self):
        project = json.loads((ROOT / "OpenCorgiAction.tiled-project").read_text())
        entries = project["propertyTypes"]
        self.assertEqual(len(entries), len({entry["id"] for entry in entries}))
        by_name = {entry["name"]: entry for entry in entries}
        for name, spec in compiler.BEHAVIOR_SCHEMA["classes"].items():
            with self.subTest(name=name):
                self.assertEqual(by_name[name + " Subtype"]["values"], spec["subtypes"])
                subtype_fields = ["subtype"] if len(spec["subtypes"]) > 1 else []
                self.assertEqual([p["name"] for p in by_name[name]["members"]],
                                 subtype_fields + spec["properties"])

    def test_all_trap_kinds_have_focused_classes(self):
        self.assertEqual(set(compiler.TRAP_KIND_NAMES),
                         {s["kind"] for s in compiler.BEHAVIOR_SCHEMA["classes"].values()})
        project = json.loads((ROOT / "OpenCorgiAction.tiled-project").read_text())
        names = {t["name"] for t in project["propertyTypes"]}
        self.assertFalse({"Trap", "TrapKind", "TrapSubtype"} & names)

    def test_recipe_defaults_equal_explicit_family_settings(self):
        for name, recipe in compiler.BEHAVIOR_SCHEMA["recipes"].items():
            with self.subTest(recipe=name):
                self.obj["type"] = name
                self.obj["properties"] = []
                expected = self.compile()
                self.obj["type"] = recipe["family"]
                self.obj["properties"] = [
                    {"name": field, "value": value} for field, value in
                    dict(recipe["defaults"], subtype=recipe["subtype"]).items()]
                self.assertEqual(self.compile(), expected)

    def test_recipe_rejects_conflicting_subtype_but_allows_setting_overrides(self):
        self.obj["type"] = "Poison Generator"
        self.obj["properties"] = []
        output = self.compile()
        self.assertIn(".spawn_interval = 16", output)
        self.assertIn(".spawn_limit = 0", output)
        self.set_property("spawn_interval", 0)
        self.set_property("spawn_limit", 1)
        output = self.compile()
        self.assertIn(".spawn_interval = 0", output)
        self.assertIn(".spawn_limit = 1", output)
        self.set_property("subtype", "question_star")
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*subtype.*class.*fixes subtype"):
            self.compile()

    def test_recipe_templates_keep_zero_false_and_signed_overrides(self):
        self.obj.clear()
        self.obj.update(id=1, template=str(ROOT / "levels/templates/warp_pad.tx"),
                        x=0, y=80, properties=[{"name": "hidden", "value": False}])
        self.assertIn(".hidden = 0", self.compile())
        self.obj.update(template=str(ROOT / "levels/templates/conveyor_left.tx"),
                        properties=[{"name": "vx", "value": 2.5}])
        self.assertIn(".vx = 163840", self.compile())

    def test_recipe_editor_fields_and_documentation_follow_schema(self):
        from generate_tiled_classes import class_reference
        schema = compiler.BEHAVIOR_SCHEMA
        project = json.loads((ROOT / "OpenCorgiAction.tiled-project").read_text())
        types = {t["name"]: t for t in project["propertyTypes"]}
        for name, recipe in schema["recipes"].items():
            fields = types[name]["members"]
            self.assertEqual([p["name"] for p in fields], recipe["properties"])
            for prop in fields:
                default = recipe["defaults"].get(prop["name"], schema["properties"][prop["name"]]["value"])
                self.assertEqual(prop["value"], default)
        self.assertEqual((ROOT / "docs/generated/tiled_classes.adoc").read_text(),
                         class_reference(schema, compiler.AUTHORING_CLASSES))

    def test_class_generator_is_repeatable_without_legacy_trap_class(self):
        import generate_tiled_classes as generator
        project = self.root / "project.tiled-project"
        shutil.copy(ROOT / "OpenCorgiAction.tiled-project", project)
        with mock.patch.multiple(generator, ROOT=self.root, PROJECT=project,
                                 TEMPLATES=self.root / "templates"):
            generator.main()
            first = project.read_bytes()
            generator.main()
            self.assertEqual(first, project.read_bytes())

    def test_invalid_recipe_schema_is_rejected(self):
        from authoring_schema import authoring_classes
        schema = copy.deepcopy(compiler.BEHAVIOR_SCHEMA)
        schema["recipes"]["Conveyor"]["properties"].append("hint_text")
        with self.assertRaisesRegex(ValueError, "Conveyor.*unsupported family properties"):
            authoring_classes(schema)

    @unittest.skipUnless(shutil.which("tiled"), "Tiled is not installed")
    def test_recipe_survives_tiled_export_with_template_overrides(self):
        self.map.update(type="map", version="1.10", tiledversion="1.10.2", nextlayerid=3)
        self.map["tilesets"][0].update(name="Terrain", tilewidth=16, tileheight=16, columns=1)
        self.map["layers"][0].update(id=1, width=1, height=1)
        self.map["layers"][1].update(id=2, draworder="topdown")
        for layer in self.map["layers"]:
            layer.update(opacity=1, visible=True, x=0, y=0)
        self.obj.clear()
        self.obj.update(id=1, template=str(ROOT / "levels/templates/conveyor_left.tx"),
                        x=0, y=80, properties=[{"name": "vx", "type": "float", "value": 2.5}])
        expected = self.compile()
        exported = self.root / "exported.tmj"
        result = subprocess.run([shutil.which("tiled"), "--export-map",
                                 str(self.root / "map.tmj"), str(exported)],
                                env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        compiler.write_outputs(exported, self.root / "exported.h",
                               self.root / "exported.c", "map")
        self.assertEqual((self.root / "exported.c").read_text(), expected)

    def test_check_command_writes_no_generated_outputs(self):
        path = self.root / "map.tmj"
        path.write_text(json.dumps(self.map))
        result = subprocess.run([sys.executable, str(ROOT / "tools/compile_map.py"),
                                 str(path), "--check"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "map.c").exists())
        self.assertFalse((self.root / "map.h").exists())

    def test_spawns_use_offsets_and_reject_absolute_coordinates(self):
        self.obj["type"] = "Stage Spawner"
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
        self.obj["type"] = "Spike Block"
        self.set_property("listen_channel", 1)
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*listen_channel.*cannot"):
            self.compile()
        self.obj["type"] = "Falling Floor"
        self.set_property("trigger_channel", 2)
        other = copy.deepcopy(self.obj)
        other["id"] = 2
        other["name"] = "other"
        other["properties"] = [{"name": "listen_channel", "value": 2},
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

    def test_template_inheritance_and_instance_overrides(self):
        base = {"type": "template", "object": {"type": "Question Block",
                "width": 16, "height": 16,
                "properties": [{"name": "subtype", "value": "question_star"},
                               {"name": "hidden", "value": True}]}}
        (self.root / "star.tx").write_text(json.dumps(base))
        self.obj.clear()
        self.obj.update({"id": 1, "name": "instance", "template": "star.tx",
                         "x": 7.5, "y": 80, "width": 32,
                         "properties": [{"name": "hidden", "value": False}]})
        output = self.compile()
        self.assertIn(".kind = TRAP_QUESTION_BLOCK", output)
        self.assertIn(".subtype = 104", output)
        self.assertIn(".hidden = 0", output)
        self.assertIn(".x = 491520, .y = 0, .w = 2097152, .h = 1048576", output)
        deps = subprocess.check_output([sys.executable, str(ROOT / "tools/map_dependencies.py"),
                                       str(self.root / "map.tmj")], text=True)
        self.assertIn("star.tx", deps)

    def test_template_errors_have_object_context(self):
        self.obj["template"] = "missing.tx"
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*template.*missing.tx"):
            self.compile()
        (self.root / "loop.tx").write_text(json.dumps({"type": "template", "object": {"template": "loop.tx"}}))
        self.obj["template"] = "loop.tx"
        with self.assertRaisesRegex(compiler.MapValidationError, "example.*cyclic"):
            self.compile()

    def test_generated_presets_compile_and_match_schema(self):
        for filename, preset in compiler.BEHAVIOR_SCHEMA["presets"].items():
            with self.subTest(preset=filename):
                template = ROOT / "levels/templates" / (filename + ".tx")
                data = json.loads(template.read_text())
                self.assertEqual(data["object"]["type"], preset["class"])
                self.assertEqual({p["name"]: p["value"] for p in data["object"]["properties"]}, preset["properties"])
                self.obj.clear()
                self.obj.update({"id": 1, "template": str(template), "x": 0, "y": 80})
                self.compile()

    def preview_tileset(self, alignment="topleft"):
        tileset = json.loads((ROOT / "levels/objects.tsj").read_text())
        tileset["objectalignment"] = alignment
        for tile in tileset["tiles"]:
            tile["image"] = str(ROOT / "levels" / tile["image"])
        (self.root / "objects.tsj").write_text(json.dumps(tileset))
        self.map["tilesets"].append({"firstgid": 86, "source": "objects.tsj"})
        return tileset

    def test_named_metatiles_match_numeric_ids(self):
        self.set_property("visual_metatile", 1)
        self.set_property("spent_metatile", 3)
        expected = self.compile()
        self.set_property("visual_metatile", "brick")
        self.set_property("spent_metatile", "solid")
        self.assertEqual(self.compile(), expected)
        self.set_property("visual_metatile", "inherit")
        self.set_property("spent_metatile", "default_spent")
        self.assertIn(".visual_metatile = 255", self.compile())
        self.set_property("visual_metatile", "typo")
        with self.assertRaisesRegex(compiler.MapValidationError, "visual_metatile.*typo"):
            self.compile()
        self.set_property("visual_metatile", 1.5)
        with self.assertRaisesRegex(compiler.MapValidationError, "visual_metatile.*integer"):
            self.compile()

    def test_preview_body_and_bottom_anchor_are_independent_of_art(self):
        self.preview_tileset("bottomleft")
        self.obj.clear()
        self.obj.update(id=1, gid=86, x=7.5, y=96, width=16, height=16,
                        properties=[{"name": "body_width", "value": 21}])
        expected = self.compile()
        self.assertIn("{ ENEMY_WALKER, ENEMY_SPRITE_WALKER, -1, 21, 16, 0, 0, 126552, 491520, 0 }", expected)
        self.preview_tileset_without_duplicate("topleft")
        self.obj["y"] = 80
        self.assertEqual(self.compile(), expected)
        self.obj["width"] = 32
        with self.assertRaisesRegex(compiler.MapValidationError, "body_width/body_height"):
            self.compile()

    def preview_tileset_without_duplicate(self, alignment):
        self.map["tilesets"] = self.map["tilesets"][:1]
        return self.preview_tileset(alignment)

    def test_preview_tiles_cannot_be_painted_as_terrain(self):
        self.preview_tileset()
        self.map["layers"][0]["data"] = [86]
        with self.assertRaisesRegex(compiler.MapValidationError, "object preview art on a tile layer"):
            self.compile()

    def test_preview_template_gid_remapping_and_dependencies(self):
        template = ROOT / "levels/templates/enemies/defrag.tx"
        self.obj.clear()
        self.obj.update(id=1, template=str(template), x=0, y=80)
        output = self.compile()
        self.assertIn("{ ENEMY_DEFRAG, ENEMY_SPRITE_DEFRAG, -1, 16, 32, 0, 0, 126552, 0, 0 }", output)
        # The same template must work when its tileset is already loaded at another GID.
        self.map["tilesets"].append({"firstgid": 300, "source": str(ROOT / "levels/objects.tsj")})
        self.assertEqual(self.compile(), output)
        deps = subprocess.check_output([sys.executable, str(ROOT / "tools/map_dependencies.py"),
                                       str(self.root / "map.tmj")], text=True)
        self.assertIn("defrag.tx", deps)
        self.assertIn("objects.tsj", deps)
        self.assertIn("defrag.png", deps)

    def test_every_preview_template_compiles(self):
        for spec in compiler.BEHAVIOR_SCHEMA["previews"]:
            with self.subTest(preview=spec["name"]):
                group = "enemies" if spec["class"] == "Enemy" else "items"
                self.obj.clear()
                self.obj.update(id=1, template=str(ROOT / "levels/templates" / group / (spec["name"] + ".tx")), x=0, y=80)
                self.compile()

    def test_preview_tileset_validation(self):
        self.preview_tileset()
        self.map["tilesets"][1]["firstgid"] = 1
        with self.assertRaisesRegex(compiler.MapValidationError, "GID ranges overlap"):
            self.compile()
        self.map["tilesets"][1]["firstgid"] = 86
        tileset = json.loads((self.root / "objects.tsj").read_text())
        tileset["tiles"][0]["imageheight"] = 32
        (self.root / "objects.tsj").write_text(json.dumps(tileset))
        with self.assertRaisesRegex(compiler.MapValidationError, "object tile 0 image dimensions"):
            self.compile()


if __name__ == "__main__":
    unittest.main()
