// Self-tests for the level editor's play testing (src/game/front/level_editor_playtest.* - not original, ours): the
// trail's motion classification, thinning, gap and combo tracking, side file, and Simulate's key rule.

#include <SDL2/SDL.h>

#include <cmath>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_playtest.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace PT = Front::LevelEditor::PlayTest;
using PT::Motion;

void TestEditorTrailClassify()
{
    Check(PT::Classify(PT::kStateGround, false, 0, false, false) == Motion::Ground &&
              PT::Classify(PT::kStateAir, false, 0, false, false) == Motion::Air &&
              PT::Classify(PT::kStateWall, false, 0, false, false) == Motion::Wall &&
              PT::Classify(PT::kStateWallplant, false, 0, false, false) == Motion::Wall &&
              PT::Classify(PT::kStateLip, false, 0, false, false) == Motion::Lip &&
              PT::Classify(PT::kStateRail, false, 0, false, false) == Motion::Grind,
          "editor trail: the physics states (OnGround 0 .. InWallplant 5) to motions");
    Check(PT::Classify(PT::kStateGround, false, PT::kManual, false, false) == Motion::Manual &&
              PT::Classify(PT::kStateGround, false, PT::kNoseManual, false, false) == Motion::Manual &&
              PT::Classify(PT::kStateAir, false, PT::kManual, false, false) == Motion::Air &&
              PT::Classify(PT::kStateGround, false, 0x255ED86F, false, false) == Motion::Ground,
          "editor trail: a manual is the ground with a manual's balance trick");
    Check(PT::Classify(PT::kStateRail, true, 0, false, false) == Motion::Bail &&
              PT::Classify(PT::kStateGround, true, 0, true, true) == Motion::Bail &&
              PT::Classify(PT::kStateGround, false, 0, true, false) == Motion::Walk &&
              PT::Classify(PT::kStateGround, false, 0, true, true) == Motion::Drive &&
              PT::Classify(99, false, 0, false, false) == Motion::Ground,
          "editor trail: a bail first, then driving, walking; an unknown state is the ground");
    Motion m;
    bool names = true;
    for (int i = 0; i < PT::kMotions; ++i)
        names &= PT::MotionFromName(PT::MotionName(static_cast<Motion>(i)), &m) && m == static_cast<Motion>(i) &&
                 (PT::MotionColour(static_cast<Motion>(i)) >> 24) == 0xFF;
    Check(names && !PT::MotionFromName("fly", &m), "editor trail: motion names round trip, colours opaque");
}
SELFTEST(Early, TestEditorTrailClassify);

void TestEditorTrailRecorder()
{
    // A straight run, 10 units a frame for 100 frames: thinned to every kMaxStep.
    PT::Recorder r;
    for (int i = 0; i <= 100; ++i)
        r.Feed({i * 10.0f, 0.0f, 0.0f}, Motion::Ground);
    Check(r.samples.size() == 1 + 1000 / 150 && r.samples[1].pos.x == 150.0f && !r.samples[1].jump,
          "editor trail: a straight line kept every 150 units");
    // A motion change keeps the frame before it and the new one.
    PT::Recorder c;
    for (int i = 0; i < 5; ++i)
        c.Feed({i * 10.0f, 0.0f, 0.0f}, Motion::Ground);
    c.Feed({50.0f, 10.0f, 0.0f}, Motion::Air);
    Check(c.samples.size() == 3 && c.samples[1].pos.x == 40.0f && c.samples[1].motion == Motion::Ground &&
              c.samples[2].motion == Motion::Air && c.samples[2].pos.x == 50.0f,
          "editor trail: a motion change keeps its stretch's last frame");
    // A jump (a restart) marks the new point, keeps the last before.
    PT::Recorder j;
    j.Feed({0.0f, 0.0f, 0.0f}, Motion::Ground);
    j.Feed({10.0f, 0.0f, 0.0f}, Motion::Ground);
    j.Feed({5000.0f, 0.0f, 0.0f}, Motion::Ground);
    Check(j.samples.size() == 3 && !j.samples[1].jump && j.samples[2].jump, "editor trail: a jump isn't linked");
    // A turn: along x then along z, 30 units a step - each corner point kept.
    PT::Recorder t;
    t.Feed({0.0f, 0.0f, 0.0f}, Motion::Ground);
    for (int i = 1; i <= 5; ++i)
        t.Feed({i * 30.0f, 0.0f, 0.0f}, Motion::Ground);
    size_t straight = t.samples.size();
    t.Feed({150.0f, 0.0f, 30.0f}, Motion::Ground);
    Check(straight == 2 && t.samples.size() == 3 && t.samples[2].pos.z == 30.0f, "editor trail: a turn keeps a point");
    // Full: no more after kMaxSamples.
    PT::Recorder f;
    for (size_t i = 0; i < PT::kMaxSamples + 10; ++i)
        f.Feed({static_cast<float>(i) * 200.0f, 0.0f, 0.0f}, Motion::Air);
    Check(f.full && f.samples.size() == PT::kMaxSamples, "editor trail: a run stops at kMaxSamples");
}
SELFTEST(Early, TestEditorTrailRecorder);

void TestEditorTrailGaps()
{
    PT::GapTracker g;
    Check(g.Update({{"A", 0, 0, 100}, {"B", 0, 2, 50}}).empty(), "editor trail: the first read only learns the list");
    auto got = g.Update({{"A", 1, 0, 100}, {"B", 0, 2, 50}});
    Check(got.size() == 1 && got[0].kind == PT::GapChange::Got && got[0].name == "A" && got[0].score == 100,
          "editor trail: a gap got (pending up)");
    auto landed = g.Update({{"A", 0, 1, 100}, {"B", 0, 2, 50}});
    Check(landed.size() == 1 && landed[0].kind == PT::GapChange::Landed && landed[0].name == "A",
          "editor trail: landed (count up)");
    g.Update({{"A", 0, 1, 100}, {"B", 1, 2, 50}});
    auto lost = g.Update({{"A", 0, 1, 100}, {"B", 0, 2, 50}});
    Check(lost.size() == 1 && lost[0].kind == PT::GapChange::Lost && lost[0].name == "B", "editor trail: lost (a bail)");
    auto both = g.Update({{"A", 0, 2, 100}, {"B", 0, 2, 50}});
    Check(both.size() == 2 && both[0].kind == PT::GapChange::Got && both[1].kind == PT::GapChange::Landed,
          "editor trail: got and landed between two reads");
    auto added = g.Update({{"A", 0, 2, 100}, {"B", 0, 2, 50}, {"C", 0, 3, 10}});
    Check(added.empty(), "editor trail: a gap added to the list (landed before) isn't a landing");
    g.Forget();
    Check(!g.known && g.Update({{"A", 5, 9, 1}}).empty(), "editor trail: forgotten, the next read learns again");
}
SELFTEST(Early, TestEditorTrailGaps);

void TestEditorTrailCombo()
{
    PT::ComboTracker c;
    PT::ComboEnd end;
    bool decided = false;
    c.Update(100, false, &end);
    c.Update(500, false, &end);
    decided = c.Update(0, false, &end);
    int frames = 0;
    while (!decided && frames < 100) {
        decided = c.Update(0, false, &end);
        ++frames;
    }
    Check(decided && end.landed && end.points == 500 && frames == PT::kSettleFrames,
          "editor trail: a combo lands once it has settled");
    PT::ComboTracker b;
    b.Update(300, false, &end);
    bool early = b.Update(0, false, &end);
    b.Update(0, false, &end);
    decided = b.Update(0, true, &end);
    Check(!early && decided && !end.landed && end.points == 300, "editor trail: a bail while it settles - lost");
    PT::ComboTracker n;
    n.Update(700, false, &end);
    decided = n.Update(0, true, &end);
    Check(decided && !end.landed && end.points == 700, "editor trail: points gone as it bails - lost");
    PT::ComboTracker z;
    Check(!z.Update(0, true, &end) && !z.Update(0, false, &end), "editor trail: no points, no combo");
}
SELFTEST(Early, TestEditorTrailCombo);

void TestEditorTrailFile()
{
    PT::LevelTrails t;
    t.landed = {"Over the rail", "Big Gap 2"};
    PT::Run r;
    r.seconds = 12.5f;
    r.visible = false;
    r.samples = {{{1.5f, 2.0f, -3.5f}, Motion::Ground, false}, {{100.0f, 50.0f, 0.0f}, Motion::Air, false},
                 {{900.0f, 0.0f, 0.0f}, Motion::Grind, true}};
    r.events = {{PT::EventKind::Gap, {10.0f, 0.0f, 0.0f}, "Over the rail", 250, PT::GapStatus::Landed},
                {PT::EventKind::Bail, {20.0f, 0.0f, 0.0f}, "", 0, PT::GapStatus::Lost},
                {PT::EventKind::Combo, {30.0f, 0.0f, 0.0f}, "", 4500, PT::GapStatus::Landed}};
    t.Add(r);
    PT::Run r2;
    r2.samples = {{{0.0f, 0.0f, 0.0f}, Motion::Manual, false}};
    t.Add(r2);
    std::string text = PT::Format(t);
    PT::LevelTrails back;
    bool ok = PT::Parse(text, &back) && back.runs.size() == 2 && back.landed == t.landed && back.nextNumber == 3 &&
              back.runs[0].number == 1 && back.runs[1].number == 2 && !back.runs[0].visible &&
              std::fabs(back.runs[0].seconds - 12.5f) < 0.01f && back.runs[0].samples.size() == 3 &&
              back.runs[0].samples[0].pos.z == -3.5f && back.runs[0].samples[2].jump &&
              back.runs[0].samples[2].motion == Motion::Grind && back.runs[0].events.size() == 3 &&
              back.runs[0].events[0].name == "Over the rail" && back.runs[0].events[0].value == 250 &&
              back.runs[0].events[0].status == PT::GapStatus::Landed && back.runs[0].events[1].kind == PT::EventKind::Bail &&
              back.runs[0].events[2].value == 4500 && back.runs[1].samples[0].motion == Motion::Manual;
    Check(ok, "editor trail: the side file round trips");
    PT::LevelTrails untouched = t;
    Check(!PT::Parse("thug2-decomp camera bookmarks 1\n", &untouched) && untouched.runs.size() == 2,
          "editor trail: another file refused");
    PT::LevelTrails skip;
    Check(PT::Parse("thug2-decomp trails 1\r\ns 1 2 3 air 0\nrun 4 1.0 1\ns x\ns 1 2 3 swim 0\ns 1 2 3 air 0\nzzz\n", &skip) &&
              skip.runs.size() == 1 && skip.runs[0].number == 4 && skip.runs[0].samples.size() == 1 && skip.nextNumber == 5,
          "editor trail: bad lines skipped (a sample before a run, a bad motion)");
    PT::LevelTrails many;
    for (size_t i = 0; i < PT::kMaxRuns + 3; ++i)
        many.Add(PT::Run{});
    Check(many.runs.size() == PT::kMaxRuns && many.runs.front().number == 4 &&
              many.runs.back().number == static_cast<int>(PT::kMaxRuns) + 3,
          "editor trail: the oldest runs dropped past kMaxRuns");
    Check(PT::FileName(0x1234ABCD) == "1234ABCD.trails.txt", "editor trail: the file's name");
    Check(r.GapsLanded() == 1 && r.Bails() == 1 && r.BestCombo() == 4500, "editor trail: a run's counts");
}
SELFTEST(Early, TestEditorTrailFile);

void TestEditorSimulateKeys()
{
    Check(PT::SimulateKeyKeeps(SDL_SCANCODE_W, false, false, true) && !PT::SimulateKeyKeeps(SDL_SCANCODE_W, false, false, false) &&
              PT::SimulateKeyKeeps(SDL_SCANCODE_F6, false, false, false) &&
              PT::SimulateKeyKeeps(SDL_SCANCODE_KP_7, false, false, false) &&
              PT::SimulateKeyKeeps(SDL_SCANCODE_3, true, false, false) && PT::SimulateKeyKeeps(SDL_SCANCODE_LSHIFT, false, false, false) &&
              PT::SimulateKeyKeeps(SDL_SCANCODE_H, false, true, false),
          "editor simulate: flying, the view and bookmark keys keep it running");
    Check(!PT::SimulateKeyKeeps(SDL_SCANCODE_S, true, false, true) && !PT::SimulateKeyKeeps(SDL_SCANCODE_Z, true, false, true) &&
              !PT::SimulateKeyKeeps(SDL_SCANCODE_DELETE, false, false, true) &&
              !PT::SimulateKeyKeeps(SDL_SCANCODE_G, false, false, true) &&
              !PT::SimulateKeyKeeps(SDL_SCANCODE_UP, false, false, true) &&
              !PT::SimulateKeyKeeps(SDL_SCANCODE_ESCAPE, false, false, true) &&
              !PT::SimulateKeyKeeps(SDL_SCANCODE_G, false, true, true) && !PT::SimulateKeyKeeps(SDL_SCANCODE_R, false, false, true),
          "editor simulate: Ctrl+S / Z, Delete, G, the arrows, Escape, Alt+G, R stop it");
}
SELFTEST(Early, TestEditorSimulateKeys);

} // namespace
