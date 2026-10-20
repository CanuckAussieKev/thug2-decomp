// Self-tests for the level editor's chains (src/game/front/level_editor_chains.*, not original - phase 4 of the editor
// plan): the chain topology edits (extend, insert, delete with re-linking, link, unlink, what's connected), the layouts'
// point / links / drop / terrain records (round trip, malformed ones refused, older files still read, the chain points
// kept out of `node` records), the edge snapping of drawn points, the chain points' node fields and the grind terrains.

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_create.h"
#include "hook/selftest.h"

namespace {

namespace Chains = Front::LevelEditor::Chains;
namespace Layout = Front::LevelEditor::Layout;
namespace NC = Front::LevelEditor::NodeCreate;
using Front::LevelEditor::V3;

using Links = std::vector<uint32_t>;

// --- topology -----------------------------------------------------------------------------------------------------

void TestEditorChainsGraph()
{
    // A rail 1 -> 2 -> 3.
    Chains::Graph rail = {{1, {2}}, {2, {3}}, {3, {}}};
    Check(Chains::IsFirst(rail, 1) && !Chains::IsFirst(rail, 2) && Chains::IsLast(rail, 3) && !Chains::IsLast(rail, 1),
          "editor chains: the ends");
    Check(Chains::Predecessors(rail, 3) == Links{2} && Chains::Predecessors(rail, 1).empty(),
          "editor chains: the points linking to one");

    // Extend: from the last point appended, from the first prepended, not from the middle.
    Chains::Graph g = rail;
    Check(Chains::Extend(&g, 3, 4) && g[3] == Links{4} && g[4].empty(), "editor chains: extend from the last point");
    Check(Chains::Extend(&g, 1, 5) && g[5] == Links{1} && g[1] == Links{2}, "editor chains: extend from the first point");
    Check(!Chains::Extend(&g, 2, 6) && !g.count(6), "editor chains: no extending from the middle");
    Check(!Chains::Extend(&g, 4, 1), "editor chains: extend refuses a point that's there already");

    // Insert after: between a point and its next.
    g = rail;
    Check(Chains::InsertAfter(&g, 1, 9) && g[1] == Links{9} && g[9] == Links{2} && g[2] == Links{3},
          "editor chains: a point inserted after another");
    Check(!Chains::InsertAfter(&g, 3, 10) && !g.count(10), "editor chains: nothing inserted after the last point");

    // Delete: the middle one's neighbours linked, an end's link dropped.
    g = rail;
    Chains::Remove(&g, 2);
    Check(g[1] == Links{3} && g[2].empty() && Chains::Predecessors(g, 2).empty(),
          "editor chains: deleting the middle links its neighbours");
    g = rail;
    Chains::Remove(&g, 3);
    Check(g[2].empty() && g[1] == Links{2}, "editor chains: deleting the last point ends the rail at the one before");
    g = rail;
    Chains::Remove(&g, 1);
    Check(g[2] == Links{3} && g[1].empty(), "editor chains: deleting the first point");
    // Two points linking to it (a fork): both on to its next; one that links to its next already doesn't twice.
    Chains::Graph fork = {{1, {3}}, {2, {3}}, {3, {4}}, {4, {}}, {5, {3, 4}}};
    Chains::Remove(&fork, 3);
    Check(fork[1] == Links{4} && fork[2] == Links{4} && fork[5] == Links{4}, "editor chains: a fork re-linked once");
    // A closed loop of two: no point links to itself.
    Chains::Graph loop = {{1, {2}}, {2, {1}}};
    Chains::Remove(&loop, 2);
    Check(loop[1].empty(), "editor chains: no self-link when a loop closes up");

    // Link: a free end's next; else the other's; refused when neither has a free next.
    Chains::Graph two = {{1, {2}}, {2, {}}, {7, {8}}, {8, {}}};
    Check(Chains::Link(&two, 2, 7) && two[2] == Links{7}, "editor chains: the last point linked on");
    two = {{1, {2}}, {2, {}}, {7, {8}}, {8, {}}};
    Check(Chains::Link(&two, 1, 8) && two[8] == Links{1} && two[1] == Links{2},
          "editor chains: a first point linked from the other's last");
    two = {{1, {2}}, {2, {3}}, {3, {}}, {7, {8}}, {8, {9}}, {9, {}}};
    Check(!Chains::Link(&two, 2, 8) && two[2] == Links{3} && two[8] == Links{9},
          "editor chains: two middle points aren't linked");
    Check(!Chains::Link(&two, 3, 3), "editor chains: a point isn't linked to itself");

    // Unlink: the chain split there.
    g = rail;
    Check(Chains::Unlink(&g, 1) && g[1].empty() && Chains::IsFirst(g, 2), "editor chains: unlinked from its next");
    Check(!Chains::Unlink(&g, 3), "editor chains: the last point has nothing to unlink");

    // What's connected: either way, the point first.
    Chains::Graph paths = {{1, {2}}, {2, {3}}, {3, {}}, {7, {8}}, {8, {}}};
    Links connected = Chains::Connected(paths, 2);
    Check(connected.size() == 3 && connected.front() == 2, "editor chains: a path's points, either way");
    Check(Chains::Connected(paths, 8).size() == 2, "editor chains: another path apart");
}

// --- records ------------------------------------------------------------------------------------------------------

Chains::State Sample()
{
    Chains::State s;
    Chains::Point a;
    a.name = "ed_rail01";
    a.cls = "RailNode";
    a.pos = {-120.5f, 8.0f, 330.125f};
    a.like = 0x8E1A2B3C;
    a.links = {0x12345678};
    Chains::Point b;
    b.name = "ed_rail02";
    b.cls = "RailNode";
    b.pos = {1.0f / 3.0f, 2.0e-7f, -4096.75f};
    b.terrain = "TERRAIN_GRINDWOOD";
    Chains::Point c;
    c.name = "ed_path01";
    c.cls = "Waypoint";
    c.pos = {0.0f, 0.0f, 0.0f};
    c.links = {0xAAAAAAAA, 0x00000001};
    s.points = {a, b, c};
    s.links = {{0x00000010, {0xDEADBEEF}}, {0x0000ABCD, {}}};
    s.dropped = {0x00000002, 0x0BADF00D};
    s.terrains = {{0x00000003, "TERRAIN_GRINDCONC"}};
    return s;
}

bool ReadAll(const std::string& text, Chains::State* state)
{
    *state = {};
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string kind;
        if (!(in >> kind))
            continue;
        if (!Chains::IsRecord(kind) || !Chains::ReadRecord(kind, in, state))
            return false;
    }
    return true;
}

bool ReadsAs(const char* line)
{
    Chains::State state;
    return ReadAll(line, &state);
}

void TestEditorChainsRecords()
{
    Chains::State s = Sample();
    std::string text = Chains::WriteRecords(s);
    Chains::State back;
    Check(ReadAll(text, &back) && Chains::SameState(s, back), "editor chains: the records round trip exactly");
    Check(text.find("links 0000ABCD -") != std::string::npos, "editor chains: a node with no links writes -");
    Check(text.find("point ed_rail01 RailNode pos=") == 0, "editor chains: point records start with the name, class");
    Check(Chains::Empty(Chains::State{}) && !Chains::Empty(s) && Chains::WriteRecords(Chains::State{}).empty(),
          "editor chains: nothing changed, nothing written");

    // Unknown keys skipped; what's malformed refused.
    Check(ReadsAs("point ed_rail09 RailNode pos=1,2,3 colour=red"), "editor chains: unknown keys skipped");
    Check(!ReadsAs("point ed_rail09 RailNode like=00000001"), "editor chains: a point needs its pos");
    Check(!ReadsAs("point ed_rail09 RailNode pos=1,2,3 pos=1,2,3"), "editor chains: a repeated key refused");
    Check(!ReadsAs("point ed_rail09 LevelLight pos=1,2,3"), "editor chains: only rail / path classes");
    Check(!ReadsAs("point ed_rail09 RailNode pos=1,2"), "editor chains: a short position refused");
    Check(!ReadsAs("point ed_rail09 RailNode pos=1,2,3 links=0000001"), "editor chains: a link of 7 digits refused");
    Check(!ReadsAs("point ed_rail09 RailNode pos=1,2,3 terrain=TERRAIN-X"), "editor chains: a bad terrain name refused");
    Check(!ReadsAs("point ed rail RailNode pos=1,2,3"), "editor chains: a bad name refused");
    Check(!ReadsAs("point ed_rail09 RailNode pos=1,2,3\npoint ED_RAIL09 RailNode pos=4,5,6"),
          "editor chains: a point named twice refused");
    Check(!ReadsAs("links 00000010"), "editor chains: a links record needs its links (or -)");
    Check(!ReadsAs("links 00000010 -\nlinks 00000010 00000011"), "editor chains: a node's links twice refused");
    Check(!ReadsAs("drop XYZ"), "editor chains: a drop needs a checksum");
    Check(!ReadsAs("terrain 00000003"), "editor chains: a terrain record needs its terrain");
    Chains::State dropped;
    Check(ReadAll("drop 00000005\ndrop 00000001\ndrop 00000005", &dropped) && dropped.dropped == Links({1, 5}),
          "editor chains: drops sorted, a repeat once");
    Chains::State linked;
    Check(ReadAll("links 00000020 00000001\nlinks 00000010 -", &linked) && linked.links.size() == 2 &&
              linked.links[0].name == 0x10 && linked.links[0].links.empty() && linked.links[1].links == Links{1},
          "editor chains: links records sorted by node");
}

void TestEditorChainsLayout()
{
    // A layout with chains: the records after the others, read back the same; the header stays version 2.
    Layout::File file;
    file.scene = 0x4B7E41E2;
    file.hidden = {0x00000099};
    file.chains = Sample();
    std::string text = Layout::Write(file);
    Check(text.rfind("thug2-decomp layout 2\n", 0) == 0, "editor chains: layouts stay version 2");
    Layout::File back;
    int line = 0;
    Check(Layout::Read(text, &back, &line) && Chains::SameState(back.chains, file.chains) && back.hidden == file.hidden,
          "editor chains: a layout's chain records round trip");
    Check(text.find("node ed_rail") == std::string::npos, "editor chains: added chain points aren't node records");
    // An older layout (phase 3, no chain records) reads with no chains; a bad chain record refuses the file.
    Check(Layout::Read("thug2-decomp layout 2\nscene 4B7E41E2\nhide 00000099  # x\n", &back, &line) &&
              Chains::Empty(back.chains) && back.hidden.size() == 1,
          "editor chains: an older layout reads");
    Check(!Layout::Read("thug2-decomp layout 2\nscene 4B7E41E2\npoint ed_rail01 RailNode\n", &back, &line) && line == 3,
          "editor chains: a malformed chain record refuses the layout");
}

// --- drawing ------------------------------------------------------------------------------------------------------

void TestEditorChainsSnap()
{
    // A ledge: ground at 100 for x < 50, nothing (a drop) beyond.
    auto ground = [](V3 from, float* y) {
        if (from.x >= 50.0f)
            return false;
        *y = 100.0f;
        return true;
    };
    bool snapped = false;
    V3 at = Chains::SnapToEdge({40.0f, 100.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 32.0f, ground, &snapped);
    Check(snapped && at.x == 50.0f && at.y == 100.0f && at.z == 0.0f, "editor chains: a point snapped to the ledge's edge");
    at = Chains::SnapToEdge({0.0f, 100.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 32.0f, ground, &snapped);
    Check(!snapped && at.x == 0.0f, "editor chains: no edge within reach: where the ray hit");
    at = Chains::SnapToEdge({40.0f, 100.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 32.0f, ground, &snapped);
    Check(!snapped && at.x == 40.0f, "editor chains: a wall's hit isn't snapped");
    // A step down of less than 8 isn't an edge; more is.
    auto step = [](V3 from, float* y) {
        *y = from.z > 20.0f ? 94.0f : 100.0f;
        return true;
    };
    at = Chains::SnapToEdge({0.0f, 100.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 32.0f, step, &snapped);
    Check(!snapped, "editor chains: a small step isn't an edge");
    auto drop = [](V3 from, float* y) {
        *y = from.z > 20.0f ? 80.0f : 100.0f;
        return true;
    };
    at = Chains::SnapToEdge({0.0f, 100.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 32.0f, drop, &snapped);
    Check(snapped && at.z == 20.0f && at.x == 0.0f, "editor chains: a drop of more than 8 is an edge");
}

void TestEditorChainsNodes()
{
    // A drawn rail point's node: made at the start, a known terrain (the default, or its own); a waypoint has none.
    NC::Spec rail;
    rail.name = "ed_rail01";
    rail.cls = "RailNode";
    bool start = false;
    std::string terrain;
    for (const NC::Field& f : NC::Fields(rail)) {
        start = start || (f.type == NC::Field::kFlag && f.text == "CreatedAtStart");
        if (f.key == "TerrainType")
            terrain = f.text;
    }
    Check(start && terrain == "TERRAIN_GRINDMETAL", "editor chains: a new rail point: CreatedAtStart, a grind metal");
    rail.terrain = "TERRAIN_GRINDWOOD";
    terrain.clear();
    for (const NC::Field& f : NC::Fields(rail))
        if (f.key == "TerrainType")
            terrain = f.text;
    Check(terrain == "TERRAIN_GRINDWOOD", "editor chains: a rail point's own terrain");
    NC::Spec waypoint;
    waypoint.name = "ed_path01";
    waypoint.cls = "Waypoint";
    bool noTerrain = true;
    for (const NC::Field& f : NC::Fields(waypoint))
        noTerrain = noTerrain && f.key != "TerrainType";
    Check(noTerrain, "editor chains: a waypoint has no terrain");
    NC::Spec other = rail;
    other.links = {1};
    Check(!NC::SameSpec(rail, other), "editor chains: specs differ by their links");
    Check(Chains::IsChainClass("railnode") && Chains::IsChainClass("Waypoint") && !Chains::IsChainClass("ClimbingNode"),
          "editor chains: the classes drawn");

    const std::vector<std::string>& terrains = Chains::GrindTerrainNames();
    bool grind = terrains.size() == 21;
    for (const std::string& t : terrains)
        grind = grind && t.rfind("TERRAIN_GRIND", 0) == 0;
    Check(grind && terrains[6] == "TERRAIN_GRINDMETAL", "editor chains: the 21 grind terrains in terrain.qb's order");
}

} // namespace

SELFTEST(Early, TestEditorChainsGraph);
SELFTEST(Early, TestEditorChainsRecords);
SELFTEST(Early, TestEditorChainsLayout);
SELFTEST(Early, TestEditorChainsSnap);
SELFTEST(Early, TestEditorChainsNodes);
