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

# --- SDL2 (used by the PartyMod code for windowing and input) -------------------------------------------
# The official 2.30.9 Visual C++ development package: the same version as the SDL2.dll PartyMod ships with.
# Downloaded and checked against its SHA-256 at configure time; not stored in the repo.
include(FetchContent)
FetchContent_Declare(sdl2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip
    URL_HASH SHA256=8c91d91e5bcb997d062ec2b553c53832ebf95654d4aa35e8c02a954d4ce752ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sdl2)
# The package has its headers directly in include/; PartyMod includes them as <SDL2/...>.
file(COPY "${sdl2_SOURCE_DIR}/include/" DESTINATION "${CMAKE_BINARY_DIR}/sdl2-include/SDL2")
add_library(sdl2 SHARED IMPORTED)
set_target_properties(sdl2 PROPERTIES
    IMPORTED_IMPLIB "${sdl2_SOURCE_DIR}/lib/x86/SDL2.lib"
    IMPORTED_LOCATION "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/sdl2-include")

# --- PartyMod (ported, MIT licence: see src/partymod/LICENSE and UPSTREAM.md) ---------------------------
add_library(partymod STATIC
    src/partymod/cfunc.c
    src/partymod/config.c
    src/partymod/event.c
    src/partymod/gfx.c
    src/partymod/glyph.c
    src/partymod/input.c
    src/partymod/log.c
    src/partymod/main.c
    src/partymod/net.c
    src/partymod/patch.c
    src/partymod/patches.c
    src/partymod/script.c
    src/partymod/window.c
    src/partymod/util/bps.c
    src/partymod/util/hash.c
    src/partymod/util/sb.c
)
target_include_directories(partymod PRIVATE src/partymod src/partymod/thirdparty)
target_compile_definitions(partymod PRIVATE THUG2_DECOMP)
# Upstream code, kept close to the original: it relies on int-to-pointer conversions for game addresses that
# MSVC warns about, so its warnings are off rather than editing every line.
target_compile_options(partymod PRIVATE /W0 /utf-8)
target_link_libraries(partymod PUBLIC sdl2)

add_library(thug2_decomp SHARED
    src/proxy/dinput8.cpp
    src/proxy/dinput8.def
    src/hook/hook.cpp
    src/hook/log.cpp
    src/hook/selftest.cpp
    src/game/core/crc.cpp
    src/game/core/dbg.cpp
    src/game/core/mem.cpp
    src/game/core/str.cpp
    src/game/core/string.cpp
    src/game/mth/geometry.cpp
    src/game/mth/math.cpp
    src/game/mth/matrix.cpp
    src/game/mth/random.cpp
    src/game/mth/slerp.cpp
    src/game/mth/vector.cpp
    src/game/util/bitstream.cpp
    src/game/util/lzss.cpp
)
target_include_directories(thug2_decomp PRIVATE src)
target_link_libraries(thug2_decomp PRIVATE partymod)
target_compile_definitions(thug2_decomp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(thug2_decomp PRIVATE /W4 /permissive- /utf-8)
# The proxy has to be called dinput8.dll for Windows to load it instead of the system one.
set_target_properties(thug2_decomp PROPERTIES OUTPUT_NAME dinput8)

if(EXISTS "${THUG2_GAME_DIR}")
    add_custom_command(TARGET thug2_decomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2_decomp> "${THUG2_GAME_DIR}"
        COMMENT "Copying dinput8.dll into ${THUG2_GAME_DIR}")
endif()
