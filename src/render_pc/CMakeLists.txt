# The PC platform layer (src/render_pc), API-neutral: the original's device and its frame, the scenes, meshes, textures,
# materials and shaders, the screen effects, the 2D and text, the movies' frames, the loading screen, and the vertex and
# index buffers. It implements render::Device (engine/gfx/render_device.h) and reaches the graphics API only through
# render::Gpu and render::GpuContext (engine/gfx/gpu.h, gpu_context.h), which app/ installs from a Gpu library
# (src/gpu_d3d9 today, Vulkan later). No file of it includes a Direct3D header (tools/d3d_headers.test.ts). It includes the
# engine and the layers below it (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds
# without it, and it refers to nothing of a Gpu library (the pixel shaders' assembly and the screen shot's JPEG are Gpu
# operations).
add_library(thug2_render_pc STATIC
    screenshot_capture.cpp
    flash.cpp
    text.cpp
    device.cpp
    movie.cpp
    loading_bar.cpp
    loading_console_panel.cpp
    loading_dialog_panel.cpp
    draw_frame.cpp
    depth_lines_draw.cpp
    ghost_draw.cpp
    outline_draw.cpp
    game_view_frame.cpp
    wireframe.cpp
    view_mode.cpp
    billboard_quad.cpp
    pc_engine.cpp
    scene_load.cpp
    pc_font.cpp
    geom.cpp
    pc_lights.cpp
    primitive_states.cpp
    loading_screen.cpp
    pc_mesh.cpp
    pc_mesh_life.cpp
    pc_mesh_load.cpp
    pc_mesh_render.cpp
    pc_fog.cpp
    texture_splats.cpp
    shatter.cpp
    pc_model.cpp
    pc_particles.cpp
    pc_flexible_particles.cpp
    pc_primitive.cpp
    pc_scene.cpp
    pc_sprite.cpp
    pc_texture.cpp
    pc_texture_projection.cpp
    pc_viewport.cpp
    pc_weather.cpp
    pc_billboards.cpp
    pc_buffers.cpp
    pc_gamma.cpp
    pc_grass.cpp
    pc_instance.cpp
    mesh_pass.cpp
    pc_material.cpp
    pc_material_load.cpp
    render_state.cpp
    d3d9_device.cpp
    shadow_base.cpp
    mesh_list.cpp
    pixel_shaders.cpp
    simple_material.cpp
    screen_fx.cpp
    draw2d.cpp
    sub_mesh.cpp
    scene_render.cpp
    glow.cpp
    bitmap_texture.cpp
    texture_file.cpp
    graphics_options_d3d.cpp
    pc_d3d_resources.cpp
    vertex_buffers.cpp
    editor_support.cpp
)
# imgui: the loading console's panel (loading_console_panel.cpp: its font atlas) and the editor mode's loading dialog
# (loading_dialog_panel.cpp: its fonts and draw list)
target_link_libraries(thug2_render_pc PUBLIC thug2_engine PRIVATE imgui)
