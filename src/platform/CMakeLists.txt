# The platform layer (src/platform): the OS and SDK side - the SDL window and its event pump, the run log and crash
# reports, the C runtime's game-visible parts, the settings file, the registry,
# the GameSpy SDK's glue (the SDK keeps its own Winsock sockets), D3DX and the screenshot files. It includes nothing of the project but base (tools/layers.ts
# checks it); what the game does on its events and with its devices is passed in or handed back by app/ (WinMain,
# the start-up).
add_library(thug2_platform STATIC
    crash.cpp
    crt.cpp
    debug_log.cpp
    gamespy.cpp
    mute_audio.cpp
    output_sinks.cpp
    platform_config.cpp
    platform_system.cpp
    process_flags.cpp
    registry.cpp
    run_log.cpp
    screenshot_files.cpp
    sdl_keys.cpp
    sdl_events.cpp
    sdl_window.cpp
)
target_include_directories(thug2_platform PRIVATE "${stb_SOURCE_DIR}")
# sdl3: the window and its events; ole32: the audio session (mute_audio); d3d9, winmm: crt.h's imports;
# gamespy_sdk: gamespy.cpp (it links Winsock, ws2_32, itself); stb_image_write: the PNG files
target_link_libraries(thug2_platform PUBLIC thug2_base sdl3 ole32 d3d9 winmm gamespy_sdk stb_image_write)
