# The Direct3D 9 Gpu (src/gpu_d3d9): the one place that talks to Direct3D 9 and D3DX9. It implements render::Gpu
# (gpu_d3d9.cpp: each call is one Direct3D call or a few), render::GpuContext (d3d9_context.cpp: the instance, the device,
# the present parameters, the caps, the reset), render::UiBackend (d3d9_ui_backend.cpp: Dear ImGui's SDL2 and Direct3D 9
# backends) and the D3DX wrapper declared in render_pc/d3dx.h and d3dx_jpg.h (d3dx.cpp: the pixel shaders' assembly and
# the front buffer's JPEG); d3d9_state / d3d9_handles hold the Direct3D objects and the handle conversions. It includes
# the engine and the layers below it (tools/layers.ts checks it); app/ installs it and is the only library that includes or
# links it. The PC platform layer (src/render_pc) reaches Direct3D only through the interfaces this implements.
add_library(thug2_gpu_d3d9 STATIC
    gpu_d3d9.cpp
    d3d9_context.cpp
    d3d9_state.cpp
    d3d9_ui_backend.cpp
    d3dx.cpp
)
# imgui_backends: ImGui's SDL2 and Direct3D 9 backends (d3d9_ui_backend.cpp); d3dx9: D3DX9 (d3dx.cpp: the only file that
# calls it); thug2_render_pc: d3dx.cpp includes its d3dx.h and d3dx_jpg.h (they declare no Direct3D; render_pc calls what
# d3dx.cpp defines, so the two libraries refer to each other at link time and the exes link both).
target_link_libraries(thug2_gpu_d3d9 PUBLIC thug2_engine PRIVATE thug2_render_pc imgui_backends d3dx9)
