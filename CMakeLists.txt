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
# and checked against its SHA-256. src/platform/d3dx.cpp is the one place that calls it.
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
# Its implementation compiled on its own, as the other fetched code is: the game's file includes only its declarations.
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/stb_image_write_impl.cpp
    CONTENT "#define STB_IMAGE_WRITE_IMPLEMENTATION\n#define STBI_WRITE_NO_STDIO\n#include \"stb_image_write.h\"\n")
add_library(stb_image_write STATIC ${CMAKE_BINARY_DIR}/stb_image_write_impl.cpp)
target_include_directories(stb_image_write PUBLIC ${stb_SOURCE_DIR})
target_compile_options(stb_image_write PRIVATE /W0)

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

# --- The boundary layer (docs/architecture.md "Layers") ----------------------------------------------------------
# The places where bytes come from outside: files on disk (formats), network messages (wire). Each is a static library
# that depends only on the standard library and thug2_base; the game sees their typed APIs. (GPU vertex data: src/gpu,
# thug2_gpu - it needs the Direct3D device; the platform APIs: src/platform.)
# --- Microsoft GSL 4.2.0 (MIT; THIRD_PARTY_NOTICES.md) -----------------------------------------------------------
# The C++ Core Guidelines' support library, header-only: gsl::owner (the ownership mark), gsl::at (indexing checked
# against the extent - an index out of range ends the game with a report instead of reading past), gsl::narrow.
FetchContent_Declare(gsl
    URL https://github.com/microsoft/GSL/archive/refs/tags/v4.2.0.tar.gz
    URL_HASH SHA256=2c717545a073649126cb99ebd493fa2ae23120077968795d2c69cbab821e4ac6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(GSL_TEST OFF CACHE BOOL "" FORCE)
set(GSL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(gsl)

add_library(thug2_options INTERFACE)
target_compile_features(thug2_options INTERFACE cxx_std_20)
target_link_libraries(thug2_options INTERFACE Microsoft.GSL::GSL)
# DIRECTINPUT_VERSION: DirectInput 8, the version the game asks for (dinput.h reads it)
target_compile_definitions(thug2_options INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX DIRECTINPUT_VERSION=0x0800)
target_include_directories(thug2_options INTERFACE src)
# /w14456 /w14457 /w14458: a local or parameter that hides a local, a parameter or a member (docs/style-guide.md)
# /w14062: an enumerator that a switch over its enum leaves out (a switch that lists every case has no default)
# /we4150: deleting a pointer to an incomplete type (no destructor runs, the global operator delete frees it) is an error
target_compile_options(thug2_options INTERFACE /W3 /permissive- /utf-8 /w14456 /w14457 /w14458 /w14062 /we4150)

# The layers below the game, in link order (each folder's CMakeLists.txt; docs/architecture.md "Layers").
add_subdirectory(src/base)
add_subdirectory(src/formats)
add_subdirectory(src/wire)
add_subdirectory(src/platform)
add_subdirectory(src/gpu)
add_subdirectory(src/engine)
add_subdirectory(src/game)

# --- The game ----------------------------------------------------------------------------------------------------
# thug2-decomp.exe: the game built from this source. It runs from the game folder of the player's own THUG2 install
# (its Data folder, binkw32.dll): README.md "Building and running".
add_executable(thug2 WIN32
    src/app/main.cpp
    src/app/extensions.cpp
    src/frontend/mouse_menus.cpp
    src/frontend/new_frontend.cpp
    src/frontend/new_frontend_extension.cpp
    src/frontend/new_frontend_draw.cpp
    src/frontend/new_frontend_game.cpp
    src/frontend/new_frontend_mirror.cpp
    src/frontend/new_frontend_model.cpp
    src/frontend/new_frontend_options.cpp
    src/editor/level_editor.cpp
    src/devtools/restart_tester.cpp
    src/devtools/screenshot.cpp
    src/devtools/extensions.cpp
    src/devtools/test_view.cpp
    src/devtools/start_level.cpp
    src/devtools/automation.cpp
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
    src/editor/level_editor_gui_text.cpp
    src/editor/level_editor_camera.cpp
    src/editor/level_editor_play.cpp
    src/editor/level_editor_playtest.cpp
    src/editor/level_editor_playtest_ui.cpp
    src/editor/level_editor_prefs.cpp
    src/editor/level_editor_ui_backend.cpp
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
    src/editor/level_editor_extension.cpp
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
    src/app/component_types.cpp
    src/app/pc_setup.cpp
    src/app/startup.cpp
    src/app/winmain.cpp
    src/app/startup_init.cpp
    src/app/command_table.cpp
)
set_target_properties(thug2 PROPERTIES OUTPUT_NAME thug2-decomp)
# The files that call Winsock's IPv4-only functions the game uses (inet_addr, inet_ntoa, gethostbyname): Winsock marks
# them deprecated unless this is defined.
set_source_files_properties(src/app/winmain.cpp PROPERTIES COMPILE_DEFINITIONS _WINSOCK_DEPRECATED_NO_WARNINGS)
target_include_directories(thug2 PRIVATE "${stb_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated"
                                         "${CMAKE_BINARY_DIR}/editor_fonts")
# (the UI fonts, compressed at build time: engine/ui/ui_fonts.cpp includes them; the generated build_info.h)
add_dependencies(thug2 editor_fonts build_info)
# ws2_32: Winsock; dsound: DirectSoundCreate8 (platform/platform_system.cpp); dinput8 / dxguid: DirectInput's data
# formats and effect GUIDs (engine/pad_input/direct_input.cpp); d3d9, winmm: platform/crt.h's imports
target_link_libraries(thug2 PRIVATE thug2_game thug2_engine thug2_gpu thug2_platform thug2_wire thug2_formats thug2_base sdl2 ws2_32 dsound dinput8 dxguid ole32 imgui lzham stb_image_write gamespy_sdk d3dx9 d3d9 winmm)
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
    src/tests/format_check_net.cpp
    src/tests/format_check_files.cpp)
target_link_libraries(thug2_format_check PRIVATE thug2_wire)

# NOT ORIGINAL - thug2_maths_check.exe: the plain C++ that replaced the x87 inline assembly compared with that
# assembly over many inputs, and maths::matrix_multiply with D3DX's matrix product (src/tests/maths_check.cpp; the
# reference half needs MSVC for 32-bit x86).
add_executable(thug2_maths_check src/tests/maths_check.cpp src/tests/maths_check_d3dx.cpp)
# (d3dx9: D3DX's matrix product, the reference for maths::matrix_multiply; its DLL next to the check)
target_link_libraries(thug2_maths_check PRIVATE thug2_base d3dx9)
add_custom_command(TARGET thug2_maths_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/x86/D3DX9_43.dll" "$<TARGET_FILE_DIR:thug2_maths_check>/d3dx9_43.dll")

# NOT ORIGINAL - thug2_parse_check.exe: the number parsing that replaced atoi / atol / atof (base/core/
# parse_number.h) compared with the C runtime's over many texts (src/tests/parse_check.cpp).
add_executable(thug2_parse_check src/tests/parse_check.cpp)
target_link_libraries(thug2_parse_check PRIVATE thug2_formats)

# NOT ORIGINAL - thug2_printf_check.exe: the printf-style formatting without C varargs (base/core/format.h) compared
# with the C runtime's snprintf over every conversion, flag, width, precision and edge value (src/tests/printf_check.cpp).
add_executable(thug2_printf_check src/tests/printf_check.cpp)
target_link_libraries(thug2_printf_check PRIVATE thug2_formats)

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
