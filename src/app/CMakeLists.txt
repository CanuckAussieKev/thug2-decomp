# The app (src/app): the composition root, which may include every layer. Two exes share it:
# - thug2-decomp.exe (target thug2), the game built from this source, with the new front end, the level editor and the
#   dev tools;
# - thug2-editor.exe (target thug2-editor), the level editor on the engine and the game: no new front end, no dev tools
#   (tools/cmake_layers.ts checks it reaches neither). -edit <level> starts it in the editor in that level.
# Both run from the game folder of the player's own THUG2 install (its Data folder, binkw32.dll): README.md "Building and
# running".
# What they share - WinMain, the start-up, the main loop, the command and component tables - is the object library
# thug2_app; each exe adds its own extensions (app/extensions.h: extensions.cpp, editor_extensions.cpp).
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
                                       thug2_audio_dsound thug2_media_bink thug2_input_dinput thug2_fs_win32 thug2_net_winsock
                                       thug2_script_qb thug2_engine
                                       thug2_wire thug2_formats thug2_platform thug2_base)
# The Vulkan backend (src/gpu_vulkan): installed by app/renderer_choice.cpp with renderer=vulkan.
if(THUG2_VULKAN)
    target_link_libraries(thug2_app PUBLIC thug2_gpu_vulkan)
    target_compile_definitions(thug2_app PRIVATE THUG2_VULKAN)
endif()

add_executable(thug2 WIN32
    extensions.cpp
)
set_target_properties(thug2 PROPERTIES OUTPUT_NAME thug2-decomp)
# Every layer, highest first (the libraries bring the system and third-party libraries they need).
target_link_libraries(thug2 PRIVATE thug2_app thug2_frontend thug2_editor thug2_devtools thug2_game thug2_render_pc thug2_gpu_d3d9
                                    thug2_audio_dsound thug2_media_bink thug2_input_dinput thug2_fs_win32 thug2_net_winsock
                                    thug2_script_qb thug2_engine
                                    thug2_wire thug2_formats thug2_platform thug2_base)

add_executable(thug2-editor WIN32
    editor_extensions.cpp
)
set_target_properties(thug2-editor PROPERTIES OUTPUT_NAME thug2-editor)
# The editor, the game and below - not the new front end, not the dev tools.
target_link_libraries(thug2-editor PRIVATE thug2_app thug2_editor thug2_game thug2_render_pc thug2_gpu_d3d9
                                           thug2_audio_dsound thug2_media_bink thug2_input_dinput thug2_fs_win32 thug2_net_winsock
                                           thug2_script_qb thug2_engine
                                           thug2_wire thug2_formats thug2_platform thug2_base)
# The DLLs the exe needs, next to it: d3dx9_43.dll (the package's Microsoft-distributable runtime; see
# THIRD_PARTY_NOTICES.md), SDL2.dll and SDL's controller mappings.
add_custom_command(TARGET thug2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_D3DX9_NATIVE}/release/bin/x86/D3DX9_43.dll" "$<TARGET_FILE_DIR:thug2>/d3dx9_43.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl2_SOURCE_DIR}/lib/x86/SDL2.dll" "$<TARGET_FILE_DIR:thug2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${THUG2_CONTROLLER_DB}" "$<TARGET_FILE_DIR:thug2>"
    COMMENT "Copying d3dx9_43.dll, SDL2.dll and gamecontrollerdb.txt next to thug2-decomp.exe")

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
    add_custom_command(TARGET thug2-editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2-editor> $<TARGET_PDB_FILE:thug2-editor> "${THUG2_DEPLOY_DIR}"
        COMMENT "Copying thug2-editor.exe into ${THUG2_DEPLOY_DIR}")
endif()
