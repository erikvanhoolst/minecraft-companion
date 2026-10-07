# Build projects and material counts

Open **Projects** from the tab bar. Tap **New project** to create another list, and use
**Name** to cycle House, Tower, Bridge, Storage, Farm, Mine and Project labels. Previous/Next
selects another project. The native input ABI has no text entry, so project names are presets
in the interface; arbitrary names can also be supplied in the persisted JSON file while the
module is stopped.

Add a common building material with the lower Previous/Next picker and **Add chosen material**.
The picker includes cobblestone, oak/spruce/birch planks, glass, stone, oak wood, dirt, sand,
bricks, stone bricks and glass panes. For any other material, hold it in the game's selected
hotbar slot and tap **Add held hotbar item**. This preserves its exact auxiliary variant.
New requirements start at 64. Tap a material row, then use ±1, ±16 and ±64 to adjust its target.
For example, add cobblestone and +64 for 128, oak planks and -16 twice for 32, and glass and
-16 three times for 16. Requirements stay between 1 and 999999; **Remove material** removes a
row. Six rows appear per page; Previous/Next project controls switch projects, while material
pagination is shown as `Materials 1-6 / ...`; the adjacent < and > buttons change material pages.

The inventory total covers all 36 carried slots, including the hotbar. Item identifiers
`minecraft:...`, `tile....` and `item....` match the same unprefixed identifier; other namespaces
remain distinct. Auxiliary values must match exactly, so oak and birch planks remain separate.
The remaining count is `max(0, required - owned)`. Quantities in chests, worn armor and the
crafting grid are not counted, and other projects do not reserve inventory quantities.

Unreadable inventory is shown as `? have / ? left`. A torn or failed sample clears earlier
counts. An occupied slot without a readable identifier also makes the aggregate unknown;
it never appears as an empty inventory. Material rows can still be edited during that period.
Adding a held item requires a current readable selection. A project can contain up to 24
materials; up to eight projects are retained. Duplicate materials select the existing row.
**Delete project**, followed by **Confirm delete**, removes the current project; any other
project action cancels the confirmation. Deleting the last project starts a new empty list.

## Persistence

The host passes the entire `dualscreen/manifest.json` object to the native module at creation.
Place these optional settings at its top level, then reload the package:

```json
{
  "data_directory": "/absolute/writable/companion-directory"
}
```

Each edit saves `projects.json` in that directory using a temporary file followed by an atomic
rename. Linux defaults to `$XDG_DATA_HOME/minecraft-companion`, or
`$HOME/.local/share/minecraft-companion`. Android requires an explicitly configured writable
absolute directory. An optional absolute `projects_file` overrides the directory for this
feature. Without an available directory, projects last for the session only; storage status
appears at the bottom of the Projects page. A failed write retains the current in-memory edits
and shows the failure. Malformed, oversized or symlinked existing files are left untouched,
and that session cannot save over them.

The file uses this format (the limits above apply when loading):

```json
{
  "version": 1,
  "projects": [
    {
      "name": "My house",
      "materials": [
        {"id": "cobblestone", "name": "Cobblestone", "aux": 0, "required": 128},
        {"id": "planks", "name": "Oak planks", "aux": 0, "required": 32},
        {"id": "glass", "name": "Glass", "aux": 0, "required": 16}
      ]
    }
  ]
}
```

Projects are shared companion lists across worlds. They do not write to guest memory or
Minecraft save files. Published keys are `projects.name`, `.position`, `.choice`, `.selected`,
`.summary`, `.message`, `.storage`, `.page`, `.has_materials`, `.delete_armed` and
`projects.row0` through `projects.row5` fields `.visible`, `.name`, `.counts`, `.required`,
`.owned`, `.missing`, `.known`. Unknown numeric inventory totals are -1 and `.known` is 0.
