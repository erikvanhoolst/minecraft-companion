// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mc_zip.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

// stb_image's zlib decoder (implemented in mc_assets.cpp through STB_IMAGE_IMPLEMENTATION).
#include "third_party/stb_image.h"

namespace mc_zip {
namespace {

std::uint16_t U16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool ReadRange(const EdenDsmodHostApi& host, const std::string& path, std::uint64_t off,
               std::vector<std::uint8_t>& out, std::size_t size) {
    out.resize(size);
    return size == 0 ||
           host.read_romfs(host.userdata, path.c_str(), off, out.data(), size) == size;
}

std::string Normalise(std::string_view name) {
    std::string s{name};
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

} // namespace

bool Archive::Open(const EdenDsmodHostApi& host, std::string p) {
    entries.clear();
    path = std::move(p);
    if (!host.read_romfs)
        return false;
    const std::uint64_t size = host.read_romfs(host.userdata, path.c_str(), 0, nullptr, 0);
    if (size < 22)
        return false;
    // End of central directory record: within the last 64 KiB + 22 bytes.
    const std::uint64_t tail = std::min<std::uint64_t>(size, 0x10000 + 22);
    std::vector<std::uint8_t> buf;
    if (!ReadRange(host, path, size - tail, buf, static_cast<std::size_t>(tail)))
        return false;
    std::int64_t eocd = -1;
    for (std::int64_t i = static_cast<std::int64_t>(buf.size()) - 22; i >= 0; --i) {
        if (buf[i] == 'P' && buf[i + 1] == 'K' && buf[i + 2] == 5 && buf[i + 3] == 6) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return false;
    const std::uint8_t* e = buf.data() + eocd;
    const std::uint32_t count = U16(e + 10);
    const std::uint32_t cd_size = U32(e + 12);
    const std::uint32_t cd_offset = U32(e + 16);
    if (cd_offset + static_cast<std::uint64_t>(cd_size) > size)
        return false;
    std::vector<std::uint8_t> cd;
    if (!ReadRange(host, path, cd_offset, cd, cd_size))
        return false;
    std::size_t pos = 0;
    for (std::uint32_t i = 0; i < count && pos + 46 <= cd.size(); ++i) {
        const std::uint8_t* h = cd.data() + pos;
        if (!(h[0] == 'P' && h[1] == 'K' && h[2] == 1 && h[3] == 2))
            break;
        Entry en;
        en.method = U16(h + 10);
        en.compressed_size = U32(h + 20);
        en.uncompressed_size = U32(h + 24);
        const std::uint16_t name_len = U16(h + 28), extra_len = U16(h + 30), comment_len = U16(h + 32);
        en.local_header_offset = U32(h + 42);
        if (pos + 46 + name_len > cd.size())
            break;
        std::string name(reinterpret_cast<const char*>(h + 46), name_len);
        entries.emplace(Normalise(name), en);
        pos += 46 + name_len + extra_len + comment_len;
    }
    return !entries.empty();
}

bool Archive::Contains(std::string_view name) const {
    return entries.find(Normalise(name)) != entries.end();
}

std::optional<std::vector<std::uint8_t>> Archive::Read(const EdenDsmodHostApi& host,
                                                       std::string_view name) const {
    const auto it = entries.find(Normalise(name));
    if (it == entries.end() || !host.read_romfs)
        return std::nullopt;
    const Entry& en = it->second;
    std::vector<std::uint8_t> lh;
    if (!ReadRange(host, path, en.local_header_offset, lh, 30) || lh[0] != 'P' || lh[1] != 'K' ||
        lh[2] != 3 || lh[3] != 4)
        return std::nullopt;
    const std::uint64_t data = en.local_header_offset + 30 + U16(lh.data() + 26) + U16(lh.data() + 28);
    std::vector<std::uint8_t> raw;
    if (en.compressed_size > (64u << 20) || !ReadRange(host, path, data, raw, en.compressed_size))
        return std::nullopt;
    if (en.method == 0)
        return raw;
    if (en.method != 8)
        return std::nullopt;
    int out_len = 0;
    char* out = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(raw.data()),
                                                 static_cast<int>(raw.size()), &out_len);
    if (!out)
        return std::nullopt;
    std::vector<std::uint8_t> result(reinterpret_cast<std::uint8_t*>(out),
                                     reinterpret_cast<std::uint8_t*>(out) + out_len);
    std::free(out);
    if (result.size() != en.uncompressed_size)
        return std::nullopt;
    return result;
}

} // namespace mc_zip
