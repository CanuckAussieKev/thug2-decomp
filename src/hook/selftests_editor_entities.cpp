// Self-tests for the level editor's entities (not original - src/game/front/level_editor_entities.*): bouncy sets'
// stems and anchors, paths sampled for the preview, the palette's type keys, the speed / emitter state and its layout
// records; and the team lobby flag's node (level_editor_node_create.h).

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_node_create.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace E = Front::LevelEditor::Entities;
namespace NC = Front::LevelEditor::NodeCreate;
using LE::V3;

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

void TestEditorEntitySets()
{
    Check(E::SetStem("BO_Bouncy_Ball01") == "BO_Bouncy_Ball" && E::SetStem("Bouncy_StatueHead03") == "Bouncy_StatueHead" &&
              E::SetStem("JG_YMCA_TrashCan_12") == "JG_YMCA_TrashCan" && E::SetStem("Cone") == "Cone" &&
              E::SetStem("0042") == "0042" && E::SetStem("a1b2") == "a1b",
          "editor entities: a bouncy set's stem (trailing digits, then '_')");
    V3 anchor = E::SetAnchor({{10, 5, -4}, {-6, 2, 8}, {0, 9, 0}});
    Check(Near(anchor, {2, 2, 2}, 1e-5f), "editor entities: a set's anchor (the middle across, the lowest y)");
    Check(Near(E::SetAnchor({{3, 4, 5}}), {3, 4, 5}, 0.0f) && Near(E::SetAnchor({}), {0, 0, 0}, 0.0f),
          "editor entities: one member's anchor is its place; none's the origin");
    // Placed about a point: each member keeps its offset from the anchor.
    std::vector<V3> places = {{100, 10, 0}, {140, 12, 30}};
    V3 a = E::SetAnchor(places), at = {0, 50, 0};
    V3 first = at + (places[0] - a), second = at + (places[1] - a);
    Check(Near(second - first, places[1] - places[0], 1e-4f) && Near(first.y, 50.0f, 1e-5f),
          "editor entities: a placed set keeps its members' spacing, its lowest on the ground");
}
SELFTEST(Early, TestEditorEntitySets);

void TestEditorEntityPaths()
{
    std::vector<V3> square = {{0, 0, 0}, {100, 0, 0}, {100, 0, 100}, {0, 0, 100}};
    Check(Near(E::PathLength(square, false), 300.0f, 1e-3f) && Near(E::PathLength(square, true), 400.0f, 1e-3f),
          "editor entities: a path's length (open, closed)");
    V3 pos, dir;
    bool ok = E::PathSample(square, false, 150.0f, &pos, &dir);
    Check(ok && Near(pos, {100, 0, 50}, 1e-3f) && Near(dir, {0, 0, 1}, 1e-5f),
          "editor entities: halfway along the second segment, heading along it");
    ok = E::PathSample(square, false, 350.0f, &pos, &dir); // (past the end of an open path: from the start again)
    Check(ok && Near(pos, {50, 0, 0}, 1e-3f) && Near(dir, {1, 0, 0}, 1e-5f),
          "editor entities: an open path wraps to its start");
    ok = E::PathSample(square, true, 350.0f, &pos, &dir); // (closed: along the closing segment)
    Check(ok && Near(pos, {0, 0, 50}, 1e-3f) && Near(dir, {0, 0, -1}, 1e-5f),
          "editor entities: a closed path's last segment goes back to its first point");
    ok = E::PathSample(square, false, 0.0f, &pos, &dir);
    Check(ok && Near(pos, {0, 0, 0}, 0.0f), "editor entities: distance 0 is the first point");
    ok = E::PathSample(square, false, -50.0f, &pos, &dir);
    Check(ok && Near(pos, {50, 0, 100}, 1e-3f), "editor entities: a negative distance counts back from the end");
    // Repeated points (no length) are passed over.
    std::vector<V3> repeated = {{0, 0, 0}, {0, 0, 0}, {0, 10, 0}};
    ok = E::PathSample(repeated, false, 4.0f, &pos, &dir);
    Check(ok && Near(pos, {0, 4, 0}, 1e-4f) && Near(dir, {0, 1, 0}, 1e-5f),
          "editor entities: a zero-length segment is passed over");
    ok = E::PathSample(repeated, false, 0.0f, &pos, &dir); // (at its start: the first segment with a length's heading)
    Check(ok && Near(pos, {0, 0, 0}, 1e-5f) && Near(dir, {0, 1, 0}, 1e-5f),
          "editor entities: a zero-length segment at the start isn't sampled (no direction)");
    Check(!E::PathSample({{1, 2, 3}}, false, 1.0f, &pos, &dir) && !E::PathSample({{1, 2, 3}, {1, 2, 3}}, true, 1.0f, &pos, &dir),
          "editor entities: one point, or no length: no sample");
    Check(Near(E::MphToUnits(10.0f), 176.0f, 1e-3f), "editor entities: mph in inches a second");
}
SELFTEST(Early, TestEditorEntityPaths);

void TestEditorEntityTypeKeys()
{
    std::string a = E::TypeKey(1, 2, 3, "veh\\veh_duck\\veh_duck.mdl");
    Check(a == E::TypeKey(1, 2, 3, "VEH/veh_DUCK/veh_duck.mdl") && a != E::TypeKey(1, 2, 4, "veh\\veh_duck\\veh_duck.mdl") &&
              a != E::TypeKey(1, 9, 3, "veh\\veh_duck\\veh_duck.mdl") && a != E::TypeKey(5, 2, 3, "veh\\veh_duck\\veh_duck.mdl") &&
              a != E::TypeKey(1, 2, 3, "veh\\veh_half_pipe\\veh_half_pipe.mdl"),
          "editor entities: one palette entry per class, type, profile and model (case and slashes ignored)");
}
SELFTEST(Early, TestEditorEntityTypeKeys);

void TestEditorEntityState()
{
    E::State s;
    E::SetSpeed(&s, 0x10, 30.0f);
    E::SetSpeed(&s, 0x20, 500.0f); // (clamped)
    E::SetSpeed(&s, 0x10, 12.5f);  // (replaced)
    Check(s.speeds.size() == 2 && E::SpeedOf(s, 0x10)->mph == 12.5f && E::SpeedOf(s, 0x20)->mph == E::kMaxMph,
          "editor entities: speeds set, replaced, clamped");
    E::SetSpeed(&s, 0x20, 0.0f);
    Check(s.speeds.size() == 1 && !E::SpeedOf(s, 0x20), "editor entities: speed 0 takes it out");
    E::EmitterSound e;
    e.name = 0x30;
    e.sound = 0xABCDEF01;
    e.volume = 900.0f;
    e.radius = 0.5f;
    E::SetEmitter(&s, e);
    const E::EmitterSound* got = E::EmitterOf(s, 0x30);
    Check(got && got->volume == E::kMaxVolume && got->radius == E::kMinRadius && got->sound == 0xABCDEF01,
          "editor entities: an emitter's volume and radius clamped");
    e.sound = 0;
    e.radius = 0.0f;
    E::SetEmitter(&s, e);
    Check(!E::EmitterOf(s, 0x30), "editor entities: an emitter with no sound and no radius is taken out");
    E::State t = s;
    Check(E::SameState(s, t) && !E::Empty(s) && E::Empty(E::State{}), "editor entities: SameState / Empty");
    E::SetSpeed(&t, 0x10, 13.0f);
    Check(!E::SameState(s, t), "editor entities: SameState sees a speed changed");
}
SELFTEST(Early, TestEditorEntityState);

bool ReadAll(const std::string& text, E::State* state)
{
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty())
            continue;
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (!E::IsRecord(kind) || !E::ReadRecord(kind, in, state))
            return false;
    }
    return true;
}

void TestEditorEntityRecords()
{
    E::State s;
    E::SetSpeed(&s, 0x0000ABCD, 27.125f);
    E::EmitterSound e;
    e.name = 0x1234ABCD;
    e.sound = 0xDEADBEEF;
    e.volume = 75.5f;
    e.radius = 320.25f;
    E::SetEmitter(&s, e);
    e.name = 0x00000077;
    e.sound = 0x00000001;
    e.volume = 100.0f;
    e.radius = 0.0f;
    E::SetEmitter(&s, e);
    std::string text = E::WriteRecords(s);
    E::State back;
    Check(ReadAll(text, &back) && E::SameState(s, back), "editor entities: records round trip exactly");
    Check(text.find("speed 0000ABCD 27.125\n") != std::string::npos &&
              text.find("emitter 1234ABCD sound=DEADBEEF vol=75.5 radius=320.25\n") != std::string::npos,
          "editor entities: the records' form");
    E::State x;
    Check(!ReadAll("speed 0000ABCD 0\n", &x) && !ReadAll("speed 0000ABCD 201\n", &x) && !ReadAll("speed 00ABCD 5\n", &x),
          "editor entities: a speed out of range or a bad name refuses the record");
    x = {};
    Check(!ReadAll("speed 0000ABCD 5\nspeed 0000ABCD 6\n", &x), "editor entities: a node's speed twice refuses it");
    x = {};
    Check(!ReadAll("emitter 00000001 sound=1 vol=5\n", &x) && !ReadAll("emitter 00000001 vol=500\n", &x) &&
              !ReadAll("emitter 00000001 vol=5 vol=6\n", &x) && !ReadAll("emitter 00000001 radius=0.5\n", &x) &&
              !ReadAll("emitter 00000001 novalue\n", &x),
          "editor entities: a malformed emitter record is refused");
    x = {};
    Check(ReadAll("emitter 00000001 sound=00000002 later=yes # a comment\n", &x) && x.emitters.size() == 1 &&
              x.emitters[0].volume == 100.0f && x.emitters[0].radius == 0.0f,
          "editor entities: unknown keys and a comment skipped; defaults kept");
    Check(E::IsRecord("speed") && E::IsRecord("emitter") && !E::IsRecord("point") && !E::IsRecord("set"),
          "editor entities: its record kinds");
}
SELFTEST(Early, TestEditorEntityRecords);

void TestEditorLobbyFlag()
{
    Check(NC::LobbyFlagName(NC::Team::Green) == "TRG_Flag_Green" && NC::IsLobbyFlag("TRG_Flag_Red") &&
              NC::IsLobbyFlag("trg_flag_blue") && !NC::IsLobbyFlag("TRG_CTF_Red"),
          "editor lobby flag: the name the game's Create_Team_Flags looks for");
    NC::Spec flag = NC::Default(NC::Kind::FlagSet, NC::Team::Yellow, {1, 2, 3}, 0.5f, NC::LobbyFlagName(NC::Team::Yellow));
    NC::Kind kind;
    Check(NC::KindOf(flag, &kind) && kind == NC::Kind::FlagSet && flag.cls == "GameObject" && flag.type == "Flag_Yellow",
          "editor lobby flag: a GameObject Flag_<Colour> (the flag set's kind)");
    std::vector<NC::Field> fields = NC::Fields(flag);
    const NC::Field* script = nullptr;
    const NC::Field* model = nullptr;
    for (const NC::Field& f : fields) {
        if (f.key == "TriggerScript")
            script = &f;
        if (f.key == "Model")
            model = &f;
    }
    Check(script && script->text == "TRG_Flag_YellowScript" && model &&
              model->text == "gameobjects\\flags\\flag_yellow\\flag_yellow.mdl",
          "editor lobby flag: its level's TRG_Flag_<Colour>Script and the flag's model");
    NC::Spec ctf = NC::Default(NC::Kind::FlagSet, NC::Team::Yellow, {1, 2, 3}, 0.5f, "TRG_CTF_Yellow");
    bool ctfScript = false;
    for (const NC::Field& f : NC::Fields(ctf))
        ctfScript = ctfScript || (f.key == "TriggerScript" && f.text == "TRG_CTF_YellowScript");
    Check(ctfScript, "editor lobby flag: a CTF flag keeps TRG_CTF_<Colour>Script");
}
SELFTEST(Early, TestEditorLobbyFlag);

} // namespace
