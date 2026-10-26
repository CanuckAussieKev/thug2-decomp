// Self-tests for the level editor's camera tools, play from here and preferences (src/game/front/level_editor_camera.*,
// level_editor_play.*, level_editor_prefs.* - not original, ours): their pure pieces.

#include <SDL2/SDL.h>

#include <cmath>
#include <cstring>
#include <string>

#include "game/front/level_editor.h"
#include "game/front/level_editor_camera.h"
#include "game/front/level_editor_play.h"
#include "game/front/level_editor_prefs.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace Camera = Front::LevelEditor::Camera;
namespace Play = Front::LevelEditor::Play;
namespace Prefs = Front::LevelEditor::Prefs;

constexpr float kPi = 3.14159265f;

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool NearV(LE::V3 a, LE::V3 b, float eps = 1e-2f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

void TestEditorCameraFlight()
{
    Check(Camera::Ease(0.0f) == 0.0f && Camera::Ease(1.0f) == 1.0f && Near(Camera::Ease(0.5f), 0.5f) &&
              Camera::Ease(0.25f) < 0.25f && Camera::Ease(2.0f) == 1.0f && Camera::Ease(-1.0f) == 0.0f,
          "editor camera: Ease is a smoothstep, clamped");
    // the yaw the shorter way round (170 degrees to -170 goes through 180, not back through 0)
    float a = 170.0f * kPi / 180.0f, b = -170.0f * kPi / 180.0f;
    Check(Near(Camera::NearestAngle(a, b), 190.0f * kPi / 180.0f) && Near(Camera::NearestAngle(b, a), -190.0f * kPi / 180.0f) &&
              Near(Camera::NearestAngle(0.1f, 0.3f), 0.3f),
          "editor camera: NearestAngle takes the shorter way");
    Camera::Pose from = {{0.0f, 0.0f, 0.0f}, a, 0.0f}, to = {{100.0f, 200.0f, -300.0f}, b, 0.5f};
    Camera::Pose start = Camera::Blend(from, to, 0.0f), end = Camera::Blend(from, to, 1.0f), mid = Camera::Blend(from, to, 0.5f);
    Check(NearV(start.pos, from.pos) && Near(start.yaw, a) && NearV(end.pos, to.pos) && Near(end.pitch, 0.5f) &&
              Near(std::remainder(end.yaw - b, 2.0f * kPi), 0.0f) && NearV(mid.pos, {50.0f, 100.0f, -150.0f}) &&
              Near(mid.yaw, kPi),
          "editor camera: Blend goes from the start to the end pose, the yaw through 180 degrees");
}
SELFTEST(Early, TestEditorCameraFlight);

void TestEditorCameraFrame()
{
    // A sphere of radius 100 framed through a 60-degree lens: every edge point inside the view.
    float tanHalf = std::tan(30.0f * kPi / 180.0f);
    float d = Camera::FrameDistance(100.0f, tanHalf, 10.0f);
    LE::Frame frame = LE::FrameFromYawPitch(0.3f, -0.2f);
    LE::V3 target = {500.0f, 20.0f, -40.0f};
    LE::V3 eye = target + frame.back * d;
    LE::Lens lens = {tanHalf, tanHalf};
    bool inside = true;
    for (LE::V3 dir : {frame.right, frame.right * -1.0f, frame.up, frame.up * -1.0f}) {
        float nx, ny;
        inside &= LE::Project(eye, frame, lens, target + dir * 100.0f, 2.0f, &nx, &ny) && std::fabs(nx) < 1.0f &&
                  std::fabs(ny) < 1.0f;
    }
    Check(inside && d > 100.0f / std::sin(30.0f * kPi / 180.0f), "editor camera: F frames the whole selection");
    Check(Camera::FrameDistance(1.0f, tanHalf, 150.0f) == 150.0f && Camera::FrameDistance(100.0f, 0.0f, 10.0f) == 300.0f,
          "editor camera: framing's minimum and a missing lens");
    Check(Camera::SpeedForRadius(200.0f) == 800.0f && Camera::SpeedForRadius(1.0f) == 100.0f &&
              Camera::SpeedForRadius(1e6f) == 20000.0f,
          "editor camera: the speed from the selection's size");
    Check(Camera::FlyRamp(0.0f, 0) == 1.0f && Near(Camera::FlyRamp(0.0f, 500), 0.05f) &&
              Camera::FlyRamp(0.5f, 500) == 1.0f && Near(Camera::FlyRamp(0.25f, 500), 0.5f) &&
              Near(Camera::FlyRamp(0.125f, 500), 0.15625f),
          "editor camera: flying's ramp");
}
SELFTEST(Early, TestEditorCameraFrame);

void TestEditorCameraOrbit()
{
    LE::V3 pivot = {100.0f, 0.0f, 300.0f};
    LE::V3 pos = {-200.0f, 400.0f, -100.0f};
    float yaw = 0.4f, pitch = -0.3f;
    LE::Frame before = LE::FrameFromYawPitch(yaw, pitch);
    LE::Lens lens = LE::LensFor(72.0f, 1.777f);
    float nx0, ny0;
    bool seen0 = LE::Project(pos, before, lens, pivot, 2.0f, &nx0, &ny0);
    float yaw2 = yaw, pitch2 = pitch;
    LE::Look(&yaw2, &pitch2, 120.0f, -40.0f, 0.0035f);
    LE::Frame after = LE::FrameFromYawPitch(yaw2, pitch2);
    LE::V3 moved = Camera::OrbitAbout(pos, pivot, before, after);
    float nx1, ny1;
    bool seen1 = LE::Project(moved, after, lens, pivot, 2.0f, &nx1, &ny1);
    LE::V3 r0 = pos - pivot, r1 = moved - pivot;
    Check(seen0 && seen1 && Near(nx0, nx1) && Near(ny0, ny1), "editor camera: an orbit keeps the pivot where it is on the screen");
    Check(Near(std::sqrt(LE::Dot(r0, r0)), std::sqrt(LE::Dot(r1, r1)), 0.05f) && !NearV(pos, moved),
          "editor camera: an orbit keeps the distance and moves the camera");
    Check(NearV(Camera::OrbitAbout(pos, pivot, before, before), pos), "editor camera: no turn, no move");
}
SELFTEST(Early, TestEditorCameraOrbit);

void TestEditorCameraOrtho()
{
    // The picture's width at the pivot is the equivalent perspective view's.
    for (float equivalent : {200.0f, 1500.0f, 8000.0f}) {
        float eye = Camera::EyeDistance(equivalent);
        float angle = Camera::OrthoAngle(72.0f, equivalent);
        float width = std::tan(angle * kPi / 360.0f) * eye, wanted = std::tan(36.0f * kPi / 180.0f) * equivalent;
        Check(eye >= equivalent * 5.0f && angle < 72.0f && Near(width, wanted, wanted * 1e-4f),
              "editor camera: an ortho view's lens keeps the picture's size");
        Check(Camera::OrthoNear(equivalent) > 2.0f && Camera::OrthoNear(equivalent) < eye - equivalent,
              "editor camera: an ortho view's near plane lies before its pivot");
    }
    Check(Camera::EyeDistance(1.0f) == 15000.0f && Camera::EyeDistance(1e6f) == 60000.0f,
          "editor camera: the eye's distance limits (within the far plane, 96000)");
    struct Axis {
        Camera::View view;
        LE::V3 forward;
    } axes[] = {{Camera::View::Top, {0, -1, 0}},  {Camera::View::Bottom, {0, 1, 0}}, {Camera::View::Front, {0, 0, 1}},
                {Camera::View::Back, {0, 0, -1}}, {Camera::View::Right, {-1, 0, 0}}, {Camera::View::Left, {1, 0, 0}}};
    bool all = true;
    for (const Axis& axis : axes) {
        float yaw, pitch;
        Camera::AxisYawPitch(axis.view, &yaw, &pitch);
        LE::V3 forward = LE::FrameFromYawPitch(yaw, pitch).back * -1.0f;
        all &= LE::Dot(forward, axis.forward) > 0.999f;
    }
    Check(all, "editor camera: the numpad's views look along their axes");
    Check(std::strcmp(Camera::ViewName(Camera::View::Top), "Top") == 0 &&
              std::strcmp(Camera::ViewName(Camera::View::None), "Perspective") == 0,
          "editor camera: view names");
}
SELFTEST(Early, TestEditorCameraOrtho);

void TestEditorCameraBookmarks()
{
    Camera::Bookmark marks[Camera::kBookmarks];
    marks[0].set = true;
    marks[0].pose = {{1.2345678f, -2.25f, 1234567.0f}, 0.1f, -0.7f};
    marks[8].set = true;
    marks[8].pose = {{-0.000123f, 3.0e-7f, 99999.9f}, -3.1f, 1.2f};
    marks[8].view = Camera::View::Top;
    marks[8].equivalent = 1777.25f;
    std::string text = Camera::FormatBookmarks(marks);
    Camera::Bookmark back[Camera::kBookmarks];
    bool parsed = Camera::ParseBookmarks(text, back);
    bool same = parsed;
    for (int i = 0; i < Camera::kBookmarks; ++i) {
        same &= back[i].set == marks[i].set;
        if (marks[i].set)
            same &= std::memcmp(&back[i].pose, &marks[i].pose, sizeof(Camera::Pose)) == 0 && back[i].view == marks[i].view &&
                    back[i].equivalent == marks[i].equivalent;
    }
    Check(same, "editor camera: bookmarks round trip exactly (only the set ones)");
    Check(!Camera::ParseBookmarks("thug2-decomp layout 2\n1 0 0 0 0 0 0 0\n", back) && !back[0].set,
          "editor camera: a file that isn't a bookmarks file is refused");
    Check(Camera::ParseBookmarks("thug2-decomp camera bookmarks 1\n0 1 2 3 0 0 0 0\n10 1 2 3 0 0 0 0\n2 1 2\n"
                                 "3 1 2 3 0.5 0.25 99 0\n4 1 2 3 0.5 0.25 1 300 extra\n",
                                 back) &&
              !back[0].set && !back[1].set && !back[2].set && back[3].set && back[3].view == Camera::View::Top &&
              back[3].equivalent == 300.0f,
          "editor camera: malformed bookmark lines are skipped");
    Check(Camera::BookmarksFileName(0x0012ABCD) == "0012ABCD.cameras.txt", "editor camera: the bookmarks' file name");
}
SELFTEST(Early, TestEditorCameraBookmarks);

void TestEditorPlay()
{
    using Step = Play::Step;
    Check(Play::OnKey(true, true, false, true) == Step::PauseThenEnter, "editor play: the key in play opens the editor");
    Check(Play::OnKey(false, true, false, true) == Step::None && Play::OnKey(true, false, false, true) == Step::None &&
              Play::OnKey(true, true, true, true) == Step::None && Play::OnKey(true, true, false, false) == Step::None,
          "editor play: not without a press, outside a level, paused, or where the editor isn't offered");
    uint8_t keys[SDL_NUM_SCANCODES] = {};
    bool none = !Play::KeyDown(keys) && !Play::KeyDown(nullptr);
    keys[SDL_SCANCODE_F5] = 1;
    bool f5 = Play::KeyDown(keys);
    keys[SDL_SCANCODE_F5] = 0;
    keys[SDL_SCANCODE_P] = 1;
    bool pAlone = Play::KeyDown(keys);
    keys[SDL_SCANCODE_RCTRL] = 1;
    bool ctrlP = Play::KeyDown(keys);
    Check(none && f5 && !pAlone && ctrlP, "editor play: the key is F5 or Ctrl+P (P alone is the camera log)");
    Play::Resume resume;
    resume.pos = {1.0f, 2.0f, 3.0f};
    resume.objects = {7u};
    Play::Keep(resume, 5);
    Play::Resume got;
    bool otherLoad = Play::TakeResume(&got, 6);
    bool taken = Play::TakeResume(&got, 5);
    bool again = Play::TakeResume(&got, 5);
    Check(!otherLoad && taken && !again && NearV(got.pos, resume.pos) && got.objects.size() == 1 && got.objects[0] == 7u,
          "editor play: the camera comes back once, in the same level load only");
}
SELFTEST(Early, TestEditorPlay);

void TestEditorPrefs()
{
    Prefs::Values wild;
    wild.flySpeed = 1;
    wild.flyAccelMs = 99999;
    wild.lookPct = 0;
    wild.gizmoPct = 1000;
    wild.uiScalePct = 30;
    wild.autosaveSeconds = 1;
    wild.snapMove = 0;
    wild.snapTurn = 360;
    wild.accent = 42;
    Prefs::Values c = Prefs::Clamped(wild);
    Check(c.flySpeed == 50 && c.flyAccelMs == 3000 && c.lookPct == 10 && c.gizmoPct == 400 && c.uiScalePct == 75 &&
              c.autosaveSeconds == 10 && c.snapMove == 1 && c.snapTurn == 90 && c.accent == Prefs::kAccentCount - 1,
          "editor prefs: values brought within their limits");
    wild.uiScalePct = -5;
    Check(Prefs::Clamped(wild).uiScalePct == 0, "editor prefs: UI scale 0 or below is automatic");
    Prefs::Values d;
    Prefs::Values dc = Prefs::Clamped(d);
    Check(dc.flySpeed == d.flySpeed && dc.flyAccelMs == d.flyAccelMs && dc.lookPct == d.lookPct && dc.gizmoPct == d.gizmoPct &&
              dc.uiScalePct == d.uiScalePct && dc.autosaveSeconds == d.autosaveSeconds && dc.snapMove == d.snapMove &&
              dc.snapTurn == d.snapTurn && dc.accent == d.accent && d.flySpeed == 800 && d.snapMove == 25 && d.snapTurn == 15 &&
              d.autosaveSeconds == 60 && d.uiScalePct == 0 && !d.wasdFlies,
          "editor prefs: the defaults are the editor's behaviour until now");
    float rgb[3];
    Prefs::AccentRgb(0, rgb);
    bool orange = rgb[0] == 0.82f && rgb[1] == 0.34f && rgb[2] == 0.11f;
    Prefs::AccentRgb(99, rgb);
    Check(orange && rgb[0] >= 0.0f && std::strcmp(Prefs::AccentName(1), "Blue") == 0,
          "editor prefs: the accents (orange first, as before)");
    // Set (not saved) is what Get returns, a scale / accent change moves the style's generation.
    Prefs::Values saved = Prefs::Get();
    int generation = Prefs::StyleGeneration();
    Prefs::Values v = saved;
    v.snapMove = 40;
    Prefs::Set(v, false);
    bool kept = Prefs::Get().snapMove == 40 && Prefs::StyleGeneration() == generation;
    v.accent = (saved.accent + 1) % Prefs::kAccentCount;
    Prefs::Set(v, false);
    bool moved = Prefs::StyleGeneration() == generation + 1;
    Prefs::Set(saved, false);
    Check(kept && moved, "editor prefs: Set keeps the values; a new accent rebuilds the style");
}
SELFTEST(Early, TestEditorPrefs);

} // namespace
