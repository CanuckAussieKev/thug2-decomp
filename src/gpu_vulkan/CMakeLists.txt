# The Vulkan graphics backend (src/gpu_vulkan; docs/design/vulkan.md): the second implementation of the engine's
# graphics interfaces, beside Direct3D 9's (src/render_pc over src/gpu_d3d9), chosen with renderer=vulkan. It opens the
# system's Vulkan loader and makes the context (instance, device, swapchain) that src/tests/vulkan_context_check.cpp tries;
# it generates and compiles the fixed-function emulation's shaders (ff_key.h, ff_shaders.h, glsl_compiler.h) that
# src/tests/ff_shader_check.cpp checks; and it is Vulkan's render::Gpu and render::GpuContext (gpu_vulkan.h): the resources
# (vulkan_formats, vulkan_memory, vulkan_ring, vulkan_samplers, vulkan_jpeg), and the draws - the pixel shaders recognised
# (vulkan_pixel_program), the key and uniforms from the state block (vulkan_draw_state), the pipelines and their caches
# (vulkan_pipelines), the frames in flight (vulkan_frames), the passes, draws, clears and present (vulkan_draws) - which
# src/tests/vulkan_gpu_check.cpp checks; with ray tracing, the material table and bindless textures (vulkan_rt_materials,
# its shaders compiled by vulkan_rt_glsl) that src/tests/vulkan_rt_materials_check.cpp checks. It includes the engine and the layers below it (tools/layers.ts checks it);
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
    vulkan_pixel_program.cpp
    vulkan_draw_state.cpp
    vulkan_pipelines.cpp
    vulkan_frames.cpp
    vulkan_draws.cpp
    vulkan_gpu.cpp
    vulkan_gpu_context.cpp
    vulkan_ui_backend.cpp
    vulkan_ui_windows.cpp
    vulkan_rt_scene.cpp
    vulkan_rt_shadows.cpp
    vulkan_rt_effects.cpp
    vulkan_rt_reflections.cpp
    vulkan_rt_fog.cpp
    vulkan_rt_sky.cpp
    vulkan_rt_sun.cpp
    vulkan_rt_sky_pass.cpp
    vulkan_rt_clouds.cpp
    vulkan_rt_glsl.cpp
    vulkan_rt_materials.cpp
    vulkan_hdr.cpp
    vulkan_motion.cpp
    vulkan_upscale.cpp
    vulkan_upscale_fsr.cpp
    vulkan_upscale_dlss.cpp
)
# volk: the Vulkan loader, opened at run time (no import library) - public, as the context's header declares Vulkan's
# functions through it; vulkan_memory_allocator: device memory (VMA, compiled once: cmake/vulkan_libraries.cmake) - public,
# as the memory's header declares its handles; sdl3: the window's Vulkan surface and the display's size; glslang (+ its
# default resource limits): GLSL to SPIR-V at run time; stb_image_write: the screen shot command's JPEG; imgui_backends:
# ImGui's SDL3 platform backend (vulkan_ui_backend.cpp - the Gpu draws ImGui's lists itself, so no renderer backend)
target_link_libraries(thug2_gpu_vulkan PUBLIC thug2_engine volk vulkan_memory_allocator
    PRIVATE sdl3 glslang glslang-default-resource-limits stb_image_write imgui_backends)
# NOT ORIGINAL - the upscalers' headers (cmake/upscalers.cmake; their DLLs are loaded at run time): FSR's C API, and
# Streamline's (DLSS)
if(THUG2_FSR)
    target_link_libraries(thug2_gpu_vulkan PRIVATE fsr_sdk)
endif()
if(THUG2_DLSS)
    target_link_libraries(thug2_gpu_vulkan PRIVATE streamline_sdk)
endif()
