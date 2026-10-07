// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_clock.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace mc_clock {
namespace {
using namespace dsmod_sdk::int_types;
constexpr s32 DayTicks = 24000, SunsetTicks = 12000, TicksPerSecond = 20;

template <class T> bool Read(const EdenDsmodHostApi& host, u64 at, T& out) {
    return dsmod_sdk::ReadGuest<dsmod_sdk::MissingIsMapped::Reject>(host, at, &out, sizeof(T));
}

bool IsType(const EdenDsmodHostApi& host, u64 at, u64 offset) {
    u64 vptr{};
    return offset && Read(host, at, vptr) && vptr == host.main_base + offset;
}

struct State {
    u64 source{}, dimension{}, level{};
    s32 id{-1}, time{-1};
    bool cycle_ok{}, cycle{};
    bool operator==(const State&) const = default;
};

bool ReadState(const EdenDsmodHostApi& host, const mc_reader::Layout& l,
               u64 player, State& s) {
    if (!IsType(host, player, l.vt_local_player) ||
        !Read(host, player + l.player_block_source, s.source) || !s.source ||
        !IsType(host, s.source, l.vt_block_source) ||
        !Read(host, s.source + l.source_dimension, s.dimension) || !s.dimension ||
        !Read(host, s.dimension + l.dimension_id, s.id) || s.id < 0 || s.id > 2 ||
        !IsType(host, s.dimension, l.vt_dimensions[s.id]) ||
        !Read(host, s.dimension + l.dimension_level, s.level) || !s.level ||
        !std::any_of(l.vt_levels.begin(), l.vt_levels.end(),
                     [&](u64 offset) { return IsType(host, s.level, offset); }) ||
        !Read(host, s.level + l.level_time, s.time) || s.time < 0)
        return false;

    // 1.2.12 GameRule = {set byte, type byte, padding, value at +4, name at +8}.
    // Check both vector headers, type, value and the libc++ short name before using rule 1.
    // A failed rule read still permits displaying time, but cannot promise a countdown.
    std::array<u64, 3> rules{}, after{};
    std::array<u8, 32> rule{}, again{};
    constexpr char Name[] = "dodaylightcycle";
    if (l.level_game_rules && Read(host, s.level + l.level_game_rules, rules) && rules[0] &&
        rules[0] <= rules[1] && rules[1] <= rules[2] &&
        (rules[1] - rules[0]) % rule.size() == 0 &&
        rules[1] - rules[0] >= 2 * rule.size() && rules[2] - rules[0] <= 128 * rule.size() &&
        Read(host, rules[0] + rule.size(), rule) &&
        Read(host, rules[0] + rule.size(), again) && rule == again &&
        Read(host, s.level + l.level_game_rules, after) && rules == after &&
        rule[1] == 1 && rule[4] <= 1 && rule[8] == (sizeof(Name) - 1) * 2 &&
        std::memcmp(rule.data() + 9, Name, sizeof(Name)) == 0) {
        s.cycle_ok = true;
        s.cycle = rule[4] != 0;
    }
    return true;
}
} // namespace

void Sample(const EdenDsmodHostApi& host, const mc_reader::Layout* layout,
            u64 player, bool world_ready) {
    bool ready = false, countdown_ok = false, soon = false;
    s64 ticks = -1, seconds = -1;
    std::string time = "World --:--", sunset = "World time unavailable";
    State first{}, second{};
    if (!layout || !layout->level_time || !layout->player_block_source) {
        sunset = "Clock unavailable for this build";
    } else if (world_ready && player && ReadState(host, *layout, player, first) &&
               ReadState(host, *layout, player, second) && first == second) {
        if (first.id != 0) {
            sunset = first.id == 1 ? "No sunset in Nether" : "No sunset in End";
        } else {
            ready = true;
            ticks = first.time % DayTicks;
            const int minutes = (ticks * 1440 / DayTicks + 360) % 1440;
            char text[64];
            std::snprintf(text, sizeof(text), "World %02d:%02d", minutes / 60, minutes % 60);
            time = text;
            if (!first.cycle_ok) {
                sunset = "Sunset countdown unavailable";
            } else if (!first.cycle) {
                sunset = "Daylight cycle off";
            } else {
                countdown_ok = true;
                const int remaining = (SunsetTicks - ticks + DayTicks) % DayTicks;
                seconds = (remaining + TicksPerSecond - 1) / TicksPerSecond;
                soon = remaining > 0 && remaining <= 60 * TicksPerSecond;
                if (remaining == 0) {
                    sunset = "Sunset now";
                } else {
                    std::snprintf(text, sizeof(text), "%s in ~%02lld:%02lld",
                                  ticks < SunsetTicks ? "Sunset" : "Next sunset",
                                  static_cast<long long>(seconds / 60),
                                  static_cast<long long>(seconds % 60));
                    sunset = text;
                }
            }
        }
    }
    host.publish_i64(host.userdata, "clock.ready", ready);
    host.publish_i64(host.userdata, "clock.ticks", ticks);
    host.publish_i64(host.userdata, "clock.sunset_seconds", seconds);
    host.publish_i64(host.userdata, "clock.countdown_ok", countdown_ok);
    host.publish_i64(host.userdata, "clock.soon", soon);
    host.publish_text(host.userdata, "clock.time", time.c_str());
    host.publish_text(host.userdata, "clock.sunset", sunset.c_str());
}
} // namespace mc_clock
