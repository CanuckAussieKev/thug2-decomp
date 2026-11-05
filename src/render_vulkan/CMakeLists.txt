# The Vulkan graphics backend (src/render_vulkan; docs/design/vulkan.md): the second implementation of the engine's
# graphics interfaces, beside Direct3D 9's (src/render_pc over src/gpu_d3d9). It doesn't draw yet: it opens the system's Vulkan loader
# and makes the context (instance, device, swapchain) that src/tests/vulkan_context_check.cpp tries, and generates and
# compiles the fixed-function emulation's shaders (ff_key.h, ff_shaders.h, glsl_compiler.h) that
# src/tests/ff_shader_check.cpp checks. It includes the engine and the layers below it (tools/layers.ts checks it);
# nothing below app/ includes or links it, and only it sees the Vulkan and glslang headers (tools/vulkan_headers.test.ts).
add_library(thug2_render_vulkan STATIC
    vulkan_loader.cpp
    vulkan_context.cpp
    ff_key.cpp
    ff_shaders.cpp
    glsl_compiler.cpp
)
# volk: the Vulkan loader, opened at run time (no import library) - public, as the context's header declares Vulkan's
# functions through it; vulkan_memory_allocator: device memory (header only); sdl2: the window's Vulkan surface;
# glslang (+ its default resource limits): GLSL to SPIR-V at run time
target_link_libraries(thug2_render_vulkan PUBLIC thug2_engine volk
    PRIVATE vulkan_memory_allocator sdl2 glslang glslang-default-resource-limits)
