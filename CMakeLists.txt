cmake_minimum_required(VERSION 3.25)
project(thug2_decomp LANGUAGES CXX)

# The game is a 32-bit exe, so everything that loads into it must be 32-bit.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with the x86 preset: the game is 32-bit.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the DLL doesn't need a separate runtime installed next to the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(THUG2_GAME_DIR "C:/Games/THUG2" CACHE PATH "Game folder the built dinput8.dll is copied into")

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
target_compile_definitions(thug2_decomp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(thug2_decomp PRIVATE /W4 /permissive- /utf-8)
# The proxy has to be called dinput8.dll for Windows to load it instead of the system one.
set_target_properties(thug2_decomp PROPERTIES OUTPUT_NAME dinput8)

if(EXISTS "${THUG2_GAME_DIR}")
    add_custom_command(TARGET thug2_decomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2_decomp> "${THUG2_GAME_DIR}"
        COMMENT "Copying dinput8.dll into ${THUG2_GAME_DIR}")
endif()
