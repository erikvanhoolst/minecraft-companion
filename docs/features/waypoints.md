# Waypoints and navigation

Open **Waypoints** and tap **Base**, **Village**, **Mine** or **Place** to mark your
current block coordinates. The newly marked place becomes the destination. Tap
any saved row to navigate there; **Previous** and **Next** browse the list. Repeated
names receive a number, such as `Base 2`. Each world and dimension supports up to
64 waypoints. **Stop** clears navigation while retaining the place; **Delete**
removes the selected place.

The arrow points relative to your current heading: up means ahead, right means
turn right. Distance measures horizontal X/Z displacement in blocks. Height
difference shows destination Y minus your Y separately. Within two horizontal
blocks the screen says `At destination (horizontal)`; compare height before
assuming you have reached a cave or elevated build. The Map page also shows the
selected destination, arrow, distance, and a green marker while it is inside the
current viewport.

Lists and active destinations are isolated by `world.key` and `world.dimension`.
They are available only when both `world.ready` and `map.ready` are set. The world
identity feature requires explicit world/dimension selection and confirmation
because this repository has no verified offsets for a persistent game-world ID
or dimension. Confirm the identity on **Map** before marking a place, and select
the correct world/dimension when switching games or portals. Returning to a
previous scope restores its selected destination. Unsupported or disconnected
worlds clear arrows, markers and list rows instead of showing stale navigation.

## Local storage

The module receives the complete `dualscreen/manifest.json` object as config.
Add these optional keys at its top level, alongside `pages` and `actions`:

```json
{
  "data_directory": "/absolute/path/to/minecraft-companion",
  "world_key": "My survival world",
  "world_dimension": 0
}
```

`waypoints.json` is stored inside `data_directory`, independently of exploration
and building projects. On Linux, absent `data_directory` defaults to
`$XDG_DATA_HOME/minecraft-companion`, or
`$HOME/.local/share/minecraft-companion`. Android requires an explicit absolute
directory the emulator process can write. Optional `waypoints_file` overrides
the full file path and must also be absolute. Invalid explicit storage config
never silently falls back to a different location. The page displays whether
data is saved, session only, or a write failed.

Waypoint add, select, stop and delete save immediately via an exclusively created
temporary file followed by atomic rename. Writes use local host storage; no guest
memory or game save is modified. Files over 2 MiB, symlinks, nonregular files,
malformed JSON and invalid coordinate records are preserved and storage becomes
session only. Fix or move the faulty file before restarting. The version-1 file
contains a `worlds` array, each entry with `key`, `dimension`, `selected`, and
`points` (`id`, `name`, `x`, `y`, `z`). Advanced users can change names while the
emulator is stopped. Runtime 18 offers no keyboard text-entry API, so the in-app
creation flow uses the four labels above.

Navigation uses the map reader's published coordinates and heading. It does not
infer game offsets, search for terrain, plan safe walking routes, or account for
portal travel; the arrow indicates a straight horizontal direction.
