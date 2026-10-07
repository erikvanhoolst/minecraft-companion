// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_notebook.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <limits>
using namespace mc_notebook;
using Json = nlohmann::json;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct Publisher {
    std::map<std::string, std::string> text;
    std::map<std::string, std::int64_t> ints;
    EdenDsmodHostApi host{};
    Publisher() {
        host.userdata = this;
        host.publish_text = [](void* p, const char* k, const char* v) { static_cast<Publisher*>(p)->text[k] = v; };
        host.publish_i64 = [](void* p, const char* k, std::int64_t v) { static_cast<Publisher*>(p)->ints[k] = v; };
    }
};
std::string Contents(const std::filesystem::path& p) {
    std::ifstream in(p); return {std::istreambuf_iterator<char>(in), {}};
}
void Write(const std::filesystem::path& p, const std::string& data) { std::ofstream out(p); out << data; }
int main() {
    const auto dir = std::filesystem::temp_directory_path() / ("mc-notebook-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = dir / "nested/redstone.json";
    const auto config = Json{{"notebook_file", file.string()}}.dump();
    Publisher p;
    Notebook notebook(config.c_str());
    notebook.Sample(p.host);
    Check(notebook.All().size() == 1 && p.ints["notebook.schema"] == 1, "default example works without game or storage");
    auto key = p.text["notebook.image"].substr(10);
    auto image = notebook.LoadImage(key);
    Check(image && image->width == 512 && image->height == 384 && image->rgba.size() == 512 * 384 * 4,
          "schema returns an RGBA image without game assets");
    Check(!notebook.LoadImage("notebook/schema/garbage"), "unknown image keys rejected");
    Check(!notebook.OnAction(nullptr) && !notebook.OnAction("projects_new") && !notebook.OnAction("notebook_unknown"), "unrelated actions ignored");
    notebook.OnAction("notebook_toggle", 0);
    Check(!notebook.All()[0].steps[0].done, "hidden step actions cannot toggle checklist");
    notebook.OnAction("notebook_steps"); notebook.OnAction("notebook_toggle", -1); notebook.OnAction("notebook_toggle", 4);
    notebook.OnAction("notebook_toggle", 0); notebook.Sample(p.host);
    Check(p.text["notebook.progress"] == "1 / 4 steps done" && p.text["notebook.step0.check"] == "[x] 1", "touch toggles progress and checkmark");
    Check(std::filesystem::exists(file), "first edit creates nested storage");
    Notebook restored(config.c_str());
    Check(restored.All()[0].steps[0].done, "step completion survives reload");
    notebook.OnAction("notebook_new"); notebook.Sample(p.host);
    Check(notebook.All().size() == 2 && p.text["notebook.image"].substr(10) != key && !notebook.LoadImage(key),
          "switching designs changes image key and rejects stale schema");
    Notebook selected(config.c_str()); selected.Sample(p.host);
    Check(p.text["notebook.design"] == "Design 2 / 2", "active design persists");
    notebook.OnAction("notebook_delete_confirm"); Check(notebook.All().size() == 2, "delete requires arming");
    notebook.OnAction("notebook_delete"); notebook.OnAction("notebook_previous"); notebook.OnAction("notebook_delete_confirm");
    Check(notebook.All().size() == 2, "design navigation cancels deletion");
    notebook.OnAction("notebook_delete"); notebook.OnAction("notebook_delete_confirm");
    Check(notebook.All().size() == 1, "confirmed deletion removes just the active design");

    Json data = Json::parse(Contents(file));
    auto& design = data["designs"][0];
    design["name"] = "Eigen ontwerp";
    design["schema"] = {".#wLT^>v<PBO"};
    design["steps"] = Json::array();
    for (int i = 0; i < 32; ++i) design["steps"].push_back({{"text", i == 0 ? std::string(192, 'a') : "Step " + std::to_string(i + 1)}, {"done", false}});
    std::string notes;
    for (int i = 0; i < 80; ++i) notes += "Line " + std::to_string(i) + " é\n";
    design["notes"] = notes;
    Write(file, data.dump());
    notebook.OnAction("notebook_steps"); notebook.OnAction("notebook_toggle", 0); notebook.Sample(p.host);
    Check(p.text["notebook.storage"].find("tap Reload") != std::string::npos && Json::parse(Contents(file))["designs"][0]["name"] == "Eigen ontwerp",
          "session edits never overwrite an externally imported notebook");
    notebook.OnAction("notebook_reload"); notebook.Sample(p.host);
    Check(notebook.All()[0].name == "Eigen ontwerp" && p.text["notebook.step0.line2"].size() == 48,
          "reload accepts custom schema, notes and full length steps");
    for (int i = 0; i < 20; ++i) notebook.OnAction("notebook_page_next");
    notebook.Sample(p.host);
    Check(p.text["notebook.page"] == "Page 8 / 8" && p.text["notebook.step3.check"] == "[ ] 32", "all 32 steps reachable and pagination bounded");
    notebook.OnAction("notebook_toggle", 3);
    Check(notebook.All()[0].steps[31].done, "row action uses current step page");
    notebook.OnAction("notebook_notes");
    for (int i = 0; i < 20; ++i) notebook.OnAction("notebook_page_next");
    notebook.Sample(p.host);
    Check(p.text["notebook.page"] == "Page 6 / 6" && p.text["notebook.note11"] == "Line 79 é", "title and final Unicode note lines remain reachable");
    notebook.OnAction("notebook_page_previous"); notebook.Sample(p.host);
    Check(p.text["notebook.page"] == "Page 5 / 6", "notes scroll independently of step page");
    Check(notebook.LoadImage(p.text["notebook.image"].substr(10)).has_value(), "all schematic token types render");
    const auto arrows = RenderSchema({"^>v<"});
    auto pixel = [&](int x, int y, int channel) { return arrows.rgba[(y * 512 + x) * 4 + channel]; };
    Check(pixel(15, 7, 0) == 245 && pixel(15, 7, 1) == 245 && pixel(32+24, 15, 0) == 245 &&
          pixel(64+15, 24, 0) == 245 && pixel(96+7, 15, 0) == 245, "repeater arrowheads point up, right, down and left");
    const auto saved = Contents(file);
    Write(file, "{broken"); notebook.OnAction("notebook_reload"); notebook.OnAction("notebook_new"); notebook.Sample(p.host);
    Check(Contents(file) == "{broken" && notebook.All()[0].name == "Eigen ontwerp" && p.text["notebook.storage"].find("unreadable") != std::string::npos,
          "failed reload keeps previous designs and preserves damaged file");
    Write(file, saved); notebook.OnAction("notebook_reload"); notebook.OnAction("notebook_steps"); notebook.OnAction("notebook_toggle", 0);
    Check(Json::parse(Contents(file))["designs"][0]["steps"][0]["done"] == true, "valid reload recovers saving after an error");
    const auto valid = Json::parse(Contents(file));
    for (int scenario = 0; scenario < 9; ++scenario) {
        auto bad = valid;
        if (scenario == 0) bad["active"] = std::numeric_limits<std::uint64_t>::max();
        if (scenario == 1) bad["active"] = -1;
        if (scenario == 2) bad["designs"][0]["schema"] = {"..", "."};
        if (scenario == 3) bad["designs"][0]["schema"] = {"?"};
        if (scenario == 4) bad["designs"][0]["notes"] = std::string(4097, 'a');
        if (scenario == 5) bad["designs"][0]["steps"][0]["done"] = 1;
        if (scenario == 6) bad["designs"][0]["steps"][0]["text"] = "control\tchar";
        if (scenario == 7) bad["designs"].push_back(bad["designs"][0]);
        if (scenario == 8) bad["version"] = 1.0;
        Write(file, bad.dump()); Notebook invalid(config.c_str()); invalid.Sample(p.host);
        Check(p.text["notebook.storage"].find("unreadable") != std::string::npos, "malformed data rejected without partial loading");
        invalid.OnAction("notebook_new"); Check(Contents(file) == bad.dump(), "malformed data preserved");
    }
    const auto relative = Json{{"notebook_file", "relative.json"}}.dump();
    Notebook invalid_config(relative.c_str()); invalid_config.Sample(p.host);
    Check(p.text["notebook.storage"].find("invalid") != std::string::npos, "relative storage config refused");
    std::filesystem::remove(file); std::filesystem::create_symlink(dir / "target", file);
    Notebook symlink(config.c_str()); symlink.OnAction("notebook_new");
    Check(!std::filesystem::exists(dir / "target"), "symlinks never used to save data");
    const auto blocked = dir / "blocked"; Write(blocked, "not a directory");
    const auto fail_config = Json{{"data_directory", blocked.string()}}.dump();
    Notebook fail(fail_config.c_str()); fail.OnAction("notebook_new"); fail.Sample(p.host);
    Check(fail.All().size() == 2 && p.text["notebook.storage"].find("Save failed") != std::string::npos, "save errors retain session edits and publish honest status");
    std::filesystem::remove_all(dir);
    std::puts("notebook tests passed");
}
