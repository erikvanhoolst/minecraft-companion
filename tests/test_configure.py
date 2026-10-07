"""Configuration must preserve the installed UI and reject unsafe input atomically."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from configure_companion import configure


class ConfigureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.manifest = Path(self.temporary.name) / "manifest.json"
        self.original = {"title_id": "0100D71004694000", "pages": [{"id": "map"}],
                         "module": {"libraries": {"linux-x86_64": {"sha256": "unchanged"}}}}
        self.manifest.write_text(json.dumps(self.original))

    def test_configures_worlds_without_changing_package_contract(self):
        configure(self.manifest, ["Mijn wereld", "Wereld twee"], "nether", "/device/storage")
        result = json.loads(self.manifest.read_text())
        self.assertEqual(result["world_key"], "Mijn wereld")
        self.assertEqual(result["world_keys"], ["Mijn wereld", "Wereld twee"])
        self.assertEqual(result["world_dimension"], 1)
        self.assertEqual(result["data_directory"], "/device/storage")
        for key in self.original:
            self.assertEqual(result[key], self.original[key])

    def test_invalid_inputs_leave_manifest_unchanged(self):
        before = self.manifest.read_bytes()
        for worlds, directory in [([""], None), (["x", "x"], None), (["x\n"], None),
                                   (["é" * 65], None), (["x"], "relative")]:
            with self.subTest(worlds=worlds, directory=directory):
                with self.assertRaises(ValueError):
                    configure(self.manifest, worlds, "overworld", directory)
                self.assertEqual(self.manifest.read_bytes(), before)

    def test_rejects_other_game_manifest(self):
        self.manifest.write_text('{"title_id":"another-game"}')
        with self.assertRaises(ValueError):
            configure(self.manifest, ["world"], "overworld", None)


if __name__ == "__main__":
    unittest.main()
