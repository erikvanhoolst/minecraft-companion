// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_waypoints.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

using mc_waypoints::Waypoints;
using Json = nlohmann::json;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct Fixture {
    EdenDsmodHostApi host{};
    std::map<std::string, std::int64_t> ints{{"world.ready", 1}, {"world.dimension", 0},
        {"map.ready", 1}, {"map.view", 128}, {"map.y", 64}, {"player.health_ok", 1}, {"player.health", 20}};
    std::map<std::string, double> numbers{{"map.px", 64.25}, {"map.pz", 64.5}};
    std::map<std::string, std::string> texts{{"world.key", "Survival"}};
    Fixture() {
        host.userdata = this;
        host.get_i64 = [](void* p, const char* k, std::int64_t fallback) {
            auto& v = static_cast<Fixture*>(p)->ints; return v.contains(k) ? v[k] : fallback;
        };
        host.get_f64 = [](void* p, const char* k, double fallback) {
            auto& v = static_cast<Fixture*>(p)->numbers; return v.contains(k) ? v[k] : fallback;
        };
        host.get_text = [](void* p, const char* k) -> const char* {
            auto& v = static_cast<Fixture*>(p)->texts; return v.contains(k) ? v[k].c_str() : nullptr;
        };
        host.publish_i64 = [](void* p, const char* k, std::int64_t v) { static_cast<Fixture*>(p)->ints[k] = v; };
        host.publish_f64 = [](void* p, const char* k, double v) { static_cast<Fixture*>(p)->numbers[k] = v; };
        host.publish_text = [](void* p, const char* k, const char* v) { static_cast<Fixture*>(p)->texts[k] = v; };
    }
    void Sample(Waypoints& points, int health, int x = 0, int z = 0) {
        ints["player.health"] = health; ints["map.x"] = x; ints["map.z"] = z;
        points.Sample(host);
    }
    void Action(Waypoints& points, const char* action) {
        Check(points.OnAction(action, 0), "action handled"); points.Sample(host);
    }
    Json Read(const std::filesystem::path& path) {
        std::ifstream in(path); return Json::parse(in);
    }
};
int main() {
    const auto directory = std::filesystem::temp_directory_path() / ("mc-death-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = directory / "waypoints.json";
    const auto config = Json{{"data_directory", directory.string()}}.dump();
    Fixture f;
    Waypoints points(config.c_str());
    f.Sample(points, 0);
    Check(!f.ints["death.available"], "starting dead does not fabricate a marker");
    f.Sample(points, 20, -20, -10);
    f.Sample(points, 10, -10, -10);
    f.Sample(points, 1, -5, -10);
    f.Sample(points, 0, 1000, 1000);
    Check(f.ints["death.available"] && f.texts["death.coords"] == "X -5  Y 64  Z -10" &&
        f.texts["waypoint.target"] == "Death", "death uses last live position, never a respawn jump");
    Check(f.ints["death.trail_points"] == 3 && !f.ints["death.trail_visible"], "trail freezes and defaults off");
    const auto saved = f.Read(file);
    for (int i = 0; i < 10; ++i) f.Sample(points, 0, 2000, 2000);
    Check(f.Read(file) == saved, "repeated death screen samples do not replace or rewrite marker");
    f.Sample(points, 20, -20, -10);
    Check(f.ints["death.marker"] && f.ints["waypoint.distance"] == 15 &&
        f.ints["waypoint.dy"] == 0 && !f.ints["waypoint.marker"], "respawn navigation uses the red death marker");
    f.Action(points, "death_trail");
    Check(f.ints["death.trail_visible"] && f.ints["death.trail_enabled"], "trail toggle reveals overlay");
    auto key = f.texts["death.trail_image"].substr(std::string("module:mc:").size());
    const auto image = points.LoadTrail(key);
    Check(image && image->width == 128 && image->height == 128, "overlay loads at map resolution");
    Check(image->rgba[(64 * 128 + 69) * 4 + 3] == 255 && image->rgba[3] == 0,
        "recorded route is colored and unknown pixels transparent");
    const auto old_key = f.texts["death.trail_image"];
    f.numbers["map.px"] = 64.75; points.Sample(f.host);
    Check(f.texts["death.trail_image"] == old_key, "sub-block movement does not regenerate overlay");
    f.ints["map.view"] = 64; f.numbers["map.px"] = 32; f.numbers["map.pz"] = 32; points.Sample(f.host);
    Check(f.texts["death.trail_image"] != old_key && points.LoadTrail(key), "zoom replaces image while previous asset remains loadable");
    f.Action(points, "waypoint_stop");
    Check(!f.ints["waypoint.active"] && f.ints["death.marker"], "stop navigation retains the death marker");
    f.Action(points, "death_select");
    Waypoints restored(config.c_str()); restored.Sample(f.host);
    Check(f.ints["death.available"] && f.ints["death.trail_visible"] && f.texts["waypoint.target"] == "Death",
        "marker, frozen trail, toggle and selection survive restart");
    f.ints["player.health"] = 0;
    f.ints["world.dimension"] = 1; restored.Sample(f.host);
    Check(!f.ints["death.available"] && !f.ints["death.marker"] && !f.ints["death.trail_visible"], "dimension hides death and trail");
    f.Sample(restored, 0);
    Check(!f.ints["death.available"], "scope switch while dead cannot synthesize death");
    f.ints["world.dimension"] = 0; restored.Sample(f.host);
    Check(f.ints["death.available"], "return restores death in original dimension");
    f.texts["world.key"] = "Creative"; restored.Sample(f.host);
    Check(!f.ints["death.available"], "different world has no inherited death");
    f.texts["world.key"] = "Survival"; f.Sample(restored, 20);
    f.ints["player.health_ok"] = 0; f.Sample(restored, 0);
    f.ints["player.health_ok"] = 1; f.Sample(restored, 0);
    Check(f.texts["death.coords"] == "X -5  Y 64  Z -10", "unreadable health never counts as zero");
    f.Sample(restored, 20);
    f.ints["world.ready"] = 0; f.Sample(restored, 0);
    Check(!f.ints["death.available"] && f.texts["death.trail_image"].empty(), "unconfirmed scope clears all bindings");
    f.ints["world.ready"] = 1; f.Sample(restored, 0);
    Check(f.texts["death.coords"] == "X -5  Y 64  Z -10", "reconfirmation while dead does not overwrite");
    f.Sample(restored, 20);
    for (int i = 0; i < 64; ++i) Check(restored.OnAction("waypoint_add", 0), "fill manual waypoints");
    f.Sample(restored, 20, 5);
    f.Sample(restored, 0);
    Check(f.ints["waypoint.count"] == 64 && f.texts["death.coords"] == "X 5  Y 64  Z 0",
        "latest death replaces previous death without evicting a manual waypoint");
    f.Action(restored, "waypoint_delete");
    Check(!f.ints["death.available"] && !f.ints["death.trail_visible"] && f.ints["waypoint.count"] == 64,
        "deleting Death clears its marker and trail, retaining all manual waypoints");
    f.Sample(restored, 20);
    for (int x = 2; x <= 5000; x += 2) f.Sample(restored, 20, x);
    f.Sample(restored, 0);
    Check(f.ints["death.trail_points"] == 2048, "route memory is bounded");
    const auto bounded = f.Read(file);
    Check(bounded["worlds"][0]["death"]["trail"][0]["start"] == true, "trimmed trail begins a new segment");
    // A discontinuous jump is shown as disconnected points, never a fabricated walk.
    f.Sample(restored, 20, 0);
    f.Sample(restored, 20, 1000);
    f.Sample(restored, 0);
    const auto jump = f.Read(file);
    Check(jump["worlds"][0]["death"]["trail"][1]["start"] == true, "teleport breaks trail");
    f.ints["map.view"] = 256; f.Sample(restored, 20, 500);
    key = f.texts["death.trail_image"].substr(std::string("module:mc:").size());
    const auto disconnected = restored.LoadTrail(key);
    Check(disconnected && disconnected->width == 256, "largest zoom overlay loads");
    for (std::size_t i = 3; i < disconnected->rgba.size(); i += 4)
        Check(disconnected->rgba[i] == 0, "teleport creates no line through intermediate viewport");
    Json crossing = jump;
    crossing["worlds"][0]["death"]["trail"] = Json::array({
        {{"x", -30000000}, {"z", 0}, {"start", true}},
        {{"x", 30000000}, {"z", 0}, {"start", false}}});
    { std::ofstream out(file); out << crossing.dump(); }
    Waypoints clipped(config.c_str()); clipped.Sample(f.host);
    key = f.texts["death.trail_image"].substr(std::string("module:mc:").size());
    const auto crossing_image = clipped.LoadTrail(key);
    Check(crossing_image && crossing_image->rgba[(32 * 256 + 128) * 4 + 3] == 255,
        "distant valid trail endpoints clip to the visible viewport");
    f.ints["map.ready"] = 0; restored.Sample(f.host);
    Check(!f.ints["death.marker"] && !f.ints["death.trail_visible"], "map loss clears overlays");
    Json corrupt = jump;
    corrupt["worlds"][0]["death"]["trail"][0]["x"] = 30000001;
    { std::ofstream out(file); out << corrupt.dump(); }
    Waypoints invalid(config.c_str()); f.ints["map.ready"] = 1; invalid.Sample(f.host);
    Check(!f.ints["death.available"] && f.texts["waypoint.storage"].find("kept intact") != std::string::npos,
        "invalid death trail preserves file and disables persistence");
    const auto cap_config = Json{{"data_directory", (directory / "cap").string()}}.dump();
    Waypoints capped(cap_config.c_str());
    for (int scope = 0; scope < 5; ++scope) {
        f.texts["world.key"] = "Scope " + std::to_string(scope);
        for (int x = 0; x < 4096; x += 2) f.Sample(capped, 20, x);
        f.Sample(capped, 0);
    }
    const auto cap_data = f.Read(directory / "cap/waypoints.json");
    std::size_t total = 0;
    for (const auto& world : cap_data["worlds"]) {
        Check(world.contains("death"), "global trail cap preserves every death marker");
        total += world["death"]["trail"].size();
    }
    Check(total == 8192 && cap_data["worlds"][0]["death"]["trail"].empty(),
        "saved trails obey their global cap while retaining newest trail");
    Waypoints cap_restored(cap_config.c_str()); cap_restored.Sample(f.host);
    Check(f.ints["death.available"] && f.ints["death.trail_points"] == 2048,
        "globally capped store remains readable after restart");
    std::filesystem::remove_all(directory);
    std::puts("death and return trail fixtures passed");
}
