// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mc_map.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "nlohmann/json.hpp"

#include "mc_reader.h"

namespace mc_map {
namespace {

constexpr int Views[] = {64, 128, 256};
constexpr int SubChunks = 16;
constexpr int MaxViewChunks = 64; // a sane bound on the client's view grid (27 at default)
constexpr u8 FlowingWater = 8, Water = 9;
// The map item's brightness steps: lower than the block to the north, level, higher.
constexpr u32 Shades[] = {180, 220, 255};
// Ask for a new picture this often while the map is on screen (ticks at 60 Hz), and at most this
// often while the player walks. Every picture is a new key in the host's image cache (64 MiB, a
// 256-block picture is 256 KiB), so drawing more often would push the item icons out sooner.
constexpr u32 RefreshTicks = 60, MinTicks = 10;

template <class T>
bool Read(const EdenDsmodHostApi& host, u64 at, T& out) {
    return dsmod_sdk::ReadGuest<dsmod_sdk::MissingIsMapped::Reject>(host, at, &out, sizeof(T));
}

bool ReadBytes(const EdenDsmodHostApi& host, u64 at, void* out, std::size_t size) {
    return dsmod_sdk::ReadGuest<dsmod_sdk::MissingIsMapped::Reject>(host, at, out, size);
}

int FloorDiv16(int v) {
    return v >= 0 ? v / 16 : -((15 - v) / 16);
}

/// One LevelChunk of the view, read as far as the picture needs it.
struct Chunk {
    bool ok{};
    std::array<u16, 256> height{};
    std::array<u64, SubChunks> sub_ptr{};
    std::array<std::vector<u8>, SubChunks> blocks; // 4096 ids each, read on first use

    u8 Block(const EdenDsmodHostApi& host, int x, int y, int z) {
        if (y < 0 || y >= SubChunks * 16)
            return 0;
        const int s = y >> 4;
        if (!sub_ptr[s])
            return 0;
        auto& ids = blocks[s];
        if (ids.empty()) {
            ids.resize(4096);
            if (!ReadBytes(host, sub_ptr[s], ids.data(), ids.size())) {
                sub_ptr[s] = 0;
                return 0;
            }
        }
        return ids[(x << 8) | (z << 4) | (y & 15)];
    }
};

const char* FacingName(double heading) {
    static const char* const names[] = {"North",      "North-east", "East",       "South-east",
                                        "South",      "South-west", "West",       "North-west"};
    const int i = static_cast<int>(std::floor((heading + 22.5) / 45.0)) & 7;
    return names[i];
}

} // namespace

Map::Map(const char* config) : data_directory{mc_exploration::DefaultDirectory()} {
    const auto j = nlohmann::json::parse(config ? config : "{}", nullptr, false);
    if (j.is_object()) {
        const auto valid_key = [](const std::string& s) {
            return !s.empty() && s.size() <= 128 && std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32; });
        };
        if (j.contains("world_keys") && j["world_keys"].is_array())
            for (const auto& key : j["world_keys"]) {
                if (worlds.size() >= 32) break;
                if (key.is_string()) {
                    const auto text = key.get<std::string>();
                    if (valid_key(text) && std::find(worlds.begin(), worlds.end(), text) == worlds.end()) worlds.push_back(text);
                }
            }
        if (j.contains("world_key") && j["world_key"].is_string()) {
            const auto key = j["world_key"].get<std::string>();
            if (valid_key(key)) {
                auto it = std::find(worlds.begin(), worlds.end(), key);
                if (it == worlds.end() && worlds.size() < 32) { worlds.push_back(key); selected_world = worlds.size()-1; }
                else if (it != worlds.end()) selected_world = it-worlds.begin();
            }
        }
        if (j.contains("world_dimension") && j["world_dimension"].is_number_integer()) {
            const auto d = j["world_dimension"].get<std::int64_t>();
            if (d >= 0 && d <= 2) dimension = static_cast<int>(d);
        }
        if (j.contains("data_directory")) {
            data_directory.clear();
            if (j["data_directory"].is_string()) {
                const auto path = j["data_directory"].get<std::string>();
                if (path.find('\0') == std::string::npos && std::filesystem::path(path).is_absolute()) data_directory=path;
            }
        }
    }
}
Map::~Map() {
    { std::lock_guard lock{mutex}; stopping=true; wake.notify_all(); }
    if (worker.joinable()) worker.join();
}
void Map::Invalidate() {
    std::lock_guard lock{mutex};
    ++generation;
    pending = {};
    pictures = {};
    render_diag.clear(); history_diag.clear(); history_tiles=history_route=0;
    // Serial numbers are never reused: old keys may remain in the host image cache.
    want_picture=true; route_start=true;
}
void Map::Worker() {
    u64 completed{};
    for (;;) {
        Request request; EdenDsmodHostApi host;
        {
            std::unique_lock lock{mutex};
            wake.wait(lock,[&]{return stopping || (pending.serial && pending.serial!=completed);});
            if (stopping) break;
            request=pending; host=worker_host;
        }
        DrawRequest(host,request);
        completed=request.serial;
    }
    for (auto& [scope,store]:histories) store->Save();
}

bool Map::ReadPlace(const EdenDsmodHostApi& host, const mc_reader::Reader& reader, Place& out) {
    const mc_reader::Layout* lay = reader.ActiveLayout();
    const u64 player = reader.Player();
    float pos[3]{}, feet{}, yaw{};
    if (!player || !Read(host, player + lay->player_pos, pos) ||
        !Read(host, player + lay->player_aabb_min_y, feet) ||
        !Read(host, player + lay->player_yaw, yaw))
        return false;
    for (const float v : {pos[0], pos[2], feet, yaw})
        if (!std::isfinite(v) || std::fabs(v) > 3.0e7f)
            return false;
    out = {pos[0], feet, pos[2], yaw};
    return true;
}

bool Map::ReadColors(const EdenDsmodHostApi& host, const mc_reader::Reader& reader) {
    // Block::mBlocks[256], each Block holding the Color its material draws on a map.
    const mc_reader::Layout* lay = reader.ActiveLayout();
    std::array<u64, 256> blocks{};
    if (!ReadBytes(host, host.main_base + lay->block_registry, blocks.data(), sizeof(blocks)))
        return false;
    int named = 0;
    for (std::size_t id = 0; id < blocks.size(); ++id) {
        float rgba[4]{};
        colors[id] = 0;
        if (!blocks[id] || !Read(host, blocks[id] + lay->block_map_color, rgba))
            continue;
        const auto channel = [](float v) {
            return static_cast<u32>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
        };
        colors[id] = channel(rgba[0]) << 16 | channel(rgba[1]) << 8 | channel(rgba[2]);
        named += colors[id] != 0;
    }
    // Stone, grass and water must have one, or this is not the table it should be.
    return named > 32 && colors[1] && colors[2] && colors[Water];
}

bool Map::Render(const EdenDsmodHostApi& host, const Request& request, mc_assets::Image& out,
                 std::string& why) {
    const mc_reader::Layout* lay = request.layout;
    const int ox = request.origin_x, oz = request.origin_z, view = request.view;
    const auto& colors = request.colors;
    // The client's chunk views: the larger of the player's two PlayerChunkSources.
    u64 views[2]{};
    if (!Read(host, request.player + lay->player_chunk_views, views)) {
        why = "no chunk view";
        return false;
    }
    struct Grid {
        s32 min_x{}, min_z{}, size_x{}, size_z{};
        u64 chunks{};
    } grid;
    for (const u64 v : views) {
        s32 b[9]{};
        u64 begin{}, end{};
        if (!v || !Read(host, v + lay->view_bounds, b) || !Read(host, v + lay->view_chunks, begin) ||
            !Read(host, v + lay->view_chunks + 8, end))
            continue;
        const s32 size_x = b[6], size_z = b[8];
        if (size_x <= 0 || size_z <= 0 || size_x > MaxViewChunks || size_z > MaxViewChunks ||
            b[3] - b[0] + 1 != size_x || b[5] - b[2] + 1 != size_z ||
            end - begin != static_cast<u64>(size_x) * size_z * 16)
            continue;
        if (size_x * size_z > grid.size_x * grid.size_z)
            grid = {b[0], b[2], size_x, size_z, begin};
    }
    if (!grid.chunks) {
        why = "no chunk view";
        return false;
    }

    // The chunks under the picture, plus one row north of it for the shading.
    const int cx0 = FloorDiv16(ox), cx1 = FloorDiv16(ox + view - 1);
    const int cz0 = FloorDiv16(oz - 1), cz1 = FloorDiv16(oz + view - 1);
    const int ncx = cx1 - cx0 + 1, ncz = cz1 - cz0 + 1;
    std::vector<Chunk> chunks(static_cast<std::size_t>(ncx) * ncz);
    int loaded = 0;
    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            Chunk& c = chunks[static_cast<std::size_t>(cz - cz0) * ncx + (cx - cx0)];
            const int gx = cx - grid.min_x, gz = cz - grid.min_z;
            if (gx < 0 || gz < 0 || gx >= grid.size_x || gz >= grid.size_z)
                continue;
            u64 level_chunk{};
            s32 pos[2]{};
            if (!Read(host, grid.chunks + (static_cast<u64>(gz) * grid.size_x + gx) * 16,
                      level_chunk) ||
                !level_chunk || !Read(host, level_chunk + lay->chunk_pos, pos) || pos[0] != cx ||
                pos[1] != cz || !Read(host, level_chunk + lay->chunk_heightmap, c.height) ||
                !Read(host, level_chunk + lay->chunk_subchunks, c.sub_ptr))
                continue;
            c.ok = std::all_of(c.height.begin(), c.height.end(),
                               [](u16 h) { return h <= SubChunks * 16; });
            loaded += c.ok;
        }
    }
    if (!loaded) {
        why = "no chunks around the player";
        return false;
    }

    // The top of each column: the heightmap points just above the top light-blocking block;
    // plants, snow layers and glass can stand above it, and blocks with no map colour (glass,
    // air) are looked through, as the game's map does.
    struct Top {
        int y{-1};   // -1: no data (unloaded chunk)
        u8 id{};
        int depth{}; // water: blocks of water down from the surface
    };
    const int rows = view + 1; // row 0 is the one north of the picture
    std::vector<Top> tops(static_cast<std::size_t>(rows) * view);
    for (int r = 0; r < rows; ++r) {
        const int z = oz - 1 + r;
        for (int i = 0; i < view; ++i) {
            const int x = ox + i;
            Chunk& c = chunks[static_cast<std::size_t>(FloorDiv16(z) - cz0) * ncx +
                              (FloorDiv16(x) - cx0)];
            if (!c.ok)
                continue;
            const int lx = x & 15, lz = z & 15;
            int y = c.height[lz * 16 + lx];
            for (int up = 0; up < 16 && c.Block(host, lx, y, lz) != 0; ++up)
                ++y;
            --y;
            u8 id = 0;
            while (y >= 0 && colors[id = c.Block(host, lx, y, lz)] == 0)
                --y;
            Top& t = tops[static_cast<std::size_t>(r) * view + i];
            if (y < 0) {
                t.y = 0; // the void: nothing to draw, but not unloaded either
                continue;
            }
            t.y = y;
            t.id = id;
            if (id == Water || id == FlowingWater) {
                int d = 1;
                for (u8 below; d < 16 && ((below = c.Block(host, lx, y - d, lz)) == Water ||
                                          below == FlowingWater);)
                    ++d;
                t.depth = d;
            }
        }
    }

    out.width = out.height = static_cast<u32>(view);
    out.rgba.assign(static_cast<std::size_t>(view) * view * 4, 0);
    for (int j = 0; j < view; ++j) {
        for (int i = 0; i < view; ++i) {
            const Top& t = tops[static_cast<std::size_t>(j + 1) * view + i];
            const Top& north = tops[static_cast<std::size_t>(j) * view + i];
            const u32 color = t.y >= 0 ? colors[t.id] : 0;
            if (!color)
                continue;
            const int checker = (ox + i + oz + j) & 1;
            int shade = 1;
            if (t.depth) {
                const double d = t.depth * 0.1 + checker * 0.2;
                shade = d < 0.5 ? 2 : d > 0.9 ? 0 : 1;
            } else if (north.y >= 0) {
                const double d = (t.y - north.y) + (checker - 0.5) * 0.4;
                shade = d > 0.6 ? 2 : d < -0.6 ? 0 : 1;
            }
            const u32 m = Shades[shade];
            u8* px = out.rgba.data() + (static_cast<std::size_t>(j) * view + i) * 4;
            px[0] = static_cast<u8>((color >> 16 & 0xFF) * m / 255);
            px[1] = static_cast<u8>((color >> 8 & 0xFF) * m / 255);
            px[2] = static_cast<u8>((color & 0xFF) * m / 255);
            px[3] = 255;
        }
    }
    return true;
}

void Map::Publish(const EdenDsmodHostApi& host, bool ready, const Place& place) {
    // map.image is the newest picture asked for; underneath it the newest one drawn (already in
    // the host's cache), which the marker follows until the new one lands.
    u64 shown{};
    int origin_x{}, origin_z{};
    std::string why;
    s64 us{};
    std::string history_status;
    std::size_t tiles{}, routes{};
    {
        std::lock_guard lock{mutex};
        const Picture& p = pictures[0].serial != serial ? pictures[0] : pictures[1];
        shown = p.serial;
        origin_x = pictures[0].serial ? pictures[0].origin_x : pending.origin_x;
        origin_z = pictures[0].serial ? pictures[0].origin_z : pending.origin_z;
        why = render_diag;
        us = render_us;
        history_status=history_diag; tiles=history_tiles; routes=history_route;
    }
    ready = ready && serial != 0;
    const bool scoped = ready && confirmed && !worlds.empty();
    static const char* const dimensions[] = {"Overworld", "Nether", "End"};
    const std::string world_key = worlds.empty() ? "" : worlds[selected_world];
    const std::string label = world_key.empty() ? "Configure world_keys in manifest" : world_key + " / " + dimensions[dimension];
    host.publish_i64(host.userdata,"world.ready",scoped);
    host.publish_text(host.userdata,"world.key",world_key.c_str());
    host.publish_i64(host.userdata,"world.dimension",dimension);
    host.publish_text(host.userdata,"world.label",label.c_str());
    host.publish_text(host.userdata,"world.diag",scoped ? "" : world_key.empty() ? "Configure world_keys in manifest" : "Choose world + dimension, then Confirm");
    host.publish_text(host.userdata,"map.history_diag",history_status.c_str());
    host.publish_i64(host.userdata,"map.history_tiles",tiles);
    host.publish_i64(host.userdata,"map.history_routes",routes);
    host.publish_i64(host.userdata,"map.routes",show_routes);
    host.publish_i64(host.userdata, "map.ready", ready ? 1 : 0);
    host.publish_text(host.userdata, "map.diag", !ready ? diag.c_str() : why.c_str());
    char key[48];
    std::snprintf(key, sizeof(key), "module:mc:mapview/%llu", static_cast<unsigned long long>(serial));
    host.publish_text(host.userdata, "map.image", ready ? key : "");
    std::snprintf(key, sizeof(key), "module:mc:mapview/%llu", static_cast<unsigned long long>(shown));
    host.publish_text(host.userdata, "map.image_prev", ready && shown ? key : "");
    host.publish_i64(host.userdata, "map.view", view);
    host.publish_i64(host.userdata, "map.render_us", us);
    host.publish_f64(host.userdata, "map.px", place.x - origin_x);
    host.publish_f64(host.userdata, "map.pz", place.z - origin_z);
    // Yaw 180 faces north (-Z) and -90 east: clockwise from north is yaw + 180.
    const double heading = std::fmod(std::fmod(place.yaw + 180.0, 360.0) + 360.0, 360.0);
    host.publish_f64(host.userdata, "map.heading", heading);
    host.publish_i64(host.userdata, "map.x", static_cast<s64>(std::floor(place.x)));
    host.publish_i64(host.userdata, "map.y", static_cast<s64>(std::floor(place.y + 0.001)));
    host.publish_i64(host.userdata, "map.z", static_cast<s64>(std::floor(place.z)));
    host.publish_text(host.userdata, "map.facing", ready ? FacingName(heading) : "");
}

void Map::Sample(const EdenDsmodHostApi& host, const mc_reader::Reader& reader) {
    const mc_reader::Layout* lay = reader.ActiveLayout();
    Place place;
    const u64 player=reader.Player();
    if (!player || (previous_player && previous_player!=player)) {
        if (player_was_ready || (previous_player && previous_player!=player)) { confirmed=false; Invalidate(); }
        player_was_ready=false;
    }
    if (player) previous_player=player;
    if (!lay || !lay->player_chunk_views) {
        diag = "no map for this game version";
        Publish(host, false, place);
        return;
    }
    if (!reader.Player() || !ReadPlace(host, reader, place)) {
        if (player_was_ready) { confirmed=false; Invalidate(); player_was_ready=false; }
        diag = "waiting for the player";
        Publish(host, false, place);
        return;
    }
    player_was_ready=true;
    if (colors_player != reader.Player()) {
        if (!ReadColors(host, reader)) {
            diag = "no block colours";
            Publish(host, false, place);
            return;
        }
        colors_player = reader.Player();
        want_picture = true;
    }
    diag.clear();

    // Capture once a second, and at most six times a second while walking, independently of
    // whether the map tab is visible. Requests may supersede a slow render.
    ++ticks_since_request;
    const int bx = static_cast<int>(std::floor(place.x)), bz = static_cast<int>(std::floor(place.z));
    const bool moved = bx != last_block_x || bz != last_block_z;
    if (want_picture || ticks_since_request >= RefreshTicks || (moved && ticks_since_request >= MinTicks)) {
        Request next;
        next.serial=++serial; next.origin_x=bx-view/2; next.origin_z=bz-view/2; next.view=view;
        next.player=reader.Player(); next.layout=lay; next.colors=colors;
        next.scoped=confirmed && !worlds.empty();
        if (next.scoped) next.world=worlds[selected_world];
        next.dimension=dimension; next.player_x=bx; next.player_z=bz;
        {
            std::lock_guard lock{mutex};
            next.route_start=route_start; next.generation=generation; pending=next; worker_host=host;
            if (!worker.joinable()) worker=std::thread{&Map::Worker,this};
            wake.notify_one();
        }
        last_block_x=bx; last_block_z=bz; ticks_since_request=0; want_picture=false;
    }
    Publish(host, true, place);
}

bool Map::OnAction(const char* action, s64) {
    if (!action)
        return false;
    if (std::strcmp(action,"world_next")==0) {
        if (worlds.size()<2) return false;
        selected_world=(selected_world+1)%worlds.size(); confirmed=false; Invalidate(); return true;
    }
    if (std::strcmp(action,"world_dimension")==0) {
        dimension=(dimension+1)%3; confirmed=false; Invalidate(); return true;
    }
    if (std::strcmp(action,"world_confirm")==0) {
        if (worlds.empty() || !player_was_ready) return false;
        confirmed=true; Invalidate(); return true;
    }
    if (std::strcmp(action,"map_routes")==0) { std::lock_guard lock{mutex}; show_routes=!show_routes; want_picture=true; return true; }
    const bool in = std::strcmp(action, "map_zoom_in") == 0;
    if (!in && std::strcmp(action, "map_zoom_out") != 0)
        return false;
    const auto* it = std::find(std::begin(Views), std::end(Views), view);
    if (in && it != std::begin(Views))
        view = *--it;
    else if (!in && it + 1 < std::end(Views))
        view = *++it;
    else
        return false; // already at the end: refuse, so the button gives the refused haptic
    Invalidate();
    return true;
}

std::optional<mc_assets::Image> Map::Load(const EdenDsmodHostApi&, std::string_view key) {
    if (!key.starts_with("mapview/")) return std::nullopt;
    key.remove_prefix(8); u64 wanted{};
    const auto result=std::from_chars(key.data(),key.data()+key.size(),wanted);
    if (result.ec!=std::errc{} || result.ptr!=key.data()+key.size() || !wanted) return std::nullopt;
    std::lock_guard lock{mutex};
    for (const Picture& p:pictures) if (p.serial==wanted && !p.image.rgba.empty()) return p.image;
    return std::nullopt; // the host retries when the worker completes
}
bool Map::DrawRequest(const EdenDsmodHostApi& host,const Request& request) {
    mc_assets::Image live;
    std::string why;
    const auto t0=std::chrono::steady_clock::now();
    const bool ok=Render(host,request,live,why);
    bool draw_routes{};
    {
        std::lock_guard lock{mutex};
        if (request.generation!=generation || pictures[0].serial>request.serial || stopping) return false;
        draw_routes=show_routes;
    }
    // Worker owns all stores. Filesystem work is outside the tick mutex. Each store keeps its
    // full identity even when the selected scene changes, and results are rechecked at commit.
    if (request.scoped) {
        if (history && !history->IsScope(request.world,request.dimension)) history->Save();
        const std::pair scope{request.world,request.dimension};
        auto found=histories.find(scope);
        if (found==histories.end()) {
            if (histories.size()==4) {
                // Disk-backed scopes reload on demand. Session-only scopes have a bounded cache.
                auto old=histories.begin(); old->second->Save();
                if (history==old->second.get()) history=nullptr;
                histories.erase(old);
            }
            auto store=std::make_unique<mc_exploration::History>(request.world,request.dimension,data_directory);
            store->Load();
            found=histories.emplace(scope,std::move(store)).first;
        }
        history=found->second.get();
        if (ok) history->Merge(request.origin_x,request.origin_z,live);
        history->Visit(request.player_x,request.player_z,request.route_start);
        const auto now=std::chrono::steady_clock::now();
        if (now-last_save>=std::chrono::seconds(5)) { history->Save(); last_save=now; }
        live=history->Draw(request.origin_x,request.origin_z,request.view,draw_routes);
    }
    std::lock_guard lock{mutex};
    if (request.generation!=generation || pictures[0].serial>request.serial || stopping) return false;
    if (request.scoped) {
        route_start=false;
        history_diag=history->Diagnostic(); history_tiles=history->TileCount(); history_route=history->RouteCount();
    }
    Picture picture{request.serial,request.origin_x,request.origin_z,std::move(live)};
    if (picture.image.rgba.empty()) {
        picture.image.width=picture.image.height=request.view;
        picture.image.rgba.resize(static_cast<std::size_t>(request.view)*request.view*4);
    }
    render_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count();
    render_diag=why;
    pictures[1]=std::move(pictures[0]); pictures[0]=std::move(picture);
    wake.notify_all();
    return true;
}

} // namespace mc_map
