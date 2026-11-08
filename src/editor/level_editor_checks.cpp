// NOT ORIGINAL - the level editor's problem checker (ours). See level_editor_checks.h.

#include "editor/level_editor_checks.h"

#include <SDL3/SDL.h>

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

#include "base/core/enum_ops.h"
#include "editor/level_editor.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_ui.h"
#include "base/core/checksums.h"
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
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "platform/sdl_time.h"
#include "imgui.h"
#include "engine/gfx/sector.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_ui_app.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::checks {

namespace {

constexpr uint32_t kFnvOffsetBasis = 2166136261U; // FNV-1a
constexpr uint32_t kFnvPrime = 16777619U;
constexpr int kBitsPerByte = 8;
constexpr int kProbeDirections = 6;  // the six axis directions
constexpr int kMaxCastSteps = 6;     // surfaces followed along one cast at most
constexpr float kSameSpotDistance = 0.5F; // two hits closer than this are one
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr double kNearLimitFraction = 0.9; // a meter at this share of its limit is near it
constexpr double kRecheckDelaySeconds = 0.4;
constexpr float kGoToDistance = 400.0F; // how far from an issue the camera stops
constexpr std::size_t kLabelTextSize = 64;
constexpr float kListHeight = 340.0F; // the issue list's height (before the UI scale)
constexpr float kKindColumnWidth = 64.0F;
constexpr float kWhereColumnWidth = 150.0F;
constexpr float kWindowTopOffset = 48.0F; // the window's first position: below the view's top edge
constexpr float kWindowPivotX = 0.5F;     // centred on its position
constexpr float kWindowWidth = 620.0F;
constexpr ImVec4 kErrorColour = ImVec4(0.90F, 0.28F, 0.30F, 1.0F);
constexpr ImVec4 kWarningColour = ImVec4(0.96F, 0.65F, 0.14F, 1.0F);
constexpr ImVec4 kInfoColour = ImVec4(0.45F, 0.70F, 0.95F, 1.0F);
constexpr ImVec4 kNoProblemsColour = ImVec4(0.45F, 0.80F, 0.50F, 1.0F);

} // namespace

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
    uint32_t hash = kFnvOffsetBasis;
    auto mix = [&](uint8_t byte) { hash = (hash ^ byte) * kFnvPrime; };
    mix(static_cast<uint8_t>(rule));
    for (int i = 0; i < 4; ++i) {
        mix(static_cast<uint8_t>(subject >> (kBitsPerByte * i)));
    }
    for (const char character : detail) {
        mix(static_cast<uint8_t>(character));
    }
    return hash;
}

void get_sort(std::vector<Issue>* issues)
{
    std::ranges::stable_sort(*issues, [](const Issue& a, const Issue& b) {
        if (a.severity != b.severity) {
            return a.severity < b.severity;
        }
        if (a.level_own_ != b.level_own_) {
            return !a.level_own_;
        }
        if (a.rule != b.rule) {
            return a.rule < b.rule;
        }
        return a.text < b.text;
    });
}

bool get_behind(const Probe& probe) { return probe.in >= 0.0F && (probe.out < 0.0F || probe.in < probe.out); }

Place get_judge(std::span<const Probe, kProbeCount> probes)
{
    for (int axis = 0; axis < 3; ++axis) {
        if (get_behind(core::at(probes, 2 * axis)) && get_behind(core::at(probes, (2 * axis) + 1))) {
            return Place::kInsideSolid;
        }
    }
    return get_behind(core::at(probes, 2)) ? Place::kBelowGround : Place::kClear;
}

bool floating(std::span<const Probe, kProbeCount> probes, float near)
{
    for (int i = 0; i < kProbeDirections; ++i) {
        if ((core::at(probes, i).out >= 0.0F && core::at(probes, i).out <= near) ||
            (core::at(probes, i).in >= 0.0F && core::at(probes, i).in <= near)) {
            return false;
        }
    }
    return true;
}

std::vector<int> lonely_points(const std::vector<std::vector<int>>& links)
{
    std::vector<int> incoming(links.size(), 0);
    for (const std::vector<int>& out : links) {
        for (const int to : out) {
            if (to >= 0 && std::cmp_less(to, links.size())) {
                ++core::at(incoming, to);
            }
        }
    }
    std::vector<int> lonely;
    for (size_t i = 0; i < links.size(); ++i) {
        const bool links_out = std::any_of(core::at(links, i).begin(), core::at(links, i).end(),
                                           [&](int to) { return to >= 0 && std::cmp_less(to, links.size()); });
        if (!links_out && core::at(incoming, i) == 0) {
            lonely.push_back(static_cast<int>(i));
        }
    }
    return lonely;
}

namespace {

const std::array<const char*, 4> kTeamNames = {{"Red", "Blue", "Green", "Yellow"}};

} // namespace

std::vector<MpProblem> multiplayer_problems(const MpCounts& counts)
{
    std::vector<MpProblem> out;
    if (counts.player1 == 0) {
        out.push_back({.severity = Severity::kError, .text = "No Player1 restart", .team = -1});
    }
    if (counts.multiplayer == 0) {
        out.push_back({.severity = Severity::kWarning, .text = "No Multiplayer restart", .team = -1});
    } else if (counts.multiplayer == 1) {
        out.push_back({.severity = Severity::kDetailsOf, .text = "Only one Multiplayer restart", .team = -1});
    }
    if (counts.horse == 0) {
        out.push_back({.severity = Severity::kWarning, .text = "No HORSE restart", .team = -1});
    }
    if (counts.crowns == 0) {
        out.push_back({.severity = Severity::kWarning, .text = "King of the Hill: no crowns", .team = -1});
    }
    bool any_team = false;
    for (const TeamNodes& team_nodes : counts.teams) {
        any_team = any_team || team_nodes.flag || team_nodes.base || team_nodes.restart;
    }
    if (!any_team) {
        out.push_back({.severity = Severity::kWarning, .text = "No Capture the Flag flags, bases or team restarts", .team = -1});
        return out;
    }
    for (int team = 0; team < 4; ++team) {
        const TeamNodes& team_nodes = core::at(counts.teams, team);
        const bool any = team_nodes.flag || team_nodes.base || team_nodes.restart;
        // (Red and Blue always play; Green and Yellow only with 3 / 4 teams - complete or not there at all)
        if (team >= 2 && !any) {
            continue;
        }
        if (team_nodes.flag && team_nodes.base && team_nodes.restart) {
            continue;
        }
        std::string missing;
        auto add = [&](bool have, const std::string& what) {
            if (!have) {
                missing += (missing.empty() ? "" : ", ") + what;
            }
        };
        const std::string colour = core::at(kTeamNames, team);
        add(team_nodes.flag, "its flag (TRG_CTF_" + colour + ")");
        add(team_nodes.base, "its base (TRG_CTF_" + colour + "_Base)");
        add(team_nodes.restart, "its team restart (TRG_CTF_Restart_" + colour + ")");
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
    std::string word;
    std::string key;
    int words = 0;
    int want = 2;
    while (words < want && in >> word) {
        if (words == 0) {
            if (word == "lock" || word == "order") {
                want = 3;
            } else if (word == "level") {
                want = 1;
            }
        }
        key += ((words != 0) ? " " : "") + word;
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
        if (!key.empty()) {
            keys.insert(key);
        }
    }
    std::vector<std::string> out;
    for (const std::string& u : unresolved) {
        if (keys.contains(record_key(u)) || std::ranges::find(out, u) != out.end()) {
            continue;
        }
        out.push_back(u);
    }
    return out;
}

// --- the game side ------------------------------------------------------------------------------------------------

bool get_cast(V3 from, V3 to, SurfaceOut surface, uint32_t* sector)
{
    auto* const hit = surface.point;
    auto* const normal = surface.normal;
    // (a ProbeRay: constructor 0x00417760; 0x00417710 casts it - thiscall, true on a hit; the hit's normal at +0x24, its
    // point at +0x34, the collision object at +0x54 and its checksum at +0x58 - as the editor's other modules read it)
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& hit_normal = ray->hit().normal;
    const auto& hit_point = ray->hit().point;
    *hit = {.x = hit_point[0], .y = hit_point[1], .z = hit_point[2]};
    if (normal) {
        *normal = {.x = hit_normal[0], .y = hit_normal[1], .z = hit_normal[2]};
    }
    if (sector) {
        *sector = ray->hit().object ? ray->hit().object_checksum_ : 0;
    }
    return true;
}

namespace {

float span_to(V3 a, V3 b) { return std::sqrt(get_dot(a - b, a - b)); }

const std::array<V3, 6> kDirections = {{{.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0}, {.x = 0, .y = 1, .z = 0}, {.x = 0, .y = -1, .z = 0}, {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}}};

} // namespace

void probe_each(V3 point, std::span<const float, kProbeCount> reach, std::span<Probe, kProbeCount> probes)
{
    for (int i = 0; i < kProbeDirections; ++i) {
        const V3 far = point + core::at(kDirections, i) * core::at(reach, i);
        V3 hit{};
        core::at(probes, i) = {};
        if (get_cast(point, far, {.point = &hit})) {
            core::at(probes, i).out = span_to(hit, point);
        }
        // Coming back in: the face facing away nearest to the point - the last met on the way in (cast again from just
        // past each hit; a few at most).
        V3 from = far;
        for (int step = 0; step < kMaxCastSteps && get_cast(from, point, {.point = &hit}); ++step) {
            core::at(probes, i).in = span_to(hit, point);
            const V3 on = hit + (point - far) * (0.5F / core::at(reach, i)); // (half a unit on, towards the point)
            if (span_to(on, point) < kSameSpotDistance) {
                break;
            }
            from = on;
        }
    }
}

void probe_around(V3 point, float reach, std::span<Probe, kProbeCount> probes)
{
    const std::array<float, 6> reaches = {{reach, reach, reach, reach, reach, reach}};
    probe_each(point, reaches, probes);
}

namespace {

constexpr float kHalf = 0.5F; // half

// --- the state ----------------------------------------------------------------------------------------------------

// NOT ORIGINAL grouping - level_editor_checks.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_checks_state()).
struct LevelEditorChecksState {
    std::vector<Issue> issues_; // the last check's
    std::vector<Issue> layout_issues_; // the loaded layout's unresolved records
    int changes_ = -1;
    uint32_t generation_ = 0;
    int loads_ = -1;
    double due_ = -1.0; // (seconds: when to check again; -1 none due)
    bool checked_once_ = false;
    double last_seconds_ = 0.0; // (how long the last check took)
    bool open_ = false;
    int tab_ = kProblemsTab;
    bool tab_pending_ = false;
    bool show_own_ = false;
    bool show_ignored_ = false;
    std::vector<std::function<void()>> queue_;
};

LevelEditorChecksState& level_editor_checks_state()
{
    static core::NoDestroy<LevelEditorChecksState> state;
    return state.get();
}

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

double get_seconds() { return static_cast<double>(platform::ticks_ms()) / kMillisecondsPerSecond; }

uint32_t get_checksums(const std::string& text) { return checksums::hash_from_text(text.c_str()); }

std::string get_name(uint32_t checksum) { return nodes::caption(checksum); }

bool get_marked(checksums::Name node_name, uint8_t bits)
{
    const uint32_t name = node_name.value();
    auto it = marks_state().find(name);
    return it != marks_state().end() && ((it->second & bits) != 0);
}

// Whether the editor changed scenery near a point (moved pieces, copies): then a problem there isn't the level's own.
bool scenery_changed_near(V3 point, float margin)
{
    std::vector<uint32_t> pieces;
    for (const scenery::SectorState& sector_state : scenery::changed_sectors()) {
        pieces.push_back(sector_state.checksum);
    }
    for (const scenery::Replicate& replicate : scenery::get_clones()) {
        pieces.push_back(replicate.checksum);
    }
    for (const uint32_t piece : pieces) {
        OrientedBox box{};
        if (!scenery::cuboid(piece, &box)) {
            continue;
        }
        V3 min{};
        V3 max{};
        bounds_of(box, &min, &max);
        if (point.x >= min.x - margin && point.x <= max.x + margin && point.y >= min.y - margin &&
            point.y <= max.y + margin && point.z >= min.z - margin && point.z <= max.z + margin) {
            return true;
        }
    }
    return false;
}

void append_one(std::vector<Issue>* out, Issue issue, const std::string& detail = "")
{
    issue.key =
        issue_key(issue.rule, (issue.node != 0U) ? issue.node : issue.sector, detail.empty() ? issue.text : detail);
    out->push_back(std::move(issue));
}

// --- the rules ----------------------------------------------------------------------------------------------------

// The placement probes' verdict for a point, kept while neither it nor the level's collision changes (`world`).
struct Placed {
    V3 point = {};
    uint32_t world = 0;
    Place place = Place::kClear;
    float up_in_ = -1.0F; // (the surface above, from the point - kBelowGround's Fix)
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
    uint32_t hash = kFnvOffsetBasis;
    auto mix = [&](const void* data, size_t size) {
        for (const uint8_t byte : std::span<const uint8_t>(static_cast<const uint8_t*>(data), size)) {
            hash = (hash ^ byte) * kFnvPrime;
        }
    };
    for (const scenery::SectorState& sector_state : scenery::changed_sectors()) {
        mix(&sector_state.checksum, sizeof(sector_state.checksum));
        mix(&sector_state.placement.quarters, sizeof(sector_state.placement.quarters));
        mix(&sector_state.placement.twist, sizeof(sector_state.placement.twist));
        mix(&sector_state.placement.tilt_x, sizeof(sector_state.placement.tilt_x));
        mix(&sector_state.placement.tilt_z, sizeof(sector_state.placement.tilt_z));
        mix(&sector_state.placement.offset, sizeof(sector_state.placement.offset));
        mix(&sector_state.placement.scale, sizeof(sector_state.placement.scale));
        mix(&sector_state.hidden, sizeof(sector_state.hidden));
    }
    for (const walls::WallState& w : walls::get_changed()) {
        mix(&w.checksum, sizeof(w.checksum));
        mix(&w.placement.quarters, sizeof(w.placement.quarters));
        mix(&w.placement.offset, sizeof(w.placement.offset));
        mix(&w.off, sizeof(w.off));
    }
    size_t clones = scenery::get_clones().size();
    size_t faces = faces::get_changed().size();
    uint32_t shared = shared_collision::get_generation();
    mix(&clones, sizeof(clones));
    mix(&faces, sizeof(faces));
    mix(&shared, sizeof(shared));
    return hash | 1U; // (never 0: 0 is "not probed")
}

const Placed& placed_at(uint32_t name, V3 point, uint32_t world)
{
    Placed& placed = placed_state()[name];
    if (placed.world == world && placed.point.x == point.x && placed.point.y == point.y && placed.point.z == point.z) {
        return placed;
    }
    // (the sides only kSideReach: a wall's thickness - farther faces belong to other things; up and down kReach)
    const std::array<float, 6> reaches = {{kSideReach, kSideReach, kReach, kReach, kSideReach, kSideReach}};
    std::array<Probe, kProbeCount> probes;
    probe_each(point, reaches, probes);
    placed.point = point;
    placed.world = world;
    placed.place = get_judge(probes);
    placed.up_in_ = core::at(probes, 2).in;
    V3 hit{};
    placed.ground = core::at(probes, 3).out >= 0.0F ||
                    get_cast(point, point - V3{.x = 0.0F, .y = kGroundReach, .z = 0.0F}, {.point = &hit});
    return placed;
}

void check_placement(std::vector<Issue>* out, uint32_t world)
{
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.hidden || !elem.has_pos_) {
            continue;
        }
        if (elem.family != nodes::Family::kSpawn && elem.family != nodes::Family::kCrown &&
            elem.family != nodes::Family::kBitMark) {
            continue;
        }
        const V3 point = elem.pos + V3{.x = 0.0F, .y = kLift, .z = 0.0F};
        const Placed& placed = placed_at(elem.name, point, world);
        const Place place = placed.place;
        const bool own = !get_marked(checksums::Name{elem.name}, ui::kMarkMoved | ui::kMarkAdded) && !elem.added &&
                         !scenery_changed_near(elem.pos, 300.0F);
        std::string what = std::string(nodes::family_name(elem.family));
        what = what.substr(0, what.size() - 1); // ("Spawns" -> "Spawn")
        Issue issue;
        issue.node = elem.name;
        issue.has_pos_ = true;
        issue.pos = elem.pos;
        issue.level_own_ = own;
        if (place == Place::kInsideSolid) {
            issue.rule = Rule::kInsideSolid;
            issue.severity = Severity::kError;
            issue.text = what + " " + get_name(elem.name) + " is inside solid collision";
            append_one(out, issue);
        } else if (place == Place::kBelowGround) {
            issue.rule = Rule::kBelowGround;
            issue.severity = Severity::kError;
            issue.text = what + " " + get_name(elem.name) + " is under a surface (" +
                         std::to_string(static_cast<int>(placed.up_in_ - kLift)) + " units below the surface)";
            issue.fixable = node_edit::get_movable(elem);
            issue.fix_pos_ = point + V3{.x = 0.0F, .y = placed.up_in_, .z = 0.0F};
            append_one(out, issue, "below"); // (the key without the depth: Ignore holds while it's moved about)
        } else {
            if (!placed.ground) {
                issue.rule = Rule::kNoGround;
                issue.severity = Severity::kWarning;
                issue.text = what + " " + get_name(elem.name) + ": nothing below it within " +
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
    for (size_t rail = 0; rail < rails.size(); ++rail) {
        for (const int to : core::at(nodes, core::at(rails, rail)).links) {
            if (to >= 0 && std::cmp_less(to, nodes.size()) && core::at(index, to) >= 0) {
                core::at(links, rail).push_back(core::at(index, to));
            }
        }
    }
    for (const int rail : lonely_points(links)) {
        const nodes::Elem& elem = core::at(nodes, core::at(rails, rail));
        Issue issue;
        issue.rule = Rule::kLonelyRail;
        issue.severity = Severity::kWarning;
        issue.node = elem.name;
        issue.has_pos_ = elem.has_pos_;
        issue.pos = elem.pos;
        issue.level_own_ = !elem.added && !get_marked(checksums::Name{elem.name}, ui::kMarkAdded);
        issue.text = "Rail point " + get_name(elem.name) + " is not linked";
        append_one(out, issue);
    }
    // Floating: the rail points the editor moved or added.
    for (const int rail_index : rails) {
        const nodes::Elem& elem = core::at(nodes, rail_index);
        if (!elem.added && !get_marked(checksums::Name{elem.name}, ui::kMarkMoved | ui::kMarkAdded)) {
            continue;
        }
        V3 pos = elem.pos;
        scenery::get_rail_point(elem.name, &pos);
        std::array<Probe, kProbeCount> probes;
        probe_around(pos, kRailNear, probes);
        if (!floating(probes, kRailNear)) {
            continue;
        }
        Issue issue;
        issue.rule = Rule::kFloatingRail;
        issue.severity = Severity::kWarning;
        issue.node = elem.name;
        issue.has_pos_ = true;
        issue.pos = pos;
        issue.text = "Rail point " + get_name(elem.name) + " has no surface within " +
                     std::to_string(static_cast<int>(kRailNear)) + " units";
        append_one(out, issue);
    }
}

void check_collision(std::vector<Issue>* out)
{
    for (const scenery::SectorState& sector_state : scenery::changed_sectors()) {
        const render::WorldCell* sector = scenery::sector_of(sector_state.checksum);
        if (!sector) {
            continue;
        }
        const shared_collision::Companion* companion = shared_collision::companion_of(sector_state.checksum);
        Issue issue;
        issue.sector = sector_state.checksum;
        OrientedBox box{};
        if (scenery::cuboid(sector_state.checksum, &box)) {
            V3 min{};
            V3 max{};
            bounds_of(box, &min, &max);
            issue.has_pos_ = true;
            issue.pos = (min + max) * kHalf;
        }
        if (sector_state.hidden) {
            if (companion && companion->live_now()) {
                issue.rule = Rule::kHiddenCollision;
                issue.severity = Severity::kError;
                issue.text = "Scenery " + get_name(sector_state.checksum) + " is hidden but its collision is still on";
                issue.fixable = true;
                append_one(out, issue);
            }
            continue;
        }
        const bool moved = is_turned(sector_state.placement) ||
                           !is_unit_scale(sector_state.placement.scale) ||
                           sector_state.placement.offset.x != 0.0F || sector_state.placement.offset.y != 0.0F ||
                           sector_state.placement.offset.z != 0.0F;
        const bool own = sector->hit_body() != nullptr; // (its collision object - WorldCell +0x18)
        if (moved && !own && !companion && scenery::sector_geometry(sector_state.checksum)) {
            issue.rule = Rule::kCollisionLeft;
            issue.severity = Severity::kDetailsOf;
            issue.text =
                "Scenery " + get_name(sector_state.checksum) + " was moved but its collision may not have moved";
            append_one(out, issue);
        }
    }
}

void check_limits(std::vector<Issue>* out)
{
    for (const budget::Meter& meter : budget::get_meters()) {
        if (meter.limit <= 0.0 || meter.bytes) {
            continue;
        }
        Severity severity{};
        if (meter.used > meter.limit) {
            severity = Severity::kError;
        } else if (meter.used >= kNearLimitFraction * meter.limit) {
            severity = Severity::kWarning;
        } else {
            continue;
        }
        const double before = meter.used - meter.added;
        Issue issue;
        issue.rule = Rule::kLimit;
        issue.severity = severity;
        issue.level_own_ =
            severity == Severity::kError ? before > meter.limit : before >= kNearLimitFraction * meter.limit;
        issue.text = std::string(meter.name) + ": " + budget::used_text(meter) +
                     (severity == Severity::kError ? " - over the engine's limit (" : " - near the engine's limit (") +
                     meter.where + ")";
        append_one(out, issue, meter.name);
    }
}

// The editor's trigger volumes: one doing nothing yet, its middle inside solid collision or under a surface, a
// gap end without its start or a start without its end.
void check_added_triggers(std::vector<Issue>* out, uint32_t world)
{
    // The editor's trigger volumes.
    const std::vector<triggers::Added>& added = triggers::added_triggers();
    std::vector<triggers::Spec> specs;
    specs.reserve(added.size());
    for (const triggers::Added& added_trigger : added) {
        specs.push_back(added_trigger.spec);
    }
    const std::vector<triggers::LevelTrigger>& level = triggers::level_triggers();
    for (size_t i = 0; i < added.size(); ++i) {
        const triggers::Added& added_trigger = core::at(added, i);
        Issue issue;
        issue.node = triggers::checksum_of(added_trigger.spec.name);
        issue.has_pos_ = added_trigger.alive;
        issue.pos = added_trigger.pos;
        if (added_trigger.problem) {
            issue.rule = Rule::kTriggerIdle;
            issue.severity = Severity::kWarning;
            issue.text = std::string(triggers::kind_name(added_trigger.spec.kind)) + " " + added_trigger.spec.name +
                         " does nothing yet: " + added_trigger.problem;
            append_one(out, issue);
        }
        if (added_trigger.alive) {
            const Place place = placed_at(issue.node, added_trigger.pos, world).place;
            if (place != Place::kClear) {
                issue.rule = Rule::kTriggerIdle;
                issue.severity = Severity::kDetailsOf;
                issue.text = std::string(triggers::kind_name(added_trigger.spec.kind)) + " " + added_trigger.spec.name +
                             ": its middle is " +
                             (place == Place::kInsideSolid ? "inside solid collision" : "under a surface") + "";
                append_one(out, issue, "reach");
            }
        }
        // Gaps: an end for each start, a start for each end (the editor's or a level trigger's with the GapID).
        bool start = added_trigger.spec.kind == triggers::Kind::kGapStart;
        bool const end = added_trigger.spec.kind == triggers::Kind::kGapEnd;
        if ((start || end) && triggers::partner(specs, static_cast<int>(i)) < 0) {
            uint32_t gap = triggers::checksum_of(added_trigger.spec.gap);
            const bool found = std::ranges::any_of(level, [&](const triggers::LevelTrigger& level_trigger) {
                return level_trigger.calls.gap_id_ == gap &&
                       (start ? level_trigger.calls.end_gap_ : level_trigger.calls.start_gap_);
            });
            if (!found) {
                issue.rule = Rule::kGapUnpaired;
                issue.severity = Severity::kWarning;
                issue.text = start
                                 ? "Gap start " + added_trigger.spec.name + " (" + added_trigger.spec.gap + "): no end"
                                 : "Gap end " + added_trigger.spec.name + " (" + added_trigger.spec.gap + "): no start";
                append_one(out, issue);
            }
        }
    }
}

// The level's trigger volumes: faces without a node, no TriggerScript or one that isn't a script.
void check_level_triggers(std::vector<Issue>* out, const std::vector<triggers::LevelTrigger>& level)
{
    // The level's trigger volumes.
    for (const triggers::LevelTrigger& level_trigger : level) {
        if (level_trigger.off) {
            continue;
        }
        Issue issue;
        issue.sector = level_trigger.sector;
        issue.has_pos_ = true;
        issue.pos = (level_trigger.min + level_trigger.max) * kHalf;
        issue.level_own_ = true;
        if (!level_trigger.has_node_) {
            issue.rule = Rule::kTriggerIdle;
            issue.severity = Severity::kDetailsOf;
            issue.text = "Trigger faces of " + get_name(level_trigger.sector) + " have no node";
            append_one(out, issue);
        } else if (level_trigger.script == 0U) {
            issue.node = level_trigger.sector;
            issue.rule = Rule::kTriggerScript;
            issue.severity = Severity::kDetailsOf;
            issue.text = "Trigger " + get_name(level_trigger.sector) + " has no TriggerScript";
            append_one(out, issue);
        } else if (!script::script_exists(level_trigger.script)) {
            issue.node = level_trigger.sector;
            issue.rule = Rule::kTriggerScript;
            issue.severity = Severity::kError;
            issue.text = "Trigger " + get_name(level_trigger.sector) + ": its TriggerScript " +
                         get_name(level_trigger.script) + " isn't a script";
            append_one(out, issue);
        }
    }
}

// Proximity nodes (not the editor's trigger volumes, which are GenericNodes of the same family): no script, or
// scripts that aren't.
void check_proxims(std::vector<Issue>* out)
{
    static const uint32_t kTriggerScript = get_checksums("TriggerScript");
    static const uint32_t kProximNode = get_checksums("ProximNode");
    // Proximity nodes (not the editor's trigger volumes, which are GenericNodes of the same family).
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.family != nodes::Family::kFireOff || elem.hidden || elem.cls != kProximNode ||
            triggers::is_trigger(elem.name)) {
            continue;
        }
        const bool own = !elem.added && !get_marked(checksums::Name{elem.name}, ui::kMarkMoved | ui::kMarkAdded);
        Issue issue;
        issue.node = elem.name;
        issue.has_pos_ = elem.has_pos_;
        issue.pos = elem.pos;
        uint32_t script = 0;
        script::Struct* structure = nodes::struct_of(elem);
        if (structure) {
            structure->read_hash_value(kTriggerScript, &script, 0);
        }
        const triggers::ProximEdit* edit = triggers::proxim_of(elem.name);
        const bool edited = (edit != nullptr) && (!edit->enter.empty() || !edit->exit.empty());
        if ((script == 0U) && !edited) {
            issue.rule = Rule::kTriggerScript;
            issue.severity = own ? Severity::kDetailsOf : Severity::kWarning;
            issue.level_own_ = own;
            issue.text = "Proximity node " + get_name(elem.name) + " has no TriggerScript";
            append_one(out, issue);
        } else if ((script != 0U) && !script::script_exists(script)) {
            issue.rule = Rule::kTriggerScript;
            issue.severity = Severity::kError;
            issue.level_own_ = own;
            issue.text =
                "Proximity node " + get_name(elem.name) + ": its TriggerScript " + get_name(script) + " isn't a script";
            append_one(out, issue);
        }
        if (edit) {
            for (const std::string* name : {&edit->enter, &edit->exit}) {
                if (name->empty() || script::script_exists(get_checksums(*name))) {
                    continue;
                }
                issue.rule = Rule::kTriggerScript;
                issue.severity = Severity::kError;
                issue.level_own_ = false;
                issue.text = "Proximity node " + get_name(elem.name) + ": its " +
                             (name == &edit->enter ? "enter" : "exit") + " script " + *name + " isn't a script";
                append_one(out, issue, *name);
            }
        }
    }
}

void check_triggers(std::vector<Issue>* out, uint32_t world)
{
    check_added_triggers(out, world);
    const std::vector<triggers::LevelTrigger>& level = triggers::level_triggers();
    check_level_triggers(out, level);
    check_proxims(out);
}

void check_online(std::vector<Issue>* out)
{
    const char* game = "";
    if (!nodes::level_is_shared(&game)) {
        return;
    }
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (!nodes::get_online(elem.family)) {
            continue;
        }
        const bool hidden = elem.hidden && !elem.added;
        const bool added = elem.added && !elem.hidden;
        const bool moved = get_marked(checksums::Name{elem.name}, ui::kMarkMoved);
        if (!hidden && !added && !moved) {
            continue;
        }
        Issue issue;
        issue.rule = Rule::kOnlineParity;
        issue.severity = Severity::kDetailsOf;
        issue.node = elem.name;
        issue.has_pos_ = elem.has_pos_;
        issue.pos = elem.pos;
        const char* change = " moved";
        if (added) {
            change = " added";
        } else if (hidden) {
            change = " hidden";
        }
        issue.text = get_name(elem.name) + change + " in a level shared with " + "THUGPRO (" + game + ")";
        append_one(out, issue);
    }
}

MpCounts count_multiplayer(bool shipped)
{
    static std::array<std::array<uint32_t, 3>, 4> g_names;
    static bool made = false;
    if (!made) {
        for (int team = 0; team < 4; ++team) {
            const std::string colour = core::at(kTeamNames, team);
            core::at(core::at(g_names, team), 0) = get_checksums("TRG_CTF_" + colour);
            core::at(core::at(g_names, team), 1) = get_checksums("TRG_CTF_" + colour + "_Base");
            core::at(core::at(g_names, team), 2) = get_checksums("TRG_CTF_Restart_" + colour);
        }
        made = true;
    }
    static const uint32_t kCrown = get_checksums("Crown");
    MpCounts counts;
    for (const nodes::Elem& elem : nodes::get_all()) {
        // (now: what the game sees - hidden ones not; as shipped: the level's own, hidden ones too)
        if (shipped ? elem.added : elem.hidden) {
            continue;
        }
        if (elem.family == nodes::Family::kSpawn) {
            counts.player1 += static_cast<int>(core::any(elem.restart_types_ & nodes::SpawnPointKind::kPlayer1));
            counts.multiplayer +=
                static_cast<int>(core::any(elem.restart_types_ & nodes::SpawnPointKind::kMultiplayer));
            counts.horse += static_cast<int>(core::any(elem.restart_types_ & nodes::SpawnPointKind::kHorse));
        }
        if (elem.type == kCrown) {
            ++counts.crowns;
        }
        for (int team = 0; team < 4; ++team) {
            core::at(counts.teams, team).flag =
                core::at(counts.teams, team).flag || elem.name == core::at(core::at(g_names, team), 0);
            core::at(counts.teams, team).base =
                core::at(counts.teams, team).base || elem.name == core::at(core::at(g_names, team), 1);
            core::at(counts.teams, team).restart =
                core::at(counts.teams, team).restart || elem.name == core::at(core::at(g_names, team), 2);
        }
    }
    return counts;
}

void check_multiplayer(std::vector<Issue>* out)
{
    if (nodes::get_all().empty()) {
        return;
    }
    const std::vector<MpProblem> now = multiplayer_problems(count_multiplayer(false));
    std::vector<MpProblem> shipped = multiplayer_problems(count_multiplayer(true));
    for (const MpProblem& problem : now) {
        Issue issue;
        issue.rule = Rule::kMultiplayer;
        issue.severity = problem.severity;
        issue.text = problem.text;
        issue.level_own_ = std::ranges::any_of(
            shipped, [&](const MpProblem& shipped_problem) { return shipped_problem.text == problem.text; });
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
        for (const Issue& u : level_editor_checks_state().layout_issues_) {
            issues.push_back(u);
        }
    }
    get_sort(&issues);
    level_editor_checks_state().issues_ = std::move(issues);
    level_editor_checks_state().checked_once_ = true;
    level_editor_checks_state().last_seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (debug_enabled()) {
        get_log("level editor: problems checked - %u issues in %.1f ms", static_cast<unsigned>(level_editor_checks_state().issues_.size()),
                level_editor_checks_state().last_seconds_ * kMillisecondsPerSecond);
    }
}

// --- fixes (undoable) -------------------------------------------------------------------------------------------

struct NodePlace : ModuleStep {
private:
    uint32_t name;
    node_edit::Values values;

public:
    NodePlace(uint32_t elem, const node_edit::Values& v) : name(elem), values(v) {}
    void get_restore() override
    {
        node_edit::assign_value(name, values);
        if (const int index = nodes::get_find(name); index >= 0) {
            nodes::reread(index);
        }
        level_editor_checks_state().due_ = get_seconds();
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        node_edit::Values v = values;
        node_edit::get_take_in(name, &v);
        return std::make_shared<NodePlace>(name, v);
    }
};

struct LayoutRecords : ModuleStep {
private:
    std::vector<Issue> issues;

public:
    explicit LayoutRecords(std::vector<Issue> list) : issues(std::move(list)) {}
    void get_restore() override
    {
        level_editor_checks_state().layout_issues_ = issues;
        level_editor_checks_state().due_ = get_seconds();
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        return std::make_shared<LayoutRecords>(level_editor_checks_state().layout_issues_);
    }
};

void fix(const Issue& issue)
{
    switch (issue.rule) {
    case Rule::kBelowGround: {
        node_edit::Values values;
        if ((issue.node == 0U) || !node_edit::get_take_in(issue.node, &values)) {
            return;
        }
        push_module_undo(std::make_shared<NodePlace>(issue.node, values));
        values.pos = issue.fix_pos_;
        node_edit::assign_value(issue.node, values);
        if (const int index = nodes::get_find(issue.node); index >= 0) {
            nodes::reread(index);
        }
        hud::get_message("Moved to surface: " + get_name(issue.node));
        get_log("level editor: problems - %s put on the surface above it (%.0f, %.0f, %.0f)",
                get_name(issue.node).c_str(), issue.fix_pos_.x, issue.fix_pos_.y, issue.fix_pos_.z);
        break;
    }
    case Rule::kHiddenCollision:
        shared_collision::change_live(issue.sector, false);
        hud::get_message("The collision taken along by " + get_name(issue.sector) + " turned off");
        break;
    case Rule::kLayoutRecord: {
        push_module_undo(std::make_shared<LayoutRecords>(level_editor_checks_state().layout_issues_));
        std::erase_if(level_editor_checks_state().layout_issues_,
                      [&](const Issue& listed_issue) { return listed_issue.key == issue.key; });
        hud::get_message("Record removed");
        get_log("level editor: problems - layout record dropped: %s", issue.record.c_str());
        break;
    }
    default:
        break;
    }
    level_editor_checks_state().due_ = get_seconds();
}

} // namespace

// --- the layout's unresolved records ------------------------------------------------------------------------------

namespace {

// One record's line: a file of that record alone, written (its header and scene lines left out).
std::string line_of(const layout::FileLoad& one)
{
    std::istringstream in(layout::emit_out(one));
    std::string line;
    std::string out;
    int number = 0;
    while (std::getline(in, line)) {
        if (++number <= 1 || line.starts_with("scene") || line.empty()) {
            continue;
        }
        out += (out.empty() ? "" : "\n") + line;
    }
    return out;
}

// note_layout_applied's collision records (walls, faces, trick-object clusters) whose piece isn't in the level.
template <class Sector, class Report>
void check_collision_records(const layout::FileLoad& file, Sector& sector, Report& report)
{
    for (const walls::WallState& w : file.walls) {
        if (!sector(w.checksum)) {
            layout::FileLoad one;
            one.walls.push_back(w);
            report(w.checksum, "Collision " + get_name(w.checksum), one);
        }
    }
    for (const faces::SectorFaces& sector_faces : file.faces) {
        if (!sector(sector_faces.checksum)) {
            layout::FileLoad one;
            one.faces.push_back(sector_faces);
            report(sector_faces.checksum, "Collision faces of " + get_name(sector_faces.checksum), one);
        }
    }
    for (const clusters::Membership& membership : file.clusters) {
        if (!sector(membership.sector)) {
            layout::FileLoad one;
            one.clusters.push_back(membership);
            report(membership.sector, "Trick-object cluster of " + get_name(membership.sector), one);
        }
    }
}

// note_layout_applied's chain edits of level nodes (links, dropped points, terrains, facings, flat grinds,
// speed-ups) and entity edits (path speeds, sound emitters) whose node isn't in the level.
template <class Node, class Report>
void check_chain_and_entity_records(const layout::FileLoad& file, Node& node, Report& report)
{
    const chains::State& chains = file.chains;
    for (const chains::LinkEdit& link_edit : chains.links) {
        if (!node(link_edit.name)) {
            layout::FileLoad one;
            one.chains.links.push_back(link_edit);
            report(link_edit.name, "Links of " + get_name(link_edit.name), one);
        }
    }
    for (const uint32_t name : chains.dropped) {
        if (!node(name)) {
            layout::FileLoad one;
            one.chains.dropped.push_back(name);
            report(name, "Dropped point " + get_name(name), one);
        }
    }
    for (const chains::TerrainEdit& terrain_edit : chains.terrains) {
        if (!node(terrain_edit.name)) {
            layout::FileLoad one;
            one.chains.terrains.push_back(terrain_edit);
            report(terrain_edit.name, "Terrain of " + get_name(terrain_edit.name), one);
        }
    }
    for (const chains::FacingEdit& facing_edit : chains.facings) {
        if (!node(facing_edit.name)) {
            layout::FileLoad one;
            one.chains.facings.push_back(facing_edit);
            report(facing_edit.name, "Facing of " + get_name(facing_edit.name), one);
        }
    }
    for (const uint32_t name : chains.grind_flat_) {
        if (!node(name)) {
            layout::FileLoad one;
            one.chains.grind_flat_.push_back(name);
            report(name, "Flat grind of " + get_name(name), one);
        }
    }
    for (const chains::SpeedEdit& speed_edit : chains.speeds) {
        if (!node(speed_edit.name)) {
            layout::FileLoad one;
            one.chains.speeds.push_back(speed_edit);
            report(speed_edit.name, "Rail speed-up of " + get_name(speed_edit.name), one);
        }
    }
    for (const entities::Pace& pace : file.entities.speeds) {
        if (!node(pace.name)) {
            layout::FileLoad one;
            one.entities.speeds.push_back(pace);
            report(pace.name, "Path speed of " + get_name(pace.name), one);
        }
    }
    for (const entities::EmitterSound& emitter_sound : file.entities.emitters) {
        if (!node(emitter_sound.name)) {
            layout::FileLoad one;
            one.entities.emitters.push_back(emitter_sound);
            report(emitter_sound.name, "Sound emitter " + get_name(emitter_sound.name), one);
        }
    }
}

} // namespace

void note_layout_applied(const layout::FileLoad& file)
{
    level_editor_checks_state().layout_issues_.clear();
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
        if (std::ranges::none_of(level_editor_checks_state().layout_issues_,
                                 [&](const Issue& listed_issue) { return listed_issue.key == issue.key; })) {
            level_editor_checks_state().layout_issues_.push_back(issue);
        }
    };
    for (const scenery::SectorState& sector_state : file.sectors) {
        if (!sector(sector_state.checksum)) {
            layout::FileLoad one;
            one.sectors.push_back(sector_state);
            report(sector_state.checksum, "Scenery piece " + get_name(sector_state.checksum), one);
        }
    }
    for (const scenery::RailState& rail_state : file.rails) {
        if (!node(rail_state.name)) {
            layout::FileLoad one;
            one.rails.push_back(rail_state);
            report(rail_state.name, "Rail point " + get_name(rail_state.name), one);
        }
    }
    for (const layout::CloneRecord& clone_record : file.clones) {
        if (!sector(clone_record.source)) {
            layout::FileLoad one;
            one.clones.push_back(clone_record);
            report(clone_record.source, "The source of a scenery copy, " + get_name(clone_record.source), one);
        }
    }
    for (const layout::ObjectRecord& object_record : file.objects) {
        if (!object(object_record.id)) {
            layout::FileLoad one;
            one.objects.push_back(object_record);
            report(object_record.id, "Object " + get_name(object_record.id), one);
        }
    }
    for (const layout::CopyRecord& copy_record : file.copies) {
        if (!objects::find_object(copy_record.name) && !object(copy_record.source)) {
            layout::FileLoad one;
            one.copies.push_back(copy_record);
            report(copy_record.name,
                   "The source of object copy " + get_name(copy_record.name) + ", " + get_name(copy_record.source),
                   one);
        }
    }
    for (const layout::SetRecord& set_record : file.nodes) {
        if (!node(set_record.name)) {
            layout::FileLoad one;
            one.nodes.push_back(set_record);
            report(set_record.name, "Node " + get_name(set_record.name) + " (moved / edited)", one);
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
    for (const triggers::ProximEdit& proxim_edit : file.proxims) {
        if (!node(proxim_edit.node)) {
            layout::FileLoad one;
            one.proxims.push_back(proxim_edit);
            report(proxim_edit.node, "Proximity node " + get_name(proxim_edit.node), one);
        }
    }
    check_collision_records(file, sector, report);
    // The chains' edits of level nodes (links, dropped points, terrains, facings, flat grinds, speed-ups).
    check_chain_and_entity_records(file, node, report);
    if ((file.level.start != 0U) && !node(file.level.start)) {
        layout::FileLoad one;
        one.level = file.level;
        report(file.level.start, "The level's start, restart " + get_name(file.level.start), one);
    }
    for (const Issue& issue : level_editor_checks_state().layout_issues_) {
        get_log("level editor: layout record not found in the level - kept for the next save (the Problems window, "
                "Alt+P, drops it): %s",
                issue.record.c_str());
    }
    level_editor_checks_state().due_ = get_seconds();
}

void forget_layout_records()
{
    level_editor_checks_state().layout_issues_.clear();
    level_editor_checks_state().due_ = get_seconds();
}

std::vector<std::string> carried_records(const std::string& captured)
{
    std::vector<std::string> lines;
    for (const Issue& issue : level_editor_checks_state().layout_issues_) {
        if (issue.record.empty()) {
            continue;
        }
        std::istringstream in(issue.record);
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
    }
    return lines.empty() ? lines : carried_lines(lines, captured);
}

const std::vector<Issue>& layout_issues() { return level_editor_checks_state().layout_issues_; }

// --- the frame ----------------------------------------------------------------------------------------------------

void note_model(int changes, const std::unordered_map<uint32_t, uint8_t>& marks)
{
    const uint32_t generation = nodes::get_generation();
    const int loads = layout::level_loads();
    const double now = get_seconds();
    if (changes != level_editor_checks_state().changes_ || generation != level_editor_checks_state().generation_ || loads != level_editor_checks_state().loads_ || !level_editor_checks_state().checked_once_) {
        const bool first = !level_editor_checks_state().checked_once_ || loads != level_editor_checks_state().loads_;
        level_editor_checks_state().changes_ = changes;
        level_editor_checks_state().generation_ = generation;
        level_editor_checks_state().loads_ = loads;
        marks_state() = marks;
        // (a moment after a change: changes come in bursts - a drag, a typed field)
        level_editor_checks_state().due_ = first ? now : now + kRecheckDelaySeconds;
    }
    if (level_editor_checks_state().due_ >= 0.0 && now >= level_editor_checks_state().due_) {
        marks_state() = marks;
        level_editor_checks_state().due_ = -1.0;
        get_run();
    }
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_checks_state().queue_);
    for (const auto& action : queue) {
        action();
    }
}

void forget_level()
{
    placed_state().clear();
    level_editor_checks_state().issues_.clear();
    level_editor_checks_state().layout_issues_.clear();
    ignored_state().clear();
    marks_state().clear();
    level_editor_checks_state().checked_once_ = false;
    level_editor_checks_state().due_ = -1.0;
}

const std::vector<Issue>& get_issues() { return level_editor_checks_state().issues_; }

namespace {

bool get_shown(const Issue& issue)
{
    return (level_editor_checks_state().show_own_ || !issue.level_own_) && (level_editor_checks_state().show_ignored_ || !ignored_state().contains(issue.key));
}

} // namespace

void shown_counts(int* errors, int* warnings, int* infos)
{
    *errors = *warnings = *infos = 0;
    for (const Issue& issue : level_editor_checks_state().issues_) {
        if (issue.level_own_ || ignored_state().contains(issue.key)) {
            continue;
        }
        if (issue.severity == Severity::kError) {
            *errors += 1;
        } else if (issue.severity == Severity::kWarning) {
            *warnings += 1;
        } else {
            *infos += 1;
        }
    }
}

void open_window(int tab)
{
    level_editor_checks_state().open_ = true;
    level_editor_checks_state().tab_ = tab;
    level_editor_checks_state().tab_pending_ = true;
    level_editor_checks_state().due_ = get_seconds(); // (opened: checked again now)
}

void toggle_window()
{
    if (level_editor_checks_state().open_) {
        level_editor_checks_state().open_ = false;
    } else {
        open_window(kProblemsTab);
    }
}

// --- the UI -------------------------------------------------------------------------------------------------------

namespace {

ImVec4 severity_colour(Severity severity)
{
    switch (severity) {
    case Severity::kError:
        return kErrorColour;
    case Severity::kWarning:
        return kWarningColour;
    default:
        return kInfoColour;
    }
}

void go_to(const Issue& issue)
{
    bool done = false;
    if (issue.node != 0U) {
        done = go_to_node_named(issue.node);
    }
    if (!done && (issue.sector != 0U)) {
        done = go_to_scenery(issue.sector);
    }
    if (!done && issue.has_pos_) {
        go_to_point(issue.pos, kGoToDistance);
    }
}

void problems_tab(float scale, ImFont* mono)
{
    (void)mono;
    if (ImGui::Button("Check now")) {
        level_editor_checks_state().queue_.emplace_back([] { level_editor_checks_state().due_ = get_seconds(); });
    }
    ImGui::SameLine();
    int own = 0;
    int ignored = 0;
    for (const Issue& issue : level_editor_checks_state().issues_) {
        own += static_cast<int>(issue.level_own_);
        ignored += static_cast<int>(ignored_state().contains(issue.key));
    }
    std::array<char, kLabelTextSize> label{};
    text::format_to(label, "Original Level (%d)", own);
    ImGui::Checkbox(label.data(), &level_editor_checks_state().show_own_);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Problems present in the original level");
    }
    ImGui::SameLine();
    text::format_to(label, "Ignored (%d)", ignored);
    ImGui::Checkbox(label.data(), &level_editor_checks_state().show_ignored_);
    ImGui::SameLine();
    if (level_editor_checks_state().checked_once_) {
        gui::text_disabled("%.0f ms", level_editor_checks_state().last_seconds_ * kMillisecondsPerSecond);
    } else {
        gui::text_disabled("not checked");
    }
    ImGui::Separator();
    int shown = 0;
    for (const Issue& issue : level_editor_checks_state().issues_) {
        shown += static_cast<int>(get_shown(issue));
    }
    if (shown == 0) {
        gui::text_colored(kNoProblemsColour, "No problems");
        if (own != 0) {
            gui::text_disabled("(%d in the original level)", own);
        }
        return;
    }
    if (!ImGui::BeginTable("##problems", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                           ImVec2(0.0F, kListHeight * scale))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, kKindColumnWidth * scale);
    ImGui::TableSetupColumn("Problem", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, kWhereColumnWidth * scale);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < level_editor_checks_state().issues_.size(); ++i) {
        const Issue& issue = core::at(level_editor_checks_state().issues_, i);
        if (!get_shown(issue)) {
            continue;
        }
        const bool is_ignored = ignored_state().contains(issue.key);
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        gui::text_colored(severity_colour(issue.severity), "%s", severity_name(issue.severity));
        ImGui::TableNextColumn();
        if (is_ignored || issue.level_own_) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        }
        gui::text_disabled("%s%s", rule_name(issue.rule), issue.level_own_ ? " (original)" : "");
        gui::text_wrapped("%s", issue.text.c_str());
        if (is_ignored || issue.level_own_) {
            ImGui::PopStyleColor();
        }
        ImGui::TableNextColumn();
        const bool can_go = (issue.node != 0U) || (issue.sector != 0U) || issue.has_pos_;
        ImGui::BeginDisabled(!can_go);
        if (ImGui::SmallButton("Go to")) {
            const Issue copy = issue;
            level_editor_checks_state().queue_.emplace_back([copy] { go_to(copy); });
        }
        ImGui::EndDisabled();
        if (issue.fixable) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Fix")) {
                const Issue copy = issue;
                level_editor_checks_state().queue_.emplace_back([copy] { fix(copy); });
            }
            if (ImGui::IsItemHovered()) {
                const char* fix = "";
                if (issue.rule == Rule::kBelowGround) {
                    fix = "Move to the surface above";
                } else if (issue.rule == Rule::kHiddenCollision) {
                    fix = "Turn the collision off";
                } else if (issue.rule == Rule::kLayoutRecord) {
                    fix = "Drop the record: the next save leaves it out (undoable)";
                }
                gui::tooltip("%s", fix);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(is_ignored ? "Unignore" : "Ignore")) {
            if (is_ignored) {
                ignored_state().erase(issue.key);
            } else {
                ignored_state().insert(issue.key);
            }
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Ignore until the level reloads");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

void pane(PaneArea area, float scale, ImFont* mono)
{
    const auto view_x = area.x;
    const auto view_y = area.y;
    const auto view_width = area.width;
    const auto view_height = area.height;
    (void)view_height;
    if (!level_editor_checks_state().open_) {
        return;
    }
    if (ui::app::place_floating("Problems###problems", true)) {
        ImGui::SetNextWindowPos(ImVec2(view_x + (view_width * kHalf), view_y + (kWindowTopOffset * scale)),
                                ImGuiCond_FirstUseEver, ImVec2(kWindowPivotX, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(kWindowWidth * scale, 0.0F), ImGuiCond_FirstUseEver);
    }
    bool open = true;
    if (!ImGui::Begin("Problems###problems", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        level_editor_checks_state().open_ = open;
        return;
    }
    if (ImGui::BeginTabBar("##problemTabs")) {
        int errors = 0;
        int warnings = 0;
        int infos = 0;
        shown_counts(&errors, &warnings, &infos);
        std::array<char, kLabelTextSize> label{};
        text::format_to(label, "Problems (%d)###problemsTab", errors + warnings + infos);
        const ImGuiTabItemFlags problems_flags =
            level_editor_checks_state().tab_pending_ && level_editor_checks_state().tab_ == kProblemsTab ? ImGuiTabItemFlags_SetSelected : 0;
        const ImGuiTabItemFlags budget_flags = level_editor_checks_state().tab_pending_ && level_editor_checks_state().tab_ == kBudgetTab ? ImGuiTabItemFlags_SetSelected : 0;
        level_editor_checks_state().tab_pending_ = false;
        if (ImGui::BeginTabItem(label.data(), nullptr, problems_flags)) {
            level_editor_checks_state().tab_ = kProblemsTab;
            problems_tab(scale, mono);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Budget###budgetTab", nullptr, budget_flags)) {
            level_editor_checks_state().tab_ = kBudgetTab;
            budget::get_table();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    level_editor_checks_state().open_ = open;
}

} // namespace frontend::level_editor::checks
