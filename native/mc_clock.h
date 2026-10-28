// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "mc_reader.h"

namespace mc_clock {

// Stateless: every sample re-reads the player's current dimension and level. No wall-clock
// extrapolation, so pausing, sleeping, time commands and world changes follow the game.
// Publishes clock.ready, clock.ticks, clock.sunset_seconds (-1 when unavailable),
// clock.countdown_ok, clock.soon, clock.time and clock.sunset.
// Returns the player's dimension when two reads of the chain agreed and the Dimension object
// has that id's own vtable: id 0 Overworld, 1 Nether, 2 End, or -1 when unknown.
struct Dimension {
    int id{-1};
    dsmod_sdk::int_types::u64 address{}; // the Dimension object its chunks point at
};
Dimension Sample(const EdenDsmodHostApi& host, const mc_reader::Layout* layout,
                 dsmod_sdk::int_types::u64 player, bool world_ready);

} // namespace mc_clock
