# The Vulkan graphics backend (src/gpu_vulkan; docs/design/vulkan.md): the second implementation of the engine's
# graphics interfaces, beside Direct3D 9's (src/render_pc over src/gpu_d3d9). It doesn't draw yet: it opens the system's Vulkan loader
# and makes the context (instance, device, swapchain) that src/tests/vulkan_context_check.cpp tries; it generates and
# compiles the fixed-function emulation's shaders (ff_key.h, ff_shaders.h, glsl_compiler.h) that
# src/tests/ff_shader_check.cpp checks; and it is Vulkan's render::Gpu and render::GpuContext for the resources (gpu_vulkan.h:
# textures and their formats, buffers, surfaces, copies, queries, samplers - vulkan_formats, vulkan_memory, vulkan_ring,
# vulkan_samplers, vulkan_jpeg, vulkan_gpu, vulkan_gpu_context) that src/tests/vulkan_gpu_check.cpp checks. It includes the engine and the layers below it (tools/layers.ts checks it);
# nothing below app/ includes or links it, and only it sees the Vulkan and glslang headers (tools/vulkan_headers.test.ts).
add_library(thug2_gpu_vulkan STATIC
    vulkan_loader.cpp
    vulkan_context.cpp
    ff_key.cpp
    ff_shaders.cpp
    glsl_compiler.cpp
    vulkan_formats.cpp
    vulkan_memory.cpp
    vulkan_samplers.cpp
    vulkan_ring.cpp
    vulkan_jpeg.cpp
    vulkan_gpu.cpp
    vulkan_gpu_context.cpp
)
# volk: the Vulkan loader, opened at run time (no import library) - public, as the context's header declares Vulkan's
# functions through it; vulkan_memory_allocator: device memory (VMA, compiled once: cmake/vulkan_libraries.cmake) - public,
# as the memory's header declares its handles; sdl2: the window's Vulkan surface and the display's size; glslang (+ its
# default resource limits): GLSL to SPIR-V at run time; stb_image_write: the screen shot command's JPEG
target_link_libraries(thug2_gpu_vulkan PUBLIC thug2_engine volk vulkan_memory_allocator
    PRIVATE sdl2 glslang glslang-default-resource-limits stb_image_write)
