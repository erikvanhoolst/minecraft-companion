#!/usr/bin/env python3
"""Check the installable release archive and smoke-test its Linux module (no game needed)."""

from __future__ import annotations

import argparse
import ctypes
import json
import re
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

from build_dualscreen_package import PackageError, validate_staged_package

ROOT = Path(__file__).resolve().parents[1]
MACHINES = {"linux-x86_64": 62, "android-arm64-v8a": 183}
EXPORTS = {
    "eden_dsmod_get_module": ("dsmod_module_abi.h", "EDEN_DSMOD_MODULE_ABI"),
    "eden_dsmod_get_extensions": ("dsmod_module_extensions.h", "EDEN_DSMOD_EXT"),
    "eden_dsmod_get_font_extensions": ("dsmod_module_extensions.h", "EDEN_DSMOD_FONT_EXT"),
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise PackageError(message)


def verify_release(archive_path: Path, tag: str = "") -> None:
    source = json.loads((ROOT / "package/package.json").read_text())
    if tag:
        require(tag == f"v{source['version']}", "release tag must match package/package.json version")
        require(bool(re.fullmatch(r"v\d+\.\d+\.\d+", tag)), "release tag must be vMAJOR.MINOR.PATCH")

    with tempfile.TemporaryDirectory(prefix="mc-release-check-") as temporary:
        staged = Path(temporary)
        with zipfile.ZipFile(archive_path) as archive:
            names = archive.namelist()
            expected = {
                "package.json",
                "dualscreen/manifest.json",
                "dualscreen/mc_font.txt",
                *(f"dualscreen/modules/{platform}/{source['title_id']}.so" for platform in MACHINES),
            }
            require(len(names) == len(expected) and set(names) == expected,
                    "release must contain exactly the manifests, font reference and both native modules")
            require(archive.testzip() is None, "ZIP checksum failure")
            for member in archive.infolist():
                path = PurePosixPath(member.filename)
                require(not path.is_absolute() and ".." not in path.parts, "unsafe archive path")
                require(member.external_attr >> 16 == 0o100644, "unexpected archive file permissions")
            archive.extractall(staged)

        metadata = json.loads((staged / "package.json").read_text())
        manifest = json.loads((staged / "dualscreen/manifest.json").read_text())
        for key in ("format", "type", "title_id", "name", "version", "min_runtime"):
            require(metadata.get(key) == source[key], f"release {key} differs from source metadata")
        require(metadata.get("requires_module") is True, "native module must be required")
        require(manifest.get("min_runtime") == metadata["min_runtime"], "runtime requirements differ")
        validate_staged_package(staged, metadata, source["title_id"])
        libraries = metadata["module"]["libraries"]
        require(set(libraries) == set(MACHINES), "release needs Linux and Android modules")
        for platform, machine in MACHINES.items():
            module = staged / "dualscreen" / libraries[platform]["path"]
            payload = module.read_bytes()
            require(len(payload) >= 64 and payload[:6] == b"\x7fELF\x02\x01",
                    f"{platform} must be a little-endian ELF64 module")
            elf_type, elf_machine = struct.unpack_from("<HH", payload, 16)
            require(elf_type == 3 and elf_machine == machine, f"wrong ELF type/architecture for {platform}")
            symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide", str(module)], text=True)
            for symbol in EXPORTS:
                require(bool(re.search(rf"\bGLOBAL\s+DEFAULT\s+(?!UND\b)\S+\s+{symbol}\s*$", symbols, re.M)),
                        f"{platform} is missing exported entry point {symbol}")
            dependencies = subprocess.check_output(["readelf", "--dynamic", str(module)], text=True)
            require("libc++_shared.so" not in dependencies, f"{platform} depends on an unbundled C++ runtime")

        # Resolve and call the actual stripped module's entry points, checking ABI negotiation.
        module = ctypes.CDLL(str(staged / "dualscreen" / libraries["linux-x86_64"]["path"]))
        sdk = ROOT / "native/sdk/core/mods"
        for symbol, (header, prefix) in EXPORTS.items():
            text = (sdk / header).read_text()
            version = int(re.search(rf"#define {prefix}_VERSION (\d+)u", text)[1])
            abi_hash = int(re.search(rf"#define {prefix}_HASH UINT64_C\((0x[0-9a-fA-F]+)\)", text)[1], 16)
            getter = getattr(module, symbol)
            getter.argtypes = [ctypes.c_uint32, ctypes.c_uint64]
            getter.restype = ctypes.c_void_p
            require(bool(getter(version, abi_hash)), f"{symbol} rejected the supported ABI")
            require(not getter(0, abi_hash), f"{symbol} accepted an unsupported ABI version")
            require(not getter(version, 0), f"{symbol} accepted an unsupported ABI hash")

    print(f"Verified {archive_path.name}: v{source['version']}, runtime {source['min_runtime']}, Linux + Android")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--tag", default="")
    args = parser.parse_args()
    try:
        verify_release(args.archive, args.tag)
    except (PackageError, OSError, ValueError, KeyError, AttributeError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"Release verification failed: {exc}\n")


if __name__ == "__main__":
    main()
