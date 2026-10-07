#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate and import personal Redstone designs. Tap Reload on Redstone afterwards.

By default merge designs by name, preserving checked steps whose text is unchanged.
Use --replace to replace the entire notebook. The output belongs to the device
running Eden; for Android import locally and copy redstone.json to that device.
"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
import json
import os
from pathlib import Path
import tempfile

TOKENS = set(".#wLT^>v<PBO")
MAX_BYTES = 256 * 1024


def text(value: object, limit: int, *, multiline: bool = False, nonempty: bool = False) -> bool:
    return (isinstance(value, str) and (bool(value) or not nonempty)
            and len(value.encode("utf-8")) <= limit
            and not any((ord(c) < 32 and not (multiline and c == "\n")) or ord(c) == 127 for c in value))


def validate(data: object) -> dict:
    if not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1:
        raise ValueError("notebook must be an object with version 1")
    designs = data.get("designs")
    if not isinstance(designs, list) or not 1 <= len(designs) <= 16:
        raise ValueError("provide 1–16 designs")
    names = set()
    for d in designs:
        if not isinstance(d, dict) or not text(d.get("name"), 64, nonempty=True):
            raise ValueError("design name must contain 1–64 UTF-8 bytes without control characters")
        if d["name"] in names:
            raise ValueError(f"duplicate design name: {d['name']}")
        names.add(d["name"])
        rows = d.get("schema")
        if (not isinstance(rows, list) or not 1 <= len(rows) <= 12
                or any(not isinstance(row, str) or not 1 <= len(row) <= 16 or set(row) - TOKENS for row in rows)
                or len({len(row) for row in rows}) != 1):
            raise ValueError("schema must be a rectangular 1–16 by 1–12 grid using . # w L T ^ > v < P B O")
        steps = d.get("steps")
        if not isinstance(steps, list) or len(steps) > 32:
            raise ValueError("provide up to 32 steps per design")
        for step in steps:
            if (not isinstance(step, dict) or not text(step.get("text"), 192, nonempty=True)
                    or type(step.get("done")) is not bool):
                raise ValueError("each step needs text (1–192 UTF-8 bytes, one line) and a boolean done")
        if not text(d.get("notes"), 4096, multiline=True):
            raise ValueError("notes must contain up to 4096 UTF-8 bytes; newlines are allowed")
    active = data.get("active", 0)
    if type(active) is not int or not 0 <= active < len(designs):
        raise ValueError("active must be a valid design index")
    return data


def read(path: Path) -> dict:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_BYTES:
        raise ValueError("notebook must be a regular file of at most 256 KiB")
    return validate(json.loads(path.read_text(encoding="utf-8")))


def import_notebook(source: Path, output: Path, *, replace: bool = False) -> None:
    incoming = read(source)
    if output.is_symlink():
        raise ValueError("output must not be a symbolic link")
    # Refuse to overwrite malformed existing data, even when replacing.
    existing = read(output) if output.exists() else None
    if existing and not replace:
        designs = existing["designs"]
        indices = {d["name"]: i for i, d in enumerate(designs)}
        for design in incoming["designs"]:
            index = indices.get(design["name"])
            if index is None:
                designs.append(design)
            else:
                checked = defaultdict(deque)
                for step in designs[index]["steps"]:
                    checked[step["text"]].append(step["done"])
                for step in design["steps"]:
                    previous = checked[step["text"]].popleft() if checked[step["text"]] else False
                    step["done"] = step["done"] or previous
                designs[index] = design
        data = existing
    else:
        data = incoming
    validate(data)
    payload = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if len(payload) > MAX_BYTES:
        raise ValueError("merged notebook exceeds 256 KiB")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=f".{output.name}.", delete=False) as f:
            temporary = Path(f.name)
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        if output.exists():
            temporary.chmod(output.stat().st_mode & 0o777)
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="JSON containing personal designs")
    location = parser.add_mutually_exclusive_group(required=True)
    location.add_argument("--output", type=Path, help="redstone.json destination")
    location.add_argument("--data-directory", type=Path, help="same directory configured in the companion")
    parser.add_argument("--replace", action="store_true", help="replace all designs instead of merging by name")
    args = parser.parse_args()
    output = args.output if args.output else args.data_directory / "redstone.json"
    try:
        import_notebook(args.source, output, replace=args.replace)
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(1, f"Import failed: {error}\n")
    print(f"Saved {output}. Tap Reload on the Redstone tab before making further edits.")


if __name__ == "__main__":
    main()
