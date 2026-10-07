// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "mc_reader.h"

namespace mc_clock {

// Stateless: every sample re-reads the player's current dimension and level. No wall-clock
// extrapolation, so pausing, sleeping, time commands and world changes follow the game.
// Publishes clock.ready, clock.ticks, clock.sunset_seconds (-1 when unavailable),
// clock.countdown_ok, clock.soon, clock.time and clock.sunset.
void Sample(const EdenDsmodHostApi& host, const mc_reader::Layout* layout,
            dsmod_sdk::int_types::u64 player, bool world_ready);

} // namespace mc_clock
