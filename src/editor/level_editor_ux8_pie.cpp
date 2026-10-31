// NOT ORIGINAL - the level editor's pie menus' slices (ours). See level_editor_ux8_pie.h.

#include "editor/level_editor_ux8_pie.h"

#include "editor/level_editor_actions.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_transform.h"

namespace MenuSystem::LevelEditor::Ux8::Pie {

namespace {

// A registry action as a slice (its own name unless `label`).
Item ActStep(int index, const char* label, bool edits)
{
    Item item;
    const Actions::ActStep* a = Actions::Pull(index);
    item.label = label ? label : a ? a->name : "?";
    item.keys = Hints::LeadingKeyName(Actions::KeysText(index));
    item.run = [index] { Actions::Run(index); };
    item.enabled = [index] { return Actions::IsSwitchedOn(index); };
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
    return ActStep(index, label, edits);
}

Item Tool(Xform::Mode mode, int action)
{
    Item item;
    item.label = std::string("Tool: ") + Xform::ModeName(mode);
    item.keys = Hints::LeadingKeyName(Actions::KeysText(action));
    // (set directly: the keys pick a tool only with the right button up and wasd_flies=0)
    item.run = [mode] { Xform::AtHand().mode = mode; };
    item.enabled = [] { return true; };
    item.checked = [mode] { return Xform::AtHand().mode == mode ? 1 : 0; };
    return item;
}

Item ViewMode(int mode)
{
    Item item;
    item.label = std::string("View: ") + Overlays::ViewModeName(mode);
    item.keys = Hints::LeadingKeyName(Actions::KeysText(Actions::kViewModeLit + mode));
    item.run = [mode] { Overlays::ChangeViewMode(mode); };
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
        return {Tool(Xform::Mode::ShiftBy, kToolMove),
                Tool(Xform::Mode::SpinBy, kToolRotate),
                ActionById("snap25", "Snap", false),
                ActStep(kDrop, "Drop to the ground", true),
                Tool(Xform::Mode::StretchBy, kToolScale),
                ActStep(kDelete, "Delete / hide", true),
                ActStep(kDuplicate, nullptr, true),
                ActStep(kFrameSelection, nullptr, false),
                ActionById("linked_duplicate", nullptr, true),
                ActionById("arrange_window", nullptr, false)};
    }
    return {ActStep(kBrowser, "Add (browser)", false),
            ActStep(kPlace, "Place a node", false),
            ActStep(kBoxSelect, nullptr, false),
            ActStep(kRuler, nullptr, false),
            ActionById("walk_mode", nullptr, false),
            ActStep(kSimulate, nullptr, false),
            ActStep(kPlay, nullptr, false),
            ActStep(kTrailWindow, nullptr, false)};
}

std::vector<Item> ViewItems()
{
    using namespace Actions;
    std::vector<Item> items;
    for (int mode = 0; mode < Overlays::kViewModes && mode < 4; ++mode)
        items.push_back(ViewMode(mode));
    items.push_back(ActStep(kCollision, nullptr, false));
    items.push_back(ActStep(kTriggers, nullptr, false));
    items.push_back(ActStep(kJumpArcs, nullptr, false));
    Item top = ActStep(kViewTop, nullptr, false);
    if (ViewCam::Ortho()) {
        top = ActStep(kViewOrtho, "Perspective", false);
    }
    items.push_back(top);
    return items;
}

} // namespace MenuSystem::LevelEditor::Ux8::Pie
