// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "mc_assets.h"

namespace mc_exploration {
// One store belongs to one explicit world and dimension. Never infer these from guest pointers.
class History {
public:
    static constexpr std::size_t MaxTiles = 8192, MaxRoute = 8192;
    struct Point { std::int32_t x{}, z{}; bool start{}; };
    History(std::string world, int dimension, std::filesystem::path directory = {});
    bool IsScope(const std::string& key, int dim) const { return world == key && dimension == dim; }
    bool Load();
    bool Save();
    /// `biomes`, when given, holds one biome id per pixel (mc_biomes::Unknown: not read).
    void Merge(int x, int z, const mc_assets::Image& image,
               const std::vector<std::uint8_t>* biomes = nullptr);
    void Visit(int x, int z, bool start);
    /// With `biome_view` the terrain is tinted by its recorded biome; `biomes`, when given,
    /// receives the recorded biome id of every pixel.
    mc_assets::Image Draw(int x, int z, int size, bool routes = true, bool biome_view = false,
                          std::vector<std::uint8_t>* biomes = nullptr) const;
    std::size_t TileCount() const { return tiles.size(); }
    std::size_t RouteCount() const { return route.size(); }
    bool Dirty() const { return dirty; }
    const std::string& Diagnostic() const { return diagnostic; }
    std::filesystem::path File() const;
private:
    struct Tile {
        std::array<std::uint8_t, 16 * 16 * 4> rgba{};
        std::array<std::uint8_t, 16 * 16> biome; // filled with mc_biomes::Unknown
        Tile();
    };
    std::string world;
    int dimension;
    std::filesystem::path directory;
    std::map<std::pair<int,int>, Tile> tiles;
    std::vector<Point> route;
    bool dirty{}, blocked{};
    std::string diagnostic;
};
std::filesystem::path DefaultDirectory();
}
