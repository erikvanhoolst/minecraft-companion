// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the public reader against guest-memory fixtures; no game files required.
#include "mc_reader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace mc_reader;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

struct Fixture {
    static constexpr u64 Base = 0x10'00000000ULL;
    const u64 inventory = Base + 0x100, player = Base + 0x2000;
    const u64 stacks = Base + 0x4000, armor = Base + 0x6000, item = Base + 0x7000;
    std::vector<u8> memory = std::vector<u8>(0x10000);
    std::map<std::string, s64> ints;
    std::map<std::string, std::string> texts;
    EdenDsmodHostApi host{};
    const Layout* layout{};
    u64 unreadable{}, torn{};
    int torn_reads{};

    explicit Fixture(bool modern = false) {
        const char* build = modern ?
            "53E6D516A4DA5CD0C49FCE555994DA196B63E9C1000000000000000000000000" :
            "D8B7E605E809E80C76FA3BD670FAB5BA00000000000000000000000000000000";
        for (int i = 0; i < 32; ++i) {
            unsigned byte{};
            std::sscanf(build + i * 2, "%2x", &byte);
            host.build_id[i] = byte;
        }
        host.main_base = 0x80000000;
        host.main_size = 0x12000000;
        host.userdata = this;
        host.is_mapped = [](void* p, u64 at, u64 size) -> EdenDsmodBool {
            auto& f = *static_cast<Fixture*>(p);
            return at >= Base && at - Base <= f.memory.size() &&
                   size <= f.memory.size() - (at - Base) &&
                   !(f.unreadable && at <= f.unreadable && f.unreadable - at < size);
        };
        host.read_memory = [](void* p, u64 at, void* out, std::size_t size) -> EdenDsmodBool {
            auto& f = *static_cast<Fixture*>(p);
            std::memcpy(out, f.memory.data() + (at - Base), size);
            if (at == f.torn && ++f.torn_reads % 2 == 0)
                static_cast<u8*>(out)[0] ^= 1;
            return true;
        };
        host.get_read_pointer = [](void* p, u64 at, std::size_t size) -> const u8* {
            auto& f = *static_cast<Fixture*>(p);
            return f.host.is_mapped(p, at, size) ? f.memory.data() + (at - Base) : nullptr;
        };
        host.publish_i64 = [](void* p, const char* key, s64 value) {
            static_cast<Fixture*>(p)->ints[key] = value;
        };
        host.publish_text = [](void* p, const char* key, const char* value) {
            static_cast<Fixture*>(p)->texts[key] = value;
        };
        host.publish_address = [](void*, const char*, u64) {};
        layout = FindLayout(host.build_id);
        Put(inventory, host.main_base + layout->vt_inventory);
        Put(inventory + layout->inv_player, player);
        Put(player, host.main_base + layout->vt_local_player);
        Vector(inventory + layout->inv_items_begin, stacks, SlotCount);
        if (layout->player_armor)
            Vector(player + layout->player_armor, armor, ArmorSlotCount);
        for (std::size_t i = 0; i < SlotCount; ++i)
            if (layout->stack_has_vtable)
                Put(stacks + i * layout->stack_size, host.main_base + layout->vt_item_stack);
        if (layout->player_selected)
            Put<s32>(player + layout->player_selected, 0);
        Item(item, "diamond_pickaxe", 1561);
    }

    template <class T> void Put(u64 at, T value) {
        std::memcpy(memory.data() + (at - Base), &value, sizeof(value));
    }
    void Vector(u64 at, u64 begin, std::size_t count) {
        Put(at, begin);
        Put(at + 8, begin + count * layout->stack_size);
        Put(at + 16, begin + count * layout->stack_size);
    }
    void Item(u64 at, const std::string& name, u16 maximum, s32 defense = 0) {
        Put(at, host.main_base + layout->vt_armor_item);
        auto* raw = memory.data() + at - Base + layout->item_full_name;
        raw[0] = name.size() * 2;
        std::memcpy(raw + 1, name.c_str(), name.size() + 1);
        if (layout->item_max_damage)
            Put(at + layout->item_max_damage, maximum);
        if (layout->armor_defense)
            Put(at + layout->armor_defense, defense);
    }
    void Stack(u64 at, u64 item_at, u16 damage, u8 count = 1) {
        if (layout->stack_has_vtable)
            Put(at, host.main_base + layout->vt_item_stack);
        // Modern stacks reference a shared counter rather than the item directly.
        const u64 handle = Base + 0x9000;
        Put(handle, item_at);
        Put(at + layout->stack_item, layout->item_is_direct ? item_at : handle);
        Put(at + layout->stack_aux, damage);
        Put(at + layout->stack_count, count);
    }
};

int main() {
    Fixture f;
    Reader reader(f.host, nullptr);
    f.Stack(f.stacks, f.item, 0);
    reader.Sample(f.host);
    Check(reader.InventoryState().ready && reader.InventoryState().selected == 0 &&
          reader.InventoryState().slots[0].id == "diamond_pickaxe" &&
          reader.InventoryState().slots[0].count == 1,
          "public snapshot has readable identity and count");
    Check(f.ints["inv.ready"] == 1 && f.ints["equip.ready"] == 1, "snapshot ready");
    Check(f.ints["inv.slot0.dur"] == 100 && f.ints["inv.slot0.remaining"] == 1561,
          "new pickaxe is fully durable");
    Check(f.ints["inv.pickaxe_low"] == 0, "new pickaxe has no warning");

    f.Stack(f.stacks, f.item, 1404); // 157/1561 is slightly above 10%.
    reader.Sample(f.host);
    Check(f.ints["inv.pickaxe_low"] == 0 && f.ints["inv.slot0.dur"] == 11,
          "warning does not round down across the threshold");
    f.Stack(f.stacks, f.item, 1405);
    reader.Sample(f.host);
    Check(reader.InventoryState().slots[0].aux == 1405, "public snapshot preserves aux");
    Check(f.ints["inv.pickaxe_low"] == 1 && f.ints["inv.slot0.dur"] == 10,
          "worn selected pickaxe warns at 10%");
    Check(f.texts["inv.tool_durability"] == "156 / 1561 (10%)", "selected durability text");
    Check(f.texts["inv.slot0.icon"] == "module:mc:item/diamond_pickaxe/0",
          "damage does not select a texture variant");
    f.Item(f.item, "iron_pickaxe", 250);
    f.Stack(f.stacks, f.item, 225);
    reader.Sample(f.host);
    Check(f.ints["inv.pickaxe_low"] == 1 && f.ints["inv.slot0.remaining"] == 25,
          "exactly 10% also warns");
    f.Item(f.item, "diamond_pickaxe", 1561);
    f.Stack(f.stacks, f.item, 1405);

    f.Put<s32>(f.player + f.layout->player_selected, 1);
    reader.Sample(f.host);
    Check(f.ints["inv.pickaxe_low"] == 0 && f.texts["inv.tool_durability"].empty(),
          "switching to an empty slot clears the warning");
    f.Put<s32>(f.player + f.layout->player_selected, 0);
    f.unreadable = f.player + f.layout->player_selected;
    reader.Sample(f.host);
    Check(f.ints["inv.selected_ok"] == 0 && f.ints["inv.pickaxe_low"] == 0,
          "unknown selection cannot warn for slot zero");
    f.unreadable = 0;
    f.Put<s32>(f.player + f.layout->player_selected, 9);
    reader.Sample(f.host);
    Check(f.ints["inv.selected_ok"] == 0 && f.ints["inv.pickaxe_low"] == 0,
          "out-of-range selection cannot warn");
    f.Put<s32>(f.player + f.layout->player_selected, 0);

    f.Item(f.item, "diamond_axe", 1561);
    reader.Sample(f.host);
    Check(f.ints["inv.slot0.dur_ok"] == 1 && f.ints["inv.pickaxe_low"] == 0,
          "other tools display durability without a pickaxe warning");
    f.Item(f.item, "diamond_pickaxe", 1561);
    f.Stack(f.stacks, f.item, 1561);
    reader.Sample(f.host);
    Check(f.ints["inv.slot0.dur"] == 0 && f.ints["inv.pickaxe_low"] == 1,
          "zero durability remains visible");
    f.Stack(f.stacks, f.item, 1562);
    reader.Sample(f.host);
    Check(f.ints["inv.slot0.dur"] == -1 && f.ints["inv.pickaxe_low"] == 0,
          "invalid damage hides durability and clears warning");
    f.unreadable = f.item + f.layout->item_max_damage;
    reader.Sample(f.host);
    Check(f.ints["inv.slot0.dur_ok"] == 0, "unreadable maximum is unavailable");
    f.unreadable = 0;

    f.Item(f.item, "log", 0);
    f.Stack(f.stacks, f.item, 2, 8);
    reader.Sample(f.host);
    Check(f.ints["inv.slot0.dur"] == -1 && f.texts["inv.slot0.icon"] == "module:mc:item/log/2",
          "non-damageable item retains its aux texture variant");

    const char* names[] = {"iron_helmet", "iron_chestplate", "iron_leggings", "iron_boots"};
    const int defenses[] = {2, 6, 5, 2};
    for (std::size_t i = 0; i < ArmorSlotCount; ++i) {
        const u64 at = f.item + (i + 1) * 0x200;
        f.Item(at, names[i], 200, defenses[i]);
        f.Stack(f.armor + i * f.layout->stack_size, at, 100);
    }
    reader.Sample(f.host);
    for (std::size_t i = 0; i < ArmorSlotCount; ++i) {
        const std::string key = "equip.slot" + std::to_string(i);
        Check(f.texts[key + ".id"] == names[i] && f.ints[key + ".dur"] == 50,
              "armor order and individual wear");
    }
    Check(f.ints["player.armor"] == 15, "HUD still adds equipped defense");
    f.Stack(f.armor, 0, 0, 0);
    reader.Sample(f.host);
    Check(f.ints["equip.slot0.count"] == 0 && f.texts["equip.slot0.icon"].empty() &&
          f.ints["equip.slot0.dur"] == -1, "unequipping clears all equipment fields");
    f.torn = f.armor;
    reader.Sample(f.host);
    Check(f.ints["equip.ready"] == 0 && f.texts["equip.slot1.id"].empty(),
          "torn armor snapshot clears equipment");
    f.torn = 0;
    f.Put(f.player + f.layout->player_armor + 8, f.armor - 1);
    reader.Sample(f.host);
    Check(f.ints["equip.ready"] == 0, "invalid equipment vector is rejected");

    f.Item(f.item, "diamond_pickaxe", 1561);
    f.Stack(f.stacks, f.item, 1500);
    reader.Sample(f.host);
    f.torn = f.stacks;
    f.torn_reads = 0;
    reader.Sample(f.host);
    Check(f.ints["inv.ready"] == 0 && f.ints["inv.pickaxe_low"] == 0 &&
          f.texts["inv.tool_durability"].empty(), "torn inventory clears active tool state");
    Check(!reader.InventoryState().ready && reader.InventoryState().slots[0].id.empty(),
          "torn read clears public inventory snapshot");
    f.torn = 0;
    f.Vector(f.player + f.layout->player_armor, f.armor, ArmorSlotCount);
    reader.Sample(f.host);
    f.unreadable = f.stacks;
    f.Put<u64>(f.inventory + f.layout->inv_player, 0);
    reader.Sample(f.host);
    Check(reader.Player() == 0 && f.ints["equip.ready"] == 0 &&
          f.ints["inv.pickaxe_low"] == 0 && f.texts["equip.slot1.icon"].empty(),
          "world unload clears equipment and active warning");

    Fixture modern(true);
    Reader modern_reader(modern.host, nullptr);
    modern.Stack(modern.stacks, modern.item, 1500);
    modern_reader.Sample(modern.host);
    Check(modern.ints["inv.ready"] == 1 && modern.ints["equip.ready"] == 0 &&
          modern.ints["inv.slot0.dur"] == -1 && modern.ints["inv.pickaxe_low"] == 0,
          "unlocated modern equipment/damage fields stay unavailable");
    modern_reader.OnAction("rescan", 0);
    Check(!modern_reader.InventoryState().ready && modern_reader.InventoryState().slots[0].id.empty(),
          "rescan invalidates public inventory snapshot");
    std::puts("Reader equipment and durability checks passed");
}
