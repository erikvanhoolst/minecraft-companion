// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Minimal read-only ZIP access over the host's romfs reader: Minecraft 1.2.x ships its vanilla
// resource pack as romfs:/resource_packs/vanilla.zip (deflate entries, backslash paths).

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/mods/dsmod_module_abi.h"

namespace mc_zip {

struct Entry {
    std::uint64_t local_header_offset{};
    std::uint32_t compressed_size{};
    std::uint32_t uncompressed_size{};
    std::uint16_t method{}; // 0 stored, 8 deflate
};

class Archive {
public:
    /// Reads the central directory of `path` (a romfs path). False when it is not a zip.
    bool Open(const EdenDsmodHostApi& host, std::string path);
    bool IsOpen() const { return !entries.empty(); }
    /// Entry names are normalised to forward slashes.
    bool Contains(std::string_view name) const;
    std::optional<std::vector<std::uint8_t>> Read(const EdenDsmodHostApi& host,
                                                  std::string_view name) const;
    const std::string& Path() const { return path; }

private:
    std::string path;
    std::map<std::string, Entry> entries;
};

} // namespace mc_zip
