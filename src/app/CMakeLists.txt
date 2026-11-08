# The app (src/app): the composition root, which may include every layer but the level editor's:
# - thug2-decomp.exe (target thug2), the game built from this source, with the new front end and the dev tools. It
#   runs from the game folder of the player's own THUG2 install (its Data folder): README.md "Building and running".
# - The level editor is a plugin (src/editor: plugins\thug2-level-editor.dll, engine/ext/plugin.h) the exe loads at
#   start-up when it is there; the exe exports the engine API it calls (the functions marked THUG2_API,
#   base/core/api.h, and Dear ImGui's) - ENABLE_EXPORTS: thug2-decomp.lib, the import library the plugin links.
# WinMain, the start-up, the main loop, the command and component tables are the object library thug2_app; the exe
# adds its extensions (app/extensions.h: extensions.cpp, the editor's slot without the plugin: no_editor.cpp).
add_library(thug2_app OBJECT
    main.cpp
    component_types.cpp
    pc_setup.cpp
    startup.cpp
    winmain.cpp
    startup_init.cpp
    command_table.cpp
    renderer_choice.cpp
)
# The layers the shared start-up composes (the backends it installs, the game and below).
target_link_libraries(thug2_app PUBLIC thug2_game thug2_render_pc thug2_gpu_d3d9
                                       thug2_audio_dsound thug2_media_ffmpeg thug2_input_dinput thug2_fs_win32 thug2_net_winsock
                                       thug2_script_qb thug2_engine
                                       thug2_wire thug2_formats thug2_platform thug2_base)
# The Vulkan backend (src/gpu_vulkan): installed by app/renderer_choice.cpp with renderer=vulkan.
if(THUG2_VULKAN)
    target_link_libraries(thug2_app PUBLIC thug2_gpu_vulkan)
    target_compile_definitions(thug2_app PRIVATE THUG2_VULKAN)
endif()

add_executable(thug2 WIN32
    extensions.cpp
    no_editor.cpp
)
set_target_properties(thug2 PROPERTIES OUTPUT_NAME thug2-decomp ENABLE_EXPORTS ON)
# Every layer, highest first (the libraries bring the system and third-party libraries they need). The platform layer
# whole: some of what it exports is for the plugin alone (platform/child_process.h, folder_dialog.h - the level
# editor's Import Level), which the exe itself never calls, and a static library's unreferenced objects aren't linked.
target_link_libraries(thug2 PRIVATE thug2_app thug2_frontend thug2_devtools thug2_game thug2_render_pc thug2_gpu_d3d9
                                    thug2_audio_dsound thug2_media_ffmpeg thug2_input_dinput thug2_fs_win32 thug2_net_winsock
                                    thug2_script_qb thug2_engine
                                    thug2_wire thug2_formats $<LINK_LIBRARY:WHOLE_ARCHIVE,thug2_platform> thug2_base)
# The DLLs the exe needs, next to it: d3dx9_43.dll (the package's Microsoft-distributable runtime; see
# THIRD_PARTY_NOTICES.md), SDL3.dll and SDL's controller mappings.
add_custom_command(TARGET thug2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/${THUG2_ARCH}/D3DX9_43.dll" "$<TARGET_FILE_DIR:thug2>/d3dx9_43.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl3_SOURCE_DIR}/lib/${THUG2_ARCH}/SDL3.dll" "$<TARGET_FILE_DIR:thug2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "$<TARGET_FILE_DIR:thug2>"
    COMMENT "Copying d3dx9_43.dll, SDL3.dll and gamecontrollerdb.txt next to thug2-decomp.exe")
# FFmpeg's DLLs (the FFmpeg movie codec's, when tools/ffmpeg/build_ffmpeg.ts has built them: cmake/ffmpeg.cmake).
if(THUG2_FFMPEG_DLLS)
    add_custom_command(TARGET thug2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THUG2_FFMPEG_DLLS} "$<TARGET_FILE_DIR:thug2>"
        COMMENT "Copying FFmpeg's DLLs next to thug2-decomp.exe")
endif()

# The C runtime's DLLs (the C runtime is a DLL: the root CMakeLists.txt): a Release build copies the ones the exe and
# the plugin import - the Visual C++ redistributable's files, from the compiler's own redist folder - next to the exe,
# so the game needs no runtime installed. (The Universal C Runtime, ucrtbase.dll, is part of Windows. A Debug build
# uses the debug runtime Visual Studio installs: for this machine only.)
set(THUG2_RUNTIME_DLLS "")
if(MSVC)
    set(redist_root "$ENV{VCToolsRedistDir}")
    if(NOT redist_root)
        # (.../VC/Tools/MSVC/<version>/bin/Host<arch>/<arch>/cl.exe -> .../VC/Redist/MSVC/<version>)
        get_filename_component(tools_version_dir "${CMAKE_CXX_COMPILER}/../../../.." ABSOLUTE)
        get_filename_component(tools_version "${tools_version_dir}" NAME)
        get_filename_component(redist_root "${tools_version_dir}/../../../Redist/MSVC/${tools_version}" ABSOLUTE)
    endif()
    file(GLOB redist_crt LIST_DIRECTORIES TRUE "${redist_root}/${THUG2_ARCH}/Microsoft.VC*.CRT")
    set(runtime_names vcruntime140.dll msvcp140.dll)
    if(THUG2_ARCH STREQUAL "x64")
        list(APPEND runtime_names vcruntime140_1.dll)
    endif()
    foreach(name IN LISTS runtime_names)
        if(redist_crt AND EXISTS "${redist_crt}/${name}")
            list(APPEND THUG2_RUNTIME_DLLS "${redist_crt}/${name}")
        endif()
    endforeach()
    list(LENGTH runtime_names wanted)
    list(LENGTH THUG2_RUNTIME_DLLS found)
    if(NOT found EQUAL wanted)
        set(THUG2_RUNTIME_DLLS "")
        message(STATUS "The Visual C++ runtime's DLLs aren't in ${redist_root}/${THUG2_ARCH}: a Release build won't "
                       "copy them next to the exe (the game then needs the Visual C++ redistributable installed)")
    endif()
endif()
if(THUG2_RUNTIME_DLLS)
    add_custom_command(TARGET thug2 POST_BUILD
        COMMAND "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_COMMAND};-E;copy_if_different;${THUG2_RUNTIME_DLLS};$<TARGET_FILE_DIR:thug2>>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying the C runtime's DLLs next to thug2-decomp.exe (a Release build)")
endif()

# When the game folder exists (THUG2_DEPLOY_DIR: the root CMakeLists.txt), every build copies the exe, its PDB and
# the DLLs above into it.
if(EXISTS "${THUG2_DEPLOY_DIR}")
    add_custom_command(TARGET thug2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2> $<TARGET_PDB_FILE:thug2> "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/${THUG2_ARCH}/D3DX9_43.dll" "${THUG2_DEPLOY_DIR}/d3dx9_43.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl3_SOURCE_DIR}/lib/${THUG2_ARCH}/SDL3.dll" "${THUG2_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "${THUG2_DEPLOY_DIR}"
        COMMENT "Copying thug2-decomp.exe and its DLLs into ${THUG2_DEPLOY_DIR}")
    if(THUG2_FFMPEG_DLLS)
        add_custom_command(TARGET thug2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THUG2_FFMPEG_DLLS} "${THUG2_DEPLOY_DIR}"
            COMMENT "Copying FFmpeg's DLLs into ${THUG2_DEPLOY_DIR}")
    endif()
    if(THUG2_RUNTIME_DLLS)
        add_custom_command(TARGET thug2 POST_BUILD
            COMMAND "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_COMMAND};-E;copy_if_different;${THUG2_RUNTIME_DLLS};${THUG2_DEPLOY_DIR}>"
            COMMAND_EXPAND_LISTS
            COMMENT "Copying the C runtime's DLLs into ${THUG2_DEPLOY_DIR} (a Release build)")
    endif()
endif()
