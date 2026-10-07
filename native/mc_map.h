// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Minecraft (Bedrock, Switch) map tab: a top-down picture of the chunks the game holds around the
// player, drawn the way the game's own map item draws: each column takes the map colour of its
// topmost block (the Block's own colour, read from the game), shaded by the height step to the
// block north of it, and water by its depth. North is up, one pixel per block.
//
// Published values (every sample):
//   map.ready            1 while the player's chunk view was read this tick
//   map.image            "module:mc:mapview/<n>": the newest picture (map.view pixels square)
//   map.image_prev       the picture before it, drawn underneath while the new one loads
//   map.view             blocks across the picture (64, 128 or 256)
//   map.px, map.pz       the player's position in the picture, in blocks from its top-left corner
//   map.heading          the way the player faces, degrees clockwise from north
//   map.x, map.y, map.z  the block the player stands in (y: the feet)
//   map.facing           "North", "North-east", ...
//   map.diag             why map.ready is 0
//   map.render_us        what the last picture cost to draw, in microseconds
// Actions: "map_zoom_in", "map_zoom_out".

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "core/mods/dsmod_module_abi.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include "mc_assets.h"
#include "mc_reader.h"

namespace mc_map {

using namespace dsmod_sdk::int_types;

class Map {
public:
    /// Tick thread, after Reader::Sample. Cheap: it reads the player's place and asks for a new
    /// picture; the picture is drawn by Load.
    void Sample(const EdenDsmodHostApi& host, const mc_reader::Reader& reader);
    /// Tick thread.
    bool OnAction(const char* action, s64 argument);
    /// Asset worker: the picture for a "mapview/<n>" key (without "module:mc:"). The newest one
    /// asked for is drawn here, reading the game's chunks, so the tick thread never waits for it
    /// and nothing is drawn while the map is not on screen (the host only asks for what it shows).
    std::optional<mc_assets::Image> Load(const EdenDsmodHostApi& host, std::string_view key);

private:
    struct Place {
        double x{}, y{}, z{};
        float yaw{};
    };
    /// Everything a picture is drawn from, copied for the asset worker.
    struct Request {
        u64 serial{};
        int origin_x{}, origin_z{}, view{};
        u64 player{};
        const mc_reader::Layout* layout{};
        std::array<u32, 256> colors{}; // 0x00RRGGBB per legacy block id, 0 = no map colour
    };
    struct Picture {
        u64 serial{};
        int origin_x{}, origin_z{};
        mc_assets::Image image;
    };
    bool ReadPlace(const EdenDsmodHostApi& host, const mc_reader::Reader& reader, Place& out);
    bool ReadColors(const EdenDsmodHostApi& host, const mc_reader::Reader& reader);
    static bool Render(const EdenDsmodHostApi& host, const Request& request,
                       mc_assets::Image& out, std::string& why);
    void Publish(const EdenDsmodHostApi& host, bool ready, const Place& place);

    // Shared with the asset worker, under `mutex`.
    std::mutex mutex;
    Request pending;                 // the newest picture asked for
    std::array<Picture, 2> pictures; // the two newest drawn, newest first
    std::string render_diag;         // why the last drawing failed, or empty
    s64 render_us{};                 // what the last picture cost to draw

    // Tick thread only.
    u64 serial{};                    // pending.serial
    int view{128};
    int last_block_x{}, last_block_z{};
    u32 ticks_since_request{};
    bool want_picture{true};
    u64 colors_player{};             // the player the colour table was read for
    std::array<u32, 256> colors{};
    std::string diag{"starting"};
};

} // namespace mc_map
