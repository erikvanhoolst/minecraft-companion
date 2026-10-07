// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the exported module ABI with no running world or game assets.
#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct State {
    std::map<std::string, std::string> texts;
    std::map<std::string, std::int64_t> ints;
    std::vector<std::uint8_t> pixels;
    bool error{};
};
int main(int argc, char** argv) {
    State state;
    EdenDsmodHostApi host{};
    host.abi_version = EDEN_DSMOD_MODULE_ABI_VERSION;
    host.abi_hash = EDEN_DSMOD_MODULE_ABI_HASH;
    host.struct_size = sizeof(host);
    host.title_id = UINT64_C(0x0100D71004694000);
    const std::string hex = "D8B7E605E809E80C76FA3BD670FAB5BA00000000000000000000000000000000";
    for (std::size_t i = 0; i < 32; ++i) host.build_id[i] = std::stoul(hex.substr(i * 2, 2), nullptr, 16);
    host.userdata = &state;
    host.is_mapped = [](void*, std::uint64_t, std::uint64_t) -> EdenDsmodBool { return EDEN_DSMOD_FALSE; };
    host.read_memory = [](void*, std::uint64_t, void*, std::size_t) -> EdenDsmodBool { return EDEN_DSMOD_FALSE; };
    host.publish_i64 = [](void* p, const char* key, std::int64_t value) { static_cast<State*>(p)->ints[key] = value; };
    host.publish_address = [](void*, const char*, std::uint64_t) {};
    host.publish_f64 = [](void*, const char*, double) {};
    host.publish_text = [](void* p, const char* key, const char* value) { static_cast<State*>(p)->texts[key] = value; };
    host.get_i64 = [](void* p, const char* key, std::int64_t fallback) {
        const auto& values = static_cast<State*>(p)->ints;
        auto it = values.find(key); return it == values.end() ? fallback : it->second;
    };
    host.get_f64 = [](void*, const char*, double fallback) { return fallback; };
    host.get_text = [](void* p, const char* key) {
        auto& values = static_cast<State*>(p)->texts;
        auto it = values.find(key); return it == values.end() ? "" : it->second.c_str();
    };
    host.log = [](void* p, std::uint32_t level, const char*) { if (level == EDEN_DSMOD_LOG_ERROR) static_cast<State*>(p)->error = true; };
    const auto dir = std::filesystem::temp_directory_path() / ("mc-notebook-module-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto config = nlohmann::json{{"data_directory", dir.string()}}.dump();
    const auto* module = eden_dsmod_get_module(EDEN_DSMOD_MODULE_ABI_VERSION, EDEN_DSMOD_MODULE_ABI_HASH);
    const auto* extensions = eden_dsmod_get_extensions(EDEN_DSMOD_EXT_VERSION, EDEN_DSMOD_EXT_HASH);
    Check(module && extensions, "exported ABI tables available");
    auto* instance = module->create(&host, config.c_str());
    Check(instance, "module accepts supported build");
    module->sample(instance, &host);
    Check(!state.error && state.ints["inv.ready"] == 0 && state.texts["notebook.name"] == "Lever lamp",
          "notebook publishes through real module even without a world");
    const auto key = state.texts["notebook.image"];
    const auto sink = [](void* p, std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels, std::size_t size) {
        Check(width == 512 && height == 384 && size == 512 * 384 * 4, "image sink receives full schematic");
        static_cast<State*>(p)->pixels.assign(pixels, pixels + size);
    };
    Check(extensions->load_image(instance, &host, key.c_str(), &state, sink) == EDEN_DSMOD_TRUE && !state.pixels.empty(),
          "published image key resolves through module:mc router");
    if (argc == 2) {
        std::ofstream out(argv[1], std::ios::binary); out << "P6\n512 384\n255\n";
        for (std::size_t i = 0; i < state.pixels.size(); i += 4) out.write(reinterpret_cast<const char*>(state.pixels.data() + i), 3);
    }
    Check(extensions->on_action(instance, "notebook_steps", 0) == EDEN_DSMOD_TRUE &&
          extensions->on_action(instance, "notebook_toggle", 0) == EDEN_DSMOD_TRUE, "touch actions route through exported extension");
    module->sample(instance, &host);
    Check(state.texts["notebook.progress"] == "1 / 4 steps done", "touch changes publish back to manifest");
    module->destroy(instance);
    instance = module->create(&host, config.c_str());
    Check(instance, "module reload succeeds");
    module->sample(instance, &host);
    Check(state.texts["notebook.progress"] == "1 / 4 steps done", "progress survives complete module teardown and recreation");
    module->destroy(instance);
    std::filesystem::remove_all(dir);
    std::puts("notebook module integration passed");
}
