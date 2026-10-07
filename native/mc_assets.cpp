// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mc_assets.h"
#include "mc_names.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "third_party/stb_image.h"

namespace mc_assets {
namespace {

constexpr std::string_view PacksRoot = "resource_packs/";

std::string Concat(std::string_view a, std::string_view b, std::string_view c = {},
                   std::string_view d = {}) {
    std::string out;
    out.reserve(a.size() + b.size() + c.size() + d.size());
    out.append(a).append(b).append(c).append(d);
    return out;
}

/// Strip `//` line comments outside strings and trailing commas before `}` / `]`.
std::string Lenient(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool in_string = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            out.push_back(c);
            if (c == '\\' && i + 1 < text.size()) {
                out.push_back(text[++i]);
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            out.push_back(c);
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n')
                ++i;
            out.push_back('\n');
            continue;
        }
        if (c == ',') {
            std::size_t j = i + 1;
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j])))
                ++j;
            if (j < text.size() && (text[j] == '}' || text[j] == ']'))
                continue; // drop the trailing comma
        }
        out.push_back(c);
    }
    // UTF-8 BOM
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return out;
}

struct PackVersion {
    int rank; // 0 vanilla_base, 1 vanilla, 2 versioned
    std::array<int, 3> v{};
    bool operator<(const PackVersion& o) const {
        return rank != o.rank ? rank < o.rank : v < o.v;
    }
};

bool FileExists(const EdenDsmodHostApi& host, const std::string& path) {
    return host.read_romfs && host.read_romfs(host.userdata, path.c_str(), 0, nullptr, 0) > 0;
}

} // namespace

std::optional<std::vector<std::uint8_t>> ReadRomfsFile(const EdenDsmodHostApi& host,
                                                       std::string_view path) {
    if (!host.read_romfs)
        return std::nullopt;
    const std::string p{path};
    const std::size_t size = host.read_romfs(host.userdata, p.c_str(), 0, nullptr, 0);
    if (size == 0 || size > (64u << 20))
        return std::nullopt;
    std::vector<std::uint8_t> bytes(size);
    const std::size_t got = host.read_romfs(host.userdata, p.c_str(), 0, bytes.data(), size);
    if (got != size)
        return std::nullopt;
    return bytes;
}

nlohmann::json ParseLenientJson(std::string_view text) {
    return nlohmann::json::parse(Lenient(text), nullptr, false, true);
}

std::optional<Image> DecodeImage(const std::uint8_t* bytes, std::size_t size) {
    if (!bytes || size == 0 || size > INT32_MAX)
        return std::nullopt;
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        if (pixels)
            stbi_image_free(pixels);
        return std::nullopt;
    }
    Image img;
    img.width = static_cast<std::uint32_t>(w);
    img.height = static_cast<std::uint32_t>(h);
    img.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return img;
}

bool Library::EnsureLoaded(const EdenDsmodHostApi& host) {
    std::lock_guard lock{mutex};
    if (loaded.load(std::memory_order_acquire))
        return true;
    Index fresh;
    BuildIndex(host, fresh);
    if ((fresh.packs.empty() && fresh.zips.empty()) || fresh.item_textures.empty())
        return false;
    index = std::move(fresh);
    loaded.store(true, std::memory_order_release);
    return true;
}

bool Library::Loaded() const {
    return loaded.load(std::memory_order_acquire);
}

std::vector<std::string> Library::Packs() const {
    std::lock_guard lock{mutex};
    return index.packs;
}

void Library::BuildIndex(const EdenDsmodHostApi& host, Index& out) const {
    // Discover the vanilla pack folders. The ABI reads files only, so probe manifest.json for the
    // folder names the game uses: vanilla_base, vanilla, vanilla_<M>.<m> and
    // vanilla_<M>.<m>.<p>. Minor versions run well past the game's own (1.26.x); the probe stops
    // after a run of minors without any folder.
    std::vector<std::pair<PackVersion, std::string>> found;
    if (FileExists(host, Concat(PacksRoot, "vanilla_base/manifest.json")))
        found.push_back({{0, {}}, "vanilla_base"});
    if (FileExists(host, Concat(PacksRoot, "vanilla/manifest.json")))
        found.push_back({{1, {}}, "vanilla"});
    int quiet_minors = 0;
    for (int minor = 14; minor <= 80 && quiet_minors < 6; ++minor) {
        bool any = false;
        const std::string base = "vanilla_1." + std::to_string(minor);
        if (FileExists(host, Concat(PacksRoot, base, "/manifest.json"))) {
            found.push_back({{2, {1, minor, -1}}, base});
            any = true;
        }
        for (int patch = 0; patch <= 255; ++patch) {
            const std::string name = base + "." + std::to_string(patch);
            if (FileExists(host, Concat(PacksRoot, name, "/manifest.json"))) {
                found.push_back({{2, {1, minor, patch}}, name});
                any = true;
            }
        }
        quiet_minors = any ? 0 : quiet_minors + 1;
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [ver, name] : found)
        out.packs.push_back(name);
    // Minecraft 1.2.x: the vanilla packs are zip archives.
    for (const char* zip_name : {"vanilla_base.zip", "vanilla.zip"}) {
        mc_zip::Archive z;
        if (z.Open(host, Concat(PacksRoot, zip_name)))
            out.zips.push_back(std::move(z));
    }
    auto read_pack_file = [&](const std::string& root, const mc_zip::Archive* zip,
                              const char* member) -> std::optional<std::vector<std::uint8_t>> {
        if (zip)
            return zip->Read(host, member);
        return ReadRomfsFile(host, root + member);
    };
    for (const mc_zip::Archive& z : out.zips) {
        if (auto bytes = z.Read(host, "textures/item_texture.json")) {
            auto j = ParseLenientJson(
                std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
            if (j.is_object() && j.contains("texture_data") && j["texture_data"].is_object())
                for (auto& [k, v] : j["texture_data"].items())
                    if (v.is_object() && v.contains("textures"))
                        out.item_textures[k] = v["textures"];
        }
        if (auto bytes = z.Read(host, "textures/terrain_texture.json")) {
            auto j = ParseLenientJson(
                std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
            if (j.is_object() && j.contains("texture_data") && j["texture_data"].is_object())
                for (auto& [k, v] : j["texture_data"].items())
                    if (v.is_object() && v.contains("textures"))
                        out.terrain_textures[k] = v["textures"];
        }
        if (auto bytes = z.Read(host, "blocks.json")) {
            auto j = ParseLenientJson(
                std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
            if (j.is_object())
                for (auto& [k, v] : j.items())
                    if (k != "format_version" && v.is_object())
                        out.blocks[k] = v;
        }
        if (auto bytes = z.Read(host, "texts/en_US.lang")) {
            std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
            while (!text.empty()) {
                const std::size_t nl = text.find('\n');
                std::string_view line = text.substr(0, nl);
                text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
                if (const std::size_t hash = line.find('#'); hash != std::string_view::npos)
                    line = line.substr(0, hash);
                const std::size_t eq = line.find('=');
                if (eq == std::string_view::npos)
                    continue;
                std::string_view key = line.substr(0, eq), value = line.substr(eq + 1);
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                    value.remove_suffix(1);
                if (!key.empty())
                    out.lang[std::string{key}] = std::string{value};
            }
        }
    }
    (void)read_pack_file;

    for (const std::string& pack : out.packs) {
        const std::string root = Concat(PacksRoot, pack, "/");
        if (auto bytes = ReadRomfsFile(host, root + "textures/item_texture.json")) {
            auto j = ParseLenientJson(
                std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
            if (j.is_object() && j.contains("texture_data") && j["texture_data"].is_object())
                for (auto& [k, v] : j["texture_data"].items())
                    if (v.is_object() && v.contains("textures"))
                        out.item_textures[k] = v["textures"];
        }
        if (auto bytes = ReadRomfsFile(host, root + "textures/terrain_texture.json")) {
            auto j = ParseLenientJson(
                std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
            if (j.is_object() && j.contains("texture_data") && j["texture_data"].is_object())
                for (auto& [k, v] : j["texture_data"].items())
                    if (v.is_object() && v.contains("textures"))
                        out.terrain_textures[k] = v["textures"];
        }
        if (auto bytes = ReadRomfsFile(host, root + "blocks.json")) {
            auto j = ParseLenientJson(
                std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
            if (j.is_object())
                for (auto& [k, v] : j.items())
                    if (k != "format_version" && v.is_object())
                        out.blocks[k] = v;
        }
        if (auto bytes = ReadRomfsFile(host, root + "texts/en_US.lang")) {
            std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
            while (!text.empty()) {
                const std::size_t nl = text.find('\n');
                std::string_view line = text.substr(0, nl);
                text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
                if (const std::size_t hash = line.find('#'); hash != std::string_view::npos)
                    line = line.substr(0, hash);
                const std::size_t eq = line.find('=');
                if (eq == std::string_view::npos)
                    continue;
                std::string_view key = line.substr(0, eq), value = line.substr(eq + 1);
                while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())))
                    key.remove_suffix(1);
                while (!value.empty() && (value.back() == '\r' || value.back() == '\t' ||
                                          std::isspace(static_cast<unsigned char>(value.back()))))
                    value.remove_suffix(1);
                if (!key.empty())
                    out.lang[std::string{key}] = std::string{value};
            }
        }
    }
}

std::optional<std::string> Library::PickTexture(const nlohmann::json& textures, int frame,
                                                std::string* overlay) {
    const nlohmann::json* t = &textures;
    if (t->is_array()) {
        if (t->empty())
            return std::nullopt;
        const std::size_t i = frame >= 0 && static_cast<std::size_t>(frame) < t->size()
                                  ? static_cast<std::size_t>(frame)
                                  : 0;
        t = &(*t)[i];
    }
    if (t->is_object()) {
        if (!t->contains("path"))
            return std::nullopt;
        if (overlay && t->contains("overlay_color") && (*t)["overlay_color"].is_string())
            *overlay = (*t)["overlay_color"].get<std::string>();
        t = &(*t)["path"];
    }
    if (!t->is_string())
        return std::nullopt;
    return t->get<std::string>();
}

std::optional<std::string> Library::FindTextureFile(const EdenDsmodHostApi& host,
                                                    std::string_view relative) const {
    // The newest pack that carries the file wins.
    for (auto it = index.packs.rbegin(); it != index.packs.rend(); ++it) {
        for (const char* ext : {".png", ".tga"}) {
            std::string path = Concat(PacksRoot, *it, "/", relative);
            path += ext;
            if (FileExists(host, path))
                return path;
        }
    }
    for (auto it = index.zips.rbegin(); it != index.zips.rend(); ++it) {
        for (const char* ext : {".png", ".tga"}) {
            std::string member{relative};
            member += ext;
            if (it->Contains(member))
                return "zip:" + it->Path() + "#" + member;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> Library::ReadTexture(const EdenDsmodHostApi& host,
                                                              const std::string& path) const {
    if (path.rfind("zip:", 0) == 0) {
        const auto hash = path.find('#');
        if (hash == std::string::npos)
            return std::nullopt;
        const std::string archive = path.substr(4, hash - 4);
        const std::string member = path.substr(hash + 1);
        std::lock_guard lock{mutex};
        for (const mc_zip::Archive& z : index.zips)
            if (z.Path() == archive)
                return z.Read(host, member);
        return std::nullopt;
    }
    return ReadRomfsFile(host, path);
}

std::optional<std::string> Library::ItemIconPath(const EdenDsmodHostApi& host,
                                                 std::string_view icon_name, int frame) const {
    std::lock_guard lock{mutex};
    const auto it = index.item_textures.find(std::string{icon_name});
    if (it == index.item_textures.end())
        return std::nullopt;
    const auto rel = PickTexture(it->second, frame);
    return rel ? FindTextureFile(host, *rel) : std::nullopt;
}

std::optional<std::string> Library::BlockIconPath(const EdenDsmodHostApi& host,
                                                  std::string_view block_name, int variant) const {
    std::lock_guard lock{mutex};
    static const std::map<std::string, std::string> aliases{{"grass_block", "grass"}};
    std::string name{block_name};
    auto bit = index.blocks.find(name);
    if (bit == index.blocks.end()) {
        if (const auto a = aliases.find(name); a != aliases.end())
            bit = index.blocks.find(a->second);
        if (bit == index.blocks.end())
            return std::nullopt;
    }
    const nlohmann::json& entry = bit->second;
    // carried_textures is the block's inventory/hand look (pre-tinted grass, leaves, vines).
    // Grass's carried texture is only the tint overlay, so it keeps its normal side face.
    const bool use_carried = entry.contains("carried_textures") && bit->first != "grass";
    const char* field = use_carried ? "carried_textures" : "textures";
    if (!entry.contains(field))
        return std::nullopt;
    const nlohmann::json& t = entry[field];
    std::string key;
    if (t.is_string()) {
        key = t.get<std::string>();
    } else if (t.is_object()) {
        for (const char* face : {"side", "north", "east", "west", "south", "up", "down"}) {
            if (t.contains(face) && t[face].is_string()) {
                key = t[face].get<std::string>();
                break;
            }
        }
    }
    if (key.empty())
        return std::nullopt;
    const auto tit = index.terrain_textures.find(key);
    if (tit == index.terrain_textures.end())
        return std::nullopt;
    std::string overlay;
    const auto rel = PickTexture(tit->second, variant, &overlay);
    auto path = rel ? FindTextureFile(host, *rel) : std::nullopt;
    if (path && !overlay.empty())
        *path += "|" + overlay; // tint request, applied after decoding (LoadIcon)
    return path;
}

std::string Library::DisplayName(std::string_view full_name) const {
    if (!Loaded())
        return {};
    std::string_view shortn = full_name;
    if (const auto colon = shortn.find(':'); colon != std::string_view::npos)
        shortn = shortn.substr(colon + 1);
    const std::string s{shortn};
    for (const std::string& key : {"item." + s + ".name", "tile." + s + ".name",
                                   "tile." + s + "." + s + ".name"}) {
        if (const auto it = index.lang.find(key); it != index.lang.end())
            return it->second;
    }
    return {};
}

std::string Library::ItemName(std::string_view description_id, int aux) const {
    if (!Loaded())
        return {};
    return mc_names::DisplayName(description_id, aux, [this](const std::string& key) {
        const auto it = index.lang.find(key);
        return it == index.lang.end() ? nullptr : &it->second;
    });
}

std::optional<Image> Library::LoadIcon(const EdenDsmodHostApi& host, std::string_view key) const {
    std::optional<std::string> path;
    auto split_frame = [](std::string_view s, int& frame) {
        frame = 0;
        const auto slash = s.rfind('/');
        if (slash == std::string_view::npos)
            return s;
        const auto num = s.substr(slash + 1);
        int v = 0;
        if (std::from_chars(num.data(), num.data() + num.size(), v).ec == std::errc{}) {
            frame = v;
            return s.substr(0, slash);
        }
        return s;
    };
    if (key.starts_with("ui/")) {
        std::lock_guard lock{mutex};
        path = FindTextureFile(host, Concat("textures/ui/", key.substr(3)));
    } else if (key.starts_with("font/")) {
        std::lock_guard lock{mutex};
        path = FindTextureFile(host, key);
    } else if (key.starts_with("map/")) {
        std::lock_guard lock{mutex};
        path = FindTextureFile(host, Concat("textures/map/", key.substr(4)));
    } else if (key.starts_with("mapicon/")) {
        // One cell of the map's marker sheet (4 x 4 icons; 0 is the player's white arrow).
        int cell = 0;
        const auto num = key.substr(8);
        if (std::from_chars(num.data(), num.data() + num.size(), cell).ec != std::errc{} ||
            cell < 0 || cell > 15)
            return std::nullopt;
        const auto sheet = LoadIcon(host, "map/map_icons");
        if (!sheet || sheet->width < 4 || sheet->width != sheet->height)
            return std::nullopt;
        const std::uint32_t size = sheet->width / 4;
        Image icon{size, size, std::vector<std::uint8_t>(std::size_t{size} * size * 4)};
        const std::uint32_t x0 = (cell % 4) * size, y0 = (cell / 4) * size;
        for (std::uint32_t y = 0; y < size; ++y)
            std::memcpy(icon.rgba.data() + std::size_t{y} * size * 4,
                        sheet->rgba.data() + (std::size_t{y0 + y} * sheet->width + x0) * 4,
                        std::size_t{size} * 4);
        return icon;
    } else if (key.starts_with("icon/")) {
        int frame = 0;
        const auto name = split_frame(key.substr(5), frame);
        path = ItemIconPath(host, name, frame);
    } else if (key.starts_with("block/")) {
        int variant = 0;
        const auto name = split_frame(key.substr(6), variant);
        path = BlockIconPath(host, name, variant);
    } else if (key.starts_with("item/")) {
        int frame = 0;
        auto name = split_frame(key.substr(5), frame);
        if (const auto colon = name.find(':'); colon != std::string_view::npos)
            name = name.substr(colon + 1);
        path = ItemIconPath(host, name, frame);
        if (!path)
            path = BlockIconPath(host, name, frame);
        if (!path) {
            for (const std::string& candidate : LegacyNames(name)) {
                path = ItemIconPath(host, candidate, frame);
                if (!path)
                    path = BlockIconPath(host, candidate, frame);
                if (!path)
                    path = TextureByBasename(host, candidate);
                if (path)
                    break;
            }
        }
    }
    if (!path)
        return std::nullopt;
    std::string overlay;
    if (const auto bar = path->find('|'); bar != std::string::npos) {
        overlay = path->substr(bar + 1);
        path->erase(bar);
    }
    const auto bytes = ReadTexture(host, *path);
    if (!bytes)
        return std::nullopt;
    auto image = DecodeImage(bytes->data(), bytes->size());
    // overlay_color: the texture's alpha channel marks the part the game tints (grass); the
    // result is opaque, untinted outside the mask.
    if (image && overlay.size() == 7 && overlay[0] == '#') {
        const unsigned long c = std::strtoul(overlay.c_str() + 1, nullptr, 16);
        const unsigned tr = (c >> 16) & 0xFF, tg = (c >> 8) & 0xFF, tb = c & 0xFF;
        for (std::size_t i = 0; i + 3 < image->rgba.size(); i += 4) {
            const unsigned a = image->rgba[i + 3];
            const unsigned r = image->rgba[i], g = image->rgba[i + 1], b = image->rgba[i + 2];
            image->rgba[i] = static_cast<std::uint8_t>((r * (255 - a) + r * tr / 255 * a) / 255);
            image->rgba[i + 1] = static_cast<std::uint8_t>((g * (255 - a) + g * tg / 255 * a) / 255);
            image->rgba[i + 2] = static_cast<std::uint8_t>((b * (255 - a) + b * tb / 255 * a) / 255);
            image->rgba[i + 3] = 255;
        }
    }
    // Animated textures are vertical flipbooks (magma, sea lantern, clock): keep the first frame.
    if (image && image->width > 0 && image->height > image->width &&
        image->height % image->width == 0) {
        image->rgba.resize(static_cast<std::size_t>(image->width) * image->width * 4);
        image->height = image->width;
    }
    return image;
}

std::optional<Font> Library::LoadFont(const EdenDsmodHostApi& host, std::string_view name) const {
    const auto sheet = LoadIcon(host, Concat("font/", name));
    if (!sheet || sheet->width < 16 || sheet->width != sheet->height || sheet->width % 16 != 0)
        return std::nullopt;
    const std::uint32_t cell = sheet->width / 16;
    const auto ink = [&](std::uint32_t x, std::uint32_t y) {
        return sheet->rgba[(static_cast<std::size_t>(y) * sheet->width + x) * 4 + 3] != 0;
    };
    // Printable ASCII. The host scales a glyph by (5 * text_scale) / line_height: a line height
    // of 5/8 cell keeps that ratio a whole number for the 8-pixel sheet, so the pixels stay
    // square, and the 7 rows above the baseline stand 1.4x as tall as the built-in font.
    Font font;
    font.first_codepoint = 0x20;
    font.line_height = cell * 5 / 8;
    for (std::uint32_t cp = 0x20; cp < 0x7F; ++cp) {
        const std::uint32_t cx = (cp % 16) * cell, cy = (cp / 16) * cell;
        std::uint32_t width = 0;
        for (std::uint32_t x = cell; x-- > 0 && width == 0;)
            for (std::uint32_t y = 0; y < cell; ++y)
                if (ink(cx + x, cy + y)) {
                    width = x + 1;
                    break;
                }
        EdenDsmodFontGlyph g{};
        g.x = static_cast<std::uint16_t>(cx);
        g.y = static_cast<std::uint16_t>(cy);
        g.w = static_cast<std::uint16_t>(width);
        g.h = static_cast<std::uint16_t>(cell);
        g.bearing_x = 0;
        g.bearing_y = static_cast<std::int16_t>(cell * 7 / 8);
        // A space has no ink; the game advances it by half a cell.
        g.advance = static_cast<std::uint16_t>(cp == ' ' ? cell / 2 : width + cell / 8);
        font.glyphs.push_back(g);
    }
    return font;
}

std::vector<std::string> Library::LegacyNames(std::string_view raw) {
    // Minecraft 1.2.x item objects carry their registry name, which differs from the texture
    // keys for many items: block items are "tile.<block>", some names are camelCase, and a set
    // of items use a different word order in their texture names.
    std::string name{raw};
    for (const char* prefix : {"tile.", "item."})
        if (name.rfind(prefix, 0) == 0)
            name.erase(0, std::strlen(prefix));
    std::string snake;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (std::isupper(static_cast<unsigned char>(c))) {
            if (i > 0 && snake.back() != '_')
                snake.push_back('_');
            snake.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            snake.push_back(c);
        }
    }
    static const std::map<std::string, std::string> aliases{
        {"apple_enchanted", "apple_golden"},   {"golden_apple", "apple_golden"},
        {"golden_carrot", "carrot_golden"},    {"baked_potato", "potato_baked"},
        {"poisonous_potato", "potato_poisonous"}, {"speckled_melon", "melon_speckled"},
        {"beef", "beef_raw"},                  {"cooked_beef", "beef_cooked"},
        {"chicken", "chicken_raw"},            {"cooked_chicken", "chicken_cooked"},
        {"porkchop", "porkchop_raw"},          {"cooked_porkchop", "porkchop_cooked"},
        {"rabbit", "rabbit_raw"},              {"cooked_rabbit", "rabbit_cooked"},
        {"mutton_raw", "mutton_raw"},          {"mutton_cooked", "mutton_cooked"},
        {"wheat_seeds", "seeds_wheat"},        {"pumpkin_seeds", "seeds_pumpkin"},
        {"melon_seeds", "seeds_melon"},        {"beetroot_seeds", "seeds_beetroot"},
        {"book", "book_normal"},               {"writable_book", "book_writable"},
        {"written_book", "book_written"},      {"enchanted_book", "book_enchanted"},
        {"bow", "bow_standby"},                {"brewing_stand_block", "brewing_stand"},
        {"minecart", "minecart_normal"},       {"chest_minecart", "minecart_chest"},
        {"tnt_minecart", "minecart_tnt"},      {"hopper_minecart", "minecart_hopper"},
        {"command_block_minecart", "minecart_command_block"},
        {"clock", "clock_item"},               {"compass", "compass_item"},
        {"concrete_powder", "concretePowder"}, {"dye", "dye_powder"},
        {"empty_map", "map_empty"},            {"map", "map_filled"},
        {"fermented_spider_eye", "spider_eye_fermented"},
        {"glass_bottle", "potion_bottle_empty"}, {"potion", "potion_bottle_drinkable"},
        {"splash_potion", "potion_bottle_splash"}, {"lingering_potion", "potion_bottle_lingering"},
        {"horsearmordiamond", "diamond_horse_armor"}, {"horsearmorgold", "gold_horse_armor"},
        {"horsearmoriron", "iron_horse_armor"}, {"horsearmorleather", "leather_horse_armor"},
        {"redstone", "redstone_dust"},         {"slime_ball", "slimeball"},
    };
    std::vector<std::string> out;
    auto add = [&](std::string n) {
        if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end())
            out.push_back(std::move(n));
    };
    for (const std::string& n : {name, snake}) {
        if (const auto a = aliases.find(n); a != aliases.end())
            add(a->second);
    }
    add(name);
    add(snake);
    for (const std::string& n : {name, snake}) {
        if (n.rfind("golden_", 0) == 0)
            add("gold_" + n.substr(7));
        if (n.rfind("wooden_", 0) == 0)
            add("wood_" + n.substr(7));
    }
    return out;
}

std::optional<std::string> Library::TextureByBasename(const EdenDsmodHostApi& host,
                                                      std::string_view name) const {
    std::lock_guard lock{mutex};
    for (const char* dir : {"textures/items/", "textures/blocks/"}) {
        std::string rel{dir};
        rel.append(name);
        if (auto path = FindTextureFile(host, rel))
            return path;
    }
    return std::nullopt;
}

} // namespace mc_assets
