# The Direct3D 9 Gpu (src/gpu_d3d9): the one place that talks to Direct3D 9 and D3DX9. It implements render::Gpu
# (gpu_d3d9.cpp: each call is one Direct3D call or a few), render::GpuContext (d3d9_context.cpp: the instance, the device,
# the present parameters, the caps, the reset), render::UiBackend (d3d9_ui_backend.cpp: Dear ImGui's SDL3 and Direct3D 9
# backends) and its D3DX wrapper (d3dx.h / d3dx.cpp: the pixel shaders' assembly and the front buffer's JPEG, which
# the Gpu's create_pixel_shader and save_surface_jpeg call); d3d9_state / d3d9_handles hold the Direct3D objects and the handle conversions. It includes
# the engine and the layers below it (tools/layers.ts checks it); app/ installs it and is the only library that includes or
# links it. The PC platform layer (src/render_pc) reaches Direct3D only through the interfaces this implements.
add_library(thug2_gpu_d3d9 STATIC
    gpu_d3d9.cpp
    d3d9_context.cpp
    d3d9_state.cpp
    d3d9_ui_backend.cpp
    d3dx.cpp
)
# imgui_backends: ImGui's SDL3 and Direct3D 9 backends (d3d9_ui_backend.cpp); d3dx9: D3DX9 (d3dx.cpp: the only file that
# calls it). Nothing of the PC platform layer (src/render_pc) is included or linked: that layer calls this one only
# through the engine's interfaces.
target_link_libraries(thug2_gpu_d3d9 PUBLIC thug2_engine PRIVATE imgui_backends d3dx9)
