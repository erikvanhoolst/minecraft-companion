"""Regression checks for the portable package contract, using temporary fixtures."""

import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from build_dualscreen_package import PackageError, build_package, validate_staged_package


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / "package"
        self.dualscreen = self.package / "dualscreen"
        self.dualscreen.mkdir(parents=True)
        self.manifest = {"format": 1, "title_id": "0100D71004694000", "name": "Test",
                         "min_runtime": 18, "requires_module": True, "font": "file:font.txt"}
        self.write_manifest()
        (self.dualscreen / "font.txt").write_text("font reference")
        self.module = self.root / "module.so"
        self.module.write_bytes(b"test native payload")

    def write_manifest(self):
        (self.dualscreen / "manifest.json").write_text(json.dumps(self.manifest))

    def build(self, **overrides):
        args = dict(package=self.package, output=self.root / "dist", version="0.3.0",
                    modules=[f"linux-x86_64={self.module}"], module_build_ids=["D8B7E605E809E80C"])
        args.update(overrides)
        return build_package(**args)

    def test_runtime_and_hash_survive_packaging(self):
        with zipfile.ZipFile(self.build()) as archive:
            metadata = json.loads(archive.read("package.json"))
            manifest = json.loads(archive.read("dualscreen/manifest.json"))
            self.assertEqual(metadata["min_runtime"], 18)
            self.assertEqual(metadata["module"], manifest["module"])
            library = metadata["module"]["libraries"]["linux-x86_64"]
            self.assertEqual(library["sha256"], hashlib.sha256(self.module.read_bytes()).hexdigest())

    def test_identical_inputs_produce_identical_zip(self):
        first = self.build().read_bytes()
        self.assertEqual(first, self.build().read_bytes())

    def test_missing_native_module_is_rejected(self):
        with self.assertRaises(PackageError):
            self.build(modules=[])

    def test_missing_referenced_asset_is_rejected(self):
        (self.dualscreen / "font.txt").unlink()
        with self.assertRaises(PackageError):
            self.build()

    def test_asset_path_traversal_is_rejected(self):
        self.manifest["font"] = "file:../module.so"
        self.write_manifest()
        with self.assertRaises(PackageError):
            self.build()

    def test_symlink_is_rejected(self):
        (self.dualscreen / "linked.so").symlink_to(self.module)
        with self.assertRaises(PackageError):
            self.build()

    def test_invalid_runtime_is_rejected(self):
        for value in (0, -1, True, "18"):
            with self.subTest(value=value):
                self.manifest["min_runtime"] = value
                self.write_manifest()
                with self.assertRaises(PackageError):
                    self.build()

    def test_modified_module_is_rejected(self):
        staged = self.root / "staged"
        with zipfile.ZipFile(self.build()) as archive:
            archive.extractall(staged)
        metadata = json.loads((staged / "package.json").read_text())
        library = metadata["module"]["libraries"]["linux-x86_64"]
        (staged / "dualscreen" / library["path"]).write_bytes(b"changed")
        with self.assertRaises(PackageError):
            validate_staged_package(staged, metadata, metadata["title_id"])


if __name__ == "__main__":
    unittest.main()
