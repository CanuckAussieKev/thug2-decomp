# The level editor (src/editor): the in-game level editor and its Dear ImGui UI - an extension the app registers
# (ext::Extension); its own exe will link it too. It includes the game and below (tools/layers.ts checks it), plus the
# front end's mouse menus until the editor / front end API (tools/layers_baseline.txt).
add_library(thug2_editor STATIC
    level_editor.cpp
    level_editor_hud.cpp
    level_editor_layout.cpp
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
    level_editor_carry.cpp
    level_editor_scenery.cpp
    level_editor_scale.cpp
    level_editor_shared_coll.cpp
    level_editor_ui.cpp
    level_editor_gui_text.cpp
    level_editor_camera.cpp
    level_editor_play.cpp
    level_editor_playtest.cpp
    level_editor_playtest_ui.cpp
    level_editor_prefs.cpp
    level_editor_ui_backend.cpp
    level_editor_ui_layout.cpp
    level_editor_ui_menus.cpp
    level_editor_ui_strip.cpp
    level_editor_ui_theme.cpp
    level_editor_nodes.cpp
    level_editor_node_edit.cpp
    level_editor_node_create.cpp
    level_editor_triggers.cpp
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
# imgui: the editor's UI (level_editor_ui_theme.h includes it)
target_link_libraries(thug2_editor PUBLIC thug2_game imgui)
# The sibling edge tools/layers_baseline.txt lists (editor -> frontend: the mouse menus).
target_link_libraries(thug2_editor PRIVATE thug2_frontend)
