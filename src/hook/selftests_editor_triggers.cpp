// Self-tests for the level editor's trigger volumes (src/game/front/level_editor_triggers.*, not original): the kinds a
// trigger script's calls make, the scripts compiled for added triggers (byte for byte as THUG2's own Boston gap scripts
// are compiled - BO_scripts.qb), the layout's trigger records, the boxes, a gap's two ends, and the trigger node in the
// editor's overlay.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_triggers.h"
#include "game/script/qb.h"
#include "hook/selftest.h"

namespace {

namespace T = Front::LevelEditor::Triggers;
namespace NC = Front::LevelEditor::NodeCreate;
using Front::LevelEditor::V3;
using K = Front::LevelEditor::Triggers::Kind;

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

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

void TestEditorTriggersKinds()
{
    T::Calls none;
    Check(T::KindOfCalls(none, false) == K::Other && T::KindOfCalls(none, true) == K::Script,
          "editor triggers: no calls - other, or runs a script");
    T::Calls c;
    c.startGap = true;
    Check(T::KindOfCalls(c, true) == K::GapStart, "editor triggers: StartGap - a gap start");
    c.endGap = true;
    Check(T::KindOfCalls(c, true) == K::GapEnd, "editor triggers: EndGap wins over StartGap (a transfer's both ends)");
    c.teleport = true;
    Check(T::KindOfCalls(c, true) == K::Teleport, "editor triggers: a teleport wins over gaps");
    c.kill = true;
    Check(T::KindOfCalls(c, true) == K::Kill, "editor triggers: a kill wins over everything");
    std::vector<uint32_t> colours;
    for (int i = 0; i < T::kKinds; ++i) {
        K kind = static_cast<K>(i), back;
        Check(T::KindFromWord(T::KindWord(kind), &back) && back == kind, "editor triggers: a kind's word read back");
        Check(T::KindName(kind)[0] != 0, "editor triggers: a kind has a name");
        uint32_t colour = T::KindColour(kind);
        Check((colour >> 24) == 0xFF, "editor triggers: a kind's colour is opaque");
        for (uint32_t other : colours)
            Check(other != colour, "editor triggers: the kinds' colours differ");
        colours.push_back(colour);
    }
    K kind;
    Check(!T::KindFromWord("Kill", &kind) && !T::KindFromWord("", &kind), "editor triggers: unknown words refused");
}
SELFTEST(Early, TestEditorTriggersKinds);

void TestEditorTriggersScripts()
{
    // The checksums the game's scripts use (as BO_scripts.qb has them).
    Check(Crc::GenerateCRCFromString("SK3_KillSkater") == 0xDEA3057Bu &&
              Crc::GenerateCRCFromString("nodename") == 0x9F92BA78u &&
              Crc::GenerateCRCFromString("StartGap") == 0x65B9EC39u && Crc::GenerateCRCFromString("EndGap") == 0xE5399FB2u,
          "editor triggers: the commands' checksums");
    // BO_Gap_StateHouseSpine1: StartGap GapID=StateHouseSpine1 flags=[CANCEL_GROUND]
    T::Spec start;
    start.name = "ed_trigger01";
    start.kind = K::GapStart;
    start.gap = "StateHouseSpine1";
    start.flags = {"CANCEL_GROUND"};
    Check(T::Body(start) == Hex("16 39 ec b9 65 16 26 2e 44 3b 07 16 2b 6a 6d 00 16 45 be fa f4 07 05 16 4d e7 27 9a 06 01"),
          "editor triggers: StartGap with a flag - as Boston's BO_Gap_StateHouseSpine1 is compiled");
    // ... its EndGap GapID=StateHouseSpine2 score=250 text="Statehouse Stair Spine"
    T::Spec end;
    end.name = "ed_trigger02";
    end.kind = K::GapEnd;
    end.gap = "StateHouseSpine2";
    end.score = 250;
    end.text = "Statehouse Stair Spine";
    Check(T::Body(end) == Hex("16 b2 9f 39 e5 16 26 2e 44 3b 07 16 91 3b 64 99 16 ae c8 66 cd 07 17 fa 00 00 00 16 38 58 74 "
                              "c4 07 1b 17 00 00 00 53 74 61 74 65 68 6f 75 73 65 20 53 74 61 69 72 20 53 70 69 6e 65 00 01"),
          "editor triggers: EndGap with a score and a text - as Boston's");
    // BO_GapStart_HighSlap: StartGap GapID=HighSlap flags=[CANCEL_RAIL, CANCEL_MANUAL, CANCEL_GROUND]
    T::Spec slap = start;
    slap.gap = "HighSlap";
    slap.flags = {"CANCEL_RAIL", "CANCEL_MANUAL", "CANCEL_GROUND"};
    Check(T::Body(slap) == Hex("16 39 ec b9 65 16 26 2e 44 3b 07 16 2c 87 ea 2f 16 45 be fa f4 07 05 16 ee ec 4d cd 09 16 e1 "
                               "da 03 2d 09 16 4d e7 27 9a 06 01"),
          "editor triggers: StartGap with three flags (commas between) - as Boston's BO_GapStart_HighSlap");
    T::Spec bare = start;
    bare.flags.clear();
    Check(T::Body(bare) == Hex("16 39 ec b9 65 16 26 2e 44 3b 07 16 2b 6a 6d 00 01"),
          "editor triggers: StartGap without flags leaves flags= out");
    // Kill / water / teleport / a script.
    T::Spec kill;
    kill.name = "ed_trigger03";
    kill.kind = K::Kill;
    kill.target = "TRG_Restart01";
    uint32_t restart = Crc::GenerateCRCFromString("TRG_Restart01");
    std::vector<uint8_t> expected = Hex("16 7b 05 a3 de 16 78 ba 92 9f 07 16");
    for (int i = 0; i < 4; ++i)
        expected.push_back(static_cast<uint8_t>(restart >> (8 * i)));
    expected.push_back(0x01);
    Check(T::Body(kill) == expected, "editor triggers: SK3_KillSkater nodename=<restart>");
    kill.water = true;
    std::vector<uint8_t> water = T::Body(kill);
    Check(water.size() == expected.size() && water[1] == static_cast<uint8_t>(Crc::GenerateCRCFromString("SK3_KillSkater_Water")),
          "editor triggers: water: SK3_KillSkater_Water");
    T::Spec teleport = kill;
    teleport.kind = K::Teleport;
    std::vector<uint8_t> tp = T::Body(teleport);
    uint32_t tpName = Crc::GenerateCRCFromString("TeleportSkaterToNode");
    Check(tp.size() == expected.size() && tp[1] == static_cast<uint8_t>(tpName) && tp[4] == static_cast<uint8_t>(tpName >> 24),
          "editor triggers: TeleportSkaterToNode nodename=<node>");
    T::Spec runs;
    runs.name = "ed_trigger04";
    runs.kind = K::Script;
    runs.script = "0x1234ABCD";
    Check(T::Body(runs) == Hex("16 cd ab 34 12 01"), "editor triggers: a script run - a call (a checksum without a name)");
    // What lacks something makes nothing.
    T::Spec missing = kill;
    missing.target.clear();
    T::Spec noText = end;
    noText.text.clear();
    T::Spec noScore = end;
    noScore.score = 0;
    T::Spec noGap = start;
    noGap.gap.clear();
    T::Spec noScript = runs;
    noScript.script.clear();
    Check(T::Problem(missing) && T::Body(missing).empty() && T::Problem(noText) && T::Body(noText).empty() &&
              T::Problem(noScore) && T::Problem(noGap) && T::Problem(noScript) && T::Body(noScript).empty(),
          "editor triggers: a trigger without its restart, gap, name, score or script makes an empty script");
    Check(!T::Problem(start) && !T::Problem(end) && !T::Problem(kill) && !T::Problem(teleport) && !T::Problem(runs),
          "editor triggers: complete triggers work");
    // The .qb file: script <name>Script ... endscript for each, the names, the end - every token walked to it.
    std::vector<T::Spec> specs = {start, end, kill, missing};
    std::vector<uint8_t> file = T::QbFile(specs);
    uint32_t first = Crc::GenerateCRCFromString("ed_trigger01Script");
    std::vector<uint8_t> head = {0x23, 0x16, static_cast<uint8_t>(first), static_cast<uint8_t>(first >> 8),
                                 static_cast<uint8_t>(first >> 16), static_cast<uint8_t>(first >> 24), 0x01};
    Check(file.size() > head.size() && std::equal(head.begin(), head.end(), file.begin()),
          "editor triggers: the file starts: script ed_trigger01Script");
    std::vector<uint8_t> startBody = T::Body(start);
    Check(std::equal(startBody.begin(), startBody.end(), file.begin() + 7) && file[7 + startBody.size()] == 0x24 &&
              file[8 + startBody.size()] == 0x01,
          "editor triggers: ... its body, endscript");
    int scripts = 0, names = 0;
    const uint8_t* p = file.data();
    const uint8_t* stop = file.data() + file.size();
    bool walked = false;
    for (int guard = 0; p < stop && guard < 1000; ++guard) {
        if (*p == 0x00) {
            walked = p + 1 == stop;
            break;
        }
        if (*p == 0x23)
            ++scripts;
        if (*p == 0x2B)
            ++names;
        p = Script::SkipToken(p);
    }
    Check(walked && scripts == 4, "editor triggers: the file's tokens walk to its end, a script a trigger");
    Check(names >= 6, "editor triggers: the scripts', gaps' and restart's names are in the file");
    std::string text(file.begin(), file.end());
    Check(text.find(std::string("ed_trigger03Script") + '\0') != std::string::npos &&
              text.find(std::string("StateHouseSpine1") + '\0') != std::string::npos,
          "editor triggers: the names as text");
}
SELFTEST(Early, TestEditorTriggersScripts);

void TestEditorTriggersRecords()
{
    T::Placed p;
    p.spec.name = "ed_trigger07";
    p.spec.kind = K::GapEnd;
    p.spec.size = {310.5f, 64.0f, 1200.25f};
    p.spec.gap = "ed_gap02";
    p.spec.flags = {"PURE_AIR", "CANCEL_GROUND"};
    p.spec.text = "Over the \"bank\" #2";
    p.spec.score = 750;
    p.spec.replaces = 0x0A1B2C3D;
    p.pos = {-120.5f, 8.0f, 330.125f};
    p.yaw = 1.5707964f;
    std::string record = T::Record(p);
    Check(record == "ed_trigger07 gapend pos=-120.5,8,330.125 yaw=1.57079637 size=310.5,64,1200.25 gap=ed_gap02 "
                    "flags=PURE_AIR,CANCEL_GROUND score=750 replaces=0A1B2C3D text=\"Over the 'bank' 2\"",
          "editor triggers: a gap end's record (the text without quotes or #)");
    T::Placed back;
    T::Spec expected = p.spec;
    expected.text = "Over the 'bank' 2";
    Check(T::ReadRecord(record, &back) && T::SameSpec(back.spec, expected) && back.pos.x == p.pos.x &&
              back.pos.z == p.pos.z && back.yaw == p.yaw,
          "editor triggers: ... read back exactly");
    T::Placed kill;
    kill.spec.name = "ed_trigger01";
    kill.spec.kind = K::Kill;
    kill.spec.target = "TRG_Restart_StateHouse";
    kill.spec.water = true;
    kill.pos = {1.0f, 2.0f, 3.0f};
    Check(T::ReadRecord(T::Record(kill), &back) && T::SameSpec(back.spec, kill.spec) && back.spec.water,
          "editor triggers: a water kill zone's record round trip");
    T::Placed runs;
    runs.spec.name = "ed_trigger02";
    runs.spec.kind = K::Script;
    runs.spec.script = "0x00C0FFEE";
    Check(T::ReadRecord(T::Record(runs), &back) && back.spec.script == "0x00C0FFEE" && back.spec.text.empty(),
          "editor triggers: a script's record (a checksum without a name)");
    // Refused / skipped.
    Check(!T::ReadRecord("ed_trigger01 kill yaw=0", &back), "editor triggers: a record without pos= refused");
    Check(!T::ReadRecord("ed_trigger01 kill pos=1,2,3 pos=1,2,3", &back), "editor triggers: a repeated key refused");
    Check(!T::ReadRecord("ed_trigger01 sometimes pos=1,2,3", &back) && !T::ReadRecord("ed_trigger01 other pos=1,2,3", &back),
          "editor triggers: an unknown kind (or other) refused");
    Check(!T::ReadRecord("ed_trigger01 kill pos=1,2", &back) && !T::ReadRecord("ed_trigger01 kill pos=1,2,3 water=2", &back) &&
              !T::ReadRecord("ed_trigger01 gapend pos=1,2,3 score=-5", &back) &&
              !T::ReadRecord("ed_trigger01 gapend pos=1,2,3 text=\"open", &back) &&
              !T::ReadRecord("ed_trigger01 gapend pos=1,2,3 text=\"a\" score=5", &back) &&
              !T::ReadRecord("ed_trigger01 gapend pos=1,2,3 replaces=XYZ", &back) &&
              !T::ReadRecord("bad-name kill pos=1,2,3", &back),
          "editor triggers: bad values refused");
    Check(T::ReadRecord("ed_trigger01 teleport pos=1,2,3 colour=red target=TRG_A", &back) && back.spec.target == "TRG_A" &&
              back.spec.size.x == T::Spec().size.x && back.yaw == 0.0f,
          "editor triggers: an unknown key skipped, defaults for what's left out");
    Check(T::ReadRecord("ed_trigger01 kill pos=1,2,3 size=1,99999,50", &back) && back.spec.size.x == 10.0f &&
              back.spec.size.y == 50000.0f && back.spec.size.z == 50.0f,
          "editor triggers: sizes kept within 10 .. 50000");
    // In a layout: trigger records, written and read back.
    namespace Layout = Front::LevelEditor::Layout;
    Layout::File file;
    file.scene = 0x4B7E41E2;
    file.triggers = {p, kill, runs};
    std::string text = Layout::Write(file);
    Check(text.find("\ntrigger ed_trigger01 kill pos=1,2,3") != std::string::npos,
          "editor triggers: the layout's trigger lines");
    Layout::File read;
    int line = -1;
    bool ok = Layout::Read(text, &read, &line) && read.triggers.size() == 3;
    for (size_t i = 0; ok && i < 3; ++i)
        ok = T::SameSpec(read.triggers[i].spec, i == 0 ? expected : file.triggers[i].spec);
    Check(ok, "editor triggers: ... read back from the layout");
    Check(!Layout::Read("thug2-decomp layout 2\nscene 4B7E41E2\ntrigger ed_trigger01 kill\n", &read, &line) && line == 3,
          "editor triggers: a layout with a broken trigger record refused (as a broken node record is)");
    Check(Layout::Read("thug2-decomp layout 2\nscene 4B7E41E2\ntrigger ed_trigger01 kill pos=1,2,3  # a comment\n", &read,
                       &line) &&
              read.triggers.size() == 1 && read.triggers[0].pos.z == 3.0f,
          "editor triggers: a trigger record with a comment after it");
}
SELFTEST(Early, TestEditorTriggersRecords);

void TestEditorTriggersPieces()
{
    Check(T::CleanText("  Big \"Air\"\t#1  ") == "Big 'Air'1", "editor triggers: a gap's name cleaned");
    Check(T::CleanText(std::string(80, 'a')).size() == 60, "editor triggers: a gap's name at most 60 characters");
    V3 c = T::ClampSize({0.0f, -5.0f, 1e9f});
    Check(c.x == 10.0f && c.y == 10.0f && c.z == 50000.0f, "editor triggers: sizes clamped");
    V3 nan = T::ClampSize({std::nanf(""), 100.0f, 100.0f});
    Check(nan.x == 10.0f && nan.y == 100.0f, "editor triggers: a size that isn't a number is the smallest");
    // The box: centred, extents halved, turned about up.
    Front::LevelEditor::OrientedBox box = T::BoxOf({10.0f, 20.0f, 30.0f}, 0.0f, {100.0f, 40.0f, 60.0f});
    Check(box.origin.x == 10.0f && Near(box.min.x, -50.0f) && Near(box.max.y, 20.0f) && Near(box.max.z, 30.0f) &&
              Near(box.axes[0].x, 1.0f) && Near(box.axes[2].z, 1.0f) && Near(box.axes[1].y, 1.0f),
          "editor triggers: an unturned box");
    Front::LevelEditor::OrientedBox turned = T::BoxOf({0.0f, 0.0f, 0.0f}, 1.5707964f, {100.0f, 40.0f, 60.0f});
    V3 expectAt = Front::LevelEditor::TurnAboutUp({0.0f, 0.0f, 1.0f}, 1.5707964f);
    Check(Near(turned.axes[2].x, expectAt.x) && Near(turned.axes[2].z, expectAt.z) && Near(turned.axes[1].y, 1.0f) &&
              Near(Front::LevelEditor::Dot(turned.axes[0], turned.axes[2]), 0.0f) && Near(std::fabs(turned.axes[2].x), 1.0f),
          "editor triggers: a box turned a quarter about up (its axes as a node's heading turns them)");
    float t = 0.0f;
    Check(Front::LevelEditor::RayBox({200.0f, 10.0f, -500.0f}, {0.0f, 0.0f, 1.0f}, box.origin, box.axes, box.min, box.max, &t) ==
              false,
          "editor triggers: a ray beside the box misses");
    Check(Front::LevelEditor::RayBox({10.0f, 20.0f, -500.0f}, {0.0f, 0.0f, 1.0f}, box.origin, box.axes, box.min, box.max, &t) &&
              Near(t, 500.0f), "editor triggers: a ray into the box meets its face");
    // Names and checksums.
    Check(T::ChecksumOf("0x0000ABCD") == 0xABCD && T::ChecksumOf("TRG_Restart01") == Crc::GenerateCRCFromString("TRG_Restart01") &&
              T::ChecksumOf("0xABC") == Crc::GenerateCRCFromString("0xABC") && T::ChecksumOf("") == 0,
          "editor triggers: names and checksums");
    Check(T::NameFor(Crc::GenerateCRCFromString("TRG_Restart01"), "TRG_Restart01") == "TRG_Restart01" &&
              T::NameFor(0x1234, "TRG_Restart01") == "0x00001234" && T::NameFor(0xABCD, nullptr) == "0x0000ABCD",
          "editor triggers: a checksum's name written only when it is its name");
    Check(T::ScriptName(T::Spec{"ed_trigger05"}) == "ed_trigger05Script", "editor triggers: the script's name");
    // A gap's two ends.
    std::vector<T::Spec> specs(4);
    specs[0].name = "a"; specs[0].kind = K::GapStart; specs[0].gap = "ed_gap01";
    specs[1].name = "b"; specs[1].kind = K::Kill; specs[1].gap = "ed_gap01";
    specs[2].name = "c"; specs[2].kind = K::GapEnd; specs[2].gap = "ED_GAP01";
    specs[3].name = "d"; specs[3].kind = K::GapEnd; specs[3].gap = "ed_gap02";
    Check(T::Partner(specs, 0) == 2 && T::Partner(specs, 2) == 0, "editor triggers: a gap's start and end are partners");
    Check(T::Partner(specs, 1) == -1 && T::Partner(specs, 3) == -1 && T::Partner(specs, 9) == -1,
          "editor triggers: no partner for a kill, a lone end, a bad index");
    Check(T::GapFlags().size() >= 20, "editor triggers: the gap flags offered");
}
SELFTEST(Early, TestEditorTriggersPieces);

void TestEditorTriggersNode()
{
    // A trigger volume's node in the overlay: GenericNode type ed_trigger (the Triggers family), its TriggerScript after.
    NC::Spec n;
    n.name = "ed_trigger01";
    n.cls = "GenericNode";
    n.type = "ed_trigger";
    n.pos = {1.0f, 2.0f, 3.0f};
    NC::Field script = {NC::Field::kName, "TriggerScript"};
    script.text = "ed_trigger01Script";
    n.extra.push_back(script);
    n.owned = true;
    std::vector<NC::Field> fields = NC::Fields(n);
    Check(fields.size() == 6 && fields.back().key == "TriggerScript" && fields.back().text == "ed_trigger01Script" &&
              fields[3].text == "GenericNode" && fields[4].text == "ed_trigger",
          "editor triggers: the node's fields - pos, angles, name, class, type, then its TriggerScript");
    NC::Kind kind;
    Check(!NC::KindOf(n, &kind), "editor triggers: not one of the Place palette's kinds");
    NC::Spec spawn = NC::Default(NC::Kind::Spawn, NC::Team::Red, {0, 0, 0}, 0.0f, "ed_restart01");
    size_t plain = NC::Fields(spawn).size();
    spawn.extra.push_back(script);
    Check(NC::Fields(spawn).size() == plain + 1 && NC::Fields(spawn).back().key == "TriggerScript",
          "editor triggers: extra fields go after a palette kind's own too");
    NC::Spec other = n;
    Check(NC::SameSpec(n, other), "editor triggers: the same node spec");
    other.extra.back().text = "ed_trigger02Script";
    NC::Spec unowned = n;
    unowned.owned = false;
    Check(!NC::SameSpec(n, other) && !NC::SameSpec(n, unowned), "editor triggers: specs differing in extra / owned differ");
    namespace Nodes = Front::LevelEditor::Nodes;
    Check(Nodes::Classify(Crc::GenerateCRCFromString("GenericNode"), Crc::GenerateCRCFromString("ed_trigger")) ==
                  Nodes::Family::Trigger &&
              Nodes::Classify(Crc::GenerateCRCFromString("GenericNode"), Crc::GenerateCRCFromString("Crown")) ==
                  Nodes::Family::Crown &&
              Nodes::Classify(Crc::GenerateCRCFromString("GenericNode"), 0) == Nodes::Family::Other,
          "editor triggers: the node is in the Triggers family (other generic nodes as before)");
}
SELFTEST(Early, TestEditorTriggersNode);

} // namespace
