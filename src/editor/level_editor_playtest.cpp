// NOT ORIGINAL - play testing from the level editor (ours). See level_editor_playtest.h.

#include "game/rider/rider.h"
#include "editor/level_editor_playtest.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <type_traits>
#include <vector>

#include <windows.h>

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

namespace frontend::level_editor::play_test {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

const char* const kMotionNames[kMotions] = {"ground", "air", "grind", "lip", "wall", "manual", "bail", "walk", "drive"};
// (ARGB: ground grey, air sky blue, grind orange, lip pink, wall violet, manual green, bail red, walking olive,
// driving cyan)
const uint32_t kMotionColours[kMotions] = {0xFFB4B4B4, 0xFF48A8FF, 0xFFFF9A20, 0xFFFF60C8, 0xFFB070FF,
                                           0xFF40E070, 0xFFFF3030, 0xFFA0A060, 0xFF50E0E0};

float len_of(V3 v) { return std::sqrt(get_dot(v, v)); }

} // namespace

const char* motion_name(Motion motion)
{
    int i = static_cast<int>(motion);
    return i >= 0 && i < kMotions ? kMotionNames[i] : "?";
}

uint32_t motion_colour(Motion motion)
{
    int i = static_cast<int>(motion);
    return i >= 0 && i < kMotions ? kMotionColours[i] : 0xFFFFFFFF;
}

bool motion_from_name(const std::string& name, Motion* motion)
{
    for (int i = 0; i < kMotions; ++i) {
        if (name == kMotionNames[i]) {
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
    if (full)
        return;
    auto push = [this](const Sample& s) {
        if (samples.size() >= kMaxSamples) {
            full = true;
            return false;
        }
        samples.push_back(s);
        return true;
    };
    Sample now = {pos, motion, false};
    if (!have_last_ || samples.empty()) {
        have_last_ = true;
        last = now;
        last_kept_ = push(now);
        return;
    }
    V3 step = pos - last.pos;
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
        const Sample& kept = samples.back();
        V3 d = pos - kept.pos;
        float distance = len_of(d);
        if (distance >= kMaxStep) {
            keep = true;
        } else if (distance >= kMinStep && samples.size() >= 2 && !kept.jump) {
            V3 before = kept.pos - samples[samples.size() - 2].pos;
            float lb = len_of(before);
            if (lb > 0.001f) {
                float c = get_dot(before, d) / (lb * distance);
                keep = c < std::cos(kTurnDegrees * 3.14159265f / 180.0f);
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
        int old_pending = old ? old->pending : 0, old_count = old ? old->count : g.count;
        if (g.pending > old_pending)
            changes.push_back({GapChange::kGot, g.name, g.score});
        if (g.count > old_count) {
            // (got and landed between two reads: the got first)
            if (old_pending == 0 && g.pending <= old_pending)
                changes.push_back({GapChange::kGot, g.name, g.score});
            changes.push_back({GapChange::kLanded, g.name, g.score});
        } else if (old_pending > 0 && g.pending == 0) {
            changes.push_back({GapChange::kLost, g.name, g.score});
        }
    }
    last = now;
    return changes;
}

bool ComboTracker::tick_frame(int pending_now, bool bailing, ComboEnd* end)
{
    bool decided = false;
    if (ending > 0) {
        if (bailing) {
            *end = {ending, false};
            ending = 0;
            decided = true;
        } else if (--settle <= 0) {
            *end = {ending, true};
            ending = 0;
            decided = true;
        }
    }
    if (pending > 0 && pending_now <= 0) {
        if (!decided && bailing) {
            *end = {pending, false};
            decided = true;
        } else {
            ending = pending;
            settle = kSettleFrames;
        }
    }
    pending = pending_now > 0 ? pending_now : 0;
    return decided;
}

int Run::gaps_landed() const
{
    int n = 0;
    for (const Occurrence& e : events)
        n += e.kind == EventKind::kGap && e.status == GapStatus::kLanded;
    return n;
}

int Run::bails() const
{
    int n = 0;
    for (const Occurrence& e : events)
        n += e.kind == EventKind::kBail;
    return n;
}

int Run::top_combo_value() const
{
    int best = 0;
    for (const Occurrence& e : events) {
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
    runs.push_back(std::move(run));
    while (runs.size() > kMaxRuns)
        runs.erase(runs.begin());
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
    char line[256];
    for (const std::string& gap : trails.landed)
        out += "gap " + one_line(gap) + "\n";
    for (const Run& run : trails.runs) {
        std::snprintf(line, sizeof(line), "run %d %.2f %d\n", run.number, run.seconds, run.visible ? 1 : 0);
        out += line;
        for (const Sample& s : run.samples) {
            std::snprintf(line, sizeof(line), "s %.1f %.1f %.1f %s %d\n", s.pos.x, s.pos.y, s.pos.z,
                          motion_name(s.motion), s.jump ? 1 : 0);
            out += line;
        }
        for (const Occurrence& e : run.events) {
            std::snprintf(line, sizeof(line), "e %s %.1f %.1f %.1f %d %s ", event_word(e.kind), e.pos.x, e.pos.y,
                          e.pos.z, e.value, status_word(e.status));
            out += line + one_line(e.name) + "\n";
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
        if (line.rfind("gap ", 0) == 0) {
            if (line.size() > 4)
                out.landed.insert(line.substr(4));
        } else if (line.rfind("run ", 0) == 0) {
            int number = 0, visible = 1;
            float seconds = 0.0f;
            if (sscanf_s(line.c_str() + 4, "%d %f %d", &number, &seconds, &visible) < 2 || number <= 0)
                continue;
            Run r;
            r.number = number;
            r.seconds = seconds;
            r.visible = visible != 0;
            out.runs.push_back(r);
            out.next_number_ = std::max(out.next_number_, number + 1);
            run = &out.runs.back();
        } else if (line.rfind("s ", 0) == 0 && run) {
            std::istringstream fields(line.substr(2));
            Sample s;
            std::string motion;
            int jump = 0;
            if (!(fields >> s.pos.x >> s.pos.y >> s.pos.z >> motion >> jump) || !motion_from_name(motion, &s.motion))
                continue;
            s.jump = jump != 0;
            run->samples.push_back(s);
        } else if (line.rfind("e ", 0) == 0 && run) {
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
            if (!rest.empty() && rest[0] == ' ')
                rest.erase(0, 1);
            e.name = rest;
            run->events.push_back(e);
        }
    }
    while (out.runs.size() > kMaxRuns)
        out.runs.erase(out.runs.begin());
    *trails = std::move(out);
    return true;
}

std::string get_file_name(uint32_t scene)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.trails.txt", scene);
    return name;
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
    bool digit = scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0;
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

modules::Skate* get_skate() { return modules::g_skate_module; }

goals::GapTally* get_checklist()
{
    modules::Skate* skate = get_skate();
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
    objects::ObjectModule* state = skater->fetch_module(kSkaterState);
    if (!state)
        return false;
    const auto* pose = static_cast<const objects::RiderPoseState*>(state);
    int physics = pose->cur_phase_;
    bool bailing = pose->is_on(objects::PoseFlag::kBailing) != 0;
    bool driving = pose->in_vehicle_ != 0;
    bool walking = !driving && pose->physics_mode_ == 1;
    objects::ObjectModule* balance = skater->fetch_module(kSkaterBalanceTrick);
    uint32_t trick = balance ? static_cast<const objects::RiderTeeterMoves*>(balance)->trick_ : 0;
    out->pos = {skater->at_point_.x, skater->at_point_.y, skater->at_point_.z};
    out->motion = get_classify(physics, bailing, trick, walking, driving);
    out->bailing = bailing;
    out->pending = 0;
    if (objects::ObjectModule* score = skater->fetch_module(kSkaterScore)) {
        if (const modules::PointTally* s = static_cast<const objects::RiderPointKeeper*>(score)->points_held_) {
            double points = static_cast<double>(s->pot_) * s->multiplier_;
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
Recording g_rec;

std::map<uint32_t, LevelTrails> g_levels;
std::set<uint32_t> g_file_tried;
uint32_t g_editor_scene; // (the level the editor shows)
bool g_show_trails = true;
bool g_record_on = config_int("editor", "trail_record", 1) != 0;
bool g_window_open;
Ask g_ask = Ask::kNone;

// Play from a restart.
uint32_t g_restart;
uint32_t g_restart_asked;
int g_restart_frames;

// Simulate.
struct Snapshot {
    object_edit::State state;
    maths::Vec4 vel;
};
bool g_simulating;
bool g_keep_simulation;
std::vector<Snapshot> g_snapshots;
std::vector<uint32_t> g_touched_before;
bool g_handler_registered;
uint32_t g_sim_started;

uint32_t scene_now() { return scenery::world_scene_key(scenery::main_scene()); }

std::string trails_path(uint32_t scene)
{
    return game_folder_path("thug2-decomp-layouts") + "\\" + get_file_name(scene);
}

bool ingest_file(uint32_t scene, bool quiet)
{
    std::ifstream file(trails_path(scene), std::ios::binary);
    if (!file)
        return false;
    std::stringstream text;
    text << file.rdbuf();
    LevelTrails read;
    if (!parse(text.str(), &read)) {
        get_log("level editor: %s isn't a trails file - not read", trails_path(scene).c_str());
        return false;
    }
    g_levels[scene] = std::move(read);
    if (!quiet)
        hud::get_message("Trails read from " + get_file_name(scene));
    get_log("level editor: trails read: %u runs, %u gaps landed (%s)",
            static_cast<unsigned>(g_levels[scene].runs.size()), static_cast<unsigned>(g_levels[scene].landed.size()),
            trails_path(scene).c_str());
    return true;
}

bool save_file(uint32_t scene)
{
    CreateDirectoryA(game_folder_path("thug2-decomp-layouts").c_str(), nullptr);
    std::ofstream file(trails_path(scene), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << get_format(g_levels[scene]);
    return static_cast<bool>(file);
}

std::vector<GapRead> read_gaps()
{
    std::vector<GapRead> out;
    goals::GapTally* list = get_checklist();
    if (!list)
        return out;
    // (the list's nodes: the head's data is -1 - gap_checklist.cpp)
    int guard = 0;
    for (memory::ListNode* node = list->gaps_.next; node && node != &list->gaps_ && ++guard < 4096; node = node->next) {
        if (node->data == reinterpret_cast<void*>(-1))
            break;
        auto* gap = reinterpret_cast<goals::GapEntry*>(node);
        out.push_back({gap->label_.c_str() ? gap->label_.c_str() : "", gap->pending_, gap->num_, gap->points_held_});
    }
    return out;
}

void close_run(const char* why)
{
    if (!g_rec.open)
        return;
    g_rec.open = false;
    g_rec.run.samples = std::move(g_rec.recorder.samples);
    g_rec.run.full = g_rec.recorder.full;
    // (a combo still settling counts as landed)
    if (g_rec.combo.ending > 0 && !g_rec.run.samples.empty())
        g_rec.run.events.push_back(
            {EventKind::kCombo, g_rec.run.samples.back().pos, "", g_rec.combo.ending, GapStatus::kLanded});
    if (g_rec.run.samples.size() < 2) {
        get_log("level editor: trail - a run too short to keep (%s)", why);
        return;
    }
    LevelTrails& level = g_levels[g_rec.scene];
    g_rec.run.number = level.next_number_;
    get_log("level editor: trail run %d recorded (%s): %.1f s, %u points, %d gaps landed, %d bails, best combo %d%s",
            g_rec.run.number, why, g_rec.run.seconds, static_cast<unsigned>(g_rec.run.samples.size()),
            g_rec.run.gaps_landed(), g_rec.run.bails(), g_rec.run.top_combo_value(), g_rec.run.full ? " (full)" : "");
    level.append_one(std::move(g_rec.run));
}

void open_run(int load)
{
    g_rec = Recording{};
    g_rec.open = true;
    g_rec.load = load;
    g_rec.scene = scene_now();
    get_log("level editor: trail - recording a run (the level's edited copy in play)");
}

void link_record(modules::FrontEnd* front_end)
{
    int load = layout::level_loads();
    if (g_rec.open && (g_rec.load != load || !g_record_on))
        close_run(g_rec.load != load ? "another level loaded" : "recording turned off");
    bool paused = front_end && front_end->is_halted_;
    auto* skater = static_cast<objects::GameEntity*>(objects::find_object(kSkater));
    if (!g_record_on || paused || !skater || render::current_level() == 0 || !layout::editing_copy()) {
        g_rec.last_ticks_ = 0;
        return;
    }
    // (a network session or a refused mode: looked at now and then)
    if (--g_rec.refused_check_ <= 0) {
        g_rec.refused_check_ = 60;
        g_rec.refused = layout::get_refused();
    }
    if (g_rec.refused) {
        if (g_rec.open)
            close_run("refused here");
        return;
    }
    SkaterNow now;
    if (!read_skater(skater, &now))
        return;
    if (!g_rec.open) {
        open_run(load);
        g_rec.refused_check_ = 60;
    }
    uint32_t ticks = SDL_GetTicks();
    float dt = g_rec.last_ticks_ ? std::min(0.1f, static_cast<float>(ticks - g_rec.last_ticks_) / 1000.0f) : 0.0f;
    g_rec.last_ticks_ = ticks;
    Run& run = g_rec.run;
    run.seconds += dt;
    run.motion_seconds_[static_cast<int>(now.motion)] += dt;
    g_rec.recorder.feed(now.pos, now.motion);
    if (now.bailing && !g_rec.was_bailing_)
        run.events.push_back({EventKind::kBail, now.pos, "", 0, GapStatus::kLost});
    g_rec.was_bailing_ = now.bailing;
    // The gaps: got, landed, lost.
    LevelTrails& level = g_levels[g_rec.scene];
    for (const GapChange& c : g_rec.gaps.tick_frame(read_gaps())) {
        if (c.kind == GapChange::kGot) {
            run.events.push_back({EventKind::kGap, now.pos, c.name, c.score, GapStatus::kAwaiting});
            continue;
        }
        bool marked = false;
        for (auto e = run.events.rbegin(); e != run.events.rend(); ++e) {
            if (e->kind == EventKind::kGap && e->status == GapStatus::kAwaiting && e->name == c.name) {
                e->status = c.kind == GapChange::kLanded ? GapStatus::kLanded : GapStatus::kLost;
                marked = true;
                if (c.kind == GapChange::kLanded)
                    break; // (one landing a got)
            }
        }
        if (c.kind == GapChange::kLanded) {
            if (!marked)
                run.events.push_back({EventKind::kGap, now.pos, c.name, c.score, GapStatus::kLanded});
            if (level.landed.insert(c.name).second)
                get_log("level editor: trail - gap \"%s\" landed for the first time in a test run", c.name.c_str());
        }
    }
    ComboEnd end;
    if (g_rec.combo.tick_frame(now.pending, now.bailing, &end) && end.points > 0)
        run.events.push_back(
            {EventKind::kCombo, now.pos, "", end.points, end.landed ? GapStatus::kLanded : GapStatus::kLost});
}

void restart_step(modules::FrontEnd* front_end)
{
    if (!g_restart)
        return;
    if (SDL_GetTicks() - g_restart_asked > 15000 && g_restart_frames == 0) {
        get_log("level editor: play from a restart - dropped (the editor didn't close)");
        g_restart = 0;
        return;
    }
    if (++g_restart_frames > 300) {
        get_log("level editor: play from a restart - the game didn't come back from the pause menu; not moved");
        g_restart = 0;
        return;
    }
    bool paused = front_end && front_end->is_halted_;
    if (paused || g_restart_frames < 4 || !objects::find_object(kSkater) || render::current_level() == 0)
        return;
    uint32_t restart = g_restart;
    g_restart = 0;
    script::Struct* params = new (script::spawn_param_bag()) script::Struct;
    params->put_name_key(checksums::hash_from_text("node_name"), restart);
    script::launch_script(checksums::hash_from_text("ResetSkaters"), params, nullptr, false, "ResetSkaters");
    params->get_clear();
    script::delete_struct(params);
    std::string label = nodes::caption(restart);
    get_log("level editor: play from the restart %s (ResetSkaters)", label.c_str());
    layout::panel_message(("Playing from " + label + " (F5: return to editor)").c_str());
}

// --- simulate ---

struct SnapWalk {
    std::vector<Snapshot>* out;
};
void __cdecl snap_one(objects::Entity* object, void* data)
{
    auto* walk = static_cast<SnapWalk*>(data);
    auto* composite = static_cast<objects::GameEntity*>(object);
    if ((composite->object_flags_ & objects::kObjectFlagDead) ||
        (composite->composite_flags_ & objects::kCompositeFlagDestroying))
        return;
    if (composite->fetch_module(kSkaterPhysicsControl) || composite->fetch_module(kCamModuleName))
        return;
    walk->out->push_back({object_edit::store_away(composite), composite->vel_});
}

// The skaters paused again after the game is unpaused (the front end's own walk of them - FrontEnd::PauseGame: their
// vtable +0x2C pauses one).
void pause_skaters()
{
    modules::Skate* skate = get_skate();
    if (!skate)
        return;
    uint32_t count = 0;
    for (const memory::ListNode* node = skate->riders_.list_link_.next; node && !node->is_head(); node = node->next)
        ++count;
    for (uint32_t i = 0; i < count; ++i) {
        auto* skater = static_cast<objects::Rider*>(skate->get_rider_at(static_cast<int>(i)));
        if (skater)
            skater->get_freeze();
    }
}

void stop_simulation(const char* why);

void simulate_event(SDL_Event* event)
{
    if (!g_simulating || !level_editor::live_now())
        return;
    if (event->type == SDL_KEYDOWN && !event->key.repeat && ui::keys_to_editor()) {
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        bool ctrl = (event->key.keysym.mod & KMOD_CTRL) != 0, alt = (event->key.keysym.mod & KMOD_ALT) != 0;
        int x, y;
        bool right = (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
        bool fly_keys_safe = right || !xform::fly_always();
        (void)keys;
        if (!simulate_key_keeps(event->key.keysym.scancode, ctrl, alt, fly_keys_safe))
            stop_simulation("a key that edits");
    } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT && ui::mouse_to_editor()) {
        stop_simulation("a click in the view");
    }
}

void start_simulation()
{
    modules::FrontEnd* front_end = modules::front_end_instance();
    if (g_simulating || !front_end)
        return;
    if (!front_end->is_halted_) {
        hud::get_message("Simulate unavailable here");
        return;
    }
    if (layout::get_refused()) {
        hud::get_message("Simulate unavailable here");
        return;
    }
    g_snapshots.clear();
    if (objects::EntityHub* manager = objects::object_hub_slot()) {
        SnapWalk walk = {&g_snapshots};
        manager->for_each(snap_one, &walk);
    }
    g_touched_before = object_edit::get_touched();
    if (!g_handler_registered) {
        // (not from inside the event pump: Tick runs in the render)
        os_layer::register_event_handler(simulate_event);
        g_handler_registered = true;
    }
    front_end->get_pause_game(false);
    pause_skaters(); // (frozen - and still hidden, as the editor keeps the skater)
    g_simulating = true;
    g_sim_started = SDL_GetTicks();
    get_log("level editor: simulate on (%u objects noted to put back)", static_cast<unsigned>(g_snapshots.size()));
    hud::get_message("Simulating (F6: stop)", 3.0);
}

void stop_simulation(const char* why)
{
    if (!g_simulating)
        return;
    g_simulating = false;
    if (modules::FrontEnd* front_end = modules::front_end_instance())
        front_end->get_pause_game(true);
    int restored = 0, gone = 0, kept = 0;
    if (!g_keep_simulation) {
        const std::vector<uint32_t>& touched = object_edit::get_touched();
        for (const Snapshot& s : g_snapshots) {
            // (one the editor moved while it ran keeps the editor's change)
            bool edited_now =
                std::find(touched.begin(), touched.end(), s.state.id) != touched.end() &&
                std::find(g_touched_before.begin(), g_touched_before.end(), s.state.id) == g_touched_before.end();
            if (edited_now) {
                ++kept;
                continue;
            }
            if (!object_edit::get_restore(s.state)) {
                ++gone;
                continue;
            }
            if (auto* object = static_cast<objects::GameEntity*>(objects::find_object(s.state.id)))
                object->vel_ = s.vel;
            ++restored;
        }
    }
    g_snapshots.clear();
    get_log("level editor: simulate off (%s) after %.1f s: %s", why, (SDL_GetTicks() - g_sim_started) / 1000.0,
            g_keep_simulation ? "objects left where they got to"
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
    if (g_simulating) // (the editor closed some other way: the world put back first)
        stop_simulation("the editor closed");
    restart_step(front_end);
    link_record(front_end);
}

void on_editor_start()
{
    close_run("the editor opened");
    g_editor_scene = scene_now();
    if (g_editor_scene && g_file_tried.insert(g_editor_scene).second) {
        auto it = g_levels.find(g_editor_scene);
        if (it == g_levels.end() || it->second.runs.empty())
            ingest_file(g_editor_scene, true);
    }
}

void on_editor_stop() { stop_simulation("the editor closed"); }

void tick(bool dragging)
{
    g_editor_scene = scene_now();
    if (g_simulating && layout::network_active())
        stop_simulation("a network session");
    // (the keymap's - level_editor_actions.h: F6, F7 by default)
    if (actions::squeezed(actions::kSimulate) && !dragging)
        g_ask = Ask::kToggleSimulate;
    if (actions::squeezed(actions::kTrailWindow)) {
        if (ui::underway())
            g_window_open = !g_window_open;
        else
            hud::get_message("Requires the editor UI (editor_ui=1)");
    }
    Ask ask = g_ask;
    g_ask = Ask::kNone;
    switch (ask) {
    case Ask::kToggleSimulate:
        if (g_simulating)
            stop_simulation("F6");
        else if (!dragging)
            start_simulation();
        break;
    case Ask::kClear: {
        g_levels[g_editor_scene] = LevelTrails{};
        hud::get_message("Trails cleared");
        break;
    }
    case Ask::kStoreAway: {
        bool ok = g_editor_scene && save_file(g_editor_scene);
        hud::get_message(ok ? "Trails saved: " + get_file_name(g_editor_scene)
                            : std::string("Trails couldn't be saved"));
        get_log("level editor: trails %s %s", ok ? "saved to" : "not saved to", trails_path(g_editor_scene).c_str());
        break;
    }
    case Ask::kReadIn:
        if (!g_editor_scene || !ingest_file(g_editor_scene, false))
            hud::get_message("No trails file for this level");
        break;
    case Ask::kToggleKeepSimulation:
        g_keep_simulation = !g_keep_simulation;
        break;
    case Ask::kNone:
        break;
    }
}

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, const uint32_t viewport[4], float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::Circle>* circles,
                 std::vector<ui::Overlay::text_tools>* texts)
{
    if (g_simulating) {
        texts->push_back(
            {static_cast<float>(viewport[0]) + static_cast<float>(viewport[2]) * 0.5f - 150.0f,
             static_cast<float>(viewport[1]) + 36.0f, 0xFFFFD040,
             g_keep_simulation ? "SIMULATING - F6 stops (objects stay)" : "SIMULATING - F6 stops (objects put back)"});
    }
    if (!g_show_trails)
        return;
    auto it = g_levels.find(g_editor_scene);
    if (it == g_levels.end())
        return;
    const std::vector<Run>& runs = it->second.runs;
    auto to_screen = [&](V3 p, float* x, float* y) {
        float nx, ny;
        if (!cast_onto(camera, frame, lens, p, near_z, &nx, &ny))
            return false;
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!clip_to_near(camera, frame, near_z + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by;
        if (to_screen(a, &ax, &ay) && to_screen(b, &bx, &by))
            lines->push_back({ax, ay, bx, by, argb, thickness});
    };
    constexpr float kFar = 20000.0f, kLabelFar = 5000.0f;
    size_t budget = 60000;
    int newest = runs.empty() ? 0 : runs.back().number;
    for (const Run& run : runs) {
        if (!run.visible)
            continue;
        bool latest = run.number == newest;
        uint32_t alpha = latest ? 0xFF000000u : 0xA0000000u;
        float thickness = latest ? 2.0f : 1.25f;
        for (size_t i = 1; i < run.samples.size() && budget; ++i) {
            const Sample& a = run.samples[i - 1];
            const Sample& b = run.samples[i];
            if (b.jump)
                continue;
            V3 da = a.pos - camera, db = b.pos - camera;
            if (get_dot(da, da) > kFar * kFar && get_dot(db, db) > kFar * kFar)
                continue;
            line(a.pos, b.pos, (motion_colour(b.motion) & 0x00FFFFFF) | alpha, thickness);
            --budget;
        }
        // The run's start.
        if (!run.samples.empty()) {
            V3 s = run.samples.front().pos;
            V3 d = s - camera;
            float x, y;
            if (get_dot(d, d) < kLabelFar * kLabelFar && to_screen(s, &x, &y)) {
                circles->push_back({x, y, 5.0f, 0xFFFFFFFF, false, 1.5f});
                texts->push_back({x + 8.0f, y - 8.0f, 0xFFFFFFFF, "Run " + std::to_string(run.number)});
            }
        }
        // The markers.
        for (const Occurrence& e : run.events) {
            V3 d = e.pos - camera;
            float distance2 = get_dot(d, d);
            if (distance2 > kFar * kFar)
                continue;
            float x, y;
            if (e.kind == EventKind::kBail) {
                constexpr float kS = 30.0f;
                line(e.pos + V3{-kS, 0.0f, -kS}, e.pos + V3{kS, 0.0f, kS}, 0xFFFF3030, 2.0f);
                line(e.pos + V3{-kS, 0.0f, kS}, e.pos + V3{kS, 0.0f, -kS}, 0xFFFF3030, 2.0f);
                line(e.pos, e.pos + V3{0.0f, 2.0f * kS, 0.0f}, 0xFFFF3030, 1.0f);
                continue;
            }
            if (!to_screen(e.pos, &x, &y))
                continue;
            if (e.kind == EventKind::kGap) {
                uint32_t colour = e.status == GapStatus::kLanded ? 0xFF40FF60
                                  : e.status == GapStatus::kLost ? 0xFFFF4040
                                                                 : 0xFFFFE040;
                circles->push_back({x, y, 7.0f, colour, true, 1.0f});
                circles->push_back({x, y, 9.0f, 0xFF000000, false, 1.0f});
                if (distance2 < kLabelFar * kLabelFar)
                    texts->push_back({x + 10.0f, y - 7.0f, colour,
                                      e.name + (e.status == GapStatus::kLanded ? ""
                                                : e.status == GapStatus::kLost ? " (lost)"
                                                                               : " (pending)")});
            } else if (e.kind == EventKind::kCombo && distance2 < (kLabelFar * 0.6f) * (kLabelFar * 0.6f)) {
                texts->push_back({x + 6.0f, y + 4.0f, e.status == GapStatus::kLanded ? 0xFFE0E0E0 : 0xFFFF8080,
                                  (e.status == GapStatus::kLanded ? "+" : "lost ") + std::to_string(e.value)});
            }
        }
    }
}

bool get_simulating() { return g_simulating; }

bool gap_landed(const std::string& name)
{
    auto it = g_levels.find(g_editor_scene ? g_editor_scene : scene_now());
    return it != g_levels.end() && it->second.landed.count(name) != 0;
}

const char* gap_note(const std::string& name) { return gap_landed(name) ? "landed" : "untested"; }

void play_from_restart(uint32_t restart)
{
    if (!restart || !level_editor::live_now())
        return;
    g_restart = restart;
    g_restart_asked = SDL_GetTicks();
    g_restart_frames = 0;
    get_log("level editor: play from the restart %s - closing the editor", nodes::caption(restart).c_str());
    perform::get_request(); // (closes into play as F5 does - back with F5 / Ctrl+P)
}

bool& window_open() { return g_window_open; }
void get_request(Ask ask) { g_ask = ask; }

void delete_run(int number)
{
    auto it = g_levels.find(g_editor_scene);
    if (it == g_levels.end())
        return;
    std::vector<Run>& runs = it->second.runs;
    runs.erase(std::remove_if(runs.begin(), runs.end(), [number](const Run& r) { return r.number == number; }),
               runs.end());
}

void set_run_visible(int number, bool visible)
{
    auto it = g_levels.find(g_editor_scene);
    if (it == g_levels.end())
        return;
    for (Run& r : it->second.runs) {
        if (r.number == number)
            r.visible = visible;
    }
}

bool& show_trails() { return g_show_trails; }

const LevelTrails* current_trails()
{
    auto it = g_levels.find(g_editor_scene);
    return it == g_levels.end() ? nullptr : &it->second;
}

std::vector<GapRead> get_level_gaps() { return read_gaps(); }
bool& recording() { return g_record_on; }
bool& keep_simulation() { return g_keep_simulation; }

std::string get_standing()
{
    const LevelTrails* t = current_trails();
    std::string s = g_record_on ? "recording runs in play" : "not recording";
    s += ", " + std::to_string(t ? t->runs.size() : 0) + " runs kept";
    if (g_simulating)
        s += ", simulating";
    return s;
}

} // namespace frontend::level_editor::play_test
