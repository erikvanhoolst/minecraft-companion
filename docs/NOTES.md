# Technical notes

Minecraft (Bedrock Edition) for Switch, title `0100D71004694000`. The module supports two game builds; `native/mc_reader.cpp` holds one `Layout` per build.

## Build D8B7E605E809E80C: Minecraft v1.2.12 (base game, no update)

This is what the AYN Thor runs. Everything below was verified live on the desktop with the player's save.

| Item | Value |
|---|---|
| Inventory | object with vptr main+0x31FAD28 (vtable main+0x31FAD18 + 0x10) |
| `FillingContainer::mItems` | vector at +0xB0 (begin), +0xB8 (end), +0xC0 (cap) |
| Player back-pointer | +0xC8, vptr LocalPlayer main+0x2A27358 |
| ItemInstance | 0x80 bytes, not polymorphic: Item* +0x00, Block* +0x08, aux u16 +0x18, count u8 +0x1A |
| Selected hotbar slot | int at LocalPlayer +0x1A18 |
| Item | registry name std::string at +0x48 (e.g. `diamond_axe`, `tile.cobblestone`), description id at +0x30; no icon-name field (+0xD8 belongs to MapItem) |
| Durability | `Item::mMaxDamage` u16 at +0x62, stack damage u16 in aux +0x18. Static getters: main+0x12954B4 (`ldrh w0, [x0, #0x62]`) and main+0x129E898 (`ldrh w0, [x0, #0x18]`). Live registry check: diamond pickaxe maximum 1561. |
| Resource packs | `romfs:/resource_packs/vanilla.zip`, `vanilla_base.zip` (deflate, backslash paths; `native/mc_zip.cpp`) |
| Item description id | std::string at +0x30 (`item.diamond_chestplate`, `item.planks`) |
| Block of a block item | ItemInstance +0x08 points at the BlockLegacy: description id std::string at +0x10 (`tile.planks`), name at +0x28 |
| Health, hunger | `Actor::mAttributes` at player +0x1140: a BaseAttributeMap, libc++ `unordered_map<std::string, AttributeInstance>` (first node at +0x10; node = next, hash, key, value at +0x28). An AttributeInstance points at its static Attribute at +0x10 and holds {min, max, value} twice: default at +0x70, current at +0x7C (current max +0x80, value +0x84). The Attributes are statics in main: `minecraft:health` main+0x338F040, `minecraft:player.hunger` main+0x3399B88 (`minecraft:absorption` main+0x338F0E0, `player.saturation` main+0x3399BA8) |
| Air | SynchedActorData at player +0x108: `vector<unique_ptr<DataItem>>` indexed by id. Item 7 (AIR) and 43 (MAX_AIR) are `DataItem2<short>`: type byte +0x8 (1 = short), id u16 +0xA, value s16 +0xE. Both are 400 above water |
| Armor | Armor container at player +0x1118: `vector<ItemInstance>` of 4 (helmet, chestplate, leggings, boots). ArmorItem vptr main+0x2BAA6D0, defense int at +0xD8 (leather helmet 1, iron chestplate 6, diamond chestplate 8; elytra is an ArmorItem with 0) |

Icons come from the registry name (`Library::LegacyNames` in `native/mc_assets.cpp`): strip `tile.`/`item.`, camelCase to snake_case, an alias table (`wheat_seeds` → `seeds_wheat`, `beef` → `beef_raw`, ...), `golden_` → `gold_`, `wooden_` → `wood_`, then `item_texture.json`, `blocks.json` (carried textures first, except grass) or a texture file with that name. The aux value selects texture variants (log wood type, dye colour). `overlay_color` entries are applied as a tint through the texture's alpha mask (grass), and animated flipbook textures are cropped to their first frame.

Coverage on the desktop (`dbg.sh items`): 405 of 423 items. The rest are 17 technical numeric tiles that never appear in an inventory, plus banners, whose look the game composes from patterns.

The player values were checked against the game's own HUD. By writing them on the desktop: health 5.0 showed two and a half hearts, hunger 5.0 two and a half shanks, a diamond chestplate put in armor slot 1 appeared in the inventory screen and as four armor icons. In play (survival, under water): screenshots of both screens taken together showed the same 4, 3 and 2 bubbles, then no bubbles and six hearts while the player drowned (air goes below zero; the companion shows no bubbles then, like the HUD). The game draws air bubbles only under water and the armor row only with armor on; the companion shows both rows all the time (ten full bubbles above water, empty armor icons), with the HUD's bubble formula: `ceil((air - 2) * 10 / max)` full bubbles, popping ones up to `ceil(air * 10 / max)`. Above water the game resets air to the maximum at once.

### Equipment and wear

The armor vector is read twice, including its header, before publishing separate helmet, chestplate, leggings and boots slots. An empty slot is distinct from unavailable equipment data. The same stack decoder supplies icons, names and wear for inventory and equipped items; the HUD still adds their defense points.

Remaining durability is `mMaxDamage - aux` for damageable items in 1.2.12, with percentages rounded up (one remaining use never reads as 0%). Damage must be between zero and the maximum; failed reads or invalid damage publish `dur_ok = 0`, `dur = -1`. Damageable items use texture/name variant zero rather than their damage value. Non-damageable stacks retain their normal aux variants. The selected pickaxe warning uses the exact remaining/max ratio, at or below 10%, rather than a rounded percentage. It clears on selection changes, repair, empty slots, unreadable selection, or failed inventory snapshots. The 1.26.13 damage/NBT and equipment layout remains unlocated and is disabled.

The inventory layout has small wear bars (green above 25%, amber above 10%, red at or below 10%), selected-tool remaining uses and percent, and a quiet amber warning line with no popup or sound. Equipment has four labeled slots with icons, wear bars and percentages.

### Item names

The game names a stack by its description id plus `.name` in `texts/en_US.lang`. A block item (`item.planks`) is named after its block (`tile.planks`), and for many ids the aux value picks a variant first: `tile.planks` + aux 5 is `tile.planks.big_oak.name`, "Dark Oak Wood Planks". `native/mc_names.cpp` holds those variant tables (wood types, stone, colours, flowers, slabs, dyes, spawn eggs, potions, buckets, coal, skulls, tipped arrows, music discs, anvils).

Compared with the game's own inventory tooltips and hotbar popup, item by item from the creative inventory (all equal): stairs, cobblestone wall, three plank types, birch/acacia/dark oak wood; red, cyan and light blue stained glass, light blue glass pane; light gray wool; pillar quartz; mossy stone bricks; prismarine, dark prismarine; granite, diorite, andesite and their polished forms; milk, water bucket, charcoal, music disc; red and light blue bed; the three anvils; black, white and light blue banner; birch and dark oak boat; tipped arrows of night vision and swiftness; mundane, long mundane, water breathing, healing and strong healing potions, lingering water bottle, lingering night vision, strong splash swiftness; five spawn eggs; four flowers; fern, large fern, grass, double tallgrass; Jack o'Lantern; six dyes. Two rules came from this: anvil items carry their damage as aux 0-2 (the block keeps it in bits 2-3), and a strong potion keeps the plain name ("Potion of Healing"; the "II" is only in the effect line). Not handled: names given at an anvil (stored in the stack's NBT), and the game's name colours (a music disc's name is aqua in the game, white on the companion). Skulls were not seen in the game.

### Art and font

The HUD sprites come from `textures/ui/` in the player's packs (`heart`, `heart_half`, `heart_background`, `armor_*`, `hunger_*`, `bubble`, `bubble_pop`; 9x9 pixels, drawn at 6x). The companion's text uses the game's font sheet `font/default8.png` through the module's font extension: the manifest's `font` is `mc_font.txt` ("MCFONT default8"), the module measures each glyph the way the game does (last column with ink, plus one pixel) and serves the sheet as `module:mc:font/default8`.

### Map

The map tab (`native/mc_map.cpp`) draws the chunks the client holds around the player, one pixel per block, north up. Found in a full memory snapshot (`snap`) and checked live:

| Item | Value |
|---|---|
| Position | Vec3 at player +0xA0 (eye height); the bounding box at +0x178 (min x, y, z, max x, y, z), so the feet are at +0x17C. The game's "Show Coordinates" line printed the same x, feet y and z (12, 75, 4 at the spawn; -19, -24 west of it) |
| Rotation | pitch +0xD0, yaw +0xD4 (degrees). Walking forward moves along (-sin yaw, cos yaw): yaw 0 faces south (+Z), 180 north, -90 east. The marker turns by yaw + 180 |
| Chunk views | player +0x14C8 and +0x14D0 point at two `PlayerChunkSource`s; the larger one is the client's view (27 x 27 chunks at the default view distance, 11 x 11 for the other). Bounds as ints at +0xD0: min x, y, z, max x, y, z, size x, y, z. `std::vector<std::shared_ptr<LevelChunk>>` at +0x100, index (cz - min z) * size x + (cx - min x); unloaded slots are null |
| LevelChunk | Level* +0x00, Dimension* +0x08, min block +0x10, max block +0x1C, ChunkPos (int x, int z) +0x28, 16 sub chunk pointers +0xB8 (null above the top one), sub chunk count +0x138, biome ids u8[256] +0x140, grass colours u32[256] +0x240, heightmap u16[256] +0x640 (index z * 16 + x, one above the top light-blocking block) |
| Sub chunk | 4096 legacy block ids indexed x * 256 + z * 16 + y, then the data nibbles (0x3000 bytes apart) |
| Blocks | `Block::mBlocks[256]` by legacy id at main+0x33A4BE8 (a second copy at main+0x33A5D28). A Block holds its id at +0x08, description id at +0x10, name at +0x28 and its map colour at +0x70 as four floats: stone 112,112,112, grass 127,178,56, water 64,64,255, sand 247,233,163, leaves 0,124,0 (the colours of the game's map item) |

A column's colour is its topmost block with a map colour: from the heightmap up through plants and snow layers, down through blocks without one (glass). Shading follows the map item: darker (180/255) or brighter (255/255) than level (220/255) by the height step to the block north of it, with a checkerboard dither; water by its depth. The picture is drawn on the asset worker when the host asks for it (`module:mc:mapview/<n>`), so nothing is drawn while the tab is hidden; on the desktop a 128-block picture takes about 3 ms and a 256-block one about 10 ms. The paper and the player marker are the game's `textures/map/map_background` and cell 0 of `textures/map/map_icons`.

Not handled: in the Nether the heightmap starts at the roof, so the map will show the roof (not tried); blocks changed without a change in height show on the next refresh (once a second). The look was compared with the world on the top screen, not with a map item held in the game.

## Build 53E6D516A4DA5CD0: Minecraft v1.26.13 (patch v148)

Not verified live: the game does not run on the handheld's Eden Duo (IPv6 socket panic) and renders black on the desktop. The layout is from static analysis of the destructors and getters. The player's health, hunger, armor and air are not located in this build, so the companion hides those rows; item names come from the full name (`item.<name>.name`).

| Item | Value |
|---|---|
| vtables (address points) | Inventory main+0x10F3A460, PlayerInventory +0x10F3AE68, ItemStack +0x10F82638, LocalPlayer +0x10C69D18 |
| ItemStack | 0x98 bytes: WeakPtr<Item> +0x08, Block* +0x18, aux +0x20, count +0x22 |
| `FillingContainer::mItems` | +0x140 |
| Item | mIconName +0x58, mIconFrame +0x48, mFullName +0xE8 (hypotheses) |

## Finding the inventory

Minecraft maps its memory itself (`svcMapPhysicalMemory`) into 0x10'00000000..0x21'00000000, outside the heap region Eden reports. `Reader::Resolve` sweeps that window in 64 KiB steps for the Inventory vptr and validates each hit: vector shape, 36 to 72 slots, and a Player back-pointer whose vptr is LocalPlayer. Each read is done twice and compared to reject torn snapshots.

## Published values

The map publishes `map.ready`, `map.image`, `map.image_prev`, `map.view`, `map.px`, `map.pz`, `map.heading`, `map.x`, `map.y`, `map.z`, `map.facing`, `map.diag` and `map.render_us` (see `native/mc_map.h`). The inventory publishes `inv.ready`, `inv.selected`, `inv.slot<i>.count`, `inv.slot<i>.icon` (`module:mc:item/<name>/<aux>`), `inv.slot<i>.id` (the item's registry name), `inv.slot<i>.name` (the name the game shows), `inv.slot_sel_name`, `inv.diag`, `player.<stat>`, `player.<stat>_max` and `player.<stat>_ok` for health, hunger, armor and air, `hud.heart<i>`, `hud.armor<i>`, `hud.hunger<i>`, `hud.bubble<i>` (per icon: 0 empty, 1 half or popping, 2 full), `mc.sample_us`. Equipment additionally publishes `equip.ready` and `equip.slot<i>.*` (0 helmet, 1 chestplate, 2 leggings, 3 boots), with the same fields as inventory slots. Both kinds of slot publish `dur_ok`, `dur` (remaining percent, -1 unavailable), `remaining`, and `max_damage`. `inv.selected_ok` gates the hotbar highlight and selected-tool state, `inv.tool_durability` gives the remaining/max/percentage label, and `inv.pickaxe_low` gates the warning. The full list is at the top of `native/mc_reader.h`; the page layout is `package/dualscreen/manifest.json`.
