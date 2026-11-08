#pragma once

#include <cstdint>
#include <vector>

#include "editor/level_editor_ui.h"

// NOT ORIGINAL - the level editor's per-piece lighting switches (ours; the Inspector's Lighting section for one scenery
// piece that has a node - LevelGeometry / LevelObject). Only what the engine reads:
//   - Light group: the node's LightGroup (0x7E4813A7), read by cfunc_parse_node_array (0x005A34E0) for every
//     LevelGeometry / LevelObject node into its sector's light group (WorldCell +0x0C; Outdoor 0xE3714DC1 when the node
//     has none). The group decides how the time of day colours the piece and which lights touch it: the time-of-day
//     manager's colour groups (load_level's {light_group=outdoor} {light_group=nolevellights reset_light_group=indoor} -
//     tod_manager.cpp apply_color_to_scene: Outdoor pieces take the time of day's colour, Indoor ones are set back to
//     neutral), and the level lights (scene_lights.cpp LevelLamp::find_sectors) skip Indoor and NoLevelLights pieces. A
//     field edit (the `set` record's f.LightGroup=, put in the node again before the edited copy's ParseNodeArray), and
//     put on the sector at once here - the colours follow at the next time-of-day change or reload, the level lights
//     on reload.
//   - No skater shadow: the skater's (and peds') projected shadow is drawn on every mesh in view but those with mesh
//     flag 2 (shadow_base.cpp draw_shadows 0x004DBF50; the scene file's per-mesh flag 0x400 sets it - pc_scene.cpp).
//     There's no node field for it in THUG2, so it is a flag of ours on the node, ed_no_skater_shadow (a field edit like
//     the others; a bake writes it into the node array - read by this project's game only, not by THUG2.exe), and each
//     mesh of the piece gets flag 2 while it's set (put back as it was when it's taken off, the level changes or a
//     network session starts).
// Not offered (checked against our source): THUG2 has no per-piece "cast shadow" (scenery casts none - only objects'
// shadow components, from their own script parameters), no per-piece time-of-day visibility (pieces are shown / hidden
// by scripts: Kill / Create), and an object's model lighting (UseModelLights, the model light update component) comes
// from its create script's component parameters, not from its node.
namespace frontend::level_editor::lighting {

// The groups offered (their names as the node writes them).
struct Group {
    const char* label;
    const char* name;
};
constexpr int kGroups = 3;
const Group& group_at(int index); // Outdoor, Indoor, NoLevelLights

// Once a frame, the editor running or not: the edited pieces' light groups and shadow flags put on their sectors.
void tick();
// The Inspector's section for one scenery piece (`sector`: its checksum - its node's name); field edits go to
// `requests` (level_editor_ui.h's kFieldSet / kFieldRemove).
void inspector_section(uint32_t sector, std::vector<ui::Request>* requests);

} // namespace frontend::level_editor::lighting
