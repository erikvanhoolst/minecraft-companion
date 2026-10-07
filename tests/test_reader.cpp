// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the public reader against guest-memory fixtures; no game files required.
#include "mc_reader.h"
#include "mc_names.h"

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
    u64 replace_stack_on_read{};
    u64 next_tag = Base + 0xa000;

    u64 AllocateTag(u64 vtable, std::size_t size = 0x80) {
        const u64 at = next_tag;
        next_tag += size;
        if (next_tag - Base > memory.size())
            memory.resize(next_tag - Base + 0x1000);
        Put(at, host.main_base + vtable);
        return at;
    }
    void String(u64 at, const std::string& value) {
        if (value.size() <= 22) {
            Put<u8>(at, value.size() * 2);
            std::memcpy(memory.data() + at - Base + 1, value.c_str(), value.size() + 1);
        } else {
            const u64 data = AllocateTag(0, value.size() + 1);
            Put<u64>(at, (value.size() + 1) | 1);
            Put<u64>(at + 8, value.size());
            Put(at + 16, data);
            std::memcpy(memory.data() + data - Base, value.c_str(), value.size() + 1);
        }
    }
    u64 Compound(std::map<std::string, u64> tags) {
        const u64 compound = AllocateTag(layout->vt_compound_tag);
        u64 previous{};
        for (const auto& [key, tag] : tags) {
            const u64 node = AllocateTag(0);
            if (previous)
                Put(previous + 8, node);
            else {
                Put(compound + 8, node);
                Put(compound + 0x10, node);
            }
            String(node + 0x20, key);
            Put(node + 0x38, tag);
            previous = node;
        }
        Put<u64>(compound + 0x18, tags.size());
        return compound;
    }
    u64 Short(s16 value) {
        const u64 tag = AllocateTag(layout->vt_short_tag);
        Put(tag + 8, value);
        return tag;
    }
    u64 Metadata(const std::string& name, int enchantments = 3) {
        const u64 name_tag = AllocateTag(layout->vt_string_tag);
        String(name_tag + 8, name);
        const u64 display = Compound({{"Name", name_tag}});
        const u64 list = AllocateTag(layout->vt_list_tag);
        const u64 elements = AllocateTag(0, 0x100);
        for (int i = 0; i < enchantments; ++i)
            Put(elements + i * 8, Compound({{"id", Short(17 + i)}, {"lvl", Short(3)}}));
        Put(list + 8, elements);
        Put(list + 0x10, elements + enchantments * 8);
        Put(list + 0x18, elements + 0x100);
        Put<u8>(list + 0x20, 10);
        return Compound({{"display", display}, {"ench", list}});
    }

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
            if (at == f.replace_stack_on_read) {
                f.Put<u8>(f.stacks + f.layout->stack_count, 0);
                f.replace_stack_on_read = 0;
            }
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

void TestEffects() {
    Fixture f;
    Reader reader(f.host, nullptr);
    const u64 effects = Fixture::Base + 0xA000;
    const u64 header = f.player + f.layout->player_effects;
    const auto vector = [&](std::size_t count) {
        f.Put(header, effects);
        f.Put(header + 8, effects + count * 16);
        f.Put(header + 16, effects + count * 16);
    };
    const auto effect = [&](int id, s32 ticks, s32 amplifier) {
        f.Put<s32>(effects + id * 16, id);
        f.Put<s32>(effects + id * 16 + 4, ticks);
        f.Put<s32>(effects + id * 16 + 8, amplifier);
        f.Put<u8>(effects + id * 16 + 14, 1);
    };
    reader.Sample(f.host);
    Check(f.ints["effects.ready"] == 1 && f.ints["effects.count"] == 0 &&
          f.texts["effects.status"] == "No active effects", "empty effects are readable");
    vector(EffectSlots);
    effect(1, 3600, 1);
    effect(16, 1200, 0);
    f.Put(header + 16, effects + 2 * EffectSlots * 16);
    reader.Sample(f.host);
    Check(f.ints["effects.count"] == 2 && f.texts["effects.row0.name"] == "Speed" &&
          f.texts["effects.row0.strength"] == "II" && f.texts["effects.row0.time"] == "3:00" &&
          f.texts["effects.row1.name"] == "Night Vision" && f.ints["effects.row1.level"] == 1,
          "multiple effects include exact strength and tick duration");
    Check(f.ints["effects.ready"] == 1, "spare vector capacity after growth remains readable");
    f.Put(header + 16, effects + (2 * EffectSlots + 1) * 16);
    reader.Sample(f.host);
    Check(f.ints["effects.ready"] == 0, "excessive reserved capacity rejected");
    vector(EffectSlots);
    effect(1, 3599, 1);
    reader.Sample(f.host);
    Check(f.texts["effects.row0.time"] == "2:59", "duration updates from game ticks");
    reader.Sample(f.host);
    Check(f.texts["effects.row0.time"] == "2:59", "paused game ticks do not advance time");
    effect(1, 19, 255);
    reader.Sample(f.host);
    Check(f.texts["effects.row0.time"] == "0:00" && f.texts["effects.row0.strength"] == "256",
          "last partial second and command-level strength remain valid");
    effect(1, 0, 1);
    reader.Sample(f.host);
    Check(f.ints["effects.count"] == 1 && f.texts["effects.row0.name"] == "Night Vision" &&
          f.ints["effects.row1.visible"] == 0 && f.texts["effects.row1.time"].empty(),
          "expiration removes the effect and clears the unused row");
    f.unreadable = effects;
    reader.Sample(f.host);
    Check(f.ints["effects.ready"] == 0 && f.ints["effects.count"] == 0 &&
          f.texts["effects.status"] == "Waiting for effect data" &&
          f.texts["effects.row0.name"].empty(), "read failure clears stale effects");
    f.unreadable = 0;
    for (const u64 at : {effects, header}) {
        f.torn = at;
        f.torn_reads = 0;
        reader.Sample(f.host);
        Check(f.ints["effects.ready"] == 0, "torn effect data or header rejected");
    }
    f.torn = 0;
    for (const s32 bad : {-1, 256}) {
        effect(1, 3600, bad);
        reader.Sample(f.host);
        Check(f.ints["effects.ready"] == 0, "invalid amplifier rejected");
    }
    effect(1, -1, 0);
    reader.Sample(f.host);
    Check(f.ints["effects.ready"] == 0, "negative duration rejected for this build");
    effect(1, 3600, 0);
    f.Put<s32>(effects + 16, 2);
    reader.Sample(f.host);
    Check(f.ints["effects.ready"] == 0, "mismatched registry index rejected");
    effect(1, 3600, 0);
    f.Put<u8>(effects + 16 + 14, 2);
    reader.Sample(f.host);
    Check(f.ints["effects.ready"] == 0, "invalid effect flags rejected");
    effect(1, 3600, 0);
    for (const u64 end : {effects - 16, effects + 17, effects + (EffectSlots + 1) * 16}) {
        f.Put(header + 8, end);
        f.Put(header + 16, end);
        reader.Sample(f.host);
        Check(f.ints["effects.ready"] == 0, "malformed or excessive vector rejected");
    }
    vector(EffectSlots);
    for (int id = 1; id < static_cast<int>(EffectSlots); ++id)
        effect(id, 3600, 0);
    reader.Sample(f.host);
    Check(f.ints["effects.count"] == 25 && f.ints["effects.can_next"] == 1 &&
          f.ints["effects.can_prev"] == 0, "all supported effects can be paged");
    for (int page = 0; page < 8; ++page)
        Check(reader.OnAction("effects_next", 0), "next page action handled");
    reader.Sample(f.host);
    Check(f.ints["effects.row0.id"] == 25 && f.ints["effects.row1.visible"] == 0 &&
          f.ints["effects.can_next"] == 0 && f.texts["effects.page"] == "25-25 of 25",
          "last page clamps and exposes final effect");
    reader.OnAction("effects_previous", 0);
    reader.Sample(f.host);
    Check(f.ints["effects.row0.id"] == 17, "previous page changes the visible effects");
    vector(2);
    reader.Sample(f.host);
    Check(f.ints["effects.row0.id"] == 1 && f.texts["effects.page"] == "1-1 of 1" &&
          f.ints["effects.can_prev"] == 0, "expiration clamps pages back to live entries");
    f.unreadable = f.stacks;
    f.Put<u64>(f.inventory + f.layout->inv_player, 0);
    reader.Sample(f.host);
    Check(f.ints["effects.ready"] == 0 && f.ints["effects.row0.visible"] == 0,
          "world unload clears active effects");
    Fixture modern(true);
    Reader modern_reader(modern.host, nullptr);
    modern_reader.Sample(modern.host);
    Check(modern.ints["effects.ready"] == 0 &&
          modern.texts["effects.status"] == "Effects unavailable for this version",
          "unsupported effects stay unavailable");
}

int main() {
    TestEffects();
    const std::map<std::string, std::string> translations{
        {"enchantment.digging", "Efficiency"}, {"enchantment.durability", "Unbreaking"},
        {"enchantment.lootBonusDigger", "Fortune"}, {"enchantment.frostwalker", "Frost Walker"},
        {"enchantment.curse.binding", "Curse of Binding"}};
    const auto language = [&](const std::string& key) -> const std::string* {
        const auto it = translations.find(key);
        return it == translations.end() ? nullptr : &it->second;
    };
    Check(mc_names::EnchantmentName(15, 5, language) == "Efficiency V" &&
          mc_names::EnchantmentName(17, 3, language) == "Unbreaking III" &&
          mc_names::EnchantmentName(18, 3, language) == "Fortune III" &&
          mc_names::EnchantmentName(25, 2, language) == "Frost Walker II" &&
          mc_names::EnchantmentName(27, 1, language) == "Curse of Binding I",
          "1.2.12 enchantment IDs must not use the modern Bedrock order");
    Check(mc_names::EnchantmentName(15, 11, language) == "Efficiency 11" &&
          mc_names::EnchantmentName(99, 3, language) == "Enchantment 99 III" &&
          mc_names::EnchantmentName(-1, 3, language).empty(),
          "unknown enchantments and unusual levels remain readable");
    {
        Fixture detail;
        Reader inspector(detail.host, nullptr);
        detail.Stack(detail.stacks, detail.item, 100);
        const u64 metadata = detail.Metadata("Miner's lucky pickaxe", 8);
        detail.Put(detail.stacks + detail.layout->stack_user_data, metadata);
        Check(inspector.OnAction("item_inspect", 0), "inventory slot accepts touch");
        inspector.Sample(detail.host);
        Check(detail.texts["detail.name"] == "Diamond Pickaxe" &&
              detail.texts["detail.custom_name"] == "Miner's lucky pickaxe" &&
              detail.texts["detail.durability"] == "Durability: 1461 / 1561 (94%)" &&
              detail.ints["detail.metadata_ok"] == 1, "touched item shows detached metadata");
        Check(detail.texts["detail.enchant0"] == "Enchantment 17 III" &&
              detail.ints["detail.next"] == 1, "enchantment levels and paging");
        Check(inspector.OnAction("item_enchant_page", 1), "next enchantment page accepted");
        inspector.Sample(detail.host);
        Check(detail.texts["detail.enchant0"] == "Enchantment 23 III" &&
              detail.texts["detail.enchant2"].empty() && detail.ints["detail.next"] == 0 &&
              detail.ints["detail.prev"] == 1, "last page clears unused rows");
        detail.Put<s32>(detail.player + detail.layout->player_selected, 5);
        inspector.Sample(detail.host);
        Check(detail.texts["detail.custom_name"] == "Miner's lucky pickaxe",
              "game selection does not change inspected slot");
        detail.Put(detail.stacks + detail.layout->stack_user_data, detail.Metadata("A longer custom name with Unicode: é", 0));
        inspector.Sample(detail.host);
        Check(detail.texts["detail.custom_name"] == "A longer custom name with Unicode: é" &&
              detail.texts["detail.enchant_label"] == "Enchantments: None" &&
              detail.texts["detail.enchant0"].empty(), "long string and fewer enchantments refresh");
        detail.Put(detail.stacks + detail.layout->stack_user_data, metadata);
        detail.unreadable = metadata;
        inspector.Sample(detail.host);
        Check(detail.texts["detail.custom_name"].empty() &&
              detail.texts["detail.enchant_label"] == "Enchantments: Unavailable" &&
              detail.texts["detail.enchant0"].empty(), "unreadable metadata clears previous data");
        detail.unreadable = 0;
        detail.torn = metadata;
        inspector.Sample(detail.host);
        Check(detail.ints["detail.metadata_ok"] == 0, "torn metadata cannot be shown");
        detail.torn = 0;
        detail.replace_stack_on_read = metadata;
        inspector.Sample(detail.host);
        Check(detail.texts["detail.name"] == "Item unavailable" &&
              detail.texts["detail.custom_name"].empty(),
              "stack replaced during NBT read cannot publish the old item's details");
        detail.Stack(detail.stacks, detail.item, 100);
        // Malformed search cycle must stop at the bounded node count.
        const u64 cycle = detail.Compound({{"aaa", detail.Short(1)}});
        const u64 root = cycle + 0x80;
        detail.Put(root + 8, root);
        detail.Put(detail.stacks + detail.layout->stack_user_data, cycle);
        inspector.Sample(detail.host);
        Check(detail.ints["detail.metadata_ok"] == 0, "cyclic metadata is bounded and unavailable");
        detail.Put<u64>(detail.stacks + detail.layout->stack_user_data, 0);
        inspector.Sample(detail.host);
        Check(detail.texts["detail.custom_label"] == "Custom name: None" &&
              detail.texts["detail.enchant_label"] == "Enchantments: None", "ordinary item has no metadata");
        detail.unreadable = detail.item + detail.layout->item_max_damage;
        inspector.Sample(detail.host);
        Check(detail.texts["detail.durability"] == "Durability: Unavailable",
              "unreadable durability is not called non-damageable");
        detail.unreadable = 0;
        detail.Stack(detail.armor, detail.item, 500);
        detail.Put(detail.armor + detail.layout->stack_user_data, metadata);
        Check(inspector.OnAction("item_inspect", 36), "equipment touch accepted");
        inspector.Sample(detail.host);
        Check(detail.texts["detail.durability"] == "Durability: 1061 / 1561 (68%)" &&
              detail.ints["detail.armor"] == 1, "equipment details use its own slot");
        Check(inspector.OnAction("item_inspect", 35), "last inventory slot accepted");
        inspector.Sample(detail.host);
        Check(detail.texts["detail.name"] == "Empty slot" &&
              detail.texts["detail.custom_name"].empty() && detail.texts["detail.enchant0"].empty(),
              "empty slots clear all previous item data");
        Check(!inspector.OnAction("item_inspect", -1) && !inspector.OnAction("item_inspect", 40),
              "out-of-range touch rejected");
        Check(inspector.OnAction("item_close", 0), "close accepted");
        inspector.Sample(detail.host);
        Check(detail.ints["detail.open"] == 0 && detail.ints["detail.closed"] == 1 &&
              detail.texts["detail.name"].empty(), "closing clears drawer");
    }
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
    modern_reader.OnAction("item_inspect", 0);
    modern_reader.Sample(modern.host);
    Check(modern.texts["detail.name"] == "Diamond Pickaxe" &&
          modern.texts["detail.durability"] == "Durability: Unavailable" &&
          modern.texts["detail.enchant_label"] == "Enchantments: Unavailable",
          "unsupported metadata stays explicitly unavailable without hiding item identity");
    modern_reader.OnAction("rescan", 0);
    Check(!modern_reader.InventoryState().ready && modern_reader.InventoryState().slots[0].id.empty(),
          "rescan invalidates public inventory snapshot");
    std::puts("Reader item details, equipment and durability checks passed");
}
