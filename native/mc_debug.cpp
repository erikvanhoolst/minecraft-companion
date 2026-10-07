// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mc_debug.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#include "core/mods/modules/dsmod_module_sdk.h"

namespace mc_debug {
namespace {

using namespace dsmod_sdk::int_types;

constexpr u64 PageSize = 0x1000;
constexpr u64 AliasLo = 0x10'00000000ULL, AliasHi = 0x21'00000000ULL, Chunk = 0x10000;

void Log(const EdenDsmodHostApi& host, const std::string& s) {
    if (host.log)
        host.log(host.userdata, EDEN_DSMOD_LOG_INFO, ("mcdbg: " + s).c_str());
}

bool ReadBytes(const EdenDsmodHostApi& host, u64 at, void* out, std::size_t size) {
    return dsmod_sdk::ReadGuest<dsmod_sdk::MissingIsMapped::Reject>(host, at, out, size);
}

template <class T>
bool Read(const EdenDsmodHostApi& host, u64 at, T& out) {
    return ReadBytes(host, at, &out, sizeof(T));
}

/// Walks every mapped page of the game's own mappings (alias window) and the host heap window.
template <class Fn>
void ForEachMappedPage(const EdenDsmodHostApi& host, Fn&& fn) {
    if (!host.is_mapped || !host.get_read_pointer)
        return;
    for (u64 chunk = AliasLo; chunk < AliasHi; chunk += Chunk) {
        if (!host.is_mapped(host.userdata, chunk, PageSize))
            continue;
        for (u64 page = chunk; page < chunk + Chunk; page += PageSize) {
            const u8* const p = host.get_read_pointer(host.userdata, page, PageSize);
            if (p && !fn(page, p))
                return;
        }
    }
    const u64 lo = host.get_heap_begin ? host.get_heap_begin(host.userdata) : 0;
    const u64 hi = host.get_heap_end ? host.get_heap_end(host.userdata) : 0;
    for (u64 page = lo; page < hi; page += PageSize) {
        const u8* const p = host.get_read_pointer(host.userdata, page, PageSize);
        if (p && !fn(page, p))
            return;
    }
}

std::string Hex(u64 v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%llX", static_cast<unsigned long long>(v));
    return b;
}

std::string Describe(const EdenDsmodHostApi& host, u64 v) {
    if (v >= host.main_base && v < host.main_base + host.main_size)
        return "main+" + Hex(v - host.main_base);
    if (v >= AliasLo && v < AliasHi)
        return "heap";
    return "";
}

bool DecodeLibcxxString(const EdenDsmodHostApi& host, const u8* raw24, std::string& out) {
    if (raw24[0] & 1) {
        u64 size{}, data{};
        std::memcpy(&size, raw24 + 8, 8);
        std::memcpy(&data, raw24 + 16, 8);
        if (size == 0 || size > 200 || data == 0)
            return false;
        out.resize(size);
        return ReadBytes(host, data, out.data(), size);
    }
    const u8 size = raw24[0] >> 1;
    if (size == 0 || size > 22)
        return false;
    out.assign(reinterpret_cast<const char*>(raw24 + 1), size);
    return true;
}

bool Printable(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return c >= 0x20 && c < 0x7f;
    });
}

} // namespace

void Console::Tick(const EdenDsmodHostApi& host, const mc_reader::Reader& reader,
                   mc_assets::Library* assets) {
    assets_ = assets;
    if (++tick % 30 != 0 || !host.read_romfs)
        return;
    const char* path = "user:mcdbg.txt";
    const std::size_t size = host.read_romfs(host.userdata, path, 0, nullptr, 0);
    if (size == 0 || size > 65536)
        return;
    std::string text(size, '\0');
    if (host.read_romfs(host.userdata, path, 0, text.data(), size) != size)
        return;
    const u64 hash = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(text.data()), text.size());
    if (hash == last_hash)
        return;
    last_hash = hash;
    std::istringstream lines{text};
    std::string line;
    while (std::getline(lines, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        try {
            Run(host, reader, line);
        } catch (...) {
            Log(host, "command failed: " + line);
        }
    }
    Log(host, "done");
}

void Console::Run(const EdenDsmodHostApi& host, const mc_reader::Reader& reader,
                  const std::string& line) {
    std::istringstream in{line};
    std::string op;
    in >> op;
    Log(host, "> " + line);
    if (op == "dump") {
        std::string a;
        u64 len = 0x100;
        in >> a;
        if (!(in >> std::hex >> len))
            len = 0x100;
        const u64 at = std::stoull(a, nullptr, 16);
        len = std::min<u64>(len, 0x800);
        std::vector<u8> buf(len);
        if (!ReadBytes(host, at, buf.data(), len)) {
            Log(host, "unmapped");
            return;
        }
        for (u64 off = 0; off < len; off += 32) {
            std::string s = Hex(at + off) + ":";
            for (u64 k = 0; k < 32 && off + k + 8 <= len; k += 8) {
                u64 w{};
                std::memcpy(&w, buf.data() + off + k, 8);
                char b[24];
                std::snprintf(b, sizeof(b), " %016llX", static_cast<unsigned long long>(w));
                s += b;
                const std::string d = Describe(host, w);
                if (!d.empty())
                    s += "[" + d + "]";
            }
            Log(host, s);
        }
    } else if (op == "str") {
        std::string needle;
        in >> needle;
        int max = 20;
        in >> max;
        int hits = 0;
        ForEachMappedPage(host, [&](u64 page, const u8* p) {
            const u8* cur = p;
            const u8* end = p + PageSize;
            while (cur < end && hits < max) {
                const u8* f = static_cast<const u8*>(
                    memmem(cur, static_cast<std::size_t>(end - cur), needle.data(), needle.size()));
                if (!f)
                    break;
                Log(host, "  str @ " + Hex(page + static_cast<u64>(f - p)));
                ++hits;
                cur = f + 1;
            }
            return hits < max;
        });
        Log(host, "str hits: " + std::to_string(hits));
    } else if (op == "vt" || op == "ptr") {
        std::string a;
        in >> a;
        int max = 20;
        in >> max;
        const u64 want = op == "vt" ? host.main_base + std::stoull(a, nullptr, 16)
                                    : std::stoull(a, nullptr, 16);
        int hits = 0;
        ForEachMappedPage(host, [&](u64 page, const u8* p) {
            for (u64 off = 0; off + 8 <= PageSize; off += 8) {
                u64 w{};
                std::memcpy(&w, p + off, 8);
                if (w == want) {
                    Log(host, "  " + op + " @ " + Hex(page + off));
                    if (++hits >= max)
                        return false;
                }
            }
            return true;
        });
        // statics: the main image's data segment
        if (op == "ptr" && host.get_read_pointer) {
            for (u64 page = host.main_base; page < host.main_base + host.main_size && hits < max;
                 page += PageSize) {
                const u8* const p = host.get_read_pointer(host.userdata, page, PageSize);
                if (!p)
                    continue;
                for (u64 off = 0; off + 8 <= PageSize; off += 8) {
                    u64 w{};
                    std::memcpy(&w, p + off, 8);
                    if (w == want) {
                        Log(host, "  ptr @ main+" + Hex(page + off - host.main_base));
                        if (++hits >= max)
                            break;
                    }
                }
            }
        }
        Log(host, op + " hits: " + std::to_string(hits));
    } else if (op == "item") {
        std::string a;
        in >> a;
        const u64 at = std::stoull(a, nullptr, 16);
        std::vector<u8> buf(0x400);
        if (!ReadBytes(host, at, buf.data(), buf.size())) {
            Log(host, "unmapped");
            return;
        }
        u64 vptr{};
        std::memcpy(&vptr, buf.data(), 8);
        Log(host, "item vptr " + Describe(host, vptr));
        for (u64 off = 8; off + 24 <= buf.size(); off += 8) {
            std::string s;
            if (DecodeLibcxxString(host, buf.data() + off, s) && Printable(s) && s.size() >= 2)
                Log(host, "  +" + Hex(off) + " string \"" + s + "\"");
            u32 v{};
            std::memcpy(&v, buf.data() + off, 4);
            if (v < 64 && v > 0)
                Log(host, "  +" + Hex(off) + " int " + std::to_string(v));
        }
    } else if (op == "inv") {
        const auto* lay = reader.ActiveLayout();
        Log(host, "inventory " + Hex(reader.Inventory()) + " items " + Hex(reader.ItemsBegin()) +
                      " pinv " + Hex(reader.PlayerInventory()));
        if (!reader.Inventory() || !lay)
            return;
        std::vector<u8> stack(lay->stack_size);
        for (std::size_t i = 0; i < 36; ++i) {
            if (!ReadBytes(host, reader.ItemsBegin() + i * lay->stack_size, stack.data(), stack.size()))
                break;
            u64 handle{}, block{};
            std::memcpy(&handle, stack.data() + lay->stack_item, 8);
            std::memcpy(&block, stack.data() + lay->stack_block, 8);
            s16 aux{};
            std::memcpy(&aux, stack.data() + lay->stack_aux, 2);
            const u8 count = stack[lay->stack_count];
            if (count == 0 && handle == 0)
                continue;
            u64 item{};
            Read(host, handle, item);
            Log(host, "  slot " + std::to_string(i) + " count " + std::to_string(count) + " aux " +
                          std::to_string(aux) + " handle " + Hex(handle) + " -> item " + Hex(item) +
                          " block " + Hex(block));
        }
        if (reader.PlayerInventory()) {
            std::vector<u8> pinv(0x100);
            if (ReadBytes(host, reader.PlayerInventory(), pinv.data(), pinv.size()))
                for (u64 off = 0; off < pinv.size(); off += 32) {
                    std::string s = "  pinv+" + Hex(off) + ":";
                    for (u64 k = 0; k < 32; k += 8) {
                        u64 w{};
                        std::memcpy(&w, pinv.data() + off + k, 8);
                        char b[24];
                        std::snprintf(b, sizeof(b), " %016llX", static_cast<unsigned long long>(w));
                        s += b;
                    }
                    Log(host, s);
                }
        }
    } else if (op == "items") {
        // 1.2.x Item objects carry std::string "atlas.items" (short form: 0x16 then the text) at
        // +0x10. Name at +0x48, icon name at +0xD8 (see mc_reader.cpp's D8B7 layout).
        static const char pattern[] = "\x16" "atlas.items";
        const std::size_t plen = sizeof(pattern) - 1;
        std::vector<u64> found;
        ForEachMappedPage(host, [&](u64 page, const u8* p) {
            for (u64 off = 0x10; off + plen <= PageSize; off += 8) {
                if (std::memcmp(p + off, pattern, plen) != 0)
                    continue;
                u64 vptr{};
                std::memcpy(&vptr, p + off - 0x10, 8);
                if (vptr >= host.main_base && vptr < host.main_base + host.main_size)
                    found.push_back(page + off - 0x10);
            }
            return found.size() < 4000;
        });
        if (assets_)
            assets_->EnsureLoaded(host);
        int ok = 0, missing = 0, unnamed = 0;
        std::vector<std::string> seen;
        for (const u64 item : found) {
            u8 raw[24];
            std::string name, icon;
            if (ReadBytes(host, item + 0x48, raw, sizeof(raw)))
                DecodeLibcxxString(host, raw, name);
            if (ReadBytes(host, item + 0xD8, raw, sizeof(raw)))
                DecodeLibcxxString(host, raw, icon);
            if (!Printable(name))
                continue;
            if (std::find(seen.begin(), seen.end(), name) != seen.end())
                continue;
            seen.push_back(name);
            const auto* lay = reader.ActiveLayout();
            const bool use_icon = lay && lay->item_icon_name != 0 && Printable(icon);
            const std::string key = use_icon ? "icon/" + icon + "/0" : "item/" + name + "/0";
            bool good = false;
            std::string size;
            if (assets_) {
                if (const auto img = assets_->LoadIcon(host, key)) {
                    good = true;
                    size = std::to_string(img->width) + "x" + std::to_string(img->height);
#if defined(__linux__) && !defined(__ANDROID__)
                    // Desktop research aid: MC_ICON_DUMP=<dir> writes <name>_<w>x<h>.rgba.
                    if (const char* dir = std::getenv("MC_ICON_DUMP"); dir && *dir) {
                        const std::string file = std::string(dir) + "/" + name + "_" + size + ".rgba";
                        if (FILE* f = std::fopen(file.c_str(), "wb")) {
                            std::fwrite(img->rgba.data(), 1, img->rgba.size(), f);
                            std::fclose(f);
                        }
                    }
#endif
                }
            }
            (good ? ok : missing)++;
            // The display name the reader publishes (description id + aux 0 + language file).
            std::string description, display;
            if (lay && lay->item_description_id &&
                ReadBytes(host, item + lay->item_description_id, raw, sizeof(raw)))
                DecodeLibcxxString(host, raw, description);
            if (assets_)
                display = assets_->ItemName(description, 0);
            if (display.empty())
                ++unnamed;
            Log(host, std::string(good ? "  OK   " : "  MISS ") + Hex(item) + " " + name + " icon=" +
                          (Printable(icon) ? icon : std::string("-")) + " key=" + key + " " + size +
                          " desc=" + description + " name=" + (display.empty() ? "?" : display));
        }
        Log(host, "items: " + std::to_string(seen.size()) + " distinct, " + std::to_string(ok) +
                      " with icon, " + std::to_string(missing) + " missing, " +
                      std::to_string(unnamed) + " without a name");
    } else if (op == "player") {
        const u64 p = reader.Player();
        u64 vptr{};
        Read(host, p, vptr);
        Log(host, "player " + Hex(p) + " vptr " + Describe(host, vptr));
    } else if (op == "floats") {
        // floats <hexaddr|player> <hexlen>: every 4-byte word that reads as a "game number"
        // (a finite float in 0.25..100000 that is a multiple of 0.25)
        std::string a;
        u64 len = 0x2000;
        in >> a >> std::hex >> len;
        const u64 at = a == "player" ? reader.Player() : std::stoull(a, nullptr, 16);
        len = std::min<u64>(len, 0x10000);
        std::vector<u8> buf(len);
        if (!at || !ReadBytes(host, at, buf.data(), len)) {
            Log(host, "unmapped");
            return;
        }
        std::string line;
        int on_line = 0;
        for (u64 off = 0; off + 4 <= len; off += 4) {
            float f{};
            std::memcpy(&f, buf.data() + off, 4);
            if (!(f >= 0.25f && f <= 100000.0f) || f * 4.0f != static_cast<float>(static_cast<int>(f * 4.0f)))
                continue;
            char b[48];
            std::snprintf(b, sizeof(b), " +%llX=%g", static_cast<unsigned long long>(off), f);
            line += b;
            if (++on_line == 8) {
                Log(host, line);
                line.clear();
                on_line = 0;
            }
        }
        if (!line.empty())
            Log(host, line);
    } else if (op == "find") {
        // find <f32|i32|i16|u64> <value> [hexaddr|player hexlen] [max]: a value in a range, or
        // in every mapped page of the game's own mappings and the heap when no range is given
        std::string type, value, a;
        in >> type >> value;
        u64 len = 0;
        int max = 40;
        u8 want[8]{};
        std::size_t width = 4;
        if (type == "f32") {
            const float f = std::stof(value);
            std::memcpy(want, &f, 4);
        } else if (type == "i32") {
            const s32 v = static_cast<s32>(std::stol(value, nullptr, 0));
            std::memcpy(want, &v, 4);
        } else if (type == "i16") {
            const s16 v = static_cast<s16>(std::stol(value, nullptr, 0));
            std::memcpy(want, &v, 2);
            width = 2;
        } else {
            const u64 v = std::stoull(value, nullptr, 16);
            std::memcpy(want, &v, 8);
            width = 8;
        }
        u64 at = 0;
        if (in >> a) {
            at = a == "player" ? reader.Player() : std::stoull(a, nullptr, 16);
            in >> std::hex >> len >> std::dec >> max;
        }
        int hits = 0;
        const auto search = [&](u64 base, const u8* p, u64 n) {
            for (u64 off = 0; off + width <= n && hits < max; off += width) {
                if (std::memcmp(p + off, want, width) == 0) {
                    Log(host, "  hit " + Hex(base + off) + (at ? " (+" + Hex(base + off - at) + ")" : ""));
                    ++hits;
                }
            }
        };
        if (at && len) {
            std::vector<u8> buf(std::min<u64>(len, 0x100000));
            if (ReadBytes(host, at, buf.data(), buf.size()))
                search(at, buf.data(), buf.size());
        } else {
            ForEachMappedPage(host, [&](u64 page, const u8* p) {
                search(page, p, PageSize);
                return hits < max;
            });
        }
        Log(host, "find hits: " + std::to_string(hits));
    } else if (op == "attr") {
        // attr <hex main offset of an Attribute>: every AttributeInstance that points at it
        // (the word after the instance's map pointer), the map pointer, whether the local
        // player's object holds that map pointer, and the instance's six floats
        std::string a;
        in >> a;
        const u64 want = host.main_base + std::stoull(a, nullptr, 16);
        std::vector<u64> hits;
        ForEachMappedPage(host, [&](u64 page, const u8* p) {
            for (u64 off = 0; off + 8 <= PageSize; off += 8) {
                u64 w{};
                std::memcpy(&w, p + off, 8);
                if (w == want)
                    hits.push_back(page + off);
            }
            return hits.size() < 4000;
        });
        std::vector<u8> pl(0x3000);
        const bool have_player =
            reader.Player() && ReadBytes(host, reader.Player(), pl.data(), pl.size());
        int shown = 0;
        for (const u64 h : hits) {
            u64 map{};
            Read(host, h - 8, map);
            std::string where;
            if (have_player)
                for (u64 off = 0; off + 8 <= pl.size(); off += 8) {
                    u64 w{};
                    std::memcpy(&w, pl.data() + off, 8);
                    if (w == map && map)
                        where += " player+" + Hex(off);
                }
            float f[6]{};
            ReadBytes(host, h + 0x60, f, sizeof(f));
            char b[160];
            std::snprintf(b, sizeof(b), "  inst %llX map %llX floats %g %g %g | %g %g %g",
                          static_cast<unsigned long long>(h - 0x10),
                          static_cast<unsigned long long>(map), f[0], f[1], f[2], f[3], f[4], f[5]);
            if (!where.empty() || ++shown <= 3)
                Log(host, b + where);
        }
        Log(host, "attr instances: " + std::to_string(hits.size()));
    } else if (op == "amap") {
        // amap [hexaddr]: walk a BaseAttributeMap (libc++ unordered_map<std::string,
        // AttributeInstance>); default: the local player's (Layout::player_attributes)
        std::string a;
        u64 map{};
        const auto* lay = reader.ActiveLayout();
        if (in >> a)
            map = std::stoull(a, nullptr, 16);
        else if (lay && lay->player_attributes)
            Read(host, reader.Player() + lay->player_attributes, map);
        u64 node{};
        Read(host, map + 0x10, node);
        for (int n = 0; node && n < 64; ++n) {
            u8 raw[24];
            std::string key;
            if (ReadBytes(host, node + 0x10, raw, sizeof(raw)))
                DecodeLibcxxString(host, raw, key);
            const u64 inst = node + 0x28;
            u64 attr{};
            Read(host, inst + 0x10, attr);
            float f[6]{};
            ReadBytes(host, inst + 0x70, f, sizeof(f));
            char b[200];
            std::snprintf(b, sizeof(b), "  %s inst %llX attr %s floats %g %g %g | %g %g %g",
                          key.c_str(), static_cast<unsigned long long>(inst),
                          Describe(host, attr).c_str(), f[0], f[1], f[2], f[3], f[4], f[5]);
            Log(host, b);
            Read(host, node, node);
        }
    } else if (op == "vecs") {
        // vecs <hexaddr|player> <hexlen>: std::vector-shaped triples (begin <= end <= cap, all
        // non-null, at most 0x2000 bytes apart) with their element count at stride 8
        std::string a;
        u64 len = 0x3000;
        in >> a >> std::hex >> len;
        const u64 at = a == "player" ? reader.Player() : std::stoull(a, nullptr, 16);
        std::vector<u8> buf(std::min<u64>(len, 0x10000));
        if (!at || !ReadBytes(host, at, buf.data(), buf.size())) {
            Log(host, "unmapped");
            return;
        }
        for (u64 off = 0; off + 24 <= buf.size(); off += 8) {
            u64 b{}, e{}, c{};
            std::memcpy(&b, buf.data() + off, 8);
            std::memcpy(&e, buf.data() + off + 8, 8);
            std::memcpy(&c, buf.data() + off + 16, 8);
            if (!b || b < 0x10'00000000ULL || e < b || c < e || c - b > 0x2000 || e == b)
                continue;
            u64 first{};
            Read(host, b, first);
            Log(host, "  +" + Hex(off) + " vec " + Hex(b) + " bytes " + Hex(e - b) + " cap " +
                          Hex(c - b) + " first " + Hex(first) + " " + Describe(host, first));
        }
    } else if (op == "strm") {
        // strm <text> [max]: like str, but in the main image (static data)
        std::string needle;
        in >> needle;
        int max = 20;
        in >> max;
        int hits = 0;
        constexpr u64 Step = 0x10000;
        std::vector<u8> buf(Step + 256);
        for (u64 at = host.main_base; at < host.main_base + host.main_size && hits < max; at += Step) {
            const u64 n = std::min<u64>(buf.size(), host.main_base + host.main_size - at);
            if (!ReadBytes(host, at, buf.data(), n))
                continue;
            const u8* cur = buf.data();
            const u8* const stop = buf.data() + std::min<u64>(n, Step);
            while (cur < stop && hits < max) {
                const u8* f = static_cast<const u8*>(
                    memmem(cur, static_cast<std::size_t>(buf.data() + n - cur), needle.data(), needle.size()));
                if (!f || f >= stop)
                    break;
                Log(host, "  strm @ main+" + Hex(at + static_cast<u64>(f - buf.data()) - host.main_base));
                ++hits;
                cur = f + 1;
            }
        }
        Log(host, "strm hits: " + std::to_string(hits));
#if defined(__linux__) && !defined(__ANDROID__)
    } else if (op == "save") {
        // Desktop research aid. save <hexaddr|player> <hexlen> <file>: raw guest bytes to a file
        std::string a, file;
        u64 len = 0x1000;
        in >> a >> std::hex >> len >> file;
        const u64 at = a == "player" ? reader.Player() : std::stoull(a, nullptr, 16);
        std::vector<u8> buf(std::min<u64>(len, 0x4000000));
        if (!at || file.empty() || !ReadBytes(host, at, buf.data(), buf.size())) {
            Log(host, "unmapped");
            return;
        }
        if (FILE* f = std::fopen(file.c_str(), "wb")) {
            std::fwrite(buf.data(), 1, buf.size(), f);
            std::fclose(f);
            Log(host, "saved " + Hex(buf.size()) + " bytes");
        }
    } else if (op == "snap") {
        // Desktop research aid. snap <dir>: every mapped page of the game's mappings and the
        // heap, as <dir>/<hexaddr>.bin runs of contiguous pages, plus <dir>/main.bin
        std::string dir;
        in >> dir;
        FILE* f = nullptr;
        u64 run_end = 0, total = 0;
        ForEachMappedPage(host, [&](u64 page, const u8* p) {
            if (!f || page != run_end) {
                if (f)
                    std::fclose(f);
                f = std::fopen((dir + "/" + Hex(page) + ".bin").c_str(), "wb");
                if (!f)
                    return false;
            }
            std::fwrite(p, 1, PageSize, f);
            run_end = page + PageSize;
            total += PageSize;
            return true;
        });
        if (f)
            std::fclose(f);
        std::vector<u8> main(host.main_size);
        if (ReadBytes(host, host.main_base, main.data(), main.size()))
            if (FILE* m = std::fopen((dir + "/main_" + Hex(host.main_base) + ".bin").c_str(), "wb")) {
                std::fwrite(main.data(), 1, main.size(), m);
                std::fclose(m);
            }
        Log(host, "snap " + Hex(total) + " bytes");
#endif
    } else if (op == "layout") {
        const auto* lay = reader.ActiveLayout();
        if (!lay) {
            Log(host, "no layout");
            return;
        }
        Log(host, "stack_size " + Hex(lay->stack_size) + " inv_items " + Hex(lay->inv_items_begin) +
                      " inv_player " + Hex(lay->inv_player) + " pinv_sel " + Hex(lay->pinv_selected) +
                      " pinv_inv " + Hex(lay->pinv_inventory) + " item_icon " +
                      Hex(lay->item_icon_name) + " item_frame " + Hex(lay->item_icon_frame) +
                      " item_name " + Hex(lay->item_full_name));
    } else {
        Log(host, "unknown command");
    }
}

} // namespace mc_debug
