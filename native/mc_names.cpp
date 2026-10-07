// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mc_names.h"

#include <array>
#include <cctype>

namespace mc_names {
namespace {

using Names = std::array<std::string_view, 9>; // "" = no variant name for that value

// Colour names by data value (wool order, 0 = white). The language file spells light blue
// "lightBlue" for most blocks and "light_blue" for glass; both are tried.
constexpr std::array<std::string_view, 16> Colors{
    "white", "orange", "magenta", "lightBlue", "yellow", "lime",  "pink", "gray",
    "silver", "cyan",  "purple",  "blue",      "brown",  "green", "red",  "black"};

constexpr std::array<std::string_view, 6> Woods{"oak",    "spruce", "birch",
                                                "jungle", "acacia", "big_oak"};

struct Variant {
    std::string_view description_id;
    std::string_view key_prefix; // language-file prefix when it differs from the id ("" = same)
    int mask;                    // aux bits that select the variant
    int shift;
    Names names; // by (aux & mask) >> shift
};

// Blocks and items whose name depends on the aux value (Bedrock 1.2 data values; the language
// file holds every "<id>.<variant>.name" listed here).
const Variant Variants[] = {
    {"tile.stone", "", 7, 0,
     {"stone", "granite", "graniteSmooth", "diorite", "dioriteSmooth", "andesite", "andesiteSmooth"}},
    {"tile.dirt", "", 1, 0, {"default", "coarse"}},
    {"tile.sand", "", 1, 0, {"default", "red"}},
    {"tile.sandstone", "", 3, 0, {"default", "chiseled", "smooth"}},
    {"tile.red_sandstone", "", 3, 0, {"default", "chiseled", "smooth"}},
    {"tile.log", "", 3, 0, {"oak", "spruce", "birch", "jungle"}},
    {"tile.log2", "tile.log", 1, 0, {"acacia", "big_oak"}},
    {"tile.leaves", "", 3, 0, {"oak", "spruce", "birch", "jungle"}},
    {"tile.leaves2", "", 1, 0, {"acacia", "big_oak"}},
    {"tile.stonebrick", "", 7, 0, {"default", "mossy", "cracked", "chiseled", "smooth"}},
    {"tile.stone_slab", "", 7, 0,
     {"stone", "sand", "wood", "cobble", "brick", "smoothStoneBrick", "quartz", "nether_brick"}},
    {"tile.double_stone_slab", "", 7, 0,
     {"stone", "sand", "wood", "cobble", "brick", "smoothStoneBrick", "quartz", "nether_brick"}},
    {"tile.stone_slab2", "", 7, 0, {"red_sandstone", "purpur"}},
    {"tile.double_stone_slab2", "", 7, 0, {"red_sandstone", "purpur"}},
    {"tile.monster_egg", "", 7, 0,
     {"stone", "cobble", "brick", "mossybrick", "crackedbrick", "chiseledbrick"}},
    {"tile.red_flower", "", 15, 0,
     {"poppy", "blueOrchid", "allium", "houstonia", "tulipRed", "tulipOrange", "tulipWhite",
      "tulipPink", "oxeyeDaisy"}},
    {"tile.yellow_flower", "", 0, 0, {"dandelion"}},
    {"tile.double_plant", "", 7, 0, {"sunflower", "syringa", "grass", "fern", "rose", "paeonia"}},
    {"tile.tallgrass", "", 3, 0, {"", "grass", "fern"}},
    {"tile.quartz_block", "", 3, 0, {"default", "chiseled", "lines"}},
    {"tile.prismarine", "", 3, 0, {"rough", "dark", "bricks"}},
    {"tile.purpur_block", "", 3, 0, {"default", "chiseled", "lines"}},
    {"tile.cobblestone_wall", "", 1, 0, {"normal", "mossy"}},
    {"tile.sponge", "", 1, 0, {"dry", "wet"}},
    // Anvil items carry the damage as 0..2 (creative inventory, live); the placed block keeps
    // it in bits 2-3, handled in DisplayName.
    {"tile.anvil", "", 3, 0, {"intact", "slightlyDamaged", "veryDamaged"}},
    {"item.skull", "", 7, 0, {"skeleton", "wither", "zombie", "char", "creeper", "dragon"}},
};

constexpr std::string_view WoodIds[] = {"tile.planks", "tile.sapling", "tile.wooden_slab",
                                        "tile.double_wooden_slab", "item.boat"};
constexpr std::string_view ColorIds[] = {"tile.wool",     "tile.carpet",
                                         "tile.stained_hardened_clay", "tile.concrete",
                                         "tile.concretePowder", "tile.stained_glass",
                                         "tile.stained_glass_pane", "item.bed"};
// Dyes and banners count their colour the other way round (0 = black).
constexpr std::string_view DyeIds[] = {"item.dye", "item.banner"};

// Bedrock entity type ids of the spawn eggs (aux value) -> language-file entity name.
struct Entity {
    int id;
    std::string_view name;
};
constexpr Entity Entities[] = {
    {10, "chicken"},       {11, "cow"},           {12, "pig"},
    {13, "sheep"},         {14, "wolf"},          {15, "villager"},
    {16, "mooshroom"},     {17, "squid"},         {18, "rabbit"},
    {19, "bat"},           {20, "iron_golem"},    {21, "snow_golem"},
    {22, "ocelot"},        {23, "horse"},         {24, "donkey"},
    {25, "mule"},          {26, "skeleton_horse"}, {27, "zombie_horse"},
    {28, "polar_bear"},    {29, "llama"},         {30, "parrot"},
    {32, "zombie"},        {33, "creeper"},       {34, "skeleton"},
    {35, "spider"},        {36, "zombie_pigman"}, {37, "slime"},
    {38, "enderman"},      {39, "silverfish"},    {40, "cave_spider"},
    {41, "ghast"},         {42, "magma_cube"},    {43, "blaze"},
    {44, "zombie_villager"}, {45, "witch"},       {46, "stray"},
    {47, "husk"},          {48, "wither_skeleton"}, {49, "guardian"},
    {50, "elder_guardian"}, {51, "npc"},          {54, "shulker"},
    {55, "endermite"},     {57, "vindicator"},    {104, "evocation_illager"},
    {105, "vex"},
};

// Potion types by aux value -> the effect's language id ("" = a base potion: 0 water, 1-2
// mundane, 3 thick, 4 awkward). Long and strong forms share the effect's name: the game calls
// both healing potions "Potion of Healing" and shows the level only in the effect line.
constexpr std::string_view Potions[] = {
    "",               "",               "",               "",               "",
    "nightVision",    "nightVision",    "invisibility",   "invisibility",   "jump",
    "jump",           "jump",           "fireResistance", "fireResistance", "moveSpeed",
    "moveSpeed",      "moveSpeed",      "moveSlowdown",   "moveSlowdown",   "waterBreathing",
    "waterBreathing", "heal",           "heal",           "harm",           "harm",
    "poison",         "poison",         "poison",         "regeneration",   "regeneration",
    "regeneration",   "damageBoost",    "damageBoost",    "damageBoost",    "weakness",
    "weakness",       "wither",
};

template <class Range>
bool Contains(const Range& range, std::string_view id) {
    for (const std::string_view r : range)
        if (r == id)
            return true;
    return false;
}

std::string Join(std::string_view a, std::string_view b, std::string_view c = {}) {
    std::string s;
    s.reserve(a.size() + b.size() + c.size());
    s.append(a).append(b).append(c);
    return s;
}

const std::string* Get(const Lookup& lang, const std::string& key) {
    return lang ? lang(key) : nullptr;
}

/// "<prefix>.<variant>.name", or nullptr.
const std::string* GetVariant(const Lookup& lang, std::string_view prefix, std::string_view variant) {
    if (variant.empty())
        return nullptr;
    return Get(lang, Join(prefix, ".", variant) + ".name");
}

std::string PotionName(std::string_view description_id, int aux, const Lookup& lang) {
    // "Splash" / "Lingering" in front of the drinkable potion's name.
    std::string prefix;
    if (description_id.find("splash") != std::string_view::npos) {
        if (const auto* p = Get(lang, "potion.prefix.grenade"))
            prefix = *p + " ";
    } else if (description_id.find("lingering") != std::string_view::npos) {
        if (const auto* p = Get(lang, "potion.prefix.linger"))
            prefix = *p + " ";
    }
    if (aux < 0 || static_cast<std::size_t>(aux) >= std::size(Potions))
        return {};
    if (const std::string_view effect = Potions[aux]; !effect.empty()) {
        const auto* name = Get(lang, Join("potion.", effect, ".postfix"));
        return name ? prefix + *name : std::string{};
    }
    if (aux == 0) {
        const auto* water = Get(lang, "item.emptyPotion.name");
        return water ? prefix + *water : std::string{};
    }
    const auto* base = Get(lang, "item.potion.name");
    const char* kind = aux == 1   ? "potion.prefix.mundane"
                       : aux == 2 ? "potion.prefix.mundane.extended"
                       : aux == 3 ? "potion.prefix.thick"
                                  : "potion.prefix.awkward";
    const auto* adjective = Get(lang, kind);
    if (!base || !adjective)
        return {};
    return prefix + *adjective + " " + *base;
}

} // namespace

std::string DisplayName(std::string_view description_id, int aux, const Lookup& lang) {
    if (description_id.empty())
        return {};
    const std::string id{description_id};
    const auto variant = [&](std::string_view suffix) -> std::string {
        const auto* s = GetVariant(lang, id, suffix);
        return s ? *s : std::string{};
    };
    std::string name;
    if (Contains(ColorIds, id) && aux >= 0) {
        const std::string_view color = Colors[aux & 15];
        name = variant(color);
        if (name.empty() && color == "lightBlue")
            name = variant("light_blue");
    } else if (Contains(DyeIds, id) && aux >= 0) {
        name = variant(Colors[15 - (aux & 15)]);
    } else if (Contains(WoodIds, id) && aux >= 0 && (aux & 7) < 6) {
        name = variant(Woods[aux & 7]);
    } else if (id == "item.spawn_egg") {
        std::string_view entity = "unknown";
        for (const Entity& e : Entities)
            if (e.id == aux)
                entity = e.name;
        name = variant(Join("entity.", entity));
    } else if (id == "item.coal" && aux == 1) {
        if (const auto* s = Get(lang, "item.charcoal.name"))
            name = *s;
    } else if (id == "item.bucket" && aux != 0) {
        const char* key = aux == 1 ? "item.milk.name" : aux == 8 ? "item.bucketWater.name"
                          : aux == 10                            ? "item.bucketLava.name"
                                                                 : nullptr;
        if (const auto* s = key ? Get(lang, key) : nullptr)
            name = *s;
    } else if (description_id.starts_with("item.record_")) {
        // Every disc is "Music Disc"; the track ("C418 - cat") is only in its tooltip.
        if (const auto* s = Get(lang, "item.record.name"))
            name = *s;
    } else if (id == "item.arrow" && aux > 0) {
        // Tipped arrows: aux - 1 is the potion type.
        const int potion = aux - 1;
        if (static_cast<std::size_t>(potion) < std::size(Potions)) {
            const std::string_view effect = Potions[potion];
            if (const auto* s = Get(lang, Join("tipped_arrow.effect.", effect.empty() ? "empty" : effect)))
                name = *s;
        }
    } else if (description_id.find("potion") != std::string_view::npos &&
               description_id.starts_with("item.") && id != "item.emptyPotion") {
        name = PotionName(description_id, aux, lang);
    } else {
        const int value = id == "tile.anvil" && aux >= 4 ? (aux >> 2) & 3 : aux;
        for (const Variant& v : Variants) {
            if (v.description_id != id)
                continue;
            const int index = v.mask ? (value & v.mask) >> v.shift : 0;
            if (value >= 0 && static_cast<std::size_t>(index) < v.names.size()) {
                const std::string_view suffix = v.names[static_cast<std::size_t>(index)];
                if (const auto* s = GetVariant(lang, v.key_prefix.empty() ? v.description_id : v.key_prefix, suffix))
                    name = *s;
            }
            break;
        }
    }
    if (name.empty())
        if (const auto* s = Get(lang, id + ".name"))
            name = *s;
    return name;
}

std::string Fallback(std::string_view identifier) {
    std::string_view s = identifier;
    if (const auto colon = s.find(':'); colon != std::string_view::npos)
        s.remove_prefix(colon + 1);
    for (const std::string_view prefix : {"tile.", "item."})
        if (s.starts_with(prefix))
            s.remove_prefix(prefix.size());
    std::string out;
    bool word_start = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '_' || c == '.') {
            if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
            word_start = true;
            continue;
        }
        // camelCase: a capital starts a new word
        if (std::isupper(static_cast<unsigned char>(c)) && !word_start && !out.empty())
            out.push_back(' ');
        out.push_back(word_start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        word_start = false;
    }
    return out;
}

std::string EnchantmentName(int id, int level, const Lookup& lang) {
    // 1.2.12's registration at main+0x126F800..0x12708CC. Unlike modern Bedrock,
    // Frost Walker and the curses follow fishing; Efficiency is 15, Unbreaking 17.
    constexpr const char* keys[]{"protect.all", "protect.fire", "protect.fall", "protect.explosion",
        "protect.projectile", "oxygen", "waterWorker", "thorns", "waterWalker", "damage.all",
        "damage.undead", "damage.arthropods", "knockback", "fire", "lootBonus", "digging",
        "untouching", "durability", "lootBonusDigger", "arrowDamage", "arrowKnockback",
        "arrowFire", "arrowInfinite", "lootBonusFishing", "fishingSpeed", "frostwalker",
        "mending", "curse.binding", "curse.vanishing"};
    constexpr const char* roman[]{"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    if (id < 0 || level <= 0)
        return {};
    std::string name;
    if (id < static_cast<int>(std::size(keys)))
        if (const auto* translated = Get(lang, std::string("enchantment.") + keys[id]))
            name = *translated;
    if (name.empty())
        name = "Enchantment " + std::to_string(id);
    return name + " " + (level <= 10 ? std::string(roman[level]) : std::to_string(level));
}

} // namespace mc_names
