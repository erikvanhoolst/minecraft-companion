# Exploration history

The Map page keeps discovered terrain after the game unloads its chunks. A background worker
captures the map area once a second, or up to six times a second while walking, including while
Inventory or another page is open. Existing terrain survives unloaded or unreadable columns.
A gold trace shows earlier routes; **Routes** toggles that overlay. Routes do not join after a
world/dimension confirmation or across a large teleport.

Configure the installed `dualscreen/manifest.json` with these optional top-level fields:

```json
{
  "world_keys": ["Survival", "Creative"],
  "world_key": "Survival",
  "world_dimension": 0,
  "data_directory": "/absolute/writable/minecraft-companion"
}
```

World keys are stable, case-sensitive names you choose for your saves. Use different keys for
different saves, even if they share the same displayed Minecraft name. Up to 32 unique presets,
with up to 128 UTF-8 bytes per key, are supported. `world_key` selects the initial preset; the
first preset is selected when it is absent. Dimension numbers are `0` Overworld, `1` Nether,
and `2` End. **World** cycles presets and **Dimension** cycles these three dimensions.

Select the actual world and dimension and press **Confirm** before recording history or using
waypoints. The displayed world/dimension is always visible. Confirmation is required at startup,
after selecting another preset/dimension, and after a detected player disconnection or replacement.
There are no verified world-save or dimension memory offsets in the supported layouts, so the app
cannot automatically identify a save or every portal transition. Select the destination dimension
before exploring it. A persistent guest pointer is not treated as a save identity. Without a
configured world key the live map still works, and no permanent history is recorded.

On Linux the default directory is `$XDG_DATA_HOME/minecraft-companion` (if that environment variable
is absolute), otherwise `$HOME/.local/share/minecraft-companion`. Set `data_directory` to an absolute
writable directory on Android: the module ABI exposes no app-private storage path. An empty,
relative, or invalid configured directory disables disk persistence. The map displays a session-only
notice in this mode. Presets and the initial dimension come from the manifest each time the module
is created; changing these fields requires reloading the package/game.

Each world/dimension gets a separate `map-<key-hash>-<dimension>.mch` file. Files verify the full world
key and dimension as well as a checksum, so filenames cannot silently merge identities. Snapshots
use an exclusive unique temporary file, `fsync`, and atomic rename; existing symlinks and nonregular
files are refused. Corrupt or unreadable files remain untouched and the diagnostic explains why
they could not be used. Rename the problematic file manually before recording a replacement.
The worker flushes dirty state every five seconds, at a world/dimension change, and on normal module
destruction. Abrupt termination can lose the last few seconds. Storage errors remain visible.

Memory is bounded to 8,192 terrain tiles of 16×16 blocks and 8,192 route points per scope. When a
limit is reached existing history remains and the map displays a limit notice. A session holds at
most four world/dimension stores; additional scopes reload from disk. Session-only histories can
be lost when the four-scope cache is exceeded. Recorded terrain is a map-colour snapshot rather
than game chunks and refreshes whenever it is observed again. Only the area within the current map
zoom is captured; unobserved areas stay blank. To revisit old routes, move back into their area or
zoom out. The existing Nether map still reads the roof heightmap rather than cave interiors.

Published identity values, before waypoint sampling, are `world.ready` (confirmed fresh map player),
`world.key` (full explicit key), `world.dimension` (0/1/2), `world.label`, and `world.diag`.
History diagnostics and counts are `map.history_diag`, `map.history_tiles`, `map.history_routes`;
`map.routes` exposes the overlay setting. Old worker results and cached image keys are discarded
when the map generation changes, preventing another world or zoom from reappearing on screen.

Validation: Linux build, CTest reader and exploration fixtures, and Python package tests. Fixtures
cover negative coordinates, unloaded chunks, routes, scope isolation, reload and checksum failure,
symlink refusal, bounded history, explicit identity confirmation, scope switches during a render,
and background capture without an asset-load callback. Live game/Android validation remains needed.
