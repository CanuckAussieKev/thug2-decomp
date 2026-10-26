// Self-tests for the level editor's arrange tools, adjust-last and linked copies (src/game/front/level_editor_arrange.*,
// level_editor_adjust.*, level_editor_links.*: not original - ours): align / distribute,
// edge-to-edge snapping, the readout, the top outline for auto-rails, the adjust panel's numbers, linked groups and
// their layout records.

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_adjust.h"
#include "game/front/level_editor_arrange.h"
#include "game/front/level_editor_links.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace A = Front::LevelEditor::Arrange;
namespace Adj = Front::LevelEditor::Adjust;
namespace Links = Front::LevelEditor::Links;
using LE::V3;

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool Near(V3 a, V3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

void TestArrangeAlign()
{
    std::vector<A::Span> spans = {{0, 10}, {20, 24}, {-5, 5}};
    std::vector<float> m = A::AlignMoves(spans, A::Edge::Min, A::Reference::Bounds);
    Check(m.size() == 3 && Near(m[0], -5) && Near(m[1], -25) && Near(m[2], 0), "arrange: align min on the bounds");
    m = A::AlignMoves(spans, A::Edge::Max, A::Reference::Bounds);
    Check(Near(m[0], 14) && Near(m[1], 0) && Near(m[2], 19), "arrange: align max on the bounds");
    m = A::AlignMoves(spans, A::Edge::Centre, A::Reference::First);
    Check(Near(m[0], 0) && Near(m[1], -17) && Near(m[2], 5), "arrange: align centres on the first");
    A::Span s = A::SpanAlong({{1, 2, 3}, {-4, 0, 9}, {2, -1, 0}}, {0, 0, 1});
    Check(Near(s.lo, 0) && Near(s.hi, 9), "arrange: a span along an axis");
}
SELFTEST(Early, TestArrangeAlign);

void TestArrangeDistribute()
{
    // (given out of order: sorted by centre - 0..10, 100..110 and 30..60 in the middle)
    std::vector<A::Span> spans = {{0, 10}, {100, 110}, {30, 60}};
    std::vector<float> m = A::DistributeMoves(spans, false);
    Check(Near(m[0], 0) && Near(m[1], 0) && Near(m[2], 10), "arrange: distribute centres evenly (the ends stay)");
    m = A::DistributeMoves(spans, true);
    // (sizes 10 + 30 + 10 in 110: gaps of 30 - the middle starts at 40)
    Check(Near(m[0], 0) && Near(m[1], 0) && Near(m[2], 10), "arrange: distribute with equal gaps");
    std::vector<A::Span> four = {{0, 10}, {12, 14}, {15, 45}, {90, 100}};
    m = A::DistributeMoves(four, true);
    // (sizes 10 + 2 + 30 + 10 = 52 in 100: gaps 16 - 26..28, 44..74)
    Check(Near(m[1], 14) && Near(m[2], 29) && Near(m[0], 0) && Near(m[3], 0), "arrange: equal gaps between different sizes");
    m = A::DistributeMoves({{0, 1}, {5, 6}}, false);
    Check(Near(m[0], 0) && Near(m[1], 0), "arrange: fewer than three pieces don't distribute");
}
SELFTEST(Early, TestArrangeDistribute);

void TestArrangeEdgeSnap()
{
    A::Aabb moving = {{0, 0, 0}, {10, 10, 10}};
    std::vector<A::Aabb> others = {{{14, 0, 0}, {30, 10, 10}}};
    std::vector<A::Contact> contacts;
    V3 d = A::EdgeSnap(moving, {1, 0, 0}, others, 6.0f, 5, &contacts);
    Check(Near(d, {4, 0, 0}) && contacts.size() == 1 && contacts[0].axis == 0 && Near(contacts[0].at, 14),
          "arrange: edge snap - its max face flush with a neighbour's min");
    d = A::EdgeSnap(moving, {-6, 0, 0}, others, 6.0f, 5, nullptr);
    Check(Near(d, {-6, 0, 0}), "arrange: edge snap - nothing within reach, the move as it was");
    std::vector<A::Aabb> above = {{{14, 40, 0}, {30, 50, 10}}};
    d = A::EdgeSnap(moving, {1, 0, 0}, above, 6.0f, 5, nullptr);
    Check(Near(d, {1, 0, 0}), "arrange: edge snap - a neighbour at another height isn't side by side");
    d = A::EdgeSnap(moving, {1, 0, 0}, others, 6.0f, 4, nullptr);
    Check(Near(d, {1, 0, 0}), "arrange: edge snap - only on the axes asked");
    std::vector<A::Aabb> under = {{{0, -20, 0}, {10, -3, 10}}};
    d = A::EdgeSnap(moving, {0, 0, 0}, under, 6.0f, 2, nullptr);
    Check(Near(d, {0, -3, 0}), "arrange: edge snap - stacked on a neighbour's top");
    std::vector<A::Aabb> beside = {{{12, 0, 0}, {30, 13, 10}}};
    d = A::EdgeSnap(moving, {0, 0, 0}, beside, 6.0f, 2, nullptr);
    Check(Near(d, {0, 3, 0}), "arrange: edge snap - its top level with a neighbour's beside it");
    Check(Near(A::SnapReach(100), 6) && Near(A::SnapReach(1500), 15) && Near(A::SnapReach(50000), 24),
          "arrange: the snap's reach grows with the distance (6..24)");
}
SELFTEST(Early, TestArrangeEdgeSnap);

void TestArrangeReadout()
{
    A::Aabb box = {{0, 0, 0}, {10, 10, 10}};
    V3 nearest;
    Check(Near(A::BoxSegmentDistance(box, {-5, 5, 5}, {15, 5, 5}), 0.0f), "arrange: a segment through the box: 0");
    float d = A::BoxSegmentDistance(box, {20, 0, -10}, {20, 0, 30}, &nearest);
    Check(Near(d, 10.0f, 1e-2f) && Near(nearest.x, 20) && nearest.z >= -1e-2f && nearest.z <= 10.01f,
          "arrange: the distance from a box to a rail beside it");
    A::Readout r;
    r.size = {120, 40, 300};
    r.moved = 125.4f;
    r.haveGround = true;
    r.above = 32;
    r.haveRail = true;
    r.rail = 85;
    std::vector<std::string> lines = A::ReadoutLines(r);
    Check(lines.size() == 4 && lines[0] == "size 120 x 40 x 300" && lines[1] == "moved 125" &&
              lines[2] == "above ground 32" && lines[3] == "rail 85",
          "arrange: the readout's lines");
    r.ledge = true;
    r.moved = 0.0f;
    r.haveGround = false;
    lines = A::ReadoutLines(r);
    Check(lines.size() == 2 && lines[1] == "ledge 85", "arrange: the readout leaves out what isn't known");
}
SELFTEST(Early, TestArrangeReadout);

// A box's 12 triangles (two a face), min..max.
void AddBox(std::vector<V3>* t, V3 a, V3 b)
{
    V3 c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = {(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z};
    static const int kFaces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (const auto& f : kFaces) {
        t->insert(t->end(), {c[f[0]], c[f[1]], c[f[2]]});
        t->insert(t->end(), {c[f[0]], c[f[2]], c[f[3]]});
    }
}

void TestArrangeTopEdges()
{
    std::vector<V3> box;
    AddBox(&box, {0, 0, 0}, {100, 20, 40});
    std::vector<A::Run> runs = A::TopEdges(box, 0.5f, 1.0f);
    bool allTop = !runs.empty();
    float total = 0.0f;
    for (const A::Run& r : runs) {
        total += r.length;
        for (V3 p : r.points)
            allTop = allTop && Near(p.y, 20);
    }
    Check(runs.size() == 4 && allTop && Near(total, 280), "arrange: a box's top outline - its four top edges, not its bottom's");
    Check(Near(runs[0].length, 100) && runs[0].points.size() == 2, "arrange: the longest edge first, one segment");
    // A top split in two along x: the two halves' edges along x joined into one straight run.
    std::vector<V3> split;
    V3 c00 = {0, 20, 0}, c10 = {50, 20, 0}, c20 = {100, 20, 0}, c01 = {0, 20, 40}, c11 = {50, 20, 40}, c21 = {100, 20, 40};
    split.insert(split.end(), {c00, c10, c11, c00, c11, c01, c10, c20, c21, c10, c21, c11});
    runs = A::TopEdges(split, 0.5f, 1.0f);
    bool merged = false;
    for (const A::Run& r : runs)
        merged = merged || (Near(r.length, 100) && r.points.size() == 2);
    Check(runs.size() == 4 && merged, "arrange: collinear edges of a split top are one run (an open plane: its edges)");
    // A step: a top with a wall rising behind it - that edge is an inside corner, not a lip.
    std::vector<V3> step;
    step.insert(step.end(), {c00, c20, c21, c00, c21, c01});
    V3 w0 = {0, 60, 40}, w1 = {100, 60, 40};
    step.insert(step.end(), {c01, c21, w1, c01, w1, w0});
    runs = A::TopEdges(step, 0.5f, 1.0f);
    bool back = false;
    for (const A::Run& r : runs) {
        for (V3 p : r.points)
            back = back || (Near(p.z, 40) && Near(p.y, 20) && r.points.size() == 2 && Near(r.length, 100));
    }
    Check(runs.size() == 3 && !back, "arrange: an edge with a wall rising from it isn't offered");
    Check(A::TopEdges(box, 0.5f, 150.0f).empty(), "arrange: runs shorter than the least length left out");
    auto project = [](V3 p, float* x, float* y) {
        *x = p.x;
        *y = p.z;
        return true;
    };
    runs = A::TopEdges(box, 0.5f, 1.0f);
    int at = A::RunAt(runs, project, 50.0f, 3.0f, 8.0f);
    Check(at >= 0 && Near(runs[at].points.front().z, 0) && Near(runs[at].points.back().z, 0) &&
              A::RunAt(runs, project, 50.0f, 20.0f, 8.0f) < 0,
          "arrange: the edge under the cursor (none away from them)");
}
SELFTEST(Early, TestArrangeTopEdges);

void TestAdjustNumbers()
{
    Adj::Action a;
    a.kind = Adj::Kind::Duplicate;
    a.count = 500;
    a.offset = {37, 0, -12};
    a.grid = true;
    a.degrees = 100;
    a.quarters = true;
    Adj::Action n = Adj::Normalised(a, 25.0f);
    Check(n.count == Adj::kMaxCopies && Near(n.offset, {25, 0, -0.0f}) && Near(n.degrees, 90),
          "adjust: copies capped, offset on the grid, scenery turns in quarters");
    a.count = 0;
    a.grid = false;
    Check(Adj::Normalised(a, 25.0f).count == 1 && Near(Adj::Normalised(a, 25.0f).offset, {37, 0, -12}),
          "adjust: at least one copy; no grid - the offset as typed");
    Adj::Action s;
    s.kind = Adj::Kind::Scale;
    s.factor = {3, 0.01f, 99};
    s.uniform = true;
    Check(Near(Adj::Normalised(s, 0).factor, {3, 3, 3}), "adjust: a uniform factor - all three the first");
    s.uniform = false;
    Check(Near(Adj::Normalised(s, 0).factor, {3, 0.05f, 20}), "adjust: factors kept within 0.05..20");
    a.quarters = true;
    a.degrees = -90;
    LE::Transform::RepeatStep step = Adj::StepOf(Adj::Normalised(a, 0));
    Check(step.quarters == 3 && step.turn == 0.0f, "adjust: scenery's step in quarter turns (-90: three)");
    a.quarters = false;
    a.degrees = 30;
    step = Adj::StepOf(a);
    Check(step.quarters == 0 && Near(step.turn, 30.0f * 3.14159265f / 180.0f), "adjust: an object's step in radians");
    Adj::Action b = a;
    Check(Adj::Same(a, b), "adjust: the same numbers");
    b.count = 2;
    Check(!Adj::Same(a, b), "adjust: a changed count differs");
    Check(std::string(Adj::Title(Adj::Kind::Distribute)) == "Distribute", "adjust: titles");
}
SELFTEST(Early, TestAdjustNumbers);

void TestLinksGroups()
{
    Links::State state;
    Links::Join(&state, false, 0x100, {{0x201, 0}, {0x202, 0}});
    Check(state.size() == 1 && state[0].members.size() == 2, "links: copies joined to their source");
    Links::Join(&state, false, 0x202, {{0x203, 0}});
    bool isSource = true;
    Check(state.size() == 1 && state[0].members.size() == 3 && Links::GroupOf(state, 0x203, &isSource) == 0 && !isSource,
          "links: a copy of a linked copy joins its source's group");
    Links::Join(&state, false, 0x100, {{0x100, 0}, {0x201, 0}});
    Check(state[0].members.size() == 3, "links: the source and copies already linked aren't added again");
    Check(Links::Unlink(&state, 0x202) && state[0].members.size() == 2 && Links::GroupOf(state, 0x202) < 0,
          "links: unlink a copy");
    Check(Links::Unlink(&state, 0x100) && state.empty(), "links: unlink the source - the group dissolved");
    Check(!Links::Unlink(&state, 0x999), "links: unlinking what isn't linked");
    Links::Join(&state, true, 0x300, {{0x301, 5}});
    Check(state[0].scenery && state[0].members[0].quarters == 1, "links: a scenery copy's quarter turns kept in 0..3");
    Links::Unlink(&state, 0x301);
    Check(state.empty(), "links: a group without copies goes");
}
SELFTEST(Early, TestLinksGroups);

void TestLinksRecords()
{
    Links::Record r;
    r.scenery = true;
    r.source = {0x1A2B3C4D, -1, 0};
    r.members = {{0, 0, 1}, {0xDEADBEEF, -1, 0}, {0, 3, 2}};
    r.members[0].clone = 0;
    std::string line = Links::WriteRecord(r);
    Check(line == "link sector 1A2B3C4D c0@1,DEADBEEF,c3@2", "links: a sector group's record");
    std::istringstream in(line.substr(5));
    Links::Record back;
    Check(Links::ReadRecord(in, &back) && back.scenery && back.source.id == 0x1A2B3C4D && back.members.size() == 3 &&
              back.members[0].clone == 0 && back.members[0].quarters == 1 && back.members[1].id == 0xDEADBEEF &&
              back.members[2].clone == 3 && back.members[2].quarters == 2,
          "links: the record read back");
    Links::Record o;
    o.source = {0x11111111, -1, 0};
    o.members = {{0x22222222, -1, 0}};
    std::istringstream in2(Links::WriteRecord(o).substr(5));
    Check(Links::WriteRecord(o) == "link object 11111111 22222222" && Links::ReadRecord(in2, &back) && !back.scenery,
          "links: an object group's record");
    const char* bad[] = {"object c1 22222222", "object 11111111 22222222@1", "sector 1A2B3C4D", "thing 11111111 22222222",
                         "sector 1A2B3C4D c0@7", "sector 1A2B3C4D 123"};
    bool refused = true;
    for (const char* text : bad) {
        std::istringstream b(text);
        refused = refused && !Links::ReadRecord(b, &back);
    }
    Check(refused, "links: malformed records refused (clones only for sectors, quarters 0..3, a member needed)");
}
SELFTEST(Early, TestLinksRecords);

} // namespace
