# The platform layer (src/platform): the OS and SDK side - the SDL window and its event pump, the run log and crash
# reports, the C runtime's game-visible parts, the settings file, the registry, Winsock,
# the GameSpy SDK's glue, D3DX and the screenshot files. It includes nothing of the project but base (tools/layers.ts
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
    sdl_events.cpp
    sdl_window.cpp
    sockets.cpp
)
target_include_directories(thug2_platform PRIVATE "${stb_SOURCE_DIR}")
# sdl2: the window and its events; ole32: the audio session (mute_audio); ws2_32: Winsock;
# d3d9, winmm: crt.h's imports; gamespy_sdk: gamespy.cpp; stb_image_write: the PNG files
target_link_libraries(thug2_platform PUBLIC thug2_base sdl2 ws2_32 ole32 d3d9 winmm gamespy_sdk stb_image_write)
# crt.cpp and sockets.cpp call Winsock's IPv4-only functions the game uses (inet_addr, inet_ntoa, gethostbyname): Winsock
# marks them deprecated unless this is defined.
set_source_files_properties(crt.cpp sockets.cpp PROPERTIES COMPILE_DEFINITIONS _WINSOCK_DEPRECATED_NO_WARNINGS)
