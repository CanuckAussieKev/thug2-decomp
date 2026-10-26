// Self-tests for the level editor's navigation and hints (src/game/front/level_editor_hints.*, level_editor_nav.*,
// level_editor_ux8.*; not original): the status bar's context hints (from the keys as bound, by state, the modifiers'
// lists, the glyph tokens); walk mode's steps (falling, walls and sliding, steps, jumps, ceilings, a level's edge);
// double clicks, the teleport's place, the follow camera, wheel drill-through's stepping; the pies' slices and a
// stick's response; quick selection sets; UX8's keys registered without clashing with any other's.

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <SDL2/SDL_scancode.h>

#include "game/front/level_editor.h"
#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_hints.h"
#include "game/front/level_editor_nav.h"
#include "game/front/level_editor_ux8.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace Actions = Front::LevelEditor::Actions;
namespace Hints = Front::LevelEditor::Hints;
namespace Nav = Front::LevelEditor::Nav;
using LE::V3;

bool Has(const std::vector<Hints::Hint>& hints, const std::string& keys, const std::string& what)
{
    for (const Hints::Hint& h : hints) {
        if (h.keys == keys && (what.empty() || h.what == what))
            return true;
    }
    return false;
}
bool HasWhat(const std::vector<Hints::Hint>& hints, const std::string& what)
{
    for (const Hints::Hint& h : hints) {
        if (h.what == what)
            return true;
    }
    return false;
}

void TestEditorUx8Hints()
{
    std::map<std::string, std::string> keys = {
        {"forward", "W"}, {"left", "A"}, {"back", "S"}, {"right", "D"}, {"fly_up", "E / Space"}, {"fly_down", "Q"},
        {"drop", "G"}, {"pie_context", "Q / Tab"}, {"walk_mode", "Shift+`"}, {"duplicate", "Ctrl+D"},
        {"tool_move", "W"}, {"tool_rotate", "E"}, {"tool_scale", "R"}, {"browser", "B"}, {"undo", "Ctrl+Z"},
        {"nudge_forward", "Up"}, {"nudge_back", "Down"}, {"nudge_left", "Left"}, {"nudge_right", "Right"},
        {"follow_selection", "Shift+F"}, {"frame_selection", "F"}, {"delete", "Delete"}};
    auto keyOf = [&](const char* id) {
        auto it = keys.find(id);
        return it == keys.end() ? std::string() : it->second;
    };
    Hints::Context idle;
    std::vector<Hints::Hint> h = Hints::Build(idle, keyOf);
    Check(Has(h, "RMB+WASD", "fly") && Has(h, "LMB", "select") && Has(h, "2xLMB", "go there") &&
              Has(h, "Alt+Wheel", "drill through") && Has(h, "Q", "pie") && Has(h, "Shift+`", "walk"),
          "editor ux8: flying hints - RMB+WASD, click, double click, Alt+wheel, the pie, walk (first keys)");
    Check(h.size() <= Hints::kMaxHints, "editor ux8: at most kMaxHints hints");
    Hints::Context fly = idle;
    fly.flyAlways = true;
    Check(Has(Hints::Build(fly, keyOf), "WASD", "fly"), "editor ux8: wasd_flies=1 - WASD without RMB");
    Hints::Context looking = idle;
    looking.rmb = true;
    h = Hints::Build(looking, keyOf);
    Check(Has(h, "WASD", "fly") && Has(h, "Wheel", "fly speed") && Has(h, "QE", "down / up") &&
              !HasWhat(h, "select"),
          "editor ux8: the right button held - flying's keys, the wheel's speed");
    Hints::Context sel = idle;
    sel.selection = Hints::Selection::Objects;
    h = Hints::Build(sel, keyOf);
    Check(Has(h, "G", "drop") && Has(h, "WER", "") && Has(h, "Up Down Left Right", "nudge") &&
              Has(h, "Shift+F", "follow") && Has(h, "Ctrl+D", "copy"),
          "editor ux8: an object selected - drop, the tools, nudges, follow, copy");
    keys["drop"] = "K";
    keys["follow_selection"] = "";
    h = Hints::Build(sel, keyOf);
    Check(Has(h, "K", "drop") && !Has(h, "G", "drop") && !HasWhat(h, "follow"),
          "editor ux8: a rebound key shows as bound; an unbound action is left out");
    sel.following = true;
    keys["follow_selection"] = "Shift+F";
    Check(Has(Hints::Build(sel, keyOf), "Shift+F", "stop following"), "editor ux8: following - Shift+F stops");
    Hints::Context drag = sel;
    drag.drag = Hints::Drag::Move;
    h = Hints::Build(drag, keyOf);
    Check(Has(h, "Esc", "put it back") && Has(h, "Ctrl", "grid") && !HasWhat(h, "drop"),
          "editor ux8: dragging - the drag's modifiers and Esc, not the selection's keys");
    drag.carried = 3;
    Check(Has(Hints::Build(drag, keyOf), "Alt", "leave the 3 carried"), "editor ux8: dragging scenery that carries");
    Hints::Context walk = idle;
    walk.walking = true;
    h = Hints::Build(walk, keyOf);
    Check(Has(h, "WASD", "walk") && Has(h, "E", "jump") && Has(h, "Shift+`", "stop walking") && !HasWhat(h, "select"),
          "editor ux8: walking - walk, jump (the fly-up key), stop");
    Hints::Context pie = sel;
    pie.pieOpen = true;
    h = Hints::Build(pie, keyOf);
    Check(Has(h, "LMB", "run it") && Has(h, "Q", "release: run") && !HasWhat(h, "drop"),
          "editor ux8: a pie up - its own hints");
    pie.pieSticky = true;
    Check(!HasWhat(Hints::Build(pie, keyOf), "release: run"), "editor ux8: a tapped pie - no let-go");
    Hints::Context pad = sel;
    pad.pad = true;
    h = Hints::Build(pad, keyOf);
    Check(Has(h, "PadA", "") && Has(h, "LS", "fly") && Has(h, "DPad", "nudge") && !Has(h, "G", ""),
          "editor ux8: controller mode - the controller's glyphs");
    Hints::Context alt = idle;
    alt.mods = Actions::kAlt;
    alt.modded = {{"Alt+P", "Problems window"}};
    h = Hints::Build(alt, keyOf);
    Check(Has(h, "Alt+Wheel", "cycle under cursor") && Has(h, "Alt+P", "Problems window") &&
              !HasWhat(h, "pie"),
          "editor ux8: Alt held - what Alt changes and what it's bound to");
    Hints::Context ruler = idle;
    ruler.rulerArmed = true;
    Check(Has(Hints::Build(ruler, keyOf), "LMB", "put an end"), "editor ux8: the ruler armed - a click puts an end");
    // The pieces.
    Check(Hints::FirstKey("Ctrl+Y / Ctrl+Shift+Z") == "Ctrl+Y" && Hints::FirstKey("Right mouse") == "Right mouse",
          "editor ux8: a keys text's first alternative");
    Check(Hints::JoinKeys({"W", "A", "S", "D"}) == "WASD" && Hints::JoinKeys({"Up", "Down"}) == "Up Down",
          "editor ux8: single keys run together, longer ones spaced");
    std::vector<std::string> t = Hints::Tokens("RMB+WASD");
    std::vector<std::string> kp = Hints::Tokens("Ctrl+Keypad+");
    Check(t.size() == 2 && t[0] == "RMB" && t[1] == "WASD" && Hints::Tokens("Keypad+").size() == 1 && kp.size() == 2 &&
              kp[1] == "Keypad+",
          "editor ux8: tokens split on '+', a key ending in '+' whole");
    Check(Hints::GlyphOf("LMB") == Hints::Glyph::MouseLeft && Hints::GlyphOf("Wheel") == Hints::Glyph::Wheel &&
              Hints::GlyphOf("PadB") == Hints::Glyph::PadB && Hints::GlyphOf("LT/RT") == Hints::Glyph::PadOther &&
              Hints::GlyphOf("B") == Hints::Glyph::Key && Hints::GlyphOf("2xLMB") == Hints::Glyph::DoubleClick,
          "editor ux8: glyphs - mouse, controller, keys");
    std::vector<Actions::Action> actions(3);
    actions[0].name = "Undo";
    actions[0].keys = {{SDL_SCANCODE_Z, Actions::kCtrl}};
    actions[1].name = "Turn";
    actions[1].keys = {{SDL_SCANCODE_Z, 0}};
    actions[2].name = "Redo";
    actions[2].keys = {{SDL_SCANCODE_Y, Actions::kCtrl | Actions::kShift}, {SDL_SCANCODE_Y, Actions::kCtrl}};
    auto m = Hints::ModifierHints(actions, Actions::kCtrl, 9);
    Check(m.size() == 2 && m[0].first == "Ctrl+Z" && m[0].second == "Undo" && m[1].first == "Ctrl+Y" &&
              Hints::ModifierHints(actions, 0, 9).empty() && Hints::ModifierHints(actions, Actions::kCtrl, 1).size() == 1,
          "editor ux8: a modifier's list - its exact chords, in order, limited");
}
SELFTEST(Early, TestEditorUx8Hints);

// A stand-in level for walking: a floor at y 0 for |x| < 1000, a wall at x = 100 (z < 400, up to 300 high), a step 20
// high over x -300..-200, a low ceiling at y 80 over z > 600 (x > -50).
bool FakeCast(V3 from, V3 to, V3* hit)
{
    float best = 2.0f;
    V3 d = to - from;
    auto consider = [&](float t, V3 p, bool ok) {
        if (ok && t >= 0.0f && t <= 1.0f && t < best) {
            best = t;
            *hit = p;
        }
    };
    if (d.y != 0.0f) {
        for (float plane : {0.0f, 20.0f, 80.0f}) {
            float t = (plane - from.y) / d.y;
            V3 p = from + d * t;
            bool ok = plane == 0.0f    ? std::fabs(p.x) < 1000.0f
                      : plane == 20.0f ? p.x >= -300.0f && p.x <= -200.0f
                                       : p.z > 600.0f && p.x > -50.0f && p.x < 1000.0f;
            consider(t, p, ok);
        }
    }
    if (d.x != 0.0f) {
        float t = (100.0f - from.x) / d.x;
        V3 p = from + d * t;
        consider(t, p, p.z < 400.0f && p.y < 300.0f && p.y > 0.0f);
    }
    return best <= 1.0f;
}

void Run(Nav::Walker* w, float yaw, Nav::WalkInput in, float seconds, float* maxY = nullptr)
{
    for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) {
        Nav::Step(w, yaw, in, 1.0f / 60.0f, FakeCast);
        in.jump = false;
        if (maxY && w->feet.y > *maxY)
            *maxY = w->feet.y;
    }
}

void TestEditorUx8Walk()
{
    const float kHalfPi = 1.57079633f;
    Nav::Walker w = Nav::WalkerAt({0.0f, 500.0f, 0.0f});
    Check(w.feet.y == 500.0f - Nav::kEyeHeight && Nav::EyeOf(w).y == 500.0f, "editor ux8: walker / eye round trip");
    Run(&w, 0.0f, {}, 3.0f);
    Check(w.grounded && std::fabs(w.feet.y) < 0.01f && w.vy == 0.0f, "editor ux8: walking drops onto the ground and stands");
    // Into the wall at x = 100 (yaw 90 degrees: forward +x).
    Nav::Walker a = w;
    Run(&a, kHalfPi, {1.0f, 0.0f, false, false}, 2.0f);
    Check(a.feet.x <= 100.0f - Nav::kRadius + 0.01f && a.feet.x > 60.0f && a.grounded,
          "editor ux8: a wall stops the walker a radius short");
    // Sliding: 45 degrees into it - x stops, z goes on.
    Nav::Walker b = w;
    Run(&b, kHalfPi * 0.5f, {1.0f, 0.0f, false, false}, 1.5f);
    Check(b.feet.x <= 100.0f - Nav::kRadius + 0.01f && b.feet.z > 150.0f, "editor ux8: along a wall the walker slides");
    // A step 20 high (under kStepHeight) walked onto.
    Nav::Walker c = w;
    c.feet.x = -100.0f;
    Run(&c, -kHalfPi, {1.0f, 0.0f, false, false}, 0.5f); // (110 units: onto the step at -200..-300)
    Check(c.feet.x < -200.0f && c.feet.x > -300.0f && std::fabs(c.feet.y - 20.0f) < 0.01f && c.grounded, "editor ux8: a step walked up");
    // Running is faster.
    Nav::Walker r1 = w, r2 = w;
    r1.feet.z = r2.feet.z = -500.0f;
    Run(&r1, 3.14159265f, {1.0f, 0.0f, false, false}, 0.5f);
    Run(&r2, 3.14159265f, {1.0f, 0.0f, false, true}, 0.5f);
    Check(std::fabs(r1.feet.z + 500.0f - -Nav::kWalkSpeed * 0.5f) < 6.0f && r2.feet.z < r1.feet.z - 100.0f,
          "editor ux8: walking speed, Shift runs");
    // A jump: up about kJumpSpeed^2 / 2g and down again.
    Nav::Walker j = w;
    float top = 0.0f;
    Run(&j, 0.0f, {0.0f, 0.0f, true, false}, 1.5f, &top);
    float expect = Nav::kJumpSpeed * Nav::kJumpSpeed / (2.0f * Nav::kGravity);
    Check(top > expect - 3.0f && top < expect + 3.0f && j.grounded && std::fabs(j.feet.y) < 0.01f,
          "editor ux8: a jump rises its height and lands");
    Nav::Walker air = Nav::WalkerAt({0.0f, 400.0f, 0.0f});
    Run(&air, 0.0f, {0.0f, 0.0f, true, false}, 0.05f);
    Check(air.vy < 0.0f, "editor ux8: no jump in the air");
    // A low ceiling stops it (the head 62 up, the ceiling at 80).
    Nav::Walker k = w;
    k.feet = {0.0f, 0.0f, 700.0f};
    float kTop = 0.0f;
    Run(&k, 0.0f, {0.0f, 0.0f, true, false}, 1.0f, &kTop);
    Check(kTop < 80.0f - Nav::kEyeHeight + 1.0f && k.grounded, "editor ux8: a ceiling stops a jump");
    // No ground at all (x beyond the floor): stays at its height.
    Nav::Walker e = Nav::WalkerAt({2000.0f, 300.0f, 0.0f});
    Run(&e, 0.0f, {}, 1.0f);
    Check(e.feet.y == 300.0f - Nav::kEyeHeight, "editor ux8: no ground below - the walker doesn't fall out of the level");
    // Its right is the camera frame's right.
    Nav::Walker s = w;
    s.feet = {-600.0f, 0.0f, -300.0f};
    Nav::Step(&s, 0.3f, {0.0f, 1.0f, false, false}, 0.1f, FakeCast);
    LE::Frame frame = LE::FrameFromYawPitch(0.3f, 0.0f);
    V3 moved = LE::Normalize(s.feet - V3{-600.0f, 0.0f, -300.0f});
    Check(LE::Dot(moved, frame.right) > 0.999f, "editor ux8: right is the view's right");
}
SELFTEST(Early, TestEditorUx8Walk);

void TestEditorUx8Nav()
{
    Check(Nav::IsDoubleClick(1000, 10, 10, 1300, 12, 9) && !Nav::IsDoubleClick(1000, 10, 10, 1500, 10, 10) &&
              !Nav::IsDoubleClick(1000, 10, 10, 1100, 30, 10) && !Nav::IsDoubleClick(1000, 10, 10, 900, 10, 10),
          "editor ux8: a double click - soon, near, in order");
    V3 cam = {0.0f, 100.0f, 0.0f};
    V3 far = Nav::TeleportTarget(cam, {0.0f, 100.0f, 1000.0f});
    V3 near = Nav::TeleportTarget(cam, {0.0f, 0.0f, 100.0f});
    Check(std::fabs(far.z - (1000.0f - Nav::kStandOff)) < 0.01f && far.y >= 100.0f + Nav::kLift - 0.01f,
          "editor ux8: a teleport stands off from the point, above it");
    Check(near.z > 0.0f && near.z < 100.0f && std::fabs(near.z - 20.0f) < 0.5f && near.y >= Nav::kLift - 0.01f,
          "editor ux8: a near teleport goes 80% of the way");
    Nav::Follower f;
    V3 d0 = Nav::FollowStep(&f, true, {10, 0, 0});
    V3 d1 = Nav::FollowStep(&f, true, {15, 2, 0});
    V3 d2 = Nav::FollowStep(&f, false, {50, 0, 0});
    V3 d3 = Nav::FollowStep(&f, true, {60, 0, 0});
    Check(d0.x == 0.0f && d1.x == 5.0f && d1.y == 2.0f && d2.x == 0.0f && d3.x == 0.0f,
          "editor ux8: following - the selection's move a frame; afresh after a pause");
    Check(Nav::DrillIndex(-1, 1, 4) == 0 && Nav::DrillIndex(-1, -1, 4) == 3 && Nav::DrillIndex(0, 1, 4) == 1 &&
              Nav::DrillIndex(3, 1, 4) == 0 && Nav::DrillIndex(0, -1, 4) == 3 && Nav::DrillIndex(1, 6, 4) == 3 &&
              Nav::DrillIndex(0, 1, 0) == -1,
          "editor ux8: drilling deeper / nearer round the ends");
    Check(Nav::CycleIndexShowing(0, 4) == 3 && Nav::CycleIndexShowing(2, 4) == 1, "editor ux8: the cycle shows the drilled one");
    Check(Nav::SliceAt(0, -100, 8, 20) == 0 && Nav::SliceAt(100, 0, 8, 20) == 2 && Nav::SliceAt(0, 100, 8, 20) == 4 &&
              Nav::SliceAt(-100, 0, 8, 20) == 6 && Nav::SliceAt(5, 5, 8, 20) == -1 && Nav::SliceAt(-30, -100, 8, 20) == 0 &&
              Nav::SliceAt(-50, -100, 8, 20) == 7 && Nav::SliceAt(0, -100, 0, 20) == -1,
          "editor ux8: pie slices - up first, clockwise, the dead zone");
    bool round = true;
    for (int n = 3; n <= 10; ++n) {
        for (int i = 0; i < n; ++i) {
            float dx, dy;
            Nav::SliceDirection(i, n, &dx, &dy);
            round = round && Nav::SliceAt(dx * 100.0f, dy * 100.0f, n, 20.0f) == i;
        }
    }
    Check(round, "editor ux8: each slice's direction points at itself");
    Check(Nav::Stick(0, 0.2f) == 0.0f && Nav::Stick(5000, 0.2f) == 0.0f && Nav::Stick(32767, 0.2f) == 1.0f &&
              Nav::Stick(-32768, 0.2f) == -1.0f && Nav::Stick(19661, 0.2f) > 0.2f && Nav::Stick(19661, 0.2f) < 0.3f &&
              Nav::Stick(-19661, 0.2f) < -0.2f,
          "editor ux8: a stick - dead zone, full tilt, squared between, the sign kept");
}
SELFTEST(Early, TestEditorUx8Nav);

void TestEditorUx8Sets()
{
    namespace Organize = Front::LevelEditor::Organize;
    Organize::State s;
    Organize::Group other;
    other.name = "Mine";
    other.members = {{'o', 7}};
    s.groups.push_back(other);
    Check(Nav::QuickSetName(3) == "Set 3", "editor ux8: quick set names");
    Check(!Nav::StoreGroup(&s, "Set 1", {}) && s.groups.size() == 1, "editor ux8: an empty selection stores nothing");
    Check(Nav::StoreGroup(&s, "Set 1", {{'o', 1}, {'o', 2}}) && s.groups.size() == 2 && Nav::FindGroup(s, "Set 1") == 1,
          "editor ux8: a set stored as a group");
    Check(Nav::StoreGroup(&s, "Set 1", {{'s', 9}}) && s.groups.size() == 2 && s.groups[1].members.size() == 1 &&
              s.groups[1].members[0].kind == 's' && s.groups[0].name == "Mine",
          "editor ux8: storing again replaces the set, the other groups kept");
    Check(Nav::StoreGroup(&s, "Mine", {{'o', 7}, {'o', 8}}) && Nav::RemoveFromGroup(&s, 0, {{'o', 7}}) &&
              s.groups[0].members.size() == 1 && s.groups[0].members[0].id == 8 &&
              !Nav::RemoveFromGroup(&s, 0, {{'o', 99}}) && !Nav::RemoveFromGroup(&s, 5, {{'o', 8}}) &&
              Nav::FindGroup(s, "Set 2") == -1,
          "editor ux8: taking from a set");
}
SELFTEST(Early, TestEditorUx8Sets);

void TestEditorUx8Keys()
{
    Front::LevelEditor::Ux8::RegisterActions();
    const std::vector<Actions::Action>& all = Actions::All();
    const char* const mine[] = {"pie_context", "pie_view", "walk_mode", "follow_selection", "teleport",
                                "controller_mode", "quick_set_store_1", "quick_set_9", "hint_drill"};
    bool found = true;
    for (const char* id : mine)
        found = found && Actions::Find(id) >= 0;
    Check(found, "editor ux8: its actions in the registry");
    // No key press of UX8's defaults is any other action's default.
    bool clean = true;
    std::string clash;
    for (size_t i = 0; i < all.size(); ++i) {
        const Actions::Action& a = all[i];
        bool ours = a.id.rfind("pie_", 0) == 0 || a.id == "walk_mode" || a.id == "follow_selection" ||
                    a.id == "teleport" || a.id == "controller_mode" || a.id.rfind("quick_set", 0) == 0;
        if (!ours || a.kind == Actions::Kind::Info || a.kind == Actions::Kind::Hold)
            continue;
        for (size_t j = 0; j < all.size(); ++j) {
            if (j == i || all[j].kind == Actions::Kind::Info || all[j].kind == Actions::Kind::Hold)
                continue;
            for (const Actions::Chord& c : a.defaults) {
                for (const Actions::Chord& d : all[j].defaults) {
                    if (Actions::SameBinding(c, d)) {
                        clean = false;
                        clash = a.id + " / " + all[j].id;
                    }
                }
            }
        }
    }
    Check(clean, ("editor ux8: its default keys clash with none " + clash).c_str());
    // And a press of each resolves to it alone (the more specific chord wins over the plain one it extends).
    std::vector<std::vector<Actions::Chord>> bound(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].kind == Actions::Kind::Press || all[i].kind == Actions::Kind::Toggle)
            bound[i] = all[i].defaults;
    }
    auto only = [&](int scancode, uint8_t held, const char* id) {
        std::vector<int> r = Actions::Resolve(bound, scancode, held);
        return r.size() == 1 && all[r[0]].id == id;
    };
    Check(only(SDL_SCANCODE_F, Actions::kShift, "follow_selection") && only(SDL_SCANCODE_F, 0, "frame_selection") &&
              only(SDL_SCANCODE_Q, Actions::kAlt, "pie_view") && only(SDL_SCANCODE_Q, 0, "pie_context") &&
              only(SDL_SCANCODE_TAB, Actions::kShift, "pie_view") &&
              only(SDL_SCANCODE_1, Actions::kCtrl | Actions::kAlt, "quick_set_store_1") &&
              only(SDL_SCANCODE_1, Actions::kCtrl, "bookmark_store_1") &&
              only(SDL_SCANCODE_1, Actions::kAlt | Actions::kShift, "quick_set_1") &&
              only(SDL_SCANCODE_1, Actions::kAlt, "view_mode_lit") &&
              only(SDL_SCANCODE_GRAVE, Actions::kShift, "walk_mode") && only(SDL_SCANCODE_GRAVE, 0, "tool_space"),
          "editor ux8: Shift+F, Q / Alt+Q, Shift+Tab, Ctrl+Alt+1, Alt+Shift+1, Shift+` each fire alone");
}
SELFTEST(Early, TestEditorUx8Keys);

} // namespace
