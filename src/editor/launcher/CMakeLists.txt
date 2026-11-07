# The level editor's launcher (src/editor/launcher): thug2-editor.exe, beside thug2-decomp.exe in the game folder - it
# starts the game in the editor mode (-editor) and ends. Plain Win32: no layer of the game (tools/layers.json
# editor_launcher), so it moves with the editor when the editor gets a repository of its own. Its icon:
# tools/editor_icon.ts.
add_executable(thug2_editor_launcher WIN32
    thug2_editor_launcher.cpp
    thug2-editor.rc
)
set_target_properties(thug2_editor_launcher PROPERTIES OUTPUT_NAME thug2-editor)
# Into the game folder with the exe (THUG2_DEPLOY_DIR: the root CMakeLists.txt).
if(EXISTS "${THUG2_DEPLOY_DIR}")
    add_custom_command(TARGET thug2_editor_launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2_editor_launcher>
                $<TARGET_PDB_FILE:thug2_editor_launcher> "${THUG2_DEPLOY_DIR}"
        COMMENT "Copying thug2-editor.exe into ${THUG2_DEPLOY_DIR}")
endif()
