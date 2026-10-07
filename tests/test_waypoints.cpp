// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_waypoints.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <chrono>

using namespace mc_waypoints;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
bool Near(double a, double b) { return std::abs(a - b) < 0.00001; }
struct Fixture {
    EdenDsmodHostApi host{};
    std::map<std::string, std::int64_t> ints;
    std::map<std::string, double> numbers;
    std::map<std::string, std::string> texts;
    Fixture() {
        host.userdata = this;
        host.get_i64 = [](void* p, const char* key, std::int64_t fallback) {
            auto& f = *static_cast<Fixture*>(p);
            const auto found = f.ints.find(key);
            return found == f.ints.end() ? fallback : found->second;
        };
        host.get_f64 = [](void* p, const char* key, double fallback) {
            auto& f = *static_cast<Fixture*>(p);
            const auto found = f.numbers.find(key);
            return found == f.numbers.end() ? fallback : found->second;
        };
        host.get_text = [](void* p, const char* key) -> const char* {
            auto& f = *static_cast<Fixture*>(p);
            const auto found = f.texts.find(key);
            return found == f.texts.end() ? nullptr : found->second.c_str();
        };
        host.publish_i64 = [](void* p, const char* key, std::int64_t value) { static_cast<Fixture*>(p)->ints[key] = value; };
        host.publish_f64 = [](void* p, const char* key, double value) { static_cast<Fixture*>(p)->numbers[key] = value; };
        host.publish_text = [](void* p, const char* key, const char* value) { static_cast<Fixture*>(p)->texts[key] = value; };
        ints = {{"world.ready", 1}, {"world.dimension", 0}, {"map.ready", 1},
                {"map.x", -10}, {"map.y", 64}, {"map.z", -20}, {"map.view", 128}};
        numbers = {{"map.heading", 0}, {"map.px", 64.5}, {"map.pz", 64.25}};
        texts["world.key"] = "fixture-world";
    }
    void Action(Waypoints& points, const char* action, std::int64_t argument = 0) {
        Check(points.OnAction(action, argument), "action handled");
        points.Sample(host);
    }
};

int main() {
    auto nav = Navigate(0, 0, 0, 90, 0, 5, -10);
    Check(Near(nav.distance, 10) && Near(nav.bearing, 0) && Near(nav.relative, 270) && nav.vertical == 5,
          "north target turns left while facing east");
    Check(Near(Navigate(0, 0, 0, 0, 10, 0, 0).bearing, 90), "east bearing");
    Check(Near(Navigate(0, 0, 0, 0, 0, 0, 10).bearing, 180), "south bearing");
    Check(Near(Navigate(0, 0, 0, 0, -10, 0, 0).bearing, 270), "west bearing");
    Check(Near(Navigate(-8, 64, -9, 450, -5, 60, -5).distance, 5), "negative diagonal distance");
    Check(Navigate(1, 0, 2, -90, 1, 100, 2).arrived, "horizontal arrival ignores height, shown separately");
    Check(Navigate(0, 0, 0, 0, 2, 0, 0).arrived && !Navigate(0, 0, 0, 0, 3, 0, 0).arrived,
          "arrival threshold is two blocks");
    const auto arrow = ArrowImage();
    Check(arrow.width == 64 && arrow.rgba[3] == 0 && arrow.rgba[(57 * 64 + 32) * 4 + 3] == 255,
          "navigation arrow transparent background and filled shaft");
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("mc-waypoint-test-" + std::to_string(stamp));
    const auto file = directory / "waypoints.json";
    const std::string config = nlohmann::json{{"data_directory", directory.string()}}.dump();
    Fixture f;
    Waypoints points(config.c_str());
    points.Sample(f.host);
    f.Action(points, "waypoint_add", 0);
    Check(f.texts["waypoint.target"] == "Base" && f.ints["waypoint.active"] == 1 && f.ints["waypoint.arrow"] == 0,
          "mark selects new base and shows arrival");
    Check(std::filesystem::exists(file), "mark persists locally");
    f.ints["map.x"] = -7; f.ints["map.z"] = -16; f.ints["map.y"] = 70;
    points.Sample(f.host);
    Check(f.ints["waypoint.distance"] == 5 && f.ints["waypoint.dy"] == -6 && f.ints["waypoint.arrow"] == 1,
          "distance, direction and height update while walking");
    Check(f.ints["waypoint.marker"] == 1 && Near(f.numbers["waypoint.map_x"], 61.5 * 6),
          "selected waypoint map marker follows viewport");
    f.ints["map.x"] = 1000;
    points.Sample(f.host);
    Check(f.ints["waypoint.marker"] == 0, "off-screen waypoint does not draw outside map");
    f.ints["world.dimension"] = 1; points.Sample(f.host);
    Check(f.ints["waypoint.count"] == 0 && f.ints["waypoint.active"] == 0, "dimension change isolates waypoints");
    f.Action(points, "waypoint_add", 2);
    Check(f.texts["waypoint.target"] == "Mine", "nether stores its own destination");
    f.texts["world.key"] = "other-world"; points.Sample(f.host);
    Check(f.ints["waypoint.count"] == 0, "different world never reuses dimension waypoints");
    f.texts["world.key"] = "fixture-world"; f.ints["world.dimension"] = 0; points.Sample(f.host);
    Check(f.texts["waypoint.target"] == "Base", "return restores this world's selected destination");
    f.ints["world.ready"] = 0; points.Sample(f.host);
    f.Action(points, "waypoint_add", 1);
    Check(f.ints["waypoint.active"] == 0 && f.texts["waypoint.row0.name"].empty(), "unconfirmed world clears navigation and rows");
    f.ints["world.ready"] = 1; points.Sample(f.host);
    Check(f.ints["waypoint.count"] == 1, "unconfirmed world cannot save stale position");
    for (int i = 0; i < 7; ++i) f.Action(points, "waypoint_add", 0);
    Check(f.texts["waypoint.page"] == "2 / 2" && f.texts["waypoint.row1.name"] == "Base 8",
          "pagination and duplicate preset naming");
    f.Action(points, "waypoint_select", 0);
    Check(f.texts["waypoint.target"] == "Base 7", "select uses the visible page row");
    f.Action(points, "waypoint_delete");
    Check(f.ints["waypoint.count"] == 7 && f.ints["waypoint.active"] == 0 && f.ints["waypoint.row1.present"] == 0,
          "delete clears selected navigation and obsolete rows");
    f.Action(points, "waypoint_prev"); f.Action(points, "waypoint_select", 0);
    Waypoints restored(config.c_str()); restored.Sample(f.host);
    Check(f.texts["waypoint.target"] == "Base" && f.ints["waypoint.count"] == 7,
          "restart restores points and selection from disk");
    f.Action(restored, "waypoint_stop");
    Check(f.ints["waypoint.count"] == 7 && f.ints["waypoint.active"] == 0, "stop keeps saved points");
    for (int i = 7; i < 64; ++i) f.Action(restored, "waypoint_add", 3);
    f.Action(restored, "waypoint_add", 3);
    Check(f.ints["waypoint.count"] == 64 && f.texts["waypoint.status"].find("maximum") != std::string::npos,
          "waypoint count capped per world and dimension");
    const auto corrupt_file = directory / "bad.json";
    { std::ofstream out(corrupt_file); out << "{broken saved data"; }
    const auto bad_config = nlohmann::json{{"waypoints_file", corrupt_file.string()}}.dump();
    Waypoints corrupt(bad_config.c_str()); corrupt.Sample(f.host); f.Action(corrupt, "waypoint_add", 0);
    std::ifstream corrupt_input(corrupt_file); std::string kept; std::getline(corrupt_input, kept);
    Check(kept == "{broken saved data" && f.texts["waypoint.storage"].find("kept intact") != std::string::npos,
          "malformed saved data remains untouched while session continues");
    Waypoints relative("{\"data_directory\":\"relative-dir\"}"); relative.Sample(f.host);
    Check(f.texts["waypoint.storage"].find("absolute") != std::string::npos,
          "relative storage paths rejected without changing working directory");
    Waypoints invalid_config("{\"data_directory\":42}"); invalid_config.Sample(f.host);
    Check(f.texts["waypoint.storage"].find("invalid storage") != std::string::npos,
          "explicit invalid storage type does not silently fall back");
    const auto linked_file = directory / "linked.json";
    std::filesystem::create_symlink(file, linked_file);
    const auto linked_config = nlohmann::json{{"waypoints_file", linked_file.string()}}.dump();
    const auto original_size = std::filesystem::file_size(file);
    Waypoints linked(linked_config.c_str()); linked.Sample(f.host); f.Action(linked, "waypoint_add", 0);
    Check(std::filesystem::is_symlink(linked_file) && std::filesystem::file_size(file) == original_size &&
              f.texts["waypoint.storage"].find("kept intact") != std::string::npos,
          "symlink saved target is never loaded or overwritten");
    const auto fixed_temp = directory / "waypoints.json.tmp";
    std::filesystem::create_symlink(corrupt_file, fixed_temp);
    f.Action(restored, "waypoint_stop");
    Check(std::filesystem::is_symlink(fixed_temp) && std::filesystem::file_size(corrupt_file) == 18,
          "unique exclusive temporary file never follows fixed-name symlink");
    f.ints["world.ready"] = 1;
    f.ints["map.ready"] = 0;
    restored.Sample(f.host);
    Check(f.ints["waypoint.active"] == 0 && f.ints["waypoint.arrow"] == 0 &&
              f.ints["waypoint.marker"] == 0 && f.texts["waypoint.coords"].empty(),
          "map loss clears every navigation binding");
    Check(!relative.OnAction("unrelated", 0) && !relative.OnAction(nullptr, 0), "unknown actions ignored");
    std::filesystem::remove_all(directory);
    std::puts("waypoint fixtures passed");
}
