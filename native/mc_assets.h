// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Minecraft (Bedrock, Switch) asset-free art: item and block icons decoded from the player's own
// romfs resource packs. Nothing here ships with the package.
//
// The game stacks its vanilla resource packs (vanilla_base, vanilla, vanilla_1.14 ... newest);
// a later pack overrides an earlier one. Item art is keyed by Item::mIconName plus
// Item::mIconFrame (an index into the entry's texture array) in textures/item_texture.json. A
// block item has no icon name: blocks.json names the block's face textures, which
// textures/terrain_texture.json maps to files. Texture paths carry no extension (.png or .tga).

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "mc_zip.h"

namespace mc_assets {

struct Image {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba; // tightly packed RGBA8
};

/// Reads whole files from the host's romfs (any registered source prefix is passed through).
std::optional<std::vector<std::uint8_t>> ReadRomfsFile(const EdenDsmodHostApi& host,
                                                       std::string_view path);

/// nlohmann::json over the game's lenient JSON (line comments, trailing commas). Discarded on
/// error (`is_discarded()`).
nlohmann::json ParseLenientJson(std::string_view text);

/// Decodes PNG/TGA bytes to RGBA8.
std::optional<Image> DecodeImage(const std::uint8_t* bytes, std::size_t size);

/// Glyph metrics of a game font, in the form the host's font extension takes.
struct Font {
    std::uint32_t line_height{};
    std::uint32_t first_codepoint{};
    std::vector<EdenDsmodFontGlyph> glyphs;
};

class Library {
public:
    /// Builds the pack index once; later calls are no-ops. Safe to call from any thread.
    /// Returns false when no vanilla pack could be read (romfs not ready yet).
    bool EnsureLoaded(const EdenDsmodHostApi& host);
    /// Lock-free, so the sample thread never waits for an index build or an icon decode.
    bool Loaded() const;

    /// Pack folder names, oldest first (vanilla_base, vanilla, vanilla_1.14, ...).
    std::vector<std::string> Packs() const;

    /// Romfs path (with extension) of an item icon, or nullopt.
    std::optional<std::string> ItemIconPath(const EdenDsmodHostApi& host, std::string_view icon_name,
                                            int frame) const;
    /// Romfs path of a block's side face (falls back to any face), or nullopt. `variant` (the
    /// stack's aux value) selects the texture in a variant list, e.g. log_side: oak, spruce, ...
    std::optional<std::string> BlockIconPath(const EdenDsmodHostApi& host,
                                             std::string_view block_name, int variant = 0) const;
    /// English display name from texts/en_US.lang ("item.apple.name", "tile.stone.stone.name"),
    /// or an empty string. Lock-free; empty until loaded.
    std::string DisplayName(std::string_view full_name) const;
    /// Exact language key, including potion effect names ("potion.moveSpeed").
    std::string LanguageText(std::string_view key) const;
    /// The name the game shows for a stack: its item's description id ("tile.log") with the
    /// variant the aux value selects (mc_names.h), or an empty string. Lock-free; empty until
    /// loaded.
    std::string ItemName(std::string_view description_id, int aux) const;

    /// Resolves a module image key (without the "module:mc:" prefix) and decodes it:
    ///   icon/<icon_name>/<frame>      an item by icon name and frame
    ///   block/<block_name>            a block item by its block name
    ///   item/<full_name>/<frame>      an item by full name (icon name assumed = short name)
    ///   ui/<name>                     a HUD sprite, textures/ui/<name> (heart, armor_full, ...)
    ///   font/<name>                   a font sheet, font/<name> (default8: the game's ASCII font)
    ///   map/<name>                    a map texture, textures/map/<name> (map_background)
    ///   mapicon/<cell>                one 8x8 marker of textures/map/map_icons (0: the player)
    std::optional<Image> LoadIcon(const EdenDsmodHostApi& host, std::string_view key) const;

    /// Metrics for the font sheet font/<name> (16 x 16 cells, codepoint = row * 16 + column),
    /// measured the way the game measures it: a glyph is as wide as its last column with ink,
    /// plus one pixel of spacing. The atlas is the module image "font/<name>".
    std::optional<Font> LoadFont(const EdenDsmodHostApi& host, std::string_view name) const;

private:
    struct Index {
        std::vector<std::string> packs; // oldest first
        std::vector<mc_zip::Archive> zips; // zip-packed packs (1.2.x), searched after `packs`
        std::map<std::string, nlohmann::json> item_textures;    // icon name -> "textures" value
        std::map<std::string, nlohmann::json> terrain_textures; // terrain key -> "textures" value
        std::map<std::string, nlohmann::json> blocks;           // block name -> entry
        std::map<std::string, std::string> lang;                // en_US.lang
    };
    /// The texture path for `frame`; `overlay` receives an "overlay_color" ("#rrggbb") if any.
    static std::optional<std::string> PickTexture(const nlohmann::json& textures, int frame,
                                                  std::string* overlay = nullptr);
    std::optional<std::string> FindTextureFile(const EdenDsmodHostApi& host,
                                               std::string_view relative) const;
    /// Reads a texture located by FindTextureFile: a romfs file, or "zip:<archive>#<member>".
    std::optional<std::vector<std::uint8_t>> ReadTexture(const EdenDsmodHostApi& host,
                                                         const std::string& path) const;
    void BuildIndex(const EdenDsmodHostApi& host, Index& out) const;
    /// Texture-key candidates for a legacy (1.2.x) item registry name, best first.
    static std::vector<std::string> LegacyNames(std::string_view raw);
    /// textures/items/<name> or textures/blocks/<name> in the newest pack that has it.
    std::optional<std::string> TextureByBasename(const EdenDsmodHostApi& host,
                                                 std::string_view name) const;

    mutable std::mutex mutex;
    std::atomic<bool> loaded{false}; // set once; `index` is immutable from then on
    Index index;
};

} // namespace mc_assets
