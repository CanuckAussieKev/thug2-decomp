// NOT ORIGINAL - the system's Vulkan loader opened through volk (vulkan_loader.h).
#include "gpu_vulkan/vulkan_loader.h"

#include <cstdint>
#include <volk.h>

#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace render::vulkan {

bool open_loader()
{
    if (volkInitialize() == VK_SUCCESS) {
        return true;
    }
#ifdef __APPLE__
    // NOT ORIGINAL (macOS port) - volk opens the loader by name ("libvulkan.dylib", which macOS doesn't search the
    // package managers' folders for) and then /usr/local/lib's: on Apple Silicon the loader and MoltenVK are Homebrew's,
    // under /opt/homebrew/lib. Each candidate is opened here and volk handed its entry point.
    constexpr const char* kCandidates[] = {
        "/opt/homebrew/lib/libvulkan.dylib",
        "/opt/homebrew/lib/libMoltenVK.dylib",
        "/usr/local/lib/libvulkan.dylib",
        "/usr/local/lib/libMoltenVK.dylib",
    };
    for (const char* candidate : kCandidates) {
        void* module = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
        if (module == nullptr) {
            continue;
        }
        auto get_instance_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr"));
        if (get_instance_proc != nullptr) {
            volkInitializeCustom(get_instance_proc);
            return true;
        }
    }
#endif
    return false;
}

uint32_t loader_version() { return volkGetInstanceVersion(); }

} // namespace render::vulkan
