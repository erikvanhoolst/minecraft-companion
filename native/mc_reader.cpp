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
        const u8* s = a.data() + i * layout->stack_size;
        if (layout->stack_has_vtable) {
            u64 vptr{};
            std::memcpy(&vptr, s, 8);
            if (vptr != main_base + layout->vt_item_stack) {
                diag = "slot vtable mismatch";
                return false;
            }
        }
        Slot& out = slots[i];
        out.count = s[layout->stack_count];
        std::memcpy(&out.aux, s + layout->stack_aux, 2);
        u64 item_handle{};
        std::memcpy(&item_handle, s + layout->stack_item, 8);
        out.item = 0;
        out.block = 0;
        out.id.clear();
        out.name.clear();
        out.icon.clear();
        if (out.count == 0 || item_handle == 0)
            continue;
        // WeakPtr<Item>: handle -> SharedCounter { Item* ptr; ... } (verify); or a raw Item*
        u64 item = item_handle;
        if (!layout->item_is_direct && (!Read(host, item_handle, item) || !item))
            continue;
        out.item = item;
        std::memcpy(&out.block, s + layout->stack_block, 8);
        const auto printable = [](const std::string& t) {
            return !t.empty() && std::all_of(t.begin(), t.end(), [](unsigned char c) {
                return c > 0x20 && c < 0x7f;
            });
        };
        if (layout->item_full_name) {
            std::string name;
            if (ReadString(host, item + layout->item_full_name, name) && printable(name))
                out.id = name;
        }
        if (layout->item_icon_name) {
            std::string icon;
            int frame = 0;
            if (ReadString(host, item + layout->item_icon_name, icon) && printable(icon)) {
                if (layout->item_icon_frame)
                    Read(host, item + layout->item_icon_frame, frame);
                else
                    frame = out.aux; // legacy builds index the texture array by the aux value
                out.icon = "module:mc:icon/" + icon + "/" + std::to_string(frame);
            }
        }
        if (out.icon.empty() && !out.id.empty())
            out.icon = "module:mc:item/" + out.id + "/" + std::to_string(out.aux);
    }
    selected = 0;
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

void Reader::NameSlots(const EdenDsmodHostApi& host, std::array<Slot, SlotCount>& slots,
                       const mc_assets::Library* assets) {
    // The language file loads with the icons (on the asset worker); until then the names are
    // stand-ins and are not cached.
    const bool lang = assets && assets->Loaded();
    for (Slot& slot : slots) {
        if (!slot.count || !slot.item)
            continue;
        const auto key = std::make_pair(slot.item, slot.aux);
        if (const auto it = name_cache.find(key); it != name_cache.end()) {
            slot.name = it->second;
            continue;
        }
        std::string name;
        if (lang && layout->item_description_id) {
            // A block item takes its block's name ("tile.planks" + aux 5 = Dark Oak Wood
            // Planks); its own description id is a generic "item.planks".
            std::string description_id;
            if (!(slot.block && layout->block_description_id &&
                  ReadString(host, slot.block + layout->block_description_id, description_id) &&
                  description_id.starts_with("tile.")))
                ReadString(host, slot.item + layout->item_description_id, description_id);
            name = assets->ItemName(description_id, slot.aux);
        } else if (lang) {
            name = assets->DisplayName(slot.id);
        }
        if (name.empty())
            name = mc_names::Fallback(slot.id);
        if (lang && name_cache.size() < 4096)
            name_cache.emplace(key, name);
        slot.name = std::move(name);
    }
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
    if (!player || !layout->player_attributes)
        return stats;
    ReadAttribute(host, layout->attr_health, health_instance, stats.health);
    ReadAttribute(host, layout->attr_hunger, hunger_instance, stats.hunger);

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
        // Armor points are the sum of the worn ArmorItems' defense, as the HUD's armor row.
        u64 begin{}, end{};
        if (Read(host, player + layout->player_armor, begin) &&
            Read(host, player + layout->player_armor + 8, end) && begin &&
            end - begin == 4ULL * layout->stack_size) {
            std::vector<u8> raw(4 * layout->stack_size);
            if (ReadBytes(host, begin, raw.data(), raw.size())) {
                int total = 0;
                bool ok = true;
                for (int i = 0; i < 4 && ok; ++i) {
                    const u8* s = raw.data() + i * layout->stack_size;
                    u64 item{};
                    std::memcpy(&item, s + layout->stack_item, 8);
                    if (!s[layout->stack_count] || !item)
                        continue;
                    if (!layout->item_is_direct && !Read(host, item, item))
                        continue;
                    u64 vptr{};
                    s32 defense{};
                    if (!Read(host, item, vptr)) {
                        ok = false;
                    } else if (vptr == main_base + layout->vt_armor_item) {
                        ok = Read(host, item + layout->armor_defense, defense) && defense >= 0 &&
                             defense <= 20;
                        total += defense;
                    }
                }
                if (ok) {
                    stats.armor = {true, std::min(total, 20), 20};
                }
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

void Reader::Publish(const EdenDsmodHostApi& host, const std::array<Slot, SlotCount>& slots,
                     int selected, bool ready, const char* why) {
    host.publish_i64(host.userdata, "inv.ready", ready ? 1 : 0);
    host.publish_i64(host.userdata, "inv.selected", selected);
    host.publish_text(host.userdata, "inv.diag", why ? why : "");
    char key[32];
    for (std::size_t i = 0; i < SlotCount; ++i) {
        const Slot& s = slots[i];
        std::snprintf(key, sizeof(key), "inv.slot%zu.count", i);
        host.publish_i64(host.userdata, key, ready ? s.count : 0);
        std::snprintf(key, sizeof(key), "inv.slot%zu.icon", i);
        host.publish_text(host.userdata, key, ready && s.count ? s.icon.c_str() : "");
        std::snprintf(key, sizeof(key), "inv.slot%zu.id", i);
        host.publish_text(host.userdata, key, ready && s.count ? s.id.c_str() : "");
        std::snprintf(key, sizeof(key), "inv.slot%zu.name", i);
        host.publish_text(host.userdata, key, ready && s.count ? s.name.c_str() : "");
    }
    if (ready && selected >= 0 && static_cast<std::size_t>(selected) < SlotCount &&
        slots[static_cast<std::size_t>(selected)].count)
        host.publish_text(host.userdata, "inv.slot_sel_name",
                          slots[static_cast<std::size_t>(selected)].name.c_str());
    else
        host.publish_text(host.userdata, "inv.slot_sel_name", "");
    host.publish_address(host.userdata, "inv.object", inventory);
    host.publish_address(host.userdata, "inv.items", items_begin);
    host.publish_address(host.userdata, "inv.pinv", player_inventory);
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
        NameSlots(host, slots, assets);
    }
    Publish(host, slots, selected, ready, diag.c_str());
    PublishPlayer(host, ready ? ReadPlayer(host) : PlayerStats{});
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    host.publish_i64(host.userdata, "mc.sample_us", static_cast<s64>(us));
    host.publish_i64(host.userdata, "mc.alive", 1);
}

bool Reader::OnAction(const char* action, s64) {
    if (action && std::strcmp(action, "rescan") == 0) {
        inventory = 0;
        player_inventory = 0;
        health_instance = 0;
        hunger_instance = 0;
        scan_cursor = 0;
        return true;
    }
    return false;
}

} // namespace mc_reader
