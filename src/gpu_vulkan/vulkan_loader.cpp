// NOT ORIGINAL - the system's Vulkan loader opened through volk (vulkan_loader.h).
#include "gpu_vulkan/vulkan_loader.h"

#include <cstdint>
#include <volk.h>

namespace render::vulkan {

bool open_loader()
{
    return volkGetInstanceVersion() != 0 || volkInitialize() == VK_SUCCESS;
}

uint32_t loader_version()
{
    return volkGetInstanceVersion();
}

} // namespace render::vulkan
