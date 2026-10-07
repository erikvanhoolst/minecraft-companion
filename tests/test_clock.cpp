// SPDX-License-Identifier: GPL-3.0-or-later
// Public clock sampling against mapped guest fixtures, including torn pointer chains.
#include "mc_clock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace mc_reader;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

struct Fixture {
    static constexpr u64 Base = 0x10'00000000ULL;
    u64 player = Base + 0x100, source = Base + 0x2000, dimension = Base + 0x3000;
    u64 level = Base + 0x4000, rules = Base + 0x6000;
    std::vector<u8> memory = std::vector<u8>(0x8000);
    EdenDsmodHostApi host{};
    const Layout* layout{};
    std::map<std::string, s64> ints;
    std::map<std::string, std::string> texts;
    u64 unreadable{}, torn{};
    unsigned reads{};

    Fixture() {
        const char* build = "D8B7E605E809E80C76FA3BD670FAB5BA00000000000000000000000000000000";
        for (int i = 0; i < 32; ++i) {
            unsigned byte{};
            std::sscanf(build + 2 * i, "%2x", &byte);
            host.build_id[i] = byte;
        }
        layout = FindLayout(host.build_id);
        host.main_base = 0x80000000;
        host.userdata = this;
        host.is_mapped = [](void* p, u64 at, u64 size) -> EdenDsmodBool {
            auto& f = *static_cast<Fixture*>(p);
            return at >= Base && at - Base <= f.memory.size() &&
                   size <= f.memory.size() - (at - Base) &&
                   !(f.unreadable && at <= f.unreadable && f.unreadable - at < size);
        };
        host.read_memory = [](void* p, u64 at, void* out, std::size_t size) -> EdenDsmodBool {
            auto& f = *static_cast<Fixture*>(p);
            std::memcpy(out, f.memory.data() + at - Base, size);
            if (at == f.torn && ++f.reads % 2 == 0)
                static_cast<u8*>(out)[0] ^= 1;
            return true;
        };
        host.publish_i64 = [](void* p, const char* key, s64 value) {
            static_cast<Fixture*>(p)->ints[key] = value;
        };
        host.publish_text = [](void* p, const char* key, const char* value) {
            static_cast<Fixture*>(p)->texts[key] = value;
        };
        Put(player, host.main_base + layout->vt_local_player);
        Put(player + layout->player_block_source, source);
        Put(source, host.main_base + layout->vt_block_source);
        Put(source + layout->source_dimension, dimension);
        Dimension(0);
        Put(dimension + layout->dimension_level, level);
        Put(level, host.main_base + layout->vt_levels[1]);
        Put<s32>(level + layout->level_time, 0);
        Put(level + layout->level_game_rules, rules);
        Put(level + layout->level_game_rules + 8, rules + 3 * 32);
        Put(level + layout->level_game_rules + 16, rules + 3 * 32);
        Put<u8>(rules + 32 + 1, 1);
        Put<u8>(rules + 32 + 4, 1);
        constexpr char name[] = "dodaylightcycle";
        Put<u8>(rules + 32 + 8, (sizeof(name) - 1) * 2);
        std::memcpy(memory.data() + rules + 32 + 9 - Base, name, sizeof(name));
    }
    template <class T> void Put(u64 at, T value) {
        std::memcpy(memory.data() + at - Base, &value, sizeof(value));
    }
    void Dimension(s32 id) {
        Put(dimension, host.main_base + layout->vt_dimensions[id]);
        Put(dimension + layout->dimension_id, id);
    }
    void Sample(bool ready = true) { mc_clock::Sample(host, layout, player, ready); }
    void At(s32 ticks, const char* time, s64 seconds, const char* sunset) {
        Put(level + layout->level_time, ticks);
        Sample();
        Check(ints["clock.ready"] == 1 && ints["clock.countdown_ok"] == 1,
              "valid Overworld sample is ready");
        Check(texts["clock.time"] == time && ints["clock.sunset_seconds"] == seconds &&
              texts["clock.sunset"] == sunset, "time and sunset boundary");
    }
    void Unavailable() {
        Sample();
        Check(ints["clock.ready"] == 0 && ints["clock.countdown_ok"] == 0 &&
              ints["clock.sunset_seconds"] == -1 && ints["clock.ticks"] == -1 &&
              ints["clock.soon"] == 0 && texts["clock.time"] == "World --:--",
              "invalid sample clears every previous clock value");
    }
};

int main() {
    Fixture f;
    f.At(0, "World 06:00", 600, "Sunset in ~10:00");
    f.At(6000, "World 12:00", 300, "Sunset in ~05:00");
    f.At(10799, "World 16:47", 61, "Sunset in ~01:01");
    Check(f.ints["clock.soon"] == 0, "warning threshold uses exact ticks");
    f.At(10800, "World 16:48", 60, "Sunset in ~01:00");
    Check(f.ints["clock.soon"] == 1, "last minute is highlighted");
    f.At(11999, "World 17:59", 1, "Sunset in ~00:01");
    f.At(12000, "World 18:00", 0, "Sunset now");
    Check(f.ints["clock.soon"] == 0, "sunset clears warning");
    f.At(12001, "World 18:00", 1200, "Next sunset in ~20:00");
    f.At(18000, "World 00:00", 900, "Next sunset in ~15:00");
    f.At(23999, "World 05:59", 601, "Next sunset in ~10:01");
    f.At(24000, "World 06:00", 600, "Sunset in ~10:00");
    f.At(192000, "World 06:00", 600, "Sunset in ~10:00");
    // Sleeping/time commands can jump backwards or forwards; pausing has no extrapolation.
    f.At(1000, "World 07:00", 550, "Sunset in ~09:10");
    for (int i = 0; i < 3; ++i) f.Sample();
    Check(f.ints["clock.sunset_seconds"] == 550, "paused time holds countdown");
    f.Put<u8>(f.rules + 32 + 4, 0);
    f.Sample();
    Check(f.ints["clock.ready"] == 1 && f.ints["clock.countdown_ok"] == 0 &&
          f.ints["clock.sunset_seconds"] == -1 && f.texts["clock.sunset"] == "Daylight cycle off",
          "disabled daylight cycle cannot promise a sunset");
    f.Put<u8>(f.rules + 32 + 4, 1);
    for (u64 at : {f.level + f.layout->level_game_rules, f.rules + 32}) {
        f.unreadable = at;
        f.Sample();
        Check(f.ints["clock.ready"] == 1 && f.ints["clock.countdown_ok"] == 0,
              "unreadable rule preserves time but hides countdown");
    }
    f.unreadable = 0;
    f.torn = f.rules + 32;
    f.Sample();
    Check(f.ints["clock.countdown_ok"] == 0, "torn rule hides countdown");
    f.torn = 0;
    f.Put<u8>(f.rules + 32 + 1, 2);
    f.Sample();
    Check(f.ints["clock.countdown_ok"] == 0, "wrong rule type cannot be interpreted as bool");
    f.Put<u8>(f.rules + 32 + 1, 1);
    f.Put<u8>(f.rules + 32 + 9, 'x');
    f.Sample();
    Check(f.ints["clock.countdown_ok"] == 0, "wrong rule name is rejected");
    f.Put<u8>(f.rules + 32 + 9, 'd');
    f.Put(f.level + f.layout->level_game_rules + 8, f.rules + 31);
    f.Sample();
    Check(f.ints["clock.countdown_ok"] == 0, "truncated rule vector is rejected");
    f.Put(f.level + f.layout->level_game_rules + 8, f.rules + 3 * 32);
    for (u64 offset : f.layout->vt_levels) {
        f.Put(f.level, f.host.main_base + offset);
        f.Sample();
        Check(f.ints["clock.ready"] == 1, "supported client/server level types");
    }
    // A new level object, such as a world switch, must replace the previous clock immediately.
    const u64 next_level = Fixture::Base + 0x5000;
    std::memcpy(f.memory.data() + next_level - Fixture::Base,
                f.memory.data() + f.level - Fixture::Base, 0x200);
    f.Put(f.dimension + f.layout->dimension_level, next_level);
    f.level = next_level;
    f.At(6000, "World 12:00", 300, "Sunset in ~05:00");
    for (int id : {1, 2}) {
        f.Dimension(id);
        f.Unavailable();
        Check(f.texts["clock.sunset"] == (id == 1 ? "No sunset in Nether" : "No sunset in End"),
              "dimensions without a sun have no countdown");
    }
    f.Dimension(0);
    for (u64 at : {f.player, f.player + f.layout->player_block_source, f.source,
                   f.source + f.layout->source_dimension, f.dimension,
                   f.dimension + f.layout->dimension_id, f.dimension + f.layout->dimension_level,
                   f.level, f.level + f.layout->level_time}) {
        f.At(11999, "World 17:59", 1, "Sunset in ~00:01");
        f.unreadable = at;
        f.Unavailable();
        f.unreadable = 0;
        f.torn = at;
        f.reads = 0;
        f.Unavailable();
        f.torn = 0;
    }
    f.Put<s32>(f.level + f.layout->level_time, -1);
    f.Unavailable();
    f.Put<s32>(f.level + f.layout->level_time, std::numeric_limits<s32>::min());
    f.Unavailable();
    f.Put<s32>(f.level + f.layout->level_time, 0);
    f.Put<s32>(f.dimension + f.layout->dimension_id, 3);
    f.Unavailable();
    f.Dimension(0);
    f.Sample(false);
    Check(f.ints["clock.ready"] == 0, "world exit clears clock");
    mc_clock::Sample(f.host, f.layout, 0, true);
    Check(f.ints["clock.ready"] == 0, "missing player clears clock");
    Layout unknown{};
    mc_clock::Sample(f.host, &unknown, f.player, true);
    Check(f.texts["clock.sunset"] == "Clock unavailable for this build", "unknown offsets disabled");
    mc_clock::Sample(f.host, nullptr, f.player, true);
    Check(f.ints["clock.ready"] == 0, "unsupported build is safe");
    const char* modern = "53E6D516A4DA5CD0C49FCE555994DA196B63E9C1000000000000000000000000";
    for (int i = 0; i < 32; ++i) {
        unsigned byte{};
        std::sscanf(modern + 2 * i, "%2x", &byte);
        f.host.build_id[i] = byte;
    }
    mc_clock::Sample(f.host, FindLayout(f.host.build_id), f.player, true);
    Check(f.ints["clock.ready"] == 0 && f.ints["clock.sunset_seconds"] == -1,
          "experimental build never uses legacy clock offsets");
    std::puts("clock fixtures passed");
}
