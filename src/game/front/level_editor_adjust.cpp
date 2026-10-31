// NOT ORIGINAL - the level editor's "adjust last action" (ours; see level_editor_adjust.h).

#include "game/front/level_editor_adjust.h"

#include <algorithm>
#include <cmath>

#include "game/front/level_editor_arrange.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_ui.h"
#include "game/sys/log.h"

namespace MenuSystem::LevelEditor::Adjust {

namespace {
constexpr float kPi = 3.14159265f;
}

// --- the pure parts ---------------------------------------------------------------------------------------------

const char* Title(Kind kind)
{
    switch (kind) {
    case Kind::ShiftBy: return "Move";
    case Kind::Turn: return "Turn";
    case Kind::StretchBy: return "Scale";
    case Kind::Duplicate: return "Duplicate";
    case Kind::Justify: return "Align";
    case Kind::Distribute: return "Distribute";
    case Kind::Place: return "Place";
    case Kind::None: break;
    }
    return "";
}

ActStep Normalised(const ActStep& action, float grid)
{
    ActStep n = action;
    n.count = std::clamp(n.count, 1, kMaxCopies);
    n.axis = std::clamp(n.axis, 0, 2);
    n.edge = std::clamp(n.edge, 0, 2);
    n.reference = std::clamp(n.reference, 0, 1);
    auto factor = [](float f) { return std::clamp(f, 0.05f, 20.0f); };
    n.factor = {factor(n.factor.x), factor(n.factor.y), factor(n.factor.z)};
    if (n.uniform)
        n.factor.y = n.factor.z = n.factor.x;
    if (n.quarters)
        n.degrees = 90.0f * std::round(n.degrees / 90.0f);
    if (n.grid && grid > 0.0f)
        n.offset = {SnapToGrid(n.offset.x, grid), SnapToGrid(n.offset.y, grid), SnapToGrid(n.offset.z, grid)};
    return n;
}

bool Same(const ActStep& a, const ActStep& b)
{
    auto same3 = [](V3 p, V3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    return a.kind == b.kind && same3(a.offset, b.offset) && a.degrees == b.degrees && a.quarters == b.quarters &&
           same3(a.factor, b.factor) && a.uniform == b.uniform && a.count == b.count && a.grid == b.grid &&
           a.axis == b.axis && a.edge == b.edge && a.reference == b.reference && a.local == b.local &&
           a.gaps == b.gaps && same3(a.pos, b.pos);
}

Xform::RepeatStep StepOf(const ActStep& action)
{
    Xform::RepeatStep step;
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
    Edit::Selected source, after; // (Duplicate: what it copied; the selection the action left)
    V3 start = {};                // (Move: where it was)
    uint64_t serial = 0;          // (the undo stack's top when it was made)
};
RamStats g_last;
bool g_applying;
bool g_shown = true;
bool g_pending;
ActStep g_request;

bool SameSelection(const Edit::Selected& a, const Edit::Selected& b) { return a.kind == b.kind && a.ids == b.ids; }

// The action made again from where it started (its step taken back first - Place: moved / turned in place).
void Apply(const ActStep& wanted)
{
    ActStep n = Normalised(wanted, Xform::Grid());
    if (Same(n, g_last.action))
        return;
    g_applying = true;
    size_t depth = Edit::Deepness();
    bool pushed = true;
    switch (n.kind) {
    case Kind::Place: {
        V3 pos = Edit::Pos();
        if (pos.x != n.pos.x || pos.y != n.pos.y || pos.z != n.pos.z)
            Edit::PlaceAt(n.pos);
        float turn = n.degrees - g_last.action.degrees;
        if (std::fabs(turn) > 1e-4f)
            Edit::TurnBy(turn * kPi / 180.0f);
        Edit::Settle();
        pushed = false;
        break;
    }
    case Kind::ShiftBy:
        Edit::TakeBack();
        Edit::Begin();
        Edit::PlaceAt(g_last.start + n.offset);
        Edit::End();
        break;
    case Kind::Turn:
        Edit::TakeBack();
        Edit::Begin();
        Edit::TurnBy(n.degrees * kPi / 180.0f);
        Edit::End();
        break;
    case Kind::StretchBy:
        Edit::TakeBack();
        if (!Edit::ScaleBy(n.factor, n.uniform)) {
            Edit::Begin(); // (at a limit, or back to 1: a step that changes nothing keeps the panel's place)
            Edit::End();
        }
        break;
    case Kind::Duplicate: {
        Edit::TakeBack();
        Edit::ChooseOne(g_last.source);
        depth = Edit::Deepness();
        Xform::RepeatStep step = StepOf(n);
        for (int i = 0; i < n.count; ++i) {
            if (Edit::Duplicate(&step).empty())
                break;
        }
        Edit::Fold(depth);
        pushed = Edit::Deepness() > depth;
        break;
    }
    case Kind::Justify:
    case Kind::Distribute: {
        Edit::TakeBack();
        bool done = n.kind == Kind::Justify
                        ? Arrange::Justify(n.axis, static_cast<Arrange::Edge>(n.edge), static_cast<Arrange::Reference>(n.reference),
                                         n.local)
                        : Arrange::Distribute(n.axis, n.gaps, n.local);
        if (!done) {
            Edit::Begin();
            Edit::End();
        }
        break;
    }
    case Kind::None:
        break;
    }
    g_applying = false;
    if (n.kind != Kind::Place && !pushed) {
        g_last.valid = false; // (nothing made: the step under it isn't this action's)
        Hud::Message("Adjust: invalid values");
        return;
    }
    (void)depth;
    g_last.action = n;
    g_last.serial = Edit::TopSerial();
    g_last.after = Edit::Selection();
}

} // namespace

void Note(const ActStep& action)
{
    if (g_applying)
        return;
    g_last.valid = action.kind != Kind::None;
    g_last.action = action;
    g_last.serial = Edit::TopSerial();
    g_last.after = Edit::Selection();
    if (action.kind == Kind::ShiftBy)
        g_last.start = Edit::Pos() - action.offset;
    if (action.kind == Kind::Place) {
        g_last.action.pos = Edit::Pos();
        g_last.action.degrees = 0.0f;
    }
    g_pending = false;
}

void NoteDuplicate(const Edit::Selected& source, const ActStep& action)
{
    if (g_applying)
        return;
    g_last.source = source;
    Note(action);
}

bool Applying() { return g_applying; }

void Request(const ActStep& action, bool now)
{
    g_request = action;
    g_pending = true;
    (void)now;
}

const ActStep& FinalOne() { return g_last.action; }

bool Adjustable()
{
    if (!g_last.valid || Edit::TopSerial() != g_last.serial)
        return false;
    // (a placed thing: still the one selected)
    return g_last.action.kind != Kind::Place || SameSelection(Edit::Selection(), g_last.after);
}

bool& Shown() { return g_shown; }

void Tick()
{
    // (F4 - its registry entry, level_editor_arrange.cpp - switches Shown)
    if (!g_pending || Edit::Dragging())
        return;
    g_pending = false;
    if (!Adjustable()) {
        Hud::Message("Adjust: only the last operation can be adjusted");
        return;
    }
    Apply(g_request);
}

void Forget()
{
    g_last = RamStats{};
    g_pending = false;
    g_applying = false;
}

} // namespace MenuSystem::LevelEditor::Adjust
