// NOT ORIGINAL - the level editor's "adjust last action" (ours; see level_editor_adjust.h).

#include "editor/level_editor_adjust.h"

#include <algorithm>
#include <cmath>

#include "editor/level_editor_arrange.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_ui.h"
#include "platform/run_log.h"

namespace frontend::level_editor::adjust {

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
    auto factor = [](float f) { return std::clamp(f, 0.05f, 20.0f); };
    n.factor = {.x = factor(n.factor.x), .y = factor(n.factor.y), .z = factor(n.factor.z)};
    if (n.uniform)
        n.factor.y = n.factor.z = n.factor.x;
    if (n.quarters)
        n.degrees = 90.0f * std::round(n.degrees / 90.0f);
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
        step.quarters = ((static_cast<int>(std::lround(action.degrees / 90.0f)) % 4) + 4) % 4;
        step.turn = 0.0f;
    } else {
        step.turn = action.degrees * kPi / 180.0f;
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
RamStats g_last;
bool g_applying;
bool g_shown = true;
bool g_pending;
ActStep g_request;

bool same_selection(const edit::Selected& a, const edit::Selected& b) { return a.kind == b.kind && a.ids == b.ids; }

// The action made again from where it started (its step taken back first - Place: moved / turned in place).
void get_apply(const ActStep& wanted)
{
    const ActStep n = normalised(wanted, xform::get_grid());
    if (get_same(n, g_last.action))
        return;
    g_applying = true;
    size_t depth = edit::deepness();
    bool pushed = true;
    switch (n.kind) {
    case Kind::kPlace: {
        const V3 pos = edit::get_pos();
        if (pos.x != n.pos.x || pos.y != n.pos.y || pos.z != n.pos.z)
            edit::place_at(n.pos);
        const float turn = n.degrees - g_last.action.degrees;
        if (std::fabs(turn) > 1e-4f)
            edit::turn_by(turn * kPi / 180.0f);
        edit::get_settle();
        pushed = false;
        break;
    }
    case Kind::kShiftBy:
        edit::take_back();
        edit::get_begin();
        edit::place_at(g_last.start + n.offset);
        edit::get_end();
        break;
    case Kind::kTurn:
        edit::take_back();
        edit::get_begin();
        edit::turn_by(n.degrees * kPi / 180.0f);
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
        edit::choose_one(g_last.source);
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
    g_applying = false;
    if (n.kind != Kind::kPlace && !pushed) {
        g_last.valid = false; // (nothing made: the step under it isn't this action's)
        hud::get_message("Adjust: invalid values");
        return;
    }
    (void)depth;
    g_last.action = n;
    g_last.serial = edit::top_serial();
    g_last.after = edit::get_selection();
}

} // namespace

void get_note(const ActStep& action)
{
    if (g_applying)
        return;
    g_last.valid = action.kind != Kind::kNone;
    g_last.action = action;
    g_last.serial = edit::top_serial();
    g_last.after = edit::get_selection();
    if (action.kind == Kind::kShiftBy)
        g_last.start = edit::get_pos() - action.offset;
    if (action.kind == Kind::kPlace) {
        g_last.action.pos = edit::get_pos();
        g_last.action.degrees = 0.0f;
    }
    g_pending = false;
}

void note_duplicate(const edit::Selected& source, const ActStep& action)
{
    if (g_applying)
        return;
    g_last.source = source;
    get_note(action);
}

bool applying() { return g_applying; }

void get_request(const ActStep& action, bool now)
{
    g_request = action;
    g_pending = true;
    (void)now;
}

const ActStep& final_one() { return g_last.action; }

bool get_adjustable()
{
    if (!g_last.valid || edit::top_serial() != g_last.serial)
        return false;
    // (a placed thing: still the one selected)
    return g_last.action.kind != Kind::kPlace || same_selection(edit::get_selection(), g_last.after);
}

bool& get_shown() { return g_shown; }

void tick()
{
    // (F4 - its registry entry, level_editor_arrange.cpp - switches Shown)
    if (!g_pending || edit::get_dragging())
        return;
    g_pending = false;
    if (!get_adjustable()) {
        hud::get_message("Adjust: only the last operation can be adjusted");
        return;
    }
    get_apply(g_request);
}

void get_forget()
{
    g_last = RamStats{};
    g_pending = false;
    g_applying = false;
}

} // namespace frontend::level_editor::adjust
