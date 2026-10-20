// Self-tests for the level editor's node edits (src/game/front/level_editor_node_edit.*, not original - phase 2 of the
// editor plan): the angle and heading maths, snapping and the drop to the ground, the undo steps of node edits, which
// nodes the editor moves, and the layouts' version 2 `set` records (and version 1 files still read).

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace NE = Front::LevelEditor::NodeEdit;
using LE::V3;

constexpr float kPi = 3.14159265f;

bool Near(float a, float b, float tolerance = 1e-5f) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance = 1e-5f)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

// --- angles and headings ------------------------------------------------------------------------------------------

void TestEditorNodeAngles()
{
    Check(NE::WrapAngle(0.0f) == 0.0f && Near(NE::WrapAngle(1.0f), 1.0f) && Near(NE::WrapAngle(-1.0f), -1.0f),
          "editor node edits: angles within a half turn stay");
    Check(Near(NE::WrapAngle(kPi), kPi) && Near(NE::WrapAngle(-kPi), kPi) && Near(NE::WrapAngle(3.0f * kPi), kPi),
          "editor node edits: a half turn either way is +pi");
    Check(Near(NE::WrapAngle(2.0f * kPi + 0.1f), 0.1f, 1e-4f) && Near(NE::WrapAngle(-2.0f * kPi - 0.25f), -0.25f, 1e-4f) &&
              Near(NE::WrapAngle(1.5f * kPi), -0.5f * kPi, 1e-4f),
          "editor node edits: whole turns taken off");
    Check(NE::WrapAngle(NAN) == 0.0f && NE::WrapAngle(INFINITY) == 0.0f, "editor node edits: no angle from NaN");

    // Z / X: 15 degrees about the up axis - the Y angle only.
    V3 angles = {0.1f, 0.0f, -0.2f};
    V3 turned = NE::Turned(angles, 1);
    Check(Near(turned.y, 15.0f * kPi / 180.0f) && turned.x == 0.1f && turned.z == -0.2f,
          "editor node edits: X turns the Y angle 15 degrees, X and Z angles kept");
    Check(Near(NE::Turned({0, 0, 0}, -1).y, -15.0f * kPi / 180.0f), "editor node edits: Z turns back");
    V3 round = {0, 0.3f, 0};
    for (int i = 0; i < 24; ++i)
        round = NE::Turned(round, 1);
    Check(Near(round.y, 0.3f, 1e-4f), "editor node edits: 24 notches are a whole turn");
    Check(Near(NE::Turned({0, 0, 0}, 12).y, kPi, 1e-4f) && Near(NE::Turned({0, 3.0f, 0}, 1).y, 3.0f + 0.2617994f - 2.0f * kPi, 1e-4f),
          "editor node edits: turning past a half turn wraps");
    // The way a node faces (the game's angles-to-matrix 0x0041A730: (0, a, 0) faces (sin a, 0, cos a)) turns as the
    // editor turns objects (TurnAboutUp).
    auto facing = [](float a) { return V3{std::sin(a), 0.0f, std::cos(a)}; };
    bool same = true;
    for (float a : {0.0f, 0.7f, -2.5f, 3.1f}) {
        same &= Near(LE::TurnAboutUp(facing(a), 15.0f * kPi / 180.0f), facing(NE::Turned({0, a, 0}, 1).y), 1e-4f);
        same &= Near(LE::TurnAboutUp(facing(a), -15.0f * kPi / 180.0f), facing(NE::Turned({0, a, 0}, -1).y), 1e-4f);
    }
    Check(same, "editor node edits: a node turns the way objects turn");

    // The Inspector's heading: the Y angle in degrees, (-180, 180].
    Check(Near(NE::HeadingDegrees({0, kPi / 2.0f, 0}), 90.0f, 1e-3f) && Near(NE::HeadingDegrees({0, -kPi, 0}), 180.0f, 1e-3f) &&
              Near(NE::HeadingDegrees({1, -0.5f * kPi, 2}), -90.0f, 1e-3f),
          "editor node edits: the heading in degrees");
    V3 set = NE::WithHeading({0.25f, 1.0f, -0.5f}, 270.0f);
    Check(Near(set.y, -kPi / 2.0f, 1e-5f) && set.x == 0.25f && set.z == -0.5f,
          "editor node edits: a heading typed in sets the Y angle (wrapped), the others kept");
    Check(Near(NE::HeadingDegrees(NE::WithHeading({0, 0, 0}, 37.5f)), 37.5f, 1e-3f) &&
              Near(NE::HeadingDegrees(NE::WithHeading({0, 0, 0}, -179.0f)), -179.0f, 1e-3f),
          "editor node edits: heading in and out");
}
SELFTEST(Early, TestEditorNodeAngles);

// --- snapping and the drop to the ground --------------------------------------------------------------------------

void TestEditorNodeSnapDrop()
{
    Check(LE::SnapToGrid(37.0f, 25.0f) == 25.0f && LE::SnapToGrid(38.0f, 25.0f) == 50.0f &&
              LE::SnapToGrid(-13.0f, 25.0f) == -25.0f && LE::SnapToGrid(-12.0f, 25.0f) == 0.0f &&
              LE::SnapToGrid(1250.0f, 25.0f) == 1250.0f,
          "editor: snapping to 25");
    Check(LE::SnapToGrid(5.3f, 0.0f) == 5.3f, "editor: no grid, no snapping");
    Check(Near(LE::GroundRayStart({10, 120, 5}, 120, true), {10, 170, 5}) &&
              Near(LE::GroundRayStart({10, 120, 5}, 80, false), {10, 79, 5}),
          "editor: G's ray starts above a node's or object's base, just under scenery's bottom");
    Check(Near(LE::DroppedTo({10, 120, 5}, 120, 40), {10, 40, 5}) &&
              Near(LE::DroppedTo({10, 130, 5}, 100, 40), {10, 70, 5}) &&
              Near(LE::DroppedTo({-3, -50, 2}, -50, 25), {-3, 25, 2}),
          "editor: dropped so the base is on the ground (down or up)");
}
SELFTEST(Early, TestEditorNodeSnapDrop);

// --- undo steps of node edits, which nodes move --------------------------------------------------------------------

void TestEditorNodeUndo()
{
    // A world of nodes by name, read and written as the editor does through NodeEdit::Read / Set.
    std::map<uint32_t, NE::Values> world;
    NE::Reader read = [&](uint32_t name, NE::Values* values) {
        auto it = world.find(name);
        if (it == world.end())
            return false;
        *values = it->second;
        return true;
    };
    NE::Writer write = [&](uint32_t name, const NE::Values& values) { world[name] = values; };
    const uint32_t kA = 0x11111111, kB = 0x22222222, kMissing = 0x33333333;
    NE::Values v0 = {{0, 0, 0}, {0, 0, 0}}, v1 = {{100, 0, -50}, {0, 0, 0}}, v2 = {{100, 0, -50}, {0, 0.5f, 0}};
    world[kA] = v0;
    world[kB] = {{7, 8, 9}, {0, 1, 0}};

    Check(NE::Capture({kA, kMissing}, read).nodes.size() == 1, "editor node edits: a step skips nodes that aren't there");
    // The editor's history: a step before each change (kept when it changed something), undo puts it back and keeps
    // what it replaced for redo.
    std::vector<NE::Step> undo, redo;
    auto change = [&](const NE::Values& to) {
        NE::Step before = NE::Capture({kA}, read);
        write(kA, to);
        if (NE::Differs(before, read)) {
            undo.push_back(before);
            redo.clear();
        }
    };
    auto stepHistory = [&](std::vector<NE::Step>* from, std::vector<NE::Step>* to) {
        NE::Step step = from->back();
        from->pop_back();
        std::vector<uint32_t> names;
        for (const auto& node : step.nodes)
            names.push_back(node.first);
        to->push_back(NE::Capture(names, read));
        NE::Put(step, write);
    };
    change(v1); // moved
    change(v2); // turned
    change(v2); // a click that moved nothing: no step
    Check(undo.size() == 2, "editor node edits: a move and a turn are steps; nothing changed, no step");
    stepHistory(&undo, &redo);
    Check(NE::Same(world[kA], v1), "editor node edits: undo puts the turn back");
    stepHistory(&undo, &redo);
    Check(NE::Same(world[kA], v0) && undo.empty() && redo.size() == 2, "editor node edits: undo puts the move back");
    stepHistory(&redo, &undo);
    stepHistory(&redo, &undo);
    Check(NE::Same(world[kA], v2) && redo.empty(), "editor node edits: redo does them again");
    // Home: the level's place as a change of its own (undoable).
    change(v0);
    Check(NE::Same(world[kA], v0) && undo.size() == 3, "editor node edits: Home is a step");
    stepHistory(&undo, &redo);
    Check(NE::Same(world[kA], v2) && NE::Same(world[kB], {{7, 8, 9}, {0, 1, 0}}),
          "editor node edits: undoing Home; other nodes untouched");
    Check(NE::Same(v1, v1) && !NE::Same(v1, v2) && !NE::Same(v0, v1), "editor node edits: places compared");

    // Which nodes the editor moves: point nodes, not chains, objects (moved as objects), scenery or particles.
    namespace N = Front::LevelEditor::Nodes;
    auto movable = [](N::Family family, bool hasPos = true, uint32_t name = 0x1234) {
        N::Node node;
        node.family = family;
        node.hasPos = hasPos;
        node.name = name;
        return NE::Movable(node);
    };
    Check(movable(N::Family::Spawn) && movable(N::Family::Crown) && movable(N::Family::Flag) &&
              movable(N::Family::Light) && movable(N::Family::Sound) && movable(N::Family::Trigger) &&
              movable(N::Family::Other),
          "editor node edits: spawns, crowns, flags, lights, sounds, triggers and other nodes move");
    Check(!movable(N::Family::Rail) && !movable(N::Family::Ladder) && !movable(N::Family::Path) &&
              !movable(N::Family::Object) && !movable(N::Family::Scenery) && !movable(N::Family::Particle),
          "editor node edits: ladders, paths, objects, scenery and particles don't (yet); a rail only when the rail "
          "manager has its point (not offline)");
    Check(!movable(N::Family::Spawn, false) && !movable(N::Family::Spawn, true, 0),
          "editor node edits: nor a node without a place or a name");
}
SELFTEST(Early, TestEditorNodeUndo);

// --- layouts: version 2 and its set records -------------------------------------------------------------------------

void TestEditorLayoutV2()
{
    namespace Layout = Front::LevelEditor::Layout;
    Layout::File file;
    file.scene = 0x4DDB9865;
    file.objects.push_back({0x1B636D59, {1, 2, 3}, {1, 0, 0, 0, 1, 0, 0, 0, 1}});
    file.nodes.push_back({0x8E1A2B3C, true, true, {-1842.0f, 128.5f, 3306.125f}, {0.0f, 1.57079637f, -1e-3f}});
    file.nodes.push_back({0x00000007, true, false, {1580.0f, 266.0f, -1600.0f}, {9, 9, 9}});
    file.nodes.push_back({0x00000008, false, true, {9, 9, 9}, {0.1f, -3.14159274f, 0.0f}});
    file.nodes.push_back({0x00000009, false, false, {1, 1, 1}, {1, 1, 1}}); // (nothing to say: not written)
    std::string text = Layout::Write(file);
    Check(text.rfind("thug2-decomp layout 2\n", 0) == 0, "editor layouts: written as version 2");
    Check(text.find("\nset 8E1A2B3C pos=-1842,128.5,3306.125 angles=0,1.57079637,-0.00100000005") != std::string::npos,
          "editor layouts: a set record's text");
    Check(text.find("set 00000009") == std::string::npos, "editor layouts: an empty set record isn't written");
    Layout::File back;
    int line = -1;
    bool read = Layout::Read(text, &back, &line);
    bool same = read && back.version == 2 && back.scene == file.scene && back.objects.size() == 1 && back.nodes.size() == 3;
    if (same) {
        const Layout::SetRecord& a = back.nodes[0];
        const Layout::SetRecord& b = back.nodes[1];
        const Layout::SetRecord& c = back.nodes[2];
        same = a.name == 0x8E1A2B3C && a.hasPos && a.hasAngles && a.pos[0] == -1842.0f && a.pos[2] == 3306.125f &&
               a.angles[1] == 1.57079637f && a.angles[2] == -1e-3f && b.name == 7 && b.hasPos && !b.hasAngles &&
               b.pos[1] == 266.0f && c.name == 8 && !c.hasPos && c.hasAngles && c.angles[1] == -3.14159274f;
    }
    Check(same, "editor layouts: set records written and read back (floats exact; pos or angles alone)");

    // Read: other fields skipped (a later version's), bad ones refused.
    Check(Layout::Read("thug2-decomp layout 2\nset 0000000A pos=1,2,3 types=Multiplayer,Horse order=7\n", &back, &line) &&
              back.nodes.size() == 1 && back.nodes[0].pos[2] == 3.0f && !back.nodes[0].hasAngles,
          "editor layouts: a set record's other fields skipped");
    Check(!Layout::Read("thug2-decomp layout 2\nset 0000000A pos=1,2\n", &back, &line) && line == 2,
          "editor layouts: a set record's short pos refused");
    Check(!Layout::Read("thug2-decomp layout 2\nset 0000000A pos=1,2,3,4\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nset 0000000A angles=a,b,c\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nset 0000000A pos=1,2,3 pos=4,5,6\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nset 0000000A pos\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nset XYZ pos=1,2,3\n", &back, &line),
          "editor layouts: malformed set records refused");
    Check(Layout::Read("thug2-decomp layout 2\nset 0000000B   # TRG_Restart01\n", &back, &line) && back.nodes.size() == 1 &&
              !back.nodes[0].hasPos && !back.nodes[0].hasAngles,
          "editor layouts: a set record with nothing to set read (and ignored)");

    // Version 1 files still load; later versions' records are skipped; headers checked.
    const char kVersion1[] = "thug2-decomp layout 1\n"
                             "scene 4DDB9865  # TR\n"
                             "sector 61EF3AD4 1 -117.599998 0 150.75 0\n"
                             "object 1B636D59 -3512.30005 -338.700012 3293.6001 1 0 0 0 1 0 0 0 1\n";
    Check(Layout::Read(kVersion1, &back, &line) && back.version == 1 && back.scene == 0x4DDB9865 &&
              back.sectors.size() == 1 && back.objects.size() == 1 && back.nodes.empty() &&
              back.objects[0].pos[1] == -338.700012f,
          "editor layouts: a version 1 file still loads");
    Check(Layout::Read("thug2-decomp layout 3\nscene 0000000A\nnode X Restart pos=1,2,3\nchain Y\n", &back, &line) &&
              back.version == 3 && back.scene == 10,
          "editor layouts: a later version read, its new records skipped");
    Check(!Layout::Read("thug2-decomp layout\n", &back, &line) && !Layout::Read("thug2-decomp layout 0\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 1x\n", &back, &line) &&
              !Layout::Read("thug2-decomp layouts 2\n", &back, &line),
          "editor layouts: a header without a good version refused");
}
SELFTEST(Early, TestEditorLayoutV2);

} // namespace
