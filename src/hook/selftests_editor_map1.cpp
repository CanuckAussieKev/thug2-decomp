// Self-tests for the level editor's feature map rows (src/game/front/level_editor_chains.* and
// level_editor_triggers.*, not original): rail speed-ups (records, the scripts - byte for byte as NeverScript compiles
// them), ledges (records), jump boosts and breakables (trigger kinds, their scripts as NeverScript compiles THUG2's own
// trigger_jump_boost / Boston's BO_ShatteringGlass call forms), proximity nodes' enter / exit scripts (`if Inside ...
// else ...` byte for byte) and their records.

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_triggers.h"
#include "game/script/qb.h"
#include "hook/selftest.h"

namespace {

namespace C = Front::LevelEditor::Chains;
namespace T = Front::LevelEditor::Triggers;
using K = Front::LevelEditor::Triggers::Kind;

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

// A .qb image walked token by token: whether it ends exactly at its final 00, and how many scripts / names it has.
bool Walk(const std::vector<uint8_t>& file, int* scripts, int* names)
{
    *scripts = *names = 0;
    const uint8_t* p = file.data();
    const uint8_t* stop = file.data() + file.size();
    for (int guard = 0; p < stop && guard < 5000; ++guard) {
        if (*p == 0x00)
            return p + 1 == stop;
        if (*p == 0x23)
            ++*scripts;
        if (*p == 0x2B)
            ++*names;
        p = Script::SkipToken(p);
    }
    return false;
}

bool ReadChainLine(const std::string& line, C::State* state)
{
    std::istringstream in(line);
    std::string kind;
    in >> kind;
    return C::IsRecord(kind) && C::ReadRecord(kind, in, state);
}

void TestEditorMapRailSpeed()
{
    // The fixed scripts: NeverScript's image walks to its end - two scripts, their names after them.
    int scripts = 0, names = 0;
    Check(Walk(C::RailSpeedQb(), &scripts, &names) && scripts == 2 && names >= 10,
          "editor map: the rail speed-up scripts' image walks to its end (2 scripts)");
    std::string text(C::RailSpeedQb().begin(), C::RailSpeedQb().end());
    Check(text.find(std::string("ed_RailSpeedLoop") + '\0') != std::string::npos &&
              text.find(std::string("TriggerType") + '\0') != std::string::npos,
          "editor map: ... with their names");
    uint32_t speedName = Crc::GenerateCRCFromString("ed_RailSpeed");
    const std::vector<uint8_t>& qb = C::RailSpeedQb();
    Check(qb[0] == 0x23 && qb[1] == 0x16 && qb[2] == static_cast<uint8_t>(speedName) &&
              qb[5] == static_cast<uint8_t>(speedName >> 24),
          "editor map: the first script is ed_RailSpeed");
    // A point's script: the node's own called first (its arguments passed on), then ed_RailSpeed rate=.
    Check(C::SpeedBody(Crc::GenerateCRCFromString("ed_orig"), 0.05f) ==
              Hex("16 eb29e2c1 2c 01 16 7d9ec1f6 16 c6c01320 07 1a cdcc4c3d 01"),
          "editor map: a speed-up's body - as NeverScript compiles `ed_orig <...>` / `ed_RailSpeed rate=0.05`");
    Check(C::SpeedBody(0, 0.05f) == Hex("16 7d9ec1f6 16 c6c01320 07 1a cdcc4c3d 01"),
          "editor map: a speed-up's body without a script of its own");
    Check(C::SpeedScriptName(0x00ABCDEFu) == "ed_railspeed_00ABCDEF", "editor map: a speed-up script's name");
    std::vector<uint8_t> file = C::SpeedQbFile({{1u, 0u, 0.05f}, {2u, 7u, 0.1f}});
    Check(Walk(file, &scripts, &names) && scripts == 2 && names == 2, "editor map: speed-up scripts' image walks");
    // Rates kept within limits.
    Check(C::ClampSpeed(0.0f) == C::kSpeedMin && C::ClampSpeed(9.0f) == C::kSpeedMax && C::ClampSpeed(0.05f) == 0.05f &&
              C::ClampSpeed(std::nanf("")) == C::kSpeedMin,
          "editor map: speed-up rates clamped (NaN to the least)");
    // Records: point speed= and type= / hang=, the speed record; round trip.
    C::State state;
    C::Point rail;
    rail.name = "ed_rail01";
    rail.cls = "RailNode";
    rail.pos = {1.0f, 2.0f, 3.0f};
    rail.speed = 0.0625f; // (rates a float holds exactly: written as typed)
    C::Point ledge;
    ledge.name = "ed_ledge01";
    ledge.cls = "ClimbingNode";
    ledge.type = "Ledge";
    ledge.hang = C::kHangRight;
    ledge.pos = {4.0f, 5.0f, 6.0f};
    state.points = {rail, ledge};
    state.speeds = {{0x00001234u, 0.25f}, {0x00ABCDEFu, 0.03125f}};
    std::string written = C::WriteRecords(state);
    Check(written.find(" speed=0.0625") != std::string::npos && written.find(" type=Ledge hang=right") != std::string::npos &&
              written.find("speed 00001234 0.25") != std::string::npos,
          "editor map: speed= / type=Ledge / hang= / speed records written");
    C::State back;
    std::istringstream lines(written);
    std::string line;
    bool all = true;
    while (std::getline(lines, line))
        all = all && ReadChainLine(line, &back);
    Check(all && C::SameState(state, back), "editor map: the speed-up / ledge records read back the same");
    Check(C::IsLedge(back.points[1]) && !C::IsLedge(back.points[0]) && back.points[1].hang == C::kHangRight,
          "editor map: a ledge read back is one, its side kept");
    // Refused: a rate out of range, speed= on a ladder, type= on a rail or other than Ledge, an unknown side, the same
    // node's speed twice.
    C::State refused;
    Check(!ReadChainLine("point ed_rail02 RailNode pos=0,0,0 speed=0.9", &refused) &&
              !ReadChainLine("point ed_rail02 RailNode pos=0,0,0 speed=0", &refused) &&
              !ReadChainLine("point ed_ladder02 ClimbingNode pos=0,0,0 speed=0.05", &refused) &&
              !ReadChainLine("point ed_rail02 RailNode pos=0,0,0 type=Ledge", &refused) &&
              !ReadChainLine("point ed_ledge02 ClimbingNode pos=0,0,0 type=Ladder", &refused) &&
              !ReadChainLine("point ed_ledge02 ClimbingNode pos=0,0,0 type=Ledge hang=up", &refused) &&
              !ReadChainLine("speed 00001234 0.001", &refused) && !ReadChainLine("speed 00001234", &refused) &&
              !ReadChainLine("speed 1234 0.05", &refused),
          "editor map: malformed speed-up / ledge records refused");
    C::State twice;
    Check(ReadChainLine("speed 00001234 0.05", &twice) && !ReadChainLine("speed 00001234 0.06", &twice),
          "editor map: a node's speed record twice refused");
    // States differ by a speed-up, a ledge's side, a point's speed.
    C::State other = state;
    other.speeds[0].rate = 0.2f;
    Check(!C::SameState(state, other), "editor map: states differ by a level point's rate");
    other = state;
    other.points[0].speed = 0.0f;
    Check(!C::SameState(state, other), "editor map: states differ by a point's speed-up");
    other = state;
    other.points[1].hang = C::kHangLeft;
    Check(!C::SameState(state, other), "editor map: states differ by a ledge's side");
    other = state;
    other.points[1].type.clear();
    Check(!C::SameState(state, other), "editor map: states differ by ladder / ledge");
    Check(!C::Empty(C::State{{}, {}, {}, {}, {}, {}, {{1u, 0.05f}}}), "editor map: speed records alone aren't empty");
    Check(std::string(C::HangWord(C::kHangLeft)) == "left" && std::string(C::HangWord(C::kHangRight)) == "right" &&
              std::string(C::HangWord(0)).empty(),
          "editor map: the hang sides' words");
}
SELFTEST(Early, TestEditorMapRailSpeed);

void TestEditorMapTriggers()
{
    // Kinds: the older words keep their places; boost / break read back.
    K kind;
    Check(T::KindFromWord("boost", &kind) && kind == K::Boost && T::KindFromWord("break", &kind) && kind == K::Break &&
              T::KindFromWord("gapend", &kind) && kind == K::GapEnd,
          "editor map: boost / break words");
    T::Calls calls;
    calls.boost = true;
    Check(T::KindOfCalls(calls, true) == K::Boost, "editor map: trigger_jump_boost - a jump boost");
    calls.shatter = true;
    Check(T::KindOfCalls(calls, true) == K::Boost, "editor map: a boost wins over a shatter");
    calls.boost = false;
    Check(T::KindOfCalls(calls, true) == K::Break, "editor map: Shatter - a breakable");
    calls.kill = true;
    Check(T::KindOfCalls(calls, true) == K::Kill, "editor map: a kill wins over a breakable");
    // A boost's script: trigger_jump_boost speed=900, as NeverScript compiles it; clamped.
    T::Spec boost;
    boost.name = "ed_trigger01";
    boost.kind = K::Boost;
    boost.strength = 900;
    Check(!T::Problem(boost) && T::Body(boost) == Hex("16 f083bbb8 16 0901d9f0 07 17 84030000 01"),
          "editor map: a jump boost's body - `trigger_jump_boost speed=900`");
    boost.strength = 99999;
    std::vector<uint8_t> clamped = T::Body(boost);
    Check(clamped.size() == 17 && clamped[12] == static_cast<uint8_t>(T::kBoostMax) &&
              clamped[13] == static_cast<uint8_t>(T::kBoostMax >> 8),
          "editor map: a boost's strength clamped");
    // A breakable's script: PlaySound, Shatter (BO_ShatteringGlass's values), Kill - as NeverScript compiles
    //   PlaySound HitGlassPane2x
    //   Shatter name=ed_sector area=5000 variance=1.0 spread=1.5 life=3 bounce=0 scale=0.4 vel_x=0 vel_y=60 vel_z=0
    //   Kill name=ed_sector
    T::Spec glass;
    glass.name = "ed_trigger02";
    glass.kind = K::Break;
    Check(T::Problem(glass) != nullptr && T::Body(glass).empty(), "editor map: a breakable without its piece does nothing");
    glass.target = "ed_sector";
    glass.sound = "HitGlassPane2x";
    std::vector<uint8_t> shatter = Hex("16 4841c2ee 16 f981dca1 07 16 ee605f74 16 97c26b28 07 17 88130000 16 d99d30bd 07 1a "
                                       "0000803f 16 875f4a83 07 1a 0000c03f 16 01c132e6 07 17 03000000 16 a60bc37d 07 17 "
                                       "00000000 16 7bdab913 07 1a cdcccc3e 16 6d908761 07 17 00000000 16 fba08016 07 17 "
                                       "3c000000 16 41f1898f 07 17 00000000 01 16 96a9d6f8 16 f981dca1 07 16 ee605f74 01");
    std::vector<uint8_t> withSound = Hex("16 b1b25151 16 c5a4a2b0 01");
    withSound.insert(withSound.end(), shatter.begin(), shatter.end());
    Check(T::Body(glass) == withSound, "editor map: a breakable's body - BO_ShatteringGlass's Shatter and Kill");
    glass.sound.clear();
    Check(T::Body(glass) == shatter, "editor map: ... without a sound");
    // Records: strength= / sound= round trip; refused out of range, bad, twice.
    T::Placed placed;
    placed.spec = boost;
    placed.spec.strength = 1200;
    placed.pos = {1.0f, 2.0f, 3.0f};
    T::Placed back;
    std::string record = T::Record(placed);
    Check(record.find(" strength=1200") != std::string::npos && T::ReadRecord(record, &back) &&
              T::SameSpec(back.spec, placed.spec),
          "editor map: a boost's record round trip");
    placed.spec = glass;
    placed.spec.sound = "HitMetalCan";
    placed.spec.replaces = 0x1234ABCDu;
    record = T::Record(placed);
    Check(record.find(" sound=HitMetalCan") != std::string::npos && T::ReadRecord(record, &back) &&
              T::SameSpec(back.spec, placed.spec) && back.spec.target == "ed_sector",
          "editor map: a breakable's record round trip");
    Check(!T::ReadRecord("ed_trigger03 boost pos=0,0,0 strength=50", &back) &&
              !T::ReadRecord("ed_trigger03 boost pos=0,0,0 strength=600 strength=700", &back) &&
              !T::ReadRecord("ed_trigger03 boost pos=0,0,0 strength=6x", &back) &&
              !T::ReadRecord("ed_trigger03 break pos=0,0,0 sound=a-b", &back) &&
              T::ReadRecord("ed_trigger03 boost pos=0,0,0", &back) && back.spec.strength == 900,
          "editor map: malformed boost / breakable records refused (no strength: 900)");
    Check(!T::BreakSounds().empty() && std::string(T::BreakSounds().front()) == "HitGlassPane2x",
          "editor map: the breakables' sounds start with the glass");
}
SELFTEST(Early, TestEditorMapTriggers);

void TestEditorMapProxim()
{
    uint32_t enter = Crc::GenerateCRCFromString("ed_enter"), exit = Crc::GenerateCRCFromString("ed_exit"),
             own = Crc::GenerateCRCFromString("ed_orig");
    // As NeverScript compiles `if Inside { ed_enter } else { ed_exit }`, and with `ed_orig <...>` for the exit.
    Check(T::ProximBody(enter, exit, own) ==
              Hex("47 1100 16 4b0d0145 01 16 ee6ba5ff 01 48 0a00 01 16 870d7627 01 28 01"),
          "editor map: a proximity node's script - if Inside {enter} else {exit}");
    Check(T::ProximBody(enter, 0, own) ==
              Hex("47 1100 16 4b0d0145 01 16 ee6ba5ff 01 48 0b00 01 16 eb29e2c1 2c 01 28 01"),
          "editor map: ... its own script for the branch not chosen (its arguments passed on)");
    // No own script: an empty branch - the if still jumps to the else's body, the else to the end.
    std::vector<uint8_t> enterOnly = T::ProximBody(enter, 0, 0);
    Check(enterOnly == Hex("47 1100 16 4b0d0145 01 16 ee6ba5ff 01 48 0400 01 28 01"),
          "editor map: an empty else");
    std::vector<uint8_t> exitOnly = T::ProximBody(0, exit, own);
    Check(exitOnly == Hex("47 1200 16 4b0d0145 01 16 eb29e2c1 2c 01 48 0a00 01 16 870d7627 01 28 01"),
          "editor map: its own script for entering, the exit chosen");
    Check(T::ProximBody(0, 0, own).empty(), "editor map: nothing chosen - no script");
    int scripts = 0, names = 0;
    std::vector<uint8_t> file = T::ProximQbFile({{1u, enter, exit, 0u}, {2u, enter, 0u, own}});
    Check(Walk(file, &scripts, &names) && scripts == 2 && names == 2, "editor map: proximity scripts' image walks");
    Check(T::ProximScriptName(0x0000BEEFu) == "ed_proxim_0000BEEF", "editor map: a proximity script's name");
    // Records.
    T::ProximEdit e;
    e.node = 0x12345678u;
    e.enter = "BO_Library_On";
    e.exit = "BO_Library_Off";
    e.shape = true;
    e.box = true;
    e.size = {400.0f, 200.0f, 300.0f};
    e.offset = {0.0f, 100.0f, -5.5f};
    T::ProximEdit back;
    std::string record = T::ProximRecord(e, "TRG_Proxim_In_Library");
    Check(record.find("12345678 enter=BO_Library_On exit=BO_Library_Off shape=box size=400,200,300 offset=0,100,-5.5") == 0 &&
              T::ReadProximRecord(record, &back) && T::SameProxim(e, back),
          "editor map: a proximity record round trip (its comment skipped)");
    T::ProximEdit sphere;
    sphere.node = 0xABCDEF01u;
    sphere.shape = true;
    sphere.radius = 350.0f;
    Check(T::ReadProximRecord(T::ProximRecord(sphere, nullptr), &back) && T::SameProxim(sphere, back) &&
              T::ProximEdited(back),
          "editor map: a sphere's record round trip");
    Check(!T::ProximEdited(T::ProximEdit{0x1u}), "editor map: an edit of nothing isn't one");
    T::ProximEdit moved = e;
    moved.offset.z = 7.0f;
    Check(!T::SameProxim(e, moved), "editor map: proximity edits differ by a box's offset");
    Check(!T::ReadProximRecord("1234 enter=a", &back) && !T::ReadProximRecord("12345678 shape=box", &back) &&
              !T::ReadProximRecord("12345678 shape=sphere", &back) &&
              !T::ReadProximRecord("12345678 shape=cone radius=5", &back) &&
              !T::ReadProximRecord("12345678 shape=sphere radius=5", &back) &&
              !T::ReadProximRecord("12345678 enter=a enter=b", &back) &&
              !T::ReadProximRecord("12345678 enter=a-b", &back) &&
              !T::ReadProximRecord("12345678 shape=box size=1,2", &back) && T::ReadProximRecord("12345678", &back),
          "editor map: malformed proximity records refused");
    Check(T::ReadProximRecord("12345678 exit=x later=1", &back) && back.exit == "x",
          "editor map: a later version's keys skipped");
}
SELFTEST(Early, TestEditorMapProxim);

} // namespace
