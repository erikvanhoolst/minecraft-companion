// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_projects.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <limits>
using namespace mc_projects;
using namespace mc_reader;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct Publisher {
    std::map<std::string, std::string> text;
    std::map<std::string, std::int64_t> ints;
    EdenDsmodHostApi host{};
    Publisher() {
        host.userdata = this;
        host.publish_text = [](void* p, const char* key, const char* value) { static_cast<Publisher*>(p)->text[key] = value; };
        host.publish_i64 = [](void* p, const char* key, std::int64_t value) { static_cast<Publisher*>(p)->ints[key] = value; };
    }
};
int main() {
    InventorySnapshot inv;
    Material stone{"cobblestone", "Cobblestone", 0, 128};
    Check(!CountMaterial(stone, inv).known, "unavailable inventory is unknown, not empty");
    inv.ready = true;
    auto count = CountMaterial(stone, inv);
    Check(count.known && count.owned == 0 && count.missing == 128, "readable empty inventory needs everything");
    inv.selected = 0;
    inv.slots[0] = {64, 0, "tile.cobblestone", "Cobblestone", ""};
    inv.slots[9] = {32, 0, "minecraft:cobblestone", "Cobblestone", ""};
    inv.slots[35] = {16, 0, "item.cobblestone", "Cobblestone", ""};
    inv.slots[34] = {64, 1, "cobblestone", "Other variant", ""};
    count = CountMaterial(stone, inv);
    Check(count.owned == 112 && count.missing == 16, "aggregates hotbar, main slots and slot 35 but exact aux only");
    stone.required = 64;
    Check(CountMaterial(stone, inv).missing == 0, "surplus has zero remaining");
    inv.slots[20] = {1, 0, "", "", ""};
    Check(!CountMaterial(stone, inv).known, "unreadable occupied identity makes aggregate unknown");
    inv.slots[20] = {};
    Check(CanonicalId("mod:cobblestone") == "mod:cobblestone", "custom namespaces remain distinct");
    const auto dir = std::filesystem::temp_directory_path() /
        ("mc-projects-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = dir / "nested/projects.json";
    const auto config = nlohmann::json{{"projects_file", file.string()}}.dump();
    Publisher p;
    Projects projects(config.c_str());
    projects.Sample(p.host, inv);
    Check(projects.OnAction("projects_add_choice"), "preset add action handled");
    projects.OnAction("projects_plus64");
    projects.Sample(p.host, inv);
    Check(projects.All()[0].materials[0].required == 128 && p.ints["projects.row0.missing"] == 16,
          "quantity edit feeds live counter");
    projects.OnAction("projects_add_selected");
    Check(projects.All()[0].materials.size() == 1, "adding held alias selects same material without duplicate");
    for (int i = 0; i < 3; ++i) projects.OnAction("projects_minus64");
    Check(projects.All()[0].materials[0].required == 1, "quantity lower bound enforced");
    projects.OnAction("projects_plus16");
    projects.OnAction("projects_name");
    projects.OnAction("projects_new");
    inv.slots[0] = {5, 2, "planks", "Birch planks", ""};
    projects.Sample(p.host, inv);
    projects.OnAction("projects_add_selected");
    projects.Sample(p.host, inv);
    Check(projects.All()[1].materials[0].aux == 2 && p.ints["projects.row0.owned"] == 5,
          "held item preserves variant and aggregates separately");
    inv.ready = false;
    projects.Sample(p.host, inv);
    Check(p.ints["projects.row0.known"] == 0 && p.ints["projects.row0.missing"] == -1 &&
          p.text["projects.row0.counts"].find("? left") != std::string::npos,
          "failed sample clears previous counts visibly");
    projects.OnAction("projects_add_selected");
    Check(projects.All()[1].materials.size() == 1, "cannot add stale selected item");
    Projects loaded(config.c_str());
    Check(loaded.All().size() == 2 && loaded.All()[0].name == "House 1" &&
          loaded.All()[0].materials[0].required == 17 && loaded.All()[1].materials[0].aux == 2,
          "atomic persistence round trip retains multiple projects, names, quantities and variants");
    Check(projects.StorageStatus() == "Saved on device", "save status truthful");
    loaded.OnAction("projects_delete_confirm");
    Check(loaded.All().size() == 2, "project deletion requires explicit prior request");
    loaded.OnAction("projects_delete");
    loaded.OnAction("projects_next");
    loaded.OnAction("projects_delete_confirm");
    Check(loaded.All().size() == 2, "project selection cancels pending deletion");
    loaded.OnAction("projects_delete");
    loaded.OnAction("projects_delete_confirm");
    Check(loaded.All().size() == 1, "confirmed deletion removes only active project");
    for (int i = 0; i < 12; ++i) { loaded.OnAction("projects_add_choice"); loaded.OnAction("projects_choice_next"); }
    loaded.Sample(p.host, inv);
    Check(loaded.All()[0].materials.size() == 12, "curated variants remain separate materials");
    loaded.OnAction("projects_page_previous");
    loaded.OnAction("projects_row_0");
    loaded.OnAction("projects_plus1");
    Check(loaded.All()[0].materials[0].required == 18, "row action edits selected page row");
    loaded.OnAction("projects_page_next");
    loaded.Sample(p.host, inv);
    Check(p.text["projects.page"] == "Materials 7-12 / 12", "material pagination exposes later requirements");
    const std::string corrupt = "{bad json";
    { std::ofstream out(file); out << corrupt; }
    Projects damaged(config.c_str());
    damaged.OnAction("projects_add_choice");
    std::ifstream in(file); std::string contents((std::istreambuf_iterator<char>(in)), {});
    Check(contents == corrupt && damaged.StorageStatus().find("unreadable") != std::string::npos,
          "invalid existing files are never overwritten by session edits");
    const auto invalid_data = nlohmann::json{{"version", 1}, {"projects", nlohmann::json::array({
        {{"name", "Overflow"}, {"materials", nlohmann::json::array({
            {{"id", "planks"}, {"name", "Invalid variant"},
             {"aux", std::numeric_limits<std::uint64_t>::max()}, {"required", 64}}
        })}}
    })}};
    { std::ofstream out(file); out << invalid_data.dump(); }
    Projects invalid(config.c_str());
    Check(invalid.All()[0].materials.empty() && invalid.StorageStatus().find("unreadable") != std::string::npos,
          "unsigned overflowing variant rejected before integer conversion");
    const auto relative_config = nlohmann::json{{"projects_file", "relative.json"}}.dump();
    Projects relative(relative_config.c_str());
    Check(relative.StorageStatus().find("invalid") != std::string::npos, "relative storage paths rejected");
    const auto blocked = dir / "blocked";
    { std::ofstream out(blocked); out << "a file, not a directory"; }
    const auto blocked_config = nlohmann::json{{"data_directory", blocked.string()}}.dump();
    Projects failed(blocked_config.c_str());
    failed.OnAction("projects_add_choice");
    Check(failed.All()[0].materials.size() == 1 && failed.StorageStatus().find("Save failed") != std::string::npos,
          "write failure retains project in session with visible status");
    std::filesystem::remove_all(dir);
    std::puts("projects tests passed");
}
