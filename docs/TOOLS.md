# Tools and desktop testing

All scripts read their machine-specific paths from `local.env` (copy `local.env.example`).

## Build and install

| Script | Purpose |
|---|---|
| `scripts/build.sh [linux\|android\|package\|all]` | Builds the module for Linux (desktop tests) and Android arm64 (the handheld) and packs `dist/0100D71004694000.dsmod.zip`. |
| `scripts/build_dualscreen_package.py` | The package builder from the Eden Duo companions repository (reproducible zip, SHA-256 pins in `package.json` and the manifest). Called by `build.sh`. |
| `scripts/install_desktop.sh` | Unpacks the zip into `$EDEN_DATA/load/0100D71004694000/Minecraft` for `eden-cli`. |
| `scripts/thor.sh install` | Copies the package into Eden Duo's load folder on the handheld over adb. The in-app installer works as well: long-press Minecraft, Add-ons, Install, Dual screen mods. |

## Desktop session

| Script | Purpose |
|---|---|
| `scripts/run_desktop.sh [--aux-window]` | Launches Minecraft in `eden-cli` with the live console (`EDEN_DSMOD_CMD`), a virtual second screen and screenshots under `$MC_RUN` (default `/tmp/mc`). `MC_FILTER` sets the log filter, `MC_VK_ICD` the Vulkan driver, `MC_ICON_DUMP=<dir>` makes the `items` command write every icon as raw RGBA. |
| `scripts/mc.sh cmd\|btn\|shot\|value ...` | Drives that session through `duo.py`: Eden console commands, `.btn` input scripts (`"A 150"`, `"wait 2000"`, `"DDown 150"`), two-screen screenshots, published values (`mc.sh value inv.ready inv.slot0.icon`). |
| `scripts/dbg.sh "<cmd>" ...` | Runs the module's own research console (see below) and prints its output. |
| `scripts/probe.py` | Heap helpers on top of the Eden console (`vt <Class>`, `dump <addr>`), using `$MC_ANALYSIS/rtti.json`. |

After `scripts/build.sh` and `scripts/install_desktop.sh`, `scripts/mc.sh cmd reload` loads the new module into the running game.

## Research console in the module

`native/mc_debug.cpp` reads commands from `user:mcdbg.txt` (`<Eden data>/dualscreen/user/0100D71004694000/mcdbg.txt`) whenever the file changes, and writes `mcdbg:` lines to the Eden log. It works on the handheld too (`scripts/thor.sh dbg "inv"`, then `scripts/thor.sh mcdbg`).

| Command | Output |
|---|---|
| `layout` | The active Layout table |
| `inv` | The resolved inventory: every non-empty slot with count, aux and item pointer |
| `items` | Every Item object in memory with its icon key and whether the icon resolves (1.2.x) |
| `item <addr>` | Strings and small ints at each offset of an object |
| `dump <addr> [len]`, `str <text>`, `vt <offset>`, `ptr <addr>` | Hex dump, string search, vtable search, pointer search |
| `strm <text>` | String search in the main image (static objects such as the `Attribute`s) |
| `player`, `floats <addr\|player> [len]`, `vecs <addr\|player> [len]` | The local player object; the float-looking words and the vector-shaped fields of an object |
| `find <f32\|i32\|i16\|u64> <value> [<addr\|player> <len>]` | A value in a range, or in every mapped page |
| `attr <main offset>`, `amap [addr]` | AttributeInstances of a static Attribute (flags the player's); the entries of an attribute map, by default the player's |
| `save <addr\|player> <len> <file>`, `snap <dir>` | Desktop only: raw guest bytes to a file; every mapped page of the game (about 3 GiB in a world) as `<dir>/<addr>.bin` plus the main image, for offline analysis with Python (`mmap`). The map's chunk layout was found that way |

To check a value against the game, write it with Eden's console (`scripts/mc.sh cmd writeb <addr> <byte>`) and look at the HUD: that is how health, hunger and armor were first confirmed.

Driving the game through `.btn` scripts: a button press goes to both virtual pads (player 1 and the handheld port), so `Plus` can make Minecraft add a split-screen player 2. Prefix the step with `P1:` (`P1:Plus 200`, added in `patches/eden-duo-desktop.patch`) to press player 1 only; that opens the pause menu without a second player. With the software Vulkan driver the game runs at 1-5 fps and short presses fall between frames; use the GPU's driver (`MC_VK_ICD`, see `local.env.example`). The desktop run sometimes stops right after a world loads: the log fills with `HW.Memory Unmapped Read` from the game and frames stop. That is the emulated game, not the module (its reads are checked with `is_mapped` first and do not log); kill `eden-cli` and start again.

## Research scripts

| Script | Purpose |
|---|---|
| `research/nso_reloc.py <main.bin> <out> [Class ...]` | Relocates a decompressed `main` NSO and finds RTTI typeinfo and vtables by class name. The game keeps its Itanium RTTI names, so vtables are found without symbols. Output: `rtti.json`. |
| `research/icon_resolver.py <romfs> <name>` | Python prototype of the icon lookup over unpacked resource packs. |

## Desktop emulator patches

The Eden Duo APK on the handheld is used unchanged. The desktop `eden-cli` needed local patches to run Minecraft; they are in `patches/eden-duo-desktop.patch` (apply to igawa6/eden-duo v1.1.0):

- `bsd.cpp`: refuse AF_INET6 sockets with `EAFNOSUPPORT`. Without it Minecraft 1.26.13 panics on its first network call. The handheld's Eden Duo has the same bug, which is why 1.26.13 does not run there.
- `k_page_table_base.cpp`: tolerate a stale DeviceShared attribute in Lock/UnlockForTransferMemory and UnmapPhysicalMemory (left behind by the HLE nvmap).
- `application_functions.cpp`: `GetPseudoDeviceId` no longer crashes when a title's control data is missing (the base game without update).
- `yuzu_cmd/yuzu.cpp`: `.btn` steps prefixed `P1:` press player 1 only (no split-screen join on `Plus`).
- `svc_physical_memory.cpp`, `svc_transfer_memory.cpp`: diagnostics only.
- Config: `airplane_mode=true`. Minecraft 1.2.12 runs at 60 fps on the Intel UHD 620 with the Mesa Vulkan driver (`MC_VK_ICD=/usr/share/vulkan/icd.d/intel_icd.json`). Without a usable GPU, the software driver (`vulkan-swrast`, `lvp_icd.json`) works at 1-5 fps. 1.26.13 renders black on both.
