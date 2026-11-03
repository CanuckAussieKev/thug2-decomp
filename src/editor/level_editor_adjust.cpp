// NOT ORIGINAL - the level editor's "adjust last action" (ours; see level_editor_adjust.h).

#include "editor/level_editor_adjust.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "editor/level_editor.h"
#include "editor/level_editor_arrange.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_transform.h"
#include "engine/core/no_destroy.h"
namespace frontend::level_editor::adjust {

namespace {

constexpr float kFactorMin = 0.05f; // the smallest scale factor a step applies
constexpr float kFactorMax = 20.0f; // the largest scale factor a step applies
constexpr float kTurnEpsilon = 1e-4f; // a turn smaller than this (radians) is none

} // namespace

namespace {

constexpr float kDegreesPerHalfTurn = 180.0f; // degrees in half a turn
constexpr float kQuarterTurnDegrees = 90.0f; // degrees in a quarter turn

} // namespace

namespace {
constexpr float kPi = 3.14159265f;
}

// --- the pure parts ---------------------------------------------------------------------------------------------

const char* get_title(Kind kind)
{
    switch (kind) {
    case Kind::kShiftBy:
        return "Move";
    case Kind::kTurn:
        return "Turn";
    case Kind::kScale:
        return "Scale";
    case Kind::kDuplicate:
        return "Duplicate";
    case Kind::kJustify:
        return "Align";
    case Kind::kDistribute:
        return "Distribute";
    case Kind::kPlace:
        return "Place";
    case Kind::kNone:
        break;
    }
    return "";
}

ActStep normalised(const ActStep& action, float grid)
{
    ActStep n = action;
    n.count = std::clamp(n.count, 1, kMaxCopies);
    n.axis = std::clamp(n.axis, 0, 2);
    n.edge = std::clamp(n.edge, 0, 2);
    n.reference = std::clamp(n.reference, 0, 1);
    auto factor = [](float f) { return std::clamp(f, kFactorMin, kFactorMax); };
    n.factor = {.x = factor(n.factor.x), .y = factor(n.factor.y), .z = factor(n.factor.z)};
    if (n.uniform)
        n.factor.y = n.factor.z = n.factor.x;
    if (n.quarters)
        n.degrees = kQuarterTurnDegrees * std::round(n.degrees / kQuarterTurnDegrees);
    if (n.grid && grid > 0.0f)
        n.offset = {.x = snap_to_grid(n.offset.x, grid), .y = snap_to_grid(n.offset.y, grid), .z = snap_to_grid(n.offset.z, grid)};
    return n;
}

bool get_same(const ActStep& a, const ActStep& b)
{
    auto same3 = [](V3 p, V3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    return a.kind == b.kind && same3(a.offset, b.offset) && a.degrees == b.degrees && a.quarters == b.quarters &&
           same3(a.factor, b.factor) && a.uniform == b.uniform && a.count == b.count && a.grid == b.grid &&
           a.axis == b.axis && a.edge == b.edge && a.reference == b.reference && a.local == b.local &&
           a.gaps == b.gaps && same3(a.pos, b.pos);
}

xform::RepeatStep step_of(const ActStep& action)
{
    xform::RepeatStep step;
    step.offset = action.offset;
    if (action.quarters) {
        step.quarters = ((static_cast<int>(std::lround(action.degrees / kQuarterTurnDegrees)) % 4) + 4) % 4;
        step.turn = 0.0f;
    } else {
        step.turn = action.degrees * kPi / kDegreesPerHalfTurn;
    }
    return step;
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

struct RamStats {
    bool valid = false;
    ActStep action;
    edit::Selected source, after; // (Duplicate: what it copied; the selection the action left)
    V3 start = {};                // (Move: where it was)
    uint64_t serial = 0;          // (the undo stack's top when it was made)
};
// NOT ORIGINAL grouping - level_editor_adjust.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_adjust_state()).
struct LevelEditorAdjustState {
    RamStats last_{};
    bool applying_{};
    bool shown_ = true;
    bool pending_{};
    ActStep request_{};
};

LevelEditorAdjustState& level_editor_adjust_state()
{
    static core::NoDestroy<LevelEditorAdjustState> state;
    return state.get();
}

bool same_selection(const edit::Selected& a, const edit::Selected& b) { return a.kind == b.kind && a.ids == b.ids; }

// The action made again from where it started (its step taken back first - Place: moved / turned in place).
void get_apply(const ActStep& wanted)
{
    const ActStep n = normalised(wanted, xform::get_grid());
    if (get_same(n, level_editor_adjust_state().last_.action))
        return;
    level_editor_adjust_state().applying_ = true;
    size_t depth = edit::deepness();
    bool pushed = true;
    switch (n.kind) {
    case Kind::kPlace: {
        const V3 pos = edit::get_pos();
        if (pos.x != n.pos.x || pos.y != n.pos.y || pos.z != n.pos.z)
            edit::place_at(n.pos);
        const float turn = n.degrees - level_editor_adjust_state().last_.action.degrees;
        if (std::fabs(turn) > kTurnEpsilon)
            edit::turn_by(turn * kPi / kDegreesPerHalfTurn);
        edit::get_settle();
        pushed = false;
        break;
    }
    case Kind::kShiftBy:
        edit::take_back();
        edit::get_begin();
        edit::place_at(level_editor_adjust_state().last_.start + n.offset);
        edit::get_end();
        break;
    case Kind::kTurn:
        edit::take_back();
        edit::get_begin();
        edit::turn_by(n.degrees * kPi / kDegreesPerHalfTurn);
        edit::get_end();
        break;
    case Kind::kScale:
        edit::take_back();
        if (!edit::scale_by(n.factor, n.uniform)) {
            edit::get_begin(); // (at a limit, or back to 1: a step that changes nothing keeps the panel's place)
            edit::get_end();
        }
        break;
    case Kind::kDuplicate: {
        edit::take_back();
        edit::choose_one(level_editor_adjust_state().last_.source);
        depth = edit::deepness();
        xform::RepeatStep step = step_of(n);
        for (int i = 0; i < n.count; ++i) {
            if (edit::get_duplicate(&step).empty())
                break;
        }
        edit::fold(depth);
        pushed = edit::deepness() > depth;
        break;
    }
    case Kind::kJustify:
    case Kind::kDistribute: {
        edit::take_back();
        const bool done = n.kind == Kind::kJustify
                              ? arrange::justify(n.axis, static_cast<arrange::Edge>(n.edge),
                                                 static_cast<arrange::Reference>(n.reference), n.local)
                              : arrange::distribute(n.axis, n.gaps, n.local);
        if (!done) {
            edit::get_begin();
            edit::get_end();
        }
        break;
    }
    case Kind::kNone:
        break;
    }
    level_editor_adjust_state().applying_ = false;
    if (n.kind != Kind::kPlace && !pushed) {
        level_editor_adjust_state().last_.valid = false; // (nothing made: the step under it isn't this action's)
        hud::get_message("Adjust: invalid values");
        return;
    }
    (void)depth;
    level_editor_adjust_state().last_.action = n;
    level_editor_adjust_state().last_.serial = edit::top_serial();
    level_editor_adjust_state().last_.after = edit::get_selection();
}

} // namespace

void get_note(const ActStep& action)
{
    if (level_editor_adjust_state().applying_)
        return;
    level_editor_adjust_state().last_.valid = action.kind != Kind::kNone;
    level_editor_adjust_state().last_.action = action;
    level_editor_adjust_state().last_.serial = edit::top_serial();
    level_editor_adjust_state().last_.after = edit::get_selection();
    if (action.kind == Kind::kShiftBy)
        level_editor_adjust_state().last_.start = edit::get_pos() - action.offset;
    if (action.kind == Kind::kPlace) {
        level_editor_adjust_state().last_.action.pos = edit::get_pos();
        level_editor_adjust_state().last_.action.degrees = 0.0f;
    }
    level_editor_adjust_state().pending_ = false;
}

void note_duplicate(const edit::Selected& source, const ActStep& action)
{
    if (level_editor_adjust_state().applying_)
        return;
    level_editor_adjust_state().last_.source = source;
    get_note(action);
}

bool applying() { return level_editor_adjust_state().applying_; }

void get_request(const ActStep& action, bool now)
{
    level_editor_adjust_state().request_ = action;
    level_editor_adjust_state().pending_ = true;
    (void)now;
}

const ActStep& final_one() { return level_editor_adjust_state().last_.action; }

bool get_adjustable()
{
    if (!level_editor_adjust_state().last_.valid || edit::top_serial() != level_editor_adjust_state().last_.serial)
        return false;
    // (a placed thing: still the one selected)
    return level_editor_adjust_state().last_.action.kind != Kind::kPlace || same_selection(edit::get_selection(), level_editor_adjust_state().last_.after);
}

bool& get_shown() { return level_editor_adjust_state().shown_; }

void tick()
{
    // (F4 - its registry entry, level_editor_arrange.cpp - switches Shown)
    if (!level_editor_adjust_state().pending_ || edit::get_dragging())
        return;
    level_editor_adjust_state().pending_ = false;
    if (!get_adjustable()) {
        hud::get_message("Adjust: only the last operation can be adjusted");
        return;
    }
    get_apply(level_editor_adjust_state().request_);
}

void get_forget()
{
    level_editor_adjust_state().last_ = RamStats{};
    level_editor_adjust_state().pending_ = false;
    level_editor_adjust_state().applying_ = false;
}

} // namespace frontend::level_editor::adjust
