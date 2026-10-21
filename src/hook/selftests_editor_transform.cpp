// Self-tests for the level editor's transform tools (src/game/front/level_editor_transform.*, level_editor_snap.*: not
// original - ours, docs/pending/editor-ux2.md): the gizmo's axes and handles, the angle / grid / scale snapping, typed
// values, the repeat of a duplicate, and the snapping's pure parts.

#include <cmath>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_snap.h"
#include "game/front/level_editor_transform.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace T = Front::LevelEditor::Transform;
namespace Snap = Front::LevelEditor::Snap;
using LE::V3;

constexpr float kPi = 3.14159265f;

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool Near(V3 a, V3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

void TestTransformAxes()
{
    V3 axes[3];
    T::HeadingAxes(0.0f, axes);
    Check(Near(axes[0], {1, 0, 0}) && Near(axes[1], {0, 1, 0}) && Near(axes[2], {0, 0, 1}),
          "transform: heading 0's axes are the world's");
    const V3 world[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    bool same = true;
    for (float yaw : {0.3f, 1.5707963f, -2.0f, 3.0f}) {
        T::HeadingAxes(yaw, axes);
        for (int i = 0; i < 3; ++i)
            same = same && Near(axes[i], LE::TurnAboutUp(world[i], yaw));
        same = same && Near(T::HeadingOf(axes[2]), T::Wrap(yaw));
    }
    Check(same, "transform: a heading's axes are the world's turned as TurnAboutUp turns, and HeadingOf reads it back");
    T::HeadingAxes(kPi / 2.0f, axes);
    Check(Near(axes[0], {0, 0, -1}) && Near(axes[2], {1, 0, 0}), "transform: heading 90: x along -z, z along +x");
    V3 rows[3] = {{2, 0, 0}, {0, 0, 0}, {0, 0, 3}}, unit[3];
    T::UnitAxes(rows, unit);
    Check(Near(unit[0], {1, 0, 0}) && Near(unit[1], {0, 1, 0}) && Near(unit[2], {0, 0, 1}),
          "transform: rows made unit (a zero row: the world's)");
    Check(Near(T::AngleAbout({10, 0, 10}, {20, 5, 10}), kPi / 2.0f) && Near(T::AngleAbout({0, 0, 0}, {0, 0, 5}), 0.0f),
          "transform: the angle about a pivot (+z 0, +x 90 degrees)");
}
SELFTEST(Early, TestTransformAxes);

void TestTransformSnapping()
{
    Check(Near(T::Wrap(3.5f), 3.5f - 2.0f * kPi) && Near(T::Wrap(-3.5f), -3.5f + 2.0f * kPi) && Near(T::Wrap(1.0f), 1.0f) &&
              Near(T::Wrap(kPi), kPi),
          "transform: angles wrapped into (-pi, pi]");
    float d = kPi / 180.0f;
    Check(Near(T::SnapAngle(17.0f * d, 15.0f), 15.0f * d) && Near(T::SnapAngle(23.0f * d, 15.0f), 30.0f * d) &&
              Near(T::SnapAngle(-8.0f * d, 15.0f), -15.0f * d) && Near(T::SnapAngle(17.0f * d, 0.0f), 17.0f * d) &&
              Near(T::SnapAngle(30.0f * d, 22.5f), 22.5f * d),
          "transform: the rotation snap's steps (0: free)");
    Check(T::Quarters(100.0f * d) == 1 && T::Quarters(-100.0f * d) == -1 && T::Quarters(190.0f * d) == 2 &&
              T::Quarters(40.0f * d) == 0 && T::Quarters(270.0f * d) == 3,
          "transform: the nearest quarter turns (scenery's rotate drag)");
    Check(Near(T::SnapRelative(37.0f, 25.0f), 25.0f) && Near(T::SnapRelative(38.0f, 25.0f), 50.0f) &&
              Near(T::SnapRelative(12.0f, 25.0f), 0.0f) && Near(T::SnapRelative(-13.0f, 25.0f), -25.0f) &&
              Near(T::SnapRelative(7.3f, 5.0f), 5.0f) && Near(T::SnapRelative(7.3f, 0.0f), 7.3f),
          "transform: relative grid snapping - whole steps from where the drag started");
    Check(Near(T::FactorAlong(100.0f, 200.0f), 2.0f) && Near(T::FactorAlong(100.0f, -50.0f), 0.05f) &&
              Near(T::FactorAlong(0.0f, 50.0f), 1.0f) && Near(T::FactorAlong(10.0f, 1000.0f), 20.0f),
          "transform: a scale handle's factor (distance over distance, 0.05..20)");
    Check(Near(T::FactorFromPixels(2.0f, 100.0f), 1.0f) && Near(T::FactorFromPixels(50.0f, 100.0f), 2.0f) &&
              Near(T::FactorFromPixels(50.0f, 25.0f), 0.5f),
          "transform: the centre handle's factor from pixels (grabbed on the pivot: 1)");
    Check(Near(T::SnapFactor(1.26f), 1.3f) && Near(T::SnapFactor(0.02f), 0.1f) && Near(T::SnapFactor(2.04f), 2.0f),
          "transform: scale snapping to tenths (at least 0.1)");
    Check(T::NextMode(T::Mode::Move) == T::Mode::Rotate && T::NextMode(T::Mode::Rotate) == T::Mode::Scale &&
              T::NextMode(T::Mode::Scale) == T::Mode::Move,
          "transform: Space cycles Move, Rotate, Scale");
    Check(T::GridSizes().size() == 6 && T::GridSizes().front() == 1.0f && T::GridSizes().back() == 100.0f &&
              T::AngleSteps()[2] == 15.0f,
          "transform: the toolbar's grid sizes (1 - 100) and angle steps (15 the third)");
}
SELFTEST(Early, TestTransformSnapping);

void TestTransformGizmo()
{
    T::Gizmo gizmo;
    gizmo.length = 100.0f;
    gizmo.mode = T::Mode::Move;
    std::vector<T::Segment> move = T::GizmoSegments(gizmo);
    Check(move.size() == 3 && move[0].arrow && move[1].handle == 1 && Near(move[2].b, {0, 0, 100}),
          "transform: Move's three arrows along the gizmo's axes");
    gizmo.mode = T::Mode::Rotate;
    std::vector<T::Segment> rotate = T::GizmoSegments(gizmo);
    bool ring = rotate.size() == 49;
    for (const T::Segment& s : rotate)
        ring = ring && s.handle == 1 && !s.arrow && Near(s.a.y, 0.0f) && Near(s.b.y, 0.0f);
    Check(ring, "transform: Rotate's heading ring (48 segments and a spoke, level, handle 1)");
    gizmo.rotates = false;
    Check(T::GizmoSegments(gizmo).empty(), "transform: no ring for what doesn't turn");
    gizmo.rotates = true;
    gizmo.mode = T::Mode::Scale;
    std::vector<T::Segment> scale = T::GizmoSegments(gizmo);
    int centre = 0;
    for (const T::Segment& s : scale)
        centre += s.handle == T::kCentre ? 1 : 0;
    Check(scale.size() == 3 + 4 * 12 && centre == 12, "transform: Scale's three axes with a cube each and a centre cube");

    // (a projector looking down: x to the right, z down the screen)
    T::Projector top = [](V3 p, float* x, float* y) {
        *x = 500.0f + p.x;
        *y = 500.0f - p.z;
        return true;
    };
    T::Projector front = [](V3 p, float* x, float* y) {
        *x = 500.0f + p.x;
        *y = 500.0f - p.y;
        return true;
    };
    gizmo.mode = T::Mode::Move;
    Check(T::HandleAt(gizmo, front, 550.0f, 503.0f, 8.0f) == 0 && T::HandleAt(gizmo, front, 503.0f, 450.0f, 8.0f) == 1 &&
              T::HandleAt(gizmo, top, 502.0f, 440.0f, 8.0f) == 2 && T::HandleAt(gizmo, front, 560.0f, 440.0f, 8.0f) == -1,
          "transform: Move's arrow under the cursor (within 8 pixels), none away from them");
    gizmo.mode = T::Mode::Scale;
    Check(T::HandleAt(gizmo, front, 501.0f, 501.0f, 8.0f) == T::kCentre && T::HandleAt(gizmo, front, 600.0f, 504.0f, 8.0f) == 0,
          "transform: Scale's centre cube wins at the pivot; an axis' cube at its end");
    gizmo.mode = T::Mode::Rotate;
    Check(T::HandleAt(gizmo, top, 580.0f, 500.0f, 8.0f) == 1 && T::HandleAt(gizmo, top, 540.0f, 470.0f, 8.0f) == -1,
          "transform: Rotate's ring under the cursor (0.8 of the length out), not inside it");
    Check(T::HandleColour(0, false) == 0xFFFF4040u && T::HandleColour(2, true) == 0xFFFFFF40u &&
              T::HandleColour(T::kCentre, false) == 0xFFE8E8E8u,
          "transform: handle colours (x red, lit yellow, centre white)");
}
SELFTEST(Early, TestTransformGizmo);

void TestTransformTyped()
{
    T::Typed typed;
    float v = 0.0f;
    Check(!T::TypedValue(typed, &v), "transform typed: nothing typed - no value");
    for (char c : std::string("x12.5"))
        T::TypeKey(&typed, c);
    Check(typed.axis == 0 && typed.axisChanged && T::TypedValue(typed, &v) && Near(v, 12.5f),
          "transform typed: X then 12.5");
    Check(!T::TypeKey(&typed, '.') && typed.text == "12.5", "transform typed: a second '.' refused");
    T::TypeKey(&typed, '-');
    Check(T::TypedValue(typed, &v) && Near(v, -12.5f), "transform typed: '-' turns the sign over");
    T::TypeKey(&typed, '-');
    Check(T::TypedValue(typed, &v) && Near(v, 12.5f), "transform typed: '-' again turns it back");
    T::TypeKey(&typed, 'z');
    Check(typed.axis == 2, "transform typed: another axis replaces it");
    T::TypeKey(&typed, 'z');
    Check(typed.axis == -1, "transform typed: the same axis again drops it");
    std::string described = T::Describe(T::Mode::Move, typed, 1);
    Check(described == "Move along Y: 12.5_", "transform typed: the status bar's text (the arrow's axis by default)");
    for (int i = 0; i < 4; ++i)
        T::TypeKey(&typed, '\b');
    Check(typed.text.empty() && !T::TypeKey(&typed, '\b'), "transform typed: Backspace erases, nothing left to erase");
    T::TypeKey(&typed, '-');
    Check(!T::TypedValue(typed, &v), "transform typed: '-' alone is no value");
    Check(!T::TypeKey(&typed, 'q') && !T::TypeKey(&typed, ' '), "transform typed: other keys aren't taken");
    T::Typed scale;
    T::TypeKey(&scale, '2');
    Check(T::Describe(T::Mode::Scale, scale, -1) == "Scale (all axes): 2_" &&
              T::Describe(T::Mode::Rotate, scale, -1) == "Turn (degrees of heading): 2_",
          "transform typed: Scale and Rotate's texts");
}
SELFTEST(Early, TestTransformTyped);

void TestTransformRepeat()
{
    T::Repeat memory;
    T::RepeatStep step;
    Check(!T::RepeatFor(memory, T::Kind::Objects, {5}, {0, 0, 0}, 0.0f, &step), "repeat: nothing duplicated yet");
    T::NoteDuplicate(&memory, T::Kind::Objects, {5, 7}, {0, 0, 0}, 0.0f);
    Check(T::RepeatFor(memory, T::Kind::Objects, {7, 5}, {100, 0, 0}, 0.0f, &step) && Near(step.offset, {100, 0, 0}) &&
              step.turn == 0.0f && step.quarters == 0,
          "repeat: the copies' move since they were made (in any order)");
    Check(Near(T::RepeatTarget({100, 0, 0}, step), {200, 0, 0}), "repeat: the next copy as far again (a row)");
    Check(!T::RepeatFor(memory, T::Kind::Objects, {5}, {100, 0, 0}, 0.0f, &step) &&
              !T::RepeatFor(memory, T::Kind::Scenery, {5, 7}, {100, 0, 0}, 0.0f, &step),
          "repeat: not for another selection, nor another kind");
    T::NoteDuplicate(&memory, T::Kind::Objects, {9}, {0, 0, 0}, 0.2f);
    Check(T::RepeatFor(memory, T::Kind::Objects, {9}, {100, 0, 0}, 0.2f + kPi / 2.0f, &step) &&
              Near(step.turn, kPi / 2.0f) && Near(T::RepeatTarget({100, 0, 0}, step), {100, 0, -100}),
          "repeat: a turned copy - the next one's offset turned as much (an arc)");
    T::NoteDuplicate(&memory, T::Kind::Scenery, {11}, {0, 0, 0}, 0.0f);
    Check(T::RepeatFor(memory, T::Kind::Scenery, {11}, {100, 0, 0}, kPi / 2.0f, &step) && step.quarters == 1 &&
              step.turn == 0.0f && T::RepeatTarget({100, 0, 0}, step).x == 100.0f &&
              T::RepeatTarget({100, 0, 0}, step).z == -100.0f,
          "repeat: scenery turns by whole quarters - its offset turned exactly");
}
SELFTEST(Early, TestTransformRepeat);

void TestSnapPure()
{
    std::vector<V3> points = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}};
    Check(Snap::Nearest(points, {9, 2, 0}) == 1 && Snap::Nearest(points, {9, 8, 0}) == 2 && Snap::Nearest({}, {0, 0, 0}) == -1,
          "snap: the nearest point");
    auto project = [](V3 p, float* x, float* y) {
        if (p.z < 0.0f)
            return false;
        *x = p.x;
        *y = p.y;
        return true;
    };
    std::vector<V3> corners = {{0, 0, -1}, {5, 5, 0}, {50, 50, 0}};
    Check(Snap::NearestOnScreen(corners, project, 1.0f, 1.0f) == 1, "snap: the corner nearest the cursor on screen (behind: passed)");
    V3 rows[3] = {{2, 0, 0}, {0, 2, 0}, {0, 0, 2}};
    float t = 0.3f;
    V3 normal = {std::sin(t), std::cos(t), 0.0f};
    Check(Snap::AlignRows(rows, normal) && Near(rows[1], normal * 2.0f) && Near(rows[2], {0, 0, 2}) &&
              Near(rows[0], V3{std::cos(t), -std::sin(t), 0.0f} * 2.0f),
          "snap: rows stood square to a slope (up along the normal, the heading kept, lengths kept, right = up x at)");
    V3 flat[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Check(!Snap::AlignRows(flat, {0, 0, 0}) && !Snap::AlignRows(flat, {0, 0, 1}), "snap: no normal / at along it: unchanged");
    Check(Near(Snap::OnSurface({5, 100, 5}, 80.0f, {30, 10, 40}), {30, 30, 40}),
          "snap: the selection's base onto the surface's point");
}
SELFTEST(Early, TestSnapPure);

} // namespace
