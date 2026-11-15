// NOT ORIGINAL - this machine's capabilities for the settings model (settings_capabilities.h).

#include "app/settings_capabilities.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/no_destroy.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
#include "engine/file/data_overlay.h"
#include "engine/file/disk.h"
#include "engine/gfx/texture_sets.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#ifdef THUG2_VULKAN
#include "gpu_vulkan/gpu_vulkan.h"
#endif

namespace app {

namespace {

constexpr std::string_view kSurfaceMapSuffix = ".pbr";                  // (formats/surface_map_format.h)
constexpr const char* kMaterialLibrary = "thug2-decomp-materials.t2dm"; // (gpu_vulkan/vulkan_detail.h)
constexpr int kDeepest = 4; // (Data\models\<group>\<model>\...: the deepest a surface map lies)

// The files and folders of a folder (its listing's names, "." and ".." left out).
class Listing final : public fs::FoundVisitor {
public:
    bool found(const fs::Found& file) override
    {
        const std::string_view name(file.name);
        if (name != "." && name != "..") {
            (file.folder ? folders : files).emplace_back(name);
        }
        return true;
    }
    std::vector<std::string> folders;
    std::vector<std::string> files;
};

// Whether a surface-map file (a texture dictionary's name + ".pbr") is in `folder` or below it - where tools/pbr_maps
// writes them: Data\levels, Data\models and the data overlay's levels.
bool has_surface_maps(const std::string& folder, int depth)
{
    Listing listing;
    if (!fs::disk().list((folder + "\\*").c_str(), listing)) {
        return false;
    }
    if (std::ranges::any_of(listing.files, [](const std::string& file) { return file.ends_with(kSurfaceMapSuffix); })) {
        return true;
    }
    return depth > 0 && std::ranges::any_of(listing.folders, [&folder, depth](const std::string& sub) {
               std::string path = folder;
               path += "\\";
               path += sub;
               return has_surface_maps(path, depth - 1);
           });
}

FoundCapabilities find()
{
    FoundCapabilities found;
    config::Capabilities& caps = found.caps;
    constexpr size_t kPointer64 = 8;
    caps.x64 = sizeof(void*) == kPointer64;
#ifdef THUG2_VULKAN
    const int index = build_info::get_int(build_info::kGraphics, "vulkan_device", -1);
    const render::vulkan::Usable usable = render::vulkan::usable(index);
    found.device = usable.device;
    found.problem = usable.usable ? std::string() : usable.problem;
    caps.vulkan = usable.usable;
    caps.ray_queries = usable.usable && usable.ray_queries;
    caps.vendor = usable.vendor_id;
    caps.rtx = usable.vendor_id == config::kVendorNvidia ? config::rtx_series(usable.device) : 0;
    caps.gpu = usable.device;
#else
    found.problem = "this build was made without the Vulkan library (THUG2_VULKAN)";
#endif
    const std::string game = build_info::executable_directory();
    caps.surface_maps = has_surface_maps(game + "Data\\levels", 1) || has_surface_maps(game + "Data\\models", kDeepest) ||
                        has_surface_maps(frontend::data_overlay::get_folder() + "\\levels", 1);
    caps.material_library = fs::disk().kind((game + kMaterialLibrary).c_str()) == fs::PathKind::kFile;
    caps.height_maps = false; // (none are made yet: rt_displacement shows its test pattern alone)
    caps.texture_sets = render::texture_sets::installed_sets();
    caps.delit_texture_sets = render::texture_sets::delit_sets();
    get_log("settings: capabilities - %s%s, ray tracing %s, %s, surface maps %s, material library %s, %zu texture set(s) "
            "(%zu de-lit)",
            caps.gpu.empty() ? "no Vulkan device" : caps.gpu.c_str(),
            caps.rtx > 0 ? (" (RTX " + std::to_string(caps.rtx) + " series)").c_str() : "",
            caps.ray_queries ? "possible" : "not possible", caps.x64 ? "64-bit" : "32-bit",
            caps.surface_maps ? "found" : "none", caps.material_library ? "found" : "none", caps.texture_sets.size(),
            caps.delit_texture_sets.size());
    return found;
}

} // namespace

const FoundCapabilities& find_capabilities()
{
    static core::NoDestroy<FoundCapabilities> state;
    static bool made = false;
    if (!made) {
        made = true;
        state.get() = find();
        config::set_capabilities(state.get().caps);
    }
    return state.get();
}

} // namespace app
