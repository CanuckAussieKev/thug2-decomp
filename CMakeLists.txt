cmake_minimum_required(VERSION 3.25)
project(thug2_decomp LANGUAGES C CXX)

# The game is a 32-bit exe, so everything that loads into it must be 32-bit.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with the x86 preset: the game is 32-bit.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the DLL doesn't need a separate runtime installed next to the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(THUG2_GAME_DIR "C:/Games/THUG2/Game" CACHE PATH "Game folder the built dinput8.dll is copied into")

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
# with dinput8.dll (below), so a game folder needs no PartyMod files. SDL's controller mappings, the same way: the
# gamecontrollerdb.txt PartyMod ships is SDL_GameControllerDB's at commit 8b2d1a7 (2022-09-06), zlib.
set(THUG2_CONTROLLER_DB "${CMAKE_BINARY_DIR}/gamecontrollerdb.txt")
if(NOT EXISTS "${THUG2_CONTROLLER_DB}")
    file(DOWNLOAD
        https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/8b2d1a791c8e5004c736e5137699e8e97ae558c5/gamecontrollerdb.txt
        "${THUG2_CONTROLLER_DB}"
        EXPECTED_HASH SHA256=ada0fe3998a5c7914b57ee15a3900f8b43acb604bf8314abca92231ebea057eb)
endif()

# --- MinHook (BSD 2-clause, see THIRD_PARTY_NOTICES.md) --------------------------------------------------
# Installs the replacement jumps and builds trampolines, so the original code of a replaced function can
# still be called (self-tests, shadow checks) from any thread. Only the 32-bit parts are built.
FetchContent_Declare(minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.3.tar.gz
    URL_HASH SHA256=5bec16358ec9086d4593124bf558635e89135abea2c76e5761ecaf09f4546b19
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake) # download only: built below with just the 32-bit sources
FetchContent_MakeAvailable(minhook)
add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde32.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
target_compile_options(minhook PRIVATE /W0)

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

# --- stb_image_write 1.16 / stb_image 2.27 (Sean Barrett, public domain or MIT, see THIRD_PARTY_NOTICES.md) ---------
# The screenshot key's PNG writer (src/game/sys/screenshot_files.cpp) and, for its self-test only, the PNG reader:
# the stb repository at commit 1ee679c (2021-07-12, stb_image_write's last change), headers only.
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/1ee679ca2ef753a528db5ba6801e1067b40481b8.tar.gz
    URL_HASH SHA256=be9842d207353c89c0b970d618030fe98a944ab3fe1374534cba1b8e934bfdd7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(stb)

# --- The build's commit (the screenshot sidecar names the DLL's source) -------------------------------------------
set(THUG2_BUILD_INFO "${CMAKE_BINARY_DIR}/generated/build_info.h")
add_custom_target(build_info
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${THUG2_BUILD_INFO}
            -P "${CMAKE_SOURCE_DIR}/cmake/build_info.cmake"
    BYPRODUCTS "${THUG2_BUILD_INFO}"
    COMMENT "Noting the source's commit")

# --- The editor's fonts (SIL Open Font License 1.1, see THIRD_PARTY_NOTICES.md) ---------------------------
# Inter 4.1 (the UI) and JetBrains Mono 2.304 (numbers, names, checksums): the official release archives, checked
# against their SHA-256. The TTFs are compressed into the DLL at build time with ImGui's own binary_to_compressed_c
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
        COMMENT "Compressing ${symbol} into the DLL")
    list(APPEND THUG2_EDITOR_FONT_INCLUDES "${out}")
endforeach()
add_custom_target(editor_fonts DEPENDS ${THUG2_EDITOR_FONT_INCLUDES})

# The rewritten game code and the hook machinery, shared by the DLL and the check tool.
add_library(thug2_code OBJECT
    src/hook/hook.cpp
    src/hook/log.cpp
    src/hook/shadow.cpp
    src/hook/selftest.cpp
    src/hook/selftest_runner.cpp
    src/hook/selftests_skaterB.cpp
    src/hook/selftests_menu.cpp
    src/hook/selftests_left.cpp
    src/hook/selftests_gfx.cpp
    src/hook/selftests_platform.cpp
    src/hook/selftests_pc.cpp
    src/hook/selftests_glyph.cpp
    src/hook/selftests_skaterA.cpp
    src/hook/selftests_gfx2.cpp
    src/hook/selftests_el3d.cpp
    src/hook/selftests_fe_anim.cpp
    src/hook/selftests_fe_element.cpp
    src/hook/selftests_fe_menu.cpp
    src/hook/selftests_fe_manager.cpp
    src/hook/selftests_fe_scroll.cpp
    src/hook/selftests_fe_sprite.cpp
    src/hook/selftests_fe_cfuncs.cpp
    src/hook/selftests_fe_text.cpp
    src/hook/selftests_tod.cpp
    src/hook/selftests_tod2.cpp
    src/hook/selftests_viewer.cpp
    src/hook/selftests_viewer2.cpp
    src/hook/selftests_viewer3.cpp
    src/hook/selftests_viewer4.cpp
    src/hook/selftests_viewer5.cpp
    src/hook/selftests_viewer6.cpp
    src/hook/selftests_viewer7.cpp
    src/hook/selftests_viewer8.cpp
    src/hook/selftests_viewer9.cpp
    src/hook/selftests_viewer10.cpp
    src/hook/selftests_viewer11.cpp
    src/hook/selftests_viewer12.cpp
    src/hook/selftests_viewer13.cpp
    src/hook/selftests_viewer14.cpp
    src/hook/selftests_viewer15.cpp
    src/hook/selftests_viewer16.cpp
    src/hook/selftests_viewer17.cpp
    src/hook/selftests_viewer18.cpp
    src/hook/selftests_viewer19.cpp
    src/hook/selftests_viewer20.cpp
    src/hook/selftests_viewer21.cpp
    src/hook/selftests_tod3.cpp
    src/hook/selftests_tod4.cpp
    src/hook/selftests_mouse.cpp
    src/hook/selftests_automation.cpp
    src/hook/selftests_screenshot.cpp
    src/hook/selftests_sprite.cpp
    src/hook/selftests_netB.cpp
    src/hook/selftests_mainloop.cpp
    src/hook/selftests_misc.cpp
    src/hook/selftests_lfxc.cpp
    src/hook/selftests_lfxc2.cpp
    src/hook/selftests_lfxc3.cpp
    src/hook/selftests_park.cpp
    src/hook/selftests_scache.cpp
    src/hook/selftests_text.cpp
    src/hook/selftests_winmain.cpp
    src/hook/selftests_appmain.cpp
    src/hook/selftests_appmain2.cpp
    src/hook/selftests_appmain3.cpp
    src/hook/selftests_appmain4.cpp
    src/hook/selftests_appmain5.cpp
    src/hook/selftests_appmain6.cpp
    src/hook/selftests_appmain7.cpp
    src/hook/selftests_anim.cpp
    src/hook/selftests_anim2.cpp
    src/hook/selftests_anim3.cpp
    src/hook/selftests_anim4.cpp
    src/hook/selftests_anim5.cpp
    src/hook/selftests_anim6.cpp
    src/hook/selftests_anim7.cpp
    src/hook/selftests_anim8.cpp
    src/hook/selftests_anim9.cpp
    src/hook/selftests_anim10.cpp
    src/hook/selftests_anim11.cpp
    src/hook/selftests_anim12.cpp
    src/hook/selftests_anim13.cpp
    src/hook/selftests_anim14.cpp
    src/hook/selftests_anim15.cpp
    src/hook/selftests_anim16.cpp
    src/hook/selftests_anim17.cpp
    src/hook/selftests_anim18.cpp
    src/hook/selftests_anim19.cpp
    src/hook/selftests_appmain8.cpp
    src/hook/selftests_appmain9.cpp
    src/hook/selftests_appmain10.cpp
    src/hook/selftests_appmain12.cpp
    src/hook/selftests_appmain11.cpp
    src/hook/selftests_objsys1.cpp
    src/hook/selftests_objsys2.cpp
    src/hook/selftests_objsys3.cpp
    src/hook/selftests_objsys4.cpp
    src/hook/selftests_objsys5.cpp
    src/hook/selftests_objsys6.cpp
    src/hook/selftests_objsys7.cpp
    src/hook/selftests_objsys8.cpp
    src/hook/selftests_objsys9.cpp
    src/hook/selftests_objsys10.cpp
    src/hook/selftests_objsys11.cpp
    src/hook/selftests_gapsB.cpp
    src/hook/selftests_mva.cpp
    src/hook/selftests_mva2.cpp
    src/hook/selftests_mva3.cpp
    src/hook/selftests_qb.cpp
    src/hook/selftests_gapsA.cpp
    src/hook/selftests_mvb.cpp
    src/hook/selftests_csD.cpp
    src/hook/selftests_cobj.cpp
    src/hook/selftests_nodes.cpp
    src/hook/selftests_coim.cpp
    src/hook/selftests_obj.cpp
    src/hook/selftests_evt.cpp
    src/hook/selftests_trk.cpp
    src/hook/selftests_csA.cpp
    src/hook/selftests_csC.cpp
    src/hook/selftests_csB.cpp
    src/hook/selftests_parkedA.cpp
    src/hook/selftests_parkmax.cpp
    src/hook/selftests_netC.cpp
    src/hook/selftests_netD.cpp
    src/hook/selftests_netF.cpp
    src/hook/selftests_netA.cpp
    src/hook/selftests_netE.cpp
    src/hook/selftests_menulvl.cpp
    src/hook/selftests_motion.cpp
    src/hook/selftests_gamenet.cpp
    src/hook/selftests_trigger.cpp
    src/hook/selftests_editor.cpp
    src/hook/selftests_editor_ui.cpp
    src/hook/selftests_editor_nodes.cpp
    src/hook/selftests_editor_create.cpp
    src/hook/selftests_editor_preview.cpp
    src/hook/selftests_editor_levels.cpp
    src/hook/selftests_editor_picking.cpp
    src/hook/selftests_editor_view.cpp
    src/hook/selftests_editor_triggers.cpp
    src/hook/selftests_editor_walls.cpp
    src/hook/selftests_editor_scale.cpp
    src/hook/selftests_editor_shared_coll.cpp
    src/hook/selftests_editor_chains.cpp
    src/hook/selftests_editor_fields.cpp
    src/hook/selftests_editor_shadows.cpp
    src/hook/selftests_editor_career.cpp
    src/hook/selftests_editor_carry.cpp
    src/hook/selftests_pcmstream.cpp
    src/hook/selftests_pcmplay.cpp
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
    src/game/front/menu_backdrop.cpp
    src/game/front/level_editor.cpp
    src/game/front/restart_tester.cpp
    src/game/sys/screenshot.cpp
    src/game/sys/screenshot_files.cpp
    src/game/gfx/screenshot_capture.cpp
    src/game/front/career_complete.cpp
    src/game/front/start_level.cpp
    src/game/front/automation.cpp
    src/game/front/level_editor_hud.cpp
    src/game/front/level_editor_layout.cpp
    src/game/front/level_editor_objects.cpp
    src/game/front/level_editor_picking.cpp
    src/game/front/level_editor_shadows.cpp
    src/game/front/level_editor_view.cpp
    src/game/front/level_editor_walls.cpp
    src/game/front/level_editor_carry.cpp
    src/game/front/level_editor_scenery.cpp
    src/game/front/level_editor_scale.cpp
    src/game/front/level_editor_shared_coll.cpp
    src/game/front/level_editor_ui.cpp
    src/game/front/level_editor_ui_backend.cpp
    src/game/front/level_editor_ui_fonts.cpp
    src/game/front/level_editor_ui_layout.cpp
    src/game/front/level_editor_nodes.cpp
    src/game/front/level_editor_node_edit.cpp
    src/game/front/level_editor_node_create.cpp
    src/game/front/level_editor_triggers.cpp
    src/game/front/level_editor_triggers_ui.cpp
    src/game/front/level_editor_career.cpp
    src/game/front/level_editor_preview.cpp
    src/game/front/level_editor_chains.cpp
    src/game/front/level_editor_chains_ui.cpp
    src/game/front/level_editor_fields.cpp
    src/game/front/level_editor_fields_ui.cpp
    src/game/front/level_editor_level_props.cpp
    src/game/skate/competition.cpp
    src/game/gfx/anim_controller.cpp
    src/game/gfx/screen.cpp
    src/game/gfx/settings.cpp
    src/game/gfx/glyphs.cpp
    src/game/gfx/flash.cpp
    src/game/gfx/font.cpp
    src/game/gfx/text.cpp
    src/game/gfx/tod_manager.cpp
    src/game/skate/victory_conditions.cpp
    src/game/skate/trick_goals.cpp
    src/game/skate/skate_net.cpp
    src/game/sys/viewer.cpp
    src/game/obj/game_objects.cpp
    src/game/skater/trick_lists.cpp
    src/game/skate/gap_checklist.cpp
    src/game/skater/manual.cpp
    src/game/obj/movie_manager.cpp
    src/game/obj/anim_movies.cpp
    src/game/obj/trick_objects.cpp
    src/game/obj/rail_manager.cpp
    src/game/skate/profile_manager.cpp
    src/game/skate/records.cpp
    src/game/skate/career.cpp
    src/game/skate/skater_profile.cpp
    src/game/skater/skater.cpp
    src/game/gfx/device.cpp
    src/game/gfx/movie.cpp
    src/game/pcm/bink_stream.cpp
    src/game/pcm/bink_stream_files.cpp
    src/game/pcm/music.cpp
    src/game/pcm/stream.cpp
    src/game/pcm/stream_play.cpp
    src/game/pcm/stream_commands.cpp
    src/game/pcm/music_api.cpp
    src/game/gfx/loading_bar.cpp
    src/game/gfx/render.cpp
    src/game/gfx/wireframe.cpp
    src/game/gfx/sprite.cpp
    src/game/gfx/camera.cpp
    src/game/gfx/scene.cpp
    src/game/gfx/engine_glue.cpp
    src/game/gfx/render_state.cpp
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
    src/game/sys/key_names.cpp
    src/game/obj/anim_blender.cpp
    src/game/obj/ride_tracker.cpp
    src/game/obj/avoid.cpp
    src/game/obj/camera_lookaround.cpp
    src/game/obj/camera_util.cpp
    src/game/obj/component_types.cpp
    src/game/ed/goal_editor_util.cpp
    src/game/obj/editor_camera.cpp
    src/game/file/async.cpp
    src/game/file/file.cpp
    src/game/file/pip.cpp
    src/game/file/pre.cpp
    src/game/lst/hashtable.cpp
    src/game/lst/head.cpp
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
    src/game/net/gamenet_manager.cpp
    src/game/ed/park_accessors.cpp
    src/game/ed/cursor_tools.cpp
    src/game/ed/upper_menu.cpp
    src/game/ed/park_lists.cpp
    src/game/ed/park_io.cpp
    src/game/ed/park_cfuncs.cpp
    src/game/net/service.cpp
    src/game/net/online_hosts.cpp
    src/game/net/online_identity.cpp
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
    src/game/sfx/sfx_voices.cpp
    src/game/gfx/anim_blend.cpp
    src/game/obj/event.cpp
    src/game/obj/flip_rotate.cpp
    src/game/obj/motion.cpp
    src/game/obj/object.cpp
    src/game/obj/objman.cpp
    src/game/park/park_cursor.cpp
    src/game/park/park_editor.cpp
    src/game/park/park_memory.cpp
    src/game/obj/smtptr.cpp
    src/game/obj/tracker.cpp
    src/game/obj/trigger_component.cpp
    src/game/skater/rotation.cpp
    src/game/sio/device.cpp
    src/game/sio/inpman.cpp
    src/game/sio/sdl_input.cpp
    src/game/skater/controls.cpp
    src/game/skater/core_physics.cpp
    src/game/skater/debug_collision.cpp
    src/game/sys/platform.cpp
    src/game/sys/platform_cfuncs.cpp
    src/game/sys/debug_mode.cpp
    src/game/sys/config.cpp
    src/game/sys/debug_log.cpp
    src/game/sys/pc_setup.cpp
    src/game/sys/sdl_events.cpp
    src/game/sys/sdl_window.cpp
    src/game/sys/registry.cpp
    src/game/sys/timer.cpp
    src/game/sys/main.cpp
    src/game/sys/mlp.cpp
    src/game/sys/game_clock.cpp
    src/game/sys/module.cpp
    src/game/sys/winmain.cpp
    src/game/mc/memcard.cpp
    src/game/mc/memcard_cfuncs.cpp
    src/game/script/eval.cpp
    src/game/script/node_manager.cpp
    src/game/script/node_prefixes.cpp
    src/game/script/nodes.cpp
    src/game/script/cache.cpp
    src/game/script/cscript_manager.cpp
    src/game/script/cscript_wait.cpp
    src/game/script/cscript_exec.cpp
    src/game/script/load.cpp
    src/game/script/cscript_core.cpp
    src/game/script/names.cpp
    src/game/script/parse.cpp
    src/game/script/patched.cpp
    src/game/script/precompiled.cpp
    src/game/script/qb.cpp
    src/game/script/struct.cpp
    src/game/script/skate_cfuncs.cpp
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
target_include_directories(thug2_code PUBLIC src PRIVATE ${minhook_SOURCE_DIR}/src) # MinHook's hde32 decoder
target_link_libraries(thug2_code PUBLIC minhook sdl2 ws2_32 imgui) # ws2_32: the command line's server address
# (the editor's fonts, compressed at build time: level_editor_ui_fonts.cpp includes them)
add_dependencies(thug2_code editor_fonts)
target_include_directories(thug2_code PRIVATE "${CMAKE_BINARY_DIR}/editor_fonts")
# (stb's headers; the generated build_info.h)
add_dependencies(thug2_code build_info)
target_include_directories(thug2_code PRIVATE "${stb_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(thug2_code PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(thug2_code PRIVATE /W4 /permissive- /utf-8)

add_library(thug2_decomp SHARED
    src/proxy/dinput8.cpp
    src/proxy/dinput8.def
)
target_link_libraries(thug2_decomp PRIVATE thug2_code minhook)
target_compile_options(thug2_decomp PRIVATE /W4 /permissive- /utf-8)
# The proxy has to be called dinput8.dll for Windows to load it instead of the system one.
set_target_properties(thug2_decomp PROPERTIES OUTPUT_NAME dinput8)

# A stock install keeps the game in a Game subfolder (C:/Games/THUG2/Game): a cached C:/Games/THUG2 steps into it.
set(THUG2_DEPLOY_DIR "${THUG2_GAME_DIR}")
if(NOT EXISTS "${THUG2_DEPLOY_DIR}/Data" AND EXISTS "${THUG2_DEPLOY_DIR}/Game/Data")
    set(THUG2_DEPLOY_DIR "${THUG2_DEPLOY_DIR}/Game")
endif()
if(EXISTS "${THUG2_DEPLOY_DIR}")
    add_custom_command(TARGET thug2_decomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2_decomp> "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll" "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "${THUG2_DEPLOY_DIR}"
        COMMENT "Copying dinput8.dll, SDL2.dll and gamecontrollerdb.txt into ${THUG2_DEPLOY_DIR}")
endif()

# thug2_check: the start-up checks and the early self-tests against THUG2PM.exe's code, without starting the
# game (see tools/check/main.cpp):  thug2_check [<path to THUG2PM.exe>]
add_executable(thug2_check tools/check/main.cpp)
target_link_libraries(thug2_check PRIVATE thug2_code minhook delayimp)
# PartyMod's SDL code is linked (window, input: never run offline), so SDL2 is only loaded if used.
target_link_options(thug2_check PRIVATE /DELAYLOAD:SDL2.dll)
target_compile_options(thug2_check PRIVATE /W4 /permissive- /utf-8)
# Keep the tool itself away from 0x400000: its child process loads the game image there.
target_link_options(thug2_check PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
