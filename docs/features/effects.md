# Active potion effects

Tap **Effects** on the companion's second screen. Minecraft can remain in normal play;
its inventory menu does not need to be open. The reader samples the local player's
active effects independently of which companion tab is selected.

Each row gives the effect name, strength and remaining time. Strength is the game's
zero-based amplifier plus one: amplifier 0 shows **I**, amplifier 1 shows **II**.
Levels above X use their decimal number. Time is minutes:seconds, taken from the
remaining game ticks at 20 ticks per second, rounded down like the game's timer.
An effect can show **0:00** during its final partial second. Pausing the game does
not advance an independent companion timer.

Eight rows fit on a page. **Previous** and **Next** expose the rest; expired effects,
milk-cleared effects and replaced effects follow the next readable sample. Pages
clamp when effects disappear. Names use `texts/en_US.lang` from the game's resource
packs, with English fallback names while assets load or a translation is missing.

**No active effects** appears only after a readable empty snapshot. **Waiting for
effect data** clears all old rows after an unreadable or inconsistent snapshot,
including a world unload. The unlocated Minecraft 1.26.13 effect layout displays
**Effects unavailable for this version**. This feature supports the verified
Minecraft 1.2.12 build only, with effect IDs 1 through 25.

The companion reads game memory and does not change effects or the player's inventory.
