# (3.27: $<COMPILE_ONLY:...>, the level editor plugin's view of the layers' headers - src/editor/CMakeLists.txt)
cmake_minimum_required(VERSION 3.27)
project(thug2_decomp LANGUAGES C CXX)

# The architecture: x86, the game's own (32-bit), or x64, the 64-bit build being ported (docs/design/x64-port.md).
# THUG2_ARCH also names the third-party packages' folders (SDL3's lib/<arch>, D3DX9's lib/<arch> and bin/<arch>).
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(THUG2_ARCH x86)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(THUG2_ARCH x64)
else()
    message(FATAL_ERROR "Build with an x86 or x64 preset.")
endif()

# The x64 port goes library by library (docs/design/x64-port.md "Order of work"): an x64 configure builds only the
# libraries of src/ named here (base, platform, engine... as in tools/layers.json) and the checks whose libraries
# are all built; "gamespy_sdk" here builds the GameSpy SDK. The fetched libraries the others compile themselves
# (Dear ImGui, stb_image_write, LZHAM, volk, VMA, glslang) are built on x64 only when a library here links them.
# An x86 configure builds everything, whatever the list says.
set(THUG2_X64_READY base formats wire platform gamespy_sdk engine net_winsock render_pc gpu_d3d9 gpu_vulkan audio_dsound media_ffmpeg
                    input_dinput fs_win32 script_qb game frontend editor devtools app)

# Whether this configure builds `name` (a library of src/, or gamespy_sdk): always on x86; on x64 when x64-ready.
function(thug2_builds name result)
    if(THUG2_ARCH STREQUAL "x86" OR name IN_LIST THUG2_X64_READY)
        set(${result} TRUE PARENT_SCOPE)
    else()
        set(${result} FALSE PARENT_SCOPE)
    endif()
endfunction()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# compile_commands.json in the build folder, for clang-tidy / clangd (.clang-tidy, docs/style-guide.md).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# The C runtime as a DLL: one heap and one C++ library for the exe and its plugins (engine/ext/plugin.h: the level
# editor's std::string and std::vector cross between them). A Release build copies the runtime's DLLs next to the exe
# (src/app/CMakeLists.txt); a Debug build uses the debug runtime Visual Studio installs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# The optimised build's inlining and linking (MSVC; frame times: tools/game/frame_bench.ts). CMake's RelWithDebInfo
# defaults are /Ob1 - only functions marked inline are expanded, so the small helpers (an anonymous namespace's
# accessors, a matrix's elements, core::at's callers) stayed calls in the hot loops - and an incremental link (jump
# thunks, no /OPT:REF). The game's release build is RelWithDebInfo (CMakePresets.json): /Ob2 and a full link instead.
if(MSVC)
    foreach(language C CXX)
        string(REPLACE "/Ob1" "/Ob2" CMAKE_${language}_FLAGS_RELWITHDEBINFO "${CMAKE_${language}_FLAGS_RELWITHDEBINFO}")
    endforeach()
    # (the exe's unreferenced functions dropped; a plugin keeps all of its own - tools/plugin_api.ts counts the engine
    # functions its code imports, called or not)
    string(REGEX REPLACE "/INCREMENTAL($| )" "/INCREMENTAL:NO /OPT:REF\\1" CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
           "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")
    foreach(kind SHARED MODULE)
        string(REGEX REPLACE "/INCREMENTAL($| )" "/INCREMENTAL:NO\\1" CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO
               "${CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO}")
    endforeach()
endif()

set(THUG2_GAME_DIR "C:/Games/THUG2/Game" CACHE PATH
    "The game folder of a THUG2 install (its Data folder) the built exe is copied into, if it exists (an install root steps into its Game folder)")

# --- SDL3 (the window and input: src/platform/sdl_*.cpp, src/engine/pad_input/sdl_input.cpp) --------------------
# The official 3.2.30 Visual C++ development package (PartyMod's window and input were written for SDL 2; the game
# moved to SDL 3 - its 3.2 series' last release). Downloaded and checked against its SHA-256 at configure time; not
# stored in the repo. Its headers are in include/SDL3, as the code includes them (<SDL3/...>).
include(FetchContent)
FetchContent_Declare(sdl3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.30/SDL3-devel-3.2.30-VC.zip
    URL_HASH SHA256=3a93c2182ddaf64692a8907928e6a54467f0f7c1caddf3019cfa69fbf1dfe3f2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sdl3)
add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES
    IMPORTED_IMPLIB "${sdl3_SOURCE_DIR}/lib/${THUG2_ARCH}/SDL3.lib"
    IMPORTED_LOCATION "${sdl3_SOURCE_DIR}/lib/${THUG2_ARCH}/SDL3.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")
# The package's SDL3.dll is copied into the game folder with the exe (below). SDL's controller mappings, the same
# way: the gamecontrollerdb.txt PartyMod ships is SDL_GameControllerDB's at commit 8b2d1a7 (2022-09-06), zlib (SDL 3
# reads SDL 2's mapping format).
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
# and checked against its SHA-256. src/gpu_d3d9/d3dx.cpp is the one place that calls it.
FetchContent_Declare(d3dx9
    URL https://www.nuget.org/api/v2/package/Microsoft.DXSDK.D3DX/9.29.952.8
    URL_HASH SHA256=ead0906ae8a26c18a7525da7490127a2110f7c58f18293738283e30e97c6ea4b
    DOWNLOAD_NAME Microsoft.DXSDK.D3DX.9.29.952.8.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(d3dx9)
set(THUG2_D3DX9_NATIVE "${d3dx9_SOURCE_DIR}/build/native")
add_library(d3dx9 SHARED IMPORTED)
set_target_properties(d3dx9 PROPERTIES
    IMPORTED_IMPLIB "${THUG2_D3DX9_NATIVE}/release/lib/${THUG2_ARCH}/d3dx9.lib"
    IMPORTED_LOCATION "${THUG2_D3DX9_NATIVE}/release/bin/${THUG2_ARCH}/D3DX9_43.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${THUG2_D3DX9_NATIVE}/include")

# --- Dear ImGui 1.91.9b, its docking branch (MIT, see THIRD_PARTY_NOTICES.md) ----------------------------
# Our UI layers (the level editor's src/editor/level_editor_ui*.cpp, the new front end's src/frontend/new_frontend*.cpp):
# the core (imgui), and the official SDL3 platform backend with the Direct3D 9 renderer backend (imgui_backends: the
# graphics backend's - only gpu_d3d9/d3d9_ui_backend.cpp includes their headers; render::UiBackend).
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
    URL_HASH SHA256=466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5
    # (the files dated when they are unpacked, not as in the archive: a new version is then newer than what an existing
    # build compiled from the old one, so everything that includes ImGui is rebuilt)
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEFAULT_FONT)
# One ImGui for the exe and its plugins (one context, GImGui, and one font atlas, which the new front end shares): built
# into the exe, which exports its API (IMGUI_API); a plugin compiles its headers through imgui_import, below.
target_compile_definitions(imgui PUBLIC "IMGUI_API=__declspec(dllexport)")
target_compile_options(imgui PRIVATE /W0)
add_library(imgui_import INTERFACE)
target_include_directories(imgui_import INTERFACE ${imgui_SOURCE_DIR})
target_compile_definitions(imgui_import INTERFACE IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEFAULT_FONT
                                                  "IMGUI_API=__declspec(dllimport)")
add_library(imgui_backends STATIC
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
# (the backends are the graphics backend's alone: not exported)
target_compile_definitions(imgui_backends PUBLIC "IMGUI_IMPL_API=")
target_link_libraries(imgui_backends PUBLIC imgui PRIVATE sdl3)
target_compile_options(imgui_backends PRIVATE /W0)

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

# --- Vulkan (the Vulkan renderer, src/gpu_vulkan: docs/design/vulkan.md; see THIRD_PARTY_NOTICES.md) ----------
# Fetched and pinned like the others; no Vulkan SDK is needed to build. The Vulkan loader is the system's
# vulkan-1.dll (installed with every Vulkan driver), loaded at run time by volk: nothing links an import library, and
# a machine without Vulkan still starts the exe. The SDK's validation layers are used when they are installed and asked
# for (src/gpu_vulkan/vulkan_context.h).
option(THUG2_VULKAN "Build the Vulkan renderer's library (src/gpu_vulkan) and its context check" ON)
if(THUG2_VULKAN)
    include(cmake/vulkan_libraries.cmake)
endif()
# The Vulkan renderer's upscalers (AMD FSR, NVIDIA DLSS through Streamline): optional, 64-bit (cmake/upscalers.cmake)
include(cmake/upscalers.cmake)

include(cmake/ffmpeg.cmake)

# --- The GameSpy SDK (IGN's BSD-3 release) ---------------------------------------------------------------------
# (on x64 too, since X9: its 64-bit fixes are in cmake/gamespy_sdk.cmake, the checks in docs/gamespy.md)
thug2_builds(gamespy_sdk THUG2_GAMESPY_SDK)
if(THUG2_GAMESPY_SDK)
    include(cmake/gamespy_sdk.cmake)
endif()
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

# --- The UI fonts (SIL Open Font License 1.1, see THIRD_PARTY_NOTICES.md) ---------------------------------
# Inter 4.1 (the UI) and JetBrains Mono 2.304 (numbers, names, checksums): the official release archives, checked
# against their SHA-256. The TTFs are compressed into the exe at build time with ImGui's own binary_to_compressed_c
# (stb compression; ImGui decompresses them at start-up) - nothing is added to the game folder. They stay in the exe
# (src/engine/ui/ui_fonts.cpp), not in the level editor's plugin: the loading console's panel uses them too.
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

# --- quickjs-ng 0.17.0 (MIT; THIRD_PARTY_NOTICES.md) ---------------------------------------------------------------
# The JavaScript engine the TypeScript scripts run in (src/script_qb/ts_bridge.cpp, docs/qb-ts.md "In the game"): its
# core only, no libc / OS modules. The same pinned release as scripting/ (the standalone runtime and its tests).
FetchContent_Declare(quickjs
    URL https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.17.0.tar.gz
    URL_HASH SHA256=559bc4c420475e55c7ab4510adbc562f55d7524d75e8e89d79ce4bb02f5687d9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(QJS_BUILD_LIBC OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(quickjs)
if(NOT quickjs_POPULATED)
    FetchContent_Populate(quickjs)
    add_subdirectory(${quickjs_SOURCE_DIR} ${quickjs_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

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

# --- The layers (docs/architecture.md "Layers"; tools/layers.json is the map, tools/cmake_layers.ts checks that the
# libraries below link as it says) ---------------------------------------------------------------------------------
# One static library per folder of src/, each in its folder's CMakeLists.txt, linking only the layers below it:
#   thug2_base <- thug2_platform, thug2_formats <- thug2_wire;
#   thug2_platform + thug2_wire <- thug2_engine <- thug2_game <- thug2_frontend, thug2_editor, thug2_devtools <- the exe (src/app);
#   thug2_engine <- the backends: thug2_render_pc (graphics: the API-neutral PC platform layer) and thug2_gpu_d3d9 (its Direct3D 9 Gpu), thug2_gpu_vulkan (graphics, Vulkan: not drawing yet), thug2_audio_dsound (sound), thug2_media_ffmpeg (movies),
#   thug2_input_dinput (DirectInput), thug2_fs_win32 (files), thug2_net_winsock (sockets) - linked by the exe alone.
# The exes are written next to each other at the top of the build folder (thug2-decomp.exe and the checks).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# THUG2_GAME_DIR is the folder holding the game. A stock install keeps the game in a Game subfolder
# (C:/Games/THUG2/Game) - some players delete what's around it and keep Game's contents at the top: a cached
# install root (C:/Games/THUG2) steps into its Game folder when the game isn't at the top (as thug2-level-converter-core/lib/install.ts).
# When it exists, every build copies the exe, its PDB and its DLLs into it (src/app/CMakeLists.txt), and the level
# editor plugin into its plugins folder (src/editor/CMakeLists.txt).
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
# (on x64 only the x64-ready ones: THUG2_X64_READY above; the checks build when the libraries they link do)
foreach(library IN ITEMS base formats wire platform engine render_pc gpu_d3d9 gpu_vulkan audio_dsound media_ffmpeg
                         input_dinput fs_win32 net_winsock script_qb game frontend editor devtools app)
    thug2_builds(${library} build_library)
    if(build_library AND NOT (library STREQUAL "gpu_vulkan" AND NOT THUG2_VULKAN))
        add_subdirectory(src/${library})
    endif()
endforeach()
add_subdirectory(src/tests)
# The level editor's launcher, thug2-editor.exe (no layer of the game: src/editor/launcher/CMakeLists.txt).
add_subdirectory(src/editor/launcher)
# The retail-limits DLL for the original game (32-bit only: src/retail_limits/CMakeLists.txt).
if(THUG2_ARCH STREQUAL "x86")
    add_subdirectory(src/retail_limits)
endif()

# On x64 the fetched libraries the build compiles itself are built only when an x64-ready library links them.
if(THUG2_ARCH STREQUAL "x64")
    set_target_properties(imgui imgui_backends stb_image_write lzham imgui_binary_to_compressed
        PROPERTIES EXCLUDE_FROM_ALL TRUE)
    if(THUG2_VULKAN)
        set_target_properties(volk vulkan_memory_allocator PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endif()
