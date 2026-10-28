// SPDX-License-Identifier: GPL-3.0-or-later
// Biomes of Minecraft Bedrock 1.2 by their legacy id (LevelChunk's biome bytes): the English names
// the game used at the time and the colours biome maps commonly use for them (Amidst's palette).
// Variants (id + 128) are drawn a little lighter than the biome they vary.
#pragma once

#include <cstdint>

namespace mc_biomes {

constexpr std::uint8_t Unknown = 0xFF; // no biome recorded for a column

struct Biome {
    const char* name;
    std::uint32_t color; // 0xRRGGBB
};

namespace detail {
inline constexpr Biome Base[] = {
    {"Ocean", 0x000070},              {"Plains", 0x8DB360},
    {"Desert", 0xFA9418},             {"Extreme Hills", 0x606060},
    {"Forest", 0x056621},             {"Taiga", 0x0B6659},
    {"Swamp", 0x07F9B2},              {"River", 0x0000FF},
    {"Nether", 0xBF3B3B},             {"The End", 0x8080FF},
    {"Frozen Ocean", 0x9090A0},       {"Frozen River", 0xA0A0FF},
    {"Ice Plains", 0xFFFFFF},         {"Ice Mountains", 0xA0A0A0},
    {"Mushroom Island", 0xFF00FF},    {"Mushroom Shore", 0xA000FF},
    {"Beach", 0xFADE55},              {"Desert Hills", 0xD25F12},
    {"Forest Hills", 0x22551C},       {"Taiga Hills", 0x163933},
    {"Extreme Hills Edge", 0x72789A}, {"Jungle", 0x537B09},
    {"Jungle Hills", 0x2C4205},       {"Jungle Edge", 0x628B17},
    {"Deep Ocean", 0x000030},         {"Stone Beach", 0xA2A284},
    {"Cold Beach", 0xFAF0C0},         {"Birch Forest", 0x307444},
    {"Birch Forest Hills", 0x1F5F32}, {"Roofed Forest", 0x40511A},
    {"Cold Taiga", 0x31554A},         {"Cold Taiga Hills", 0x243F36},
    {"Mega Taiga", 0x596651},         {"Mega Taiga Hills", 0x545F3E},
    {"Extreme Hills+", 0x507050},     {"Savanna", 0xBDB25F},
    {"Savanna Plateau", 0xA79D64},    {"Mesa", 0xD94515},
    {"Mesa Plateau F", 0xB09765},     {"Mesa Plateau", 0xCA8C65},
};
constexpr int BaseCount = sizeof(Base) / sizeof(Base[0]);

struct Variant {
    std::uint8_t id;
    const char* name;
};
inline constexpr Variant Variants[] = {
    {129, "Sunflower Plains"},      {130, "Desert M"},
    {131, "Extreme Hills M"},       {132, "Flower Forest"},
    {133, "Taiga M"},               {134, "Swamp M"},
    {140, "Ice Plains Spikes"},     {149, "Jungle M"},
    {151, "Jungle Edge M"},         {155, "Birch Forest M"},
    {156, "Birch Forest Hills M"},  {157, "Roofed Forest M"},
    {158, "Cold Taiga M"},          {160, "Mega Spruce Taiga"},
    {161, "Mega Spruce Taiga Hills"}, {162, "Extreme Hills+ M"},
    {163, "Savanna M"},             {164, "Savanna Plateau M"},
    {165, "Mesa (Bryce)"},          {166, "Mesa Plateau F M"},
    {167, "Mesa Plateau M"},
};
} // namespace detail

/// The biome's name, or nullptr for an id the game does not use.
inline const char* Name(int id) {
    if (id >= 0 && id < detail::BaseCount)
        return detail::Base[id].name;
    for (const auto& v : detail::Variants)
        if (v.id == id)
            return v.name;
    return nullptr;
}

/// The map colour (0xRRGGBB), or 0 for an id the game does not use.
inline std::uint32_t Color(int id) {
    if (id >= 0 && id < detail::BaseCount)
        return detail::Base[id].color;
    if (!Name(id))
        return 0;
    const std::uint32_t base = detail::Base[id - 128].color;
    std::uint32_t lighter = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const std::uint32_t c = base >> shift & 0xFF;
        lighter |= (c + (255 - c) / 4) << shift;
    }
    return lighter;
}

/// One map pixel in the biome view: the biome's colour with 30% of the terrain mixed in, so
/// rivers, coasts and relief stay recognisable. A column without a recorded biome is the terrain
/// greyed out; one with neither stays transparent.
inline void Tint(std::uint8_t* rgba, std::uint8_t id) {
    const std::uint32_t color = id == Unknown ? 0 : Color(id);
    if (!color) {
        const unsigned grey = (rgba[0] * 3u + rgba[1] * 6u + rgba[2]) / 10u * 6u / 10u;
        rgba[0] = rgba[1] = rgba[2] = static_cast<std::uint8_t>(grey);
        return;
    }
    const bool terrain = rgba[3] != 0;
    for (int c = 0; c < 3; ++c) {
        const unsigned biome = color >> (16 - 8 * c) & 0xFF;
        rgba[c] = static_cast<std::uint8_t>(terrain ? (biome * 7u + rgba[c] * 3u) / 10u : biome);
    }
    rgba[3] = 255;
}

} // namespace mc_biomes
