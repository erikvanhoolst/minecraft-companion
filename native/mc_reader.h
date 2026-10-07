// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Minecraft (Bedrock Edition, Switch, title 0100D71004694000): live inventory reader.
//
// Published values (all every sample; see package/dualscreen/manifest.json):
//   inv.ready            1 while a consistent inventory snapshot was read this tick
//   inv.selected         selected hotbar slot 0..8
//   inv.slot<i>.count    item count in slot i (0..35; 0..8 hotbar), 0 = empty
//   inv.slot<i>.icon     "module:mc:icon/<icon_name>/<frame>" or "module:mc:item/<name>/<aux>" or ""
//   inv.slot<i>.id       item identifier, e.g. "log", "tile.cobblestone", "minecraft:diamond_sword"
//   inv.slot<i>.name     the name the game shows for the item, e.g. "Oak Wood"
//   inv.slot<i>.dur_ok   1 when durability is readable for this damageable item
//   inv.slot<i>.dur      remaining durability percent (rounded up), -1 = unavailable
//   inv.slot<i>.remaining, inv.slot<i>.max_damage   remaining uses and maximum durability
//   inv.slot_sel_name    inv.slot<selected>.name
//   inv.selected_ok      1 when the selected hotbar index was read reliably
//   inv.tool_durability  selected item's "remaining / maximum (percent%)", or ""
//   inv.pickaxe_low      selected pickaxe has at most 10% durability remaining
//   equip.ready         consistent snapshot of the four worn armor slots
//   equip.slot<i>.*     same fields as inventory slots; 0 helmet, 1 chestplate, 2 legs, 3 boots
//   inv.diag             short text explaining why inv.ready is 0
//   player.health, player.health_max, player.hunger, player.armor, player.air, player.air_max
//                        the local player's state as the game's HUD shows it (half icons:
//                        health 20 = ten hearts, air 0..air_max)
//   player.<stat>_ok     1 while that value was read this tick (health, hunger, armor, air)
//   hud.heart<i>, hud.armor<i>, hud.hunger<i>, hud.bubble<i>   (i = 0..9, left to right)
//                        per-icon state like the game's HUD: 0 empty, 1 half, 2 full; a bubble
//                        is 0 gone, 1 popping, 2 full. Hunger and bubbles fill from the right.
//   mc.sample_us         cost of the last sample in microseconds

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/mods/dsmod_module_abi.h"
#include "core/mods/modules/dsmod_module_sdk.h"

namespace mc_assets {
class Library;
}

namespace mc_reader {

using namespace dsmod_sdk::int_types;

/// Everything that depends on the exact game build. Offsets are main-relative for code/data,
/// structure-relative for fields. One entry per verified build; 0 means "unknown for this build"
/// and switches the feature off.
struct Layout {
    const char* build_id; // 64 upper-case hex characters

    // vtables (main-relative), from the binary's RTTI (research/nso_reloc.py)
    u64 vt_inventory;        // Inventory
    u64 vt_player_inventory; // PlayerInventory (primary)
    u64 vt_item_stack;       // ItemStack
    u64 vt_local_player;     // LocalPlayer

    // Inventory : FillingContainer : Container
    u32 inv_items_begin; // std::vector<ItemStack>::begin inside Inventory
    u32 inv_player;      // Player* back-pointer inside FillingContainer

    // PlayerInventory
    u32 pinv_selected;  // int mSelected
    u32 pinv_inventory; // std::unique_ptr<Inventory> mInventory

    // ItemStack
    u32 stack_size;  // sizeof(ItemStack)
    u32 stack_item;  // WeakPtr<Item> (pointer to the shared counter holding Item*)
    u32 stack_block; // Block const*
    u32 stack_aux;   // short mAuxValue
    u32 stack_count; // uchar mCount

    // Item
    u32 item_icon_name;  // std::string mIconName
    u32 item_icon_frame; // int mIconFrame
    u32 item_full_name;  // HashedString mFullName (hash, then std::string)
    bool item_is_direct; // true: the slot holds a raw Item*; false: a WeakPtr handle -> Item*
    bool stack_has_vtable; // ItemStack is polymorphic (vt_item_stack checked)
    u32 player_selected;   // int selected hotbar slot inside the LocalPlayer object (0: use pinv)
    u32 item_description_id{};  // std::string mDescriptionId ("item.apple", "item.log")
    u32 block_description_id{}; // std::string mDescriptionId in the stack's block ("tile.log"):
                                // a block item is named after its block, as the game does
    u32 item_max_damage{};      // u16 Item::mMaxDamage; 0 = unknown
    bool damage_in_aux{};      // this build stores damage in the stack's aux (not NBT)

    // The local player's HUD state
    u32 player_attributes{};  // BaseAttributeMap* inside the player: an unordered_map<
                              // std::string, AttributeInstance> pointing at static Attributes
    u64 attr_health{};        // the static Attribute objects (main-relative)
    u64 attr_hunger{};
    u32 attr_attribute{};     // Attribute const* inside AttributeInstance
    u32 attr_value{};         // float current value inside AttributeInstance (current max at -4)
    u32 player_entity_data{}; // SynchedActorData: std::vector<std::unique_ptr<DataItem>>
    u32 data_air{};           // DataItem ids of the air supply and its maximum (shorts)
    u32 data_air_max{};
    u32 player_armor{};       // std::vector<ItemStack> of the four armor slots
    u64 vt_armor_item{};      // ArmorItem (address point)
    u32 armor_defense{};      // int mDefense inside ArmorItem

    // The map tab (mc_map.cpp): the player's place and the chunks the client holds around it
    u32 player_pos{};          // Vec3 position (eye height; feet = AABB min y)
    u32 player_aabb_min_y{};   // float: the bottom of the player's bounding box
    u32 player_yaw{};          // float degrees: 0 faces +Z (south), 90 -X (west), 180 north
    u32 player_chunk_views{};  // two PlayerChunkSource* side by side; the larger grid is used
    u32 view_bounds{};         // PlayerChunkSource: ints min x, y, z, max x, y, z, size x, y, z
    u32 view_chunks{};         // std::vector<std::shared_ptr<LevelChunk>>, row-major by z
    u32 chunk_pos{};           // LevelChunk: ChunkPos (int x, int z)
    u32 chunk_subchunks{};     // 16 pointers to SubChunk block data, bottom up
    u32 chunk_heightmap{};     // u16[256], index z * 16 + x: one above the top light-blocking block
    u64 block_registry{};      // Block* mBlocks[256] by legacy id (main-relative)
    u32 block_map_color{};     // Color (4 floats) inside Block: the colour the game's maps use
};

constexpr std::size_t SlotCount = 36;
constexpr std::size_t ArmorSlotCount = 4;
constexpr std::size_t HudIcons = 10;

bool SupportsBuildHex(const char* build_hex);
bool SupportsBuildId(const u8* build_id);
const Layout* FindLayout(const u8* build_id);

/// A detached copy of the last sample; ready=false never represents an empty inventory.
struct InventoryItem {
    int count{};
    int aux{};
    std::string id, name, icon;
};
struct InventorySnapshot {
    bool ready{};
    int selected{-1};
    std::array<InventoryItem, SlotCount> slots{};
};

class Reader {
public:
    Reader(const EdenDsmodHostApi& host, const char* config_json);

    /// `assets` supplies the display names (from the game's language file) once it has loaded.
    void Sample(const EdenDsmodHostApi& host, const mc_assets::Library* assets = nullptr);
    bool OnAction(const char* action, s64 argument);

    const InventorySnapshot& InventoryState() const { return snapshot; }

    const Layout* ActiveLayout() const { return layout; }
    u64 Inventory() const { return inventory; }
    u64 ItemsBegin() const { return items_begin; }
    u64 PlayerInventory() const { return player_inventory; }
    u64 Player() const { return player; }

private:
    struct Slot {
        u8 count{};
        s16 aux{};
        u64 item{};
        u64 block{};
        std::string id;
        std::string name;
        std::string icon;
        int max_damage{};
        int remaining{};
        int durability{-1};
    };
    struct Stat {
        bool ok{};
        int value{};
        int max{};
    };
    struct PlayerStats {
        Stat health, hunger, armor, air;
        bool equipment_ok{};
        std::array<Slot, ArmorSlotCount> equipment{};
    };

    bool Resolve(const EdenDsmodHostApi& host);
    bool ValidateInventory(const EdenDsmodHostApi& host, u64 obj, u64& items_begin,
                           u64& item_count, u64& player_out) const;
    bool ReadSlots(const EdenDsmodHostApi& host, std::array<Slot, SlotCount>& slots,
                   int& selected);
    bool DecodeSlot(const EdenDsmodHostApi& host, const u8* raw, Slot& slot);
    bool ReadString(const EdenDsmodHostApi& host, u64 at, std::string& out) const;
    void NameSlot(const EdenDsmodHostApi& host, Slot& slot, const mc_assets::Library* assets);
    PlayerStats ReadPlayer(const EdenDsmodHostApi& host);
    bool ReadAttribute(const EdenDsmodHostApi& host, u64 attribute, u64& cached, Stat& out);
    void Publish(const EdenDsmodHostApi& host, const std::array<Slot, SlotCount>& slots,
                 int selected, bool ready, const char* diag);
    void PublishPlayer(const EdenDsmodHostApi& host, const PlayerStats& stats);
    void PublishSlot(const EdenDsmodHostApi& host, const char* prefix, const Slot& slot, bool ready);

    InventorySnapshot snapshot;
    const Layout* layout{};
    u64 main_base{};
    u64 main_size{};
    u64 inventory{};        // resolved Inventory object (heap), 0 while unknown
    u64 items_begin{};      // its ItemStack array
    u64 items_count{};
    u64 player_inventory{}; // resolved PlayerInventory object, 0 while unknown
    u64 player{};           // the LocalPlayer the inventory belongs to
    u64 health_instance{};  // the player's AttributeInstances, found by walking the map once
    u64 hunger_instance{};
    std::vector<u64> pinv_candidates; // PlayerInventory objects seen during the sweep
    u64 scan_cursor{};      // heap scan position for the vtable search
    u32 scan_ticks{};
    std::string diag{"starting"};
    /// Display names by (Item*, aux); items are registry singletons, so this stays small.
    std::map<std::pair<u64, s16>, std::string> name_cache;
};

} // namespace mc_reader
