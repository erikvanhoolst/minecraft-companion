// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Item display names as the game shows them (hotbar popup, inventory tooltips), built the way
// Minecraft (Bedrock) builds them: the item's description id ("item.apple", "tile.log"), a
// variant suffix chosen by the stack's aux value for the items whose name depends on it ("tile.log"
// + aux 2 -> "tile.log.birch"), then ".name" looked up in the game's own language file.

#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace mc_names {

/// Returns the translation for a language-file key, or nullptr when the file has no such key.
using Lookup = std::function<const std::string*(const std::string& key)>;

/// The name the game shows for an item stack, or "" when the language file has no entry for it.
std::string DisplayName(std::string_view description_id, int aux, const Lookup& lang);

/// Readable stand-in when no translation exists: "tile.stone_slab" -> "Stone Slab".
std::string Fallback(std::string_view identifier);

/// Minecraft 1.2.12 enchantment IDs, with the game's translated name and level.
std::string EnchantmentName(int id, int level, const Lookup& lang = {});

} // namespace mc_names
