// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/mods/dsmod_module_abi.h"
#include "mc_assets.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mc_waypoints {
struct Navigation {
    double distance{}, bearing{}, relative{};
    std::int64_t vertical{};
    bool arrived{};
};
// North is -Z; headings and arrow rotations are clockwise from up.
Navigation Navigate(double x, double y, double z, double heading,
                    double target_x, double target_y, double target_z);
mc_assets::Image ArrowImage();

class Waypoints {
public:
    static constexpr int PageSize = 6;
    explicit Waypoints(const char* config = nullptr);
    void Sample(const EdenDsmodHostApi& host);
    bool OnAction(const char* action, std::int64_t argument);
    std::optional<mc_assets::Image> LoadTrail(std::string_view key);

private:
    struct Point {
        std::uint64_t id{};
        std::string name;
        std::int64_t x{}, y{}, z{};
    };
    struct Group {
        std::vector<Point> points;
        std::optional<Point> death;
        struct Step { std::int64_t x{}, z{}; bool start{}; };
        std::vector<Step> death_trail;
        std::uint64_t selected{};
        int page{};
    };
    using Scope = std::pair<std::string, std::int64_t>;
    void Load();
    void Save();
    void Publish(const EdenDsmodHostApi& host);
    void TrackDeath(bool health_ok, std::int64_t health);
    void PublishDeath(const EdenDsmodHostApi& host, const Group* group);
    static constexpr std::size_t MaxTrail = 2048;
    std::vector<Group::Step> live_trail;
    Point last_alive;
    bool alive{}, trail_start{true}, show_death_trail{};
    std::mutex trail_mutex;
    struct TrailImage { std::string key; mc_assets::Image image; };
    std::array<TrailImage, 2> trail_images;
    std::uint64_t trail_serial{}, drawn_death{};
    double drawn_x{}, drawn_z{}, drawn_view{};
    std::map<Scope, Group> groups;
    Scope current;
    std::filesystem::path file;
    std::string storage_status, action_status;
    std::uint64_t next_id{1};
    std::int64_t x{}, y{}, z{};
    double heading{}, map_px{}, map_pz{}, map_view{};
    bool ready{}, persistence_blocked{};
};
} // namespace mc_waypoints
