# The Direct3D 9 graphics backend (src/render_d3d9): the original's PC platform layer - the device and its frame, the
# scenes, meshes, textures, materials and shaders, the screen effects, the 2D and text, the movies' frames, the loading
# screen - and the vertex and index buffers. It implements render::Device (engine/gfx/render_device.h); app/ installs it
# (app/main.cpp). It includes the engine and the layers below it (tools/layers.ts checks it); nothing below app/ includes
# or links it, so the engine builds without Direct3D. The sources keep their order in the engine's list before the move.
add_library(thug2_render_d3d9 STATIC
    screenshot_capture.cpp
    flash.cpp
    text.cpp
    device.cpp
    movie.cpp
    loading_bar.cpp
    loading_console_panel.cpp
    draw_frame.cpp
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
    d3d9_state.cpp
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
)
# imgui: the loading console's panel (loading_console_panel.cpp: its font atlas)
target_link_libraries(thug2_render_d3d9 PUBLIC thug2_engine PRIVATE imgui)
