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

set(THUG2_GAME_DIR "C:/Games/THUG2/Game" CACHE PATH
    "The folder holding THUG2's exe, which the built dinput8.dll is copied into (an install root steps into its Game folder)")

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
    src/hook/crash.cpp
    src/hook/shadow.cpp
    src/hook/selftest.cpp
    src/hook/selftest_runner.cpp
    src/hook/selftests_skaterB.cpp
    src/hook/sk5_testkit.cpp
    src/hook/selftests_skater_vehicle_camera.cpp
    src/hook/selftests_skater_walk_camera.cpp
    src/hook/selftests_skater_trick_queue.cpp
    src/hook/selftests_skater_walk_pieces.cpp
    src/hook/selftests_skater_walk_collision.cpp
    src/hook/selftests_menu.cpp
    src/hook/selftests_left.cpp
    src/hook/selftests_gfx.cpp
    src/hook/selftests_platform.cpp
    src/hook/selftests_platform_d3d.cpp
    src/hook/selftests_platform_input.cpp
    src/hook/selftests_pc.cpp
    src/hook/selftests_glyph.cpp
    src/hook/selftests_skaterA.cpp
    src/hook/selftests_skater_jump.cpp
    src/hook/selftests_skater_core_members.cpp
    src/hook/selftests_skater_core_life.cpp
    src/hook/selftests_skater_core_ground.cpp
    src/hook/selftests_skater_core_moves.cpp
    src/hook/selftests_skater_camera.cpp
    src/hook/selftests_skater_collision.cpp
    src/hook/selftests_skater_grind.cpp
    src/hook/selftests_skater_states.cpp
    src/hook/selftests_skater_rail.cpp
    src/hook/selftests_skater_ground.cpp
    src/hook/selftests_skater_wallride.cpp
    src/hook/selftests_skater_physics_harness.cpp
    src/hook/selftests_skater_state.cpp
    src/hook/selftests_leftovers.cpp
    src/hook/selftests_byte_scan.cpp
    src/hook/selftests_skater_feelers.cpp
    src/hook/selftests_skater_turning.cpp
    src/hook/selftests_skater_walls.cpp
    src/hook/selftests_collision_events.cpp
    src/hook/selftests_skater_wallplant.cpp
    src/hook/selftests_skater_acid_drop.cpp
    src/hook/selftests_skater_state_history.cpp
    src/hook/selftests_skater_vehicle_sound.cpp
    src/hook/selftests_skater_skitch_items.cpp
    src/hook/selftests_skater_stats.cpp
    src/hook/selftests_skater_tagging.cpp
    src/hook/selftests_thaw_bmx.cpp
    src/hook/selftests_thaw_bmx_parked.cpp
    src/hook/selftests_skater_adjust.cpp
    src/hook/selftests_skater_adjust_frame.cpp
    src/hook/selftests_balance_trick.cpp
    src/hook/selftests_skater_camera_modes.cpp
    src/hook/selftests_projectile.cpp
    src/hook/selftests_rail_editor.cpp
    src/hook/selftests_rail_editor_draw.cpp
    src/hook/selftests_rail_editor_script.cpp
    src/hook/selftests_obj_small_components.cpp
    src/hook/selftests_obj_vehicle_sound.cpp
    src/hook/selftests_obj_vehicle.cpp
    src/hook/selftests_skater_trick.cpp
    src/hook/selftests_gfx2.cpp
    src/hook/selftests_el3d.cpp
    src/hook/selftests_fe_anim.cpp
    src/hook/selftests_fe_element.cpp
    src/hook/selftests_fe_menu.cpp
    src/hook/selftests_fe_manager.cpp
    src/hook/selftests_fe_scroll.cpp
    src/hook/selftests_fe_sprite.cpp
    src/hook/selftests_hd_ui.cpp
    src/hook/selftests_mute_audio.cpp
    src/hook/selftests_graphics_options.cpp
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
    src/hook/selftests_park_graffiti.cpp
    src/hook/selftests_park_scene.cpp
    src/hook/selftests_viewer_object.cpp
    src/hook/selftests_park_pieces.cpp
    src/hook/selftests_park_metapieces.cpp
    src/hook/selftests_park_generator.cpp
    src/hook/selftests_park_editor_state.cpp
    src/hook/selftests_park_rails.cpp
    src/hook/selftests_park_clipboard.cpp
    src/hook/selftests_park_gaps.cpp
    src/hook/selftests_viewer21.cpp
    src/hook/selftests_tod3.cpp
    src/hook/selftests_tod4.cpp
    src/hook/selftests_thugpro_guards.cpp
    src/hook/selftests_skater_end_run.cpp
    src/hook/selftests_skater_flip_name.cpp
    src/hook/selftests_skater_gap.cpp
    src/hook/selftests_skater_sound_net.cpp
    src/hook/selftests_skater_nonlocal.cpp
    src/hook/selftests_skater_core_update.cpp
    src/hook/selftests_skater_nonlocal_playback.cpp
    src/hook/selftests_skater_queries.cpp
    src/hook/selftests_skater_physics_control.cpp
    src/hook/selftests_skater_timers.cpp
    src/hook/selftests_skater_state_component.cpp
    src/hook/selftests_mouse.cpp
    src/hook/selftests_automation.cpp
    src/hook/selftests_crash.cpp
    src/hook/selftests_screenshot.cpp
    src/hook/selftests_sprite.cpp
    src/hook/selftests_loading.cpp
    src/hook/selftests_netB.cpp
    src/hook/selftests_mainloop.cpp
    src/hook/selftests_memory_sizes.cpp
    src/hook/selftests_misc.cpp
    src/hook/selftests_ec3.cpp
    src/hook/selftests_gs4_tricks.cpp
    src/hook/selftests_gs4_hunt.cpp
    src/hook/selftests_gs4_texture.cpp
    src/hook/selftests_gs4_models.cpp
    src/hook/selftests_gs4_skater.cpp
    src/hook/selftests_gs4_lists.cpp
    src/hook/selftests_gs4_config.cpp
    src/hook/selftests_career_cfuncs.cpp
    src/hook/selftests_competition_cfuncs.cpp
    src/hook/selftests_goals_competition.cpp
    src/hook/selftests_goals_goal.cpp
    src/hook/selftests_goals_game_mode.cpp
    src/hook/selftests_goals_cat.cpp
    src/hook/selftests_goals_manager.cpp
    src/hook/selftests_goals_horse.cpp
    src/hook/selftests_score.cpp
    src/hook/selftests_skate_module.cpp
    src/hook/selftests_skater_query_cfuncs.cpp
    src/hook/selftests_movie_cfuncs.cpp
    src/hook/selftests_skater_profile_cfuncs.cpp
    src/hook/selftests_appearance_cfuncs.cpp
    src/hook/selftests_cas3_face.cpp
    src/hook/selftests_cas3_keys.cpp
    src/hook/selftests_cas3_options.cpp
    src/hook/selftests_cas3_lookat.cpp
    src/hook/selftests_cas3_frames.cpp
    src/hook/selftests_profile_list_cfuncs.cpp
    src/hook/selftests_preload_cfuncs.cpp
    src/hook/selftests_lfxc.cpp
    src/hook/selftests_lfxc2.cpp
    src/hook/selftests_lfxc3.cpp
    src/hook/selftests_gs2_world.cpp
    src/hook/selftests_gs2_startup.cpp
    src/hook/selftests_gs2_save.cpp
    src/hook/selftests_gs2_files.cpp
    src/hook/selftests_gs2_contents.cpp
    src/hook/selftests_gs2_textures.cpp
    src/hook/selftests_gs2_saving.cpp
    src/hook/selftests_gs1.cpp
    src/hook/selftests_ec2.cpp
    src/hook/selftests_engcmd1.cpp
    src/hook/selftests_obj_scene_cmds.cpp
    src/hook/selftests_obj_terrain.cpp
    src/hook/selftests_obj_lens_flare.cpp
    src/hook/selftests_obj_walk_hang.cpp
    src/hook/selftests_engcmd2.cpp
    src/hook/selftests_engcmd3.cpp
    src/hook/selftests_engcmd4.cpp
    src/hook/selftests_park.cpp
    src/hook/selftests_park_editor.cpp
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
    src/hook/selftests_pole_tunnel.cpp
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
    src/hook/selftests_ped_logic.cpp
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
    src/hook/selftests_net_buddies.cpp
    src/hook/selftests_net_vault.cpp
    src/hook/selftests_net_stats.cpp
    src/hook/selftests_netA.cpp
    src/hook/selftests_netE.cpp
    src/hook/selftests_menulvl.cpp
    src/hook/selftests_motion.cpp
    src/hook/selftests_obj_motion1.cpp
    src/hook/selftests_obj_motion2.cpp
    src/hook/selftests_obj_motion3.cpp
    src/hook/selftests_obj_motion4.cpp
    src/hook/selftests_obj_contact.cpp
    src/hook/selftests_obj_particle.cpp
    src/hook/selftests_gamenet.cpp
    src/hook/selftests_skater_gamenet.cpp
    src/hook/selftests_skater_gamenet2.cpp
    src/hook/selftests_skater_gamenet3.cpp
    src/hook/selftests_skater_gamenet4.cpp
    src/hook/selftests_skater_gamenet5.cpp
    src/hook/selftests_skater_gamenet6.cpp
    src/hook/selftests_net3_players.cpp
    src/hook/selftests_net_scores.cpp
    src/hook/selftests_net3_lobby.cpp
    src/hook/selftests_net_servers.cpp
    src/hook/selftests_net_buddy_status.cpp
    src/hook/selftests_skater_walk.cpp
    src/hook/selftests_skater_walk2.cpp
    src/hook/selftests_thugpro.cpp
    src/hook/selftests_thugpro_join.cpp
    src/hook/selftests_gamenet_join.cpp
    src/hook/selftests_net_players.cpp
    src/hook/selftests_net_flow.cpp
    src/hook/selftests_net_qr2.cpp
    src/hook/selftests_net_lobby.cpp
    src/hook/selftests_net_menus.cpp
    src/hook/selftests_net_newplayers.cpp
    src/hook/selftests_net_commands.cpp
    src/hook/selftests_net_counts.cpp
    src/hook/selftests_net_game_commands.cpp
    src/hook/selftests_net_lists.cpp
    src/hook/selftests_net_members.cpp
    src/hook/selftests_net_player_lists.cpp
    src/hook/selftests_net_settings.cpp
    src/hook/selftests_trigger.cpp
    src/hook/selftests_obj_trigger.cpp
    src/hook/selftests_obj_shadow.cpp
    src/hook/selftests_obj_skeleton.cpp
    src/hook/selftests_obj_sound.cpp
    src/hook/selftests_obj_stream.cpp
    src/hook/selftests_obj_suspend.cpp
    src/hook/selftests_obj_rigidbody.cpp
    src/hook/selftests_obj_rigidbody_physics.cpp
    src/hook/selftests_obj_rigidbody_support.cpp
    src/hook/selftests_editor.cpp
    src/hook/selftests_engine_glue.cpp
    src/hook/selftests_scene_load.cpp
    src/hook/selftests_pc_font.cpp
    src/hook/selftests_geom.cpp
    src/hook/selftests_pc_lights.cpp
    src/hook/selftests_loading_screen.cpp
    src/hook/selftests_pc_mesh.cpp
    src/hook/selftests_pc_mesh_life.cpp
    src/hook/selftests_pc_mesh_load.cpp
    src/hook/selftests_pc_mesh_render.cpp
    src/hook/selftests_splats.cpp
    src/hook/selftests_splats2.cpp
    src/hook/selftests_particle_system.cpp
    src/hook/selftests_particles.cpp
    src/hook/selftests_rnd3_particle_types.cpp
    src/hook/selftests_rnd3_particle_manager.cpp
    src/hook/selftests_rnd3_model_particles.cpp
    src/hook/selftests_cas4_appearance.cpp
    src/hook/selftests_cas4_builder.cpp
    src/hook/selftests_cas4_bone_volumes.cpp
    src/hook/selftests_cas4_engine.cpp
    src/hook/selftests_rnd3_primitive.cpp
    src/hook/selftests_rnd3_scene.cpp
    src/hook/selftests_cas1_timeline.cpp
    src/hook/selftests_cas1_anim_cache.cpp
    src/hook/selftests_cas1_channel.cpp
    src/hook/selftests_rnd1_model.cpp
    src/hook/selftests_rnd1_model_geoms.cpp
    src/hook/selftests_rnd1_effects.cpp
    src/hook/selftests_rnd1_particles.cpp
    src/hook/selftests_rnd1_polys.cpp
    src/hook/selftests_rnd4_sector.cpp
    src/hook/selftests_rnd4_sprite.cpp
    src/hook/selftests_rnd4_texture.cpp
    src/hook/selftests_rnd4_texdict.cpp
    src/hook/selftests_rnd4_scene.cpp
    src/hook/selftests_rnd4_projection.cpp
    src/hook/selftests_rnd5_viewport.cpp
    src/hook/selftests_rnd5_weather.cpp
    src/hook/selftests_rnd5_shadows.cpp
    src/hook/selftests_rnd5_skeleton.cpp
    src/hook/selftests_cas2_frames.cpp
    src/hook/selftests_cas2_poses.cpp
    src/hook/selftests_cas2_track.cpp
    src/hook/selftests_cas5_scene_list.cpp
    src/hook/selftests_cas5_text.cpp
    src/hook/selftests_cas5_geom.cpp
    src/hook/selftests_cas5_lights.cpp
    src/hook/selftests_cas5_light_manager.cpp
    src/hook/selftests_cas5_scene_lights.cpp
    src/hook/selftests_cas5_mesh.cpp
    src/hook/selftests_pc_particles.cpp
    src/hook/selftests_flexible_particles.cpp
    src/hook/selftests_pc_primitive.cpp
    src/hook/selftests_pc_scene.cpp
    src/hook/selftests_pc_sprite.cpp
    src/hook/selftests_pc_texture.cpp
    src/hook/selftests_pc_viewport.cpp
    src/hook/selftests_pc_weather.cpp
    src/hook/selftests_pc_billboards.cpp
    src/hook/selftests_pc_buffers.cpp
    src/hook/selftests_pc_gamma.cpp
    src/hook/selftests_pc_grass.cpp
    src/hook/selftests_pc_instance.cpp
    src/hook/selftests_pc_material.cpp
    src/hook/selftests_pc_material_load.cpp
    src/hook/selftests_editor_ui.cpp
    src/hook/selftests_frontend.cpp
    src/hook/selftests_editor_nodes.cpp
    src/hook/selftests_editor_create.cpp
    src/hook/selftests_editor_preview.cpp
    src/hook/selftests_editor_ux4.cpp
    src/hook/selftests_editor_levels.cpp
    src/hook/selftests_editor_picking.cpp
    src/hook/selftests_editor_view.cpp
    src/hook/selftests_editor_triggers.cpp
    src/hook/selftests_editor_map1.cpp
    src/hook/selftests_editor_walls.cpp
    src/hook/selftests_editor_map3.cpp
    src/hook/selftests_editor_map4.cpp
    src/hook/selftests_editor_goals.cpp
    src/hook/selftests_goal_editor.cpp
    src/hook/selftests_goal_editor_component.cpp
    src/hook/selftests_editor_scale.cpp
    src/hook/selftests_collision_component.cpp
    src/hook/selftests_lock_obj.cpp
    src/hook/selftests_obj_bbox.cpp
    src/hook/selftests_obj_color_camera.cpp
    src/hook/selftests_obj_car_physics.cpp
    src/hook/selftests_obj_collide_light.cpp
    src/hook/selftests_obj_fam_particle.cpp
    src/hook/selftests_obj_text_input.cpp
    src/hook/selftests_obj_lock_quat.cpp
    src/hook/selftests_mth_x87.cpp
    src/hook/selftests_obj_model.cpp
    src/hook/selftests_obj_model_init.cpp
    src/hook/selftests_obj_model_members.cpp
    src/hook/selftests_obj_light_motion.cpp
    src/hook/selftests_editor_shared_coll.cpp
    src/hook/selftests_editor_chains.cpp
    src/hook/selftests_editor_fields.cpp
    src/hook/selftests_editor_shadows.cpp
    src/hook/selftests_editor_career.cpp
    src/hook/selftests_editor_carry.cpp
    src/hook/selftests_editor_entities.cpp
    src/hook/selftests_editor_modes.cpp
    src/hook/selftests_editor_ux3.cpp
    src/hook/selftests_editor_ux5.cpp
    src/hook/selftests_editor_ux9.cpp
    src/hook/selftests_editor_transform.cpp
    src/hook/selftests_editor_arrange.cpp
    src/hook/selftests_editor_ux1.cpp
    src/hook/selftests_editor_ux6.cpp
    src/hook/selftests_editor_ux8.cpp
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
target_include_directories(thug2_code PUBLIC src PRIVATE ${minhook_SOURCE_DIR}/src) # MinHook's hde32 decoder
target_link_libraries(thug2_code PUBLIC minhook sdl2 ws2_32 dsound ole32 imgui lzham) # ws2_32: the command line's server address;
# dsound: DirectSoundCreate8 (game/sys/platform_system.cpp)
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

# THUG2_GAME_DIR is the folder holding the game. A stock install keeps the game in a Game subfolder
# (C:/Games/THUG2/Game) - some players delete what's around it and keep Game's contents at the top: a cached
# install root (C:/Games/THUG2) steps into its Game folder when the game isn't at the top (as tools/pipeline/lib/install.ts).
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
    add_custom_command(TARGET thug2_decomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2_decomp> "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll" "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "${THUG2_DEPLOY_DIR}"
        COMMENT "Copying dinput8.dll, SDL2.dll and gamecontrollerdb.txt into ${THUG2_DEPLOY_DIR}")
endif()

# thug2_check: the start-up checks and the early self-tests against the reference build's code, without starting the
# game (see tools/check/main.cpp):  thug2_check [<path to the reference build>]  (default: local.json's referenceExe)
add_executable(thug2_check tools/check/main.cpp)
target_compile_definitions(thug2_check PRIVATE THUG2_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(thug2_check PRIVATE thug2_code minhook delayimp)
# PartyMod's SDL code is linked (window, input: never run offline), so SDL2 is only loaded if used.
target_link_options(thug2_check PRIVATE /DELAYLOAD:SDL2.dll)
target_compile_options(thug2_check PRIVATE /W4 /permissive- /utf-8)
# Keep the tool itself away from 0x400000: its child process loads the game image there.
target_link_options(thug2_check PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)

# thug2_standalone: the game built from source alone - no hook DLL, no original exe. It doesn't link yet: every
# GameFunction / GameVariable left, every hook / self-test call from game code and every library not linked yet is an
# unresolved symbol. Not part of a normal build; `node tools/standalone_report.ts` builds it and sorts what's missing.
get_target_property(THUG2_CODE_SOURCES thug2_code SOURCES)
set(THUG2_GAME_SOURCES ${THUG2_CODE_SOURCES})
list(FILTER THUG2_GAME_SOURCES EXCLUDE REGEX "^src/hook/")
add_executable(thug2_standalone WIN32 EXCLUDE_FROM_ALL src/standalone/main.cpp ${THUG2_GAME_SOURCES})
target_compile_definitions(thug2_standalone PRIVATE THUG2_STANDALONE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(thug2_standalone PRIVATE src "${stb_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated"
                                                    "${CMAKE_BINARY_DIR}/editor_fonts")
add_dependencies(thug2_standalone editor_fonts build_info)
target_link_libraries(thug2_standalone PRIVATE sdl2 ws2_32 dsound ole32 imgui lzham)
target_compile_options(thug2_standalone PRIVATE /W3 /permissive- /utf-8)
