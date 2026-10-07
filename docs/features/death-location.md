# Death locations and return trails

Confirm the actual world and dimension on **Map** before playing. When two
consecutive reliable samples show living health followed by zero health, the
companion saves **Death** at the last valid living block coordinates. It selects
the death as your navigation destination and immediately saves locally in
`waypoints.json`, alongside your manual waypoints. Each world and dimension keeps
one latest death; another detected death replaces it. It does not use any of the
64 manual waypoint slots or remove your places when that list is full.

The red square on **Map** marks the death while it is inside the viewport,
including when you select a different destination. Tap **Death** on Map or
Waypoints to navigate there. The arrow, horizontal distance and separate height
difference work just like other waypoints. **Stop** ends navigation and retains
the death; **Delete** with Death selected removes both its marker and return trail.

Tap **Death trail: On/Off** on either page to show the recorded route before your
death in red on Map. The trail is off initially, independent of the gold **Routes**
exploration trace. Its display preference saves locally. Recording runs on every
tab while the confirmed scope, health and position are readable. A death freezes
that recording; walking after respawn leaves it intact until the next death.
The saved marker and trail survive module reload and returning to this scope.

The route samples movement at least two horizontal blocks apart and includes
the final living position. It retains up to 2,048 recent points per death and
8,192 saved points across all scopes. At the total limit, other scopes' trails
are discarded while their death markers remain. Teleports over 64 blocks and
gaps in readable health start separate segments; the app never draws a connecting
walk across those gaps. Leaving or unconfirming a scope resets the live recording.
The trail follows the observed X/Z route; compare the height difference for caves
or elevated terrain. It does not plan a safe path or connect portals.

Death detection currently requires Minecraft **1.2.12**, the build with readable
health and map positions. Zero or unknown health at startup, unreadable data,
loading screens, unconfirmed worlds and switching dimensions do not create a
marker. If health or position becomes unreadable before zero is observed, that
death cannot be detected reliably. Confirm again after loading a world or when
the player object changes, as required by the existing world identity controls.

Storage uses the same atomic saves, status messages and protection for invalid
files as [waypoints](waypoints.md). The companion reads the game and writes only
its own local data; it does not alter Minecraft saves or inventory.
