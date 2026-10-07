// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_notebook.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace mc_notebook {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaxDesigns = 16, StepRows = 4, NoteRows = 14;
constexpr std::string_view Tokens = ".#wLT^>v<PBO";
bool ValidText(const std::string& s, std::size_t limit, bool multiline = false) {
    return s.size() <= limit && std::none_of(s.begin(), s.end(), [multiline](unsigned char c) {
            return (c < 32 && !(multiline && c == '\n')) || c == 127;
        });
}
// Wrap whole UTF-8 codepoints, including long unbroken words, and preserve blank lines.
std::vector<std::string> Lines(const std::string& s, std::size_t width = 72, bool words = true) {
    std::vector<std::string> out;
    std::string line;
    std::size_t columns = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\n') { out.push_back(line); line.clear(); columns = 0; ++i; continue; }
        if (columns == width) {
            const auto space = line.find_last_of(' ');
            if (words && space != std::string::npos && space > width / 2) {
                out.push_back(line.substr(0, space)); line.erase(0, space + 1);
                columns = std::count_if(line.begin(), line.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
            } else { out.push_back(line); line.clear(); columns = 0; }
        }
        std::size_t end = i + 1;
        while (end < s.size() && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80) ++end;
        line.append(s, i, end - i); i = end; ++columns;
    }
    out.push_back(line);
    return out;
}
Design Example() {
    return {"Lever lamp", {".........", ".Lwww>wwO", "........."},
        {{"Place a row of solid support blocks under the wire and repeater."},
         {"Place the lever (L), redstone dust (w), and lamp (O) as shown."},
         {"Place the repeater facing right (>); use its default one-tick delay."},
         {"Flip the lever and check that the lamp turns on and off."}},
        "Top-down plan; each cell is one block. All components are on the same level.\n"
        "L = lever, w = dust, arrows = repeater direction, O = lamp.\n"
        "Import your own designs with scripts/import_redstone.py, then tap Reload. "
        "Use notes for layers, repeater delays and changes to try."};
}
std::filesystem::path Storage(const Json& config) {
    if (config.contains("notebook_file")) {
        std::filesystem::path p = config.at("notebook_file").get<std::string>();
        if (!p.is_absolute()) throw std::runtime_error("absolute notebook_file required");
        return p;
    }
    std::filesystem::path dir;
    if (config.contains("data_directory")) {
        dir = config.at("data_directory").get<std::string>();
        if (!dir.is_absolute()) throw std::runtime_error("absolute data_directory required");
    }
#ifndef __ANDROID__
    else if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) dir = std::filesystem::path(xdg) / "minecraft-companion";
    else if (const char* home = std::getenv("HOME"); home && *home) dir = std::filesystem::path(home) / ".local/share/minecraft-companion";
#endif
    return dir.is_absolute() ? dir / "redstone.json" : std::filesystem::path{};
}
}

mc_assets::Image RenderSchema(const std::vector<std::string>& rows) {
    // Fixed 16 x 12 canvas keeps cell size and direction consistent for all designs.
    mc_assets::Image image{512, 384, std::vector<std::uint8_t>(512 * 384 * 4)};
    using Color = std::array<std::uint8_t, 4>;
    auto rect = [&](int x, int y, int w, int h, Color color) {
        for (int py = std::max(0, y); py < std::min(384, y + h); ++py)
            for (int px = std::max(0, x); px < std::min(512, x + w); ++px)
                std::copy(color.begin(), color.end(), image.rgba.begin() + (py * 512 + px) * 4);
    };
    const Color red{228, 66, 58, 255}, stone{137, 146, 155, 255}, gold{245, 187, 76, 255}, white{245, 245, 245, 255};
    rect(0, 0, 512, 384, {36, 38, 41, 255});
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 16; ++x) {
        const int bx = x * 32, by = y * 32;
        rect(bx, by, 31, 31, {49, 52, 57, 255});
        const char c = y < static_cast<int>(rows.size()) && x < static_cast<int>(rows[y].size()) ? rows[y][x] : '.';
        if (c == '.') continue;
        if (c == 'w') { rect(bx, by + 14, 31, 4, red); rect(bx + 14, by, 4, 31, red); }
        else if (c == '#' || c == 'P' || c == 'B' || c == 'O') {
            rect(bx + 5, by + 5, 22, 22, c == '#' ? stone : c == 'O' ? gold : stone);
            if (c == 'P') rect(bx + 8, by + 11, 16, 10, gold);
            if (c == 'B') rect(bx + 11, by + 11, 10, 10, red);
            if (c == 'O') rect(bx + 10, by + 10, 12, 12, {255, 232, 146, 255});
        } else if (c == 'L') {
            rect(bx + 6, by + 21, 20, 6, stone);
            for (int i = 0; i < 14; ++i) rect(bx + 9 + i / 2, by + 21 - i, 5, 3, gold);
        } else if (c == 'T') { rect(bx + 14, by + 15, 5, 13, gold); rect(bx + 10, by + 5, 13, 13, red); }
        else {
            rect(bx + 4, by + 4, 24, 24, stone);
            const bool horizontal = c == '>' || c == '<';
            const bool forward = c == '>' || c == 'v';
            for (int i = 6; i <= 25; ++i) {
                const int pos = forward ? i : 31 - i;
                if (horizontal) rect(bx + pos, by + 14, 1, 4, red);
                else rect(bx + 14, by + pos, 4, 1, red);
            }
            for (int i = 0; i < 8; ++i) {
                const int pos = forward ? 24 - i : 7 + i;
                if (horizontal) { rect(bx + pos, by + 15 - i, 2, 2, white); rect(bx + pos, by + 15 + i, 2, 2, white); }
                else { rect(bx + 15 - i, by + pos, 2, 2, white); rect(bx + 15 + i, by + pos, 2, 2, white); }
            }
        }
    }
    return image;
}

Notebook::Notebook(const char* config_json) {
    try {
        const auto config = config_json && *config_json ? Json::parse(config_json) : Json::object();
        if (!config.is_object()) throw std::runtime_error("object config required");
        file = Storage(config);
        status = file.empty() ? "Session only: no data directory" : "Storage ready";
        Load();
    } catch (...) { blocked = true; status = "Session only: invalid storage config"; }
    if (designs.empty()) designs.push_back(Example());
}

bool Notebook::Load() {
    if (file.empty()) { message = "Configure storage to import designs"; return false; }
    try {
        if (std::filesystem::is_symlink(file)) throw std::runtime_error("symlink refused");
        if (!std::filesystem::exists(file)) {
            file_time.reset(); file_size = 0; blocked = false; status = "Storage ready";
            message = designs.empty() ? "No saved notebook yet; showing example" : "No saved notebook; session designs kept";
            return false;
        }
        if (!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file) > 256 * 1024)
            throw std::runtime_error("unsafe file");
        std::ifstream in(file);
        if (!in) throw std::runtime_error("cannot open");
        const auto data = Json::parse(in);
        if (!data.is_object() || !data.at("version").is_number_integer() || data.at("version") != 1 || !data.at("designs").is_array() ||
            data.at("designs").empty() || data.at("designs").size() > MaxDesigns) throw std::runtime_error("invalid notebook");
        std::vector<Design> loaded;
        for (const auto& entry : data.at("designs")) {
            Design d;
            d.name = entry.at("name").get<std::string>();
            d.notes = entry.at("notes").get<std::string>();
            if (d.name.empty() || !ValidText(d.name, 64) || !ValidText(d.notes, 4096, true) ||
                !entry.at("schema").is_array() || entry.at("schema").empty() || entry.at("schema").size() > 12 ||
                !entry.at("steps").is_array() || entry.at("steps").size() > 32) throw std::runtime_error("invalid design");
            std::size_t width = 0;
            for (const auto& raw : entry.at("schema")) {
                auto row = raw.get<std::string>();
                if (row.empty() || row.size() > 16 || (width && width != row.size()) || row.find_first_not_of(Tokens) != std::string::npos)
                    throw std::runtime_error("invalid schema");
                width = row.size(); d.schema.push_back(std::move(row));
            }
            for (const auto& raw : entry.at("steps")) {
                Step step{raw.at("text").get<std::string>(), raw.at("done").get<bool>()};
                if (step.text.empty() || !ValidText(step.text, 192)) throw std::runtime_error("invalid step");
                d.steps.push_back(std::move(step));
            }
            if (std::any_of(loaded.begin(), loaded.end(), [&](const Design& other) { return other.name == d.name; }))
                throw std::runtime_error("duplicate design name");
            loaded.push_back(std::move(d));
        }
        std::size_t selected = 0;
        if (data.contains("active")) {
            const auto& value = data.at("active");
            if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<std::int64_t>() < 0))
                throw std::runtime_error("invalid selection");
            const auto index = value.get<std::uint64_t>();
            if (index >= loaded.size()) throw std::runtime_error("invalid selection");
            selected = static_cast<std::size_t>(index);
        }
        const auto loaded_time = std::filesystem::last_write_time(file);
        const auto loaded_size = std::filesystem::file_size(file);
        designs = std::move(loaded); file_time = loaded_time; file_size = loaded_size; active = selected; step_page = note_page = 0;
        ++revision; blocked = false; delete_armed = false;
        status = "Loaded from device"; message = "Notebook loaded";
        return true;
    } catch (...) {
        blocked = true; status = "Session only: existing notebook unreadable";
        message = "Reload failed; previous designs kept"; return false;
    }
}

void Notebook::Save() {
    if (file.empty() || blocked) return;
    std::filesystem::path temporary;
    try {
        if (std::filesystem::is_symlink(file)) throw std::runtime_error("symlink refused");
        const bool exists = std::filesystem::exists(file);
        if (exists != file_time.has_value() || (exists &&
            (std::filesystem::last_write_time(file) != *file_time || std::filesystem::file_size(file) != file_size))) {
            blocked = true; status = "Notebook changed on device: tap Reload"; return;
        }
        std::filesystem::create_directories(file.parent_path());
        Json data{{"version", 1}, {"active", active}, {"designs", Json::array()}};
        for (const auto& d : designs) {
            Json steps = Json::array();
            for (const auto& s : d.steps) steps.push_back({{"text", s.text}, {"done", s.done}});
            data["designs"].push_back({{"name", d.name}, {"schema", d.schema}, {"steps", steps}, {"notes", d.notes}});
        }
        temporary = file; temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        { std::ofstream out(temporary, std::ios::binary); out << data.dump(2) << '\n'; out.flush(); if (!out) throw std::runtime_error("write failed"); }
        std::filesystem::rename(temporary, file);
        file_time = std::filesystem::last_write_time(file); file_size = std::filesystem::file_size(file);
        status = "Saved on device";
    } catch (...) {
        if (!temporary.empty()) { std::error_code ec; std::filesystem::remove(temporary, ec); }
        status = "Save failed: changes kept for this session";
    }
}

bool Notebook::OnAction(const char* raw, std::int64_t argument) {
    if (!raw) return false;
    const std::string_view action(raw);
    if (!action.starts_with("notebook_")) return false;
    std::lock_guard lock(mutex);
    if (action != "notebook_delete" && action != "notebook_delete_confirm") delete_armed = false;
    if (action == "notebook_reload") { Load(); return true; }
    if (action == "notebook_previous" || action == "notebook_next") {
        active = action == "notebook_next" ? (active + 1) % designs.size() : (active + designs.size() - 1) % designs.size();
        step_page = note_page = 0; ++revision; Save();
    } else if (action == "notebook_schema") view = 0;
    else if (action == "notebook_steps") view = 1;
    else if (action == "notebook_notes") view = 2;
    else if (action == "notebook_page_previous" || action == "notebook_page_next") {
        auto& page = view == 1 ? step_page : note_page;
        const auto count = view == 1 ? (designs[active].steps.size() + StepRows - 1) / StepRows :
            (Lines(designs[active].name + "\n\n" + (designs[active].notes.empty() ? "No notes yet." : designs[active].notes)).size() + NoteRows - 1) / NoteRows;
        if (action == "notebook_page_previous") { if (page) --page; }
        else if (page + 1 < count) ++page;
    } else if (action == "notebook_toggle") {
        if (view != 1 || argument < 0 || argument >= static_cast<std::int64_t>(StepRows)) return true;
        const auto index = step_page * StepRows + static_cast<std::size_t>(argument);
        if (index < designs[active].steps.size()) {
            designs[active].steps[index].done = !designs[active].steps[index].done; Save();
        }
    } else if (action == "notebook_new") {
        if (designs.size() == MaxDesigns) message = "Notebook limit: 16 designs";
        else { designs.push_back(Example()); active = designs.size() - 1; std::size_t suffix = 1;
            do { designs[active].name = "Design " + std::to_string(suffix++); }
            while (std::any_of(designs.begin(), designs.end() - 1, [&](const Design& d) { return d.name == designs[active].name; }));
            step_page = note_page = 0; ++revision; message = "Example added; import to customize"; Save(); }
    } else if (action == "notebook_delete") { delete_armed = true; message = "Tap Confirm delete to remove this design"; }
    else if (action == "notebook_delete_confirm") {
        if (delete_armed) {
            designs.erase(designs.begin() + static_cast<std::ptrdiff_t>(active));
            if (designs.empty()) designs.push_back(Example());
            active = std::min(active, designs.size() - 1); step_page = note_page = 0; ++revision;
            delete_armed = false; message = "Design removed"; Save();
        }
    } else return false;
    return true;
}

void Notebook::Sample(const EdenDsmodHostApi& host) {
    std::lock_guard lock(mutex);
    auto text = [&](const std::string& key, const std::string& value) { if (host.publish_text) host.publish_text(host.userdata, key.c_str(), value.c_str()); };
    auto integer = [&](const std::string& key, std::int64_t value) { if (host.publish_i64) host.publish_i64(host.userdata, key.c_str(), value); };
    const auto& d = designs[active];
    // Header is shortened only; full names also appear in the notes view.
    auto names = Lines(d.name, 29);
    text("notebook.name", names.front() + (names.size() > 1 ? "..." : ""));
    text("notebook.design", "Design " + std::to_string(active + 1) + " / " + std::to_string(designs.size()));
    const auto completed = std::count_if(d.steps.begin(), d.steps.end(), [](const Step& s) { return s.done; });
    text("notebook.progress", std::to_string(completed) + " / " + std::to_string(d.steps.size()) + " steps done");
    text("notebook.image", "module:mc:notebook/schema/" + std::to_string(revision));
    text("notebook.storage", status); text("notebook.message", message);
    integer("notebook.schema", view == 0); integer("notebook.steps", view == 1); integer("notebook.notes", view == 2);
    integer("notebook.paged", view != 0); integer("notebook.delete_armed", delete_armed);
    integer("notebook.no_steps", view == 1 && d.steps.empty());
    const auto notes = Lines(d.name + "\n\n" + (d.notes.empty() ? "No notes yet." : d.notes));
    // Pagination counts the title too, so no note lines become unreachable.
    note_page = std::min(note_page, (notes.size() - 1) / NoteRows);
    const auto page = view == 1 ? step_page : note_page;
    const auto pages = view == 1 ? std::max<std::size_t>(1, (d.steps.size() + StepRows - 1) / StepRows) : (notes.size() + NoteRows - 1) / NoteRows;
    text("notebook.page", "Page " + std::to_string(page + 1) + " / " + std::to_string(pages));
    for (std::size_t row = 0; row < StepRows; ++row) {
        const auto prefix = "notebook.step" + std::to_string(row);
        const auto index = step_page * StepRows + row;
        const bool present = view == 1 && index < d.steps.size();
        integer(prefix + ".visible", present);
        text(prefix + ".check", present ? (d.steps[index].done ? "[x] " : "[ ] ") + std::to_string(index + 1) : "");
        const auto lines = present ? Lines(d.steps[index].text, 72, false) : std::vector<std::string>{};
        for (std::size_t line = 0; line < 3; ++line) text(prefix + ".line" + std::to_string(line), line < lines.size() ? lines[line] : "");
    }
    for (std::size_t row = 0; row < NoteRows; ++row) {
        const auto index = note_page * NoteRows + row;
        text("notebook.note" + std::to_string(row), index < notes.size() ? notes[index] : "");
    }
}
std::optional<mc_assets::Image> Notebook::LoadImage(std::string_view key) {
    std::lock_guard lock(mutex);
    if (key != "notebook/schema/" + std::to_string(revision)) return std::nullopt;
    return RenderSchema(designs[active].schema);
}
std::vector<Design> Notebook::All() const { std::lock_guard lock(mutex); return designs; }
} // namespace mc_notebook
