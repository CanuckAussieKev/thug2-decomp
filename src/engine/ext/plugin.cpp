// NOT ORIGINAL - the plugins' loader (plugin.h).

#include "engine/ext/plugin.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "base/core/no_destroy.h"
#include "engine/file/disk.h"
#include "platform/crt.h"
#include "platform/run_log.h"

namespace ext {

namespace {

// A plugin loaded: its slot and its extension.
struct LoadedPlugin {
    std::string slot;
    Extension* extension = nullptr;
};

// NOT ORIGINAL grouping - the plugins loaded at start-up. Made on first use, never destroyed (plugins()).
struct Plugins {
    std::vector<LoadedPlugin> loaded_;
    bool done_ = false;
};

Plugins& plugins()
{
    static core::NoDestroy<Plugins> state;
    return state.get();
}

// The DLLs a listing of plugins\*.dll finds.
class DllNames final : public fs::FoundVisitor {
public:
    bool found(const fs::Found& file) override
    {
        if (!file.folder) {
            names_.emplace_back(file.name);
        }
        return true;
    }
    std::vector<std::string> names_;
};

bool same_build(const PluginBuild& plugin)
{
    return plugin.compiler == kThisBuild.compiler && plugin.compiler_version == kThisBuild.compiler_version &&
           plugin.pointer_bytes == kThisBuild.pointer_bytes && plugin.debug == kThisBuild.debug;
}

const char* compiler_name(Compiler compiler)
{
    switch (compiler) {
    case Compiler::kMsvc:
        return "MSVC";
    case Compiler::kClang:
        return "Clang";
    case Compiler::kGcc:
        return "GCC";
    case Compiler::kOther:
        return "another compiler";
    }
    return "an unknown compiler";
}

constexpr uint32_t kBitsPerByte = 8U;

void log_build_mismatch(const std::string& file, const PluginBuild& plugin)
{
    get_log("plugins: %s was built by %s %u, %u-bit, %s - the game by %s %u, %u-bit, %s: not loaded (build it with the "
            "game's compiler and configuration)",
            file.c_str(), compiler_name(plugin.compiler), plugin.compiler_version, plugin.pointer_bytes * kBitsPerByte,
            plugin.debug != 0U ? "Debug" : "Release", compiler_name(kThisBuild.compiler), kThisBuild.compiler_version,
            kThisBuild.pointer_bytes * kBitsPerByte, kThisBuild.debug != 0U ? "Debug" : "Release");
}

// One plugin: loaded (through crt::library_function: the thread's x87 word kept around LoadLibrary), its entry called
// and its info checked.
void load_one(const std::string& file)
{
    const std::string path = game_folder_path(("plugins\\" + file).c_str());
    const auto entry = crt::library_function<PluginEntry>(crt::Dll{.file = path.c_str()}, kPluginEntryName);
    if (entry == nullptr) {
        get_log("plugins: %s couldn't be loaded, or has no %s - not a plugin of this game", file.c_str(),
                kPluginEntryName);
        return;
    }
    const PluginInfo* info = entry();
    if (info == nullptr) {
        get_log("plugins: %s gave no plugin info - not loaded", file.c_str());
        return;
    }
    if (info->api_version != kPluginApiVersion) {
        get_log("plugins: %s is for API version %u, the game's is %u - not loaded (a build of it for this game is "
                "needed)",
                file.c_str(), info->api_version, kPluginApiVersion);
        return;
    }
    if (!same_build(info->build)) {
        log_build_mismatch(file, info->build);
        return;
    }
    if (info->slot == nullptr || info->extension == nullptr) {
        get_log("plugins: %s has no extension - not loaded", file.c_str());
        return;
    }
    const auto taken = std::ranges::find(plugins().loaded_, std::string(info->slot), &LoadedPlugin::slot);
    if (taken != plugins().loaded_.end()) {
        get_log("plugins: %s is another %s - not loaded (one plugin per slot)", file.c_str(), info->slot);
        return;
    }
    plugins().loaded_.push_back({.slot = info->slot, .extension = info->extension});
    get_log("plugins: %s loaded - %s (slot %s)", file.c_str(), info->name != nullptr ? info->name : "?", info->slot);
}

} // namespace

void load_plugins()
{
    if (plugins().done_) {
        return;
    }
    plugins().done_ = true;
    DllNames dlls;
    fs::disk().list(game_folder_path("plugins\\*.dll").c_str(), dlls);
    // (in name order: the listing's is the file system's)
    std::ranges::sort(dlls.names_);
    for (const std::string& file : dlls.names_) {
        load_one(file);
    }
    if (dlls.names_.empty()) {
        get_log("plugins: none (no plugins\\*.dll next to the exe)");
    }
}

Extension* plugin_at(const char* slot)
{
    const auto found = std::ranges::find_if(plugins().loaded_, [slot](const LoadedPlugin& plugin) {
        return std::strcmp(plugin.slot.c_str(), slot) == 0;
    });
    return found != plugins().loaded_.end() ? found->extension : nullptr;
}

} // namespace ext
