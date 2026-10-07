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
constexpr std::size_t MaxPoints = 64, MaxGroups = 128, MaxFileBytes = 2 * 1024 * 1024;
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
            if (std::none_of(group.points.begin(), group.points.end(), [&](const Point& p) {
                    return p.id == group.selected;
                })) group.selected = 0;
            loaded.emplace(std::move(scope), std::move(group));
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
        Json data{{"version", 1}, {"worlds", Json::array()}};
        for (const auto& [scope, group] : groups) {
            Json world{{"key", scope.first}, {"dimension", scope.second},
                       {"selected", group.selected}, {"points", Json::array()}};
            for (const auto& point : group.points)
                world["points"].push_back({{"id", point.id}, {"name", point.name},
                    {"x", point.x}, {"y", point.y}, {"z", point.z}});
            data["worlds"].push_back(std::move(world));
        }
        std::filesystem::create_directories(file.parent_path());
        descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (descriptor < 0) throw std::runtime_error("open");
        own_temporary = true;
        const std::string bytes = data.dump(2) + '\n';
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
        current = std::move(scope);
    }
    Publish(host);
}

bool Waypoints::OnAction(const char* action, std::int64_t argument) {
    if (!action) return false;
    const std::string_view name{action};
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
    integer("waypoint.marker", marker);
    if (marker) {
        number("waypoint.map_x", px * 768 / map_view);
        number("waypoint.map_z", pz * 768 / map_view);
    }
}
} // namespace mc_waypoints
