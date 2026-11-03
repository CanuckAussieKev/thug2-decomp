// NOT ORIGINAL - play testing from the level editor (ours). See level_editor_playtest.h.

#include "game/rider/rider.h"
#include "platform/keyboard.h"
#include "editor/level_editor_playtest.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <array>

#include "editor/level_editor.h"
#include "engine/core/checksums.h"
#include "frontend/front_end.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui.h"
#include "engine/gfx/screen.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/balance_trick.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_state.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/sys/mlp.h"
#include "platform/sdl_events.h"
#include "platform/run_log.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/field_reader.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::play_test {

namespace {

constexpr float kDegreesPerHalfTurn = 180.0f; // degrees in half a turn
constexpr float kHalf = 0.5f; // half

} // namespace

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

const std::array<const char*, kMotions> kMotionNames = {{"ground", "air", "grind", "lip", "wall", "manual", "bail", "walk", "drive"}};
// (ARGB: ground grey, air sky blue, grind orange, lip pink, wall violet, manual green, bail red, walking olive,
// driving cyan)
const std::array<uint32_t, kMotions> kMotionColours = {{0xFFB4B4B4, 0xFF48A8FF, 0xFFFF9A20, 0xFFFF60C8, 0xFFB070FF,
                                           0xFF40E070, 0xFFFF3030, 0xFFA0A060, 0xFF50E0E0}};

float len_of(V3 v) { return std::sqrt(get_dot(v, v)); }

} // namespace

const char* motion_name(Motion motion)
{
    const int i = static_cast<int>(motion);
    return i >= 0 && i < kMotions ? core::at(kMotionNames, i) : "?";
}

uint32_t motion_colour(Motion motion)
{
    const int i = static_cast<int>(motion);
    return i >= 0 && i < kMotions ? core::at(kMotionColours, i) : 0xFFFFFFFF;
}

bool motion_from_name(const std::string& name, Motion* motion)
{
    for (int i = 0; i < kMotions; ++i) {
        if (name == core::at(kMotionNames, i)) {
            *motion = static_cast<Motion>(i);
            return true;
        }
    }
    return false;
}

Motion get_classify(int physics_state, bool bailing, uint32_t balance_trick, bool walking, bool driving)
{
    if (bailing)
        return Motion::kBail;
    if (driving)
        return Motion::kDrive;
    if (walking)
        return Motion::kWalk;
    switch (physics_state) {
    case kStateAir:
        return Motion::kAir;
    case kStateWall:
    case kStateWallplant:
        return Motion::kWall;
    case kStateLip:
        return Motion::kLip;
    case kStateRail:
        return Motion::kGrind;
    default:
        return balance_trick == kManual || balance_trick == kNoseManual ? Motion::kManual : Motion::kGround;
    }
}

void Recorder::feed(V3 pos, Motion motion)
{
    if (full_)
        return;
    auto push = [this](const Sample& s) {
        if (samples_.size() >= kMaxSamples) {
            full_ = true;
            return false;
        }
        samples_.push_back(s);
        return true;
    };
    Sample now = {.pos = pos, .motion = motion, .jump = false};
    if (!have_last_ || samples_.empty()) {
        have_last_ = true;
        last = now;
        last_kept_ = push(now);
        return;
    }
    const V3 step = pos - last.pos;
    if (get_dot(step, step) > kJump * kJump) {
        // (moved there: the stretch before ends where it was, the new one starts unlinked)
        if (!last_kept_)
            push(last);
        now.jump = true;
        last_kept_ = push(now);
        last = now;
        return;
    }
    bool keep = false;
    if (motion != last.motion) {
        if (!last_kept_)
            push(last); // (the last frame of the motion before: its stretch keeps its colour up to here)
        keep = true;
    } else {
        const Sample& kept = samples_.back();
        const V3 d = pos - kept.pos;
        const float distance = len_of(d);
        if (distance >= kMaxStep) {
            keep = true;
        } else if (distance >= kMinStep && samples_.size() >= 2 && !kept.jump) {
            const V3 before = kept.pos - core::at(samples_, samples_.size() - 2).pos;
            const float lb = len_of(before);
            if (lb > 0.001f) {
                const float c = get_dot(before, d) / (lb * distance);
                keep = c < std::cos(kTurnDegrees * 3.14159265f / kDegreesPerHalfTurn);
            }
        }
    }
    last_kept_ = keep && push(now);
    last = now;
}

std::vector<GapChange> GapTracker::tick_frame(const std::vector<GapRead>& now)
{
    std::vector<GapChange> changes;
    if (!known) {
        known = true;
        last = now;
        return changes;
    }
    for (const GapRead& g : now) {
        const GapRead* old = nullptr;
        for (const GapRead& o : last) {
            if (o.name == g.name) {
                old = &o;
                break;
            }
        }
        const int old_pending = old ? old->pending : 0;
        const int old_count = old ? old->count : g.count;
        if (g.pending > old_pending)
            changes.push_back({.kind = GapChange::Kind::kGot, .name = g.name, .score = g.score});
        if (g.count > old_count) {
            // (got and landed between two reads: the got first)
            if (old_pending == 0 && g.pending <= old_pending)
                changes.push_back({.kind = GapChange::Kind::kGot, .name = g.name, .score = g.score});
            changes.push_back({.kind = GapChange::Kind::kLanded, .name = g.name, .score = g.score});
        } else if (old_pending > 0 && g.pending == 0) {
            changes.push_back({.kind = GapChange::Kind::kLost, .name = g.name, .score = g.score});
        }
    }
    last = now;
    return changes;
}

bool ComboTracker::tick_frame(int pending_now, bool bailing, ComboEnd* end)
{
    bool decided = false;
    if (ending_ > 0) {
        if (bailing) {
            *end = {.points = ending_, .landed = false};
            ending_ = 0;
            decided = true;
        } else if (--settle <= 0) {
            *end = {.points = ending_, .landed = true};
            ending_ = 0;
            decided = true;
        }
    }
    if (pending > 0 && pending_now <= 0) {
        if (!decided && bailing) {
            *end = {.points = pending, .landed = false};
            decided = true;
        } else {
            ending_ = pending;
            settle = kSettleFrames;
        }
    }
    pending = pending_now > 0 ? pending_now : 0;
    return decided;
}

int gaps_landed(const Run& run_record)
{
    int n = 0;
    for (const Occurrence& e : run_record.events)
        n += e.kind == EventKind::kGap && e.status == GapStatus::kLanded;
    return n;
}

int bails(const Run& run_record)
{
    int n = 0;
    for (const Occurrence& e : run_record.events)
        n += e.kind == EventKind::kBail;
    return n;
}

int top_combo_value(const Run& run_record)
{
    int best = 0;
    for (const Occurrence& e : run_record.events) {
        if (e.kind == EventKind::kCombo && e.status == GapStatus::kLanded)
            best = std::max(best, e.value);
    }
    return best;
}

void LevelTrails::append_one(Run run)
{
    if (run.number <= 0)
        run.number = next_number_;
    next_number_ = std::max(next_number_, run.number + 1);
    runs_.push_back(std::move(run));
    while (runs_.size() > kMaxRuns)
        runs_.erase(runs_.begin());
}

namespace {

const char* const kHeader = "thug2-decomp trails 1";
const char* event_word(EventKind kind)
{
    return kind == EventKind::kGap ? "gap" : kind == EventKind::kBail ? "bail" : "combo";
}
const char* status_word(GapStatus status)
{
    return status == GapStatus::kLanded ? "landed" : status == GapStatus::kLost ? "lost" : "pending";
}
// (a name kept to one line)
std::string one_line(const std::string& text)
{
    std::string out = text;
    for (char& c : out) {
        if (c == '\r' || c == '\n')
            c = ' ';
    }
    return out;
}

} // namespace

std::string get_format(const LevelTrails& trails)
{
    std::string out = std::string(kHeader) + "\n";
    std::array<char, 256> line{};
    for (const std::string& gap : trails.landed())
        out += "gap " + one_line(gap) + "\n";
    for (const Run& run : trails.runs()) {
        text::format_to(line, "run %d %.2f %d\n", run.number, run.seconds, run.visible ? 1 : 0);
        out += line.data();
        for (const Sample& s : run.samples_) {
            text::format_to(line, "s %.1f %.1f %.1f %s %d\n", s.pos.x, s.pos.y, s.pos.z, motion_name(s.motion),
                            s.jump ? 1 : 0);
            out += line.data();
        }
        for (const Occurrence& e : run.events) {
            text::format_to(line, "e %s %.1f %.1f %.1f %d %s ", event_word(e.kind), e.pos.x, e.pos.y, e.pos.z, e.value,
                            status_word(e.status));
            out += line.data() + one_line(e.name) + "\n";
        }
    }
    return out;
}

bool parse(const std::string& text, LevelTrails* trails)
{
    std::istringstream in(text);
    std::string line;
    auto trim = [](std::string& s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
            s.pop_back();
    };
    if (!std::getline(in, line))
        return false;
    trim(line);
    if (line != kHeader)
        return false;
    LevelTrails out;
    Run* run = nullptr;
    while (std::getline(in, line)) {
        trim(line);
        if (line.starts_with("gap ")) {
            if (line.size() > 4)
                out.landed().insert(line.substr(4));
        } else if (line.starts_with("run ")) {
            int number = 0, visible = 1;
            float seconds = 0.0f;
            text::FieldReader fields(std::string_view(line).substr(4)); // "%d %f %d", the last one optional
            if (!(fields.read(number) && fields.read(seconds)) || number <= 0)
                continue;
            fields.read(visible);
            Run r;
            r.number = number;
            r.seconds = seconds;
            r.visible = visible != 0;
            out.runs().push_back(r);
            out.set_next_number(std::max(out.next_number(), number + 1));
            run = &out.runs().back();
        } else if (line.starts_with("s ") && run) {
            std::istringstream fields(line.substr(2));
            Sample s;
            std::string motion;
            int jump = 0;
            if (!(fields >> s.pos.x >> s.pos.y >> s.pos.z >> motion >> jump) || !motion_from_name(motion, &s.motion))
                continue;
            s.jump = jump != 0;
            run->samples_.push_back(s);
        } else if (line.starts_with("e ") && run) {
            std::istringstream fields(line.substr(2));
            Occurrence e;
            std::string kind, status;
            if (!(fields >> kind >> e.pos.x >> e.pos.y >> e.pos.z >> e.value >> status))
                continue;
            if (kind == "gap")
                e.kind = EventKind::kGap;
            else if (kind == "bail")
                e.kind = EventKind::kBail;
            else if (kind == "combo")
                e.kind = EventKind::kCombo;
            else
                continue;
            e.status = status == "landed" ? GapStatus::kLanded
                       : status == "lost" ? GapStatus::kLost
                                          : GapStatus::kAwaiting;
            std::string rest;
            std::getline(fields, rest);
            if (!rest.empty() && core::at(rest, 0) == ' ')
                rest.erase(0, 1);
            e.name = rest;
            run->events.push_back(e);
        }
    }
    while (out.runs().size() > kMaxRuns)
        out.runs().erase(out.runs().begin());
    *trails = std::move(out);
    return true;
}

std::string get_file_name(uint32_t scene)
{
    std::array<char, 32> name{};
    text::format_to(name, "%08X.trails.txt", scene);
    return name.data();
}

bool simulate_key_keeps(int scancode, bool ctrl, bool alt, bool fly_keys_safe)
{
    switch (scancode) {
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI:
        return true;
    default:
        break;
    }
    const bool digit = scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0;
    if (alt)
        return !ctrl && scancode == SDL_SCANCODE_H; // (Alt+H: everything shown again - the view only)
    if (ctrl)
        return digit || scancode == SDL_SCANCODE_COMMA; // (bookmarks stored, the preferences)
    if (digit || (scancode >= SDL_SCANCODE_KP_DIVIDE && scancode <= SDL_SCANCODE_KP_PERIOD))
        return true; // (bookmarks, the ortho views)
    switch (scancode) {
    case SDL_SCANCODE_W:
    case SDL_SCANCODE_A:
    case SDL_SCANCODE_S:
    case SDL_SCANCODE_D:
    case SDL_SCANCODE_Q:
    case SDL_SCANCODE_E:
    case SDL_SCANCODE_SPACE:
        return fly_keys_safe; // (flying - not when they nudge a selection)
    case SDL_SCANCODE_F:
    case SDL_SCANCODE_C:
    case SDL_SCANCODE_O:
    case SDL_SCANCODE_T:
    case SDL_SCANCODE_B:
    case SDL_SCANCODE_N:
    case SDL_SCANCODE_P:
    case SDL_SCANCODE_H:
    case SDL_SCANCODE_I:
    case SDL_SCANCODE_SLASH:
    case SDL_SCANCODE_TAB:
    case SDL_SCANCODE_GRAVE:
    case SDL_SCANCODE_F1:
    case SDL_SCANCODE_F5:
    case SDL_SCANCODE_F6:
    case SDL_SCANCODE_F7:
        return true;
    default:
        return false;
    }
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kSkater = 0x5B8AB877;               // Skater (the local skater's alias)
constexpr uint32_t kSkaterState = 0x43686585;          // SkaterState
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3;   // SkaterBalanceTrick
constexpr uint32_t kSkaterScore = 0xD262802F;          // SkaterScore
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // (skaters)
constexpr uint32_t kCamModuleName = 0xC4E311FA;        // (camera objects)


goals::GapTally* get_checklist()
{
    modules::Skate* skate = modules::Skate::sole();
    return skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
}

// The skater's state (the header's offsets).
struct SkaterNow {
    V3 pos;
    Motion motion;
    bool bailing;
    int pending; // the combo's points
};
bool read_skater(objects::GameEntity* skater, SkaterNow* out)
{
    const objects::ObjectModule* state = skater->fetch_module(kSkaterState);
    if (!state)
        return false;
    const auto* pose = core::down_cast<const objects::RiderPoseState>(state);
    const int physics = pose->cur_phase();
    const bool bailing = pose->is_on(objects::PoseFlag::kBailing) != 0;
    const bool driving = pose->in_vehicle() != 0;
    const bool walking = !driving && pose->physics_mode() == 1;
    const objects::ObjectModule* balance = skater->fetch_module(kSkaterBalanceTrick);
    const uint32_t trick = balance ? core::down_cast<const objects::RiderTeeterMoves>(balance)->trick() : 0;
    out->pos = {.x = skater->at_point().x, .y = skater->at_point().y, .z = skater->at_point().z};
    out->motion = get_classify(physics, bailing, trick, walking, driving);
    out->bailing = bailing;
    out->pending = 0;
    if (const objects::ObjectModule* score = skater->fetch_module(kSkaterScore)) {
        if (const modules::PointTally* s = core::down_cast<const objects::RiderPointKeeper>(score)->points_held()) {
            const double points = static_cast<double>(s->pot()) * s->multiplier();
            if (points > 0.0 && points < 2.0e9)
                out->pending = static_cast<int>(points);
        }
    }
    return true;
}

// The run being recorded.
struct Recording {
    bool open = false;
    int load = -1;
    uint32_t scene = 0;
    Run run;
    Recorder recorder;
    GapTracker gaps;
    ComboTracker combo;
    bool was_bailing_ = false;
    uint32_t last_ticks_ = 0;
    int refused_check_ = 0;
    bool refused = false;
};
// NOT ORIGINAL grouping - level_editor_playtest.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_playtest_state()).
struct LevelEditorPlaytestState {
    Recording rec_{};
    uint32_t editor_scene_{}; // (the level the editor shows)
    bool show_trails_ = true;
    bool window_open_{};
    Ask ask_ = Ask::kNone;
    // Play from a restart.
    uint32_t restart_{};
    uint32_t restart_asked_{};
    int restart_frames_{};
};

LevelEditorPlaytestState& level_editor_playtest_state()
{
    static core::NoDestroy<LevelEditorPlaytestState> state;
    return state.get();
}

std::map<uint32_t, LevelTrails>& levels()
{
    static std::map<uint32_t, LevelTrails> value;
    return value;
}
std::set<uint32_t>& file_tried()
{
    static std::set<uint32_t> value;
    return value;
}

// Simulate.
struct Snapshot {
    object_edit::State state;
    maths::Vec4 vel;
};
// NOT ORIGINAL grouping - the simulation: whether it runs or is kept, its snapshots, what was touched (were globals).
// Made on first use, never destroyed (simulation()).
struct Simulation {
    bool simulating_{};
    bool keep_simulation_{};
    std::vector<Snapshot> snapshots_{};
    std::vector<uint32_t> touched_before_{};
    bool handler_registered_{};
    uint32_t sim_started_{};
};

Simulation& simulation()
{
    static core::NoDestroy<Simulation> state;
    return state.get();
}

uint32_t scene_now() { return scenery::world_scene_key(scenery::main_scene()); }

std::string trails_path(uint32_t scene)
{
    return game_folder_path("thug2-decomp-layouts") + "\\" + get_file_name(scene);
}

bool ingest_file(uint32_t scene, bool quiet)
{
    const std::ifstream file(trails_path(scene), std::ios::binary);
    if (!file)
        return false;
    std::stringstream text;
    text << file.rdbuf();
    LevelTrails read;
    if (!parse(text.str(), &read)) {
        get_log("level editor: %s isn't a trails file - not read", trails_path(scene).c_str());
        return false;
    }
    levels()[scene] = std::move(read);
    if (!quiet)
        hud::get_message("Trails read from " + get_file_name(scene));
    get_log("level editor: trails read: %u runs, %u gaps landed (%s)",
            static_cast<unsigned>(levels()[scene].runs().size()), static_cast<unsigned>(levels()[scene].landed().size()),
            trails_path(scene).c_str());
    return true;
}

bool save_file(uint32_t scene)
{
    CreateDirectoryA(game_folder_path("thug2-decomp-layouts").c_str(), nullptr);
    std::ofstream file(trails_path(scene), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << get_format(levels()[scene]);
    return static_cast<bool>(file);
}

std::vector<GapRead> read_gaps()
{
    std::vector<GapRead> out;
    const goals::GapTally* list = get_checklist();
    if (!list)
        return out;
    // (the list's nodes: the head's data is -1 - gap_checklist.cpp)
    int guard = 0;
    for (const memory::ListNode* node = list->gaps().next(); node && node != &list->gaps() && ++guard < 4096; node = node->next()) {
        if (node->data() == memory::ListNode::head_marker())
            break;
        auto* gap = static_cast<goals::GapEntry*>(node->data());
        out.push_back({.name = gap->label().c_str() ? gap->label().c_str() : "", .pending = gap->pending(), .count = gap->num(), .score = gap->points_held()});
    }
    return out;
}

void close_run(const char* why)
{
    if (!level_editor_playtest_state().rec_.open)
        return;
    level_editor_playtest_state().rec_.open = false;
    level_editor_playtest_state().rec_.run.samples_ = std::move(level_editor_playtest_state().rec_.recorder.samples());
    level_editor_playtest_state().rec_.run.full_ = level_editor_playtest_state().rec_.recorder.full();
    // (a combo still settling counts as landed)
    if (level_editor_playtest_state().rec_.combo.ending() > 0 && !level_editor_playtest_state().rec_.run.samples_.empty())
        level_editor_playtest_state().rec_.run.events.push_back(
            {.kind = EventKind::kCombo, .pos = level_editor_playtest_state().rec_.run.samples_.back().pos, .name = "", .value = level_editor_playtest_state().rec_.combo.ending(), .status = GapStatus::kLanded});
    if (level_editor_playtest_state().rec_.run.samples_.size() < 2) {
        get_log("level editor: trail - a run too short to keep (%s)", why);
        return;
    }
    LevelTrails& level = levels()[level_editor_playtest_state().rec_.scene];
    level_editor_playtest_state().rec_.run.number = level.next_number();
    get_log("level editor: trail run %d recorded (%s): %.1f s, %u points, %d gaps landed, %d bails, best combo %d%s",
            level_editor_playtest_state().rec_.run.number, why, level_editor_playtest_state().rec_.run.seconds, static_cast<unsigned>(level_editor_playtest_state().rec_.run.samples_.size()),
            gaps_landed(level_editor_playtest_state().rec_.run), bails(level_editor_playtest_state().rec_.run), top_combo_value(level_editor_playtest_state().rec_.run), level_editor_playtest_state().rec_.run.full_ ? " (full)" : "");
    level.append_one(std::move(level_editor_playtest_state().rec_.run));
}

void open_run(int load)
{
    level_editor_playtest_state().rec_ = Recording{};
    level_editor_playtest_state().rec_.open = true;
    level_editor_playtest_state().rec_.load = load;
    level_editor_playtest_state().rec_.scene = scene_now();
    get_log("level editor: trail - recording a run (the level's edited copy in play)");
}

void link_record(modules::FrontEnd* front_end)
{
    const int load = layout::level_loads();
    if (level_editor_playtest_state().rec_.open && (level_editor_playtest_state().rec_.load != load || !recording()))
        close_run(level_editor_playtest_state().rec_.load != load ? "another level loaded" : "recording turned off");
    const bool paused = front_end && front_end->is_halted();
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!recording() || paused || !skater || render::current_level() == 0 || !layout::editing_copy()) {
        level_editor_playtest_state().rec_.last_ticks_ = 0;
        return;
    }
    // (a network session or a refused mode: looked at now and then)
    if (--level_editor_playtest_state().rec_.refused_check_ <= 0) {
        level_editor_playtest_state().rec_.refused_check_ = 60;
        level_editor_playtest_state().rec_.refused = layout::get_refused();
    }
    if (level_editor_playtest_state().rec_.refused) {
        if (level_editor_playtest_state().rec_.open)
            close_run("refused here");
        return;
    }
    SkaterNow now{};
    if (!read_skater(skater, &now))
        return;
    if (!level_editor_playtest_state().rec_.open) {
        open_run(load);
        level_editor_playtest_state().rec_.refused_check_ = 60;
    }
    const uint32_t ticks = SDL_GetTicks();
    const float dt = level_editor_playtest_state().rec_.last_ticks_ ? std::min(0.1f, static_cast<float>(ticks - level_editor_playtest_state().rec_.last_ticks_) / 1000.0f) : 0.0f;
    level_editor_playtest_state().rec_.last_ticks_ = ticks;
    Run& run = level_editor_playtest_state().rec_.run;
    run.seconds += dt;
    core::at(run.motion_seconds_, static_cast<int>(now.motion)) += dt;
    level_editor_playtest_state().rec_.recorder.feed(now.pos, now.motion);
    if (now.bailing && !level_editor_playtest_state().rec_.was_bailing_)
        run.events.push_back({.kind = EventKind::kBail, .pos = now.pos, .name = "", .value = 0, .status = GapStatus::kLost});
    level_editor_playtest_state().rec_.was_bailing_ = now.bailing;
    // The gaps: got, landed, lost.
    LevelTrails& level = levels()[level_editor_playtest_state().rec_.scene];
    for (const GapChange& c : level_editor_playtest_state().rec_.gaps.tick_frame(read_gaps())) {
        if (c.kind == GapChange::Kind::kGot) {
            run.events.push_back({.kind = EventKind::kGap, .pos = now.pos, .name = c.name, .value = c.score, .status = GapStatus::kAwaiting});
            continue;
        }
        bool marked = false;
        for (auto e = run.events.rbegin(); e != run.events.rend(); ++e) {
            if (e->kind == EventKind::kGap && e->status == GapStatus::kAwaiting && e->name == c.name) {
                e->status = c.kind == GapChange::Kind::kLanded ? GapStatus::kLanded : GapStatus::kLost;
                marked = true;
                if (c.kind == GapChange::Kind::kLanded)
                    break; // (one landing a got)
            }
        }
        if (c.kind == GapChange::Kind::kLanded) {
            if (!marked)
                run.events.push_back({.kind = EventKind::kGap, .pos = now.pos, .name = c.name, .value = c.score, .status = GapStatus::kLanded});
            if (level.landed().insert(c.name).second)
                get_log("level editor: trail - gap \"%s\" landed for the first time in a test run", c.name.c_str());
        }
    }
    ComboEnd end;
    if (level_editor_playtest_state().rec_.combo.tick_frame(now.pending, now.bailing, &end) && end.points > 0)
        run.events.push_back(
            {.kind = EventKind::kCombo, .pos = now.pos, .name = "", .value = end.points, .status = end.landed ? GapStatus::kLanded : GapStatus::kLost});
}

void restart_step(modules::FrontEnd* front_end)
{
    if (!level_editor_playtest_state().restart_)
        return;
    if (SDL_GetTicks() - level_editor_playtest_state().restart_asked_ > 15000 && level_editor_playtest_state().restart_frames_ == 0) {
        get_log("level editor: play from a restart - dropped (the editor didn't close)");
        level_editor_playtest_state().restart_ = 0;
        return;
    }
    if (++level_editor_playtest_state().restart_frames_ > 300) {
        get_log("level editor: play from a restart - the game didn't come back from the pause menu; not moved");
        level_editor_playtest_state().restart_ = 0;
        return;
    }
    const bool paused = front_end && front_end->is_halted();
    if (paused || level_editor_playtest_state().restart_frames_ < 4 || !objects::find_object(kSkater) || render::current_level() == 0)
        return;
    const uint32_t restart = level_editor_playtest_state().restart_;
    level_editor_playtest_state().restart_ = 0;
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{checksums::hash_from_text("node_name")}, restart);
    script::launch_script(checksums::hash_from_text("ResetSkaters"), params, nullptr, false, "ResetSkaters");
    params->get_clear();
    script::delete_struct(params);
    const std::string label = nodes::caption(restart);
    get_log("level editor: play from the restart %s (ResetSkaters)", label.c_str());
    layout::panel_message(("Playing from " + label + " (F5: return to editor)").c_str());
}

// --- simulate ---

struct SnapWalk {
    std::vector<Snapshot>* out;
};
void snap_one(objects::Entity* object, void* data)
{
    auto* walk = static_cast<SnapWalk*>(data);
    auto* composite = core::down_cast<objects::GameEntity>(object);
    if ((composite->object_flags() & objects::kObjectFlagDead) ||
        (composite->composite_flags() & objects::kCompositeFlagDestroying))
        return;
    if (composite->fetch_module(kSkaterPhysicsControl) || composite->fetch_module(kCamModuleName))
        return;
    walk->out->push_back({.state = object_edit::store_away(composite), .vel = composite->vel()});
}

// The skaters paused again after the game is unpaused (the front end's own walk of them - FrontEnd::PauseGame: their
// vtable +0x2C pauses one).
void pause_skaters()
{
    modules::Skate* skate = modules::Skate::sole();
    if (!skate)
        return;
    uint32_t count = 0;
    for (const memory::ListNode* node = skate->riders().list_link().next(); node && !node->is_head(); node = node->next())
        ++count;
    for (uint32_t i = 0; i < count; ++i) {
        auto* skater = core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(i)));
        if (skater)
            skater->get_freeze();
    }
}

void stop_simulation(const char* why);

void simulate_event(SDL_Event* event)
{
    if (!simulation().simulating_ || !level_editor::live_now())
        return;
    if (event->type == SDL_KEYDOWN && !event->key.repeat && ui::keys_to_editor()) {
        const platform::KeyState keys = platform::keyboard_keys();
        const bool ctrl = (event->key.keysym.mod & KMOD_CTRL) != 0;
        const bool alt = (event->key.keysym.mod & KMOD_ALT) != 0;
        int x = 0, y = 0;
        const bool right = (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
        const bool fly_keys_safe = right || !xform::fly_always();
        (void)keys;
        if (!simulate_key_keeps(event->key.keysym.scancode, ctrl, alt, fly_keys_safe))
            stop_simulation("a key that edits");
    } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT && ui::mouse_to_editor()) {
        stop_simulation("a click in the view");
    }
}

void start_simulation()
{
    modules::FrontEnd* front_end = modules::front_end_instance().get();
    if (simulation().simulating_ || !front_end)
        return;
    if (!front_end->is_halted()) {
        hud::get_message("Simulate unavailable here");
        return;
    }
    if (layout::get_refused()) {
        hud::get_message("Simulate unavailable here");
        return;
    }
    simulation().snapshots_.clear();
    if (objects::EntityHub* manager = objects::object_hub_slot().get()) {
        SnapWalk walk = {&simulation().snapshots_};
        manager->for_each(snap_one, &walk);
    }
    simulation().touched_before_ = object_edit::get_touched();
    if (!simulation().handler_registered_) {
        // (not from inside the event pump: Tick runs in the render)
        os_layer::register_event_handler(simulate_event);
        simulation().handler_registered_ = true;
    }
    front_end->get_pause_game(false);
    pause_skaters(); // (frozen - and still hidden, as the editor keeps the skater)
    simulation().simulating_ = true;
    simulation().sim_started_ = SDL_GetTicks();
    get_log("level editor: simulate on (%u objects noted to put back)", static_cast<unsigned>(simulation().snapshots_.size()));
    hud::get_message("Simulating (F6: stop)", 3.0);
}

void stop_simulation(const char* why)
{
    if (!simulation().simulating_)
        return;
    simulation().simulating_ = false;
    if (modules::FrontEnd* front_end = modules::front_end_instance().get())
        front_end->get_pause_game(true);
    int restored = 0, gone = 0, kept = 0;
    if (!simulation().keep_simulation_) {
        const std::vector<uint32_t>& touched = object_edit::get_touched();
        for (const Snapshot& s : simulation().snapshots_) {
            // (one the editor moved while it ran keeps the editor's change)
            const bool edited_now = std::ranges::find(touched, s.state.id) != touched.end() &&
                                    std::ranges::find(simulation().touched_before_, s.state.id) == simulation().touched_before_.end();
            if (edited_now) {
                ++kept;
                continue;
            }
            if (!object_edit::get_restore(s.state)) {
                ++gone;
                continue;
            }
            if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(s.state.id)))
                object->vel() = s.vel;
            ++restored;
        }
    }
    simulation().snapshots_.clear();
    get_log("level editor: simulate off (%s) after %.1f s: %s", why, (SDL_GetTicks() - simulation().sim_started_) / 1000.0,
            simulation().keep_simulation_ ? "objects left where they got to"
                              : ("put back " + std::to_string(restored) + " objects" +
                                 (gone ? ", " + std::to_string(gone) + " gone" : "") +
                                 (kept ? ", " + std::to_string(kept) + " moved in the editor meanwhile kept" : ""))
                                    .c_str());
    hud::get_message("Simulation stopped", 2.0);
}

} // namespace

// --- the game side's entry points ---

void tick_frame(modules::FrontEnd* front_end)
{
    if (!level_editor::is_switched_on() || frame_loop::frames_run() == 0)
        return;
    if (simulation().simulating_) // (the editor closed some other way: the world put back first)
        stop_simulation("the editor closed");
    restart_step(front_end);
    link_record(front_end);
}

void on_editor_start()
{
    close_run("the editor opened");
    level_editor_playtest_state().editor_scene_ = scene_now();
    if (level_editor_playtest_state().editor_scene_ && file_tried().insert(level_editor_playtest_state().editor_scene_).second) {
        auto it = levels().find(level_editor_playtest_state().editor_scene_);
        if (it == levels().end() || it->second.runs().empty())
            ingest_file(level_editor_playtest_state().editor_scene_, true);
    }
}

void on_editor_stop() { stop_simulation("the editor closed"); }

void tick(bool dragging)
{
    level_editor_playtest_state().editor_scene_ = scene_now();
    if (simulation().simulating_ && layout::network_active())
        stop_simulation("a network session");
    // (the keymap's - level_editor_actions.h: F6, F7 by default)
    if (actions::squeezed(actions::kSimulate) && !dragging)
        level_editor_playtest_state().ask_ = Ask::kToggleSimulate;
    if (actions::squeezed(actions::kTrailWindow)) {
        if (ui::underway())
            level_editor_playtest_state().window_open_ = !level_editor_playtest_state().window_open_;
        else
            hud::get_message("Requires the editor UI (editor_ui=1)");
    }
    const Ask ask = level_editor_playtest_state().ask_;
    level_editor_playtest_state().ask_ = Ask::kNone;
    switch (ask) {
    case Ask::kToggleSimulate:
        if (simulation().simulating_)
            stop_simulation("F6");
        else if (!dragging)
            start_simulation();
        break;
    case Ask::kClear: {
        levels()[level_editor_playtest_state().editor_scene_] = LevelTrails{};
        hud::get_message("Trails cleared");
        break;
    }
    case Ask::kStoreAway: {
        const bool ok = level_editor_playtest_state().editor_scene_ && save_file(level_editor_playtest_state().editor_scene_);
        hud::get_message(ok ? "Trails saved: " + get_file_name(level_editor_playtest_state().editor_scene_)
                            : std::string("Trails couldn't be saved"));
        get_log("level editor: trails %s %s", ok ? "saved to" : "not saved to", trails_path(level_editor_playtest_state().editor_scene_).c_str());
        break;
    }
    case Ask::kReadIn:
        if (!level_editor_playtest_state().editor_scene_ || !ingest_file(level_editor_playtest_state().editor_scene_, false))
            hud::get_message("No trails file for this level");
        break;
    case Ask::kToggleKeepSimulation:
        simulation().keep_simulation_ = !simulation().keep_simulation_;
        break;
    case Ask::kNone:
        break;
    }
}

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::Circle>* circles,
                 std::vector<ui::Overlay::OverlayText>* texts)
{
    if (simulation().simulating_) {
        texts->push_back(
            {.x = static_cast<float>(core::at(viewport, 0)) + static_cast<float>(core::at(viewport, 2)) * kHalf - 150.0f,
             .y = static_cast<float>(core::at(viewport, 1)) + 36.0f, .argb = 0xFFFFD040,
             .text = simulation().keep_simulation_ ? "SIMULATING - F6 stops (objects stay)" : "SIMULATING - F6 stops (objects put back)"});
    }
    if (!level_editor_playtest_state().show_trails_)
        return;
    auto it = levels().find(level_editor_playtest_state().editor_scene_);
    if (it == levels().end())
        return;
    const std::vector<Run>& runs = it->second.runs();
    auto to_screen = [&](V3 p, float* x, float* y) {
        float nx = 0.0f, ny = 0.0f;
        if (!cast_onto(camera, frame, lens, p, near_z, &nx, &ny))
            return false;
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!clip_to_near(camera, frame, near_z + 0.01f, &a, &b))
            return;
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (to_screen(a, &ax, &ay) && to_screen(b, &bx, &by))
            lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
    };
    constexpr float kFar = 20000.0f, kLabelFar = 5000.0f;
    size_t budget = 60000;
    const int newest = runs.empty() ? 0 : runs.back().number;
    for (const Run& run : runs) {
        if (!run.visible)
            continue;
        const bool latest = run.number == newest;
        const uint32_t alpha = latest ? 0xFF000000u : 0xA0000000u;
        const float thickness = latest ? 2.0f : 1.25f;
        for (size_t i = 1; i < run.samples_.size() && budget; ++i) {
            const Sample& a = core::at(run.samples_, i - 1);
            const Sample& b = core::at(run.samples_, i);
            if (b.jump)
                continue;
            const V3 da = a.pos - camera;
            const V3 db = b.pos - camera;
            if (get_dot(da, da) > kFar * kFar && get_dot(db, db) > kFar * kFar)
                continue;
            line(a.pos, b.pos, (motion_colour(b.motion) & 0x00FFFFFF) | alpha, thickness);
            --budget;
        }
        // The run's start.
        if (!run.samples_.empty()) {
            const V3 s = run.samples_.front().pos;
            const V3 d = s - camera;
            float x = 0.0f, y = 0.0f;
            if (get_dot(d, d) < kLabelFar * kLabelFar && to_screen(s, &x, &y)) {
                circles->push_back({.x = x, .y = y, .radius = 5.0f, .argb = 0xFFFFFFFF, .filled = false, .thickness = 1.5f});
                texts->push_back({.x = x + 8.0f, .y = y - 8.0f, .argb = 0xFFFFFFFF, .text = "Run " + std::to_string(run.number)});
            }
        }
        // The markers.
        for (const Occurrence& e : run.events) {
            const V3 d = e.pos - camera;
            const float distance2 = get_dot(d, d);
            if (distance2 > kFar * kFar)
                continue;
            float x = 0.0f, y = 0.0f;
            if (e.kind == EventKind::kBail) {
                constexpr float kS = 30.0f;
                line(e.pos + V3{.x = -kS, .y = 0.0f, .z = -kS}, e.pos + V3{.x = kS, .y = 0.0f, .z = kS}, 0xFFFF3030, 2.0f);
                line(e.pos + V3{.x = -kS, .y = 0.0f, .z = kS}, e.pos + V3{.x = kS, .y = 0.0f, .z = -kS}, 0xFFFF3030, 2.0f);
                line(e.pos, e.pos + V3{.x = 0.0f, .y = 2.0f * kS, .z = 0.0f}, 0xFFFF3030, 1.0f);
                continue;
            }
            if (!to_screen(e.pos, &x, &y))
                continue;
            if (e.kind == EventKind::kGap) {
                const uint32_t colour = e.status == GapStatus::kLanded ? 0xFF40FF60
                                        : e.status == GapStatus::kLost ? 0xFFFF4040
                                                                       : 0xFFFFE040;
                circles->push_back({.x = x, .y = y, .radius = 7.0f, .argb = colour, .filled = true, .thickness = 1.0f});
                circles->push_back({.x = x, .y = y, .radius = 9.0f, .argb = 0xFF000000, .filled = false, .thickness = 1.0f});
                if (distance2 < kLabelFar * kLabelFar)
                    texts->push_back({.x = x + 10.0f, .y = y - 7.0f, .argb = colour,
                                      .text = e.name + (e.status == GapStatus::kLanded ? ""
                                                : e.status == GapStatus::kLost ? " (lost)"
                                                                               : " (pending)")});
            } else if (e.kind == EventKind::kCombo && distance2 < (kLabelFar * 0.6f) * (kLabelFar * 0.6f)) {
                texts->push_back({.x = x + 6.0f, .y = y + 4.0f, .argb = e.status == GapStatus::kLanded ? 0xFFE0E0E0 : 0xFFFF8080,
                                  .text = (e.status == GapStatus::kLanded ? "+" : "lost ") + std::to_string(e.value)});
            }
        }
    }
}

bool get_simulating() { return simulation().simulating_; }

bool gap_landed(const std::string& name)
{
    auto it = levels().find(level_editor_playtest_state().editor_scene_ ? level_editor_playtest_state().editor_scene_ : scene_now());
    return it != levels().end() && it->second.landed().count(name) != 0;
}

const char* gap_note(const std::string& name) { return gap_landed(name) ? "landed" : "untested"; }

void play_from_restart(uint32_t restart)
{
    if (!restart || !level_editor::live_now())
        return;
    level_editor_playtest_state().restart_ = restart;
    level_editor_playtest_state().restart_asked_ = SDL_GetTicks();
    level_editor_playtest_state().restart_frames_ = 0;
    get_log("level editor: play from the restart %s - closing the editor", nodes::caption(restart).c_str());
    perform::get_request(); // (closes into play as F5 does - back with F5 / Ctrl+P)
}

bool& window_open() { return level_editor_playtest_state().window_open_; }
void get_request(Ask ask) { level_editor_playtest_state().ask_ = ask; }

void delete_run(int number)
{
    auto it = levels().find(level_editor_playtest_state().editor_scene_);
    if (it == levels().end())
        return;
    std::vector<Run>& runs = it->second.runs();
    std::erase_if(runs, [number](const Run& r) { return r.number == number; });
}

void set_run_visible(int number, bool visible)
{
    auto it = levels().find(level_editor_playtest_state().editor_scene_);
    if (it == levels().end())
        return;
    for (Run& r : it->second.runs()) {
        if (r.number == number)
            r.visible = visible;
    }
}

bool& show_trails() { return level_editor_playtest_state().show_trails_; }

const LevelTrails* current_trails()
{
    auto it = levels().find(level_editor_playtest_state().editor_scene_);
    return it == levels().end() ? nullptr : &it->second;
}

std::vector<GapRead> get_level_gaps() { return read_gaps(); }
bool& recording()
{
    static bool on = config_int("editor", "trail_record", 1) != 0;
    return on;
}
bool& keep_simulation() { return simulation().keep_simulation_; }

std::string get_standing()
{
    const LevelTrails* t = current_trails();
    std::string s = recording() ? "recording runs in play" : "not recording";
    s += ", " + std::to_string(t ? t->runs().size() : 0) + " runs kept";
    if (simulation().simulating_)
        s += ", simulating";
    return s;
}

} // namespace frontend::level_editor::play_test
