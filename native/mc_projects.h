// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "mc_reader.h"
#include <filesystem>
#include <string>
#include <vector>

namespace mc_projects {
struct Material {
    std::string id, name;
    int aux{};
    int required{64};
};
struct Project {
    std::string name;
    std::vector<Material> materials;
};
struct Count {
    bool known{};
    int owned{}, missing{};
};

std::string CanonicalId(std::string id);
Count CountMaterial(const Material& material, const mc_reader::InventorySnapshot& inventory);

/// Projects are companion data; only immutable inventory snapshots enter this component.
class Projects {
public:
    explicit Projects(const char* config_json = nullptr);
    void Sample(const EdenDsmodHostApi& host, const mc_reader::InventorySnapshot& inventory);
    bool OnAction(const char* action, std::int64_t argument = 0);
    const std::vector<Project>& All() const { return projects; }
    const std::string& StorageStatus() const { return storage_status; }

private:
    void Load();
    void Save();
    void Publish(const EdenDsmodHostApi& host) const;
    void Add(Material material);
    std::vector<Project> projects;
    mc_reader::InventorySnapshot inventory;
    std::filesystem::path file;
    std::size_t active{}, selected{}, first{}, choice{};
    std::string message, storage_status;
    bool storage_blocked{}, delete_armed{};
};
} // namespace mc_projects
