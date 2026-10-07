// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/mods/dsmod_module_abi.h"
#include "mc_assets.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
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

private:
    struct Point {
        std::uint64_t id{};
        std::string name;
        std::int64_t x{}, y{}, z{};
    };
    struct Group {
        std::vector<Point> points;
        std::uint64_t selected{};
        int page{};
    };
    using Scope = std::pair<std::string, std::int64_t>;
    void Load();
    void Save();
    void Publish(const EdenDsmodHostApi& host);
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
