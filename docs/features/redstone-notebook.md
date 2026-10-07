# Redstone notebook

The **Redstone** tab keeps circuit plans, build instructions and personal notes on
Eden Duo's second screen while you build on the main screen. It works independently
of inventory, game memory and confirmed world/dimension. Designs are a shared
companion library, not tied to one Minecraft save.

## Touch controls

- **Previous / Next** beside the title select a saved design. The selection and
  checked steps survive module reloads.
- **Schema** shows a top-down grid with one block per cell. Arrows indicate repeater
  output direction. This is a reference drawing; it does not simulate a circuit.
- **Steps** shows four instructions at a time. Tap any instruction to toggle its
  checkmark. **Previous / Next** below the instructions expose later steps.
- **Notes** shows your text with line wrapping and page controls. Layer heights,
  repeater delays and changes to try belong here.
- **New example** adds a copy of the simple lever/lamp circuit. **Delete** followed
  by **Confirm delete** removes the current design. Other notebook actions cancel
  pending deletion. Removing the last design restores the example.
- **Reload** reads imported designs without restarting the game. Invalid input
  keeps the previous designs in memory and leaves the file untouched.

There is no keyboard input in the current runtime. Create and edit personal text
and schematics on your computer using the JSON format below, then import them.
The UI follows the app's existing English labels.

## Personal designs

Copy [the example notebook](../examples/redstone.json), edit it with a text editor,
and import it into the same storage directory configured for the companion:

```sh
python3 scripts/import_redstone.py \
  --source my-circuits.json \
  --data-directory "$HOME/.local/share/minecraft-companion"
```

Tap **Reload** on Redstone before making further checklist edits. If the app
notices that the file changed externally, it preserves that file and displays
**Notebook changed on device: tap Reload**. Changes made in the session before
reloading are replaced by the imported data.

By default the importer updates designs with matching names and appends new ones.
It retains checked steps whose instruction text is unchanged. Changed instructions
use their imported `done` value. `--replace` replaces the entire library and its
progress with the source notebook. Invalid existing files are preserved even with
`--replace`; move such a file aside explicitly before importing a repaired copy.

For Android, import to a local staging file with `--output /tmp/redstone.json`,
then copy it into the configured device directory as `redstone.json` and tap
**Reload**. For example, with the directory used by `scripts/thor.sh install`:

```sh
python3 scripts/import_redstone.py --source my-circuits.json --output /tmp/redstone.json
adb push /tmp/redstone.json \
  /sdcard/Android/data/dev.igawa6.edenduo/files/dualscreen/user/0100D71004694000/companion/redstone.json
```

## File format and schematic legend

`redstone.json` contains `version: 1`, an optional zero-based `active` selection,
and a `designs` array. Each design requires:

- `name`: unique, nonempty, up to 64 UTF-8 bytes.
- `schema`: a rectangular array of 1–12 rows, each 1–16 ASCII cells wide.
- `steps`: up to 32 objects with `text` (1–192 UTF-8 bytes, no newlines) and a
  boolean `done`. Long instructions wrap over three lines.
- `notes`: up to 4096 UTF-8 bytes. Newlines and empty notes are supported; tabs and
  other control characters are rejected.

| Cell | Meaning |
|---|---|
| `.` | Empty space |
| `#` | Solid block |
| `w` | Redstone dust |
| `L` | Lever |
| `T` | Redstone torch |
| `^`, `>`, `v`, `<` | Repeater facing up, right, down, left |
| `P` | Piston |
| `B` | Button |
| `O` | Redstone lamp |

The fixed display is 16 by 12 cells; unused cells remain blank. For multi-level
builds, describe the layers in the steps and notes or save a separately named
design for each layer. Dust drawings show connection stubs as a visual convention;
they do not calculate actual Minecraft connectivity. Confirm placement, support
blocks and circuit behavior in your game.

## Storage and checks

Up to 16 designs are saved in `<data_directory>/redstone.json`. Linux uses the same
XDG/HOME defaults as Projects. Android needs an explicitly configured absolute
writable `data_directory`. An optional absolute `notebook_file` in the installed
manifest overrides the filename. Without storage, the example and checklist edits
remain available for the session, with a visible storage status.

Each checklist edit, design selection, creation or confirmed deletion saves via a
complete temporary file and rename. Files over 256 KiB, symlinks, malformed data,
duplicate names and invalid selections are refused. A failed save retains session
changes and displays an error. Installing a new package does not bundle or replace
personal notebooks; keep your installed storage configuration after reinstalling.

`mc_notebook` CTest fixtures exercise drawing, controls, selection and completion
persistence, pagination, UTF-8, imports, invalid files and storage failures.
The `mc_notebook_module` fixture also verifies the exported module callbacks, image
routing and completion after module recreation without a running world.
`tests/test_notebook_import.py` checks merges, replacement, progress preservation,
limits and the import CLI. Manifest tests check that Redstone is reachable from
all five tabs and that their touch targets do not overlap.
