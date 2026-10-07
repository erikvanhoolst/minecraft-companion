"""Personal designs import safely and retain checklist progress across updates."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from import_redstone import import_notebook, validate


class NotebookImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source.json"
        self.output = self.root / "nested/redstone.json"
        self.example = json.loads((ROOT / "docs/examples/redstone.json").read_text())
        self.write(self.source, self.example)

    def write(self, path, data):
        path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

    def test_initial_import_and_merge_preserve_progress_and_personal_text(self):
        import_notebook(self.source, self.output)
        existing = json.loads(self.output.read_text())
        existing["designs"][0]["steps"][0]["done"] = True
        self.write(self.output, existing)
        updated = copy.deepcopy(self.example)
        updated["designs"][0]["notes"] = "Eigen notities: één blok hoger.\nVertraging: twee ticks."
        updated["designs"][0]["steps"][1]["text"] = "Changed instruction"
        new = copy.deepcopy(updated["designs"][0])
        new["name"] = "Tweede ontwerp"
        updated["designs"].append(new)
        self.write(self.source, updated)
        import_notebook(self.source, self.output)
        result = json.loads(self.output.read_text())
        self.assertEqual(len(result["designs"]), 2)
        self.assertTrue(result["designs"][0]["steps"][0]["done"])
        self.assertFalse(result["designs"][0]["steps"][1]["done"])
        self.assertEqual(result["designs"][0]["notes"], updated["designs"][0]["notes"])
        self.assertEqual(result["active"], 0)

    def test_repeated_instructions_keep_each_occurrences_progress(self):
        data = copy.deepcopy(self.example)
        data["designs"][0]["steps"] = [{"text": "Place dust", "done": False}] * 2
        self.write(self.source, data)
        import_notebook(self.source, self.output)
        existing = json.loads(self.output.read_text())
        existing["designs"][0]["steps"][0]["done"] = True
        self.write(self.output, existing)
        import_notebook(self.source, self.output)
        steps = json.loads(self.output.read_text())["designs"][0]["steps"]
        self.assertEqual([step["done"] for step in steps], [True, False])

    def test_replace_and_empty_steps_notes(self):
        import_notebook(self.source, self.output)
        updated = copy.deepcopy(self.example)
        updated["designs"][0].update(name="Empty", steps=[], notes="")
        self.write(self.source, updated)
        import_notebook(self.source, self.output, replace=True)
        self.assertEqual(json.loads(self.output.read_text()), updated)

    def test_invalid_input_never_changes_existing_file(self):
        import_notebook(self.source, self.output)
        original = self.output.read_bytes()
        variants = []
        for field, value in [("name", ""), ("name", "é" * 33), ("notes", "x" * 4097),
                             ("notes", "tab\there"), ("schema", ["..", "."]),
                             ("schema", ["? invalid"]), ("schema", ["w" * 17]),
                             ("schema", ["."] * 13), ("steps", [{"text": "x", "done": 1}]),
                             ("steps", [{"text": "x" * 193, "done": False}])]:
            data = copy.deepcopy(self.example)
            data["designs"][0][field] = value
            variants.append(data)
        for field, value in [("version", True), ("version", 1.0), ("active", -1), ("active", 2**64),
                             ("active", False), ("designs", []), ("designs", self.example["designs"] * 2)]:
            data = copy.deepcopy(self.example); data[field] = value; variants.append(data)
        for data in variants:
            with self.subTest(data=data):
                self.write(self.source, data)
                with self.assertRaises(ValueError):
                    import_notebook(self.source, self.output)
                self.assertEqual(self.output.read_bytes(), original)

    def test_corrupt_existing_file_is_preserved_even_for_replace(self):
        self.output.parent.mkdir()
        self.output.write_text("{broken")
        with self.assertRaises(ValueError):
            import_notebook(self.source, self.output, replace=True)
        self.assertEqual(self.output.read_text(), "{broken")

    def test_symlink_and_oversize_input_are_refused(self):
        self.output.parent.mkdir()
        self.output.symlink_to(self.source)
        with self.assertRaises(ValueError):
            import_notebook(self.source, self.output)
        self.source.write_bytes(b" " * (256 * 1024 + 1))
        with self.assertRaises(ValueError):
            import_notebook(self.source, self.output)

    def test_merge_limit_leaves_file_unchanged(self):
        data = copy.deepcopy(self.example)
        data["designs"] = [dict(copy.deepcopy(data["designs"][0]), name=f"Design {i}") for i in range(16)]
        self.write(self.source, data)
        import_notebook(self.source, self.output)
        original = self.output.read_bytes()
        self.write(self.source, self.example)
        with self.assertRaises(ValueError):
            import_notebook(self.source, self.output)
        self.assertEqual(self.output.read_bytes(), original)

    def test_cli_data_directory(self):
        run = subprocess.run([sys.executable, str(ROOT / "scripts/import_redstone.py"),
                              "--source", str(self.source), "--data-directory", str(self.output.parent)],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("Tap Reload", run.stdout)
        validate(json.loads(self.output.read_text()))


if __name__ == "__main__":
    unittest.main()
