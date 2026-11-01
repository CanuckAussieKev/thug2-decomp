cmake_minimum_required(VERSION 3.25)
project(thug2_decomp LANGUAGES C CXX)

# The game is a 32-bit exe (its data, the GameSpy SDK and Bink are 32-bit).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with the x86 preset: the game is 32-bit.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# compile_commands.json in the build folder, for clang-tidy / clangd (.clang-tidy, docs/style-guide.md).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static CRT so the exe doesn't need a separate runtime installed next to the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(THUG2_GAME_DIR "C:/Games/THUG2/Game" CACHE PATH
    "The game folder of a THUG2 install (its Data folder) the built exe is copied into, if it exists (an install root steps into its Game folder)")

# --- SDL2 (PartyMod's window and input: src/platform/sdl_*.cpp, src/engine/pad_input/sdl_input.cpp) --------------
# The official 2.30.9 Visual C++ development package: the same version as the SDL2.dll PartyMod ships with (the
# game folder's SDL2.dll). Downloaded and checked against its SHA-256 at configure time; not stored in the repo.
include(FetchContent)
FetchContent_Declare(sdl2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip
    URL_HASH SHA256=8c91d91e5bcb997d062ec2b553c53832ebf95654d4aa35e8c02a954d4ce752ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sdl2)
# The package has its headers directly in include/; the code includes them as <SDL2/...>.
file(COPY "${sdl2_SOURCE_DIR}/include/" DESTINATION "${CMAKE_BINARY_DIR}/sdl2-include/SDL2")
add_library(sdl2 SHARED IMPORTED)
set_target_properties(sdl2 PROPERTIES
    IMPORTED_IMPLIB "${sdl2_SOURCE_DIR}/lib/x86/SDL2.lib"
    IMPORTED_LOCATION "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/sdl2-include")
# The package's SDL2.dll is byte-for-byte the one PartyMod ships (SHA-256 921e4c99...): copied into the game folder
# with the exe (below), so a game folder needs no PartyMod files. SDL's controller mappings, the same way: the
# gamecontrollerdb.txt PartyMod ships is SDL_GameControllerDB's at commit 8b2d1a7 (2022-09-06), zlib.
set(THUG2_CONTROLLER_DB "${CMAKE_BINARY_DIR}/gamecontrollerdb.txt")
if(NOT EXISTS "${THUG2_CONTROLLER_DB}")
    file(DOWNLOAD
        https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/8b2d1a791c8e5004c736e5137699e8e97ae558c5/gamecontrollerdb.txt
        "${THUG2_CONTROLLER_DB}"
        EXPECTED_HASH SHA256=ada0fe3998a5c7914b57ee15a3900f8b43acb604bf8314abca92231ebea057eb)
endif()

# --- D3DX9 --------------------------------------------------------------------------------------------------
# Microsoft's NuGet package Microsoft.DXSDK.D3DX 9.29.952.8 (the DirectX SDK June 2010's d3dx9_43.dll, import library
# and headers; its licence is summarised in THIRD_PARTY_NOTICES.md). A .nupkg is a zip: downloaded under a .zip name
# and checked against its SHA-256. src/engine/gfx/d3dx.cpp is the one place that calls it.
FetchContent_Declare(d3dx9
    URL https://www.nuget.org/api/v2/package/Microsoft.DXSDK.D3DX/9.29.952.8
    URL_HASH SHA256=ead0906ae8a26c18a7525da7490127a2110f7c58f18293738283e30e97c6ea4b
    DOWNLOAD_NAME Microsoft.DXSDK.D3DX.9.29.952.8.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(d3dx9)
set(THUG2_D3DX9_NATIVE "${d3dx9_SOURCE_DIR}/build/native")
add_library(d3dx9 SHARED IMPORTED)
set_target_properties(d3dx9 PROPERTIES
    IMPORTED_IMPLIB "${THUG2_D3DX9_NATIVE}/release/lib/x86/d3dx9.lib"
    IMPORTED_LOCATION "${THUG2_D3DX9_NATIVE}/release/bin/x86/D3DX9_43.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${THUG2_D3DX9_NATIVE}/include")

# --- Dear ImGui 1.91.9b (MIT, see THIRD_PARTY_NOTICES.md) -----------------------------------------------
# The level editor's UI layer (src/editor/level_editor_ui*.cpp): the core, the official SDL2 platform backend
# and the Direct3D 9 renderer backend. Only level_editor_ui_backend.cpp includes the backends' headers.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
    URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}
    PRIVATE "${CMAKE_BINARY_DIR}/sdl2-include/SDL2") # (the SDL2 backend includes <SDL.h>)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEFAULT_FONT)
target_link_libraries(imgui PRIVATE sdl2)
target_compile_options(imgui PRIVATE /W0)

# --- stb_image_write 1.16 (Sean Barrett, public domain or MIT, see THIRD_PARTY_NOTICES.md) ---------
# The screenshot key's PNG writer (src/platform/screenshot_files.cpp): the stb repository at commit 1ee679c (2021-07-12, stb_image_write's last change), headers only.
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/1ee679ca2ef753a528db5ba6801e1067b40481b8.tar.gz
    URL_HASH SHA256=be9842d207353c89c0b970d618030fe98a944ab3fe1374534cba1b8e934bfdd7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(stb)

# --- LZHAM 1.0 (Rich Geldreich; public domain, earlier releases MIT - see THIRD_PARTY_NOTICES.md) -----------------
# THUGPRO's compression method 3 on the wire (src/engine/net/net_lzham.cpp: the THUGPRO peer mode, register TP-R11):
# lzham_codec at commit d379b1f (master, LZHAM_DLL_VERSION 0x1010 - the same codec THUGPRO builds in), the codec's
# own sources compiled here as a static library, single-threaded (no helper threads are ever asked for).
FetchContent_Declare(lzham
    URL https://github.com/richgel999/lzham_codec/archive/d379b1f9121e2197881c61cfc4713c78848bdfe7.tar.gz
    URL_HASH SHA256=34931869697f2afb6d5880438ca313f61ddb6cb4c8fcea947cad1aaaf88dd8c2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake) # sources only: the codec's own CMake files are for gcc
FetchContent_MakeAvailable(lzham)

# --- The GameSpy SDK (IGN's BSD-3 release) ---------------------------------------------------------------------
include(cmake/gamespy_sdk.cmake)
file(GLOB LZHAM_DECOMP_SOURCES ${lzham_SOURCE_DIR}/lzhamdecomp/*.cpp)
add_library(lzham STATIC
    ${LZHAM_DECOMP_SOURCES}
    ${lzham_SOURCE_DIR}/lzhamcomp/lzham_lzbase.cpp
    ${lzham_SOURCE_DIR}/lzhamcomp/lzham_lzcomp.cpp
    ${lzham_SOURCE_DIR}/lzhamcomp/lzham_lzcomp_internal.cpp
    ${lzham_SOURCE_DIR}/lzhamcomp/lzham_lzcomp_state.cpp
    ${lzham_SOURCE_DIR}/lzhamcomp/lzham_match_accel.cpp
    ${lzham_SOURCE_DIR}/lzhamcomp/lzham_win32_threading.cpp
    ${lzham_SOURCE_DIR}/lzhamlib/lzham_lib.cpp)
target_include_directories(lzham PUBLIC ${lzham_SOURCE_DIR}/include
    PRIVATE ${lzham_SOURCE_DIR}/lzhamdecomp ${lzham_SOURCE_DIR}/lzhamcomp)
target_compile_options(lzham PRIVATE /W0)

# --- The build's commit (the screenshot sidecar names the exe's source) -------------------------------------------
set(THUG2_BUILD_INFO "${CMAKE_BINARY_DIR}/generated/build_info.h")
add_custom_target(build_info
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${THUG2_BUILD_INFO}
            -P "${CMAKE_SOURCE_DIR}/cmake/build_info.cmake"
    BYPRODUCTS "${THUG2_BUILD_INFO}"
    COMMENT "Noting the source's commit")

# --- The editor's fonts (SIL Open Font License 1.1, see THIRD_PARTY_NOTICES.md) ---------------------------
# Inter 4.1 (the UI) and JetBrains Mono 2.304 (numbers, names, checksums): the official release archives, checked
# against their SHA-256. The TTFs are compressed into the exe at build time with ImGui's own binary_to_compressed_c
# (stb compression; ImGui decompresses them at start-up) - nothing is added to the game folder.
FetchContent_Declare(font_inter
    URL https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
    URL_HASH SHA256=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(font_jetbrains_mono
    URL https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
    URL_HASH SHA256=6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(font_inter font_jetbrains_mono)
add_executable(imgui_binary_to_compressed ${imgui_SOURCE_DIR}/misc/fonts/binary_to_compressed_c.cpp)
target_compile_options(imgui_binary_to_compressed PRIVATE /W0)
target_compile_definitions(imgui_binary_to_compressed PRIVATE _CRT_SECURE_NO_WARNINGS)
set(THUG2_EDITOR_FONTS
    "Inter_Regular|${font_inter_SOURCE_DIR}/extras/ttf/Inter-Regular.ttf"
    "Inter_SemiBold|${font_inter_SOURCE_DIR}/extras/ttf/Inter-SemiBold.ttf"
    "JetBrainsMono_Regular|${font_jetbrains_mono_SOURCE_DIR}/fonts/ttf/JetBrainsMono-Regular.ttf")
set(THUG2_EDITOR_FONT_INCLUDES "")
foreach(font IN LISTS THUG2_EDITOR_FONTS)
    string(REPLACE "|" ";" parts "${font}")
    list(GET parts 0 symbol)
    list(GET parts 1 ttf)
    set(out "${CMAKE_BINARY_DIR}/editor_fonts/${symbol}.inc")
    add_custom_command(OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:imgui_binary_to_compressed> -DINPUT=${ttf} -DSYMBOL=${symbol}
                -DOUTPUT=${out} -P "${CMAKE_SOURCE_DIR}/cmake/compress_font.cmake"
        DEPENDS imgui_binary_to_compressed "${ttf}" "${CMAKE_SOURCE_DIR}/cmake/compress_font.cmake"
        COMMENT "Compressing ${symbol} into the exe")
    list(APPEND THUG2_EDITOR_FONT_INCLUDES "${out}")
endforeach()
add_custom_target(editor_fonts DEPENDS ${THUG2_EDITOR_FONT_INCLUDES})

# --- The game ----------------------------------------------------------------------------------------------------
# thug2-decomp.exe: the game built from this source. It runs from the game folder of the player's own THUG2 install
# (its Data folder, binkw32.dll): README.md "Building and running".
add_executable(thug2 WIN32
    src/main.cpp
    src/platform/run_log.cpp
    src/platform/crash.cpp
    src/engine/assets/resource_entry.cpp
    src/engine/assets/asset_types.cpp
    src/engine/assets/resource_registry.cpp
    src/engine/core/arena_alloc.cpp
    src/engine/core/checksums.cpp
    src/engine/core/dbg.cpp
    src/engine/core/dump_text.cpp
    src/engine/core/arena.cpp
    src/engine/core/manager.cpp
    src/engine/core/mem.cpp
    src/engine/core/memory_sizes.cpp
    src/engine/core/memory_report.cpp
    src/engine/core/slab_bank.cpp
    src/engine/core/single_instance.cpp
    src/engine/core/stubs.cpp
    src/engine/core/task_list.cpp
    src/engine/core/str.cpp
    src/engine/core/text_store.cpp
    src/engine/file/asset_bundle.cpp
    src/frontend/model_widget.cpp
    src/game/park_editing/cursor.cpp
    src/game/park_editing/park_floor.cpp
    src/game/park_editing/park_manager.cpp
    src/game/park_editing/park_map.cpp
    src/game/park_editing/park_map_format.cpp
    src/game/park_editing/rail_buffer_format.cpp
    src/game/park_editing/park_rails.cpp
    src/game/park_editing/rail_editor.cpp
    src/game/park_editing/rail_editor_draw.cpp
    src/game/park_editing/rail_editor_script.cpp
    src/game/park_editing/copy_buffer.cpp
    src/game/park_editing/gap_slots.cpp
    src/game/park_editing/piece_menu.cpp
    src/frontend/screen_element.cpp
    src/frontend/menu_element.cpp
    src/frontend/screen_element_manager.cpp
    src/frontend/scrolling_menu.cpp
    src/frontend/sprite_element.cpp
    src/frontend/screen_element_cfuncs.cpp
    src/frontend/text_element.cpp
    src/frontend/text_block.cpp
    src/frontend/front_end.cpp
    src/frontend/mouse_menus.cpp
    src/frontend/data_overlay.cpp
    src/frontend/new_frontend.cpp
    src/frontend/new_frontend_draw.cpp
    src/frontend/new_frontend_game.cpp
    src/frontend/new_frontend_mirror.cpp
    src/frontend/new_frontend_model.cpp
    src/frontend/new_frontend_options.cpp
    src/editor/level_editor.cpp
    src/frontend/restart_tester.cpp
    src/platform/screenshot.cpp
    src/platform/screenshot_files.cpp
    src/engine/gfx/screenshot_capture.cpp
    src/frontend/career_complete.cpp
    src/frontend/test_view.cpp
    src/frontend/start_level.cpp
    src/frontend/automation.cpp
    src/editor/level_editor_hud.cpp
    src/editor/level_editor_layout.cpp
    src/editor/level_editor_objects.cpp
    src/editor/level_editor_picking.cpp
    src/editor/level_editor_shadows.cpp
    src/editor/level_editor_view.cpp
    src/editor/level_editor_walls.cpp
    src/editor/level_editor_faces.cpp
    src/editor/level_editor_clusters.cpp
    src/editor/level_editor_cameras.cpp
    src/editor/level_editor_goals.cpp
    src/editor/level_editor_goals_ui.cpp
    src/editor/level_editor_checks.cpp
    src/editor/level_editor_budget.cpp
    src/editor/level_editor_notes.cpp
    src/editor/level_editor_ground.cpp
    src/editor/level_editor_ground_game.cpp
    src/editor/level_editor_gap_wizard.cpp
    src/editor/level_editor_skate_view.cpp
    src/editor/level_editor_ux9.cpp
    src/editor/level_editor_carry.cpp
    src/editor/level_editor_scenery.cpp
    src/editor/level_editor_scale.cpp
    src/editor/level_editor_shared_coll.cpp
    src/editor/level_editor_ui.cpp
    src/editor/level_editor_camera.cpp
    src/editor/level_editor_play.cpp
    src/editor/level_editor_playtest.cpp
    src/editor/level_editor_playtest_ui.cpp
    src/editor/level_editor_prefs.cpp
    src/editor/level_editor_ui_backend.cpp
    src/editor/level_editor_ui_fonts.cpp
    src/editor/level_editor_ui_layout.cpp
    src/editor/level_editor_ui_menus.cpp
    src/editor/level_editor_ui_strip.cpp
    src/editor/level_editor_ui_theme.cpp
    src/editor/level_editor_nodes.cpp
    src/editor/level_editor_node_edit.cpp
    src/editor/level_editor_node_create.cpp
    src/editor/level_editor_triggers.cpp
    src/editor/level_editor_triggers_ui.cpp
    src/editor/level_editor_entities.cpp
    src/editor/level_editor_entities_ui.cpp
    src/editor/level_editor_career.cpp
    src/editor/level_editor_preview.cpp
    src/editor/level_editor_overlays.cpp
    src/editor/level_editor_overlays_ui.cpp
    src/editor/level_editor_arcs.cpp
    src/editor/level_editor_grind_preview.cpp
    src/editor/level_editor_ruler.cpp
    src/editor/level_editor_thumbs.cpp
    src/editor/level_editor_chains.cpp
    src/editor/level_editor_chains_ui.cpp
    src/editor/level_editor_spots_ui.cpp
    src/editor/level_editor_fields.cpp
    src/editor/level_editor_fields_ui.cpp
    src/editor/level_editor_level_props.cpp
    src/editor/level_editor_actions.cpp
    src/editor/level_editor_changes.cpp
    src/editor/level_editor_organize.cpp
    src/editor/level_editor_clip.cpp
    src/editor/level_editor_ux3.cpp
    src/editor/level_editor_ux3_ui.cpp
    src/editor/level_editor_hints.cpp
    src/editor/level_editor_nav.cpp
    src/editor/level_editor_ux8.cpp
    src/editor/level_editor_ux8_pie.cpp
    src/editor/level_editor_ux8_ui.cpp
    src/editor/level_editor_snap.cpp
    src/editor/level_editor_transform.cpp
    src/editor/level_editor_transform_ui.cpp
    src/editor/level_editor_adjust.cpp
    src/editor/level_editor_arrange.cpp
    src/editor/level_editor_arrange_ui.cpp
    src/editor/level_editor_links.cpp
    src/game/skate/contest.cpp
    src/game/skate/letter_match.cpp
    src/game/skate/point_tally.cpp
    src/game/skate/score_combo.cpp
    src/game/skate/score_net.cpp
    src/game/skate/skate_module_state.cpp
    src/game/skate/skate_module_skaters.cpp
    src/game/skate/skate_rankings.cpp
    src/game/goals/objective.cpp
    src/game/goals/goal_run.cpp
    src/game/goals/goal_life.cpp
    src/game/goals/goal_tricks.cpp
    src/game/goals/goal_manager.cpp
    src/game/goals/goal_manager_cfuncs.cpp
    src/game/goals/goal_manager_cfuncs2.cpp
    src/game/goals/goal_manager_cfuncs3.cpp
    src/game/goals/goal_manager_goals.cpp
    src/game/goals/goal_manager_events.cpp
    src/game/goals/goal_manager_update.cpp
    src/game/goals/goal_ped.cpp
    src/game/goals/goal_net_types.cpp
    src/game/goals/goal_skate_line.cpp
    src/game/skate/game_mode.cpp
    src/game/goals/goal_types.cpp
    src/game/goals/create_a_trick.cpp
    src/game/skate/competition_cfuncs.cpp
    src/game/skate/skater_profile_cfuncs.cpp
    src/game/skate/appearance_cfuncs.cpp
    src/game/skate/preload_cfuncs.cpp
    src/engine/gfx/anim_controller.cpp
    src/engine/gfx/anim_track.cpp
    src/engine/gfx/boned_anim_frame_data.cpp
    src/engine/gfx/screen.cpp
    src/engine/gfx/viewport.cpp
    src/engine/gfx/weather.cpp
    src/engine/gfx/window2d.cpp
    src/engine/gfx/engine_shadows.cpp
    src/engine/gfx/engine_shadow_edges.cpp
    src/engine/gfx/rig.cpp
    src/engine/gfx/settings.cpp
    src/engine/gfx/glyphs.cpp
    src/engine/gfx/flash.cpp
    src/engine/gfx/font.cpp
    src/engine/gfx/text.cpp
    src/engine/gfx/tod_manager.cpp
    src/game/skate/victory_conditions.cpp
    src/game/skate/trick_goals.cpp
    src/game/skate/skate_net.cpp
    src/game/skate/skate_net_relay.cpp
    src/game/skate/skate_net_messages.cpp
    src/game/skate/skate_module.cpp
    src/engine/sys/model_viewer.cpp
    src/engine/sys/viewer_object.cpp
    src/engine/objects/game_objects.cpp
    src/game/rider/trick_lists.cpp
    src/game/skate/gap_checklist.cpp
    src/game/rider/manual_balance.cpp
    src/game/rider/vehicle_camera.cpp
    src/game/rider/walk_camera.cpp
    src/game/rider/walk_physics.cpp
    src/game/rider/walk_movement.cpp
    src/game/rider/walk_collision.cpp
    src/game/rider/adjust_physics.cpp
    src/game/rider/adjust_physics_component.cpp
    src/game/rider/balance_trick.cpp
    src/game/rider/skater_camera_modes.cpp
    src/game/rider/trick_queue.cpp
    src/game/rider/trick_member_functions.cpp
    src/engine/objects/movie_manager.cpp
    src/engine/objects/movie_cfuncs.cpp
    src/engine/objects/anim_movies.cpp
    src/engine/objects/trick_objects.cpp
    src/engine/objects/rail_manager.cpp
    src/game/skate/profile_manager.cpp
    src/game/skate/highscores.cpp
    src/game/skate/career.cpp
    src/game/skate/career_cfuncs.cpp
    src/game/skate/skater_profile.cpp
    src/game/skate/appearance_guard.cpp
    src/game/skate/thugpro_cas.cpp
    src/game/rider/rider.cpp
    src/game/rider/skater_query_cfuncs.cpp
    src/engine/gfx/device.cpp
    src/engine/gfx/movie.cpp
    src/engine/audio/bink_stream.cpp
    src/engine/audio/bink_stream_files.cpp
    src/engine/audio/music_player.cpp
    src/engine/audio/stream.cpp
    src/engine/audio/stream_play.cpp
    src/engine/audio/stream_commands.cpp
    src/engine/audio/music_api.cpp
    src/engine/audio/music_cfuncs.cpp
    src/engine/gfx/loading_bar.cpp
    src/engine/gfx/loading_console.cpp
    src/engine/gfx/draw_frame.cpp
    src/engine/gfx/wireframe.cpp
    src/engine/gfx/view_mode.cpp
    src/engine/gfx/billboard_quad.cpp
    src/engine/gfx/view_cam.cpp
    src/engine/gfx/world_scene.cpp
    src/engine/gfx/engine_glue.cpp
    src/engine/gfx/scene_load.cpp
    src/engine/gfx/pc_font.cpp
    src/engine/gfx/geom.cpp
    src/engine/gfx/pc_lights.cpp
    src/engine/gfx/primitive_states.cpp
    src/engine/gfx/loading_screen.cpp
    src/engine/gfx/pc_mesh.cpp
    src/engine/gfx/pc_mesh_life.cpp
    src/engine/gfx/pc_mesh_load.cpp
    src/engine/gfx/pc_mesh_render.cpp
    src/engine/gfx/pc_fog.cpp
    src/engine/gfx/texture_splats.cpp
    src/engine/gfx/shatter.cpp
    src/engine/gfx/pc_model.cpp
    src/engine/gfx/particle_system.cpp
    src/engine/gfx/particle_system_update.cpp
    src/engine/gfx/particle_types.cpp
    src/engine/gfx/flexible_particle_manager.cpp
    src/engine/gfx/model_appearance.cpp
    src/engine/gfx/model_builder.cpp
    src/engine/gfx/bone_volumes.cpp
    src/engine/gfx/engine.cpp
    src/engine/gfx/model_particles.cpp
    src/engine/gfx/engine_primitive.cpp
    src/engine/gfx/engine_scene.cpp
    src/engine/gfx/particle_sets.cpp
    src/engine/gfx/model.cpp
    src/engine/gfx/model_geoms.cpp
    src/engine/gfx/sector.cpp
    src/engine/gfx/engine_sprite.cpp
    src/engine/gfx/screen_effects.cpp
    src/engine/gfx/engine_texture.cpp
    src/engine/gfx/texture_dict.cpp
    src/engine/gfx/engine_scene_sectors.cpp
    src/engine/gfx/engine_scene_list.cpp
    src/engine/gfx/frame_source.cpp
    src/engine/gfx/engine_font.cpp
    src/engine/gfx/engine_text.cpp
    src/engine/gfx/engine_geom.cpp
    src/engine/gfx/engine_imposters.cpp
    src/engine/gfx/engine_model_lights.cpp
    src/engine/gfx/light_manager.cpp
    src/engine/gfx/scene_lights.cpp
    src/engine/gfx/engine_mesh.cpp
    src/engine/gfx/loading_bar_cfuncs.cpp
    src/engine/gfx/engine_texture_projection.cpp
    src/engine/gfx/viewport_list.cpp
    src/engine/gfx/engine_effects.cpp
    src/engine/gfx/engine_particles.cpp
    src/engine/gfx/particle_maths.cpp
    src/engine/gfx/spawned_polys.cpp
    src/engine/gfx/pc_particles.cpp
    src/engine/gfx/pc_flexible_particles.cpp
    src/engine/gfx/pc_primitive.cpp
    src/engine/gfx/pc_scene.cpp
    src/engine/gfx/pc_sprite.cpp
    src/engine/gfx/pc_texture.cpp
    src/engine/gfx/pc_texture_projection.cpp
    src/engine/gfx/pc_viewport.cpp
    src/engine/gfx/pc_weather.cpp
    src/engine/gfx/pc_billboards.cpp
    src/engine/gfx/pc_buffers.cpp
    src/engine/gfx/pc_gamma.cpp
    src/engine/gfx/pc_grass.cpp
    src/engine/gfx/pc_instance.cpp
    src/engine/gfx/pc_material.cpp
    src/engine/gfx/pc_material_load.cpp
    src/engine/gfx/render_state.cpp
    src/engine/gfx/d3dx.cpp
    src/engine/gfx/occlusion.cpp
    src/engine/gfx/shadow_base.cpp
    src/engine/gfx/mesh_list.cpp
    src/engine/gfx/pixel_shaders.cpp
    src/engine/gfx/simple_material.cpp
    src/engine/gfx/screen_fx.cpp
    src/engine/gfx/draw2d.cpp
    src/engine/gfx/sub_mesh.cpp
    src/engine/gfx/scene_render.cpp
    src/engine/gfx/glow.cpp
    src/engine/gfx/bitmap_texture.cpp
    src/engine/gfx/texture_file.cpp
    src/engine/gfx/hd_ui.cpp
    src/engine/gfx/graphics_options.cpp
    src/platform/key_names.cpp
    src/engine/objects/anim_blender.cpp
    src/engine/objects/ride_tracker.cpp
    src/engine/objects/avoid.cpp
    src/engine/objects/ped_logic.cpp
    src/engine/objects/ped_logic_path.cpp
    src/engine/objects/ped_logic_move.cpp
    src/engine/objects/projectile_components.cpp
    src/engine/objects/collision_component.cpp
    src/engine/objects/matrix_component.cpp
    src/engine/objects/camera_lookaround.cpp
    src/engine/objects/camera_util.cpp
    src/engine/objects/component_types.cpp
    src/game/park_editing/goal_editor_util.cpp
    src/game/park_editing/edited_goal.cpp
    src/game/park_editing/edited_goal_io.cpp
    src/game/park_editing/goal_editor.cpp
    src/game/park_editing/goal_editor_members.cpp
    src/game/park_editing/goal_editor_nodes.cpp
    src/engine/objects/object_proximity.cpp
    src/engine/objects/pager_message.cpp
    src/engine/objects/editor_camera.cpp
    src/engine/file/async.cpp
    src/engine/file/file_load.cpp
    src/engine/file/packs.cpp
    src/engine/file/archive.cpp
    src/engine/lists/key_map.cpp
    src/engine/lists/chain_start.cpp
    src/engine/lists/lookup_table.cpp
    src/engine/render/coll_cache.cpp
    src/engine/render/coll_capsule.cpp
    src/engine/render/coll_obj.cpp
    src/engine/render/coll_movable.cpp
    src/engine/render/coll_static.cpp
    src/engine/render/coll_line.cpp
    src/engine/render/coll_grid.cpp
    src/engine/render/coll_rect.cpp
    src/engine/render/coll_sphere.cpp
    src/engine/render/coll_world.cpp
    src/engine/render/probe_ray.cpp
    src/engine/render/coll_tests.cpp
    src/engine/render/coll_tri.cpp
    src/engine/net/chat.cpp
    src/engine/net/stats.cpp
    src/engine/net/buddy_man.cpp
    src/engine/net/vault_man.cpp
    src/engine/net/stats_man.cpp
    src/engine/net/gamenet_join.cpp
    src/engine/net/gamenet_manager.cpp
    src/engine/net/gamenet_commands.cpp
    src/engine/net/gamenet_game_commands.cpp
    src/engine/net/gamenet_lists.cpp
    src/engine/net/gamenet_members.cpp
    src/engine/net/gamenet_player_lists.cpp
    src/engine/net/gamenet_settings.cpp
    src/engine/net/gamenet_state.cpp
    src/engine/net/gamenet_handlers.cpp
    src/engine/net/gamenet_game_handlers.cpp
    src/engine/net/gamenet_server_handlers.cpp
    src/engine/net/gamenet_session_handlers.cpp
    src/engine/net/gamenet_tasks.cpp
    src/engine/net/player_info.cpp
    src/engine/net/player_flags.cpp
    src/engine/net/score_list.cpp
    src/engine/net/rooms.cpp
    src/engine/net/server_list.cpp
    src/engine/net/server_browser.cpp
    src/engine/net/buddy_status.cpp
    src/engine/net/gamenet_players.cpp
    src/engine/net/gamenet_game_cfuncs.cpp
    src/engine/net/gamenet_flow.cpp
    src/engine/net/gamenet_qr2.cpp
    src/engine/net/gamenet_lobby.cpp
    src/engine/net/gamenet_lobby_peer.cpp
    src/engine/net/gamenet_lobby_cfuncs.cpp
    src/engine/net/gamenet_lobby_menus.cpp
    src/engine/net/gamenet_newplayers.cpp
    src/engine/net/gamenet_manager_create.cpp
    src/game/park_editing/park_accessors.cpp
    src/game/park_editing/cursor_tools.cpp
    src/game/park_editing/upper_menu.cpp
    src/game/park_editing/park_lists.cpp
    src/game/park_editing/park_io.cpp
    src/game/park_editing/park_cfuncs.cpp
    src/game/park_editing/park_pieces.cpp
    src/game/park_editing/park_metapieces.cpp
    src/game/park_editing/park_generator.cpp
    src/game/park_editing/park_editor_state.cpp
    src/game/park_editing/gap_manager.cpp
    src/game/park_editing/park_editor.cpp
    src/game/park_editing/park_editor_cfuncs.cpp
    src/game/park_editing/park_scene.cpp
    src/game/park_editing/park_nodes.cpp
    src/engine/net/service.cpp
    src/engine/net/online_hosts.cpp
    src/engine/net/online_identity.cpp
    src/engine/net/gamespy.cpp
    src/engine/net/thugpro_compat.cpp
    src/engine/net/net_guards.cpp
    src/engine/net/thugpro_join.cpp
    src/engine/net/thugpro_peer.cpp
    src/engine/net/net_dispatcher.cpp
    src/engine/net/net_app_messages.cpp
    src/engine/net/net_client.cpp
    src/engine/net/net_server.cpp
    src/engine/net/net_app.cpp
    src/engine/net/net_wire.cpp
    src/engine/net/gamenet_messages.cpp
    src/engine/net/skater_messages.cpp
    src/engine/net/play_messages.cpp
    src/engine/net/net_app_view.cpp
    src/engine/net/net_latency.cpp
    src/engine/net/net_lists.cpp
    src/engine/net/net_manager.cpp
    src/engine/net/net_metrics.cpp
    src/engine/net/net_compress.cpp
    src/engine/net/net_lzham.cpp
    src/engine/net/net_descs.cpp
    src/engine/net/net_conn.cpp
    src/engine/net/net_messages.cpp
    src/engine/net/net_manager_apps.cpp
    src/engine/net/net_conn_times.cpp
    src/engine/objects/composite.cpp
    src/engine/objects/compositemgr.cpp
    src/engine/objects/cutscene.cpp
    src/engine/objects/cutscene_data.cpp
    src/engine/objects/cutscene_load.cpp
    src/engine/objects/cutscene_update.cpp
    src/engine/objects/cutscene_package.cpp
    src/engine/objects/cutscene_play.cpp
    src/engine/objects/control_pad.cpp
    src/engine/objects/proximity.cpp
    src/engine/script/preferences.cpp
    src/engine/script/struct_buffer.cpp
    src/engine/objects/emitter_object.cpp
    src/engine/objects/node_objects.cpp
    src/engine/objects/path_manager.cpp
    src/engine/objects/path_follower.cpp
    src/engine/sound/sfx.cpp
    src/engine/sound/sound_cfuncs.cpp
    src/engine/sound/sfx_voices.cpp
    src/engine/sound/mute_audio.cpp
    src/engine/sound/surface_kind.cpp
    src/engine/gfx/anim_blend.cpp
    src/engine/gfx/anim_timeline.cpp
    src/engine/gfx/anim_cache.cpp
    src/engine/gfx/anim_channel.cpp
    src/engine/objects/signal.cpp
    src/engine/objects/flip_rotate.cpp
    src/engine/objects/lock_obj.cpp
    src/engine/objects/chain_component.cpp
    src/engine/objects/lock_obj_update.cpp
    src/engine/objects/model_component.cpp
    src/engine/objects/model_component_init.cpp
    src/engine/objects/model_component_members.cpp
    src/engine/objects/model_light_update.cpp
    src/engine/objects/motion_path.cpp
    src/engine/objects/bbox_component.cpp
    src/engine/objects/color_cycle.cpp
    src/engine/objects/camera_component.cpp
    src/engine/objects/car_physics.cpp
    src/engine/objects/collide_and_die.cpp
    src/engine/objects/dynamic_light.cpp
    src/engine/objects/fam.cpp
    src/engine/objects/flexible_particle.cpp
    src/engine/objects/floating_text.cpp
    src/engine/objects/input_component.cpp
    src/engine/objects/motion.cpp
    src/engine/objects/motion_component.cpp
    src/engine/objects/motion_update.cpp
    src/engine/objects/motion_ground.cpp
    src/engine/objects/motion_rotate.cpp
    src/engine/objects/movable_contact.cpp
    src/engine/objects/node_components.cpp
    src/engine/objects/entity.cpp
    src/engine/objects/entity_hub.cpp
    src/engine/objects/particle_component.cpp
    src/engine/objects/proximity_component.cpp
    src/game/park/park_cursor.cpp
    src/game/park/park_editor.cpp
    src/game/park/park_memory.cpp
    src/engine/objects/smtptr.cpp
    src/engine/objects/tracker.cpp
    src/engine/objects/trigger_component.cpp
    src/engine/objects/projectile_component.cpp
    src/engine/objects/display_matrix_component.cpp
    src/engine/objects/static_vehicle.cpp
    src/engine/objects/vehicle_wheel.cpp
    src/engine/objects/shadow_component.cpp
    src/engine/objects/skeleton_component.cpp
    src/engine/objects/sound_component.cpp
    src/engine/objects/stream_component.cpp
    src/engine/objects/suspend_component.cpp
    src/engine/objects/texture_projection_component.cpp
    src/engine/objects/rigidbody_component.cpp
    src/engine/objects/rigidbody_physics.cpp
    src/engine/objects/rigidbody_support.cpp
    src/engine/objects/rigidbody_contacts.cpp
    src/engine/maths/quat_rotation.cpp
    src/engine/maths/rigidbody_math.cpp
    src/engine/maths/vector_transform.cpp
    src/engine/objects/velocity_component.cpp
    src/engine/objects/vibration_component.cpp
    src/engine/objects/wait_and_die.cpp
    src/engine/objects/vehicle_sound.cpp
    src/engine/objects/vehicle_component.cpp
    src/engine/objects/vehicle_physics.cpp
    src/engine/objects/vehicle_collision.cpp
    src/game/rider/rotation.cpp
    src/game/rider/local_net_logic.cpp
    src/game/rider/nonlocal_net_logic.cpp
    src/game/rider/end_run.cpp
    src/game/rider/finalize_physics.cpp
    src/game/rider/flip_and_rotate.cpp
    src/game/rider/floating_name.cpp
    src/game/rider/gap_entry.cpp
    src/game/rider/looping_sound.cpp
    src/game/rider/matrix_queries.cpp
    src/game/rider/physics_control.cpp
    src/game/rider/rotate_component.cpp
    src/game/rider/run_timer.cpp
    src/game/rider/skater_score.cpp
    src/game/rider/skater_sound.cpp
    src/game/rider/skater_state.cpp
    src/game/rider/stance_panel.cpp
    src/engine/pad_input/device.cpp
    src/engine/pad_input/input_hub.cpp
    src/engine/pad_input/sdl_input.cpp
    src/game/rider/controls.cpp
    src/game/rider/core_physics.cpp
    src/game/rider/core_physics_state.cpp
    src/game/rider/core_physics_speed.cpp
    src/game/rider/core_physics_feelers.cpp
    src/game/rider/core_physics_skitch.cpp
    src/game/rider/core_member_functions.cpp
    src/game/rider/core_physics_life.cpp
    src/game/rider/core_physics_ground.cpp
    src/game/rider/core_physics_moves.cpp
    src/game/rider/skater_camera_component.cpp
    src/game/rider/skater_camera_update.cpp
    src/game/rider/core_physics_turning.cpp
    src/game/rider/core_physics_walls.cpp
    src/game/rider/core_physics_wallplant.cpp
    src/game/rider/core_physics_acid_drop.cpp
    src/game/rider/debug_collision.cpp
    src/game/rider/collision_events.cpp
    src/game/rider/core_jump.cpp
    src/game/rider/core_collision.cpp
    src/game/rider/core_grind.cpp
    src/game/rider/core_states.cpp
    src/game/rider/core_rail.cpp
    src/game/rider/core_ground.cpp
    src/game/rider/core_wallride.cpp
    src/game/rider/walk_moves.cpp
    src/game/rider/core_update.cpp
    src/game/rider/nonlocal_playback.cpp
    src/game/rider/state_history.cpp
    src/game/rider/vehicle_sound.cpp
    src/game/rider/skitch.cpp
    src/game/rider/special_item.cpp
    src/game/rider/stats_manager.cpp
    src/game/rider/tagging.cpp
    src/game/rider/thaw_bmx.cpp
    src/game/rider/thaw_bmx_parked.cpp
    src/game/rider/thaw_bmx_physics.cpp
    src/game/rider/thaw_bmx_sticks.cpp
    src/game/rider/thaw_bmx_tricks.cpp
    src/game/rider/trick_component.cpp
    src/platform/platform.cpp
    src/platform/platform_d3d.cpp
    src/platform/platform_input.cpp
    src/platform/platform_system.cpp
    src/platform/platform_cfuncs.cpp
    src/engine/sys/debug_mode.cpp
    src/platform/platform_config.cpp
    src/platform/crt.cpp
    src/platform/debug_log.cpp
    src/platform/pc_setup.cpp
    src/platform/sdl_events.cpp
    src/platform/sdl_window.cpp
    src/platform/registry.cpp
    src/platform/stopwatch.cpp
    src/engine/sys/startup.cpp
    src/engine/sys/mlp.cpp
    src/platform/game_clock.cpp
    src/engine/sys/system_cfuncs.cpp
    src/engine/sys/game_module.cpp
    src/platform/winmain.cpp
    src/engine/sys/startup_init.cpp
    src/game/save_cards/memcard.cpp
    src/game/save_cards/memcard_cfuncs.cpp
    src/game/save_cards/save_types.cpp
    src/game/save_cards/save_file_format.cpp
    src/game/save_cards/save_files_cfuncs.cpp
    src/game/save_cards/save_contents.cpp
    src/engine/gfx/texture_cfuncs.cpp
    src/game/skate/profile_cfuncs.cpp
    src/engine/script/expression.cpp
    src/engine/script/node_manager.cpp
    src/engine/script/node_prefixes.cpp
    src/engine/script/nodes.cpp
    src/engine/script/cache.cpp
    src/engine/script/cscript_manager.cpp
    src/engine/script/cscript_wait.cpp
    src/engine/script/cscript_exec.cpp
    src/engine/script/script_tables.cpp
    src/engine/script/member_function_code.cpp
    src/engine/script/load.cpp
    src/engine/script/cscript_core.cpp
    src/engine/script/names.cpp
    src/engine/script/token_reader.cpp
    src/engine/script/patched.cpp
    src/engine/script/world_cfuncs.cpp
    src/engine/script/script_startup.cpp
    src/engine/script/precompiled.cpp
    src/engine/script/qb.cpp
    src/engine/script/param_struct.cpp
    src/engine/script/skate_cfuncs.cpp
    src/engine/script/trick_cfuncs.cpp
    src/engine/script/game_util_cfuncs.cpp
    src/engine/net/hunt_cfuncs.cpp
    src/engine/gfx/face_texture_cfuncs.cpp
    src/engine/gfx/face_texture.cpp
    src/engine/gfx/custom_anim_key.cpp
    src/engine/gfx/model_utils.cpp
    src/game/skate/cas_options.cpp
    src/engine/gfx/bone_look_at.cpp
    src/engine/maths/vector_rotate_z.cpp
    src/engine/gfx/boned_anim_frame_data_pose.cpp
    src/engine/gfx/cag_cfuncs.cpp
    src/engine/objects/model_swap_cfuncs.cpp
    src/frontend/theme_cfuncs.cpp
    src/game/rider/skater_misc_cfuncs.cpp
    src/game/rider/skater_score_cfuncs.cpp
    src/engine/objects/cutscene_list_cfuncs.cpp
    src/engine/sys/game_config.cpp
    src/engine/script/game_cfuncs.cpp
    src/engine/script/game_flow_cfuncs.cpp
    src/engine/script/net_state_cfuncs.cpp
    src/engine/script/node_object_cfuncs.cpp
    src/engine/script/script_util_cfuncs.cpp
    src/engine/script/math_cfuncs.cpp
    src/engine/script/debug_cfuncs.cpp
    src/engine/objects/object_cfuncs.cpp
    src/engine/script/script_cfuncs.cpp
    src/engine/script/array_cfuncs.cpp
    src/engine/script/cfuncs_lighting.cpp
    src/engine/script/cfuncs_scene.cpp
    src/engine/gfx/lens_flare.cpp
    src/engine/objects/walk_hang.cpp
    src/engine/script/cfuncs_render.cpp
    src/engine/script/cfuncs_polys.cpp
    src/engine/script/cfuncs_memory.cpp
    src/engine/maths/shape_tests.cpp
    src/engine/maths/maths.cpp
    src/engine/maths/mat4.cpp
    src/engine/maths/random.cpp
    src/engine/maths/rotation_blend.cpp
    src/engine/maths/vec4.cpp
    src/engine/util/bitstream.cpp
    src/engine/util/lzss.cpp
    src/engine/util/lzss_unpack.cpp
    src/engine/file/pre_format.cpp
    src/engine/file/bundle_format.cpp
    src/engine/gfx/texture_format.cpp
    src/engine/gfx/image_format.cpp
    src/engine/render/collision_format.cpp
    src/engine/gfx/skeleton_format.cpp
    src/engine/gfx/anim_format.cpp
    src/engine/gfx/font_format.cpp
    src/engine/sound/wave_format.cpp
    src/engine/util/bps.cpp
)
set_target_properties(thug2 PROPERTIES OUTPUT_NAME thug2-decomp)
target_compile_definitions(thug2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(thug2 PRIVATE src "${stb_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated"
                                         "${CMAKE_BINARY_DIR}/editor_fonts")
# (the editor's fonts, compressed at build time: level_editor_ui_fonts.cpp includes them; the generated build_info.h)
add_dependencies(thug2 editor_fonts build_info)
# ws2_32: Winsock; dsound: DirectSoundCreate8 (platform/platform_system.cpp); d3d9, winmm: platform/crt.h's imports
target_link_libraries(thug2 PRIVATE sdl2 ws2_32 dsound ole32 imgui lzham gamespy_sdk d3dx9 d3d9 winmm)
# /w14456 /w14457 /w14458: a local or parameter that hides a local, a parameter or a member (docs/style-guide.md);
# /we4150: deleting a pointer to an incomplete type (no destructor runs, the global operator delete frees it) is an error
target_compile_options(thug2 PRIVATE /W3 /permissive- /utf-8 /w14456 /w14457 /w14458 /we4150)
# The DLLs the exe needs, next to it: d3dx9_43.dll (the package's Microsoft-distributable runtime; see
# THIRD_PARTY_NOTICES.md), SDL2.dll and SDL's controller mappings.
add_custom_command(TARGET thug2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/x86/D3DX9_43.dll" "$<TARGET_FILE_DIR:thug2>/d3dx9_43.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll" "$<TARGET_FILE_DIR:thug2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "$<TARGET_FILE_DIR:thug2>"
    COMMENT "Copying d3dx9_43.dll, SDL2.dll and gamecontrollerdb.txt next to thug2-decomp.exe")

# NOT ORIGINAL - thug2_format_check.exe: offline checks of the file, save and network format readers / writers
# (src/tests/format_check.cpp; give it save files or folders: `thug2_format_check <game>/Save`).
add_executable(thug2_format_check
    src/tests/format_check.cpp
    src/engine/core/checksums.cpp
    src/game/save_cards/save_file_format.cpp
    src/engine/net/net_wire.cpp
    src/game/skate/skate_net_messages.cpp
    src/game/park_editing/park_map_format.cpp
    src/game/park_editing/rail_buffer_format.cpp
    src/engine/net/gamenet_messages.cpp
    src/engine/net/skater_messages.cpp
    src/engine/net/play_messages.cpp
    src/tests/format_check_net.cpp
    src/tests/format_check_files.cpp
    src/engine/file/pre_format.cpp
    src/engine/file/bundle_format.cpp
    src/engine/gfx/texture_format.cpp
    src/engine/gfx/image_format.cpp
    src/engine/render/collision_format.cpp
    src/engine/gfx/skeleton_format.cpp
    src/engine/gfx/anim_format.cpp
    src/engine/gfx/font_format.cpp
    src/engine/sound/wave_format.cpp
    src/engine/util/lzss_unpack.cpp)
target_include_directories(thug2_format_check PRIVATE src)
target_compile_options(thug2_format_check PRIVATE /W3 /permissive- /utf-8 /w14456 /w14457 /w14458)

# NOT ORIGINAL - thug2_maths_check.exe: the plain C++ that replaced the x87 inline assembly compared with that
# assembly over many inputs (src/tests/maths_check.cpp; the reference half needs MSVC for 32-bit x86).
add_executable(thug2_maths_check src/tests/maths_check.cpp)
target_include_directories(thug2_maths_check PRIVATE src)
target_compile_options(thug2_maths_check PRIVATE /W3 /permissive- /utf-8 /w14456 /w14457 /w14458)

# NOT ORIGINAL - thug2_parse_check.exe: the number parsing that replaced atoi / atol / atof (engine/core/
# parse_number.h) compared with the C runtime's over many texts (src/tests/parse_check.cpp).
add_executable(thug2_parse_check src/tests/parse_check.cpp)
target_include_directories(thug2_parse_check PRIVATE src)
target_compile_options(thug2_parse_check PRIVATE /W3 /permissive- /utf-8 /w14456 /w14457 /w14458)

# THUG2_GAME_DIR is the folder holding the game. A stock install keeps the game in a Game subfolder
# (C:/Games/THUG2/Game) - some players delete what's around it and keep Game's contents at the top: a cached
# install root (C:/Games/THUG2) steps into its Game folder when the game isn't at the top (as thug2-level-converter-core/lib/install.ts).
# When it exists, every build copies the exe, its PDB and the DLLs above into it.
set(THUG2_DEPLOY_DIR "${THUG2_GAME_DIR}")
function(thug2_is_game_folder dir out)
    if(EXISTS "${dir}/THUG2.exe" OR EXISTS "${dir}/Data/pre/qb_scripts.prx")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()
thug2_is_game_folder("${THUG2_DEPLOY_DIR}" THUG2_AT_TOP)
thug2_is_game_folder("${THUG2_DEPLOY_DIR}/Game" THUG2_IN_GAME)
if(NOT THUG2_AT_TOP AND THUG2_IN_GAME)
    set(THUG2_DEPLOY_DIR "${THUG2_DEPLOY_DIR}/Game")
endif()
if(EXISTS "${THUG2_DEPLOY_DIR}")
    add_custom_command(TARGET thug2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2> $<TARGET_PDB_FILE:thug2> "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/x86/D3DX9_43.dll" "${THUG2_DEPLOY_DIR}/d3dx9_43.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll" "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "${THUG2_DEPLOY_DIR}"
        COMMENT "Copying thug2-decomp.exe and its DLLs into ${THUG2_DEPLOY_DIR}")
endif()
