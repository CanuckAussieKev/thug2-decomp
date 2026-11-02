// NOT ORIGINAL - the level editor's problem checker (ours). See level_editor_checks.h.

#include "editor/level_editor_checks.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_ui.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_budget.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_shared_coll.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_walls.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/gfx/sector.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::checks {

// --- the pure parts -------------------------------------------------------------------------------------------------

const char* severity_name(Severity severity)
{
    switch (severity) {
    case Severity::kError:
        return "Error";
    case Severity::kWarning:
        return "Warning";
    default:
        return "Info";
    }
}

const char* rule_name(Rule rule)
{
    switch (rule) {
    case Rule::kInsideSolid:
        return "Inside solid";
    case Rule::kBelowGround:
        return "Below the ground";
    case Rule::kNoGround:
        return "Nothing below";
    case Rule::kFloatingRail:
        return "Floating rail point";
    case Rule::kLonelyRail:
        return "Rail of one point";
    case Rule::kCollisionLeft:
        return "Collision left behind";
    case Rule::kHiddenCollision:
        return "Hidden, collision on";
    case Rule::kLimit:
        return "Limit";
    case Rule::kTriggerIdle:
        return "Trigger never fires";
    case Rule::kTriggerScript:
        return "Trigger script";
    case Rule::kGapUnpaired:
        return "Gap unpaired";
    case Rule::kOnlineParity:
        return "Online parity";
    case Rule::kMultiplayer:
        return "Multiplayer";
    case Rule::kLayoutRecord:
        return "Layout record";
    }
    return "?";
}

uint32_t issue_key(Rule rule, uint32_t subject, const std::string& detail)
{
    // (FNV-1a over the rule, the subject's bytes and the detail)
    uint32_t hash = 2166136261u;
    auto mix = [&](uint8_t byte) { hash = (hash ^ byte) * 16777619u; };
    mix(static_cast<uint8_t>(rule));
    for (int i = 0; i < 4; ++i)
        mix(static_cast<uint8_t>(subject >> (8 * i)));
    for (const char c : detail)
        mix(static_cast<uint8_t>(c));
    return hash;
}

void get_sort(std::vector<Issue>* issues)
{
    std::ranges::stable_sort(*issues, [](const Issue& a, const Issue& b) {
        if (a.severity != b.severity)
            return a.severity < b.severity;
        if (a.level_own_ != b.level_own_)
            return !a.level_own_;
        if (a.rule != b.rule)
            return a.rule < b.rule;
        return a.text < b.text;
    });
}

bool get_behind(const Probe& probe) { return probe.in >= 0.0f && (probe.out < 0.0f || probe.in < probe.out); }

Place get_judge(std::span<const Probe, 6> probes)
{
    for (int axis = 0; axis < 3; ++axis) {
        if (get_behind(core::at(probes, 2 * axis)) && get_behind(core::at(probes, 2 * axis + 1)))
            return Place::kInsideSolid;
    }
    return get_behind(core::at(probes, 2)) ? Place::kBelowGround : Place::kClear;
}

bool floating(std::span<const Probe, 6> probes, float near)
{
    for (int i = 0; i < 6; ++i) {
        if ((core::at(probes, i).out >= 0.0f && core::at(probes, i).out <= near) || (core::at(probes, i).in >= 0.0f && core::at(probes, i).in <= near))
            return false;
    }
    return true;
}

std::vector<int> lonely_points(const std::vector<std::vector<int>>& links)
{
    std::vector<int> incoming(links.size(), 0);
    for (const std::vector<int>& out : links) {
        for (const int to : out) {
            if (to >= 0 && std::cmp_less(to, links.size()))
                ++core::at(incoming, to);
        }
    }
    std::vector<int> lonely;
    for (size_t i = 0; i < links.size(); ++i) {
        const bool links_out = std::any_of(core::at(links, i).begin(), core::at(links, i).end(),
                                           [&](int to) { return to >= 0 && std::cmp_less(to, links.size()); });
        if (!links_out && core::at(incoming, i) == 0)
            lonely.push_back(static_cast<int>(i));
    }
    return lonely;
}

namespace {

const std::array<const char*, 4> kTeamNames = {{"Red", "Blue", "Green", "Yellow"}};

} // namespace

std::vector<MpProblem> multiplayer_problems(const MpCounts& counts)
{
    std::vector<MpProblem> out;
    if (counts.player1 == 0)
        out.push_back({.severity = Severity::kError, .text = "No Player1 restart", .team = -1});
    if (counts.multiplayer == 0)
        out.push_back({.severity = Severity::kWarning, .text = "No Multiplayer restart", .team = -1});
    else if (counts.multiplayer == 1)
        out.push_back({.severity = Severity::kDetailsOf, .text = "Only one Multiplayer restart", .team = -1});
    if (counts.horse == 0)
        out.push_back({.severity = Severity::kWarning, .text = "No HORSE restart", .team = -1});
    if (counts.crowns == 0)
        out.push_back({.severity = Severity::kWarning, .text = "King of the Hill: no crowns", .team = -1});
    bool any_team = false;
    for (const TeamNodes& t : counts.teams)
        any_team = any_team || t.flag || t.base || t.restart;
    if (!any_team) {
        out.push_back({.severity = Severity::kWarning, .text = "No Capture the Flag flags, bases or team restarts", .team = -1});
        return out;
    }
    for (int team = 0; team < 4; ++team) {
        const TeamNodes& t = core::at(counts.teams, team);
        const bool any = t.flag || t.base || t.restart;
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
        const std::string colour = core::at(kTeamNames, team);
        add(t.flag, "its flag (TRG_CTF_" + colour + ")");
        add(t.base, "its base (TRG_CTF_" + colour + "_Base)");
        add(t.restart, "its team restart (TRG_CTF_Restart_" + colour + ")");
        std::string message = "Capture the Flag, team " + colour;
        message.append(": missing ").append(missing);
        out.push_back({.severity = Severity::kWarning, .text = message, .team = team});
    }
    return out;
}

std::string record_key(const std::string& line)
{
    const std::string text = line.substr(0, line.find('#'));
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

std::vector<std::string> carried_lines(const std::vector<std::string>& unresolved, const std::string& captured)
{
    std::set<std::string> keys;
    std::istringstream in(captured);
    std::string line;
    while (std::getline(in, line)) {
        const std::string key = record_key(line);
        if (!key.empty())
            keys.insert(key);
    }
    std::vector<std::string> out;
    for (const std::string& u : unresolved) {
        if (keys.count(record_key(u)) || std::ranges::find(out, u) != out.end())
            continue;
        out.push_back(u);
    }
    return out;
}

// --- the game side ------------------------------------------------------------------------------------------------

bool get_cast(V3 from, V3 to, V3* hit, V3* normal, uint32_t* sector)
{
    // (a ProbeRay: constructor 0x00417760; 0x00417710 casts it - thiscall, true on a hit; the hit's normal at +0x24, its
    // point at +0x34, the collision object at +0x54 and its checksum at +0x58 - as the editor's other modules read it)
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0f};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const auto& n = ray->hit_.normal;
    const auto& p = ray->hit_.point;
    *hit = {.x = p[0], .y = p[1], .z = p[2]};
    if (normal)
        *normal = {.x = n[0], .y = n[1], .z = n[2]};
    if (sector)
        *sector = ray->hit_.object ? ray->hit_.object_checksum_ : 0;
    return true;
}

namespace {

float span_to(V3 a, V3 b) { return std::sqrt(get_dot(a - b, a - b)); }

const std::array<V3, 6> kDirections = {{{.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0}, {.x = 0, .y = 1, .z = 0}, {.x = 0, .y = -1, .z = 0}, {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}}};

} // namespace

void probe_each(V3 point, std::span<const float, 6> reach, std::span<Probe, 6> probes)
{
    for (int i = 0; i < 6; ++i) {
        const V3 far = point + core::at(kDirections, i) * core::at(reach, i);
        V3 hit{};
        core::at(probes, i) = {};
        if (get_cast(point, far, &hit))
            core::at(probes, i).out = span_to(hit, point);
        // Coming back in: the face facing away nearest to the point - the last met on the way in (cast again from just
        // past each hit; a few at most).
        V3 from = far;
        for (int step = 0; step < 6 && get_cast(from, point, &hit); ++step) {
            core::at(probes, i).in = span_to(hit, point);
            const V3 on = hit + (point - far) * (0.5f / core::at(reach, i)); // (half a unit on, towards the point)
            if (span_to(on, point) < 0.5f)
                break;
            from = on;
        }
    }
}

void probe_around(V3 point, float reach, std::span<Probe, 6> probes)
{
    const std::array<float, 6> reaches = {{reach, reach, reach, reach, reach, reach}};
    probe_each(point, reaches, probes);
}

namespace {

// --- the state ----------------------------------------------------------------------------------------------------

std::vector<Issue> g_issues;        // the last check's
std::vector<Issue> g_layout_issues; // the loaded layout's unresolved records
std::unordered_set<uint32_t>& ignored_state()
{
    static std::unordered_set<uint32_t> value;
    return value;
}
std::unordered_map<uint32_t, uint8_t>& marks_state()
{
    static std::unordered_map<uint32_t, uint8_t> value;
    return value;
}
int g_changes = -1;
uint32_t g_generation = 0;
int g_loads = -1;
double g_due = -1.0; // (seconds: when to check again; -1 none due)
bool g_checked_once = false;
double g_last_seconds = 0.0; // (how long the last check took)
bool g_open = false;
int g_tab = kProblemsTab;
bool g_tab_pending = false;
bool g_show_own = false;
bool g_show_ignored = false;
std::vector<std::function<void()>> g_queue;

double get_seconds() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }

uint32_t get_checksums(const std::string& text) { return checksums::hash_from_text(text.c_str()); }

std::string get_name(uint32_t checksum) { return nodes::caption(checksum); }

bool get_marked(uint32_t name, uint8_t bits)
{
    auto it = marks_state().find(name);
    return it != marks_state().end() && (it->second & bits);
}

// Whether the editor changed scenery near a point (moved pieces, copies): then a problem there isn't the level's own.
bool scenery_changed_near(V3 point, float margin)
{
    std::vector<uint32_t> pieces;
    for (const scenery::SectorState& s : scenery::changed_sectors())
        pieces.push_back(s.checksum);
    for (const scenery::Replicate& c : scenery::get_clones())
        pieces.push_back(c.checksum);
    for (const uint32_t piece : pieces) {
        OrientedBox box{};
        if (!scenery::cuboid(piece, &box))
            continue;
        V3 min{}, max{};
        bounds_of(box, &min, &max);
        if (point.x >= min.x - margin && point.x <= max.x + margin && point.y >= min.y - margin &&
            point.y <= max.y + margin && point.z >= min.z - margin && point.z <= max.z + margin)
            return true;
    }
    return false;
}

void append_one(std::vector<Issue>* out, Issue issue, const std::string& detail = "")
{
    issue.key = issue_key(issue.rule, issue.node ? issue.node : issue.sector, detail.empty() ? issue.text : detail);
    out->push_back(std::move(issue));
}

// --- the rules ----------------------------------------------------------------------------------------------------

// The placement probes' verdict for a point, kept while neither it nor the level's collision changes (`world`).
struct Placed {
    V3 point = {};
    uint32_t world = 0;
    Place place = Place::kClear;
    float up_in_ = -1.0f; // (the surface above, from the point - kBelowGround's Fix)
    bool ground = true;   // (a face within kGroundReach below)
};
std::unordered_map<uint32_t, Placed>& placed_state()
{
    static std::unordered_map<uint32_t, Placed> value;
    return value;
}

// A number that changes when the static collision the probes meet may have: scenery moved / hidden / copied, walls
// switched or moved, faces taken along or their flags edited.
uint32_t world_signature()
{
    uint32_t hash = 2166136261u;
    auto mix = [&](const void* data, size_t size) {
        for (const uint8_t byte : std::span<const uint8_t>(static_cast<const uint8_t*>(data), size))
            hash = (hash ^ byte) * 16777619u;
    };
    for (const scenery::SectorState& s : scenery::changed_sectors()) {
        mix(&s.checksum, sizeof(s.checksum));
        mix(&s.placement.quarters, sizeof(s.placement.quarters));
        mix(&s.placement.offset, sizeof(s.placement.offset));
        mix(&s.placement.scale, sizeof(s.placement.scale));
        mix(&s.hidden, sizeof(s.hidden));
    }
    for (const walls::WallState& w : walls::get_changed()) {
        mix(&w.checksum, sizeof(w.checksum));
        mix(&w.placement.quarters, sizeof(w.placement.quarters));
        mix(&w.placement.offset, sizeof(w.placement.offset));
        mix(&w.off, sizeof(w.off));
    }
    size_t clones = scenery::get_clones().size(), faces = faces::get_changed().size();
    uint32_t shared = shared_collision::get_generation();
    mix(&clones, sizeof(clones));
    mix(&faces, sizeof(faces));
    mix(&shared, sizeof(shared));
    return hash | 1u; // (never 0: 0 is "not probed")
}

const Placed& placed_at(uint32_t name, V3 point, uint32_t world)
{
    Placed& placed = placed_state()[name];
    if (placed.world == world && placed.point.x == point.x && placed.point.y == point.y && placed.point.z == point.z)
        return placed;
    // (the sides only kSideReach: a wall's thickness - farther faces belong to other things; up and down kReach)
    const std::array<float, 6> reaches = {{kSideReach, kSideReach, kReach, kReach, kSideReach, kSideReach}};
    std::array<Probe, 6> probes;
    probe_each(point, reaches, probes);
    placed.point = point;
    placed.world = world;
    placed.place = get_judge(probes);
    placed.up_in_ = core::at(probes, 2).in;
    V3 hit{};
    placed.ground = core::at(probes, 3).out >= 0.0f || get_cast(point, point - V3{.x = 0.0f, .y = kGroundReach, .z = 0.0f}, &hit);
    return placed;
}

void check_placement(std::vector<Issue>* out, uint32_t world)
{
    for (const nodes::Elem& n : nodes::get_all()) {
        if (n.hidden || !n.has_pos_)
            continue;
        if (n.family != nodes::Family::kSpawn && n.family != nodes::Family::kCrown &&
            n.family != nodes::Family::kBitMark)
            continue;
        const V3 point = n.pos + V3{.x = 0.0f, .y = kLift, .z = 0.0f};
        const Placed& placed = placed_at(n.name, point, world);
        const Place place = placed.place;
        const bool own =
            !get_marked(n.name, ui::kMarkMoved | ui::kMarkAdded) && !n.added && !scenery_changed_near(n.pos, 300.0f);
        std::string what = std::string(nodes::family_name(n.family));
        what = what.substr(0, what.size() - 1); // ("Spawns" -> "Spawn")
        Issue issue;
        issue.node = n.name;
        issue.has_pos_ = true;
        issue.pos = n.pos;
        issue.level_own_ = own;
        if (place == Place::kInsideSolid) {
            issue.rule = Rule::kInsideSolid;
            issue.severity = Severity::kError;
            issue.text = what + " " + get_name(n.name) + " is inside solid collision";
            append_one(out, issue);
        } else if (place == Place::kBelowGround) {
            issue.rule = Rule::kBelowGround;
            issue.severity = Severity::kError;
            issue.text = what + " " + get_name(n.name) + " is under a surface (" +
                         std::to_string(static_cast<int>(placed.up_in_ - kLift)) + " units below the surface)";
            issue.fixable = node_edit::get_movable(n);
            issue.fix_pos_ = point + V3{.x = 0.0f, .y = placed.up_in_, .z = 0.0f};
            append_one(out, issue, "below"); // (the key without the depth: Ignore holds while it's moved about)
        } else {
            if (!placed.ground) {
                issue.rule = Rule::kNoGround;
                issue.severity = Severity::kWarning;
                issue.text = what + " " + get_name(n.name) + ": nothing below it within " +
                             std::to_string(static_cast<int>(kGroundReach)) + " units";
                append_one(out, issue);
            }
        }
    }
}

void check_rails(std::vector<Issue>* out)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    std::vector<int> rails;
    std::vector<int> index(nodes.size(), -1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (core::at(nodes, i).family == nodes::Family::kRail && !core::at(nodes, i).hidden) {
            core::at(index, i) = static_cast<int>(rails.size());
            rails.push_back(static_cast<int>(i));
        }
    }
    std::vector<std::vector<int>> links(rails.size());
    for (size_t r = 0; r < rails.size(); ++r) {
        for (const int to : core::at(nodes, core::at(rails, r)).links) {
            if (to >= 0 && std::cmp_less(to, nodes.size()) && core::at(index, to) >= 0)
                core::at(links, r).push_back(core::at(index, to));
        }
    }
    for (const int r : lonely_points(links)) {
        const nodes::Elem& n = core::at(nodes, core::at(rails, r));
        Issue issue;
        issue.rule = Rule::kLonelyRail;
        issue.severity = Severity::kWarning;
        issue.node = n.name;
        issue.has_pos_ = n.has_pos_;
        issue.pos = n.pos;
        issue.level_own_ = !n.added && !get_marked(n.name, ui::kMarkAdded);
        issue.text = "Rail point " + get_name(n.name) + " is not linked";
        append_one(out, issue);
    }
    // Floating: the rail points the editor moved or added.
    for (const int i : rails) {
        const nodes::Elem& n = core::at(nodes, i);
        if (!n.added && !get_marked(n.name, ui::kMarkMoved | ui::kMarkAdded))
            continue;
        V3 pos = n.pos;
        scenery::get_rail_point(n.name, &pos);
        std::array<Probe, 6> probes;
        probe_around(pos, kRailNear, probes);
        if (!floating(probes, kRailNear))
            continue;
        Issue issue;
        issue.rule = Rule::kFloatingRail;
        issue.severity = Severity::kWarning;
        issue.node = n.name;
        issue.has_pos_ = true;
        issue.pos = pos;
        issue.text = "Rail point " + get_name(n.name) + " has no surface within " +
                     std::to_string(static_cast<int>(kRailNear)) + " units";
        append_one(out, issue);
    }
}

void check_collision(std::vector<Issue>* out)
{
    for (const scenery::SectorState& s : scenery::changed_sectors()) {
        const render::WorldCell* sector = scenery::sector_of(s.checksum);
        if (!sector)
            continue;
        const shared_collision::Companion* companion = shared_collision::companion_of(s.checksum);
        Issue issue;
        issue.sector = s.checksum;
        OrientedBox box{};
        if (scenery::cuboid(s.checksum, &box)) {
            V3 min{}, max{};
            bounds_of(box, &min, &max);
            issue.has_pos_ = true;
            issue.pos = (min + max) * 0.5f;
        }
        if (s.hidden) {
            if (companion && companion->live_now()) {
                issue.rule = Rule::kHiddenCollision;
                issue.severity = Severity::kError;
                issue.text = "Scenery " + get_name(s.checksum) + " is hidden but its collision is still on";
                issue.fixable = true;
                append_one(out, issue);
            }
            continue;
        }
        const bool moved = s.placement.quarters != 0 || !is_unit_scale(s.placement.scale) ||
                           s.placement.offset.x != 0.0f || s.placement.offset.y != 0.0f || s.placement.offset.z != 0.0f;
        const bool own = sector->hit_body_ != nullptr; // (its collision object - WorldCell +0x18)
        if (moved && !own && !companion && scenery::sector_geometry(s.checksum)) {
            issue.rule = Rule::kCollisionLeft;
            issue.severity = Severity::kDetailsOf;
            issue.text = "Scenery " + get_name(s.checksum) + " was moved but its collision may not have moved";
            append_one(out, issue);
        }
    }
}

void check_limits(std::vector<Issue>* out)
{
    for (const budget::Meter& m : budget::get_meters()) {
        if (m.limit <= 0.0 || m.bytes)
            continue;
        Severity severity{};
        if (m.used > m.limit)
            severity = Severity::kError;
        else if (m.used >= 0.9 * m.limit)
            severity = Severity::kWarning;
        else
            continue;
        const double before = m.used - m.added;
        Issue issue;
        issue.rule = Rule::kLimit;
        issue.severity = severity;
        issue.level_own_ = severity == Severity::kError ? before > m.limit : before >= 0.9 * m.limit;
        issue.text = std::string(m.name) + ": " + budget::used_text(m) +
                     (severity == Severity::kError ? " - over the engine's limit (" : " - near the engine's limit (") +
                     m.where + ")";
        append_one(out, issue, m.name);
    }
}

void check_triggers(std::vector<Issue>* out, uint32_t world)
{
    static const uint32_t kTriggerScript = get_checksums("TriggerScript"), kProximNode = get_checksums("ProximNode");
    // The editor's trigger volumes.
    const std::vector<triggers::Added>& added = triggers::added_triggers();
    std::vector<triggers::Spec> specs;
    specs.reserve(added.size());
    for (const triggers::Added& a : added)
        specs.push_back(a.spec);
    const std::vector<triggers::LevelTrigger>& level = triggers::level_triggers();
    for (size_t i = 0; i < added.size(); ++i) {
        const triggers::Added& a = core::at(added, i);
        Issue issue;
        issue.node = triggers::checksum_of(a.spec.name);
        issue.has_pos_ = a.alive;
        issue.pos = a.pos;
        if (a.problem) {
            issue.rule = Rule::kTriggerIdle;
            issue.severity = Severity::kWarning;
            issue.text =
                std::string(triggers::kind_name(a.spec.kind)) + " " + a.spec.name + " does nothing yet: " + a.problem;
            append_one(out, issue);
        }
        if (a.alive) {
            const Place place = placed_at(issue.node, a.pos, world).place;
            if (place != Place::kClear) {
                issue.rule = Rule::kTriggerIdle;
                issue.severity = Severity::kDetailsOf;
                issue.text = std::string(triggers::kind_name(a.spec.kind)) + " " + a.spec.name + ": its middle is " +
                             (place == Place::kInsideSolid ? "inside solid collision" : "under a surface") + "";
                append_one(out, issue, "reach");
            }
        }
        // Gaps: an end for each start, a start for each end (the editor's or a level trigger's with the GapID).
        bool start = a.spec.kind == triggers::Kind::kGapStart, end = a.spec.kind == triggers::Kind::kGapEnd;
        if ((start || end) && triggers::partner(specs, static_cast<int>(i)) < 0) {
            uint32_t gap = triggers::checksum_of(a.spec.gap);
            const bool found = std::ranges::any_of(level, [&](const triggers::LevelTrigger& t) {
                return t.calls.gap_id_ == gap && (start ? t.calls.end_gap_ : t.calls.start_gap_);
            });
            if (!found) {
                issue.rule = Rule::kGapUnpaired;
                issue.severity = Severity::kWarning;
                issue.text = start ? "Gap start " + a.spec.name + " (" + a.spec.gap + "): no end"
                                   : "Gap end " + a.spec.name + " (" + a.spec.gap + "): no start";
                append_one(out, issue);
            }
        }
    }
    // The level's trigger volumes.
    for (const triggers::LevelTrigger& t : level) {
        if (t.off)
            continue;
        Issue issue;
        issue.sector = t.sector;
        issue.has_pos_ = true;
        issue.pos = (t.min + t.max) * 0.5f;
        issue.level_own_ = true;
        if (!t.has_node_) {
            issue.rule = Rule::kTriggerIdle;
            issue.severity = Severity::kDetailsOf;
            issue.text = "Trigger faces of " + get_name(t.sector) + " have no node";
            append_one(out, issue);
        } else if (!t.script) {
            issue.node = t.sector;
            issue.rule = Rule::kTriggerScript;
            issue.severity = Severity::kDetailsOf;
            issue.text = "Trigger " + get_name(t.sector) + " has no TriggerScript";
            append_one(out, issue);
        } else if (!script::script_exists(t.script)) {
            issue.node = t.sector;
            issue.rule = Rule::kTriggerScript;
            issue.severity = Severity::kError;
            issue.text =
                "Trigger " + get_name(t.sector) + ": its TriggerScript " + get_name(t.script) + " isn't a script";
            append_one(out, issue);
        }
    }
    // Proximity nodes (not the editor's trigger volumes, which are GenericNodes of the same family).
    for (const nodes::Elem& n : nodes::get_all()) {
        if (n.family != nodes::Family::kFireOff || n.hidden || n.cls != kProximNode || triggers::is_trigger(n.name))
            continue;
        const bool own = !n.added && !get_marked(n.name, ui::kMarkMoved | ui::kMarkAdded);
        Issue issue;
        issue.node = n.name;
        issue.has_pos_ = n.has_pos_;
        issue.pos = n.pos;
        uint32_t script = 0;
        script::Struct* s = nodes::struct_of(n);
        if (s)
            s->read_hash_value(kTriggerScript, &script, 0);
        const triggers::ProximEdit* edit = triggers::proxim_of(n.name);
        const bool edited = edit && (!edit->enter.empty() || !edit->exit.empty());
        if (!script && !edited) {
            issue.rule = Rule::kTriggerScript;
            issue.severity = own ? Severity::kDetailsOf : Severity::kWarning;
            issue.level_own_ = own;
            issue.text = "Proximity node " + get_name(n.name) + " has no TriggerScript";
            append_one(out, issue);
        } else if (script && !script::script_exists(script)) {
            issue.rule = Rule::kTriggerScript;
            issue.severity = Severity::kError;
            issue.level_own_ = own;
            issue.text =
                "Proximity node " + get_name(n.name) + ": its TriggerScript " + get_name(script) + " isn't a script";
            append_one(out, issue);
        }
        if (edit) {
            for (const std::string* name : {&edit->enter, &edit->exit}) {
                if (name->empty() || script::script_exists(get_checksums(*name)))
                    continue;
                issue.rule = Rule::kTriggerScript;
                issue.severity = Severity::kError;
                issue.level_own_ = false;
                issue.text = "Proximity node " + get_name(n.name) + ": its " +
                             (name == &edit->enter ? "enter" : "exit") + " script " + *name + " isn't a script";
                append_one(out, issue, *name);
            }
        }
    }
}

void check_online(std::vector<Issue>* out)
{
    const char* game = "";
    if (!nodes::level_is_shared(&game))
        return;
    for (const nodes::Elem& n : nodes::get_all()) {
        if (!nodes::get_online(n.family))
            continue;
        const bool hidden = n.hidden && !n.added;
        const bool added = n.added && !n.hidden;
        const bool moved = get_marked(n.name, ui::kMarkMoved);
        if (!hidden && !added && !moved)
            continue;
        Issue issue;
        issue.rule = Rule::kOnlineParity;
        issue.severity = Severity::kDetailsOf;
        issue.node = n.name;
        issue.has_pos_ = n.has_pos_;
        issue.pos = n.pos;
        issue.text = get_name(n.name) +
                     (added    ? " added"
                      : hidden ? " hidden"
                               : " moved") +
                     " in a level shared with " + "THUGPRO (" + game + ")";
        append_one(out, issue);
    }
}

MpCounts count_multiplayer(bool shipped)
{
    static std::array<std::array<uint32_t, 3>, 4> g_names;
    static bool made = false;
    if (!made) {
        for (int t = 0; t < 4; ++t) {
            const std::string colour = core::at(kTeamNames, t);
            core::at(core::at(g_names, t), 0) = get_checksums("TRG_CTF_" + colour);
            core::at(core::at(g_names, t), 1) = get_checksums("TRG_CTF_" + colour + "_Base");
            core::at(core::at(g_names, t), 2) = get_checksums("TRG_CTF_Restart_" + colour);
        }
        made = true;
    }
    static const uint32_t kCrown = get_checksums("Crown");
    MpCounts counts;
    for (const nodes::Elem& n : nodes::get_all()) {
        // (now: what the game sees - hidden ones not; as shipped: the level's own, hidden ones too)
        if (shipped ? n.added : n.hidden)
            continue;
        if (n.family == nodes::Family::kSpawn) {
            counts.player1 += (n.restart_types_ & nodes::kPlayer1) != 0;
            counts.multiplayer += (n.restart_types_ & nodes::kMultiplayer) != 0;
            counts.horse += (n.restart_types_ & nodes::kHorse) != 0;
        }
        if (n.type == kCrown)
            ++counts.crowns;
        for (int t = 0; t < 4; ++t) {
            core::at(counts.teams, t).flag = core::at(counts.teams, t).flag || n.name == core::at(core::at(g_names, t), 0);
            core::at(counts.teams, t).base = core::at(counts.teams, t).base || n.name == core::at(core::at(g_names, t), 1);
            core::at(counts.teams, t).restart = core::at(counts.teams, t).restart || n.name == core::at(core::at(g_names, t), 2);
        }
    }
    return counts;
}

void check_multiplayer(std::vector<Issue>* out)
{
    if (nodes::get_all().empty())
        return;
    const std::vector<MpProblem> now = multiplayer_problems(count_multiplayer(false));
    std::vector<MpProblem> shipped = multiplayer_problems(count_multiplayer(true));
    for (const MpProblem& p : now) {
        Issue issue;
        issue.rule = Rule::kMultiplayer;
        issue.severity = p.severity;
        issue.text = p.text;
        issue.level_own_ = std::ranges::any_of(shipped, [&](const MpProblem& s) { return s.text == p.text; });
        append_one(out, issue);
    }
}

void get_run()
{
    auto started = std::chrono::steady_clock::now();
    std::vector<Issue> issues;
    if (!nodes::get_all().empty()) {
        const uint32_t world = world_signature();
        check_placement(&issues, world);
        check_rails(&issues);
        check_collision(&issues);
        check_limits(&issues);
        check_triggers(&issues, world);
        check_online(&issues);
        check_multiplayer(&issues);
        for (const Issue& u : g_layout_issues)
            issues.push_back(u);
    }
    get_sort(&issues);
    g_issues = std::move(issues);
    g_checked_once = true;
    g_last_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (debug_enabled())
        get_log("level editor: problems checked - %u issues in %.1f ms", static_cast<unsigned>(g_issues.size()),
                g_last_seconds * 1000.0);
}

// --- fixes (undoable) -------------------------------------------------------------------------------------------

struct NodePlace : ModuleStep {
    uint32_t name;
    node_edit::Values values;
    NodePlace(uint32_t n, const node_edit::Values& v) : name(n), values(v) {}
    void get_restore() override
    {
        node_edit::assign_value(name, values);
        if (const int index = nodes::get_find(name); index >= 0)
            nodes::reread(index);
        g_due = get_seconds();
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        node_edit::Values v = values;
        node_edit::get_take_in(name, &v);
        return std::make_shared<NodePlace>(name, v);
    }
};

struct LayoutRecords : ModuleStep {
    std::vector<Issue> issues;
    explicit LayoutRecords(std::vector<Issue> list) : issues(std::move(list)) {}
    void get_restore() override
    {
        g_layout_issues = issues;
        g_due = get_seconds();
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        return std::make_shared<LayoutRecords>(g_layout_issues);
    }
};

void fix(const Issue& issue)
{
    switch (issue.rule) {
    case Rule::kBelowGround: {
        node_edit::Values values;
        if (!issue.node || !node_edit::get_take_in(issue.node, &values))
            return;
        push_module_undo(std::make_shared<NodePlace>(issue.node, values));
        values.pos = issue.fix_pos_;
        node_edit::assign_value(issue.node, values);
        if (const int index = nodes::get_find(issue.node); index >= 0)
            nodes::reread(index);
        hud::get_message(("Moved to surface: " + get_name(issue.node)).c_str());
        get_log("level editor: problems - %s put on the surface above it (%.0f, %.0f, %.0f)",
                get_name(issue.node).c_str(), issue.fix_pos_.x, issue.fix_pos_.y, issue.fix_pos_.z);
        break;
    }
    case Rule::kHiddenCollision:
        shared_collision::change_live(issue.sector, false);
        hud::get_message(("The collision taken along by " + get_name(issue.sector) + " turned off").c_str());
        break;
    case Rule::kLayoutRecord: {
        push_module_undo(std::make_shared<LayoutRecords>(g_layout_issues));
        std::erase_if(g_layout_issues, [&](const Issue& i) { return i.key == issue.key; });
        hud::get_message("Record removed");
        get_log("level editor: problems - layout record dropped: %s", issue.record.c_str());
        break;
    }
    default:
        break;
    }
    g_due = get_seconds();
}

} // namespace

// --- the layout's unresolved records ------------------------------------------------------------------------------

namespace {

// One record's line: a file of that record alone, written (its header and scene lines left out).
std::string line_of(const layout::FileLoad& one)
{
    std::istringstream in(layout::emit_out(one));
    std::string line, out;
    int number = 0;
    while (std::getline(in, line)) {
        if (++number <= 1 || line.starts_with("scene") || line.empty())
            continue;
        out += (out.empty() ? "" : "\n") + line;
    }
    return out;
}

} // namespace

void note_layout_applied(const layout::FileLoad& file)
{
    g_layout_issues.clear();
    auto node = [](uint32_t name) { return name && nodes::get_find(name) >= 0; };
    auto sector = [](uint32_t checksum) { return checksum && scenery::sector_of(checksum) != nullptr; };
    auto object = [&](uint32_t id) { return objects::find_object(id) != nullptr || node(id); };
    auto report = [&](uint32_t subject, const std::string& what, layout::FileLoad one) {
        one.scene = file.scene;
        Issue issue;
        issue.rule = Rule::kLayoutRecord;
        issue.severity = Severity::kError;
        issue.record = line_of(one);
        issue.fixable = !issue.record.empty();
        issue.text = what + " not found in the level: " + issue.record;
        issue.key = issue_key(Rule::kLayoutRecord, subject, issue.record);
        if (std::ranges::none_of(g_layout_issues, [&](const Issue& i) { return i.key == issue.key; }))
            g_layout_issues.push_back(issue);
    };
    for (const scenery::SectorState& s : file.sectors) {
        if (!sector(s.checksum)) {
            layout::FileLoad one;
            one.sectors.push_back(s);
            report(s.checksum, "Scenery piece " + get_name(s.checksum), one);
        }
    }
    for (const scenery::RailState& r : file.rails) {
        if (!node(r.name)) {
            layout::FileLoad one;
            one.rails.push_back(r);
            report(r.name, "Rail point " + get_name(r.name), one);
        }
    }
    for (const layout::CloneRecord& c : file.clones) {
        if (!sector(c.source)) {
            layout::FileLoad one;
            one.clones.push_back(c);
            report(c.source, "The source of a scenery copy, " + get_name(c.source), one);
        }
    }
    for (const layout::ObjectRecord& o : file.objects) {
        if (!object(o.id)) {
            layout::FileLoad one;
            one.objects.push_back(o);
            report(o.id, "Object " + get_name(o.id), one);
        }
    }
    for (const layout::CopyRecord& c : file.copies) {
        if (!objects::find_object(c.name) && !object(c.source)) {
            layout::FileLoad one;
            one.copies.push_back(c);
            report(c.name, "The source of object copy " + get_name(c.name) + ", " + get_name(c.source), one);
        }
    }
    for (const layout::SetRecord& s : file.nodes) {
        if (!node(s.name)) {
            layout::FileLoad one;
            one.nodes.push_back(s);
            report(s.name, "Node " + get_name(s.name) + " (moved / edited)", one);
        }
    }
    for (const uint32_t name : file.hidden) {
        if (!node(name)) {
            layout::FileLoad one;
            one.hidden.push_back(name);
            report(name, "Node " + get_name(name) + " (hidden)", one);
        }
    }
    for (const picking::Lock& lock : file.locks) {
        const bool there = lock.kind == picking::kObjects ? object(lock.id) : sector(lock.id);
        if (!there) {
            layout::FileLoad one;
            one.locks.push_back(lock);
            report(lock.id,
                   "Locked " + std::string(lock.kind == picking::kObjects ? "object " : "piece ") + get_name(lock.id),
                   one);
        }
    }
    for (const triggers::ProximEdit& p : file.proxims) {
        if (!node(p.node)) {
            layout::FileLoad one;
            one.proxims.push_back(p);
            report(p.node, "Proximity node " + get_name(p.node), one);
        }
    }
    for (const walls::WallState& w : file.walls) {
        if (!sector(w.checksum)) {
            layout::FileLoad one;
            one.walls.push_back(w);
            report(w.checksum, "Collision " + get_name(w.checksum), one);
        }
    }
    for (const faces::SectorFaces& f : file.faces) {
        if (!sector(f.checksum)) {
            layout::FileLoad one;
            one.faces.push_back(f);
            report(f.checksum, "Collision faces of " + get_name(f.checksum), one);
        }
    }
    for (const clusters::Membership& m : file.clusters) {
        if (!sector(m.sector)) {
            layout::FileLoad one;
            one.clusters.push_back(m);
            report(m.sector, "Trick-object cluster of " + get_name(m.sector), one);
        }
    }
    // The chains' edits of level nodes (links, dropped points, terrains, facings, flat grinds, speed-ups).
    const chains::State& chains = file.chains;
    for (const chains::LinkEdit& l : chains.links) {
        if (!node(l.name)) {
            layout::FileLoad one;
            one.chains.links.push_back(l);
            report(l.name, "Links of " + get_name(l.name), one);
        }
    }
    for (const uint32_t name : chains.dropped) {
        if (!node(name)) {
            layout::FileLoad one;
            one.chains.dropped.push_back(name);
            report(name, "Dropped point " + get_name(name), one);
        }
    }
    for (const chains::TerrainEdit& t : chains.terrains) {
        if (!node(t.name)) {
            layout::FileLoad one;
            one.chains.terrains.push_back(t);
            report(t.name, "Terrain of " + get_name(t.name), one);
        }
    }
    for (const chains::FacingEdit& f : chains.facings) {
        if (!node(f.name)) {
            layout::FileLoad one;
            one.chains.facings.push_back(f);
            report(f.name, "Facing of " + get_name(f.name), one);
        }
    }
    for (const uint32_t name : chains.grind_flat_) {
        if (!node(name)) {
            layout::FileLoad one;
            one.chains.grind_flat_.push_back(name);
            report(name, "Flat grind of " + get_name(name), one);
        }
    }
    for (const chains::SpeedEdit& s : chains.speeds) {
        if (!node(s.name)) {
            layout::FileLoad one;
            one.chains.speeds.push_back(s);
            report(s.name, "Rail speed-up of " + get_name(s.name), one);
        }
    }
    for (const entities::Pace& s : file.entities.speeds) {
        if (!node(s.name)) {
            layout::FileLoad one;
            one.entities.speeds.push_back(s);
            report(s.name, "Path speed of " + get_name(s.name), one);
        }
    }
    for (const entities::EmitterSound& e : file.entities.emitters) {
        if (!node(e.name)) {
            layout::FileLoad one;
            one.entities.emitters.push_back(e);
            report(e.name, "Sound emitter " + get_name(e.name), one);
        }
    }
    if (file.level.start && !node(file.level.start)) {
        layout::FileLoad one;
        one.level = file.level;
        report(file.level.start, "The level's start, restart " + get_name(file.level.start), one);
    }
    for (const Issue& i : g_layout_issues)
        get_log("level editor: layout record not found in the level - kept for the next save (the Problems window, "
                "Alt+P, drops it): %s",
                i.record.c_str());
    g_due = get_seconds();
}

void forget_layout_records()
{
    g_layout_issues.clear();
    g_due = get_seconds();
}

std::vector<std::string> carried_records(const std::string& captured)
{
    std::vector<std::string> lines;
    for (const Issue& i : g_layout_issues) {
        if (i.record.empty())
            continue;
        std::istringstream in(i.record);
        std::string line;
        while (std::getline(in, line))
            lines.push_back(line);
    }
    return lines.empty() ? lines : carried_lines(lines, captured);
}

const std::vector<Issue>& layout_issues() { return g_layout_issues; }

// --- the frame ----------------------------------------------------------------------------------------------------

void note_model(int changes, const std::unordered_map<uint32_t, uint8_t>& marks)
{
    const uint32_t generation = nodes::get_generation();
    const int loads = layout::level_loads();
    const double now = get_seconds();
    if (changes != g_changes || generation != g_generation || loads != g_loads || !g_checked_once) {
        const bool first = !g_checked_once || loads != g_loads;
        g_changes = changes;
        g_generation = generation;
        g_loads = loads;
        marks_state() = marks;
        // (a moment after a change: changes come in bursts - a drag, a typed field)
        g_due = first ? now : now + 0.4;
    }
    if (g_due >= 0.0 && now >= g_due) {
        marks_state() = marks;
        g_due = -1.0;
        get_run();
    }
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (const auto& action : queue)
        action();
}

void forget_level()
{
    placed_state().clear();
    g_issues.clear();
    g_layout_issues.clear();
    ignored_state().clear();
    marks_state().clear();
    g_checked_once = false;
    g_due = -1.0;
}

const std::vector<Issue>& get_issues() { return g_issues; }

namespace {

bool get_shown(const Issue& issue)
{
    return (g_show_own || !issue.level_own_) && (g_show_ignored || !ignored_state().count(issue.key));
}

} // namespace

void shown_counts(int* errors, int* warnings, int* infos)
{
    *errors = *warnings = *infos = 0;
    for (const Issue& issue : g_issues) {
        if (issue.level_own_ || ignored_state().count(issue.key))
            continue;
        (issue.severity == Severity::kError ? *errors : issue.severity == Severity::kWarning ? *warnings : *infos) += 1;
    }
}

void open_window(int tab)
{
    g_open = true;
    g_tab = tab;
    g_tab_pending = true;
    g_due = get_seconds(); // (opened: checked again now)
}

void toggle_window()
{
    if (g_open)
        g_open = false;
    else
        open_window(kProblemsTab);
}

// --- the UI -------------------------------------------------------------------------------------------------------

namespace {

ImVec4 severity_colour(Severity severity)
{
    switch (severity) {
    case Severity::kError:
        return {0.90f, 0.28f, 0.30f, 1.0f};
    case Severity::kWarning:
        return {0.96f, 0.65f, 0.14f, 1.0f};
    default:
        return {0.45f, 0.70f, 0.95f, 1.0f};
    }
}

void go_to(const Issue& issue)
{
    bool done = false;
    if (issue.node)
        done = go_to_node_named(issue.node);
    if (!done && issue.sector)
        done = go_to_scenery(issue.sector);
    if (!done && issue.has_pos_)
        go_to_point(issue.pos, 400.0f);
}

void problems_tab(float scale, ImFont* mono)
{
    (void)mono;
    if (ImGui::Button("Check now"))
        g_queue.emplace_back([] { g_due = get_seconds(); });
    ImGui::SameLine();
    int own = 0, ignored = 0;
    for (const Issue& issue : g_issues) {
        own += issue.level_own_;
        ignored += ignored_state().count(issue.key) != 0;
    }
    std::array<char, 64> label{};
    text::format_to(label, "Original Level (%d)", own);
    ImGui::Checkbox(label.data(), &g_show_own);
    if (ImGui::IsItemHovered())
        gui::tooltip("Problems present in the original level");
    ImGui::SameLine();
    text::format_to(label, "Ignored (%d)", ignored);
    ImGui::Checkbox(label.data(), &g_show_ignored);
    ImGui::SameLine();
    if (g_checked_once)
        gui::text_disabled("%.0f ms", g_last_seconds * 1000.0);
    else
        gui::text_disabled("not checked");
    ImGui::Separator();
    int shown = 0;
    for (const Issue& issue : g_issues)
        shown += get_shown(issue);
    if (!shown) {
        gui::text_colored(ImVec4(0.45f, 0.80f, 0.50f, 1.0f), "No problems");
        if (own)
            gui::text_disabled("(%d in the original level)", own);
        return;
    }
    if (!ImGui::BeginTable("##problems", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                           ImVec2(0.0f, 340.0f * scale)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 64.0f * scale);
    ImGui::TableSetupColumn("Problem", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f * scale);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < g_issues.size(); ++i) {
        const Issue& issue = core::at(g_issues, i);
        if (!get_shown(issue))
            continue;
        const bool is_ignored = ignored_state().count(issue.key) != 0;
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        gui::text_colored(severity_colour(issue.severity), "%s", severity_name(issue.severity));
        ImGui::TableNextColumn();
        if (is_ignored || issue.level_own_)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        gui::text_disabled("%s%s", rule_name(issue.rule), issue.level_own_ ? " (original)" : "");
        gui::text_wrapped("%s", issue.text.c_str());
        if (is_ignored || issue.level_own_)
            ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        const bool can_go = issue.node || issue.sector || issue.has_pos_;
        ImGui::BeginDisabled(!can_go);
        if (ImGui::SmallButton("Go to")) {
            const Issue copy = issue;
            g_queue.emplace_back([copy] { go_to(copy); });
        }
        ImGui::EndDisabled();
        if (issue.fixable) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Fix")) {
                const Issue copy = issue;
                g_queue.emplace_back([copy] { fix(copy); });
            }
            if (ImGui::IsItemHovered())
                gui::tooltip("%s", issue.rule == Rule::kBelowGround       ? "Move to the surface above"
                                        : issue.rule == Rule::kHiddenCollision ? "Turn the collision off"
                                        : issue.rule == Rule::kLayoutRecord
                                            ? "Drop the record: the next save leaves it out "
                                              "(undoable)"
                                            : "");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(is_ignored ? "Unignore" : "Ignore")) {
            if (is_ignored)
                ignored_state().erase(issue.key);
            else
                ignored_state().insert(issue.key);
        }
        if (ImGui::IsItemHovered())
            gui::tooltip("Ignore until the level reloads");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

void pane(float view_x, float view_y, float view_width, float view_height, float scale, ImFont* mono)
{
    (void)view_height;
    if (!g_open)
        return;
    ImGui::SetNextWindowPos(ImVec2(view_x + view_width * 0.5f, view_y + 48.0f * scale), ImGuiCond_FirstUseEver,
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
        shown_counts(&errors, &warnings, &infos);
        std::array<char, 64> label{};
        text::format_to(label, "Problems (%d)###problemsTab", errors + warnings + infos);
        const ImGuiTabItemFlags problems_flags =
            g_tab_pending && g_tab == kProblemsTab ? ImGuiTabItemFlags_SetSelected : 0;
        const ImGuiTabItemFlags budget_flags = g_tab_pending && g_tab == kBudgetTab ? ImGuiTabItemFlags_SetSelected : 0;
        g_tab_pending = false;
        if (ImGui::BeginTabItem(label.data(), nullptr, problems_flags)) {
            g_tab = kProblemsTab;
            problems_tab(scale, mono);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Budget###budgetTab", nullptr, budget_flags)) {
            g_tab = kBudgetTab;
            budget::get_table();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    g_open = open;
}

} // namespace frontend::level_editor::checks
