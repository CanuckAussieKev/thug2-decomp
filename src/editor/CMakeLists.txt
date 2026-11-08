# The level editor (src/editor): the in-game level editor and its Dear ImGui UI, a plugin -
# plugins\thug2-level-editor.dll next to the exe, which loads it at start-up (engine/ext/plugin.h) and puts its
# extension (ext::Extension) at the editor's place in the extension order. It includes the game and below
# (tools/layers.ts checks it) and calls them in the exe: the functions marked THUG2_API (base/core/api.h) and Dear
# ImGui's, through the exe's import library (tools/plugin_api.ts checks that the exe exports what the plugin imports).
add_library(thug2_editor SHARED
    level_editor_plugin.cpp
    level_editor.cpp
    level_editor_api.cpp
    level_editor_frame.cpp
    level_editor_pointer.cpp
    level_editor_keys.cpp
    level_editor_commands.cpp
    level_editor_requests.cpp
    level_editor_place.cpp
    level_editor_presets.cpp
    level_editor_lighting.cpp
    level_editor_panels.cpp
    level_editor_draw.cpp
    level_editor_drag.cpp
    level_editor_undo.cpp
    level_editor_selection.cpp
    level_editor_geometry.cpp
    level_editor_gizmo_lines.cpp
    level_editor_hud.cpp
    level_editor_layout.cpp
    level_editor_layout_text.cpp
    level_editor_objects.cpp
    level_editor_picking.cpp
    level_editor_shadows.cpp
    level_editor_view.cpp
    level_editor_walls.cpp
    level_editor_faces.cpp
    level_editor_clusters.cpp
    level_editor_cameras.cpp
    level_editor_goals.cpp
    level_editor_goals_ui.cpp
    level_editor_checks.cpp
    level_editor_budget.cpp
    level_editor_notes.cpp
    level_editor_ground.cpp
    level_editor_ground_game.cpp
    level_editor_gap_wizard.cpp
    level_editor_skate_view.cpp
    level_editor_ux9.cpp
    level_editor_rail_tools.cpp
    level_editor_auto_rails.cpp
    level_editor_carry.cpp
    level_editor_scenery.cpp
    level_editor_scenery_state.cpp
    level_editor_scenery_pick.cpp
    level_editor_scenery_groups.cpp
    level_editor_scale.cpp
    level_editor_coll_tree.cpp
    level_editor_rotate.cpp
    level_editor_shared_coll.cpp
    level_editor_ui.cpp
    level_editor_ui_app.cpp
    level_editor_split.cpp
    level_editor_split_ui.cpp
    level_editor_ui_dialogs.cpp
    level_editor_ui_inspector.cpp
    level_editor_ui_outliner.cpp
    level_editor_gui_text.cpp
    level_editor_camera.cpp
    level_editor_play.cpp
    level_editor_open.cpp
    level_editor_shell.cpp
    level_editor_import.cpp
    level_editor_content.cpp
    level_editor_ghost.cpp
    level_editor_scene_tools.cpp
    level_editor_props.cpp
    level_editor_prefabs.cpp
    level_editor_playtest.cpp
    level_editor_playtest_ui.cpp
    level_editor_prefs.cpp
    level_editor_ui_layout.cpp
    level_editor_ui_menus.cpp
    level_editor_ui_strip.cpp
    level_editor_ui_theme.cpp
    level_editor_nodes.cpp
    level_editor_node_edit.cpp
    level_editor_node_create.cpp
    level_editor_triggers.cpp
    level_editor_triggers_edit.cpp
    level_editor_triggers_draw.cpp
    level_editor_triggers_sync.cpp
    level_editor_triggers_records.cpp
    level_editor_triggers_ui.cpp
    level_editor_entities.cpp
    level_editor_entities_ui.cpp
    level_editor_extension.cpp
    level_editor_preview.cpp
    level_editor_overlays.cpp
    level_editor_overlays_ui.cpp
    level_editor_arcs.cpp
    level_editor_grind_preview.cpp
    level_editor_ruler.cpp
    level_editor_thumbs.cpp
    level_editor_chains.cpp
    level_editor_chains_ops.cpp
    level_editor_chains_restore.cpp
    level_editor_chains_records.cpp
    level_editor_chains_ui.cpp
    level_editor_spots_ui.cpp
    level_editor_fields.cpp
    level_editor_fields_ui.cpp
    level_editor_level_props.cpp
    level_editor_actions.cpp
    level_editor_changes.cpp
    level_editor_organize.cpp
    level_editor_clip.cpp
    level_editor_ux3.cpp
    level_editor_ux3_ui.cpp
    level_editor_hints.cpp
    level_editor_nav.cpp
    level_editor_ux8.cpp
    level_editor_ux8_pie.cpp
    level_editor_ux8_ui.cpp
    level_editor_snap.cpp
    level_editor_transform.cpp
    level_editor_transform_ui.cpp
    level_editor_adjust.cpp
    level_editor_arrange.cpp
    level_editor_arrange_ui.cpp
    level_editor_links.cpp
)
set_target_properties(thug2_editor PROPERTIES OUTPUT_NAME thug2-level-editor
                                              RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
# (its headers' THUG2_API: imported)
target_compile_definitions(thug2_editor PRIVATE THUG2_PLUGIN)
# The game's headers and below, compiled against but not linked (their code is the exe's: thug2, its import
# library); imgui_import: the editor's UI (level_editor_ui_theme.h includes it), ImGui's API in the exe; SDL3: the
# clipboard and the clock (the same SDL3.dll as the exe's).
target_link_libraries(thug2_editor PRIVATE $<COMPILE_ONLY:thug2_game> thug2 imgui_import sdl3)
# The build checks the exe's exports against what the plugin imports (tools/plugin_api.ts), when Node is found.
find_program(THUG2_NODE node)
if(THUG2_NODE)
    add_custom_command(TARGET thug2_editor POST_BUILD
        COMMAND "${THUG2_NODE}" "${CMAKE_SOURCE_DIR}/tools/plugin_api.ts" --exe $<TARGET_FILE:thug2>
                --plugin $<TARGET_FILE:thug2_editor> --imgui $<TARGET_FILE:imgui>
        COMMENT "Checking the exe's exports against the level editor plugin's imports")
endif()
# Into the game folder's plugins folder with the exe (THUG2_DEPLOY_DIR: the root CMakeLists.txt).
if(EXISTS "${THUG2_DEPLOY_DIR}")
    add_custom_command(TARGET thug2_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${THUG2_DEPLOY_DIR}/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:thug2_editor> $<TARGET_PDB_FILE:thug2_editor>
                "${THUG2_DEPLOY_DIR}/plugins"
        COMMENT "Copying thug2-level-editor.dll into ${THUG2_DEPLOY_DIR}/plugins")
endif()
