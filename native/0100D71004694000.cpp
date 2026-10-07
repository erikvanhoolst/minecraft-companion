// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Minecraft (Bedrock Edition, Switch) module: ABI glue.
//   mc_reader.{h,cpp}  live inventory reader (slots, counts, selected hotbar slot) and the
//                      player's health, hunger, armor and air
//   mc_assets.{h,cpp}  item and block icons and HUD sprites decoded from the player's romfs
//                      resource packs, and the game's language file
//   mc_names.{h,cpp}   item names as the game shows them
//   mc_map.{h,cpp}     the map tab: the chunks around the player, drawn like the game's map item
// Supported build: 53E6D516A4DA5CD0C49FCE555994DA196B63E9C1 (v1.26.13, patch version 148).

#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include "mc_assets.h"
#include "mc_debug.h"
#include "mc_map.h"
#include "mc_reader.h"
#include "mc_waypoints.h"
#include "mc_projects.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace {
using namespace dsmod_sdk::int_types;

constexpr u64 TitleId = UINT64_C(0x0100D71004694000);

EdenDsmodBool SupportsBuild(const char* build_id) {
    return mc_reader::SupportsBuildHex(build_id) ? EDEN_DSMOD_TRUE : EDEN_DSMOD_FALSE;
}

struct Module {
    Module(const EdenDsmodHostApi& api, const char* config)
        : host{api}, reader{api, config}, map{config}, waypoints{config}, projects{config} {}

    EdenDsmodBool LoadImage(const EdenDsmodHostApi* image_host, const char* key, void* receiver,
                            EdenDsmodImageSink sink) {
        if (!image_host || !key || !sink)
            return EDEN_DSMOD_FALSE;
        std::string_view k{key};
        if (k.starts_with("module:"))
            k.remove_prefix(7);
        if (!k.starts_with("mc:"))
            return EDEN_DSMOD_FALSE;
        k.remove_prefix(3);
        if (k.starts_with("death/trail/")) {
            const auto image = waypoints.LoadTrail(k);
            if (!image) return EDEN_DSMOD_FALSE;
            sink(receiver, image->width, image->height, image->rgba.data(), image->rgba.size());
            return EDEN_DSMOD_TRUE;
        }
        if (k == "waypoint/arrow") {
            const auto image = mc_waypoints::ArrowImage();
            sink(receiver, image.width, image.height, image.rgba.data(), image.rgba.size());
            return EDEN_DSMOD_TRUE;
        }
        if (k.starts_with("mapview/")) {
            const auto picture = map.Load(*image_host, k);
            if (!picture)
                return EDEN_DSMOD_FALSE;
            sink(receiver, picture->width, picture->height, picture->rgba.data(),
                 picture->rgba.size());
            return EDEN_DSMOD_TRUE;
        }
        if (!assets.EnsureLoaded(*image_host))
            return EDEN_DSMOD_FALSE; // romfs not ready yet; the host retries
        const auto image = assets.LoadIcon(*image_host, k);
        if (!image || image->rgba.empty())
            return EDEN_DSMOD_FALSE;
        sink(receiver, image->width, image->height, image->rgba.data(), image->rgba.size());
        return EDEN_DSMOD_TRUE;
    }

    // The manifest's `font` is the package file mc_font.txt, "MCFONT <sheet>": the metrics are
    // measured from the game's own font sheet, which is also the atlas (module:mc:font/<sheet>).
    EdenDsmodBool DecodeFont(const std::uint8_t* bytes, std::size_t size, void* receiver,
                             EdenDsmodFontSink sink) {
        std::string_view text{reinterpret_cast<const char*>(bytes), size};
        if (!text.starts_with("MCFONT "))
            return EDEN_DSMOD_FALSE;
        text.remove_prefix(7);
        const std::string_view sheet = text.substr(0, text.find_first_of(" \r\n"));
        if (sheet.empty() || !assets.EnsureLoaded(host))
            return EDEN_DSMOD_FALSE; // romfs not ready yet; the host retries
        const auto font = assets.LoadFont(host, sheet);
        if (!font || font->glyphs.empty())
            return EDEN_DSMOD_FALSE;
        sink(receiver, font->line_height, font->first_codepoint, font->glyphs.data(),
             static_cast<std::uint32_t>(font->glyphs.size()));
        return EDEN_DSMOD_TRUE;
    }

    EdenDsmodHostApi host; // create()'s host table, also used by decode_font
    mc_assets::Library assets;
    mc_reader::Reader reader;
    mc_map::Map map;
    mc_waypoints::Waypoints waypoints;
    mc_projects::Projects projects;
    mc_debug::Console console;
};

void* Create(const EdenDsmodHostApi* host, const char* config) {
    try {
        if (!dsmod_sdk::HostAbiMatches(host) || host->title_id != TitleId || !host->is_mapped ||
            !host->read_memory)
            return nullptr;
        if (!mc_reader::SupportsBuildId(host->build_id))
            return nullptr;
        return new Module{*host, config};
    } catch (...) {
        return nullptr;
    }
}
void Destroy(void* p) {
    try {
        delete static_cast<Module*>(p);
    } catch (...) {
    }
}
void SampleCallback(void* p, const EdenDsmodHostApi* host) {
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    try {
        if (p && host) {
            auto* m = static_cast<Module*>(p);
            m->reader.Sample(*host, &m->assets);
            m->map.Sample(*host, m->reader);
            m->waypoints.Sample(*host);
            m->projects.Sample(*host, m->reader.InventoryState());
            m->console.Tick(*host, m->reader, &m->assets);
        }
    } catch (...) {
        if (!reported.test_and_set() && host && host->log)
            host->log(host->userdata, EDEN_DSMOD_LOG_ERROR, "Minecraft DSMod sample failed");
    }
}
void TickCallback(void*, const EdenDsmodHostApi*) {}
void ConfigureCallback(void*, const EdenDsmodHostExtensions*) {}
EdenDsmodBool ActionCallback(void* p, const char* action, std::int64_t argument) {
    try {
        if (!p)
            return EDEN_DSMOD_FALSE;
        auto* m = static_cast<Module*>(p);
        return m->reader.OnAction(action, argument) || m->map.OnAction(action, argument) ||
                       m->waypoints.OnAction(action, argument) ||
                       m->projects.OnAction(action, argument)
                   ? EDEN_DSMOD_TRUE
                   : EDEN_DSMOD_FALSE;
    } catch (...) {
        return EDEN_DSMOD_FALSE;
    }
}
EdenDsmodBool DecodeFontCallback(void* p, const std::uint8_t* bytes, std::size_t size,
                                 void* receiver, EdenDsmodFontSink sink) {
    try {
        if (p && bytes && sink)
            return static_cast<Module*>(p)->DecodeFont(bytes, size, receiver, sink);
    } catch (...) {
    }
    return EDEN_DSMOD_FALSE;
}
EdenDsmodBool LoadImageCallback(void* p, const EdenDsmodHostApi* host, const char* key,
                                void* receiver, EdenDsmodImageSink sink) {
    try {
        if (p)
            return static_cast<Module*>(p)->LoadImage(host, key, receiver, sink);
    } catch (...) {
    }
    return EDEN_DSMOD_FALSE;
}

const EdenDsmodModuleApi ModuleApi{EDEN_DSMOD_MODULE_ABI_VERSION,
                                   sizeof(EdenDsmodModuleApi),
                                   0,
                                   EDEN_DSMOD_MODULE_ABI_HASH,
                                   TitleId,
                                   "Minecraft inventory DSMod",
                                   EDEN_DSMOD_CAP_EXTENSIONS | EDEN_DSMOD_CAP_ROMFS_READ,
                                   &SupportsBuild,
                                   &Create,
                                   &Destroy,
                                   &SampleCallback,
                                   &TickCallback};
const EdenDsmodModuleExtensions ModuleExtensions{
    EDEN_DSMOD_EXT_VERSION, sizeof(EdenDsmodModuleExtensions),
    EDEN_DSMOD_EXT_HASH,    &ConfigureCallback,
    &ActionCallback,        &LoadImageCallback};
const EdenDsmodFontExtensions FontExtensions{EDEN_DSMOD_FONT_EXT_VERSION,
                                             sizeof(EdenDsmodFontExtensions),
                                             EDEN_DSMOD_FONT_EXT_HASH, &DecodeFontCallback};
} // namespace

DSMOD_SDK_EXPORT_MODULE(ModuleApi)
DSMOD_SDK_EXPORT_EXTENSIONS(ModuleExtensions)
DSMOD_SDK_EXPORT_FONT_EXTENSIONS(FontExtensions)
