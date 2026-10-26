// Self-tests for the level editor's feature map, last round (map4 - src/game/front/level_editor_entities.*,
// _triggers.*, _cameras.*, _level_props.*, not original): a particle system's emitter turned with its node (Mid / End
// about its place, the boxes along the world's axes), the Camera trigger (its compiled script, its record), a camera
// shot's frames, and the edited level's ambient light / fog put over a time-of-day state's parameters.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_cameras.h"
#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_level_props.h"
#include "game/front/level_editor_triggers.h"
#include "game/gfx/tod_manager.h"
#include "hook/selftest.h"

namespace {

namespace Entities = Front::LevelEditor::Entities;
namespace Cameras = Front::LevelEditor::Cameras;
namespace LevelProps = Front::LevelEditor::LevelProps;
namespace T = Front::LevelEditor::Triggers;
using Front::LevelEditor::V3;

bool Near(float a, float b, float within = 1e-3f) { return std::fabs(a - b) < within; }
bool Near(V3 a, V3 b, float within = 1e-3f) { return Near(a.x, b.x, within) && Near(a.y, b.y, within) && Near(a.z, b.z, within); }

// --- particles ---

void TestEditorParticleTurn()
{
    const float kQuarter = 1.57079637f, kEighth = 0.785398185f;
    // The boxes: a quarter turn swaps x and z exactly; none keeps them; an eighth holds the turned box.
    V3 box = {30.0f, 5.0f, 10.0f};
    V3 quarter = Entities::TurnBoxExtents(box, kQuarter), half = Entities::TurnBoxExtents(box, 2.0f * kQuarter);
    float r = 0.70710678f;
    Check(quarter.x == 10.0f && quarter.y == 5.0f && quarter.z == 30.0f && half.x == 30.0f && half.z == 10.0f &&
              Near(Entities::TurnBoxExtents(box, kEighth), {40.0f * r, 5.0f, 40.0f * r}) &&
              Near(Entities::TurnBoxExtents(box, -kQuarter), {10.0f, 5.0f, 30.0f}),
          "editor particles: the boxes turned (a quarter swaps x and z; between, the box holding the turned one)");
    // Mid / End turned about the node's place (as TurnAboutUp turns a heading) and moved with it.
    Entities::ParticleEmitter origin;
    origin.hasMid = true;
    origin.mid = {110.0f, 20.0f, 0.0f};
    origin.hasEnd = true;
    origin.end = {120.0f, 40.0f, 0.0f};
    origin.hasBox[0] = true;
    origin.box[0] = box;
    origin.hasBox[2] = true;
    origin.box[2] = {1.0f, 2.0f, 3.0f};
    V3 from = {100.0f, 0.0f, 0.0f};
    Entities::ParticleEmitter moved = Entities::TurnParticle(origin, from, {150.0f, 10.0f, 0.0f}, 0.0f);
    Entities::ParticleEmitter turned = Entities::TurnParticle(origin, from, {200.0f, 0.0f, 0.0f}, kQuarter);
    V3 sameWay = Front::LevelEditor::TurnAboutUp({10.0f, 20.0f, 0.0f}, kQuarter);
    bool shift = Near(moved.mid, {160.0f, 30.0f, 0.0f}) && Near(moved.end, {170.0f, 50.0f, 0.0f}) &&
                 moved.box[0].x == 30.0f && moved.box[0].z == 10.0f;
    bool turn = Near(turned.mid, {200.0f, 20.0f, -10.0f}) && Near(turned.end, {200.0f, 40.0f, -20.0f}) &&
                Near(turned.mid, V3{200.0f, 0.0f, 0.0f} + sameWay) && turned.box[0].x == 10.0f &&
                turned.box[0].z == 30.0f && turned.box[2].x == 3.0f && turned.box[2].z == 1.0f && !turned.hasBox[1];
    // From the origin each time: 24 notches of 15 degrees come back to where it began (no drift).
    Entities::ParticleEmitter round = Entities::TurnParticle(origin, from, from, 24.0f * 0.261799395f);
    bool noDrift = Near(round.mid, origin.mid) && Near(round.end, origin.end) && Near(round.box[0], box);
    // A node without Mid / End / boxes: nothing made up.
    Entities::ParticleEmitter none = Entities::TurnParticle(Entities::ParticleEmitter{}, from, from, kQuarter);
    bool nothing = !none.hasMid && !none.hasEnd && !none.hasBox[0] && !none.hasBox[1] && !none.hasBox[2];
    Check(shift && turn && noDrift && nothing,
          "editor particles: Mid / End turned about the node and moved with it, from the origin (no drift)");
}
SELFTEST(Early, TestEditorParticleTurn);

// --- camera triggers and shots ---

std::vector<uint8_t> Hex(const char* text)
{
    std::vector<uint8_t> out;
    int high = -1;
    for (const char* p = text; *p; ++p) {
        char c = *p;
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            continue;
        if (high < 0) {
            high = d;
        } else {
            out.push_back(static_cast<uint8_t>(high << 4 | d));
            high = -1;
        }
    }
    return out;
}

void TestEditorCameraTrigger()
{
    T::Spec s;
    s.name = "ed_trigger05";
    s.kind = T::Kind::Camera;
    s.target = "ed_camera01";
    s.seconds = 2.5f;
    // LevelEditor_PlayCamera camera=ed_camera01 time=2.5 (tools/pipeline/game/map_features.test.ts has the same bytes)
    bool body = T::Body(s) == Hex("16 c7654bff 16 fa11e3c4 07 16 bda5ba50 16 ba676b90 07 1a 00002040 01");
    T::Spec none = s;
    none.target.clear();
    bool problem = T::Problem(none) && T::Body(none).empty() && !T::Problem(s);
    T::Spec clamped = s;
    clamped.seconds = 600.0f;
    bool clamp = T::Body(clamped) == Hex("16 c7654bff 16 fa11e3c4 07 16 bda5ba50 16 ba676b90 07 1a 00007042 01") &&
                 T::ClampSeconds(std::numeric_limits<float>::quiet_NaN()) == 3.0f && T::ClampSeconds(0.1f) == 0.5f;
    Check(body && problem && clamp, "editor camera triggers: LevelEditor_PlayCamera camera=<node> time=<seconds>");
    // The record: kind word, seconds kept, read back the same; out of range or twice refused.
    T::Placed placed;
    placed.spec = s;
    placed.pos = {1.0f, 2.0f, 3.0f};
    std::string record = T::Record(placed);
    T::Placed back;
    bool read = T::ReadRecord(record, &back) && T::SameSpec(back.spec, s) && back.spec.kind == T::Kind::Camera;
    T::Placed bad;
    bool refused = !T::ReadRecord("ed_trigger05 camera pos=0,0,0 seconds=0.1", &bad) &&
                   !T::ReadRecord("ed_trigger05 camera pos=0,0,0 seconds=61", &bad) &&
                   !T::ReadRecord("ed_trigger05 camera pos=0,0,0 seconds=2 seconds=3", &bad) &&
                   T::ReadRecord("ed_trigger05 camera pos=0,0,0 target=ed_camera01", &bad) && bad.spec.seconds == 3.0f;
    T::Kind kind;
    bool word = T::KindFromWord("camera", &kind) && kind == T::Kind::Camera &&
                std::string(T::KindWord(T::Kind::Camera)) == "camera" && T::kKinds == 9;
    T::Calls calls;
    calls.camera = true;
    bool ofCalls = T::KindOfCalls(calls, true) == T::Kind::Camera;
    Check(read && refused && word && ofCalls && record.find(" seconds=2.5") != std::string::npos,
          "editor camera triggers: the record (seconds=) read back; a script calling it is a Camera trigger");
    // A shot's frames: 60 a second, kept within 0.5 .. 60 seconds.
    Check(Cameras::ShotFrames(2.5f) == 150 && Cameras::ShotFrames(0.1f) == 30 && Cameras::ShotFrames(100.0f) == 3600 &&
              Cameras::ShotFrames(std::numeric_limits<float>::quiet_NaN()) == 180 && Cameras::ShotFrames(1.0f / 3.0f) == 30,
          "editor cameras: a shot's frames");
}
SELFTEST(Early, TestEditorCameraTrigger);

// --- the level's ambient light and fog ---

// The ambient light and fog put over a time-of-day state's parameters: the edited copy's first, then the level
// structure's ed_ keys; what neither sets left as the state has it; fog forced only when one gives a fog.
void TestEditorLightingOverride()
{
    Gfx::TODParams state{};
    state.ambientRed = 1;
    state.ambientGreen = 2;
    state.ambientBlue = 3;
    state.fogRed = 4;
    state.fogGreen = 5;
    state.fogBlue = 6;
    state.fogAlpha = 7;
    state.fogDist = 8;
    state.levRed = 99;
    LevelProps::Props none, edited, level;
    Gfx::TODParams p = state;
    bool forced = LevelProps::Override(none, none, &p);
    bool untouched = !forced && std::memcmp(&p, &state, sizeof(p)) == 0;
    level.hasFog = true;
    int levelFog[5] = {10, 20, 30, 40, 5000};
    std::memcpy(level.fog, levelFog, sizeof(levelFog));
    edited.hasAmbient = true;
    edited.ambient[0] = 300;
    edited.ambient[1] = 50;
    edited.ambient[2] = -5;
    p = state;
    forced = LevelProps::Override(edited, level, &p);
    bool mixed = forced && p.ambientRed == 255 && p.ambientGreen == 50 && p.ambientBlue == 0 && p.fogRed == 10 &&
                 p.fogGreen == 20 && p.fogBlue == 30 && p.fogAlpha == 40 && p.fogDist == 5000 && p.levRed == 99;
    edited.hasFog = true;
    int editedFog[5] = {1, 2, 3, 4, 200000};
    std::memcpy(edited.fog, editedFog, sizeof(editedFog));
    level.hasAmbient = true;
    level.ambient[0] = 7;
    p = state;
    forced = LevelProps::Override(edited, level, &p);
    bool editedWins = forced && p.ambientRed == 255 && p.fogRed == 1 && p.fogDist == 100000;
    p = state;
    forced = LevelProps::Override(none, level, &p);
    bool levelOnly = forced && p.ambientRed == 7 && p.ambientGreen == 0 && p.fogRed == 10;
    Check(untouched && mixed && editedWins && levelOnly,
          "editor lighting: the ambient light / fog over the time-of-day state (edited, then the level's ed_ keys)");
}
SELFTEST(Early, TestEditorLightingOverride);

} // namespace
