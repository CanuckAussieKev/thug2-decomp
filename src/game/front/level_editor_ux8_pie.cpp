// NOT ORIGINAL - the level editor's pie menus' slices (ours). See level_editor_ux8_pie.h.

#include "game/front/level_editor_ux8_pie.h"

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_camera.h"
#include "game/front/level_editor_hints.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_transform.h"

namespace Front::LevelEditor::Ux8::Pie {

namespace {

// A registry action as a slice (its own name unless `label`).
Item Action(int index, const char* label, bool edits)
{
    Item item;
    const Actions::Action* a = Actions::Get(index);
    item.label = label ? label : a ? a->name : "?";
    item.keys = Hints::FirstKey(Actions::KeysText(index));
    item.run = [index] { Actions::Run(index); };
    item.enabled = [index] { return Actions::Enabled(index); };
    item.checked = [index] { return Actions::Checked(index); };
    item.edits = edits;
    return item;
}
Item ActionById(const char* id, const char* label, bool edits)
{
    int index = Actions::Find(id);
    if (index < 0) {
        Item none;
        none.label = label ? label : id;
        none.run = [] {};
        none.enabled = [] { return false; };
        none.checked = [] { return -1; };
        return none;
    }
    return Action(index, label, edits);
}

Item Tool(Transform::Mode mode, int action)
{
    Item item;
    item.label = std::string("Tool: ") + Transform::ModeName(mode);
    item.keys = Hints::FirstKey(Actions::KeysText(action));
    // (set directly: the keys pick a tool only with the right button up and wasd_flies=0)
    item.run = [mode] { Transform::Current().mode = mode; };
    item.enabled = [] { return true; };
    item.checked = [mode] { return Transform::Current().mode == mode ? 1 : 0; };
    return item;
}

Item ViewMode(int mode)
{
    Item item;
    item.label = std::string("View: ") + Overlays::ViewModeName(mode);
    item.keys = Hints::FirstKey(Actions::KeysText(Actions::kViewModeLit + mode));
    item.run = [mode] { Overlays::SetViewMode(mode); };
    item.enabled = [] { return true; };
    item.checked = [mode] { return Overlays::ViewMode() == mode ? 1 : 0; };
    return item;
}

} // namespace

std::vector<Item> ContextItems(bool haveSelection)
{
    using namespace Actions;
    if (haveSelection) {
        // (clockwise from the top)
        return {Tool(Transform::Mode::Move, kToolMove),
                Tool(Transform::Mode::Rotate, kToolRotate),
                ActionById("snap25", "Snap", false),
                Action(kDrop, "Drop to the ground", true),
                Tool(Transform::Mode::Scale, kToolScale),
                Action(kDelete, "Delete / hide", true),
                Action(kDuplicate, nullptr, true),
                Action(kFrameSelection, nullptr, false),
                ActionById("linked_duplicate", nullptr, true),
                ActionById("arrange_window", nullptr, false)};
    }
    return {Action(kBrowser, "Add (browser)", false),
            Action(kPlace, "Place a node", false),
            Action(kBoxSelect, nullptr, false),
            Action(kRuler, nullptr, false),
            ActionById("walk_mode", nullptr, false),
            Action(kSimulate, nullptr, false),
            Action(kPlay, nullptr, false),
            Action(kTrailWindow, nullptr, false)};
}

std::vector<Item> ViewItems()
{
    using namespace Actions;
    std::vector<Item> items;
    for (int mode = 0; mode < Overlays::kViewModes && mode < 4; ++mode)
        items.push_back(ViewMode(mode));
    items.push_back(Action(kCollision, nullptr, false));
    items.push_back(Action(kTriggers, nullptr, false));
    items.push_back(Action(kJumpArcs, nullptr, false));
    Item top = Action(kViewTop, nullptr, false);
    if (Camera::Ortho()) {
        top = Action(kViewOrtho, "Perspective", false);
    }
    items.push_back(top);
    return items;
}

} // namespace Front::LevelEditor::Ux8::Pie
