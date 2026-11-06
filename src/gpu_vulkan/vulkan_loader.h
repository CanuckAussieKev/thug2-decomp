#pragma once

#include <cstdint>

// NOT ORIGINAL - the system's Vulkan loader (vulkan-1.dll, installed with every Vulkan driver), opened at run time by
// volk (vulkan_loader.cpp): nothing links an import library, so a machine without Vulkan still starts the exe.
namespace render::vulkan {

// Opens the loader and loads its global functions; false when there is no Vulkan on this machine. Opening it again
// does nothing.
bool open_loader();

// The highest Vulkan version the loader offers instances (a VK_MAKE_API_VERSION number), 0 when it isn't open.
uint32_t loader_version();

} // namespace render::vulkan
