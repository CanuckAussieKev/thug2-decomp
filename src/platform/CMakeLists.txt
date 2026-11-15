# The platform layer (src/platform): the OS and SDK side - the SDL window and its event pump, the run log and crash
# reports, the C runtime's game-visible parts, the settings file, the registry,
# the GameSpy SDK's glue (the SDK keeps its own Winsock sockets), D3DX and the screenshot files. It includes nothing of the project but base (tools/layers.ts
# checks it); what the game does on its events and with its devices is passed in or handed back by app/ (WinMain,
# the start-up).
add_library(thug2_platform STATIC
    crash.cpp
    crt.cpp
    debug_log.cpp
    frame_profile.cpp
    latency_probe.cpp
    mute_audio.cpp
    test_workspace.cpp
    output_sinks.cpp
    platform_config.cpp
    platform_system.cpp
    process_flags.cpp
    registry.cpp
    run_log.cpp
    screenshot_files.cpp
    std_streams.cpp
    sdl_keys.cpp
    sdl_events.cpp
    sdl_window.cpp
    app_window.cpp
    child_process.cpp
    folder_dialog.cpp
)
# NOT ORIGINAL - a portable build leaves out the five files whose Win32 side has no counterpart here (structured
# exception handling, the x64 unwind sampler, the audio session, the application window's Win32 calls, the child
# processes' job objects) and takes port/platform_stubs.cpp for their interfaces. (They stay in the list above:
# tools/cmake_layers.ts reads it there.)
if(THUG2_PORTABLE)
    set_source_files_properties(crash.cpp frame_profile.cpp mute_audio.cpp app_window.cpp child_process.cpp
        PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(thug2_platform PRIVATE ${PROJECT_SOURCE_DIR}/port/platform_stubs.cpp)
endif()
target_include_directories(thug2_platform PRIVATE "${stb_SOURCE_DIR}")
# sdl3: the window and its events; ole32: the audio session (mute_audio); d3d9, winmm: crt.h's imports;
# stb_image_write: the PNG files
if(THUG2_PORTABLE)
    # NOT ORIGINAL - a portable build's platform layer links SDL3 and the PNG writer; ole32 (the audio session), d3d9 and
    # winmm (crt.h's imports), shell32 / shlwapi / propsys / comctl32 (the folder dialog, the window's own calls) are
    # Windows' and are replaced by stubs in port/ (port/README.md).
    target_link_libraries(thug2_platform PUBLIC thug2_base sdl3 stb_image_write)
else()
    target_link_libraries(thug2_platform PUBLIC thug2_base sdl3 ole32 d3d9 winmm stb_image_write shell32 shlwapi propsys comctl32)
endif()
# The GameSpy glue (gamespy.cpp: it links the SDK, which links Winsock, ws2_32, itself) - in a configure that builds
# the SDK (THUG2_GAMESPY_SDK: x86 and, since X9, x64).
if(THUG2_GAMESPY_SDK)
    target_sources(thug2_platform PRIVATE gamespy.cpp)
    target_link_libraries(thug2_platform PUBLIC gamespy_sdk)
endif()
