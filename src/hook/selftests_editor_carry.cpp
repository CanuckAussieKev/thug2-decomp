// Self-tests for the level editor's polish (not original): carrying - a placement undone and the step between two
// placements (src/game/front/level_editor_carry.*), the footprint test, the attachment rules and headings turned by
// quarter turns; the Outliner's markers (level_editor_ui.h); the trigger volumes' resize handles and their exit test
// (level_editor_triggers.h); the collision view's face styles (level_editor_view.h: hovered walls, shared collision,
// triggers by kind).

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_carry.h"
#include "game/front/level_editor_triggers.h"
#include "game/front/level_editor_ui.h"
#include "game/front/level_editor_view.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace View = Front::LevelEditor::View;
namespace Carry = Front::LevelEditor::Carry;
namespace Ui = Front::LevelEditor::Ui;
namespace Triggers = Front::LevelEditor::Triggers;
using LE::V3;

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

const V3 kPoints[] = {{0, 0, 0}, {3, 1, -2}, {-50, 7, 12}, {120.5f, -3, 64}};
// (the scales per axis - level_editor_scale.h; stretched ones with odd turns too)
const LE::Placement kPlacements[] = {{0, {0, 0, 0}, {1, 1, 1}},     {1, {16, 0, -8}, {1, 1, 1}},  {2, {-3.5f, 2, 40}, {2, 2, 2}},
                                     {3, {100, -6, 0.25f}, {0.5f, 2.0f, 1.5f}}, {1, {7, 300, -90}, {1.21f, 1.0f, 0.8f}}};

void TestEditorCarrySteps()
{
    bool inverse = true;
    for (const LE::Placement& p : kPlacements)
        for (V3 x : kPoints)
            inverse = inverse && Near(LE::Apply(Carry::Inverse(p), LE::Apply(p, x)), x, 1e-3f);
    Check(inverse, "editor carry: Inverse undoes a placement (moves, quarter turns, scales)");
    bool between = true;
    for (const LE::Placement& from : kPlacements)
        for (const LE::Placement& to : kPlacements) {
            LE::Placement step = Carry::Between(from, to);
            for (V3 x : kPoints)
                between = between && Near(LE::Apply(step, LE::Apply(from, x)), LE::Apply(to, x), 2e-3f);
        }
    Check(between, "editor carry: Between takes what stood at a piece's old placement to its new one");
    // A move only: the step is that move; a turn about a pivot: its quarters, and the pivot stays.
    LE::Placement a = {0, {10, 0, 0}}, b = a;
    LE::MoveBy(&b, {5, 6, 7});
    LE::Placement moved = Carry::Between(a, b);
    Check(moved.quarters == 0 && LE::IsUnitScale(moved.scale) && Near(moved.offset, {5, 6, 7}, 1e-5f),
          "editor carry: a move's step is the move");
    LE::Placement c = a;
    LE::TurnAbout(&c, {100, 0, 100}, 1);
    LE::Placement turned = Carry::Between(a, c);
    Check(turned.quarters == 1 && Near(LE::Apply(turned, {100, 0, 100}), {100, 0, 100}, 1e-4f) &&
              Near(LE::Apply(turned, {100, 0, 110}), {110, 0, 100}, 1e-4f),
          "editor carry: a quarter turn's step turns about the pivot (+z towards +x)");
    Check(Carry::IsNone(Carry::Between(kPlacements[1], kPlacements[1])) &&
              Carry::IsNone(Carry::Between(kPlacements[3], kPlacements[3])),
          "editor carry: no change, no step");
    Check(Carry::IsNone({0, {0, 0, 0}}) && !Carry::IsNone({1, {0, 0, 0}}) && !Carry::IsNone({0, {0, 0.5f, 0}}) &&
              !Carry::IsNone({0, {0, 0, 0}, {1.1f, 1.1f, 1.1f}}) && !Carry::IsNone({0, {0, 0, 0}, {1, 1, 0.9f}}) &&
              Carry::IsNone({0, {0, 5e-5f, 0}, {1.0000001f, 1, 0.9999999f}}) &&
              !Carry::IsNone({0, {0, 0, 0}, {1.00001f, 1, 1}}) && !Carry::IsNone({0, {0, 0, 2e-4f}}),
          "editor carry: IsNone only for no move, turn or scale");
    // Headings: a quarter turn adds 90 degrees (wrapped into (-pi, pi]).
    Check(Near(Carry::TurnedHeading(0.0f, 1), 1.5707963f, 1e-5f) && Near(Carry::TurnedHeading(3.0f, 2), 3.0f + 3.1415927f - 6.2831853f, 1e-4f) &&
              Near(Carry::TurnedHeading(1.0f, 0), 1.0f, 1e-6f) && Near(Carry::TurnedHeading(0.0f, 3), -1.5707963f, 1e-5f),
          "editor carry: headings turned by quarter turns, wrapped");
    // The heading follows the positions: a node facing +z (angle 0) on a piece turned a quarter faces where +z went.
    V3 facing = LE::TurnAboutUp({0, 0, 1}, Carry::TurnedHeading(0.0f, 1));
    Check(Near(facing, LE::QuarterTurn({0, 0, 1}, 1), 1e-5f), "editor carry: a node's heading turns as the piece's points do");
}
SELFTEST(Early, TestEditorCarrySteps);

void TestEditorCarryRules()
{
    // A box 100 x 50 x 200 at (1000, 0, 0), unturned; and the same turned a quarter and scaled 2x.
    LE::OrientedBox box = {{1000, 0, 0}, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {-50, 0, -100}, {50, 50, 100}};
    Check(Carry::OverFootprint(box, {1000, 50, 0}, 10, 400) && Carry::OverFootprint(box, {1059, 50, 109}, 10, 400) &&
              Carry::OverFootprint(box, {1000, 449, 0}, 10, 400) && Carry::OverFootprint(box, {1000, -9, 0}, 10, 400),
          "editor carry: on top, at the grown edge, up to `above` over it, a little below it - over the footprint");
    Check(!Carry::OverFootprint(box, {1061, 50, 0}, 10, 400) && !Carry::OverFootprint(box, {1000, 50, 111}, 10, 400) &&
              !Carry::OverFootprint(box, {1000, 451, 0}, 10, 400) && !Carry::OverFootprint(box, {1000, -11, 0}, 10, 400),
          "editor carry: beside it, past its end, high above it, under it - not over the footprint");
    LE::OrientedBox turned = LE::PlacedBox({-50, 0, -100}, {50, 50, 100}, {1, {0, 0, 0}, {2, 2, 2}});
    // (turned a quarter: its x axis along -z, its z axis along +x; scaled 2x: 200 across x-local, 400 across z-local)
    Check(Carry::OverFootprint(turned, {195, 100, 0}, 10, 400) && Carry::OverFootprint(turned, {0, 100, 95}, 10, 400) &&
              !Carry::OverFootprint(turned, {0, 100, 115}, 10, 400) && Carry::OverFootprint(turned, {0, 500, 0}, 10, 400) &&
              !Carry::OverFootprint(turned, {0, 520, 0}, 10, 400),
          "editor carry: a turned, scaled piece's footprint follows its axes (margins in world units)");
    LE::OrientedBox flat = {{0, 0, 0}, {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {0, 0, 0}, {1, 1, 1}};
    Check(!Carry::OverFootprint(flat, {0, 0, 0}, 10, 400), "editor carry: a box without an axis has no footprint");
    // The attachments.
    std::vector<uint32_t> members = {0x111, 0x222}, clusters = {0x999};
    Check(Carry::Attached(0x222, {}, 0, members, clusters) == Carry::Attachment::kCluster &&
              Carry::Attached(0x999, {}, 0, members, clusters) == Carry::Attachment::kCluster,
          "editor carry: a node whose Cluster= names a member or a member's cluster is attached");
    Check(Carry::Attached(0x555, {0x444, 0x111}, 0, members, clusters) == Carry::Attachment::kLink,
          "editor carry: a node linking to a member is attached");
    Check(Carry::Attached(0, {}, 0x111, members, clusters) == Carry::Attachment::kStanding,
          "editor carry: something whose ground below is a member stands on it");
    Check(Carry::Attached(0x555, {0x444}, 0x333, members, clusters) == Carry::Attachment::kNone &&
              Carry::Attached(0, {0}, 0, members, clusters) == Carry::Attachment::kNone &&
              Carry::Attached(0, {}, 0, {0}, {0}) == Carry::Attachment::kNone,
          "editor carry: another cluster, other links, other ground, or nothing at all - not attached (0 never matches)");
}
SELFTEST(Early, TestEditorCarryRules);

void TestEditorOutlinerMarks()
{
    Check(Ui::MarkLetters(0).empty() && Ui::MarkLetters(Ui::kMarkMoved | Ui::kMarkHidden) == "MD" &&
              Ui::MarkLetters(Ui::kMarkLocked | Ui::kMarkAdded | Ui::kMarkCollisionOff | Ui::kMarkViewHidden) == "+HLC",
          "editor outliner: marks as letters, in their order");
    Check(Ui::MarkShown(0, false) && !Ui::MarkShown(0, true) && Ui::MarkShown(Ui::kMarkLocked, true) &&
              Ui::MarkShown(Ui::kMarkMoved, false) && !Ui::MarkShown(0x80, true),
          "editor outliner: Changed only shows marked rows (an unknown bit isn't a mark)");
    bool distinct = true;
    for (int a = 0; a < Ui::kMarkCount; ++a)
        for (int b = a + 1; b < Ui::kMarkCount; ++b)
            distinct = distinct && Ui::MarkLetter(a) != Ui::MarkLetter(b) && Ui::MarkColour(a) != Ui::MarkColour(b);
    Check(distinct && Ui::MarkLetter(Ui::kMarkCount) == '?' && *Ui::MarkName(0) && !*Ui::MarkName(-1),
          "editor outliner: each mark its own letter and colour, named");
}
SELFTEST(Early, TestEditorOutlinerMarks);

void TestEditorTriggerHandles()
{
    const float kQuarter = 1.5707963f;
    V3 pos = {100, 50, -20}, size = {400, 200, 60};
    // Unturned: the handles on the faces' middles, pointing out.
    Check(Near(Triggers::HandlePoint(pos, 0, size, 0), {-100, 50, -20}, 1e-4f) &&
              Near(Triggers::HandlePoint(pos, 0, size, 1), {300, 50, -20}, 1e-4f) &&
              Near(Triggers::HandlePoint(pos, 0, size, 3), {100, 150, -20}, 1e-4f) &&
              Near(Triggers::HandlePoint(pos, 0, size, 4), {100, 50, -50}, 1e-4f),
          "editor triggers: the handles on the middles of the box's faces");
    // Turned a quarter: its x face along +z (TurnAboutUp), the up handle still up.
    Check(Near(Triggers::HandleDirection(kQuarter, 1), LE::TurnAboutUp({1, 0, 0}, kQuarter), 1e-5f) &&
              Near(Triggers::HandleDirection(kQuarter, 0), LE::TurnAboutUp({-1, 0, 0}, kQuarter), 1e-5f) &&
              Near(Triggers::HandleDirection(kQuarter, 2), {0, -1, 0}, 1e-6f) &&
              Near(Triggers::HandleDirection(kQuarter, 5), LE::TurnAboutUp({0, 0, 1}, kQuarter), 1e-5f),
          "editor triggers: the handles' directions turn with the box's heading");
    // Every handle on the box's surface: its point is on the placed box's face (Inside, at the edge) and a step out is not.
    bool onFaces = true;
    for (float yaw : {0.0f, 0.7f, -2.0f})
        for (int h = 0; h < 6; ++h) {
            LE::OrientedBox box = Triggers::BoxOf(pos, yaw, size);
            V3 p = Triggers::HandlePoint(pos, yaw, size, h);
            V3 d = Triggers::HandleDirection(yaw, h);
            onFaces = onFaces && Triggers::Inside(box, p - d * 0.01f) && !Triggers::Inside(box, p + d * 0.5f);
        }
    Check(onFaces, "editor triggers: each handle sits on its face (turned boxes too)");
    // Dragging a face: the opposite face stays, the size grows by the drag.
    bool opposite = true;
    for (float yaw : {0.0f, 1.1f})
        for (int h = 0; h < 6; ++h) {
            V3 newPos, newSize;
            Triggers::ResizeFace(pos, yaw, size, h, 50.0f, &newPos, &newSize);
            V3 before = Triggers::HandlePoint(pos, yaw, size, h ^ 1), after = Triggers::HandlePoint(newPos, yaw, newSize, h ^ 1);
            V3 moved = Triggers::HandlePoint(newPos, yaw, newSize, h);
            V3 was = Triggers::HandlePoint(pos, yaw, size, h);
            opposite = opposite && Near(before, after, 1e-3f) && Near(moved, was + Triggers::HandleDirection(yaw, h) * 50.0f, 1e-3f);
        }
    Check(opposite, "editor triggers: a face dragged out moves by the drag, the opposite face stays");
    V3 p2, s2;
    Triggers::ResizeFace(pos, 0, size, 4, -1000.0f, &p2, &s2); // (-z face pushed in past the other: at the least size)
    Check(Near(s2, {400, 200, 10}, 1e-4f) && Near(p2, {100, 50, 5}, 1e-4f),
          "editor triggers: a face pushed past the other stops at the least size (10), the opposite face kept");
    Triggers::ResizeFace(pos, 0, size, 1, std::nanf(""), &p2, &s2);
    Check(Near(s2, size, 0.0f) && Near(p2, pos, 0.0f), "editor triggers: a drag that isn't a number changes nothing");
    // Inside / leaving.
    LE::OrientedBox box = Triggers::BoxOf(pos, 0.0f, size);
    Check(Triggers::Inside(box, pos) && Triggers::Inside(box, {299, 149, 9}) && !Triggers::Inside(box, {301, 50, -20}) &&
              !Triggers::Inside(box, {100, -51, -20}),
          "editor triggers: inside a box, and not past a face");
    Check(Triggers::Left(true, true, false) && !Triggers::Left(false, true, false) && !Triggers::Left(true, false, false) &&
              !Triggers::Left(true, true, true) && !Triggers::Left(true, false, true),
          "editor triggers: leaving fires only from inside to outside, never on the first frame seen");
    // The record's exit=1.
    Triggers::Placed placed;
    placed.spec.name = "ed_trigger07";
    placed.spec.kind = Triggers::Kind::Kill;
    placed.spec.target = "TRG_Restart01";
    placed.spec.exit = true;
    placed.pos = {1, 2, 3};
    std::string record = Triggers::Record(placed);
    Triggers::Placed read;
    Check(record.find(" exit=1") != std::string::npos && Triggers::ReadRecord(record, &read) && read.spec.exit &&
              Triggers::SameSpec(read.spec, placed.spec),
          "editor triggers: exit=1 written and read back");
    placed.spec.exit = false;
    Check(Triggers::Record(placed).find("exit") == std::string::npos && !Triggers::SameSpec(read.spec, placed.spec),
          "editor triggers: no exit, no field (older readers unchanged); exit tells specs apart");
    Check(!Triggers::ReadRecord("ed_trigger07 kill pos=1,2,3 exit=2", &read) &&
              !Triggers::ReadRecord("ed_trigger07 kill pos=1,2,3 exit=1 exit=1", &read),
          "editor triggers: a bad or repeated exit= isn't a record");
}
SELFTEST(Early, TestEditorTriggerHandles);

void TestEditorCarryCollisionStyle()
{
    using View::FaceFacts;
    using View::FaceStyle;
    FaceFacts plain;
    plain.flags = 0x01;
    FaceStyle s = View::StyleOf(plain);
    Check(s.fill == View::SurfaceColour(View::Surface::kSkatable) && s.edge == 0,
          "editor collision view: a plain face its surface's colour, no edges");
    FaceFacts wall;
    wall.collisionOnly = true;
    s = View::StyleOf(wall);
    FaceFacts wallOff = wall;
    wallOff.off = true;
    FaceStyle off = View::StyleOf(wallOff);
    Check(s.fill == View::SurfaceColour(View::Surface::kInvisibleWall) && s.edge == 0xE0FF60F0u && !s.dashed &&
              off.fill == View::Faded(s.fill) && off.dashed,
          "editor collision view: an invisible wall magenta with edges; switched off faded and dashed");
    FaceFacts hovered = wall, selected = wall;
    hovered.hovered = true;
    selected.selected = selected.hovered = true;
    FaceStyle h = View::StyleOf(hovered), sel = View::StyleOf(selected);
    Check(h.fill == View::kHoverWall && h.edge == 0xFFFFF060u && h.thickness > 1.0f && sel.fill == 0xC8FFA0FFu &&
              sel.edge == 0xFFFFFFFFu,
          "editor collision view: the hovered wall yellow (Shift), the selected one lighter and white-edged first");
    // A face taken along by a moved piece: classified as before it was turned off (0x10 added), faded, cyan dashes.
    FaceFacts taken;
    taken.flags = 0x11;
    taken.taken = true;
    s = View::StyleOf(taken);
    Check(s.fill == View::Faded(View::SurfaceColour(View::Surface::kSkatable)) && s.edge == View::kTakenEdge && s.dashed,
          "editor collision view: a face taken along drawn as it was, faded, cyan dashes");
    FaceFacts companion;
    companion.flags = 0x01;
    companion.companion = true;
    companion.collisionOnly = true;
    s = View::StyleOf(companion);
    Check(s.fill == View::SurfaceColour(View::Surface::kSkatable) && s.edge == View::kCompanionEdge && !s.dashed,
          "editor collision view: a moved piece's shared collision its surfaces' colours, solid cyan edges");
    FaceFacts trigger;
    trigger.flags = 0x50;
    trigger.triggerColour = 0xFFFF2020u;
    trigger.collisionOnly = true;
    s = View::StyleOf(trigger);
    FaceFacts notTrigger = trigger;
    notTrigger.flags = 0x01;
    Check(s.fill == 0x78FF2020u && s.edge == 0xE0FF60F0u &&
              View::StyleOf(notTrigger).fill == View::SurfaceColour(View::Surface::kInvisibleWall),
          "editor collision view: a level trigger's faces in its kind's colour (its other faces as before)");
}
SELFTEST(Early, TestEditorCarryCollisionStyle);

} // namespace
