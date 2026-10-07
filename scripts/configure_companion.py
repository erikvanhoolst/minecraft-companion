#!/usr/bin/env python3
"""Configure world names and persistent storage in an installed companion manifest.

Eden passes the entire dualscreen/manifest.json to the native module. Run this
against the installed manifest after installation, then reload the module.
The directory refers to the machine running Eden, including Android when editing
a manifest locally before pushing it to the handheld.
"""

from __future__ import annotations

import argparse
import json
import os
import tempfile
from pathlib import Path, PurePosixPath


DIMENSIONS = {"overworld": 0, "nether": 1, "end": 2}


def configure(manifest: Path, worlds: list[str], dimension: str,
              data_directory: str | None) -> None:
    if manifest.is_symlink():
        raise ValueError("manifest must be a regular file, not a symbolic link")
    config = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(config, dict) or config.get("title_id") != "0100D71004694000":
        raise ValueError("manifest must belong to the Minecraft companion")
    if not worlds or len(worlds) > 32 or any(not world.strip() or len(world.encode("utf-8")) > 128
                         or any(ord(c) < 32 for c in world) for world in worlds):
        raise ValueError("provide up to 32 world names containing 1–128 UTF-8 bytes without control characters")
    if len(set(worlds)) != len(worlds):
        raise ValueError("world names must be unique")
    if data_directory is not None:
        if not PurePosixPath(data_directory).is_absolute():
            raise ValueError("data directory must be an absolute path on the device running Eden")
        config["data_directory"] = data_directory
    config["world_keys"] = worlds
    config["world_key"] = worlds[0]
    config["world_dimension"] = DIMENSIONS[dimension]
    payload = json.dumps(config, ensure_ascii=False, indent=1) + "\n"
    # Only replace a complete manifest. Preserve its permissions on replacement.
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=manifest.parent,
                                         prefix=f".{manifest.name}.", delete=False) as output:
            temporary = output.name
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, manifest.stat().st_mode & 0o777)
        os.replace(temporary, manifest)
        temporary = None
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True,
                        help="installed dualscreen/manifest.json, or a local copy for the handheld")
    parser.add_argument("--world", action="append", required=True,
                        help="stable world name; repeat for additional worlds")
    parser.add_argument("--dimension", choices=DIMENSIONS, default="overworld")
    parser.add_argument("--data-directory", help="absolute storage path on the device running Eden")
    args = parser.parse_args()
    try:
        configure(args.manifest, args.world, args.dimension, args.data_directory)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Configuration failed: {error}\n")
    print(f"Configured {args.manifest}: {len(args.world)} world(s), {args.dimension}. Reload the module.")


if __name__ == "__main__":
    main()
