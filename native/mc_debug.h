// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Research console for the Minecraft module. Commands are read from the player-supplied file
// `user:mcdbg.txt` (<Eden data>/dualscreen/user/0100D71004694000/mcdbg.txt); results go to the
// host log, which is a file on every platform (the Android app's log folder can be pulled with
// adb). The file is re-read when its contents change, so a command runs once per edit.
//
//   dump <hexaddr> [len]        hex words at a guest address (len <= 0x800)
//   str <text> [max]            addresses holding the ASCII text (mapped alias + heap windows)
//   vt <hexoffset> [max]        objects whose vptr is main+<hexoffset>
//   ptr <hexaddr> [max]         8-byte words holding this pointer (static roots show as main+X)
//   item <hexaddr>              an Item object: strings found by libc++ layout at each 8-byte offset
//   inv                         the resolved inventory, slot 0 and its Item pointer chain
//   layout                      the active Layout table
//   items                       every Item object in memory (1.2.x: found by its "atlas.items"
//                               string), with the icon key the reader would use, whether the
//                               icon resolves and decodes, its description id and display name
//   player                      the local player object and its vtable
//   floats <hexaddr|player> [hexlen]
//                               4-byte words that read as game numbers (multiples of 0.25)
//   find <f32|i32|i16|u64> <value> [<hexaddr|player> <hexlen>] [max]
//                               a value in a range, or in every mapped page
//   strm <text> [max]           like str, in the main image (static data)
//   attr <hex main offset>      the AttributeInstances pointing at a static Attribute, flagging
//                               the one whose map the local player holds
//   amap [hexaddr]              a BaseAttributeMap's entries: name, instance, the six floats
//                               (default min/max/value, current min/max/value)
//   vecs <hexaddr|player> [hexlen]
//                               std::vector-shaped fields of an object
//   save <hexaddr|player> <hexlen> <file>
//                               desktop only: raw guest bytes to a file
//   snap <dir>                  desktop only: every mapped page as <dir>/<hexaddr>.bin runs of
//                               contiguous pages, plus main_<base>.bin, for offline analysis

#pragma once

#include <cstdint>
#include <string>

#include "core/mods/dsmod_module_abi.h"
#include "mc_assets.h"
#include "mc_reader.h"

namespace mc_debug {

class Console {
public:
    /// Call every tick; cheap unless the command file changed.
    void Tick(const EdenDsmodHostApi& host, const mc_reader::Reader& reader,
              mc_assets::Library* assets = nullptr);

private:
    void Run(const EdenDsmodHostApi& host, const mc_reader::Reader& reader,
             const std::string& line);
    mc_assets::Library* assets_{};
    std::uint64_t last_hash{};
    std::uint32_t tick{};
};

} // namespace mc_debug
