// NOT ORIGINAL - the level editor's problem checker (ours). See level_editor_checks.h.

#include "game/front/level_editor_checks.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_set>

#include "game/core/crc.h"
#include "game/front/level_editor_budget.h"
#include "game/front/level_editor_faces.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_shared_coll.h"
#include "game/front/level_editor_triggers.h"
#include "game/front/level_editor_walls.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "game/sys/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Checks {

// --- the pure parts -------------------------------------------------------------------------------------------------

const char* SeverityName(Severity severity)
{
    switch (severity) {
    case Severity::Error: return "Error";
    case Severity::Warning: return "Warning";
    default: return "Info";
    }
}

const char* RuleName(Rule rule)
{
    switch (rule) {
    case Rule::InsideSolid: return "Inside solid";
    case Rule::BelowGround: return "Below the ground";
    case Rule::NoGround: return "Nothing below";
    case Rule::FloatingRail: return "Floating rail point";
    case Rule::LonelyRail: return "Rail of one point";
    case Rule::CollisionLeft: return "Collision left behind";
    case Rule::HiddenCollision: return "Hidden, collision on";
    case Rule::Limit: return "Limit";
    case Rule::TriggerIdle: return "Trigger never fires";
    case Rule::TriggerScript: return "Trigger script";
    case Rule::GapUnpaired: return "Gap unpaired";
    case Rule::OnlineParity: return "Online parity";
    case Rule::Multiplayer: return "Multiplayer";
    case Rule::LayoutRecord: return "Layout record";
    }
    return "?";
}

uint32_t IssueKey(Rule rule, uint32_t subject, const std::string& detail)
{
    // (FNV-1a over the rule, the subject's bytes and the detail)
    uint32_t hash = 2166136261u;
    auto mix = [&](uint8_t byte) { hash = (hash ^ byte) * 16777619u; };
    mix(static_cast<uint8_t>(rule));
    for (int i = 0; i < 4; ++i)
        mix(static_cast<uint8_t>(subject >> (8 * i)));
    for (char c : detail)
        mix(static_cast<uint8_t>(c));
    return hash;
}

void Sort(std::vector<Issue>* issues)
{
    std::stable_sort(issues->begin(), issues->end(), [](const Issue& a, const Issue& b) {
        if (a.severity != b.severity)
            return a.severity < b.severity;
        if (a.levelOwn != b.levelOwn)
            return !a.levelOwn;
        if (a.rule != b.rule)
            return a.rule < b.rule;
        return a.text < b.text;
    });
}

bool Behind(const Probe& probe) { return probe.in >= 0.0f && (probe.out < 0.0f || probe.in < probe.out); }

Place Judge(const Probe probes[6])
{
    for (int axis = 0; axis < 3; ++axis) {
        if (Behind(probes[2 * axis]) && Behind(probes[2 * axis + 1]))
            return Place::InsideSolid;
    }
    return Behind(probes[2]) ? Place::BelowGround : Place::Clear;
}

bool Floating(const Probe probes[6], float near)
{
    for (int i = 0; i < 6; ++i) {
        if ((probes[i].out >= 0.0f && probes[i].out <= near) || (probes[i].in >= 0.0f && probes[i].in <= near))
            return false;
    }
    return true;
}

std::vector<int> LonelyPoints(const std::vector<std::vector<int>>& links)
{
    std::vector<int> incoming(links.size(), 0);
    for (const std::vector<int>& out : links) {
        for (int to : out) {
            if (to >= 0 && to < static_cast<int>(links.size()))
                ++incoming[to];
        }
    }
    std::vector<int> lonely;
    for (size_t i = 0; i < links.size(); ++i) {
        bool linksOut = std::any_of(links[i].begin(), links[i].end(),
                                    [&](int to) { return to >= 0 && to < static_cast<int>(links.size()); });
        if (!linksOut && incoming[i] == 0)
            lonely.push_back(static_cast<int>(i));
    }
    return lonely;
}

namespace {

const char* const kTeamNames[4] = {"Red", "Blue", "Green", "Yellow"};

} // namespace

std::vector<MpProblem> MultiplayerProblems(const MpCounts& counts)
{
    std::vector<MpProblem> out;
    if (counts.player1 == 0)
        out.push_back({Severity::Error, "No Player1 restart", -1});
    if (counts.multiplayer == 0)
        out.push_back({Severity::Warning, "No Multiplayer restart", -1});
    else if (counts.multiplayer == 1)
        out.push_back({Severity::Info, "Only one Multiplayer restart", -1});
    if (counts.horse == 0)
        out.push_back({Severity::Warning, "No HORSE restart", -1});
    if (counts.crowns == 0)
        out.push_back({Severity::Warning, "King of the Hill: no crowns", -1});
    bool anyTeam = false;
    for (const TeamNodes& t : counts.teams)
        anyTeam = anyTeam || t.flag || t.base || t.restart;
    if (!anyTeam) {
        out.push_back({Severity::Warning, "No Capture the Flag flags, bases or team restarts",
                       -1});
        return out;
    }
    for (int team = 0; team < 4; ++team) {
        const TeamNodes& t = counts.teams[team];
        bool any = t.flag || t.base || t.restart;
        // (Red and Blue always play; Green and Yellow only with 3 / 4 teams - complete or not there at all)
        if (team >= 2 && !any)
            continue;
        if (t.flag && t.base && t.restart)
            continue;
        std::string missing;
        auto add = [&](bool have, const std::string& what) {
            if (!have)
                missing += (missing.empty() ? "" : ", ") + what;
        };
        std::string colour = kTeamNames[team];
        add(t.flag, "its flag (TRG_CTF_" + colour + ")");
        add(t.base, "its base (TRG_CTF_" + colour + "_Base)");
        add(t.restart, "its team restart (TRG_CTF_Restart_" + colour + ")");
        out.push_back({Severity::Warning, "Capture the Flag, team " + colour + ": missing " + missing, team});
    }
    return out;
}

std::string RecordKey(const std::string& line)
{
    std::string text = line.substr(0, line.find('#'));
    std::istringstream in(text);
    std::string word, key;
    int words = 0, want = 2;
    while (words < want && in >> word) {
        if (words == 0) {
            if (word == "lock" || word == "order")
                want = 3;
            else if (word == "level")
                want = 1;
        }
        key += (words ? " " : "") + word;
        ++words;
    }
    return key;
}

std::vector<std::string> CarriedLines(const std::vector<std::string>& unresolved, const std::string& captured)
{
    std::set<std::string> keys;
    std::istringstream in(captured);
    std::string line;
    while (std::getline(in, line)) {
        std::string key = RecordKey(line);
        if (!key.empty())
            keys.insert(key);
    }
    std::vector<std::string> out;
    for (const std::string& u : unresolved) {
        if (keys.count(RecordKey(u)) || std::find(out.begin(), out.end(), u) != out.end())
            continue;
        out.push_back(u);
    }
    return out;
}

// --- the game side ------------------------------------------------------------------------------------------------

bool Cast(V3 from, V3 to, V3* hit, V3* normal, uint32_t* sector)
{
    // (a CFeeler: constructor 0x00417760; 0x00417710 casts it - thiscall, true on a hit; the hit's normal at +0x24, its
    // point at +0x34, the collision object at +0x54 and its checksum at +0x58 - as the editor's other modules read it)
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<Nx::CFeeler*>(feeler);
    ray->Construct();
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->GetCollisionAlong(&start, &end, false))
        return false;
    const float* n = reinterpret_cast<const float*>(feeler + 0x24);
    const float* p = reinterpret_cast<const float*>(feeler + 0x34);
    *hit = {p[0], p[1], p[2]};
    if (normal)
        *normal = {n[0], n[1], n[2]};
    if (sector)
        *sector = *reinterpret_cast<void* const*>(feeler + 0x54) ? *reinterpret_cast<const uint32_t*>(feeler + 0x58) : 0;
    return true;
}

namespace {

float Distance(V3 a, V3 b) { return std::sqrt(Dot(a - b, a - b)); }

const V3 kDirections[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

} // namespace

void ProbeEach(V3 point, const float reach[6], Probe probes[6])
{
    for (int i = 0; i < 6; ++i) {
        V3 far = point + kDirections[i] * reach[i];
        V3 hit;
        probes[i] = {};
        if (Cast(point, far, &hit))
            probes[i].out = Distance(hit, point);
        // Coming back in: the face facing away nearest to the point - the last met on the way in (cast again from just
        // past each hit; a few at most).
        V3 from = far;
        for (int step = 0; step < 6 && Cast(from, point, &hit); ++step) {
            probes[i].in = Distance(hit, point);
            V3 on = hit + (point - far) * (0.5f / reach[i]); // (half a unit on, towards the point)
            if (Distance(on, point) < 0.5f)
                break;
            from = on;
        }
    }
}

void ProbeAround(V3 point, float reach, Probe probes[6])
{
    const float reaches[6] = {reach, reach, reach, reach, reach, reach};
    ProbeEach(point, reaches, probes);
}

namespace {

// --- the state ----------------------------------------------------------------------------------------------------

std::vector<Issue> g_issues;         // the last check's
std::vector<Issue> g_layoutIssues;   // the loaded layout's unresolved records
std::unordered_set<uint32_t> g_ignored;
std::unordered_map<uint32_t, uint8_t> g_marks;
int g_changes = -1;
uint32_t g_generation = 0;
int g_loads = -1;
double g_due = -1.0;      // (seconds: when to check again; -1 none due)
bool g_checkedOnce = false;
double g_lastSeconds = 0.0; // (how long the last check took)
bool g_open = false;
int g_tab = kProblemsTab;
bool g_tabPending = false;
bool g_showOwn = false, g_showIgnored = false;
std::vector<std::function<void()>> g_queue;

double Seconds() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }

uint32_t Crc(const std::string& text) { return Crc::GenerateCRCFromString(text.c_str()); }

std::string Name(uint32_t checksum) { return Nodes::Label(checksum); }

bool Marked(uint32_t name, uint8_t bits)
{
    auto it = g_marks.find(name);
    return it != g_marks.end() && (it->second & bits);
}

// Whether the editor changed scenery near a point (moved pieces, copies): then a problem there isn't the level's own.
bool SceneryChangedNear(V3 point, float margin)
{
    std::vector<uint32_t> pieces;
    for (const Scenery::SectorState& s : Scenery::ChangedSectors())
        pieces.push_back(s.checksum);
    for (const Scenery::Clone& c : Scenery::Clones())
        pieces.push_back(c.checksum);
    for (uint32_t piece : pieces) {
        OrientedBox box;
        if (!Scenery::Box(piece, &box))
            continue;
        V3 min, max;
        BoundsOf(box, &min, &max);
        if (point.x >= min.x - margin && point.x <= max.x + margin && point.y >= min.y - margin &&
            point.y <= max.y + margin && point.z >= min.z - margin && point.z <= max.z + margin)
            return true;
    }
    return false;
}

void Add(std::vector<Issue>* out, Issue issue, const std::string& detail = "")
{
    issue.key = IssueKey(issue.rule, issue.node ? issue.node : issue.sector, detail.empty() ? issue.text : detail);
    out->push_back(std::move(issue));
}

// --- the rules ----------------------------------------------------------------------------------------------------

// The placement probes' verdict for a point, kept while neither it nor the level's collision changes (`world`).
struct Placed {
    V3 point = {};
    uint32_t world = 0;
    Place place = Place::Clear;
    float upIn = -1.0f; // (the surface above, from the point - BelowGround's Fix)
    bool ground = true; // (a face within kGroundReach below)
};
std::unordered_map<uint32_t, Placed> g_placed;

// A number that changes when the static collision the probes meet may have: scenery moved / hidden / copied, walls
// switched or moved, faces taken along or their flags edited.
uint32_t WorldSignature()
{
    uint32_t hash = 2166136261u;
    auto mix = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ bytes[i]) * 16777619u;
    };
    for (const Scenery::SectorState& s : Scenery::ChangedSectors()) {
        mix(&s.checksum, sizeof(s.checksum));
        mix(&s.placement.quarters, sizeof(s.placement.quarters));
        mix(&s.placement.offset, sizeof(s.placement.offset));
        mix(&s.placement.scale, sizeof(s.placement.scale));
        mix(&s.hidden, sizeof(s.hidden));
    }
    for (const Walls::WallState& w : Walls::Changed()) {
        mix(&w.checksum, sizeof(w.checksum));
        mix(&w.placement.quarters, sizeof(w.placement.quarters));
        mix(&w.placement.offset, sizeof(w.placement.offset));
        mix(&w.off, sizeof(w.off));
    }
    size_t clones = Scenery::Clones().size(), faces = Faces::Changed().size();
    uint32_t shared = SharedCollision::Generation();
    mix(&clones, sizeof(clones));
    mix(&faces, sizeof(faces));
    mix(&shared, sizeof(shared));
    return hash | 1u; // (never 0: 0 is "not probed")
}

const Placed& PlacedAt(uint32_t name, V3 point, uint32_t world)
{
    Placed& placed = g_placed[name];
    if (placed.world == world && placed.point.x == point.x && placed.point.y == point.y && placed.point.z == point.z)
        return placed;
    // (the sides only kSideReach: a wall's thickness - farther faces belong to other things; up and down kReach)
    const float reaches[6] = {kSideReach, kSideReach, kReach, kReach, kSideReach, kSideReach};
    Probe probes[6];
    ProbeEach(point, reaches, probes);
    placed.point = point;
    placed.world = world;
    placed.place = Judge(probes);
    placed.upIn = probes[2].in;
    V3 hit;
    placed.ground = probes[3].out >= 0.0f || Cast(point, point - V3{0.0f, kGroundReach, 0.0f}, &hit);
    return placed;
}

void CheckPlacement(std::vector<Issue>* out, uint32_t world)
{
    for (const Nodes::Node& n : Nodes::All()) {
        if (n.hidden || !n.hasPos)
            continue;
        if (n.family != Nodes::Family::Spawn && n.family != Nodes::Family::Crown && n.family != Nodes::Family::Flag)
            continue;
        V3 point = n.pos + V3{0.0f, kLift, 0.0f};
        const Placed& placed = PlacedAt(n.name, point, world);
        Place place = placed.place;
        bool own = !Marked(n.name, Ui::kMarkMoved | Ui::kMarkAdded) && !n.added && !SceneryChangedNear(n.pos, 300.0f);
        std::string what = std::string(Nodes::FamilyName(n.family));
        what = what.substr(0, what.size() - 1); // ("Spawns" -> "Spawn")
        Issue issue;
        issue.node = n.name;
        issue.hasPos = true;
        issue.pos = n.pos;
        issue.levelOwn = own;
        if (place == Place::InsideSolid) {
            issue.rule = Rule::InsideSolid;
            issue.severity = Severity::Error;
            issue.text = what + " " + Name(n.name) + " is inside solid collision";
            Add(out, issue);
        } else if (place == Place::BelowGround) {
            issue.rule = Rule::BelowGround;
            issue.severity = Severity::Error;
            issue.text = what + " " + Name(n.name) + " is under a surface (" +
                         std::to_string(static_cast<int>(placed.upIn - kLift)) + " units below the surface)";
            issue.fixable = NodeEdit::Movable(n);
            issue.fixPos = point + V3{0.0f, placed.upIn, 0.0f};
            Add(out, issue, "below"); // (the key without the depth: Ignore holds while it's moved about)
        } else {
            if (!placed.ground) {
                issue.rule = Rule::NoGround;
                issue.severity = Severity::Warning;
                issue.text = what + " " + Name(n.name) + ": nothing below it within " +
                             std::to_string(static_cast<int>(kGroundReach)) + " units";
                Add(out, issue);
            }
        }
    }
}

void CheckRails(std::vector<Issue>* out)
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    std::vector<int> rails;
    std::vector<int> index(nodes.size(), -1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].family == Nodes::Family::Rail && !nodes[i].hidden) {
            index[i] = static_cast<int>(rails.size());
            rails.push_back(static_cast<int>(i));
        }
    }
    std::vector<std::vector<int>> links(rails.size());
    for (size_t r = 0; r < rails.size(); ++r) {
        for (int to : nodes[rails[r]].links) {
            if (to >= 0 && to < static_cast<int>(nodes.size()) && index[to] >= 0)
                links[r].push_back(index[to]);
        }
    }
    for (int r : LonelyPoints(links)) {
        const Nodes::Node& n = nodes[rails[r]];
        Issue issue;
        issue.rule = Rule::LonelyRail;
        issue.severity = Severity::Warning;
        issue.node = n.name;
        issue.hasPos = n.hasPos;
        issue.pos = n.pos;
        issue.levelOwn = !n.added && !Marked(n.name, Ui::kMarkAdded);
        issue.text = "Rail point " + Name(n.name) + " is not linked";
        Add(out, issue);
    }
    // Floating: the rail points the editor moved or added.
    for (int i : rails) {
        const Nodes::Node& n = nodes[i];
        if (!n.added && !Marked(n.name, Ui::kMarkMoved | Ui::kMarkAdded))
            continue;
        V3 pos = n.pos;
        Scenery::RailPoint(n.name, &pos);
        Probe probes[6];
        ProbeAround(pos, kRailNear, probes);
        if (!Floating(probes, kRailNear))
            continue;
        Issue issue;
        issue.rule = Rule::FloatingRail;
        issue.severity = Severity::Warning;
        issue.node = n.name;
        issue.hasPos = true;
        issue.pos = pos;
        issue.text = "Rail point " + Name(n.name) + " has no surface within " +
                     std::to_string(static_cast<int>(kRailNear)) + " units";
        Add(out, issue);
    }
}

void CheckCollision(std::vector<Issue>* out)
{
    for (const Scenery::SectorState& s : Scenery::ChangedSectors()) {
        uint8_t* sector = Scenery::SectorOf(s.checksum);
        if (!sector)
            continue;
        const SharedCollision::Companion* companion = SharedCollision::CompanionOf(s.checksum);
        Issue issue;
        issue.sector = s.checksum;
        OrientedBox box;
        if (Scenery::Box(s.checksum, &box)) {
            V3 min, max;
            BoundsOf(box, &min, &max);
            issue.hasPos = true;
            issue.pos = (min + max) * 0.5f;
        }
        if (s.hidden) {
            if (companion && companion->Active()) {
                issue.rule = Rule::HiddenCollision;
                issue.severity = Severity::Error;
                issue.text = "Scenery " + Name(s.checksum) + " is hidden but its collision is still on";
                issue.fixable = true;
                Add(out, issue);
            }
            continue;
        }
        bool moved = s.placement.quarters != 0 || !IsUnitScale(s.placement.scale) || s.placement.offset.x != 0.0f ||
                     s.placement.offset.y != 0.0f || s.placement.offset.z != 0.0f;
        bool own = *reinterpret_cast<uint8_t**>(sector + 0x18) != nullptr; // (its collision object - CSector +0x18)
        if (moved && !own && !companion && Scenery::SectorGeometry(s.checksum)) {
            issue.rule = Rule::CollisionLeft;
            issue.severity = Severity::Info;
            issue.text = "Scenery " + Name(s.checksum) + " was moved but its collision may not have moved";
            Add(out, issue);
        }
    }
}

void CheckLimits(std::vector<Issue>* out)
{
    for (const Budget::Meter& m : Budget::Meters()) {
        if (m.limit <= 0.0 || m.bytes)
            continue;
        Severity severity;
        if (m.used > m.limit)
            severity = Severity::Error;
        else if (m.used >= 0.9 * m.limit)
            severity = Severity::Warning;
        else
            continue;
        double before = m.used - m.added;
        Issue issue;
        issue.rule = Rule::Limit;
        issue.severity = severity;
        issue.levelOwn = severity == Severity::Error ? before > m.limit : before >= 0.9 * m.limit;
        issue.text = std::string(m.name) + ": " + Budget::UsedText(m) +
                     (severity == Severity::Error ? " - over the engine's limit (" : " - near the engine's limit (") +
                     m.where + ")";
        Add(out, issue, m.name);
    }
}

void CheckTriggers(std::vector<Issue>* out, uint32_t world)
{
    static const uint32_t kTriggerScript = Crc("TriggerScript"), kProximNode = Crc("ProximNode");
    // The editor's trigger volumes.
    const std::vector<Triggers::Added>& added = Triggers::AddedTriggers();
    std::vector<Triggers::Spec> specs;
    for (const Triggers::Added& a : added)
        specs.push_back(a.spec);
    const std::vector<Triggers::LevelTrigger>& level = Triggers::LevelTriggers();
    for (size_t i = 0; i < added.size(); ++i) {
        const Triggers::Added& a = added[i];
        Issue issue;
        issue.node = Triggers::ChecksumOf(a.spec.name);
        issue.hasPos = a.alive;
        issue.pos = a.pos;
        if (a.problem) {
            issue.rule = Rule::TriggerIdle;
            issue.severity = Severity::Warning;
            issue.text = std::string(Triggers::KindName(a.spec.kind)) + " " + a.spec.name + " does nothing yet: " +
                         a.problem;
            Add(out, issue);
        }
        if (a.alive) {
            Place place = PlacedAt(issue.node, a.pos, world).place;
            if (place != Place::Clear) {
                issue.rule = Rule::TriggerIdle;
                issue.severity = Severity::Info;
                issue.text = std::string(Triggers::KindName(a.spec.kind)) + " " + a.spec.name + ": its middle is " +
                             (place == Place::InsideSolid ? "inside solid collision" : "under a surface") +
                             "";
                Add(out, issue, "reach");
            }
        }
        // Gaps: an end for each start, a start for each end (the editor's or a level trigger's with the GapID).
        bool start = a.spec.kind == Triggers::Kind::GapStart, end = a.spec.kind == Triggers::Kind::GapEnd;
        if ((start || end) && Triggers::Partner(specs, static_cast<int>(i)) < 0) {
            uint32_t gap = Triggers::ChecksumOf(a.spec.gap);
            bool found = std::any_of(level.begin(), level.end(), [&](const Triggers::LevelTrigger& t) {
                return t.calls.gapId == gap && (start ? t.calls.endGap : t.calls.startGap);
            });
            if (!found) {
                issue.rule = Rule::GapUnpaired;
                issue.severity = Severity::Warning;
                issue.text = start ? "Gap start " + a.spec.name + " (" + a.spec.gap + "): no end"
                                   : "Gap end " + a.spec.name + " (" + a.spec.gap + "): no start";
                Add(out, issue);
            }
        }
    }
    // The level's trigger volumes.
    for (const Triggers::LevelTrigger& t : level) {
        if (t.off)
            continue;
        Issue issue;
        issue.sector = t.sector;
        issue.hasPos = true;
        issue.pos = (t.min + t.max) * 0.5f;
        issue.levelOwn = true;
        if (!t.hasNode) {
            issue.rule = Rule::TriggerIdle;
            issue.severity = Severity::Info;
            issue.text = "Trigger faces of " + Name(t.sector) + " have no node";
            Add(out, issue);
        } else if (!t.script) {
            issue.node = t.sector;
            issue.rule = Rule::TriggerScript;
            issue.severity = Severity::Info;
            issue.text = "Trigger " + Name(t.sector) + " has no TriggerScript";
            Add(out, issue);
        } else if (!Script::ScriptExists(t.script)) {
            issue.node = t.sector;
            issue.rule = Rule::TriggerScript;
            issue.severity = Severity::Error;
            issue.text = "Trigger " + Name(t.sector) + ": its TriggerScript " + Name(t.script) + " isn't a script";
            Add(out, issue);
        }
    }
    // Proximity nodes (not the editor's trigger volumes, which are GenericNodes of the same family).
    for (const Nodes::Node& n : Nodes::All()) {
        if (n.family != Nodes::Family::Trigger || n.hidden || n.cls != kProximNode || Triggers::IsTrigger(n.name))
            continue;
        bool own = !n.added && !Marked(n.name, Ui::kMarkMoved | Ui::kMarkAdded);
        Issue issue;
        issue.node = n.name;
        issue.hasPos = n.hasPos;
        issue.pos = n.pos;
        uint32_t script = 0;
        Script::CStruct* s = Nodes::StructOf(n);
        if (s)
            s->GetChecksum(kTriggerScript, &script, 0);
        const Triggers::ProximEdit* edit = Triggers::ProximOf(n.name);
        bool edited = edit && (!edit->enter.empty() || !edit->exit.empty());
        if (!script && !edited) {
            issue.rule = Rule::TriggerScript;
            issue.severity = own ? Severity::Info : Severity::Warning;
            issue.levelOwn = own;
            issue.text = "Proximity node " + Name(n.name) + " has no TriggerScript";
            Add(out, issue);
        } else if (script && !Script::ScriptExists(script)) {
            issue.rule = Rule::TriggerScript;
            issue.severity = Severity::Error;
            issue.levelOwn = own;
            issue.text = "Proximity node " + Name(n.name) + ": its TriggerScript " + Name(script) + " isn't a script";
            Add(out, issue);
        }
        if (edit) {
            for (const std::string* name : {&edit->enter, &edit->exit}) {
                if (name->empty() || Script::ScriptExists(Crc(*name)))
                    continue;
                issue.rule = Rule::TriggerScript;
                issue.severity = Severity::Error;
                issue.levelOwn = false;
                issue.text = "Proximity node " + Name(n.name) + ": its " + (name == &edit->enter ? "enter" : "exit") +
                             " script " + *name + " isn't a script";
                Add(out, issue, *name);
            }
        }
    }
}

void CheckOnline(std::vector<Issue>* out)
{
    const char* game = "";
    if (!Nodes::LevelIsShared(&game))
        return;
    for (const Nodes::Node& n : Nodes::All()) {
        if (!Nodes::Online(n.family))
            continue;
        bool hidden = n.hidden && !n.added, added = n.added && !n.hidden;
        bool moved = Marked(n.name, Ui::kMarkMoved);
        if (!hidden && !added && !moved)
            continue;
        Issue issue;
        issue.rule = Rule::OnlineParity;
        issue.severity = Severity::Info;
        issue.node = n.name;
        issue.hasPos = n.hasPos;
        issue.pos = n.pos;
        issue.text = Name(n.name) + (added ? " added" : hidden ? " hidden" : " moved") + " in a level shared with " +
                     "THUGPRO (" + game + ")";
        Add(out, issue);
    }
}

MpCounts CountMultiplayer(bool shipped)
{
    static uint32_t kNames[4][3];
    static bool made = false;
    if (!made) {
        for (int t = 0; t < 4; ++t) {
            std::string colour = kTeamNames[t];
            kNames[t][0] = Crc("TRG_CTF_" + colour);
            kNames[t][1] = Crc("TRG_CTF_" + colour + "_Base");
            kNames[t][2] = Crc("TRG_CTF_Restart_" + colour);
        }
        made = true;
    }
    static const uint32_t kCrown = Crc("Crown");
    MpCounts counts;
    for (const Nodes::Node& n : Nodes::All()) {
        // (now: what the game sees - hidden ones not; as shipped: the level's own, hidden ones too)
        if (shipped ? n.added : n.hidden)
            continue;
        if (n.family == Nodes::Family::Spawn) {
            counts.player1 += (n.restartTypes & Nodes::kPlayer1) != 0;
            counts.multiplayer += (n.restartTypes & Nodes::kMultiplayer) != 0;
            counts.horse += (n.restartTypes & Nodes::kHorse) != 0;
        }
        if (n.type == kCrown)
            ++counts.crowns;
        for (int t = 0; t < 4; ++t) {
            counts.teams[t].flag = counts.teams[t].flag || n.name == kNames[t][0];
            counts.teams[t].base = counts.teams[t].base || n.name == kNames[t][1];
            counts.teams[t].restart = counts.teams[t].restart || n.name == kNames[t][2];
        }
    }
    return counts;
}

void CheckMultiplayer(std::vector<Issue>* out)
{
    if (Nodes::All().empty())
        return;
    std::vector<MpProblem> now = MultiplayerProblems(CountMultiplayer(false));
    std::vector<MpProblem> shipped = MultiplayerProblems(CountMultiplayer(true));
    for (const MpProblem& p : now) {
        Issue issue;
        issue.rule = Rule::Multiplayer;
        issue.severity = p.severity;
        issue.text = p.text;
        issue.levelOwn = std::any_of(shipped.begin(), shipped.end(), [&](const MpProblem& s) { return s.text == p.text; });
        Add(out, issue);
    }
}

void Run()
{
    auto started = std::chrono::steady_clock::now();
    std::vector<Issue> issues;
    if (!Nodes::All().empty()) {
        uint32_t world = WorldSignature();
        CheckPlacement(&issues, world);
        CheckRails(&issues);
        CheckCollision(&issues);
        CheckLimits(&issues);
        CheckTriggers(&issues, world);
        CheckOnline(&issues);
        CheckMultiplayer(&issues);
        for (const Issue& u : g_layoutIssues)
            issues.push_back(u);
    }
    Sort(&issues);
    g_issues = std::move(issues);
    g_checkedOnce = true;
    g_lastSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (DebugEnabled())
        Log("level editor: problems checked - %u issues in %.1f ms", static_cast<unsigned>(g_issues.size()),
            g_lastSeconds * 1000.0);
}

// --- fixes (undoable) -------------------------------------------------------------------------------------------

struct NodePlace : ModuleStep {
    uint32_t name;
    NodeEdit::Values values;
    NodePlace(uint32_t n, const NodeEdit::Values& v) : name(n), values(v) {}
    void Restore() override
    {
        NodeEdit::Set(name, values);
        if (int index = Nodes::Find(name); index >= 0)
            Nodes::Reread(index);
        g_due = Seconds();
    }
    std::shared_ptr<ModuleStep> Now() const override
    {
        NodeEdit::Values v = values;
        NodeEdit::Read(name, &v);
        return std::make_shared<NodePlace>(name, v);
    }
};

struct LayoutRecords : ModuleStep {
    std::vector<Issue> issues;
    explicit LayoutRecords(std::vector<Issue> list) : issues(std::move(list)) {}
    void Restore() override
    {
        g_layoutIssues = issues;
        g_due = Seconds();
    }
    std::shared_ptr<ModuleStep> Now() const override { return std::make_shared<LayoutRecords>(g_layoutIssues); }
};

void Fix(const Issue& issue)
{
    switch (issue.rule) {
    case Rule::BelowGround: {
        NodeEdit::Values values;
        if (!issue.node || !NodeEdit::Read(issue.node, &values))
            return;
        PushModuleUndo(std::make_shared<NodePlace>(issue.node, values));
        values.pos = issue.fixPos;
        NodeEdit::Set(issue.node, values);
        if (int index = Nodes::Find(issue.node); index >= 0)
            Nodes::Reread(index);
        Hud::Message(("Moved to surface: " + Name(issue.node)).c_str());
        Log("level editor: problems - %s put on the surface above it (%.0f, %.0f, %.0f)", Name(issue.node).c_str(),
            issue.fixPos.x, issue.fixPos.y, issue.fixPos.z);
        break;
    }
    case Rule::HiddenCollision:
        SharedCollision::SetActive(issue.sector, false);
        Hud::Message(("The collision taken along by " + Name(issue.sector) + " turned off").c_str());
        break;
    case Rule::LayoutRecord: {
        PushModuleUndo(std::make_shared<LayoutRecords>(g_layoutIssues));
        g_layoutIssues.erase(std::remove_if(g_layoutIssues.begin(), g_layoutIssues.end(),
                                            [&](const Issue& i) { return i.key == issue.key; }),
                             g_layoutIssues.end());
        Hud::Message("Record removed");
        Log("level editor: problems - layout record dropped: %s", issue.record.c_str());
        break;
    }
    default: break;
    }
    g_due = Seconds();
}

} // namespace

// --- the layout's unresolved records ------------------------------------------------------------------------------

namespace {

// One record's line: a file of that record alone, written (its header and scene lines left out).
std::string LineOf(const Layout::File& one)
{
    std::istringstream in(Layout::Write(one));
    std::string line, out;
    int number = 0;
    while (std::getline(in, line)) {
        if (++number <= 1 || line.rfind("scene", 0) == 0 || line.empty())
            continue;
        out += (out.empty() ? "" : "\n") + line;
    }
    return out;
}

} // namespace

void NoteLayoutApplied(const Layout::File& file)
{
    g_layoutIssues.clear();
    auto node = [](uint32_t name) { return name && Nodes::Find(name) >= 0; };
    auto sector = [](uint32_t checksum) { return checksum && Scenery::SectorOf(checksum) != nullptr; };
    auto object = [&](uint32_t id) { return Obj::FindObject(id) != nullptr || node(id); };
    auto report = [&](uint32_t subject, const std::string& what, Layout::File one) {
        one.scene = file.scene;
        Issue issue;
        issue.rule = Rule::LayoutRecord;
        issue.severity = Severity::Error;
        issue.record = LineOf(one);
        issue.fixable = !issue.record.empty();
        issue.text = what + " not found in the level: " + issue.record;
        issue.key = IssueKey(Rule::LayoutRecord, subject, issue.record);
        if (std::none_of(g_layoutIssues.begin(), g_layoutIssues.end(), [&](const Issue& i) { return i.key == issue.key; }))
            g_layoutIssues.push_back(issue);
    };
    for (const Scenery::SectorState& s : file.sectors) {
        if (!sector(s.checksum)) {
            Layout::File one;
            one.sectors.push_back(s);
            report(s.checksum, "Scenery piece " + Name(s.checksum), one);
        }
    }
    for (const Scenery::RailState& r : file.rails) {
        if (!node(r.name)) {
            Layout::File one;
            one.rails.push_back(r);
            report(r.name, "Rail point " + Name(r.name), one);
        }
    }
    for (const Layout::CloneRecord& c : file.clones) {
        if (!sector(c.source)) {
            Layout::File one;
            one.clones.push_back(c);
            report(c.source, "The source of a scenery copy, " + Name(c.source), one);
        }
    }
    for (const Layout::ObjectRecord& o : file.objects) {
        if (!object(o.id)) {
            Layout::File one;
            one.objects.push_back(o);
            report(o.id, "Object " + Name(o.id), one);
        }
    }
    for (const Layout::CopyRecord& c : file.copies) {
        if (!Obj::FindObject(c.name) && !object(c.source)) {
            Layout::File one;
            one.copies.push_back(c);
            report(c.name, "The source of object copy " + Name(c.name) + ", " + Name(c.source), one);
        }
    }
    for (const Layout::SetRecord& s : file.nodes) {
        if (!node(s.name)) {
            Layout::File one;
            one.nodes.push_back(s);
            report(s.name, "Node " + Name(s.name) + " (moved / edited)", one);
        }
    }
    for (uint32_t name : file.hidden) {
        if (!node(name)) {
            Layout::File one;
            one.hidden.push_back(name);
            report(name, "Node " + Name(name) + " (hidden)", one);
        }
    }
    for (const Picking::Lock& lock : file.locks) {
        bool there = lock.kind == Picking::kObjects ? object(lock.id) : sector(lock.id);
        if (!there) {
            Layout::File one;
            one.locks.push_back(lock);
            report(lock.id, "Locked " + std::string(lock.kind == Picking::kObjects ? "object " : "piece ") +
                                Name(lock.id), one);
        }
    }
    for (const Triggers::ProximEdit& p : file.proxims) {
        if (!node(p.node)) {
            Layout::File one;
            one.proxims.push_back(p);
            report(p.node, "Proximity node " + Name(p.node), one);
        }
    }
    for (const Walls::WallState& w : file.walls) {
        if (!sector(w.checksum)) {
            Layout::File one;
            one.walls.push_back(w);
            report(w.checksum, "Collision " + Name(w.checksum), one);
        }
    }
    for (const Faces::SectorFaces& f : file.faces) {
        if (!sector(f.checksum)) {
            Layout::File one;
            one.faces.push_back(f);
            report(f.checksum, "Collision faces of " + Name(f.checksum), one);
        }
    }
    for (const Clusters::Membership& m : file.clusters) {
        if (!sector(m.sector)) {
            Layout::File one;
            one.clusters.push_back(m);
            report(m.sector, "Trick-object cluster of " + Name(m.sector), one);
        }
    }
    // The chains' edits of level nodes (links, dropped points, terrains, facings, flat grinds, speed-ups).
    const Chains::State& chains = file.chains;
    for (const Chains::LinkEdit& l : chains.links) {
        if (!node(l.name)) {
            Layout::File one;
            one.chains.links.push_back(l);
            report(l.name, "Links of " + Name(l.name), one);
        }
    }
    for (uint32_t name : chains.dropped) {
        if (!node(name)) {
            Layout::File one;
            one.chains.dropped.push_back(name);
            report(name, "Dropped point " + Name(name), one);
        }
    }
    for (const Chains::TerrainEdit& t : chains.terrains) {
        if (!node(t.name)) {
            Layout::File one;
            one.chains.terrains.push_back(t);
            report(t.name, "Terrain of " + Name(t.name), one);
        }
    }
    for (const Chains::FacingEdit& f : chains.facings) {
        if (!node(f.name)) {
            Layout::File one;
            one.chains.facings.push_back(f);
            report(f.name, "Facing of " + Name(f.name), one);
        }
    }
    for (uint32_t name : chains.grindFlat) {
        if (!node(name)) {
            Layout::File one;
            one.chains.grindFlat.push_back(name);
            report(name, "Flat grind of " + Name(name), one);
        }
    }
    for (const Chains::SpeedEdit& s : chains.speeds) {
        if (!node(s.name)) {
            Layout::File one;
            one.chains.speeds.push_back(s);
            report(s.name, "Rail speed-up of " + Name(s.name), one);
        }
    }
    for (const Entities::Speed& s : file.entities.speeds) {
        if (!node(s.name)) {
            Layout::File one;
            one.entities.speeds.push_back(s);
            report(s.name, "Path speed of " + Name(s.name), one);
        }
    }
    for (const Entities::EmitterSound& e : file.entities.emitters) {
        if (!node(e.name)) {
            Layout::File one;
            one.entities.emitters.push_back(e);
            report(e.name, "Sound emitter " + Name(e.name), one);
        }
    }
    if (file.level.start && !node(file.level.start)) {
        Layout::File one;
        one.level = file.level;
        report(file.level.start, "The level's start, restart " + Name(file.level.start), one);
    }
    for (const Issue& i : g_layoutIssues)
        Log("level editor: layout record not found in the level - kept for the next save (the Problems window, "
            "Alt+P, drops it): %s",
            i.record.c_str());
    g_due = Seconds();
}

void ForgetLayoutRecords()
{
    g_layoutIssues.clear();
    g_due = Seconds();
}

std::vector<std::string> CarriedRecords(const std::string& captured)
{
    std::vector<std::string> lines;
    for (const Issue& i : g_layoutIssues) {
        if (i.record.empty())
            continue;
        std::istringstream in(i.record);
        std::string line;
        while (std::getline(in, line))
            lines.push_back(line);
    }
    return lines.empty() ? lines : CarriedLines(lines, captured);
}

const std::vector<Issue>& LayoutIssues() { return g_layoutIssues; }

// --- the frame ----------------------------------------------------------------------------------------------------

void NoteModel(int changes, const std::unordered_map<uint32_t, uint8_t>& marks)
{
    uint32_t generation = Nodes::Generation();
    int loads = Layout::LevelLoads();
    double now = Seconds();
    if (changes != g_changes || generation != g_generation || loads != g_loads || !g_checkedOnce) {
        bool first = !g_checkedOnce || loads != g_loads;
        g_changes = changes;
        g_generation = generation;
        g_loads = loads;
        g_marks = marks;
        // (a moment after a change: changes come in bursts - a drag, a typed field)
        g_due = first ? now : now + 0.4;
    }
    if (g_due >= 0.0 && now >= g_due) {
        g_marks = marks;
        g_due = -1.0;
        Run();
    }
}

void Update()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (const auto& action : queue)
        action();
}

void ForgetLevel()
{
    g_placed.clear();
    g_issues.clear();
    g_layoutIssues.clear();
    g_ignored.clear();
    g_marks.clear();
    g_checkedOnce = false;
    g_due = -1.0;
}

const std::vector<Issue>& Issues() { return g_issues; }

namespace {

bool Shown(const Issue& issue)
{
    return (g_showOwn || !issue.levelOwn) && (g_showIgnored || !g_ignored.count(issue.key));
}

} // namespace

void ShownCounts(int* errors, int* warnings, int* infos)
{
    *errors = *warnings = *infos = 0;
    for (const Issue& issue : g_issues) {
        if (issue.levelOwn || g_ignored.count(issue.key))
            continue;
        (issue.severity == Severity::Error ? *errors : issue.severity == Severity::Warning ? *warnings : *infos) += 1;
    }
}

void OpenWindow(int tab)
{
    g_open = true;
    g_tab = tab;
    g_tabPending = true;
    g_due = Seconds(); // (opened: checked again now)
}

void ToggleWindow()
{
    if (g_open)
        g_open = false;
    else
        OpenWindow(kProblemsTab);
}

// --- the UI -------------------------------------------------------------------------------------------------------

namespace {

ImVec4 SeverityColour(Severity severity)
{
    switch (severity) {
    case Severity::Error: return ImVec4(0.90f, 0.28f, 0.30f, 1.0f);
    case Severity::Warning: return ImVec4(0.96f, 0.65f, 0.14f, 1.0f);
    default: return ImVec4(0.45f, 0.70f, 0.95f, 1.0f);
    }
}

void GoTo(const Issue& issue)
{
    bool done = false;
    if (issue.node)
        done = GoToNodeNamed(issue.node);
    if (!done && issue.sector)
        done = GoToScenery(issue.sector);
    if (!done && issue.hasPos)
        GoToPoint(issue.pos, 400.0f);
}

void ProblemsTab(float scale, ImFont* mono)
{
    (void)mono;
    if (ImGui::Button("Check now"))
        g_queue.push_back([] { g_due = Seconds(); });
    ImGui::SameLine();
    int own = 0, ignored = 0;
    for (const Issue& issue : g_issues) {
        own += issue.levelOwn;
        ignored += g_ignored.count(issue.key) != 0;
    }
    char label[64];
    std::snprintf(label, sizeof(label), "Original Level (%d)", own);
    ImGui::Checkbox(label, &g_showOwn);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Problems present in the original level");
    ImGui::SameLine();
    std::snprintf(label, sizeof(label), "Ignored (%d)", ignored);
    ImGui::Checkbox(label, &g_showIgnored);
    ImGui::SameLine();
    ImGui::TextDisabled(g_checkedOnce ? "%.0f ms" : "not checked", g_lastSeconds * 1000.0);
    ImGui::Separator();
    int shown = 0;
    for (const Issue& issue : g_issues)
        shown += Shown(issue);
    if (!shown) {
        ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.50f, 1.0f), "No problems");
        if (own)
            ImGui::TextDisabled("(%d in the original level)", own);
        return;
    }
    if (!ImGui::BeginTable("##problems", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                           ImVec2(0.0f, 340.0f * scale)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 64.0f * scale);
    ImGui::TableSetupColumn("Problem", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f * scale);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < g_issues.size(); ++i) {
        const Issue& issue = g_issues[i];
        if (!Shown(issue))
            continue;
        bool isIgnored = g_ignored.count(issue.key) != 0;
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(SeverityColour(issue.severity), "%s", SeverityName(issue.severity));
        ImGui::TableNextColumn();
        if (isIgnored || issue.levelOwn)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextDisabled("%s%s", RuleName(issue.rule), issue.levelOwn ? " (original)" : "");
        ImGui::TextWrapped("%s", issue.text.c_str());
        if (isIgnored || issue.levelOwn)
            ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        bool canGo = issue.node || issue.sector || issue.hasPos;
        ImGui::BeginDisabled(!canGo);
        if (ImGui::SmallButton("Go to")) {
            Issue copy = issue;
            g_queue.push_back([copy] { GoTo(copy); });
        }
        ImGui::EndDisabled();
        if (issue.fixable) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Fix")) {
                Issue copy = issue;
                g_queue.push_back([copy] { Fix(copy); });
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", issue.rule == Rule::BelowGround       ? "Move to the surface above"
                                        : issue.rule == Rule::HiddenCollision ? "Turn the collision off"
                                        : issue.rule == Rule::LayoutRecord    ? "Drop the record: the next save leaves it out "
                                                                                "(undoable)"
                                                                              : "");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(isIgnored ? "Unignore" : "Ignore")) {
            if (isIgnored)
                g_ignored.erase(issue.key);
            else
                g_ignored.insert(issue.key);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ignore until the level reloads");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale, ImFont* mono)
{
    (void)viewHeight;
    if (!g_open)
        return;
    ImGui::SetNextWindowPos(ImVec2(viewX + viewWidth * 0.5f, viewY + 48.0f * scale), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Problems###problems", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        g_open = open;
        return;
    }
    if (ImGui::BeginTabBar("##problemTabs")) {
        int errors = 0, warnings = 0, infos = 0;
        ShownCounts(&errors, &warnings, &infos);
        char label[64];
        std::snprintf(label, sizeof(label), "Problems (%d)###problemsTab", errors + warnings + infos);
        ImGuiTabItemFlags problemsFlags = g_tabPending && g_tab == kProblemsTab ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags budgetFlags = g_tabPending && g_tab == kBudgetTab ? ImGuiTabItemFlags_SetSelected : 0;
        g_tabPending = false;
        if (ImGui::BeginTabItem(label, nullptr, problemsFlags)) {
            g_tab = kProblemsTab;
            ProblemsTab(scale, mono);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Budget###budgetTab", nullptr, budgetFlags)) {
            g_tab = kBudgetTab;
            Budget::Table();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    g_open = open;
}

} // namespace Front::LevelEditor::Checks
