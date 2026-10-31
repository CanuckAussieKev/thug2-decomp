// NOT ORIGINAL - play testing from the level editor (ours). See level_editor_playtest.h.

#include "game/front/level_editor_playtest.h"

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

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_play.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_transform.h"
#include "game/front/level_editor_ui.h"
#include "game/gfx/screen.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "game/sys/mlp.h"
#include "game/sys/sdl_events.h"
#include "game/sys/log.h"

namespace Front::LevelEditor::PlayTest {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

const char* const kMotionNames[kMotions] = {"ground", "air", "grind", "lip", "wall", "manual", "bail", "walk", "drive"};
// (ARGB: ground grey, air sky blue, grind orange, lip pink, wall violet, manual green, bail red, walking olive,
// driving cyan)
const uint32_t kMotionColours[kMotions] = {0xFFB4B4B4, 0xFF48A8FF, 0xFFFF9A20, 0xFFFF60C8, 0xFFB070FF,
                                           0xFF40E070, 0xFFFF3030, 0xFFA0A060, 0xFF50E0E0};

float Length(V3 v) { return std::sqrt(Dot(v, v)); }

} // namespace

const char* MotionName(Motion motion)
{
    int i = static_cast<int>(motion);
    return i >= 0 && i < kMotions ? kMotionNames[i] : "?";
}

uint32_t MotionColour(Motion motion)
{
    int i = static_cast<int>(motion);
    return i >= 0 && i < kMotions ? kMotionColours[i] : 0xFFFFFFFF;
}

bool MotionFromName(const std::string& name, Motion* motion)
{
    for (int i = 0; i < kMotions; ++i) {
        if (name == kMotionNames[i]) {
            *motion = static_cast<Motion>(i);
            return true;
        }
    }
    return false;
}

Motion Classify(int physicsState, bool bailing, uint32_t balanceTrick, bool walking, bool driving)
{
    if (bailing)
        return Motion::Bail;
    if (driving)
        return Motion::Drive;
    if (walking)
        return Motion::Walk;
    switch (physicsState) {
    case kStateAir:
        return Motion::Air;
    case kStateWall:
    case kStateWallplant:
        return Motion::Wall;
    case kStateLip:
        return Motion::Lip;
    case kStateRail:
        return Motion::Grind;
    default:
        return balanceTrick == kManual || balanceTrick == kNoseManual ? Motion::Manual : Motion::Ground;
    }
}

void Recorder::Feed(V3 pos, Motion motion)
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
    if (!haveLast || samples.empty()) {
        haveLast = true;
        last = now;
        lastKept = push(now);
        return;
    }
    V3 step = pos - last.pos;
    if (Dot(step, step) > kJump * kJump) {
        // (moved there: the stretch before ends where it was, the new one starts unlinked)
        if (!lastKept)
            push(last);
        now.jump = true;
        lastKept = push(now);
        last = now;
        return;
    }
    bool keep = false;
    if (motion != last.motion) {
        if (!lastKept)
            push(last); // (the last frame of the motion before: its stretch keeps its colour up to here)
        keep = true;
    } else {
        const Sample& kept = samples.back();
        V3 d = pos - kept.pos;
        float distance = Length(d);
        if (distance >= kMaxStep) {
            keep = true;
        } else if (distance >= kMinStep && samples.size() >= 2 && !kept.jump) {
            V3 before = kept.pos - samples[samples.size() - 2].pos;
            float lb = Length(before);
            if (lb > 0.001f) {
                float c = Dot(before, d) / (lb * distance);
                keep = c < std::cos(kTurnDegrees * 3.14159265f / 180.0f);
            }
        }
    }
    lastKept = keep && push(now);
    last = now;
}

std::vector<GapChange> GapTracker::Update(const std::vector<GapRead>& now)
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
        int oldPending = old ? old->pending : 0, oldCount = old ? old->count : g.count;
        if (g.pending > oldPending)
            changes.push_back({GapChange::Got, g.name, g.score});
        if (g.count > oldCount) {
            // (got and landed between two reads: the got first)
            if (oldPending == 0 && g.pending <= oldPending)
                changes.push_back({GapChange::Got, g.name, g.score});
            changes.push_back({GapChange::Landed, g.name, g.score});
        } else if (oldPending > 0 && g.pending == 0) {
            changes.push_back({GapChange::Lost, g.name, g.score});
        }
    }
    last = now;
    return changes;
}

bool ComboTracker::Update(int pendingNow, bool bailing, ComboEnd* end)
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
    if (pending > 0 && pendingNow <= 0) {
        if (!decided && bailing) {
            *end = {pending, false};
            decided = true;
        } else {
            ending = pending;
            settle = kSettleFrames;
        }
    }
    pending = pendingNow > 0 ? pendingNow : 0;
    return decided;
}

int Run::GapsLanded() const
{
    int n = 0;
    for (const Event& e : events)
        n += e.kind == EventKind::Gap && e.status == GapStatus::Landed;
    return n;
}

int Run::Bails() const
{
    int n = 0;
    for (const Event& e : events)
        n += e.kind == EventKind::Bail;
    return n;
}

int Run::BestCombo() const
{
    int best = 0;
    for (const Event& e : events) {
        if (e.kind == EventKind::Combo && e.status == GapStatus::Landed)
            best = std::max(best, e.value);
    }
    return best;
}

void LevelTrails::Add(Run run)
{
    if (run.number <= 0)
        run.number = nextNumber;
    nextNumber = std::max(nextNumber, run.number + 1);
    runs.push_back(std::move(run));
    while (runs.size() > kMaxRuns)
        runs.erase(runs.begin());
}

namespace {

const char* const kHeader = "thug2-decomp trails 1";
const char* EventWord(EventKind kind) { return kind == EventKind::Gap ? "gap" : kind == EventKind::Bail ? "bail" : "combo"; }
const char* StatusWord(GapStatus status)
{
    return status == GapStatus::Landed ? "landed" : status == GapStatus::Lost ? "lost" : "pending";
}
// (a name kept to one line)
std::string OneLine(const std::string& text)
{
    std::string out = text;
    for (char& c : out) {
        if (c == '\r' || c == '\n')
            c = ' ';
    }
    return out;
}

} // namespace

std::string Format(const LevelTrails& trails)
{
    std::string out = std::string(kHeader) + "\n";
    char line[256];
    for (const std::string& gap : trails.landed)
        out += "gap " + OneLine(gap) + "\n";
    for (const Run& run : trails.runs) {
        std::snprintf(line, sizeof(line), "run %d %.2f %d\n", run.number, run.seconds, run.visible ? 1 : 0);
        out += line;
        for (const Sample& s : run.samples) {
            std::snprintf(line, sizeof(line), "s %.1f %.1f %.1f %s %d\n", s.pos.x, s.pos.y, s.pos.z, MotionName(s.motion),
                          s.jump ? 1 : 0);
            out += line;
        }
        for (const Event& e : run.events) {
            std::snprintf(line, sizeof(line), "e %s %.1f %.1f %.1f %d %s ", EventWord(e.kind), e.pos.x, e.pos.y, e.pos.z,
                          e.value, StatusWord(e.status));
            out += line + OneLine(e.name) + "\n";
        }
    }
    return out;
}

bool Parse(const std::string& text, LevelTrails* trails)
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
            out.nextNumber = std::max(out.nextNumber, number + 1);
            run = &out.runs.back();
        } else if (line.rfind("s ", 0) == 0 && run) {
            std::istringstream fields(line.substr(2));
            Sample s;
            std::string motion;
            int jump = 0;
            if (!(fields >> s.pos.x >> s.pos.y >> s.pos.z >> motion >> jump) || !MotionFromName(motion, &s.motion))
                continue;
            s.jump = jump != 0;
            run->samples.push_back(s);
        } else if (line.rfind("e ", 0) == 0 && run) {
            std::istringstream fields(line.substr(2));
            Event e;
            std::string kind, status;
            if (!(fields >> kind >> e.pos.x >> e.pos.y >> e.pos.z >> e.value >> status))
                continue;
            if (kind == "gap")
                e.kind = EventKind::Gap;
            else if (kind == "bail")
                e.kind = EventKind::Bail;
            else if (kind == "combo")
                e.kind = EventKind::Combo;
            else
                continue;
            e.status = status == "landed" ? GapStatus::Landed : status == "lost" ? GapStatus::Lost : GapStatus::Pending;
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

std::string FileName(uint32_t scene)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.trails.txt", scene);
    return name;
}

bool SimulateKeyKeeps(int scancode, bool ctrl, bool alt, bool flyKeysSafe)
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
        return flyKeysSafe; // (flying - not when they nudge a selection)
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

constexpr uint32_t kSkater = 0x5B8AB877;             // Skater (the local skater's alias)
constexpr uint32_t kSkaterState = 0x43686585;        // SkaterState
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3; // SkaterBalanceTrick
constexpr uint32_t kSkaterScore = 0xD262802F;        // SkaterScore
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // (skaters)
constexpr uint32_t kCameraComponent = 0xC4E311FA;    // (camera objects)

template <typename T> T At(const void* base, uint32_t offset)
{
    return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(base) + offset);
}

uint8_t* Skate() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); } // Mdl::Skate

Game::CGapChecklist* Checklist()
{
    uint8_t* skate = Skate();
    return skate ? static_cast<Game::CGapChecklist*>(reinterpret_cast<Mdl::Skate*>(skate)->GetGapChecklist()) : nullptr;
}

// The skater's state (the header's offsets).
struct SkaterNow {
    V3 pos;
    Motion motion;
    bool bailing;
    int pending; // the combo's points
};
bool ReadSkater(Obj::CCompositeObject* skater, SkaterNow* out)
{
    Obj::CBaseComponent* state = skater->GetComponent(kSkaterState);
    if (!state)
        return false;
    int physics = At<int>(state, 0x18);
    bool bailing = At<uint8_t>(state, 0x70) != 0;
    bool driving = At<uint8_t>(state, 0x118) != 0;
    bool walking = !driving && At<int>(state, 0x1C) == 1;
    Obj::CBaseComponent* balance = skater->GetComponent(kSkaterBalanceTrick);
    uint32_t trick = balance ? At<uint32_t>(balance, 0x18) : 0;
    out->pos = {skater->m_pos.x, skater->m_pos.y, skater->m_pos.z};
    out->motion = Classify(physics, bailing, trick, walking, driving);
    out->bailing = bailing;
    out->pending = 0;
    if (Obj::CBaseComponent* score = skater->GetComponent(kSkaterScore)) {
        if (const uint8_t* s = At<const uint8_t*>(score, 0x18)) {
            double points = static_cast<double>(At<int>(s, 0x14)) * At<float>(s, 0x60);
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
    bool wasBailing = false;
    uint32_t lastTicks = 0;
    int refusedCheck = 0;
    bool refused = false;
};
Recording g_rec;

std::map<uint32_t, LevelTrails> g_levels;
std::set<uint32_t> g_fileTried;
uint32_t g_editorScene; // (the level the editor shows)
bool g_showTrails = true;
bool g_recordOn = ConfigInt("editor", "trail_record", 1) != 0;
bool g_windowOpen;
Ask g_ask = Ask::None;

// Play from a restart.
uint32_t g_restart;
uint32_t g_restartAsked;
int g_restartFrames;

// Simulate.
struct Snapshot {
    Objects::State state;
    Mth::Vector vel;
};
bool g_simulating;
bool g_keepSimulation;
std::vector<Snapshot> g_snapshots;
std::vector<uint32_t> g_touchedBefore;
bool g_handlerRegistered;
uint32_t g_simStarted;

uint32_t SceneNow() { return Scenery::SceneName(Scenery::MainScene()); }

std::string TrailsPath(uint32_t scene) { return GameFolderPath("thug2-decomp-layouts") + "\\" + FileName(scene); }

bool LoadFile(uint32_t scene, bool quiet)
{
    std::ifstream file(TrailsPath(scene), std::ios::binary);
    if (!file)
        return false;
    std::stringstream text;
    text << file.rdbuf();
    LevelTrails read;
    if (!Parse(text.str(), &read)) {
        Log("level editor: %s isn't a trails file - not read", TrailsPath(scene).c_str());
        return false;
    }
    g_levels[scene] = std::move(read);
    if (!quiet)
        Hud::Message("Trails read from " + FileName(scene));
    Log("level editor: trails read: %u runs, %u gaps landed (%s)", static_cast<unsigned>(g_levels[scene].runs.size()),
        static_cast<unsigned>(g_levels[scene].landed.size()), TrailsPath(scene).c_str());
    return true;
}

bool SaveFile(uint32_t scene)
{
    CreateDirectoryA(GameFolderPath("thug2-decomp-layouts").c_str(), nullptr);
    std::ofstream file(TrailsPath(scene), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << Format(g_levels[scene]);
    return static_cast<bool>(file);
}

std::vector<GapRead> ReadGaps()
{
    std::vector<GapRead> out;
    Game::CGapChecklist* list = Checklist();
    if (!list)
        return out;
    // (the list's nodes: the head's data is -1 - gap_checklist.cpp)
    int guard = 0;
    for (Mem::ListNode* node = list->m_gaps.next; node && node != &list->m_gaps && ++guard < 4096; node = node->next) {
        if (node->data == reinterpret_cast<void*>(-1))
            break;
        auto* gap = reinterpret_cast<Game::CGapCheck*>(node);
        out.push_back({gap->m_name.c_str() ? gap->m_name.c_str() : "", gap->m_pending, gap->m_count, gap->m_score});
    }
    return out;
}

void CloseRun(const char* why)
{
    if (!g_rec.open)
        return;
    g_rec.open = false;
    g_rec.run.samples = std::move(g_rec.recorder.samples);
    g_rec.run.full = g_rec.recorder.full;
    // (a combo still settling counts as landed)
    if (g_rec.combo.ending > 0 && !g_rec.run.samples.empty())
        g_rec.run.events.push_back({EventKind::Combo, g_rec.run.samples.back().pos, "", g_rec.combo.ending, GapStatus::Landed});
    if (g_rec.run.samples.size() < 2) {
        Log("level editor: trail - a run too short to keep (%s)", why);
        return;
    }
    LevelTrails& level = g_levels[g_rec.scene];
    g_rec.run.number = level.nextNumber;
    Log("level editor: trail run %d recorded (%s): %.1f s, %u points, %d gaps landed, %d bails, best combo %d%s",
        g_rec.run.number, why, g_rec.run.seconds, static_cast<unsigned>(g_rec.run.samples.size()), g_rec.run.GapsLanded(),
        g_rec.run.Bails(), g_rec.run.BestCombo(), g_rec.run.full ? " (full)" : "");
    level.Add(std::move(g_rec.run));
}

void OpenRun(int load)
{
    g_rec = Recording{};
    g_rec.open = true;
    g_rec.load = load;
    g_rec.scene = SceneNow();
    Log("level editor: trail - recording a run (the level's edited copy in play)");
}

void Record(Mdl::FrontEnd* frontEnd)
{
    int load = Layout::LevelLoads();
    if (g_rec.open && (g_rec.load != load || !g_recordOn))
        CloseRun(g_rec.load != load ? "another level loaded" : "recording turned off");
    bool paused = frontEnd && frontEnd->m_paused;
    auto* skater = static_cast<Obj::CCompositeObject*>(Obj::FindObject(kSkater));
    if (!g_recordOn || paused || !skater || Nx::CurrentLevel() == 0 || !Layout::EditingCopy()) {
        g_rec.lastTicks = 0;
        return;
    }
    // (a network session or a refused mode: looked at now and then)
    if (--g_rec.refusedCheck <= 0) {
        g_rec.refusedCheck = 60;
        g_rec.refused = Layout::Refused();
    }
    if (g_rec.refused) {
        if (g_rec.open)
            CloseRun("refused here");
        return;
    }
    SkaterNow now;
    if (!ReadSkater(skater, &now))
        return;
    if (!g_rec.open) {
        OpenRun(load);
        g_rec.refusedCheck = 60;
    }
    uint32_t ticks = SDL_GetTicks();
    float dt = g_rec.lastTicks ? std::min(0.1f, static_cast<float>(ticks - g_rec.lastTicks) / 1000.0f) : 0.0f;
    g_rec.lastTicks = ticks;
    Run& run = g_rec.run;
    run.seconds += dt;
    run.motionSeconds[static_cast<int>(now.motion)] += dt;
    g_rec.recorder.Feed(now.pos, now.motion);
    if (now.bailing && !g_rec.wasBailing)
        run.events.push_back({EventKind::Bail, now.pos, "", 0, GapStatus::Lost});
    g_rec.wasBailing = now.bailing;
    // The gaps: got, landed, lost.
    LevelTrails& level = g_levels[g_rec.scene];
    for (const GapChange& c : g_rec.gaps.Update(ReadGaps())) {
        if (c.kind == GapChange::Got) {
            run.events.push_back({EventKind::Gap, now.pos, c.name, c.score, GapStatus::Pending});
            continue;
        }
        bool marked = false;
        for (auto e = run.events.rbegin(); e != run.events.rend(); ++e) {
            if (e->kind == EventKind::Gap && e->status == GapStatus::Pending && e->name == c.name) {
                e->status = c.kind == GapChange::Landed ? GapStatus::Landed : GapStatus::Lost;
                marked = true;
                if (c.kind == GapChange::Landed)
                    break; // (one landing a got)
            }
        }
        if (c.kind == GapChange::Landed) {
            if (!marked)
                run.events.push_back({EventKind::Gap, now.pos, c.name, c.score, GapStatus::Landed});
            if (level.landed.insert(c.name).second)
                Log("level editor: trail - gap \"%s\" landed for the first time in a test run", c.name.c_str());
        }
    }
    ComboEnd end;
    if (g_rec.combo.Update(now.pending, now.bailing, &end) && end.points > 0)
        run.events.push_back({EventKind::Combo, now.pos, "", end.points, end.landed ? GapStatus::Landed : GapStatus::Lost});
}

void RestartStep(Mdl::FrontEnd* frontEnd)
{
    if (!g_restart)
        return;
    if (SDL_GetTicks() - g_restartAsked > 15000 && g_restartFrames == 0) {
        Log("level editor: play from a restart - dropped (the editor didn't close)");
        g_restart = 0;
        return;
    }
    if (++g_restartFrames > 300) {
        Log("level editor: play from a restart - the game didn't come back from the pause menu; not moved");
        g_restart = 0;
        return;
    }
    bool paused = frontEnd && frontEnd->m_paused;
    if (paused || g_restartFrames < 4 || !Obj::FindObject(kSkater) || Nx::CurrentLevel() == 0)
        return;
    uint32_t restart = g_restart;
    g_restart = 0;
    Script::CStruct* params = Script::NewStruct()->Init();
    params->AddChecksum(Crc::GenerateCRCFromString("node_name"), restart);
    Script::RunScript(Crc::GenerateCRCFromString("ResetSkaters"), params, nullptr, false, "ResetSkaters");
    params->Clear();
    Script::DeleteStruct(params);
    std::string label = Nodes::Label(restart);
    Log("level editor: play from the restart %s (ResetSkaters)", label.c_str());
    Layout::PanelMessage(("Playing from " + label + " (F5: return to editor)").c_str());
}

// --- simulate ---

struct SnapWalk {
    std::vector<Snapshot>* out;
};
void __cdecl SnapOne(Obj::CObject* object, void* data)
{
    auto* walk = static_cast<SnapWalk*>(data);
    auto* composite = static_cast<Obj::CCompositeObject*>(object);
    if ((composite->m_objectFlags & Obj::OBJECT_FLAG_DEAD) || (composite->m_compositeFlags & Obj::COMPOSITE_FLAG_DESTROYING))
        return;
    if (composite->GetComponent(kSkaterPhysicsControl) || composite->GetComponent(kCameraComponent))
        return;
    walk->out->push_back({Objects::Save(composite), composite->m_vel});
}

// The skaters paused again after the game is unpaused (the front end's own walk of them - FrontEnd::PauseGame: their
// vtable +0x2C pauses one).
void PauseSkaters()
{
    uint8_t* skate = Skate();
    if (!skate)
        return;
    uint32_t count = 0;
    for (auto* node = *reinterpret_cast<uint8_t**>(skate + 0x34); node && *reinterpret_cast<int*>(node + 4) != -1;
         node = *reinterpret_cast<uint8_t**>(node + 0xC))
        ++count;
    for (uint32_t i = 0; i < count; ++i) {
        auto* skater = reinterpret_cast<void***>(reinterpret_cast<Mdl::Skate*>(skate)->GetSkater(static_cast<int>(i)));
        if (skater)
            reinterpret_cast<void(__thiscall*)(void*)>((*skater)[0x2C / 4])(skater);
    }
}

void StopSimulation(const char* why);

void SimulateEvent(SDL_Event* event)
{
    if (!g_simulating || !LevelEditor::Active())
        return;
    if (event->type == SDL_KEYDOWN && !event->key.repeat && Ui::KeysToEditor()) {
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        bool ctrl = (event->key.keysym.mod & KMOD_CTRL) != 0, alt = (event->key.keysym.mod & KMOD_ALT) != 0;
        int x, y;
        bool right = (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
        bool flyKeysSafe = right || !Transform::FlyAlways();
        (void)keys;
        if (!SimulateKeyKeeps(event->key.keysym.scancode, ctrl, alt, flyKeysSafe))
            StopSimulation("a key that edits");
    } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT && Ui::MouseToEditor()) {
        StopSimulation("a click in the view");
    }
}

void StartSimulation()
{
    Mdl::FrontEnd* frontEnd = Mdl::FrontEndInstance();
    if (g_simulating || !frontEnd)
        return;
    if (!frontEnd->m_paused) {
        Hud::Message("Simulate unavailable here");
        return;
    }
    if (Layout::Refused()) {
        Hud::Message("Simulate unavailable here");
        return;
    }
    g_snapshots.clear();
    if (Obj::CCompositeObjectManager* manager = Obj::CompositeObjectManager()) {
        SnapWalk walk = {&g_snapshots};
        manager->ForEach(SnapOne, &walk);
    }
    g_touchedBefore = Objects::Touched();
    if (!g_handlerRegistered) {
        // (not from inside the event pump: Tick runs in the render)
        Platform::RegisterEventHandler(SimulateEvent);
        g_handlerRegistered = true;
    }
    frontEnd->PauseGame(false);
    PauseSkaters(); // (frozen - and still hidden, as the editor keeps the skater)
    g_simulating = true;
    g_simStarted = SDL_GetTicks();
    Log("level editor: simulate on (%u objects noted to put back)", static_cast<unsigned>(g_snapshots.size()));
    Hud::Message("Simulating (F6: stop)",
                 3.0);
}

void StopSimulation(const char* why)
{
    if (!g_simulating)
        return;
    g_simulating = false;
    if (Mdl::FrontEnd* frontEnd = Mdl::FrontEndInstance())
        frontEnd->PauseGame(true);
    int restored = 0, gone = 0, kept = 0;
    if (!g_keepSimulation) {
        const std::vector<uint32_t>& touched = Objects::Touched();
        for (const Snapshot& s : g_snapshots) {
            // (one the editor moved while it ran keeps the editor's change)
            bool editedNow = std::find(touched.begin(), touched.end(), s.state.id) != touched.end() &&
                             std::find(g_touchedBefore.begin(), g_touchedBefore.end(), s.state.id) == g_touchedBefore.end();
            if (editedNow) {
                ++kept;
                continue;
            }
            if (!Objects::Restore(s.state)) {
                ++gone;
                continue;
            }
            if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(s.state.id)))
                object->m_vel = s.vel;
            ++restored;
        }
    }
    g_snapshots.clear();
    Log("level editor: simulate off (%s) after %.1f s: %s", why, (SDL_GetTicks() - g_simStarted) / 1000.0,
        g_keepSimulation ? "objects left where they got to"
                         : ("put back " + std::to_string(restored) + " objects" +
                            (gone ? ", " + std::to_string(gone) + " gone" : "") +
                            (kept ? ", " + std::to_string(kept) + " moved in the editor meanwhile kept" : ""))
                               .c_str());
    Hud::Message("Simulation stopped",
                 2.0);
}

} // namespace

// --- the game side's entry points ---

void Update(Mdl::FrontEnd* frontEnd)
{
    if (!LevelEditor::Enabled() || Mlp::FramesRun() == 0)
        return;
    if (g_simulating) // (the editor closed some other way: the world put back first)
        StopSimulation("the editor closed");
    RestartStep(frontEnd);
    Record(frontEnd);
}

void OnEditorStart()
{
    CloseRun("the editor opened");
    g_editorScene = SceneNow();
    if (g_editorScene && g_fileTried.insert(g_editorScene).second) {
        auto it = g_levels.find(g_editorScene);
        if (it == g_levels.end() || it->second.runs.empty())
            LoadFile(g_editorScene, true);
    }
}

void OnEditorStop() { StopSimulation("the editor closed"); }

void Tick(bool dragging)
{
    g_editorScene = SceneNow();
    if (g_simulating && Layout::NetworkActive())
        StopSimulation("a network session");
    // (the keymap's - level_editor_actions.h: F6, F7 by default)
    if (Actions::Pressed(Actions::kSimulate) && !dragging)
        g_ask = Ask::ToggleSimulate;
    if (Actions::Pressed(Actions::kTrailWindow)) {
        if (Ui::Running())
            g_windowOpen = !g_windowOpen;
        else
            Hud::Message("Requires the editor UI (editor_ui=1)");
    }
    Ask ask = g_ask;
    g_ask = Ask::None;
    switch (ask) {
    case Ask::ToggleSimulate:
        if (g_simulating)
            StopSimulation("F6");
        else if (!dragging)
            StartSimulation();
        break;
    case Ask::Clear: {
        g_levels[g_editorScene] = LevelTrails{};
        Hud::Message("Trails cleared");
        break;
    }
    case Ask::Save: {
        bool ok = g_editorScene && SaveFile(g_editorScene);
        Hud::Message(ok ? "Trails saved: " + FileName(g_editorScene) : std::string("Trails couldn't be saved"));
        Log("level editor: trails %s %s", ok ? "saved to" : "not saved to", TrailsPath(g_editorScene).c_str());
        break;
    }
    case Ask::Load:
        if (!g_editorScene || !LoadFile(g_editorScene, false))
            Hud::Message("No trails file for this level");
        break;
    case Ask::ToggleKeepSimulation:
        g_keepSimulation = !g_keepSimulation;
        break;
    case Ask::None:
        break;
    }
}

void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Circle>* circles,
                std::vector<Ui::Overlay::Text>* texts)
{
    if (g_simulating) {
        texts->push_back({static_cast<float>(viewport[0]) + static_cast<float>(viewport[2]) * 0.5f - 150.0f,
                          static_cast<float>(viewport[1]) + 36.0f, 0xFFFFD040,
                          g_keepSimulation ? "SIMULATING - F6 stops (objects stay)" : "SIMULATING - F6 stops (objects put back)"});
    }
    if (!g_showTrails)
        return;
    auto it = g_levels.find(g_editorScene);
    if (it == g_levels.end())
        return;
    const std::vector<Run>& runs = it->second.runs;
    auto toScreen = [&](V3 p, float* x, float* y) {
        float nx, ny;
        if (!Project(camera, frame, lens, p, nearZ, &nx, &ny))
            return false;
        NdcToPixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!ClipToNear(camera, frame, nearZ + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by;
        if (toScreen(a, &ax, &ay) && toScreen(b, &bx, &by))
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
            if (Dot(da, da) > kFar * kFar && Dot(db, db) > kFar * kFar)
                continue;
            line(a.pos, b.pos, (MotionColour(b.motion) & 0x00FFFFFF) | alpha, thickness);
            --budget;
        }
        // The run's start.
        if (!run.samples.empty()) {
            V3 s = run.samples.front().pos;
            V3 d = s - camera;
            float x, y;
            if (Dot(d, d) < kLabelFar * kLabelFar && toScreen(s, &x, &y)) {
                circles->push_back({x, y, 5.0f, 0xFFFFFFFF, false, 1.5f});
                texts->push_back({x + 8.0f, y - 8.0f, 0xFFFFFFFF, "Run " + std::to_string(run.number)});
            }
        }
        // The markers.
        for (const Event& e : run.events) {
            V3 d = e.pos - camera;
            float distance2 = Dot(d, d);
            if (distance2 > kFar * kFar)
                continue;
            float x, y;
            if (e.kind == EventKind::Bail) {
                constexpr float s = 30.0f;
                line(e.pos + V3{-s, 0.0f, -s}, e.pos + V3{s, 0.0f, s}, 0xFFFF3030, 2.0f);
                line(e.pos + V3{-s, 0.0f, s}, e.pos + V3{s, 0.0f, -s}, 0xFFFF3030, 2.0f);
                line(e.pos, e.pos + V3{0.0f, 2.0f * s, 0.0f}, 0xFFFF3030, 1.0f);
                continue;
            }
            if (!toScreen(e.pos, &x, &y))
                continue;
            if (e.kind == EventKind::Gap) {
                uint32_t colour = e.status == GapStatus::Landed ? 0xFF40FF60 : e.status == GapStatus::Lost ? 0xFFFF4040 : 0xFFFFE040;
                circles->push_back({x, y, 7.0f, colour, true, 1.0f});
                circles->push_back({x, y, 9.0f, 0xFF000000, false, 1.0f});
                if (distance2 < kLabelFar * kLabelFar)
                    texts->push_back({x + 10.0f, y - 7.0f, colour,
                                      e.name + (e.status == GapStatus::Landed ? "" : e.status == GapStatus::Lost ? " (lost)" : " (pending)")});
            } else if (e.kind == EventKind::Combo && distance2 < (kLabelFar * 0.6f) * (kLabelFar * 0.6f)) {
                texts->push_back({x + 6.0f, y + 4.0f, e.status == GapStatus::Landed ? 0xFFE0E0E0 : 0xFFFF8080,
                                  (e.status == GapStatus::Landed ? "+" : "lost ") + std::to_string(e.value)});
            }
        }
    }
}

bool Simulating() { return g_simulating; }

bool GapLanded(const std::string& name)
{
    auto it = g_levels.find(g_editorScene ? g_editorScene : SceneNow());
    return it != g_levels.end() && it->second.landed.count(name) != 0;
}

const char* GapNote(const std::string& name) { return GapLanded(name) ? "landed" : "untested"; }

void PlayFromRestart(uint32_t restart)
{
    if (!restart || !LevelEditor::Active())
        return;
    g_restart = restart;
    g_restartAsked = SDL_GetTicks();
    g_restartFrames = 0;
    Log("level editor: play from the restart %s - closing the editor", Nodes::Label(restart).c_str());
    Play::Request(); // (closes into play as F5 does - back with F5 / Ctrl+P)
}

bool& WindowOpen() { return g_windowOpen; }
void Request(Ask ask) { g_ask = ask; }

void DeleteRun(int number)
{
    auto it = g_levels.find(g_editorScene);
    if (it == g_levels.end())
        return;
    std::vector<Run>& runs = it->second.runs;
    runs.erase(std::remove_if(runs.begin(), runs.end(), [number](const Run& r) { return r.number == number; }), runs.end());
}

void SetRunVisible(int number, bool visible)
{
    auto it = g_levels.find(g_editorScene);
    if (it == g_levels.end())
        return;
    for (Run& r : it->second.runs) {
        if (r.number == number)
            r.visible = visible;
    }
}

bool& ShowTrails() { return g_showTrails; }

const LevelTrails* CurrentTrails()
{
    auto it = g_levels.find(g_editorScene);
    return it == g_levels.end() ? nullptr : &it->second;
}

std::vector<GapRead> LevelGaps() { return ReadGaps(); }
bool& Recording() { return g_recordOn; }
bool& KeepSimulation() { return g_keepSimulation; }

std::string Status()
{
    const LevelTrails* t = CurrentTrails();
    std::string s = g_recordOn ? "recording runs in play" : "not recording";
    s += ", " + std::to_string(t ? t->runs.size() : 0) + " runs kept";
    if (g_simulating)
        s += ", simulating";
    return s;
}

} // namespace Front::LevelEditor::PlayTest
