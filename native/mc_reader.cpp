// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mc_reader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "mc_assets.h"
#include "mc_names.h"

namespace mc_reader {
namespace {

// Build 53E6D516A4DA5CD0: Minecraft v1.26.13 (patch version 148).
// vtables: from the binary's RTTI (research/nso_reloc.py).
// ItemStack: the ItemStack destructor resets the ItemStackNetIdVariant index at +0x90 and the
//   variant storage starts at +0x80, so sizeof(ItemStack) = 0x98 and ItemStackBase is 0x80. The
//   ItemStackBase destructor releases mChargedItem at +0x78, the mCanDestroy vector at +0x50 and
//   the mItem handle at +0x08; the remaining fields follow the known class layout
//   (mUserData +0x10, mBlock +0x18, mAuxValue +0x20, mCount +0x22).
// FillingContainer::mItems: the Inventory/FillingContainer destructors walk the vector at
//   +0x140 (begin) .. +0x148 (end) with a 0x98 stride.
// Everything marked "verify" below is a hypothesis to be confirmed against the running game.
constexpr Layout Layouts[] = {
    {
        .build_id = "53E6D516A4DA5CD0C49FCE555994DA196B63E9C1000000000000000000000000",
        // vtable address points (vtable start + 0x10: an object's vptr skips offset_to_top and
        // the typeinfo pointer; the ItemStack constructor stores main+0x10f82638 for example).
        .vt_inventory = 0x10f3a460,
        .vt_player_inventory = 0x10f3ae68,
        .vt_item_stack = 0x10f82638,
        .vt_local_player = 0x10c69d18,
        .inv_items_begin = 0x140,
        .inv_player = 0x158,    // verify
        .pinv_selected = 0x10,  // verify
        .pinv_inventory = 0xB8, // verify
        .stack_size = 0x98,
        .stack_item = 0x08,
        .stack_block = 0x18,
        .stack_aux = 0x20,
        .stack_count = 0x22,
        // Item: anchored statically by the getters mMaxStackSize (ldrb [this+0x88]), mId (ldrh
        // [this+0x8A]) and mMaxDamage (ldrh [this+0x110]); the fields between follow the known
        // declaration order with libc++ sizes (std::string 24, HashedString 40). Verify live.
        .item_icon_name = 0x58,
        .item_icon_frame = 0x48,
        .item_full_name = 0xF0, // the std::string inside HashedString mFullName (hash at 0xE8)
        .item_is_direct = false, // WeakPtr<Item> handle
        .stack_has_vtable = true,
        .player_selected = 0, // via PlayerInventory::mSelected
        // Description id and HUD state: not located in this build, so names come from the full
        // name and the status bars stay hidden.
    },
    // Build D8B7E605E809E80C: the un-updated base game, Minecraft v1.2.12 (2018). ItemInstance
    // has no vtable and is 0x80 bytes (the vector<ItemInstance> destructor at main+0x5b4384 steps
    // by 0x80, frees a vector at elem+0x38 and elem+0x58 and a polymorphic unique_ptr at
    // elem+0x10). FillingContainer::mItems at +0xB0 (destructor), Player* at +0xC8. Field offsets
    // inside ItemInstance, Item and the player are verified live, see docs/NOTES.md.
    {
        .build_id = "D8B7E605E809E80C76FA3BD670FAB5BA00000000000000000000000000000000",
        .vt_inventory = 0x31fad28, // address point
        .vt_player_inventory = 0,  // PlayerInventory: no RTTI in this build
        .vt_item_stack = 0,        // ItemInstance: not polymorphic
        .vt_local_player = 0x2a27358,
        .inv_items_begin = 0xB0,
        .inv_player = 0xC8,
        .pinv_selected = 0,  // unknown
        .pinv_inventory = 0, // unknown
        .stack_size = 0x80,
        .stack_item = 0x00,  // Item* (live: map and log slots)
        .stack_block = 0x08, // BlockLegacy* (null for the map, set for the log)
        .stack_aux = 0x18,   // u16 (log aux 1)
        .stack_count = 0x1A, // u8 (map 1, logs 8)
        .item_icon_name = 0,  // none (+0xD8 is a subclass field); icons come from the name
        .item_icon_frame = 0, // none; the aux value selects the texture variant
        .item_full_name = 0x48, // std::string ("map", "log"); this build has no namespace
        .item_is_direct = true,
        .stack_has_vtable = false,
        .player_selected = 0x1A18, // int in LocalPlayer, follows the hotbar (live diff)
        .item_description_id = 0x30, // std::string ("item.diamond_chestplate", "item.planks")
        .block_description_id = 0x10, // BlockLegacy: std::string ("tile.planks"), name at +0x28
        // Item::getMaxDamage at main+0x12954B4: ldrh w0, [x0, #0x62]; ret.
        // ItemInstance damage getter main+0x129E898 reads the aux at +0x18.
        .item_max_damage = 0x62,
        .damage_in_aux = true,
        // Actor::mAttributes: the map the HUD's hearts and hunger read. Health and hunger are
        // the static Attributes named "minecraft:health" and "minecraft:player.hunger"; writing
        // the current value moved the HUD (live).
        .player_attributes = 0x1140,
        .attr_health = 0x338F040,
        .attr_hunger = 0x3399B88,
        .attr_attribute = 0x10,
        .attr_value = 0x84, // {min, max, value} default at +0x70, current at +0x7C
        // SynchedActorData items 7 (AIR) and 43 (MAX_AIR), both 400 above water.
        .player_entity_data = 0x108,
        .data_air = 7,
        .data_air_max = 43,
        // Actor's armor container: four ItemInstances (helmet, chest, legs, boots); an item put
        // in slot 1 showed in the inventory screen and in the HUD's armor row (live).
        .player_armor = 0x1118,
        .vt_armor_item = 0x2baa6d0,
        .armor_defense = 0xD8, // leather helmet 1, iron chestplate 6, diamond chestplate 8
        // Map (live): walking forward moved the position along (-sin yaw, cos yaw). The player
        // holds two PlayerChunkSources; the larger one is the client's view, a 27 x 27 chunk
        // grid of shared_ptr<LevelChunk> at the default view distance. A sub chunk is 4096 legacy
        // block ids indexed x * 256 + z * 16 + y, then the data nibbles; the heightmap matched
        // the topmost block in 254 of 256 columns (the rest: tall grass above it).
        .player_pos = 0xA0,
        .player_aabb_min_y = 0x17C,
        .player_yaw = 0xD4,
        .player_chunk_views = 0x14C8,
        .view_bounds = 0xD0,
        .view_chunks = 0x100,
        .chunk_pos = 0x28,
        .chunk_subchunks = 0xB8,
        .chunk_heightmap = 0x640,
        .block_registry = 0x33A4BE8,
        .block_map_color = 0x70, // stone 112,112,112; grass 127,178,56; water 64,64,255
        // HUD calls main+0x11EF1A8 to get this vector, then advances by 16 bytes.
        // Getters main+0x1159864/6C/7C read id/ticks/amplifier at +0/+4/+8.
        .player_effects = 0x1148,
        .effect_registry = 0x3391A40,
        .effect_name = 0x20,
    },
};

// libc++ std::unordered_map<std::string, V>: the first node pointer follows the bucket array
// (pointer + bucket count); a node is {next, hash, key (24 bytes), value}.
constexpr u64 MapFirstNode = 0x10;
constexpr u64 MapNodeValue = 0x28;
constexpr int MaxMapNodes = 64;
// DataItem2<short> (SynchedActorData): {vptr, u8 type, u16 id at +0xA, ..., s16 value at +0xE}.
constexpr u64 DataItemType = 0x8, DataItemId = 0xA, DataItemValue = 0xE;
constexpr u8 DataTypeShort = 1;

constexpr u64 PageSize = 0x1000;
constexpr u64 ScanPagesPerTick = 4096; // 16 MiB per tick: a 3 GiB heap in ~3 s
constexpr std::size_t MaxSlotsAccepted = 72;

bool HexEquals(const char* hex, const u8* bytes) {
    if (!hex || !bytes)
        return false;
    for (int i = 0; i < 32; ++i) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", bytes[i]);
        if (std::toupper(static_cast<unsigned char>(hex[i * 2])) != buf[0] ||
            std::toupper(static_cast<unsigned char>(hex[i * 2 + 1])) != buf[1])
            return false;
    }
    return true;
}

template <class T>
bool Read(const EdenDsmodHostApi& host, u64 at, T& out) {
    return dsmod_sdk::ReadGuest<dsmod_sdk::MissingIsMapped::Reject>(host, at, &out, sizeof(T));
}

bool ReadBytes(const EdenDsmodHostApi& host, u64 at, void* out, std::size_t size) {
    return dsmod_sdk::ReadGuest<dsmod_sdk::MissingIsMapped::Reject>(host, at, out, size);
}

} // namespace

bool SupportsBuildHex(const char* build_hex) {
    if (!build_hex)
        return false;
    for (const Layout& l : Layouts) {
        bool same = true;
        for (int i = 0; i < 64 && same; ++i) {
            const char a = build_hex[i], b = l.build_id[i];
            if (a == '\0' || std::toupper(static_cast<unsigned char>(a)) != b)
                same = false;
        }
        if (same)
            return true;
    }
    return false;
}

const Layout* FindLayout(const u8* build_id) {
    for (const Layout& l : Layouts)
        if (HexEquals(l.build_id, build_id))
            return &l;
    return nullptr;
}

bool SupportsBuildId(const u8* build_id) {
    return FindLayout(build_id) != nullptr;
}

Reader::Reader(const EdenDsmodHostApi& host, const char*)
    : layout{FindLayout(host.build_id)}, main_base{host.main_base}, main_size{host.main_size} {}

bool Reader::ReadString(const EdenDsmodHostApi& host, u64 at, std::string& out) const {
    // libc++ std::string (24 bytes). Long mode: byte 0 bit 0 set, {cap, size, data}. Short mode:
    // size = byte0 >> 1, data at +1 (up to 22 bytes).
    u8 raw[24];
    if (!ReadBytes(host, at, raw, sizeof(raw)))
        return false;
    if (raw[0] & 1) {
        u64 size{}, data{};
        std::memcpy(&size, raw + 8, 8);
        std::memcpy(&data, raw + 16, 8);
        if (size > 256 || data == 0)
            return false;
        out.resize(size);
        return size == 0 || ReadBytes(host, data, out.data(), size);
    }
    const u8 size = raw[0] >> 1;
    if (size > 22)
        return false;
    out.assign(reinterpret_cast<const char*>(raw + 1), size);
    return true;
}

// Is `obj` the local player's Inventory? Checks the vector shape, the first and last ItemStack's
// vtable and the Player back-pointer's vtable (LocalPlayer, not a remote Player).
bool Reader::ValidateInventory(const EdenDsmodHostApi& host, u64 obj, u64& items_begin,
                               u64& item_count, u64& player_out) const {
    player_out = 0;
    u64 begin{}, end{}, cap{};
    if (!Read(host, obj + layout->inv_items_begin, begin) ||
        !Read(host, obj + layout->inv_items_begin + 8, end) ||
        !Read(host, obj + layout->inv_items_begin + 16, cap))
        return false;
    if (!begin || end < begin || cap < end || (end - begin) % layout->stack_size != 0)
        return false;
    const u64 count = (end - begin) / layout->stack_size;
    if (count < SlotCount || count > MaxSlotsAccepted)
        return false;
    if (layout->stack_has_vtable) {
        u64 vptr{};
        if (!Read(host, begin, vptr) || vptr != main_base + layout->vt_item_stack)
            return false;
        if (!Read(host, begin + (count - 1) * layout->stack_size, vptr) ||
            vptr != main_base + layout->vt_item_stack)
            return false;
    }
    if (layout->inv_player) {
        u64 player{}, player_vptr{};
        if (!Read(host, obj + layout->inv_player, player) || !player ||
            !Read(host, player, player_vptr) || player_vptr != main_base + layout->vt_local_player)
            return false;
        player_out = player;
    }
    items_begin = begin;
    item_count = count;
    return true;
}

bool Reader::Resolve(const EdenDsmodHostApi& host) {
    // Sliced sweep for objects whose vptr is the Inventory vtable. Minecraft keeps its objects in
    // memory it maps itself with svcMapPhysicalMemory (the alias region, 0x10'00000000..),
    // not in the heap region the host reports, so the sweep walks the alias window in 64 KiB
    // chunks (every mapping the game makes is 64 KiB-aligned) and then the host's heap bounds.
    if (!host.is_mapped || !host.get_read_pointer) {
        diag = "host lacks heap scan services";
        return false;
    }
    constexpr u64 AliasLo = 0x10'00000000ULL, AliasHi = 0x21'00000000ULL, Chunk = 0x10000;
    constexpr u64 ChunksPerTick = 6144; // 384 MiB of address space probed per tick
    const u64 heap_lo = host.get_heap_begin ? host.get_heap_begin(host.userdata) : 0;
    const u64 heap_hi = host.get_heap_end ? host.get_heap_end(host.userdata) : 0;
    const u64 want_inv = main_base + layout->vt_inventory;
    const u64 want_pinv = main_base + layout->vt_player_inventory;
    auto scan_page = [&](u64 page) {
        const u8* const p = host.get_read_pointer(host.userdata, page, PageSize);
        if (!p)
            return;
        for (u64 off = 0; off + 8 <= PageSize; off += 8) {
            u64 word{};
            std::memcpy(&word, p + off, 8);
            if (word == want_inv) {
                u64 begin{}, count{}, who{};
                if (ValidateInventory(host, page + off, begin, count, who)) {
                    inventory = page + off;
                    items_begin = begin;
                    items_count = count;
                    player = who;
                }
            } else if (layout->vt_player_inventory && word == want_pinv) {
                pinv_candidates.push_back(page + off);
                if (pinv_candidates.size() > 64)
                    pinv_candidates.erase(pinv_candidates.begin());
            }
        }
    };
    if (scan_cursor == 0)
        scan_cursor = AliasLo;
    u64 pages_scanned = 0;
    if (scan_cursor < AliasHi) {
        const u64 stop = std::min<u64>(AliasHi, scan_cursor + ChunksPerTick * Chunk);
        for (u64 chunk = scan_cursor; chunk < stop; chunk += Chunk) {
            if (!host.is_mapped(host.userdata, chunk, PageSize))
                continue;
            for (u64 page = chunk; page < chunk + Chunk; page += PageSize) {
                scan_page(page);
                ++pages_scanned;
            }
        }
        scan_cursor = stop;
        if (scan_cursor >= AliasHi)
            scan_cursor = heap_lo && heap_hi > heap_lo ? heap_lo : AliasHi;
    } else if (heap_lo && heap_hi > heap_lo && scan_cursor < heap_hi) {
        if (scan_cursor < heap_lo)
            scan_cursor = heap_lo;
        const u64 stop = std::min<u64>(heap_hi, scan_cursor + ScanPagesPerTick * PageSize);
        for (u64 page = scan_cursor; page < stop; page += PageSize)
            scan_page(page);
        scan_cursor = stop;
    } else {
        scan_cursor = AliasHi; // nothing more to walk this pass
    }
    ++scan_ticks;
    if (inventory) {
        // Match a PlayerInventory whose mInventory unique_ptr points at it (gives mSelected).
        player_inventory = 0;
        for (const u64 cand : pinv_candidates) {
            u64 ptr{};
            if (Read(host, cand + layout->pinv_inventory, ptr) && ptr == inventory) {
                player_inventory = cand;
                break;
            }
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "inventory %llX (%llu slots) pinv %llX after %u ticks",
                      static_cast<unsigned long long>(inventory),
                      static_cast<unsigned long long>(items_count),
                      static_cast<unsigned long long>(player_inventory), scan_ticks);
        if (host.log)
            host.log(host.userdata, EDEN_DSMOD_LOG_INFO, buf);
        diag.clear();
        return true;
    }
    const bool pass_done = scan_cursor >= AliasHi && !(heap_lo && heap_hi > heap_lo && scan_cursor < heap_hi);
    if (pass_done) {
        scan_cursor = AliasLo; // start over: the world may not be loaded yet
        pinv_candidates.clear();
        char buf[96];
        std::snprintf(buf, sizeof(buf), "no inventory yet (pass %u, pinv candidates %zu)",
                      scan_ticks, pinv_candidates.size());
        diag = buf;
    } else {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "scanning %llX (%llu pages this tick)",
                      static_cast<unsigned long long>(scan_cursor),
                      static_cast<unsigned long long>(pages_scanned));
        diag = buf;
    }
    return false;
}

bool Reader::ReadSlots(const EdenDsmodHostApi& host, std::array<Slot, SlotCount>& slots,
                       int& selected) {
    // One bulk read of the 36 ItemStacks, twice, to reject a torn snapshot.
    const std::size_t bytes = SlotCount * layout->stack_size;
    std::vector<u8> a(bytes), b(bytes);
    if (!ReadBytes(host, items_begin, a.data(), bytes) ||
        !ReadBytes(host, items_begin, b.data(), bytes) || a != b) {
        diag = "torn inventory read";
        return false;
    }
    for (std::size_t i = 0; i < SlotCount; ++i) {
        if (!DecodeSlot(host, a.data() + i * layout->stack_size, slots[i])) {
            diag = "invalid inventory slot";
            return false;
        }
    }
    selected = -1;
    if (layout->player_selected && player) {
        s32 sel{};
        if (Read(host, player + layout->player_selected, sel) && sel >= 0 && sel < 9)
            selected = sel;
    } else if (player_inventory) {
        s32 sel{};
        if (Read(host, player_inventory + layout->pinv_selected, sel) && sel >= 0 && sel < 9)
            selected = sel;
    }
    return true;
}

bool Reader::DecodeSlot(const EdenDsmodHostApi& host, const u8* raw, Slot& slot) {
    slot = Slot{};
    if (layout->stack_has_vtable) {
        u64 vptr{};
        std::memcpy(&vptr, raw, 8);
        if (vptr != main_base + layout->vt_item_stack)
            return false;
    }
    slot.count = raw[layout->stack_count];
    if (!slot.count)
        return true;
    std::memcpy(&slot.aux, raw + layout->stack_aux, 2);
    std::memcpy(&slot.item, raw + layout->stack_item, 8);
    if (!slot.item || (!layout->item_is_direct &&
                      (!Read(host, slot.item, slot.item) || !slot.item)))
        return false;
    std::memcpy(&slot.block, raw + layout->stack_block, 8);
    if (layout->item_max_damage && layout->damage_in_aux) {
        u16 maximum{};
        if (Read(host, slot.item + layout->item_max_damage, maximum) && maximum > 0) {
            slot.max_damage = maximum;
            const int damage = static_cast<u16>(slot.aux);
            if (damage <= maximum) {
                slot.remaining = maximum - damage;
                slot.durability = (slot.remaining * 100 + maximum - 1) / maximum;
            }
        }
    }
    const auto printable = [](const std::string& t) {
        return !t.empty() && std::all_of(t.begin(), t.end(), [](unsigned char c) {
            return c > 0x20 && c < 0x7f;
        });
    };
    if (layout->item_full_name) {
        std::string name;
        if (ReadString(host, slot.item + layout->item_full_name, name) && printable(name))
            slot.id = std::move(name);
    }
    // Damage is not a texture/name variant. Worn tools keep their ordinary icon and name.
    const int variant = slot.max_damage > 0 ? 0 : slot.aux;
    if (layout->item_icon_name) {
        std::string icon;
        int frame = 0;
        if (ReadString(host, slot.item + layout->item_icon_name, icon) && printable(icon)) {
            if (layout->item_icon_frame)
                Read(host, slot.item + layout->item_icon_frame, frame);
            else
                frame = variant;
            slot.icon = "module:mc:icon/" + icon + "/" + std::to_string(frame);
        }
    }
    if (slot.icon.empty() && !slot.id.empty())
        slot.icon = "module:mc:item/" + slot.id + "/" + std::to_string(variant);
    return true;
}

void Reader::NameSlot(const EdenDsmodHostApi& host, Slot& slot, const mc_assets::Library* assets) {
    if (!slot.count || !slot.item)
        return;
    // The language file loads on the asset worker; stand-in names are not cached.
    const bool lang = assets && assets->Loaded();
    const s16 variant = slot.max_damage > 0 ? 0 : slot.aux;
    const auto key = std::make_pair(slot.item, variant);
    if (const auto it = name_cache.find(key); it != name_cache.end()) {
        slot.name = it->second;
        return;
    }
    std::string name;
    if (lang && layout->item_description_id) {
        std::string description_id;
        if (!(slot.block && layout->block_description_id &&
              ReadString(host, slot.block + layout->block_description_id, description_id) &&
              description_id.starts_with("tile.")))
            ReadString(host, slot.item + layout->item_description_id, description_id);
        name = assets->ItemName(description_id, variant);
    } else if (lang) {
        name = assets->DisplayName(slot.id);
    }
    if (name.empty())
        name = mc_names::Fallback(slot.id);
    if (lang && name_cache.size() < 4096)
        name_cache.emplace(key, name);
    slot.name = std::move(name);
}

bool Reader::ReadAttribute(const EdenDsmodHostApi& host, u64 attribute, u64& cached, Stat& out) {
    // The instance is found once by walking the player's attribute map for the instance that
    // points at the static Attribute, then re-checked by that pointer on every read.
    const u64 want = main_base + attribute;
    const auto points_at_attribute = [&](u64 instance) {
        u64 a{};
        return instance && Read(host, instance + layout->attr_attribute, a) && a == want;
    };
    if (!points_at_attribute(cached)) {
        cached = 0;
        u64 map{}, node{};
        if (!Read(host, player + layout->player_attributes, map) || !map ||
            !Read(host, map + MapFirstNode, node))
            return false;
        for (int n = 0; node && n < MaxMapNodes; ++n) {
            if (points_at_attribute(node + MapNodeValue)) {
                cached = node + MapNodeValue;
                break;
            }
            if (!Read(host, node, node))
                return false;
        }
        if (!cached)
            return false;
    }
    float value{}, max{};
    if (!Read(host, cached + layout->attr_value, value) ||
        !Read(host, cached + layout->attr_value - 4, max) || !(value >= 0.0f && value <= 1024.0f) ||
        !(max >= 0.0f && max <= 1024.0f))
        return false;
    // The HUD rounds up: 0.5 health left still shows half a heart.
    out.value = static_cast<int>(std::ceil(value));
    out.max = static_cast<int>(std::ceil(max));
    out.ok = true;
    return true;
}

Reader::PlayerStats Reader::ReadPlayer(const EdenDsmodHostApi& host) {
    PlayerStats stats;
    if (!player)
        return stats;
    if (layout->player_attributes) {
        ReadAttribute(host, layout->attr_health, health_instance, stats.health);
        ReadAttribute(host, layout->attr_hunger, hunger_instance, stats.hunger);
    }

    if (layout->player_entity_data) {
        u64 begin{}, end{};
        const auto read_short = [&](u32 id, int& out) {
            u64 item{};
            u8 raw[16];
            if (!Read(host, begin + id * 8ULL, item) || !item || !ReadBytes(host, item, raw, sizeof(raw)))
                return false;
            u16 got_id{};
            s16 value{};
            std::memcpy(&got_id, raw + DataItemId, 2);
            std::memcpy(&value, raw + DataItemValue, 2);
            if (raw[DataItemType] != DataTypeShort || got_id != id)
                return false;
            out = value;
            return true;
        };
        const u32 last = std::max(layout->data_air, layout->data_air_max);
        if (Read(host, player + layout->player_entity_data, begin) &&
            Read(host, player + layout->player_entity_data + 8, end) && begin && end > begin &&
            (end - begin) / 8 > last && read_short(layout->data_air, stats.air.value) &&
            read_short(layout->data_air_max, stats.air.max) && stats.air.max > 0 &&
            stats.air.value <= stats.air.max)
            stats.air.ok = true;
    }

    if (layout->player_armor) {
        // Read the vector and stacks twice; do not display a mix of old and new equipment.
        std::array<u64, 3> vector{}, after{};
        const std::size_t bytes = ArmorSlotCount * layout->stack_size;
        if (ReadBytes(host, player + layout->player_armor, vector.data(), sizeof(vector)) &&
            vector[0] && vector[1] >= vector[0] && vector[2] >= vector[1] &&
            vector[1] - vector[0] == bytes) {
            std::vector<u8> a(bytes), b(bytes);
            if (ReadBytes(host, vector[0], a.data(), bytes) &&
                ReadBytes(host, vector[0], b.data(), bytes) && a == b &&
                ReadBytes(host, player + layout->player_armor, after.data(), sizeof(after)) &&
                vector == after) {
                bool ok = true, defense_ok = layout->vt_armor_item && layout->armor_defense;
                int total = 0;
                for (std::size_t i = 0; i < ArmorSlotCount && ok; ++i) {
                    Slot& slot = stats.equipment[i];
                    ok = DecodeSlot(host, a.data() + i * layout->stack_size, slot);
                    if (!ok || !slot.count)
                        continue;
                    u64 vptr{};
                    s32 defense{};
                    if (defense_ok && !Read(host, slot.item, vptr)) {
                        defense_ok = false;
                    } else if (defense_ok && vptr == main_base + layout->vt_armor_item) {
                        defense_ok = Read(host, slot.item + layout->armor_defense, defense) &&
                                     defense >= 0 && defense <= 20;
                        if (defense_ok)
                            total += defense;
                    }
                }
                stats.equipment_ok = ok;
                if (ok && defense_ok)
                    stats.armor = {true, std::min(total, 20), 20};
            }
        }
    }
    return stats;
}

void Reader::PublishPlayer(const EdenDsmodHostApi& host, const PlayerStats& stats) {
    const auto publish = [&](const char* name, const Stat& stat) {
        char key[40];
        std::snprintf(key, sizeof(key), "player.%s", name);
        host.publish_i64(host.userdata, key, stat.ok ? stat.value : 0);
        std::snprintf(key, sizeof(key), "player.%s_max", name);
        host.publish_i64(host.userdata, key, stat.ok ? stat.max : 0);
        std::snprintf(key, sizeof(key), "player.%s_ok", name);
        host.publish_i64(host.userdata, key, stat.ok ? 1 : 0);
    };
    publish("health", stats.health);
    publish("hunger", stats.hunger);
    publish("armor", stats.armor);
    publish("air", stats.air);
    host.publish_i64(host.userdata, "equip.ready", stats.equipment_ok ? 1 : 0);
    for (std::size_t i = 0; i < ArmorSlotCount; ++i) {
        char prefix[32];
        std::snprintf(prefix, sizeof(prefix), "equip.slot%zu", i);
        PublishSlot(host, prefix, stats.equipment[i], stats.equipment_ok);
    }

    // Icon states the way the HUD draws them: icon i covers points 2i+1 (half) and 2i+2 (full).
    const auto half_icons = [](int points, int i) {
        return 2 * i + 1 < points ? 2 : 2 * i + 1 == points ? 1 : 0;
    };
    // Bubbles (the HUD's formula): ceil((air - 2) * 10 / max) full ones, then the popping ones
    // up to ceil(air * 10 / max).
    int bubbles_full = 0, bubbles_shown = 0;
    if (stats.air.ok) {
        const double max = stats.air.max;
        bubbles_full = std::max(0, static_cast<int>(std::ceil((stats.air.value - 2) * 10.0 / max)));
        bubbles_shown = std::max(0, static_cast<int>(std::ceil(stats.air.value * 10.0 / max)));
    }
    char key[32];
    for (int i = 0; i < static_cast<int>(HudIcons); ++i) {
        const int from_right = static_cast<int>(HudIcons) - 1 - i;
        std::snprintf(key, sizeof(key), "hud.heart%d", i);
        host.publish_i64(host.userdata, key, half_icons(stats.health.value, i));
        std::snprintf(key, sizeof(key), "hud.armor%d", i);
        host.publish_i64(host.userdata, key, half_icons(stats.armor.value, i));
        std::snprintf(key, sizeof(key), "hud.hunger%d", i);
        host.publish_i64(host.userdata, key, half_icons(stats.hunger.value, from_right));
        std::snprintf(key, sizeof(key), "hud.bubble%d", i);
        host.publish_i64(host.userdata, key,
                         from_right < bubbles_full ? 2 : from_right < bubbles_shown ? 1 : 0);
    }
}

void Reader::PublishSlot(const EdenDsmodHostApi& host, const char* prefix,
                         const Slot& slot, bool ready) {
    const bool occupied = ready && slot.count;
    const bool durability_ok = occupied && slot.durability >= 0;
    const auto integer = [&](const char* field, s64 value) {
        const std::string key = std::string(prefix) + "." + field;
        host.publish_i64(host.userdata, key.c_str(), value);
    };
    const auto text = [&](const char* field, const std::string& value) {
        const std::string key = std::string(prefix) + "." + field;
        host.publish_text(host.userdata, key.c_str(), occupied ? value.c_str() : "");
    };
    integer("count", occupied ? slot.count : 0);
    text("icon", slot.icon);
    text("id", slot.id);
    text("name", slot.name);
    integer("dur_ok", durability_ok ? 1 : 0);
    integer("dur", durability_ok ? slot.durability : -1);
    integer("remaining", durability_ok ? slot.remaining : 0);
    integer("max_damage", durability_ok ? slot.max_damage : 0);
}

void Reader::Publish(const EdenDsmodHostApi& host, const std::array<Slot, SlotCount>& slots,
                     int selected, bool ready, const char* why) {
    host.publish_i64(host.userdata, "inv.ready", ready ? 1 : 0);
    host.publish_i64(host.userdata, "inv.selected", selected);
    host.publish_text(host.userdata, "inv.diag", why ? why : "");
    const bool selected_ok = ready && selected >= 0 && selected < 9;
    host.publish_i64(host.userdata, "inv.selected_ok", selected_ok ? 1 : 0);
    for (std::size_t i = 0; i < SlotCount; ++i) {
        char prefix[32];
        std::snprintf(prefix, sizeof(prefix), "inv.slot%zu", i);
        PublishSlot(host, prefix, slots[i], ready);
    }
    const Slot* active = selected_ok ? &slots[static_cast<std::size_t>(selected)] : nullptr;
    host.publish_text(host.userdata, "inv.slot_sel_name",
                      active && active->count ? active->name.c_str() : "");
    std::string durability;
    bool pickaxe_low = false;
    if (active && active->count && active->durability >= 0) {
        durability = std::to_string(active->remaining) + " / " +
                     std::to_string(active->max_damage) + " (" +
                     std::to_string(active->durability) + "%)";
        std::string id = active->id;
        if (id.starts_with("minecraft:"))
            id.erase(0, 10);
        else if (id.starts_with("item."))
            id.erase(0, 5);
        pickaxe_low = id.ends_with("_pickaxe") &&
                      active->remaining * 10 <= active->max_damage;
    }
    host.publish_text(host.userdata, "inv.tool_durability", durability.c_str());
    host.publish_i64(host.userdata, "inv.pickaxe_low", pickaxe_low ? 1 : 0);
    host.publish_address(host.userdata, "inv.object", inventory);
    host.publish_address(host.userdata, "inv.items", items_begin);
    host.publish_address(host.userdata, "inv.pinv", player_inventory);
}

Reader::Effects Reader::ReadEffects(const EdenDsmodHostApi& host) const {
    Effects result;
    if (!layout || !layout->player_effects || !player)
        return result;
    std::array<u64, 3> header{}, after{};
    if (!ReadBytes(host, player + layout->player_effects, header.data(), sizeof(header)))
        return result;
    const auto [begin, end, cap] = header;
    constexpr u64 stride = 16;
    if (end < begin || cap < end || (end - begin) % stride || (cap - begin) % stride ||
        (end - begin) / stride > EffectSlots || (cap - begin) / stride > 2 * EffectSlots ||
        (!begin && (end || cap)) || begin % 4)
        return result;
    // Entity::addEffect grows to id + 1 entries. Its vector helper can reserve twice the
    // previous capacity (main+0x11EFF20); unused capacity is not part of the snapshot.
    const auto bytes = static_cast<std::size_t>(end - begin);
    std::array<u8, EffectSlots * stride> a{}, b{};
    if ((bytes && (!ReadBytes(host, begin, a.data(), bytes) ||
                   !ReadBytes(host, begin, b.data(), bytes) || a != b)) ||
        !ReadBytes(host, player + layout->player_effects, after.data(), sizeof(after)) ||
        header != after)
        return result;
    for (std::size_t i = 0; i < bytes / stride; ++i) {
        s32 id{}, ticks{}, amplifier{};
        const u8* raw = a.data() + i * stride;
        std::memcpy(&id, raw, 4);
        std::memcpy(&ticks, raw + 4, 4);
        std::memcpy(&amplifier, raw + 8, 4);
        // Empty entries use id zero; the HUD skips them. Expired entries can linger until
        // the game removes them. Active ids must match their registry-indexed vector slot.
        if (id == 0)
            continue;
        if (id != static_cast<int>(i) || ticks < 0 || amplifier < 0 || amplifier > 255 ||
            raw[12] > 1 || raw[13] > 1 || raw[14] > 1)
            return {};
        if (ticks > 0)
            result.active.push_back({id, ticks, amplifier});
    }
    result.ready = true;
    return result;
}

void Reader::PublishEffects(const EdenDsmodHostApi& host, const Effects& effects,
                            const mc_assets::Library* assets) {
    effects_count = effects.ready ? effects.active.size() : 0;
    effects_page = std::min(effects_page, effects_count ? (effects_count - 1) / EffectRows : 0);
    const auto first = effects_page * EffectRows;
    host.publish_i64(host.userdata, "effects.ready", effects.ready);
    host.publish_i64(host.userdata, "effects.count", effects_count);
    host.publish_i64(host.userdata, "effects.can_prev", effects.ready && effects_page > 0);
    host.publish_i64(host.userdata, "effects.can_next", effects.ready && first + EffectRows < effects_count);
    std::string status;
    if (!layout || !layout->player_effects)
        status = "Effects unavailable for this version";
    else if (!effects.ready)
        status = "Waiting for effect data";
    else if (!effects_count)
        status = "No active effects";
    else
        status = std::to_string(effects_count) + (effects_count == 1 ? " active effect" : " active effects");
    host.publish_text(host.userdata, "effects.status", status.c_str());
    const std::string page_text = effects_count ?
        std::to_string(first + 1) + "-" + std::to_string(std::min(first + EffectRows, effects_count)) +
            " of " + std::to_string(effects_count) : "";
    host.publish_text(host.userdata, "effects.page", page_text.c_str());
    // Fallbacks keep names readable while the resource-pack worker is loading.
    static constexpr const char* names[EffectSlots] = {
        "", "Speed", "Slowness", "Haste", "Mining Fatigue", "Strength", "Instant Health",
        "Instant Damage", "Jump Boost", "Nausea", "Regeneration", "Resistance",
        "Fire Resistance", "Water Breathing", "Invisibility", "Blindness", "Night Vision",
        "Hunger", "Weakness", "Poison", "Wither", "Health Boost", "Absorption",
        "Saturation", "Levitation", "Fatal Poison"};
    static constexpr const char* roman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    for (std::size_t row = 0; row < EffectRows; ++row) {
        const bool visible = first + row < effects_count;
        const Effect effect = visible ? effects.active[first + row] : Effect{};
        std::string name, strength, time;
        int seconds = 0;
        if (visible) {
            name = names[effect.id];
            u64 definition{};
            s32 definition_id{};
            std::string key;
            if (assets && assets->Loaded() && layout->effect_registry &&
                Read(host, main_base + layout->effect_registry + effect.id * 8, definition) &&
                definition && Read(host, definition + 8, definition_id) && definition_id == effect.id &&
                ReadString(host, definition + layout->effect_name, key)) {
                const std::string translated = assets->LanguageText(key);
                if (!translated.empty())
                    name = translated;
            }
            strength = effect.amplifier < 10 ? roman[effect.amplifier] : std::to_string(effect.amplifier + 1);
            // Derive time from game ticks, so pauses and lag never advance a wall-clock timer.
            seconds = effect.ticks / 20;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
            time = buf;
        }
        char prefix[32], key[64];
        std::snprintf(prefix, sizeof(prefix), "effects.row%zu", row);
        const auto number = [&](const char* field, s64 value) {
            std::snprintf(key, sizeof(key), "%s.%s", prefix, field);
            host.publish_i64(host.userdata, key, value);
        };
        const auto text = [&](const char* field, const std::string& value) {
            std::snprintf(key, sizeof(key), "%s.%s", prefix, field);
            host.publish_text(host.userdata, key, value.c_str());
        };
        number("visible", visible);
        number("id", effect.id);
        number("level", visible ? effect.amplifier + 1 : 0);
        number("ticks", effect.ticks);
        number("seconds", seconds);
        text("name", name);
        text("strength", strength);
        text("time", time);
    }
}

void Reader::Sample(const EdenDsmodHostApi& host, const mc_assets::Library* assets) {
    const auto t0 = std::chrono::steady_clock::now();
    std::array<Slot, SlotCount> slots{};
    int selected = 0;
    bool ready = false;
    if (!layout) {
        diag = "unsupported build";
    } else if (!inventory && !Resolve(host)) {
        // diag set by Resolve
    } else if (!ReadSlots(host, slots, selected)) {
        // Re-validate on the next tick; a world change frees the old inventory.
        u64 begin{}, count{}, who{};
        if (!ValidateInventory(host, inventory, begin, count, who)) {
            inventory = 0;
            player_inventory = 0;
            player = 0;
            health_instance = 0;
            hunger_instance = 0;
            name_cache.clear();
            diag = "inventory went away; rescanning";
        }
    } else {
        ready = true;
        diag.clear();
        for (Slot& slot : slots)
            NameSlot(host, slot, assets);
    }
    snapshot = {};
    snapshot.ready = ready;
    snapshot.selected = ready ? selected : -1;
    if (ready)
        for (std::size_t i = 0; i < SlotCount; ++i)
            snapshot.slots[i] = {slots[i].count, slots[i].aux, slots[i].id,
                                 slots[i].name, slots[i].icon};
    Publish(host, slots, selected, ready, diag.c_str());
    PlayerStats stats = ready ? ReadPlayer(host) : PlayerStats{};
    if (stats.equipment_ok)
        for (Slot& slot : stats.equipment)
            NameSlot(host, slot, assets);
    PublishPlayer(host, stats);
    PublishEffects(host, ready ? ReadEffects(host) : Effects{}, assets);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    host.publish_i64(host.userdata, "mc.sample_us", static_cast<s64>(us));
    host.publish_i64(host.userdata, "mc.alive", 1);
}

bool Reader::OnAction(const char* action, s64) {
    if (action && std::strcmp(action, "effects_previous") == 0) {
        if (effects_page > 0)
            --effects_page;
        return true;
    }
    if (action && std::strcmp(action, "effects_next") == 0) {
        if ((effects_page + 1) * EffectRows < effects_count)
            ++effects_page;
        return true;
    }
    if (action && std::strcmp(action, "rescan") == 0) {
        snapshot = {};
        effects_page = effects_count = 0;
        inventory = 0;
        player_inventory = 0;
        player = 0;
        name_cache.clear();
        health_instance = 0;
        hunger_instance = 0;
        scan_cursor = 0;
        return true;
    }
    return false;
}

} // namespace mc_reader
