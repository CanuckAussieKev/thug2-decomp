cmake_minimum_required(VERSION 3.25)
project(thug2_decomp LANGUAGES C CXX)

# The game is a 32-bit exe (its data, the GameSpy SDK and Bink are 32-bit).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with the x86 preset: the game is 32-bit.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the exe doesn't need a separate runtime installed next to the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(THUG2_GAME_DIR "C:/Games/THUG2/Game" CACHE PATH
    "The game folder of a THUG2 install (its Data folder) the built exe is copied into, if it exists (an install root steps into its Game folder)")

# --- SDL2 (PartyMod's window and input: src/game/sys/sdl_*.cpp, src/game/sio/sdl_input.cpp) --------------
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
# and checked against its SHA-256. src/game/gfx/d3dx.cpp is the one place that calls it.
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
# The level editor's UI layer (src/game/front/level_editor_ui*.cpp): the core, the official SDL2 platform backend
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
# The screenshot key's PNG writer (src/game/sys/screenshot_files.cpp): the stb repository at commit 1ee679c (2021-07-12, stb_image_write's last change), headers only.
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/1ee679ca2ef753a528db5ba6801e1067b40481b8.tar.gz
    URL_HASH SHA256=be9842d207353c89c0b970d618030fe98a944ab3fe1374534cba1b8e934bfdd7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(stb)

# --- LZHAM 1.0 (Rich Geldreich; public domain, earlier releases MIT - see THIRD_PARTY_NOTICES.md) -----------------
# THUGPRO's compression method 3 on the wire (src/game/net/net_lzham.cpp: the THUGPRO peer mode, register TP-R11):
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
    src/game/sys/log.cpp
    src/game/sys/crash.cpp
    src/game/ass/asset.cpp
    src/game/ass/asset_types.cpp
    src/game/ass/assman.cpp
    src/game/core/alloc.cpp
    src/game/core/crc.cpp
    src/game/core/dbg.cpp
    src/game/core/dump_text.cpp
    src/game/core/heap.cpp
    src/game/core/manager.cpp
    src/game/core/mem.cpp
    src/game/core/memory_sizes.cpp
    src/game/core/memory_report.cpp
    src/game/core/pool.cpp
    src/game/core/singleton.cpp
    src/game/core/stubs.cpp
    src/game/core/task.cpp
    src/game/core/str.cpp
    src/game/core/string.cpp
    src/game/file/asset_bundle.cpp
    src/game/front/element3d.cpp
    src/game/ed/cursor.cpp
    src/game/ed/park_floor.cpp
    src/game/ed/park_manager.cpp
    src/game/ed/park_map.cpp
    src/game/ed/park_rails.cpp
    src/game/ed/rail_editor.cpp
    src/game/ed/rail_editor_draw.cpp
    src/game/ed/rail_editor_script.cpp
    src/game/ed/clipboard.cpp
    src/game/ed/gap_slots.cpp
    src/game/ed/piece_menu.cpp
    src/game/front/screen_element.cpp
    src/game/front/menu_element.cpp
    src/game/front/screen_element_manager.cpp
    src/game/front/scrolling_menu.cpp
    src/game/front/sprite_element.cpp
    src/game/front/screen_element_cfuncs.cpp
    src/game/front/text_element.cpp
    src/game/front/text_block.cpp
    src/game/front/front_end.cpp
    src/game/front/mouse_menus.cpp
    src/game/front/data_overlay.cpp
    src/game/front/new_frontend.cpp
    src/game/front/new_frontend_draw.cpp
    src/game/front/new_frontend_game.cpp
    src/game/front/new_frontend_mirror.cpp
    src/game/front/new_frontend_model.cpp
    src/game/front/new_frontend_options.cpp
    src/game/front/level_editor.cpp
    src/game/front/restart_tester.cpp
    src/game/sys/screenshot.cpp
    src/game/sys/screenshot_files.cpp
    src/game/gfx/screenshot_capture.cpp
    src/game/front/career_complete.cpp
    src/game/front/test_view.cpp
    src/game/front/start_level.cpp
    src/game/front/automation.cpp
    src/game/front/level_editor_hud.cpp
    src/game/front/level_editor_layout.cpp
    src/game/front/level_editor_objects.cpp
    src/game/front/level_editor_picking.cpp
    src/game/front/level_editor_shadows.cpp
    src/game/front/level_editor_view.cpp
    src/game/front/level_editor_walls.cpp
    src/game/front/level_editor_faces.cpp
    src/game/front/level_editor_clusters.cpp
    src/game/front/level_editor_cameras.cpp
    src/game/front/level_editor_goals.cpp
    src/game/front/level_editor_goals_ui.cpp
    src/game/front/level_editor_checks.cpp
    src/game/front/level_editor_budget.cpp
    src/game/front/level_editor_notes.cpp
    src/game/front/level_editor_ground.cpp
    src/game/front/level_editor_ground_game.cpp
    src/game/front/level_editor_gap_wizard.cpp
    src/game/front/level_editor_skate_view.cpp
    src/game/front/level_editor_ux9.cpp
    src/game/front/level_editor_carry.cpp
    src/game/front/level_editor_scenery.cpp
    src/game/front/level_editor_scale.cpp
    src/game/front/level_editor_shared_coll.cpp
    src/game/front/level_editor_ui.cpp
    src/game/front/level_editor_camera.cpp
    src/game/front/level_editor_play.cpp
    src/game/front/level_editor_playtest.cpp
    src/game/front/level_editor_playtest_ui.cpp
    src/game/front/level_editor_prefs.cpp
    src/game/front/level_editor_ui_backend.cpp
    src/game/front/level_editor_ui_fonts.cpp
    src/game/front/level_editor_ui_layout.cpp
    src/game/front/level_editor_ui_menus.cpp
    src/game/front/level_editor_ui_strip.cpp
    src/game/front/level_editor_ui_theme.cpp
    src/game/front/level_editor_nodes.cpp
    src/game/front/level_editor_node_edit.cpp
    src/game/front/level_editor_node_create.cpp
    src/game/front/level_editor_triggers.cpp
    src/game/front/level_editor_triggers_ui.cpp
    src/game/front/level_editor_entities.cpp
    src/game/front/level_editor_entities_ui.cpp
    src/game/front/level_editor_career.cpp
    src/game/front/level_editor_preview.cpp
    src/game/front/level_editor_overlays.cpp
    src/game/front/level_editor_overlays_ui.cpp
    src/game/front/level_editor_arcs.cpp
    src/game/front/level_editor_grind_preview.cpp
    src/game/front/level_editor_ruler.cpp
    src/game/front/level_editor_thumbs.cpp
    src/game/front/level_editor_chains.cpp
    src/game/front/level_editor_chains_ui.cpp
    src/game/front/level_editor_spots_ui.cpp
    src/game/front/level_editor_fields.cpp
    src/game/front/level_editor_fields_ui.cpp
    src/game/front/level_editor_level_props.cpp
    src/game/front/level_editor_actions.cpp
    src/game/front/level_editor_changes.cpp
    src/game/front/level_editor_organize.cpp
    src/game/front/level_editor_clip.cpp
    src/game/front/level_editor_ux3.cpp
    src/game/front/level_editor_ux3_ui.cpp
    src/game/front/level_editor_hints.cpp
    src/game/front/level_editor_nav.cpp
    src/game/front/level_editor_ux8.cpp
    src/game/front/level_editor_ux8_pie.cpp
    src/game/front/level_editor_ux8_ui.cpp
    src/game/front/level_editor_snap.cpp
    src/game/front/level_editor_transform.cpp
    src/game/front/level_editor_transform_ui.cpp
    src/game/front/level_editor_adjust.cpp
    src/game/front/level_editor_arrange.cpp
    src/game/front/level_editor_arrange_ui.cpp
    src/game/front/level_editor_links.cpp
    src/game/skate/competition.cpp
    src/game/skate/horse.cpp
    src/game/skate/score.cpp
    src/game/skate/score_combo.cpp
    src/game/skate/score_net.cpp
    src/game/skate/skate_module_state.cpp
    src/game/skate/skate_module_skaters.cpp
    src/game/skate/skate_rankings.cpp
    src/game/goals/goal.cpp
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
    src/game/gfx/anim_controller.cpp
    src/game/gfx/anim_track.cpp
    src/game/gfx/boned_anim_frame_data.cpp
    src/game/gfx/screen.cpp
    src/game/gfx/viewport.cpp
    src/game/gfx/weather.cpp
    src/game/gfx/window2d.cpp
    src/game/gfx/engine_shadows.cpp
    src/game/gfx/engine_shadow_edges.cpp
    src/game/gfx/skeleton.cpp
    src/game/gfx/settings.cpp
    src/game/gfx/glyphs.cpp
    src/game/gfx/flash.cpp
    src/game/gfx/font.cpp
    src/game/gfx/text.cpp
    src/game/gfx/tod_manager.cpp
    src/game/skate/victory_conditions.cpp
    src/game/skate/trick_goals.cpp
    src/game/skate/skate_net.cpp
    src/game/skate/skate_net_relay.cpp
    src/game/skate/skate_module.cpp
    src/game/sys/viewer.cpp
    src/game/sys/viewer_object.cpp
    src/game/obj/game_objects.cpp
    src/game/skater/trick_lists.cpp
    src/game/skate/gap_checklist.cpp
    src/game/skater/manual.cpp
    src/game/skater/vehicle_camera.cpp
    src/game/skater/walk_camera.cpp
    src/game/skater/walk_physics.cpp
    src/game/skater/walk_movement.cpp
    src/game/skater/walk_collision.cpp
    src/game/skater/adjust_physics.cpp
    src/game/skater/adjust_physics_component.cpp
    src/game/skater/balance_trick.cpp
    src/game/skater/skater_camera_modes.cpp
    src/game/skater/trick_queue.cpp
    src/game/skater/trick_member_functions.cpp
    src/game/obj/movie_manager.cpp
    src/game/obj/movie_cfuncs.cpp
    src/game/obj/anim_movies.cpp
    src/game/obj/trick_objects.cpp
    src/game/obj/rail_manager.cpp
    src/game/skate/profile_manager.cpp
    src/game/skate/records.cpp
    src/game/skate/career.cpp
    src/game/skate/career_cfuncs.cpp
    src/game/skate/skater_profile.cpp
    src/game/skate/appearance_guard.cpp
    src/game/skate/thugpro_cas.cpp
    src/game/skater/skater.cpp
    src/game/skater/skater_query_cfuncs.cpp
    src/game/gfx/device.cpp
    src/game/gfx/movie.cpp
    src/game/pcm/bink_stream.cpp
    src/game/pcm/bink_stream_files.cpp
    src/game/pcm/music.cpp
    src/game/pcm/stream.cpp
    src/game/pcm/stream_play.cpp
    src/game/pcm/stream_commands.cpp
    src/game/pcm/music_api.cpp
    src/game/pcm/music_cfuncs.cpp
    src/game/gfx/loading_bar.cpp
    src/game/gfx/loading_console.cpp
    src/game/gfx/render.cpp
    src/game/gfx/wireframe.cpp
    src/game/gfx/view_mode.cpp
    src/game/gfx/sprite.cpp
    src/game/gfx/camera.cpp
    src/game/gfx/scene.cpp
    src/game/gfx/engine_glue.cpp
    src/game/gfx/scene_load.cpp
    src/game/gfx/pc_font.cpp
    src/game/gfx/geom.cpp
    src/game/gfx/pc_lights.cpp
    src/game/gfx/primitive_states.cpp
    src/game/gfx/loading_screen.cpp
    src/game/gfx/pc_mesh.cpp
    src/game/gfx/pc_mesh_life.cpp
    src/game/gfx/pc_mesh_load.cpp
    src/game/gfx/pc_mesh_render.cpp
    src/game/gfx/pc_fog.cpp
    src/game/gfx/texture_splats.cpp
    src/game/gfx/shatter.cpp
    src/game/gfx/pc_model.cpp
    src/game/gfx/particle_system.cpp
    src/game/gfx/particle_system_update.cpp
    src/game/gfx/particle_types.cpp
    src/game/gfx/flexible_particle_manager.cpp
    src/game/gfx/model_appearance.cpp
    src/game/gfx/model_builder.cpp
    src/game/gfx/bone_volumes.cpp
    src/game/gfx/engine.cpp
    src/game/gfx/model_particles.cpp
    src/game/gfx/engine_primitive.cpp
    src/game/gfx/engine_scene.cpp
    src/game/gfx/particles.cpp
    src/game/gfx/model.cpp
    src/game/gfx/model_geoms.cpp
    src/game/gfx/sector.cpp
    src/game/gfx/engine_sprite.cpp
    src/game/gfx/screen_effects.cpp
    src/game/gfx/engine_texture.cpp
    src/game/gfx/texture_dict.cpp
    src/game/gfx/engine_scene_sectors.cpp
    src/game/gfx/engine_scene_list.cpp
    src/game/gfx/frame_source.cpp
    src/game/gfx/engine_font.cpp
    src/game/gfx/engine_text.cpp
    src/game/gfx/engine_geom.cpp
    src/game/gfx/engine_imposters.cpp
    src/game/gfx/engine_model_lights.cpp
    src/game/gfx/light_manager.cpp
    src/game/gfx/scene_lights.cpp
    src/game/gfx/engine_mesh.cpp
    src/game/gfx/loading_bar_cfuncs.cpp
    src/game/gfx/engine_texture_projection.cpp
    src/game/gfx/viewport_list.cpp
    src/game/gfx/engine_effects.cpp
    src/game/gfx/engine_particles.cpp
    src/game/gfx/particle_maths.cpp
    src/game/gfx/spawned_polys.cpp
    src/game/gfx/pc_particles.cpp
    src/game/gfx/pc_flexible_particles.cpp
    src/game/gfx/pc_primitive.cpp
    src/game/gfx/pc_scene.cpp
    src/game/gfx/pc_sprite.cpp
    src/game/gfx/pc_texture.cpp
    src/game/gfx/pc_texture_projection.cpp
    src/game/gfx/pc_viewport.cpp
    src/game/gfx/pc_weather.cpp
    src/game/gfx/pc_billboards.cpp
    src/game/gfx/pc_buffers.cpp
    src/game/gfx/pc_gamma.cpp
    src/game/gfx/pc_grass.cpp
    src/game/gfx/pc_instance.cpp
    src/game/gfx/pc_material.cpp
    src/game/gfx/pc_material_load.cpp
    src/game/gfx/render_state.cpp
    src/game/gfx/d3dx.cpp
    src/game/gfx/occlusion.cpp
    src/game/gfx/shadow.cpp
    src/game/gfx/mesh_list.cpp
    src/game/gfx/pixel_shaders.cpp
    src/game/gfx/simple_material.cpp
    src/game/gfx/screen_fx.cpp
    src/game/gfx/draw2d.cpp
    src/game/gfx/mesh.cpp
    src/game/gfx/scene_render.cpp
    src/game/gfx/glow.cpp
    src/game/gfx/texture.cpp
    src/game/gfx/texture_file.cpp
    src/game/gfx/hd_ui.cpp
    src/game/gfx/graphics_options.cpp
    src/game/sys/key_names.cpp
    src/game/obj/anim_blender.cpp
    src/game/obj/ride_tracker.cpp
    src/game/obj/avoid.cpp
    src/game/obj/ped_logic.cpp
    src/game/obj/ped_logic_path.cpp
    src/game/obj/ped_logic_move.cpp
    src/game/obj/projectile_components.cpp
    src/game/obj/collision_component.cpp
    src/game/obj/matrix_component.cpp
    src/game/obj/camera_lookaround.cpp
    src/game/obj/camera_util.cpp
    src/game/obj/component_types.cpp
    src/game/ed/goal_editor_util.cpp
    src/game/ed/edited_goal.cpp
    src/game/ed/edited_goal_io.cpp
    src/game/ed/goal_editor.cpp
    src/game/ed/goal_editor_members.cpp
    src/game/ed/goal_editor_nodes.cpp
    src/game/obj/object_proximity.cpp
    src/game/obj/pager_message.cpp
    src/game/obj/editor_camera.cpp
    src/game/file/async.cpp
    src/game/file/file.cpp
    src/game/file/pip.cpp
    src/game/file/pre.cpp
    src/game/lst/hashtable.cpp
    src/game/lst/head.cpp
    src/game/lst/lookup_table.cpp
    src/game/lst/search.cpp
    src/game/nx/coll_cache.cpp
    src/game/nx/coll_capsule.cpp
    src/game/nx/coll_obj.cpp
    src/game/nx/coll_movable.cpp
    src/game/nx/coll_static.cpp
    src/game/nx/coll_line.cpp
    src/game/nx/coll_grid.cpp
    src/game/nx/coll_rect.cpp
    src/game/nx/coll_sphere.cpp
    src/game/nx/coll_world.cpp
    src/game/nx/feeler.cpp
    src/game/nx/coll_tests.cpp
    src/game/nx/coll_tri.cpp
    src/game/net/chat.cpp
    src/game/net/stats.cpp
    src/game/net/buddy_man.cpp
    src/game/net/vault_man.cpp
    src/game/net/stats_man.cpp
    src/game/net/gamenet_join.cpp
    src/game/net/gamenet_manager.cpp
    src/game/net/gamenet_commands.cpp
    src/game/net/gamenet_game_commands.cpp
    src/game/net/gamenet_lists.cpp
    src/game/net/gamenet_members.cpp
    src/game/net/gamenet_player_lists.cpp
    src/game/net/gamenet_settings.cpp
    src/game/net/gamenet_state.cpp
    src/game/net/gamenet_handlers.cpp
    src/game/net/gamenet_game_handlers.cpp
    src/game/net/gamenet_server_handlers.cpp
    src/game/net/gamenet_session_handlers.cpp
    src/game/net/gamenet_tasks.cpp
    src/game/net/player_info.cpp
    src/game/net/player_flags.cpp
    src/game/net/score_list.cpp
    src/game/net/lobby.cpp
    src/game/net/server_list.cpp
    src/game/net/server_browser.cpp
    src/game/net/buddy_status.cpp
    src/game/net/gamenet_players.cpp
    src/game/net/gamenet_game_cfuncs.cpp
    src/game/net/gamenet_flow.cpp
    src/game/net/gamenet_qr2.cpp
    src/game/net/gamenet_lobby.cpp
    src/game/net/gamenet_lobby_peer.cpp
    src/game/net/gamenet_lobby_cfuncs.cpp
    src/game/net/gamenet_lobby_menus.cpp
    src/game/net/gamenet_newplayers.cpp
    src/game/net/gamenet_manager_create.cpp
    src/game/ed/park_accessors.cpp
    src/game/ed/cursor_tools.cpp
    src/game/ed/upper_menu.cpp
    src/game/ed/park_lists.cpp
    src/game/ed/park_io.cpp
    src/game/ed/park_cfuncs.cpp
    src/game/ed/park_pieces.cpp
    src/game/ed/park_metapieces.cpp
    src/game/ed/park_generator.cpp
    src/game/ed/park_editor_state.cpp
    src/game/ed/gap_manager.cpp
    src/game/ed/park_editor.cpp
    src/game/ed/park_editor_cfuncs.cpp
    src/game/ed/park_scene.cpp
    src/game/ed/park_nodes.cpp
    src/game/net/service.cpp
    src/game/net/online_hosts.cpp
    src/game/net/online_identity.cpp
    src/game/net/gamespy.cpp
    src/game/net/thugpro_compat.cpp
    src/game/net/net_guards.cpp
    src/game/net/thugpro_join.cpp
    src/game/net/thugpro_peer.cpp
    src/game/net/net_dispatcher.cpp
    src/game/net/net_app_messages.cpp
    src/game/net/net_client.cpp
    src/game/net/net_server.cpp
    src/game/net/net_app.cpp
    src/game/net/net_app_view.cpp
    src/game/net/net_latency.cpp
    src/game/net/net_lists.cpp
    src/game/net/net_manager.cpp
    src/game/net/net_metrics.cpp
    src/game/net/net_compress.cpp
    src/game/net/net_lzham.cpp
    src/game/net/net_descs.cpp
    src/game/net/net_conn.cpp
    src/game/net/net_messages.cpp
    src/game/net/net_manager_apps.cpp
    src/game/net/net_conn_times.cpp
    src/game/obj/composite.cpp
    src/game/obj/compositemgr.cpp
    src/game/obj/cutscene.cpp
    src/game/obj/cutscene_data.cpp
    src/game/obj/cutscene_load.cpp
    src/game/obj/cutscene_update.cpp
    src/game/obj/cutscene_package.cpp
    src/game/obj/cutscene_play.cpp
    src/game/obj/control_pad.cpp
    src/game/obj/proxim.cpp
    src/game/script/preferences.cpp
    src/game/script/struct_buffer.cpp
    src/game/obj/emitter.cpp
    src/game/obj/node_objects.cpp
    src/game/obj/path_manager.cpp
    src/game/obj/path_follower.cpp
    src/game/sfx/sfx.cpp
    src/game/sfx/sound_cfuncs.cpp
    src/game/sfx/sfx_voices.cpp
    src/game/sfx/mute_audio.cpp
    src/game/sfx/terrain.cpp
    src/game/gfx/anim_blend.cpp
    src/game/gfx/anim_timeline.cpp
    src/game/gfx/anim_cache.cpp
    src/game/gfx/anim_channel.cpp
    src/game/obj/event.cpp
    src/game/obj/flip_rotate.cpp
    src/game/obj/lock_obj.cpp
    src/game/obj/chain_component.cpp
    src/game/obj/lock_obj_update.cpp
    src/game/obj/model_component.cpp
    src/game/obj/model_component_init.cpp
    src/game/obj/model_component_members.cpp
    src/game/obj/model_light_update.cpp
    src/game/obj/motion_path.cpp
    src/game/obj/bbox_component.cpp
    src/game/obj/color_cycle.cpp
    src/game/obj/camera_component.cpp
    src/game/obj/car_physics.cpp
    src/game/obj/collide_and_die.cpp
    src/game/obj/dynamic_light.cpp
    src/game/obj/fam.cpp
    src/game/obj/flexible_particle.cpp
    src/game/obj/floating_text.cpp
    src/game/obj/input_component.cpp
    src/game/obj/motion.cpp
    src/game/obj/motion_component.cpp
    src/game/obj/motion_update.cpp
    src/game/obj/motion_ground.cpp
    src/game/obj/motion_rotate.cpp
    src/game/obj/movable_contact.cpp
    src/game/obj/node_components.cpp
    src/game/obj/object.cpp
    src/game/obj/objman.cpp
    src/game/obj/particle_component.cpp
    src/game/obj/proximity_component.cpp
    src/game/park/park_cursor.cpp
    src/game/park/park_editor.cpp
    src/game/park/park_memory.cpp
    src/game/obj/smtptr.cpp
    src/game/obj/tracker.cpp
    src/game/obj/trigger_component.cpp
    src/game/obj/projectile_component.cpp
    src/game/obj/display_matrix_component.cpp
    src/game/obj/static_vehicle.cpp
    src/game/obj/vehicle_wheel.cpp
    src/game/obj/shadow_component.cpp
    src/game/obj/skeleton_component.cpp
    src/game/obj/sound_component.cpp
    src/game/obj/stream_component.cpp
    src/game/obj/suspend_component.cpp
    src/game/obj/texture_projection_component.cpp
    src/game/obj/rigidbody_component.cpp
    src/game/obj/rigidbody_physics.cpp
    src/game/obj/rigidbody_support.cpp
    src/game/obj/rigidbody_contacts.cpp
    src/game/mth/quat.cpp
    src/game/mth/rigidbody_math.cpp
    src/game/mth/vector_transform.cpp
    src/game/obj/velocity_component.cpp
    src/game/obj/vibration_component.cpp
    src/game/obj/wait_and_die.cpp
    src/game/obj/vehicle_sound.cpp
    src/game/obj/vehicle_component.cpp
    src/game/obj/vehicle_physics.cpp
    src/game/obj/vehicle_collision.cpp
    src/game/skater/rotation.cpp
    src/game/skater/local_net_logic.cpp
    src/game/skater/nonlocal_net_logic.cpp
    src/game/skater/end_run.cpp
    src/game/skater/finalize_physics.cpp
    src/game/skater/flip_and_rotate.cpp
    src/game/skater/floating_name.cpp
    src/game/skater/gap.cpp
    src/game/skater/looping_sound.cpp
    src/game/skater/matrix_queries.cpp
    src/game/skater/physics_control.cpp
    src/game/skater/rotate_component.cpp
    src/game/skater/run_timer.cpp
    src/game/skater/skater_score.cpp
    src/game/skater/skater_sound.cpp
    src/game/skater/skater_state.cpp
    src/game/skater/stance_panel.cpp
    src/game/sio/device.cpp
    src/game/sio/inpman.cpp
    src/game/sio/sdl_input.cpp
    src/game/skater/controls.cpp
    src/game/skater/core_physics.cpp
    src/game/skater/core_physics_state.cpp
    src/game/skater/core_physics_speed.cpp
    src/game/skater/core_physics_feelers.cpp
    src/game/skater/core_physics_skitch.cpp
    src/game/skater/core_member_functions.cpp
    src/game/skater/core_physics_life.cpp
    src/game/skater/core_physics_ground.cpp
    src/game/skater/core_physics_moves.cpp
    src/game/skater/skater_camera_component.cpp
    src/game/skater/skater_camera_update.cpp
    src/game/skater/core_physics_turning.cpp
    src/game/skater/core_physics_walls.cpp
    src/game/skater/core_physics_wallplant.cpp
    src/game/skater/core_physics_acid_drop.cpp
    src/game/skater/debug_collision.cpp
    src/game/skater/collision_events.cpp
    src/game/skater/core_jump.cpp
    src/game/skater/core_collision.cpp
    src/game/skater/core_grind.cpp
    src/game/skater/core_states.cpp
    src/game/skater/core_rail.cpp
    src/game/skater/core_ground.cpp
    src/game/skater/core_wallride.cpp
    src/game/skater/walk_moves.cpp
    src/game/skater/core_update.cpp
    src/game/skater/nonlocal_playback.cpp
    src/game/skater/state_history.cpp
    src/game/skater/vehicle_sound.cpp
    src/game/skater/skitch.cpp
    src/game/skater/special_item.cpp
    src/game/skater/stats_manager.cpp
    src/game/skater/tagging.cpp
    src/game/skater/thaw_bmx.cpp
    src/game/skater/thaw_bmx_parked.cpp
    src/game/skater/thaw_bmx_physics.cpp
    src/game/skater/thaw_bmx_sticks.cpp
    src/game/skater/thaw_bmx_tricks.cpp
    src/game/skater/trick_component.cpp
    src/game/sys/platform.cpp
    src/game/sys/platform_d3d.cpp
    src/game/sys/platform_input.cpp
    src/game/sys/platform_system.cpp
    src/game/sys/platform_cfuncs.cpp
    src/game/sys/debug_mode.cpp
    src/game/sys/config.cpp
    src/game/sys/crt.cpp
    src/game/sys/debug_log.cpp
    src/game/sys/pc_setup.cpp
    src/game/sys/sdl_events.cpp
    src/game/sys/sdl_window.cpp
    src/game/sys/registry.cpp
    src/game/sys/timer.cpp
    src/game/sys/main.cpp
    src/game/sys/mlp.cpp
    src/game/sys/game_clock.cpp
    src/game/sys/system_cfuncs.cpp
    src/game/sys/module.cpp
    src/game/sys/winmain.cpp
    src/game/sys/startup_init.cpp
    src/game/mc/memcard.cpp
    src/game/mc/memcard_cfuncs.cpp
    src/game/mc/save_types.cpp
    src/game/mc/save_files_cfuncs.cpp
    src/game/mc/save_contents.cpp
    src/game/gfx/texture_cfuncs.cpp
    src/game/skate/profile_cfuncs.cpp
    src/game/script/eval.cpp
    src/game/script/node_manager.cpp
    src/game/script/node_prefixes.cpp
    src/game/script/nodes.cpp
    src/game/script/cache.cpp
    src/game/script/cscript_manager.cpp
    src/game/script/cscript_wait.cpp
    src/game/script/cscript_exec.cpp
    src/game/script/script_tables.cpp
    src/game/script/member_function_code.cpp
    src/game/script/load.cpp
    src/game/script/cscript_core.cpp
    src/game/script/names.cpp
    src/game/script/parse.cpp
    src/game/script/patched.cpp
    src/game/script/world_cfuncs.cpp
    src/game/script/script_startup.cpp
    src/game/script/precompiled.cpp
    src/game/script/qb.cpp
    src/game/script/struct.cpp
    src/game/script/skate_cfuncs.cpp
    src/game/script/trick_cfuncs.cpp
    src/game/script/game_util_cfuncs.cpp
    src/game/net/hunt_cfuncs.cpp
    src/game/gfx/face_texture_cfuncs.cpp
    src/game/gfx/face_texture.cpp
    src/game/gfx/custom_anim_key.cpp
    src/game/gfx/model_utils.cpp
    src/game/skate/cas_options.cpp
    src/game/gfx/bone_look_at.cpp
    src/game/mth/vector_rotate_z.cpp
    src/game/gfx/boned_anim_frame_data_pose.cpp
    src/game/gfx/cag_cfuncs.cpp
    src/game/obj/model_swap_cfuncs.cpp
    src/game/front/theme_cfuncs.cpp
    src/game/skater/skater_misc_cfuncs.cpp
    src/game/skater/skater_score_cfuncs.cpp
    src/game/obj/cutscene_list_cfuncs.cpp
    src/game/sys/game_config.cpp
    src/game/script/game_cfuncs.cpp
    src/game/script/game_flow_cfuncs.cpp
    src/game/script/net_state_cfuncs.cpp
    src/game/script/node_object_cfuncs.cpp
    src/game/script/script_util_cfuncs.cpp
    src/game/script/math_cfuncs.cpp
    src/game/script/debug_cfuncs.cpp
    src/game/obj/object_cfuncs.cpp
    src/game/script/script_cfuncs.cpp
    src/game/script/array_cfuncs.cpp
    src/game/script/cfuncs_lighting.cpp
    src/game/script/cfuncs_scene.cpp
    src/game/gfx/lens_flare.cpp
    src/game/obj/walk_hang.cpp
    src/game/script/cfuncs_render.cpp
    src/game/script/cfuncs_polys.cpp
    src/game/script/cfuncs_memory.cpp
    src/game/mth/geometry.cpp
    src/game/mth/math.cpp
    src/game/mth/matrix.cpp
    src/game/mth/random.cpp
    src/game/mth/slerp.cpp
    src/game/mth/vector.cpp
    src/game/util/bitstream.cpp
    src/game/util/lzss.cpp
    src/game/util/bps.cpp
)
set_target_properties(thug2 PROPERTIES OUTPUT_NAME thug2-decomp)
target_compile_definitions(thug2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(thug2 PRIVATE src "${stb_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated"
                                         "${CMAKE_BINARY_DIR}/editor_fonts")
# (the editor's fonts, compressed at build time: level_editor_ui_fonts.cpp includes them; the generated build_info.h)
add_dependencies(thug2 editor_fonts build_info)
# ws2_32: Winsock; dsound: DirectSoundCreate8 (game/sys/platform_system.cpp); d3d9, winmm: game/sys/crt.h's imports
target_link_libraries(thug2 PRIVATE sdl2 ws2_32 dsound ole32 imgui lzham gamespy_sdk d3dx9 d3d9 winmm)
target_compile_options(thug2 PRIVATE /W3 /permissive- /utf-8)
# The DLLs the exe needs, next to it: d3dx9_43.dll (the package's Microsoft-distributable runtime; see
# THIRD_PARTY_NOTICES.md), SDL2.dll and SDL's controller mappings.
add_custom_command(TARGET thug2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/x86/D3DX9_43.dll" "$<TARGET_FILE_DIR:thug2>/d3dx9_43.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll" "$<TARGET_FILE_DIR:thug2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "$<TARGET_FILE_DIR:thug2>"
    COMMENT "Copying d3dx9_43.dll, SDL2.dll and gamecontrollerdb.txt next to thug2-decomp.exe")

# THUG2_GAME_DIR is the folder holding the game. A stock install keeps the game in a Game subfolder
# (C:/Games/THUG2/Game) - some players delete what's around it and keep Game's contents at the top: a cached
# install root (C:/Games/THUG2) steps into its Game folder when the game isn't at the top (as tools/pipeline/lib/install.ts).
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
