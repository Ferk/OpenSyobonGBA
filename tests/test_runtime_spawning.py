"""Run the production spawning logic on the host with video/audio stand-ins."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cc"), "host C compiler is not installed")
class RuntimeSpawningTests(unittest.TestCase):
    def test_persistent_banner(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "messages"
            result = subprocess.run([
                shutil.which("cc"), "-std=c99", "-O1",
                "-I" + str(ROOT / "tests/stubs"), "-I" + str(ROOT / "include"),
                str(ROOT / "tests/runtime_messages.c"), str(ROOT / "src/font4x6.c"),
                "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_enemy_pool_and_trap_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "spawning"
            fixtures = Path(directory)
            (fixtures / "generated").mkdir()
            for path in sorted((ROOT / "levels").glob("*.tmj")):
                subprocess.run([
                    sys.executable, str(ROOT / "tools/compile_map.py"), str(path),
                    "--header", str(fixtures / "generated" / (path.stem + "_data.h")),
                    "--source", str(fixtures / (path.stem + "_data.c")),
                ], check=True, capture_output=True, text=True)
            for asset in ("enemies_16", "tiles_16", "items_16", "traps_16",
                          "p_switch_16", "spike_block_16"):
                (fixtures / (asset + ".h")).write_text(
                    f"static const unsigned int {asset}Tiles[4096] = {{0}};\n"
                    f"static const unsigned short {asset}Pal[256] = {{0}};\n"
                    f"#define {asset}TilesLen sizeof({asset}Tiles)\n"
                    f"#define {asset}PalLen sizeof({asset}Pal)\n")
            result = subprocess.run([
                shutil.which("cc"), "-std=c99", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + str(ROOT / "tests/stubs"), "-I" + str(ROOT / "include"),
                "-I" + str(fixtures), str(ROOT / "tests/runtime_spawning.c"),
                str(ROOT / "src/enemy.c"), "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
