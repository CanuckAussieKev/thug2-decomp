// NOT ORIGINAL - the level editor's pie menus' slices (ours). See level_editor_ux8_pie.h.

#include "editor/level_editor_ux8_pie.h"

#include <algorithm>
#include <vector>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_transform.h"

namespace frontend::level_editor::ux8::pie {

namespace {

// A registry action as a slice (its own name unless `label`).
Item act_step(int index, const char* label, bool edits)
{
    Item item;
    const actions::ActStep* action_step = actions::get_pull(index);
    item.label = label ? label : action_step ? action_step->name : "?";
    item.keys = hints::leading_key_name(actions::get_keys_text(index));
    item.run = [index] { actions::get_run(index); };
    item.enabled = [index] { return actions::is_switched_on(index); };
    item.checked = [index] { return actions::get_checked(index); };
    item.edits = edits;
    return item;
}
Item action_by_id(const char* id, const char* label, bool edits)
{
    const int index = actions::get_find(id);
    if (index < 0) {
        Item none;
        none.label = label ? label : id;
        none.run = [] {};
        none.enabled = [] { return false; };
        none.checked = [] { return -1; };
        return none;
    }
    return act_step(index, label, edits);
}

Item get_tool(xform::Mode mode, int action)
{
    Item item;
    item.label = std::string("Tool: ") + xform::mode_name(mode);
    item.keys = hints::leading_key_name(actions::get_keys_text(action));
    // (set directly: the keys pick a tool only with the right button up and wasd_flies=0)
    item.run = [mode] { xform::at_hand().mode = mode; };
    item.enabled = [] { return true; };
    item.checked = [mode] { return xform::at_hand().mode == mode ? 1 : 0; };
    return item;
}

Item view_mode(int mode)
{
    Item item;
    item.label = std::string("View: ") + overlays::view_mode_name(mode);
    item.keys = hints::leading_key_name(actions::get_keys_text(actions::kViewModeLit + mode));
    item.run = [mode] { overlays::change_view_mode(mode); };
    item.enabled = [] { return true; };
    item.checked = [mode] { return overlays::view_mode() == mode ? 1 : 0; };
    return item;
}

} // namespace

std::vector<Item> context_items(bool have_selection)
{
    using namespace actions;
    if (have_selection) {
        // (clockwise from the top)
        return {get_tool(xform::Mode::kShiftBy, kToolMove),
                get_tool(xform::Mode::kSpinBy, kToolRotate),
                action_by_id("snap25", "Snap", false),
                act_step(kDrop, "Drop to the ground", true),
                get_tool(xform::Mode::kScale, kToolScale),
                act_step(kDelete, "Delete / hide", true),
                act_step(kDuplicate, nullptr, true),
                act_step(kFrameSelection, nullptr, false),
                action_by_id("linked_duplicate", nullptr, true),
                action_by_id("arrange_window", nullptr, false)};
    }
    return {act_step(kBrowser, "Add (browser)", false),
            act_step(kPlace, "Place a node", false),
            act_step(kBoxSelect, nullptr, false),
            act_step(kRuler, nullptr, false),
            action_by_id("walk_mode", nullptr, false),
            act_step(kSimulate, nullptr, false),
            act_step(kPlay, nullptr, false),
            act_step(kTrailWindow, nullptr, false)};
}

std::vector<Item> view_items()
{
    using namespace actions;
    std::vector<Item> items;
    for (int mode = 0; mode < std::min(overlays::kViewModes, 4); ++mode) {
        items.push_back(view_mode(mode));
    }
    items.push_back(act_step(kCollision, nullptr, false));
    items.push_back(act_step(kTriggers, nullptr, false));
    items.push_back(act_step(kJumpArcs, nullptr, false));
    Item top = act_step(kViewTop, nullptr, false);
    if (view_cam::ortho()) {
        top = act_step(kViewOrtho, "Perspective", false);
    }
    items.push_back(top);
    return items;
}

} // namespace frontend::level_editor::ux8::pie
