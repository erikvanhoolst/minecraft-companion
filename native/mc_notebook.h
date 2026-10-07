// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mc_assets.h"
#include <filesystem>
#include <mutex>

namespace mc_notebook {
struct Step { std::string text; bool done{}; };
struct Design {
    std::string name;
    std::vector<std::string> schema;
    std::vector<Step> steps;
    std::string notes;
};
// Companion-owned schematic art; no Minecraft memory or resource packs are needed.
mc_assets::Image RenderSchema(const std::vector<std::string>& rows);
class Notebook {
public:
    explicit Notebook(const char* config = nullptr);
    void Sample(const EdenDsmodHostApi& host);
    bool OnAction(const char* action, std::int64_t argument = 0);
    std::optional<mc_assets::Image> LoadImage(std::string_view key);
    std::vector<Design> All() const;
private:
    bool Load();
    void Save();
    mutable std::mutex mutex;
    std::vector<Design> designs;
    std::filesystem::path file;
    std::optional<std::filesystem::file_time_type> file_time;
    std::uintmax_t file_size{};
    std::size_t active{}, step_page{}, note_page{};
    int view{};
    std::uint64_t revision{1};
    bool blocked{}, delete_armed{};
    std::string status, message;
};
} // namespace mc_notebook
