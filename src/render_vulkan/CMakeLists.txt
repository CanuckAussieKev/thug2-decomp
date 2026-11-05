# The Vulkan graphics backend (src/render_vulkan; docs/design/vulkan.md): the second implementation of the engine's
# graphics interfaces, beside Direct3D 9's (src/render_d3d9). It doesn't draw yet: it opens the system's Vulkan loader
# and makes the context (instance, device, swapchain) that src/tests/vulkan_context_check.cpp tries. It includes the
# engine and the layers below it (tools/layers.ts checks it); nothing below app/ includes or links it, and only it sees
# the Vulkan headers (tools/vulkan_headers.test.ts).
add_library(thug2_render_vulkan STATIC
    vulkan_loader.cpp
    vulkan_context.cpp
    ff_key.cpp
)
# volk: the Vulkan loader, opened at run time (no import library) - public, as the context's header declares Vulkan's
# functions through it; vulkan_memory_allocator: device memory (header only); sdl2: the window's Vulkan surface
target_link_libraries(thug2_render_vulkan PUBLIC thug2_engine volk PRIVATE vulkan_memory_allocator sdl2)
