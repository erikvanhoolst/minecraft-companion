// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_waypoints.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>

namespace mc_waypoints {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaxPoints = 64, MaxGroups = 128, MaxFileBytes = 8 * 1024 * 1024;
constexpr std::size_t MaxSavedTrail = 8192;
constexpr std::int64_t MaxCoordinate = 30000000;
double Normalize(double degrees) {
    return std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
}
bool Coordinate(std::int64_t value) {
    return value >= -MaxCoordinate && value <= MaxCoordinate;
}
std::int64_t BoundedInteger(const Json& value, std::int64_t minimum, std::int64_t maximum) {
    if (!value.is_number_integer() || value.get<long double>() < minimum ||
        value.get<long double>() > maximum) throw std::runtime_error("integer");
    return value.get<std::int64_t>();
}
std::filesystem::path StorageFile(const Json& config) {
    if (config.contains("waypoints_file")) {
        if (!config["waypoints_file"].is_string() || config["waypoints_file"].get<std::string>().empty())
            throw std::invalid_argument("waypoints_file");
        return config["waypoints_file"].get<std::string>();
    }
    if (config.contains("data_directory")) {
        if (!config["data_directory"].is_string() || config["data_directory"].get<std::string>().empty())
            throw std::invalid_argument("data_directory");
        return std::filesystem::path(config["data_directory"].get<std::string>()) / "waypoints.json";
    }
#if !defined(__ANDROID__)
    if (const char* dir = std::getenv("XDG_DATA_HOME"); dir && *dir)
        return std::filesystem::path(dir) / "minecraft-companion/waypoints.json";
    if (const char* dir = std::getenv("HOME"); dir && *dir)
        return std::filesystem::path(dir) / ".local/share/minecraft-companion/waypoints.json";
#endif
    return {};
}
} // namespace

Navigation Navigate(double x, double y, double z, double heading,
                    double target_x, double target_y, double target_z) {
    Navigation out;
    const double dx = target_x - x, dz = target_z - z;
    out.distance = std::hypot(dx, dz);
    out.bearing = out.distance == 0 ? Normalize(heading) :
        Normalize(std::atan2(dx, -dz) * 180.0 / std::numbers::pi);
    out.relative = Normalize(out.bearing - heading);
    out.vertical = static_cast<std::int64_t>(target_y - y);
    out.arrived = out.distance <= 2.0;
    return out;
}

mc_assets::Image ArrowImage() {
    mc_assets::Image image;
    image.width = image.height = 64;
    image.rgba.resize(64 * 64 * 4);
    // Original geometric arrow; no game asset is required for navigation.
    for (int py = 0; py < 64; ++py) {
        for (int px = 0; px < 64; ++px) {
            const bool head = py >= 5 && py <= 32 && std::abs(px - 32) <= (py - 5);
            const bool shaft = py >= 28 && py <= 57 && px >= 25 && px <= 39;
            if (!head && !shaft) continue;
            const auto at = (py * 64 + px) * 4;
            image.rgba[at] = 121; image.rgba[at + 1] = 200;
            image.rgba[at + 2] = 110; image.rgba[at + 3] = 255;
        }
    }
    return image;
}

Waypoints::Waypoints(const char* config_text) {
    Json config = Json::object();
    if (config_text && *config_text) {
        config = Json::parse(config_text, nullptr, false);
        if (!config.is_object()) config = Json::object();
    }
    try { file = StorageFile(config); }
    catch (...) { storage_status = "Session only: invalid storage configuration"; return; }
    if (!file.empty() && !file.is_absolute()) {
        file.clear();
        storage_status = "Session only: storage path must be absolute";
    } else {
        storage_status = file.empty() ? "Session only: configure data_directory" : "Saved locally";
    }
    Load();
}

void Waypoints::Load() {
    if (file.empty()) return;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(file, error);
    if (status.type() == std::filesystem::file_type::not_found) return;
    try {
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::file_size(file) > MaxFileBytes)
            throw std::runtime_error("size");
        std::ifstream input(file);
        if (!input) throw std::runtime_error("read");
        Json data = Json::parse(input);
        if (data.at("version") != 1 || !data.at("worlds").is_array() ||
            data["worlds"].size() > MaxGroups) throw std::runtime_error("schema");
        std::map<Scope, Group> loaded;
        std::vector<std::uint64_t> ids;
        std::uint64_t next = 1;
        std::size_t trail_count = 0;
        for (const auto& world : data["worlds"]) {
            Scope scope{world.at("key").get<std::string>(), BoundedInteger(world.at("dimension"), 0, 2)};
            if (scope.first.empty() || scope.first.size() > 128 || scope.second < 0 ||
                scope.second > 2 || loaded.contains(scope)) throw std::runtime_error("scope");
            Group group;
            group.selected = world.contains("selected") ? BoundedInteger(world["selected"], 0, 1000000000) : 0;
            const auto& points = world.at("points");
            if (!points.is_array() || points.size() > MaxPoints) throw std::runtime_error("points");
            for (const auto& raw : points) {
                Point point{static_cast<std::uint64_t>(BoundedInteger(raw.at("id"), 1, 1000000000)), raw.at("name").get<std::string>(),
                            BoundedInteger(raw.at("x"), -MaxCoordinate, MaxCoordinate),
                            BoundedInteger(raw.at("y"), -MaxCoordinate, MaxCoordinate),
                            BoundedInteger(raw.at("z"), -MaxCoordinate, MaxCoordinate)};
                if (!point.id || point.id >= std::numeric_limits<std::uint64_t>::max() - 1 ||
                    point.name.empty() || point.name.size() > 64 ||
                    point.name.find_first_of("\r\n\t") != std::string::npos ||
                    !Coordinate(point.x) || !Coordinate(point.y) || !Coordinate(point.z) ||
                    std::find(ids.begin(), ids.end(), point.id) != ids.end())
                    throw std::runtime_error("point");
                ids.push_back(point.id);
                next = std::max(next, point.id + 1);
                group.points.push_back(std::move(point));
            }
            if (world.contains("death")) {
                const auto& raw = world["death"];
                Point point{static_cast<std::uint64_t>(BoundedInteger(raw.at("id"), 1, 1000000000)), "Death",
                    BoundedInteger(raw.at("x"), -MaxCoordinate, MaxCoordinate),
                    BoundedInteger(raw.at("y"), -MaxCoordinate, MaxCoordinate),
                    BoundedInteger(raw.at("z"), -MaxCoordinate, MaxCoordinate)};
                if (std::find(ids.begin(), ids.end(), point.id) != ids.end()) throw std::runtime_error("death id");
                ids.push_back(point.id);
                next = std::max(next, point.id + 1);
                group.death = point;
                const auto& trail = raw.at("trail");
                if (!trail.is_array() || trail.size() > MaxTrail) throw std::runtime_error("death trail");
                for (const auto& step : trail) {
                    if (!step.at("start").is_boolean()) throw std::runtime_error("trail start");
                    group.death_trail.push_back({BoundedInteger(step.at("x"), -MaxCoordinate, MaxCoordinate),
                        BoundedInteger(step.at("z"), -MaxCoordinate, MaxCoordinate), step["start"].get<bool>()});
                }
                trail_count += group.death_trail.size();
                if (trail_count > MaxSavedTrail) throw std::runtime_error("trail limit");
            }
            if (std::none_of(group.points.begin(), group.points.end(), [&](const Point& p) {
                    return p.id == group.selected;
                }) && (!group.death || group.death->id != group.selected)) group.selected = 0;
            loaded.emplace(std::move(scope), std::move(group));
        }
        if (data.contains("death_trail_enabled")) {
            if (!data["death_trail_enabled"].is_boolean()) throw std::runtime_error("trail setting");
            show_death_trail = data["death_trail_enabled"].get<bool>();
        }
        groups = std::move(loaded);
        next_id = next;
    } catch (...) {
        persistence_blocked = true;
        storage_status = "Session only: saved file unreadable (kept intact)";
    }
}

void Waypoints::Save() {
    if (file.empty() || persistence_blocked) return;
    static std::atomic<std::uint64_t> serial{};
    const auto temporary = std::filesystem::path(file.string() + ".tmp." +
        std::to_string(::getpid()) + "." + std::to_string(++serial));
    bool own_temporary = false;
    int descriptor = -1;
    try {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(file, error);
        if (status.type() != std::filesystem::file_type::not_found &&
            (error || !std::filesystem::is_regular_file(status))) throw std::runtime_error("target");
        Json data{{"version", 1}, {"worlds", Json::array()}, {"death_trail_enabled", show_death_trail}};
        for (const auto& [scope, group] : groups) {
            Json world{{"key", scope.first}, {"dimension", scope.second},
                       {"selected", group.selected}, {"points", Json::array()}};
            for (const auto& point : group.points)
                world["points"].push_back({{"id", point.id}, {"name", point.name},
                    {"x", point.x}, {"y", point.y}, {"z", point.z}});
            if (group.death) {
                const auto& point = *group.death;
                world["death"] = {{"id", point.id}, {"x", point.x}, {"y", point.y}, {"z", point.z}, {"trail", Json::array()}};
                for (const auto& step : group.death_trail)
                    world["death"]["trail"].push_back({{"x", step.x}, {"z", step.z}, {"start", step.start}});
            }
            data["worlds"].push_back(std::move(world));
        }
        std::filesystem::create_directories(file.parent_path());
        descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (descriptor < 0) throw std::runtime_error("open");
        own_temporary = true;
        const std::string bytes = data.dump(2) + '\n';
        if (bytes.size() > MaxFileBytes) throw std::runtime_error("file limit");
        std::size_t written{};
        while (written < bytes.size()) {
            const auto count = ::write(descriptor, bytes.data() + written, bytes.size() - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("write");
            written += static_cast<std::size_t>(count);
        }
        if (::fsync(descriptor) != 0) throw std::runtime_error("flush");
        const int closed = ::close(descriptor);
        descriptor = -1;
        if (closed != 0) throw std::runtime_error("close");
        std::filesystem::rename(temporary, file);
        storage_status = "Saved locally";
    } catch (...) {
        if (descriptor >= 0) ::close(descriptor);
        std::error_code error;
        if (own_temporary) std::filesystem::remove(temporary, error);
        storage_status = "Save failed: changes remain in this session";
    }
}

void Waypoints::Sample(const EdenDsmodHostApi& host) {
    const auto integer = [&](const char* key) { return host.get_i64 ? host.get_i64(host.userdata, key, 0) : 0; };
    const char* key = host.get_text ? host.get_text(host.userdata, "world.key") : nullptr;
    Scope scope{key ? key : "", integer("world.dimension")};
    ready = integer("world.ready") && integer("map.ready") && !scope.first.empty() &&
            scope.first.size() <= 128 && scope.second >= 0 && scope.second <= 2;
    x = integer("map.x"); y = integer("map.y"); z = integer("map.z");
    heading = host.get_f64 ? host.get_f64(host.userdata, "map.heading", 0) : 0;
    map_px = host.get_f64 ? host.get_f64(host.userdata, "map.px", 0) : 0;
    map_pz = host.get_f64 ? host.get_f64(host.userdata, "map.pz", 0) : 0;
    map_view = static_cast<double>(integer("map.view"));
    ready = ready && Coordinate(x) && Coordinate(y) && Coordinate(z) && std::isfinite(heading);
    if (current != scope) {
        action_status.clear();
        alive = false;
        live_trail.clear();
        trail_start = true;
        current = std::move(scope);
    }
    TrackDeath(integer("player.health_ok") != 0, integer("player.health"));
    Publish(host);
}

void Waypoints::TrackDeath(bool health_ok, std::int64_t health) {
    // Require consecutive trustworthy observations in a confirmed scope. Loading a world,
    // an unreadable HUD or starting on the death screen never counts as a death.
    if (!ready || !health_ok || health < 0) {
        alive = false;
        trail_start = true;
        if (!ready) live_trail.clear();
        return;
    }
    if (health > 0) {
        last_alive = {0, "Death", x, y, z};
        const double distance = live_trail.empty() ? 0 : std::hypot(
            static_cast<double>(x - live_trail.back().x), static_cast<double>(z - live_trail.back().z));
        if (live_trail.empty() || trail_start || distance >= 2) {
            live_trail.push_back({x, z, trail_start || distance > 64});
            if (live_trail.size() > MaxTrail) {
                live_trail.erase(live_trail.begin());
                live_trail.front().start = true;
            }
        }
        alive = true;
        trail_start = false;
        return;
    }
    if (!alive) return;
    alive = false;
    trail_start = true;
    if (!groups.contains(current) && groups.size() >= MaxGroups) {
        action_status = "Death marker not saved: world limit reached";
        live_trail.clear();
        return;
    }
    auto& group = groups[current];
    // Keep one latest death, outside the manual waypoint limit. Never evict a user's place.
    last_alive.id = next_id++;
    group.death = last_alive;
    if (live_trail.empty() || live_trail.back().x != last_alive.x || live_trail.back().z != last_alive.z)
        live_trail.push_back({last_alive.x, last_alive.z, false});
    if (live_trail.size() > MaxTrail) live_trail.erase(live_trail.begin());
    if (!live_trail.empty()) live_trail.front().start = true;
    group.death_trail = std::move(live_trail);
    std::size_t total = 0;
    for (const auto& [scope, saved] : groups) total += saved.death_trail.size();
    for (auto& [scope, saved] : groups) {
        if (total <= MaxSavedTrail) break;
        if (scope == current) continue;
        total -= saved.death_trail.size();
        saved.death_trail.clear();
    }
    live_trail.clear();
    group.selected = group.death->id;
    action_status = "Death marked. Use Death to find your way back.";
    Save();
}

bool Waypoints::OnAction(const char* action, std::int64_t argument) {
    if (!action) return false;
    const std::string_view name{action};
    if (name == "death_trail") {
        show_death_trail = !show_death_trail;
        Save();
        return true;
    }
    if (name == "death_select") {
        if (ready) {
            const auto found = groups.find(current);
            if (found != groups.end() && found->second.death) {
                found->second.selected = found->second.death->id;
                action_status.clear();
                Save();
            }
        }
        return true;
    }
    if (!name.starts_with("waypoint_")) return false;
    if (name != "waypoint_add" && name != "waypoint_select" && name != "waypoint_delete" &&
        name != "waypoint_stop" && name != "waypoint_prev" && name != "waypoint_next") return false;
    if (!ready) { action_status = "Confirm the world on Map before marking"; return true; }
    if (!groups.contains(current) && groups.size() >= MaxGroups) {
        action_status = "World limit reached"; return true;
    }
    auto& group = groups[current];
    bool changed = false;
    if (name == "waypoint_add") {
        if (argument < 0 || argument > 3) return true;
        if (group.points.size() >= MaxPoints) {
            action_status = "64 waypoints maximum in this map"; return true;
        }
        static constexpr const char* names[]{"Base", "Village", "Mine", "Place"};
        const std::string prefix = names[argument];
        int number = 1;
        std::string label = prefix;
        while (std::any_of(group.points.begin(), group.points.end(), [&](const Point& p) { return p.name == label; }))
            label = prefix + " " + std::to_string(++number);
        group.points.push_back({next_id++, label, x, y, z});
        group.selected = group.points.back().id;
        group.page = static_cast<int>((group.points.size() - 1) / PageSize);
        action_status = "Marked " + label;
        changed = true;
    } else if (name == "waypoint_select") {
        if (argument < 0 || argument >= PageSize) return true;
        const auto index = static_cast<std::size_t>(group.page * PageSize + argument);
        if (index < group.points.size()) { group.selected = group.points[index].id; changed = true; }
        action_status.clear();
    } else if (name == "waypoint_delete") {
        const auto before = group.points.size();
        std::erase_if(group.points, [&](const Point& p) { return p.id == group.selected; });
        changed = before != group.points.size();
        if (group.death && group.death->id == group.selected) {
            group.death.reset();
            group.death_trail.clear();
            changed = true;
        }
        group.selected = 0;
        group.page = std::min(group.page, static_cast<int>(group.points.empty() ? 0 : (group.points.size() - 1) / PageSize));
        if (changed) action_status = "Waypoint deleted";
    } else if (name == "waypoint_stop") {
        changed = group.selected != 0;
        group.selected = 0;
        action_status.clear();
    } else if (name == "waypoint_prev") {
        group.page = std::max(0, group.page - 1);
    } else if (name == "waypoint_next") {
        group.page = std::min(group.page + 1, static_cast<int>(group.points.empty() ? 0 : (group.points.size() - 1) / PageSize));
    }
    if (changed) Save();
    return true;
}

void Waypoints::Publish(const EdenDsmodHostApi& host) {
    const auto integer = [&](const std::string& key, std::int64_t value) {
        if (host.publish_i64) host.publish_i64(host.userdata, key.c_str(), value);
    };
    const auto number = [&](const char* key, double value) {
        if (host.publish_f64) host.publish_f64(host.userdata, key, value);
    };
    const auto text = [&](const std::string& key, const std::string& value) {
        if (host.publish_text) host.publish_text(host.userdata, key.c_str(), value.c_str());
    };
    const auto found = groups.find(current);
    const Group* group = ready && found != groups.end() ? &found->second : nullptr;
    PublishDeath(host, group);
    integer("waypoint.ready", ready); integer("waypoint.count", group ? group->points.size() : 0);
    text("waypoint.storage", storage_status);
    text("waypoint.status", !ready ? "Choose and confirm your world on Map" : action_status);
    text("waypoint.scope", current.first.empty() ? "No world selected" :
        current.first + " / " + (current.second == 1 ? "Nether" : current.second == 2 ? "End" : "Overworld"));
    text("waypoint.page", group && !group->points.empty() ?
        std::to_string(group->page + 1) + " / " + std::to_string((group->points.size() + PageSize - 1) / PageSize) : "0 / 0");
    integer("waypoint.empty", ready && (!group || group->points.empty()));
    const Point* selected = nullptr;
    if (group) for (const auto& point : group->points) if (point.id == group->selected) selected = &point;
    if (group && group->death && group->death->id == group->selected) selected = &*group->death;
    for (int row = 0; row < PageSize; ++row) {
        const auto index = static_cast<std::size_t>((group ? group->page : 0) * PageSize + row);
        const Point* point = group && index < group->points.size() ? &group->points[index] : nullptr;
        const std::string prefix = "waypoint.row" + std::to_string(row);
        integer(prefix + ".present", point != nullptr);
        integer(prefix + ".selected", point && point == selected);
        text(prefix + ".name", point ? point->name : "");
        text(prefix + ".detail", point ? std::to_string(point->x) + ", " + std::to_string(point->y) + ", " +
            std::to_string(point->z) + "  |  " + std::to_string(static_cast<long long>(std::llround(std::hypot(
                static_cast<double>(point->x - x), static_cast<double>(point->z - z))))) + " blocks" : "");
    }
    integer("waypoint.active", selected != nullptr);
    integer("waypoint.arrow", 0); integer("waypoint.marker", 0);
    integer("waypoint.distance", 0); integer("waypoint.dy", 0);
    number("waypoint.relative", 0); number("waypoint.bearing", 0);
    number("waypoint.map_x", 0); number("waypoint.map_z", 0);
    text("waypoint.target", selected ? selected->name : "Select a destination");
    text("waypoint.navigation", ""); text("waypoint.coords", "");
    if (!selected) return;
    const auto nav = Navigate(x, y, z, heading, selected->x, selected->y, selected->z);
    integer("waypoint.arrow", !nav.arrived);
    integer("waypoint.distance", std::llround(nav.distance)); integer("waypoint.dy", nav.vertical);
    number("waypoint.relative", nav.relative); number("waypoint.bearing", nav.bearing);
    text("waypoint.navigation", nav.arrived ? "At destination (horizontal)" :
        std::to_string(static_cast<long long>(std::llround(nav.distance))) + " blocks away");
    text("waypoint.coords", "X " + std::to_string(selected->x) + "  Y " + std::to_string(selected->y) + "  Z " + std::to_string(selected->z));
    const double px = map_px + selected->x - x, pz = map_pz + selected->z - z;
    const bool marker = map_view > 0 && std::isfinite(px) && std::isfinite(pz) &&
                        px >= 0 && pz >= 0 && px < map_view && pz < map_view;
    integer("waypoint.marker", marker && !(group->death && selected == &*group->death));
    if (marker) {
        number("waypoint.map_x", px * 768 / map_view);
        number("waypoint.map_z", pz * 768 / map_view);
    }
}

std::optional<mc_assets::Image> Waypoints::LoadTrail(std::string_view key) {
    std::lock_guard lock{trail_mutex};
    for (const auto& picture : trail_images)
        if (!picture.key.empty() && picture.key == key) return picture.image;
    return std::nullopt;
}

void Waypoints::PublishDeath(const EdenDsmodHostApi& host, const Group* group) {
    const auto integer = [&](const char* key, std::int64_t value) {
        if (host.publish_i64) host.publish_i64(host.userdata, key, value);
    };
    const auto number = [&](const char* key, double value) {
        if (host.publish_f64) host.publish_f64(host.userdata, key, value);
    };
    const auto text = [&](const char* key, const std::string& value) {
        if (host.publish_text) host.publish_text(host.userdata, key, value.c_str());
    };
    const Point* death = group && group->death ? &*group->death : nullptr;
    integer("death.available", death != nullptr);
    integer("death.marker", 0);
    integer("death.trail_visible", 0);
    integer("death.trail_enabled", show_death_trail);
    integer("death.trail_points", death ? group->death_trail.size() : 0);
    number("death.map_x", 0); number("death.map_z", 0);
    text("death.trail_label", show_death_trail ? "Death trail: On" : "Death trail: Off");
    text("death.trail_image", "");
    text("death.coords", death ? "X " + std::to_string(death->x) + "  Y " +
        std::to_string(death->y) + "  Z " + std::to_string(death->z) : "");
    if (!death || !std::isfinite(map_px) || !std::isfinite(map_pz) ||
        (map_view != 64 && map_view != 128 && map_view != 256)) return;
    // map.x/z are floored block positions; map.px/pz retain the sub-block fraction.
    const double origin_x = x - std::floor(map_px), origin_z = z - std::floor(map_pz);
    const double px = death->x - origin_x, pz = death->z - origin_z;
    const bool marker = px >= 0 && pz >= 0 && px < map_view && pz < map_view;
    integer("death.marker", marker);
    if (marker) {
        number("death.map_x", px * 768 / map_view);
        number("death.map_z", pz * 768 / map_view);
    }
    if (!show_death_trail || group->death_trail.empty()) return;
    if (drawn_death != death->id || drawn_x != origin_x || drawn_z != origin_z || drawn_view != map_view) {
        const int size = static_cast<int>(map_view);
        mc_assets::Image image;
        image.width = image.height = size;
        image.rgba.resize(size * size * 4);
        const auto pixel = [&](int px, int pz) {
            if (px < 0 || pz < 0 || px >= size || pz >= size) return;
            const auto at = (pz * size + px) * 4;
            image.rgba[at] = 255; image.rgba[at + 1] = 116;
            image.rgba[at + 2] = 116; image.rgba[at + 3] = 255;
        };
        for (std::size_t i = 0; i < group->death_trail.size(); ++i) {
            const auto& point = group->death_trail[i];
            const double bx = point.x - origin_x, bz = point.z - origin_z;
            if (i == 0 || point.start) {
                if (bx >= 0 && bz >= 0 && bx < size && bz < size)
                    pixel(static_cast<int>(bx), static_cast<int>(bz));
                continue;
            }
            // Clip before rasterizing: a teleport or distant endpoint cannot cause an
            // unbounded walk or draw outside the map's transparent overlay.
            const auto& previous = group->death_trail[i - 1];
            const double ax = previous.x - origin_x, az = previous.z - origin_z;
            const double dx = bx - ax, dz = bz - az;
            double first = 0, last = 1;
            const auto clip = [&](double p, double q) {
                if (p == 0) return q >= 0;
                const double r = q / p;
                if (p < 0) first = std::max(first, r); else last = std::min(last, r);
                return first <= last;
            };
            if (!clip(-dx, ax) || !clip(dx, size - 1 - ax) ||
                !clip(-dz, az) || !clip(dz, size - 1 - az)) continue;
            const double start_x = ax + first * dx, start_z = az + first * dz;
            const double end_x = ax + last * dx, end_z = az + last * dz;
            const int steps = std::max(1, static_cast<int>(std::ceil(std::max(
                std::abs(end_x - start_x), std::abs(end_z - start_z)))));
            for (int step = 0; step <= steps; ++step)
                pixel(static_cast<int>(std::lround(start_x + (end_x - start_x) * step / steps)),
                      static_cast<int>(std::lround(start_z + (end_z - start_z) * step / steps)));
        }
        {
            std::lock_guard lock{trail_mutex};
            trail_images[1] = std::move(trail_images[0]);
            trail_images[0] = {"death/trail/" + std::to_string(++trail_serial), std::move(image)};
        }
        drawn_death = death->id;
        drawn_x = origin_x; drawn_z = origin_z; drawn_view = map_view;
    }
    std::lock_guard lock{trail_mutex};
    integer("death.trail_visible", 1);
    text("death.trail_image", "module:mc:" + trail_images[0].key);
}
} // namespace mc_waypoints
