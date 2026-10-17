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

set(THUG2_GAME_DIR "C:/Games/THUG2" CACHE PATH "Game folder the built dinput8.dll is copied into")

# --- SDL2 (the PC port's window and input: src/game/sys/sdl_*.cpp, src/game/sio/sdl_input.cpp) --------------
# The official 2.30.9 Visual C++ development package: the same version as the SDL2.dll the PC port ships with (the
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

# The rewritten game code and the hook machinery, shared by the DLL and the check tool.
add_library(thug2_code OBJECT
    src/hook/hook.cpp
    src/hook/log.cpp
    src/hook/shadow.cpp
    src/hook/selftest.cpp
    src/hook/selftest_runner.cpp
    src/hook/selftests_skaterB.cpp
    src/hook/selftests_gfx.cpp
    src/hook/selftests_platform.cpp
    src/hook/selftests_pc.cpp
    src/hook/selftests_glyph.cpp
    src/hook/selftests_skaterA.cpp
    src/hook/selftests_gfx2.cpp
    src/hook/selftests_mainloop.cpp
    src/hook/selftests_misc.cpp
    src/hook/selftests_scache.cpp
    src/hook/selftests_text.cpp
    src/hook/selftests_winmain.cpp
    src/hook/selftests_gapsB.cpp
    src/hook/selftests_qb.cpp
    src/hook/selftests_gapsA.cpp
    src/hook/selftests_csD.cpp
    src/hook/selftests_csA.cpp
    src/hook/selftests_csC.cpp
    src/hook/selftests_csB.cpp
    src/hook/selftests_parkedA.cpp
    src/game/core/alloc.cpp
    src/game/core/crc.cpp
    src/game/core/dbg.cpp
    src/game/core/heap.cpp
    src/game/core/manager.cpp
    src/game/core/mem.cpp
    src/game/core/pool.cpp
    src/game/core/singleton.cpp
    src/game/core/stubs.cpp
    src/game/core/str.cpp
    src/game/core/string.cpp
    src/game/file/asset_bundle.cpp
    src/game/front/screen_element.cpp
    src/game/gfx/screen.cpp
    src/game/gfx/settings.cpp
    src/game/gfx/glyphs.cpp
    src/game/gfx/flash.cpp
    src/game/gfx/font.cpp
    src/game/gfx/text.cpp
    src/game/gfx/device.cpp
    src/game/gfx/movie.cpp
    src/game/pcm/music.cpp
    src/game/gfx/loading_bar.cpp
    src/game/gfx/render.cpp
    src/game/file/async.cpp
    src/game/file/file.cpp
    src/game/file/pip.cpp
    src/game/file/pre.cpp
    src/game/lst/hashtable.cpp
    src/game/lst/search.cpp
    src/game/net/chat.cpp
    src/game/net/stats.cpp
    src/game/ed/park_manager.cpp
    src/game/ed/cursor.cpp
    src/game/ed/menu.cpp
    src/game/ed/park_map.cpp
    src/game/net/service.cpp
    src/game/net/online_hosts.cpp
    src/game/obj/flip_rotate.cpp
    src/game/obj/smtptr.cpp
    src/game/skater/rotation.cpp
    src/game/sio/device.cpp
    src/game/sio/sdl_input.cpp
    src/game/skater/controls.cpp
    src/game/skater/core_physics.cpp
    src/game/sys/platform.cpp
    src/game/sys/platform_cfuncs.cpp
    src/game/sys/config.cpp
    src/game/sys/debug_log.cpp
    src/game/sys/pc_setup.cpp
    src/game/sys/sdl_events.cpp
    src/game/sys/sdl_window.cpp
    src/game/sys/registry.cpp
    src/game/sys/timer.cpp
    src/game/sys/main.cpp
    src/game/sys/mlp.cpp
    src/game/sys/winmain.cpp
    src/game/mc/memcard.cpp
    src/game/mc/memcard_cfuncs.cpp
    src/game/script/eval.cpp
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
target_link_libraries(thug2_code PUBLIC minhook sdl2 ws2_32) # ws2_32: the command line's server address
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

if(EXISTS "${THUG2_GAME_DIR}")
    add_custom_command(TARGET thug2_decomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2_decomp> "${THUG2_GAME_DIR}"
        COMMENT "Copying dinput8.dll into ${THUG2_GAME_DIR}")
endif()

# thug2_check: the start-up checks and the early self-tests against THUG2PM.exe's code, without starting the
# game (see tools/check/main.cpp):  thug2_check [<path to THUG2PM.exe>]
add_executable(thug2_check tools/check/main.cpp)
target_link_libraries(thug2_check PRIVATE thug2_code minhook delayimp)
# The PC port's SDL code is linked (window, input: never run offline), so SDL2 is only loaded if used.
target_link_options(thug2_check PRIVATE /DELAYLOAD:SDL2.dll)
target_compile_options(thug2_check PRIVATE /W4 /permissive- /utf-8)
# Keep the tool itself away from 0x400000: its child process loads the game image there.
target_link_options(thug2_check PRIVATE /BASE:0x20000000 /DYNAMICBASE:NO)
