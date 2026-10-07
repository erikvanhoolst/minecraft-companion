// SPDX-License-Identifier: GPL-3.0-or-later
#include "mc_projects.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string_view>

namespace mc_projects {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaxProjects = 8, MaxMaterials = 24, Rows = 6;
constexpr int MaxQuantity = 999999;
const std::array<Material, 12> Choices{{
    {"cobblestone", "Cobblestone", 0}, {"planks", "Oak planks", 0},
    {"glass", "Glass", 0}, {"stone", "Stone", 0}, {"log", "Oak wood", 0},
    {"planks", "Spruce planks", 1}, {"planks", "Birch planks", 2},
    {"dirt", "Dirt", 0}, {"sand", "Sand", 0}, {"brick_block", "Bricks", 0},
    {"stonebrick", "Stone bricks", 0}, {"glass_pane", "Glass panes", 0}}};
const std::array<std::string_view, 7> Names{"House", "Tower", "Bridge", "Storage", "Farm", "Mine", "Project"};
bool ValidId(const std::string& id) {
    return !id.empty() && id.size() <= 128 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '-';
           });
}
bool ValidName(const std::string& name) {
    return !name.empty() && name.size() <= 64 &&
           std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
std::string Clip(const std::string& name, std::size_t limit) {
    if (name.size() <= limit) return name;
    std::size_t end = limit - 3;
    while (end && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80) --end;
    return name.substr(0, end) + "...";
}
std::filesystem::path StorageFile(const Json& config) {
    if (config.contains("projects_file")) {
        auto path = std::filesystem::path(config.at("projects_file").get<std::string>());
        if (!path.is_absolute()) throw std::runtime_error("absolute projects_file required");
        return path;
    }
    std::filesystem::path dir;
    if (config.contains("data_directory")) {
        dir = config.at("data_directory").get<std::string>();
        if (!dir.is_absolute()) throw std::runtime_error("absolute data_directory required");
    }
#ifndef __ANDROID__
    else if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        dir = std::filesystem::path(xdg) / "minecraft-companion";
        if (!dir.is_absolute()) return {};
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        dir = std::filesystem::path(home) / ".local/share/minecraft-companion";
        if (!dir.is_absolute()) return {};
    }
#endif
    return dir.empty() ? std::filesystem::path{} : dir / "projects.json";
}
} // namespace

std::string CanonicalId(std::string id) {
    if (id.starts_with("minecraft:")) id.erase(0, 10);
    else if (id.starts_with("tile.")) id.erase(0, 5);
    else if (id.starts_with("item.")) id.erase(0, 5);
    return id;
}
Count CountMaterial(const Material& material, const mc_reader::InventorySnapshot& inv) {
    if (!inv.ready) return {};
    Count count{true};
    for (const auto& slot : inv.slots) {
        // Unknown non-empty identities can hide matching materials. Never claim a deficit then.
        if (slot.count > 0 && !ValidId(slot.id)) return {};
        if (slot.count > 0 && CanonicalId(slot.id) == CanonicalId(material.id) && slot.aux == material.aux)
            count.owned += slot.count;
    }
    count.missing = std::max(0, material.required - count.owned);
    return count;
}

Projects::Projects(const char* config_json) {
    try {
        auto config = config_json && *config_json ? Json::parse(config_json) : Json::object();
        if (!config.is_object()) throw std::runtime_error("object config required");
        file = StorageFile(config);
        storage_status = file.empty() ? "Session only: no data directory" : "Storage ready";
        Load();
    } catch (...) {
        storage_blocked = true;
        storage_status = "Session only: invalid storage config";
    }
    if (projects.empty()) projects.push_back({"Project 1", {}});
}

void Projects::Load() {
    if (file.empty()) return;
    try {
        if (!std::filesystem::exists(file)) return;
        if (std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file) ||
            std::filesystem::file_size(file) > 256 * 1024)
            throw std::runtime_error("unsafe project file");
        std::ifstream in(file);
        if (!in) throw std::runtime_error("cannot open");
        const auto data = Json::parse(in);
        if (!data.is_object() || data.at("version") != 1 || !data.at("projects").is_array() ||
            data.at("projects").empty() || data.at("projects").size() > MaxProjects)
            throw std::runtime_error("invalid projects");
        std::vector<Project> loaded;
        for (const auto& entry : data.at("projects")) {
            Project p{entry.at("name").get<std::string>(), {}};
            if (!ValidName(p.name) || !entry.at("materials").is_array() ||
                entry.at("materials").size() > MaxMaterials) throw std::runtime_error("invalid project");
            for (const auto& raw : entry.at("materials")) {
                if (!raw.at("aux").is_number_integer() || !raw.at("required").is_number_integer())
                    throw std::runtime_error("integer quantities required");
                const auto bounded = [](const Json& number, std::int64_t low, std::int64_t high) {
                    if (number.is_number_unsigned() && number.get<std::uint64_t>() > static_cast<std::uint64_t>(high))
                        throw std::runtime_error("material out of range");
                    const auto value = number.get<std::int64_t>();
                    if (value < low || value > high) throw std::runtime_error("material out of range");
                    return value;
                };
                const auto aux = bounded(raw.at("aux"), -32768, 32767);
                const auto required = bounded(raw.at("required"), 1, MaxQuantity);
                Material m{CanonicalId(raw.at("id").get<std::string>()), raw.at("name").get<std::string>(),
                           static_cast<int>(aux), static_cast<int>(required)};
                if (!ValidId(m.id) || !ValidName(m.name) || aux < -32768 || aux > 32767 ||
                    required < 1 || required > MaxQuantity) throw std::runtime_error("invalid material");
                if (std::any_of(p.materials.begin(), p.materials.end(), [&](const Material& other) {
                        return other.id == m.id && other.aux == m.aux;
                    })) throw std::runtime_error("duplicate material");
                p.materials.push_back(std::move(m));
            }
            loaded.push_back(std::move(p));
        }
        projects = std::move(loaded);
        storage_status = "Loaded from device";
    } catch (...) {
        storage_blocked = true;
        storage_status = "Session only: existing project file unreadable";
    }
}

void Projects::Save() {
    if (file.empty() || storage_blocked) return;
    std::filesystem::path temporary;
    try {
        if (std::filesystem::is_symlink(file)) throw std::runtime_error("symlink refused");
        std::filesystem::create_directories(file.parent_path());
        Json data{{"version", 1}, {"projects", Json::array()}};
        for (const auto& project : projects) {
            Json materials = Json::array();
            for (const auto& m : project.materials)
                materials.push_back({{"id", m.id}, {"name", m.name}, {"aux", m.aux}, {"required", m.required}});
            data["projects"].push_back({{"name", project.name}, {"materials", materials}});
        }
        temporary = file;
        temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << data.dump(2) << '\n';
            out.flush();
            if (!out) throw std::runtime_error("write failed");
        }
        std::filesystem::rename(temporary, file);
        storage_status = "Saved on device";
    } catch (...) {
        if (!temporary.empty()) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
        }
        storage_status = "Save failed: changes kept for this session";
    }
}

void Projects::Add(Material m) {
    auto& materials = projects[active].materials;
    m.id = CanonicalId(std::move(m.id));
    const auto existing = std::find_if(materials.begin(), materials.end(), [&](const Material& old) {
        return old.id == m.id && old.aux == m.aux;
    });
    if (existing != materials.end()) {
        selected = static_cast<std::size_t>(existing - materials.begin());
        message = "Already listed: adjust its quantity";
    } else if (materials.size() >= MaxMaterials) {
        message = "Project limit: 24 materials";
        return;
    } else {
        materials.push_back(std::move(m));
        selected = materials.size() - 1;
        message = "Material added";
        Save();
    }
    first = selected / Rows * Rows;
}

bool Projects::OnAction(const char* raw, std::int64_t) {
    if (!raw) return false;
    const std::string_view action(raw);
    if (!action.starts_with("projects_")) return false;
    auto& materials = projects[active].materials;
    if (action != "projects_delete" && action != "projects_delete_confirm") delete_armed = false;
    bool changed = false;
    if (action == "projects_delete") {
        delete_armed = true;
        message = "Tap Confirm delete to remove this project";
    } else if (action == "projects_delete_confirm") {
        if (delete_armed) {
            projects.erase(projects.begin() + static_cast<std::ptrdiff_t>(active));
            if (projects.empty()) projects.push_back({"Project 1", {}});
            active = std::min(active, projects.size() - 1);
            selected = first = 0;
            delete_armed = false;
            changed = true;
        }
    } else if (action == "projects_new") {
        if (projects.size() >= MaxProjects) message = "Project limit: 8 projects";
        else {
            projects.push_back({"Project " + std::to_string(projects.size() + 1), {}});
            active = projects.size() - 1;
            selected = first = 0;
            message = "New project: add materials below";
            changed = true;
        }
    } else if (action == "projects_previous" || action == "projects_next") {
        active = action == "projects_next" ? (active + 1) % projects.size() :
                 (active + projects.size() - 1) % projects.size();
        selected = first = 0;
        message.clear();
    } else if (action == "projects_name") {
        auto& name = projects[active].name;
        std::size_t index = 0;
        for (; index < Names.size(); ++index) if (name.starts_with(Names[index])) break;
        name = std::string(Names[(index + 1) % Names.size()]) + " " + std::to_string(active + 1);
        changed = true;
    } else if (action == "projects_choice_previous" || action == "projects_choice_next") {
        choice = action == "projects_choice_next" ? (choice + 1) % Choices.size() :
                 (choice + Choices.size() - 1) % Choices.size();
    } else if (action == "projects_add_choice") {
        Add(Choices[choice]);
    } else if (action == "projects_add_selected") {
        if (!inventory.ready || inventory.selected < 0 || inventory.selected >= 9)
            message = "Selected inventory item unavailable";
        else {
            const auto& slot = inventory.slots[static_cast<std::size_t>(inventory.selected)];
            if (!slot.count || !ValidId(slot.id)) message = "Hold a material in your selected hotbar slot";
            else Add({slot.id, ValidName(slot.name) ? slot.name : CanonicalId(slot.id), slot.aux, 64});
        }
    } else if (action == "projects_page_previous" || action == "projects_page_next") {
        if (action == "projects_page_previous") first = first >= Rows ? first - Rows : 0;
        else if (first + Rows < materials.size()) first += Rows;
        if (!materials.empty()) selected = first;
    } else if (action.size() == 14 && action.starts_with("projects_row_") && action.back() >= '0' && action.back() <= '5') {
        const std::size_t row = first + static_cast<std::size_t>(action.back() - '0');
        if (row < materials.size()) selected = row;
    } else if (action == "projects_remove") {
        if (!materials.empty()) {
            materials.erase(materials.begin() + static_cast<std::ptrdiff_t>(selected));
            selected = materials.empty() ? 0 : std::min(selected, materials.size() - 1);
            first = selected / Rows * Rows;
            changed = true;
        }
    } else {
        int delta = 0;
        if (action == "projects_plus1") delta = 1;
        else if (action == "projects_minus1") delta = -1;
        else if (action == "projects_plus16") delta = 16;
        else if (action == "projects_minus16") delta = -16;
        else if (action == "projects_plus64") delta = 64;
        else if (action == "projects_minus64") delta = -64;
        else return false;
        if (!materials.empty()) {
            materials[selected].required = std::clamp(materials[selected].required + delta, 1, MaxQuantity);
            changed = true;
        }
    }
    if (changed) { message = "Project updated"; Save(); }
    return true;
}

void Projects::Sample(const EdenDsmodHostApi& host, const mc_reader::InventorySnapshot& inv) {
    inventory = inv;
    Publish(host);
}
void Projects::Publish(const EdenDsmodHostApi& host) const {
    const auto text = [&](const std::string& key, const std::string& value) {
        if (host.publish_text) host.publish_text(host.userdata, key.c_str(), value.c_str());
    };
    const auto integer = [&](const std::string& key, std::int64_t value) {
        if (host.publish_i64) host.publish_i64(host.userdata, key.c_str(), value);
    };
    const auto& project = projects[active];
    text("projects.name", Clip(project.name, 26));
    text("projects.position", std::to_string(active + 1) + " / " + std::to_string(projects.size()));
    text("projects.choice", Choices[choice].name);
    text("projects.message", message);
    text("projects.storage", storage_status);
    text("projects.page", "Materials " + std::to_string(project.materials.empty() ? 0 : first + 1) +
         "-" + std::to_string(std::min(first + Rows, project.materials.size())) +
         " / " + std::to_string(project.materials.size()));
    bool known = inventory.ready;
    int total_missing = 0;
    for (const auto& material : project.materials) {
        const auto count = CountMaterial(material, inventory);
        known = known && count.known;
        total_missing += count.missing;
    }
    text("projects.summary", !known ? "Inventory unavailable: remaining counts unknown" :
         project.materials.empty() ? "Add materials to begin" : total_missing == 0 ? "All materials in inventory" :
         "Still gather: " + std::to_string(total_missing) + " items");
    text("projects.selected", project.materials.empty() ? "Select a material to edit its quantity" :
         Clip(project.materials[selected].name, 26) + " (variant " + std::to_string(project.materials[selected].aux) + ")");
    integer("projects.has_materials", !project.materials.empty());
    integer("projects.delete_armed", delete_armed);
    for (std::size_t row = 0; row < Rows; ++row) {
        const auto key = "projects.row" + std::to_string(row);
        const auto index = first + row;
        const bool present = index < project.materials.size();
        integer(key + ".visible", present);
        if (!present) { text(key + ".name", ""); text(key + ".counts", ""); continue; }
        const auto& m = project.materials[index];
        const auto count = CountMaterial(m, inventory);
        text(key + ".name", (selected == index ? "> " : "") + Clip(m.name, 28));
        text(key + ".counts", std::to_string(m.required) + " need  /  " +
             (count.known ? std::to_string(count.owned) + " have  /  " + std::to_string(count.missing) + " left" :
                            "? have  /  ? left"));
        integer(key + ".required", m.required);
        integer(key + ".owned", count.known ? count.owned : -1);
        integer(key + ".missing", count.known ? count.missing : -1);
        integer(key + ".known", count.known);
    }
}
} // namespace mc_projects
