#pragma once

#include <string>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's UX9 tools (ours; see level_editor.h): what level_editor.cpp / level_editor_ui.cpp
// call for them, in one place:
//   - the Gap wizard (Alt+J - level_editor_gap_wizard.h): two clicks and a form make a gap's two trigger volumes;
//   - the Skateability overlay (Alt+K, the Overlays menu - level_editor_skate_view.h);
//   - the Ground tool (Alt+U - level_editor_ground.h): two clicks mark a rectangle on the ground (snapped to its grid),
//     a form raises it (a platform) or lowers it (a pit) with kicker / quarter-pipe ramps on chosen sides - a generated
//     piece; a click on a piece with the tool armed edits it (Apply / Remove). Pieces are saved as `ground` records (the
//     layout's extension records) and baked by tools/pipeline/game/apply_layout.ts.
// The keys are the action registry's (level_editor_actions.h: gap_wizard, skateability, ground_tool - rebindable).
namespace Front::LevelEditor::Ux9 {

// The actions and the layout records registered (once, from LevelEditor::Init).
void Init();
// The editor starting / stopping.
void Start();
void Stop();
// Once a frame while the editor runs (after the actions' frame): the keys, the forms' requests.
void Update();
// A tool armed or a form open: Escape goes to Cancel first.
bool Busy();
bool Cancel();
// For the status bar's hints (level_editor_hints.h): 1 the gap wizard picking its ends, 2 the ground tool marking its
// rectangle, 0 neither (a form open counts as neither).
int PickingTool();
// The mouse ray each frame (`x`, `y`: the cursor in back buffer pixels), and a left click (true: taken by a tool).
void Hover(V3 origin, V3 unitDir, float x, float y, bool haveRay);
bool Click(V3 origin, V3 unitDir, float x, float y);
// Over the view (the tools' previews, the skateability colours) and the windows (forms, the legend).
void AddOverlay(const Overlays::Painter& painter);
void Windows(float viewX, float viewY, float viewWidth, float viewHeight, float scale);
// For the screenshot sidecar: a line ("" when nothing to say).
std::string Describe();

} // namespace Front::LevelEditor::Ux9
