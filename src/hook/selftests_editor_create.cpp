// Self-tests for the level editor's added and hidden nodes (src/game/front/level_editor_node_create.*, not original -
// phase 3 of the editor plan): fresh names, each kind's node as THUG2's levels have it, the limits, hiding the last
// restart, the overlay's list as undo / redo use it (create / delete), the added crowns' reordering, the layouts'
// node / order / hide records (and older files still read), and the node manager passing over the overlay array.

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_nodes.h"
#include "game/script/node_manager.h"
#include "game/script/struct.h"
#include "hook/selftest.h"

namespace {

namespace NC = Front::LevelEditor::NodeCreate;
namespace Nodes = Front::LevelEditor::Nodes;
using Front::LevelEditor::V3;

const NC::Field* FieldNamed(const std::vector<NC::Field>& fields, const char* key)
{
    for (const NC::Field& f : fields) {
        if (f.key == key)
            return &f;
    }
    return nullptr;
}
bool HasFlag(const std::vector<NC::Field>& fields, const char* flag)
{
    for (const NC::Field& f : fields) {
        if (f.type == NC::Field::kFlag && f.text == flag)
            return true;
    }
    return false;
}

// --- names and kinds ----------------------------------------------------------------------------------------------

void TestEditorCreateNames()
{
    std::set<std::string> taken = {"ed_crown01", "ed_crown02", "ed_crown04"};
    auto isTaken = [&](const std::string& name) { return taken.count(name) != 0; };
    Check(NC::FreshName("ed_crown", isTaken) == "ed_crown03", "editor create: the first free name");
    Check(NC::FreshName("ed_light", isTaken) == "ed_light01", "editor create: names start at 01");
    for (int i = 1; i <= 99; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "ed_x%02d", i);
        taken.insert(name);
    }
    Check(NC::FreshName("ed_x", isTaken) == "ed_x100", "editor create: past 99, three digits");
    Check(std::string(NC::NameStem(NC::Kind::Spawn)) == "ed_restart" && std::string(NC::NameStem(NC::Kind::Proxim)) == "ed_proxim",
          "editor create: the stems");
    Check(NC::FixedName(NC::Kind::TeamSpawn, NC::Team::Green) == "TRG_CTF_Restart_Green" &&
              NC::FixedName(NC::Kind::FlagSet, NC::Team::Blue) == "TRG_CTF_Blue" &&
              NC::FixedName(NC::Kind::Crown, NC::Team::Red).empty(),
          "editor create: the name-driven kinds keep the names the game looks for");
    Check(NC::ValidName("ed_restart01") && NC::ValidName("TRG_CTF_Red") && !NC::ValidName("") &&
              !NC::ValidName("a b") && !NC::ValidName("x=1") && !NC::ValidName(std::string(64, 'a')),
          "editor create: names a record can hold");
}
SELFTEST(Early, TestEditorCreateNames);

void TestEditorCreateKinds()
{
    // Each kind as THUG2's own levels have it (the converters' forms).
    NC::Spec spawn = NC::Default(NC::Kind::Spawn, NC::Team::Red, {1, 2, 3}, 1.0f, "ed_restart01");
    std::vector<NC::Field> f = NC::Fields(spawn);
    const NC::Field* types = FieldNamed(f, "restart_types");
    const NC::Field* restartName = FieldNamed(f, "RestartName");
    Check(spawn.cls == "Restart" && spawn.type == "MultiPlayer" && types && types->type == NC::Field::kNames &&
              types->list == std::vector<std::string>({"MultiPlayer", "Horse"}) && restartName &&
              restartName->text == "Multi: Restart" && HasFlag(f, "CreatedAtStart"),
          "editor create: a multiplayer spawn (Restart, MultiPlayer, [MultiPlayer Horse], Multi: Restart)");
    const NC::Field* pos = FieldNamed(f, "Pos");
    const NC::Field* angles = FieldNamed(f, "Angles");
    Check(pos && pos->value[0] == 1 && pos->value[2] == 3 && angles && angles->value[0] == 0 && angles->value[1] == 1.0f &&
              angles->value[2] == 0,
          "editor create: placed and facing the camera's heading (angles (0, yaw, 0))");
    Check(NC::Default(NC::Kind::Spawn, NC::Team::Red, {}, 4.0f, "a").angles.y < 0.0f &&
              NC::Default(NC::Kind::Spawn, NC::Team::Red, {}, -4.0f, "a").angles.y > 0.0f,
          "editor create: the heading kept within a half turn");

    NC::Spec team = NC::Default(NC::Kind::TeamSpawn, NC::Team::Yellow, {}, 0, NC::FixedName(NC::Kind::TeamSpawn, NC::Team::Yellow));
    f = NC::Fields(team);
    Check(team.type == "CTF" && FieldNamed(f, "RestartName") && FieldNamed(f, "RestartName")->text == "Team: CTF Yellow" &&
              FieldNamed(f, "restart_types")->list == std::vector<std::string>({"CTF", "MultiPlayer", "Horse"}),
          "editor create: a team spawn (CTF, Team: CTF <colour>)");

    NC::Spec crown = NC::Default(NC::Kind::Crown, NC::Team::Red, {}, 0, "ed_crown01");
    f = NC::Fields(crown);
    Check(crown.cls == "GenericNode" && crown.type == "Crown" && HasFlag(f, "CreatedAtStart") && !FieldNamed(f, "restart_types"),
          "editor create: a crown (GenericNode Type=Crown)");

    NC::Spec flag = NC::Default(NC::Kind::FlagSet, NC::Team::Blue, {}, 0, "TRG_CTF_Blue");
    NC::Spec base = NC::Base(flag);
    std::vector<NC::Field> ff = NC::Fields(flag), fb = NC::Fields(base);
    Check(flag.cls == "GameObject" && flag.type == "Flag_Blue" && base.name == "TRG_CTF_Blue_Base" &&
              base.type == "Flag_Blue_Base" && FieldNamed(ff, "Model") &&
              FieldNamed(ff, "Model")->text == "gameobjects\\flags\\flag_blue\\flag_blue.mdl" && FieldNamed(fb, "Model") &&
              FieldNamed(fb, "Model")->text == "gameobjects\\flags\\flag_blue_base\\flag_blue_base.mdl" &&
              FieldNamed(ff, "TriggerScript") && FieldNamed(ff, "TriggerScript")->text == "TRG_CTF_BlueScript" &&
              !FieldNamed(fb, "TriggerScript") && HasFlag(ff, "NeverSuspend") && FieldNamed(ff, "lod_dist1")->integer == 800,
          "editor create: a CTF flag set (Airport's form: flag with its Team_Flag script, base without)");

    NC::Spec light = NC::Default(NC::Kind::Light, NC::Team::Red, {}, 0, "ed_light01");
    f = NC::Fields(light);
    const NC::Field* colour = FieldNamed(f, "Color");
    Check(light.cls == "LevelLight" && colour && colour->type == NC::Field::kIntegers && colour->integers.size() == 3 &&
              FieldNamed(f, "OuterRadius") && FieldNamed(f, "OuterRadius")->value[0] == light.outer &&
              light.outer > light.inner && FieldNamed(f, "Brightness"),
          "editor create: a level light (Color, Brightness, Inner / OuterRadius)");
    for (NC::Kind kind : {NC::Kind::Sound, NC::Kind::Proxim}) {
        NC::Spec s = NC::Default(kind, NC::Team::Red, {}, 0, "x");
        f = NC::Fields(s);
        Check(FieldNamed(f, "Shape") && FieldNamed(f, "Shape")->text == "Sphere" && FieldNamed(f, "Radius") &&
                  FieldNamed(f, "Radius")->value[0] > 0.0f,
              "editor create: a sound emitter / proximity node is a sphere with a radius");
    }
    // The kind read back from a spec (what a layout's record gives).
    bool kinds = true;
    for (int k = 0; k < NC::kKinds; ++k) {
        auto kind = static_cast<NC::Kind>(k);
        NC::Spec s = NC::Default(kind, NC::Team::Green, {}, 0, kind == NC::Kind::TeamSpawn || kind == NC::Kind::FlagSet
                                                                    ? NC::FixedName(kind, NC::Team::Green)
                                                                    : "ed_n01");
        NC::Kind back;
        kinds = kinds && NC::KindOf(s, &back) && back == kind;
    }
    NC::Spec other;
    other.cls = "Waypoint";
    NC::Kind back;
    Check(kinds && !NC::KindOf(other, &back), "editor create: each kind told from its class / type / name");
}
SELFTEST(Early, TestEditorCreateKinds);

// --- limits -------------------------------------------------------------------------------------------------------

void TestEditorCreateLimits()
{
    NC::Counts c;
    c.crowns = 9;
    Check(!NC::LimitMessage(NC::Kind::Crown, c), "editor create: a 10th crown allowed");
    c.crowns = 10;
    Check(NC::LimitMessage(NC::Kind::Crown, c) != nullptr, "editor create: an 11th crown refused (SpawnCrown's 10)");
    c.restarts = 255;
    Check(!NC::LimitMessage(NC::Kind::Spawn, c) && !NC::LimitMessage(NC::Kind::TeamSpawn, c),
          "editor create: the 256th restart allowed");
    c.restarts = 256;
    Check(NC::LimitMessage(NC::Kind::Spawn, c) && NC::LimitMessage(NC::Kind::TeamSpawn, c),
          "editor create: a 257th restart refused (GetRestartNode's 256)");
    c.dynamicLights = 255;
    c.levelLights = 149;
    Check(!NC::LimitMessage(NC::Kind::Light, c), "editor create: a light allowed below both lists' sizes");
    c.levelLights = 150;
    Check(NC::LimitMessage(NC::Kind::Light, c) != nullptr, "editor create: a light refused with 150 level lights");
    c.levelLights = 0;
    c.dynamicLights = 256;
    Check(NC::LimitMessage(NC::Kind::Light, c) != nullptr, "editor create: a light refused with 256 dynamic lights");
    Check(!NC::LimitMessage(NC::Kind::FlagSet, c) && !NC::LimitMessage(NC::Kind::Sound, c) &&
              !NC::LimitMessage(NC::Kind::Proxim, c),
          "editor create: no limit on flags, sounds, proximity nodes");
    // Hiding a restart: not the last Player1 / Multiplayer one.
    Check(NC::HideLeavesNoRestart(Nodes::kPlayer1, Nodes::kMultiplayer) &&
              NC::HideLeavesNoRestart(Nodes::kMultiplayer | Nodes::kHorse, Nodes::kPlayer1 | Nodes::kHorse) &&
              !NC::HideLeavesNoRestart(Nodes::kMultiplayer, Nodes::kMultiplayer) &&
              !NC::HideLeavesNoRestart(Nodes::kHorse | Nodes::kCtf, 0),
          "editor create: hiding the last Player1 / Multiplayer restart refused, others allowed");
}
SELFTEST(Early, TestEditorCreateLimits);

// --- the overlay's list: undo / redo of create and delete, crowns reordered ----------------------------------------

std::string Names(const NC::List& list)
{
    std::string text;
    for (const NC::Spec& s : list)
        text += (text.empty() ? "" : " ") + s.name;
    return text;
}

void TestEditorCreateUndo()
{
    NC::List list;
    auto make = [](NC::Kind kind, const char* name) { return NC::Default(kind, NC::Team::Red, {0, 0, 0}, 0, name); };
    NC::Add(&list, make(NC::Kind::Crown, "c1"));
    NC::Add(&list, make(NC::Kind::Spawn, "s1"));
    int at = NC::Add(&list, make(NC::Kind::Crown, "c2"));
    NC::Add(&list, make(NC::Kind::Light, "l1"));
    NC::Add(&list, make(NC::Kind::Crown, "c3"));
    Check(at == 2 && Names(list) == "c1 s1 c2 l1 c3", "editor create: nodes added at the end");
    // Delete, undo, redo, undo (create undone: taken; redone: put back).
    NC::Spec taken;
    int index = -1;
    Check(NC::Take(&list, "c2", &taken, &index) && index == 2 && taken.name == "c2" && Names(list) == "c1 s1 l1 c3",
          "editor create: a node deleted (its place noted)");
    taken.pos = {5, 6, 7}; // (moved before it was deleted: the step keeps where it was)
    NC::PutBack(&list, taken, index);
    Check(Names(list) == "c1 s1 c2 l1 c3" && list[2].pos.y == 6.0f, "editor create: undo puts it back where it was");
    NC::Spec again;
    int index2 = -1;
    Check(NC::Take(&list, "c2", &again, &index2) && index2 == 2 && NC::SameSpec(again, taken),
          "editor create: redo deletes it again");
    NC::PutBack(&list, again, 99);
    Check(Names(list) == "c1 s1 l1 c3 c2", "editor create: put back past the end goes last");
    Check(!NC::Take(&list, "nope", &again, &index2) && NC::IndexOf(list, "L1") == 2,
          "editor create: a missing name isn't taken; names compare without case (as checksums)");
    // Crowns: their order; moving among the crowns (other nodes keep their places).
    Check(NC::CrownOrder(list) == std::vector<std::string>({"c1", "c3", "c2"}), "editor create: the crowns' order");
    Check(NC::MoveCrown(&list, "c2", -1) && Names(list) == "c1 s1 l1 c2 c3", "editor create: a crown moved up");
    Check(NC::MoveCrown(&list, "c2", -1) && Names(list) == "c2 s1 l1 c1 c3",
          "editor create: past other nodes, swapping with the crown before");
    Check(!NC::MoveCrown(&list, "c2", -1) && !NC::MoveCrown(&list, "c3", 1) && !NC::MoveCrown(&list, "s1", 1) &&
              !NC::MoveCrown(&list, "c1", 2),
          "editor create: no move past the ends, of a non-crown, or by more than one");
    // An order record put in place.
    NC::ApplyCrownOrder(&list, {"c3", "nope", "s1", "c1"});
    Check(Names(list) == "c3 s1 l1 c1 c2", "editor create: an order record fills the crowns' slots (unknown names skipped, "
                                            "unnamed crowns after)");
    NC::ApplyCrownOrder(&list, {});
    Check(Names(list) == "c3 s1 l1 c1 c2", "editor create: an empty order changes nothing");
}
SELFTEST(Early, TestEditorCreateUndo);

// --- layouts: node / order / hide records ---------------------------------------------------------------------------

void TestEditorCreateLayout()
{
    namespace Layout = Front::LevelEditor::Layout;
    Layout::File file;
    file.scene = 0x4DDB9865;
    NC::Spec spawn = NC::Default(NC::Kind::Spawn, NC::Team::Red, {-120.5f, 8.0f, 330.125f}, 1.5707964f, "ed_restart01");
    NC::Spec light = NC::Default(NC::Kind::Light, NC::Team::Red, {0, 250, 0}, 0, "ed_light01");
    light.colour[0] = 12;
    light.colour[2] = 0;
    NC::Spec proxim = NC::Default(NC::Kind::Proxim, NC::Team::Red, {1, 2, 3}, -0.25f, "ed_proxim01");
    NC::Spec flag = NC::Default(NC::Kind::FlagSet, NC::Team::Red, {7, 8, 9}, 0, "TRG_CTF_Red");
    file.added = {spawn, light, proxim, flag, NC::Base(flag)};
    file.crownOrder = {"ed_crown02", "ed_crown01"};
    file.hidden = {0x8E1A2B3C, 0x00000007};
    file.nodes.push_back({0x0000000A, true, false, {1, 2, 3}, {}});
    std::string text = Layout::Write(file);
    Check(text.find("\nnode ed_restart01 Restart type=MultiPlayer pos=-120.5,8,330.125 angles=0,1.57079637,0 "
                    "types=MultiPlayer,Horse\n") != std::string::npos,
          "editor layouts: a node record's text");
    Check(text.find("\norder crown ed_crown02 ed_crown01\n") != std::string::npos &&
              text.find("\nhide 8E1A2B3C") != std::string::npos,
          "editor layouts: order and hide records' text");
    Layout::File back;
    int line = -1;
    bool read = Layout::Read(text, &back, &line);
    bool same = read && back.added.size() == file.added.size() && back.crownOrder == file.crownOrder &&
                back.hidden == file.hidden && back.nodes.size() == 1;
    for (size_t i = 0; same && i < file.added.size(); ++i)
        same = NC::SameSpec(back.added[i], file.added[i]);
    Check(same, "editor layouts: node / order / hide records written and read back exactly");

    // Read: unknown keys skipped; bad records refused; older files still read.
    Check(Layout::Read("thug2-decomp layout 2\nnode ed_x01 GenericNode type=Crown pos=1,2,3 later=9\n", &back, &line) &&
              back.added.size() == 1 && back.added[0].type == "Crown" && back.added[0].angles.y == 0.0f,
          "editor layouts: a node record's unknown keys skipped, angles optional");
    Check(!Layout::Read("thug2-decomp layout 2\nnode ed_x01 GenericNode type=Crown\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nnode ed_x01 pos=1,2,3\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nnode ed x01 Restart pos=1,2,3\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nnode ed_x01 Restart pos=1,2,3 pos=1,2,3\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nnode ed_x01 LevelLight pos=1,2,3 color=1,2,256\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nnode ed_x01 LevelLight pos=1,2,3 radius=x\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nnode ed_x01 Restart pos=1,2,3 types=A,,B\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\nhide XYZ\n", &back, &line) &&
              !Layout::Read("thug2-decomp layout 2\norder crown a b=c\n", &back, &line),
          "editor layouts: malformed node / hide / order records refused");
    Check(Layout::Read("thug2-decomp layout 2\norder rails a b\n", &back, &line) && back.crownOrder.empty(),
          "editor layouts: an order of something else skipped");
    const char kPhase2[] = "thug2-decomp layout 2\nscene 4DDB9865  # TR\nset 8E1A2B3C pos=1,2,3\n";
    Check(Layout::Read(kPhase2, &back, &line) && back.nodes.size() == 1 && back.added.empty() && back.hidden.empty(),
          "editor layouts: a phase 2 file (no node / hide records) still loads");
    Check(Layout::Read("thug2-decomp layout 1\nscene 4DDB9865\nsector 61EF3AD4 1 -117.6 0 150.75 0\n", &back, &line) &&
              back.version == 1 && back.sectors.size() == 1 && back.added.empty(),
          "editor layouts: a version 1 file still loads");
}
SELFTEST(Early, TestEditorCreateLayout);

// --- the node manager: the overlay passed over by GetLastNodeArray / GetLastNodeArrayName ---------------------------------

void TestEditorCreateNodeManager()
{
    using M = SkateScript::CNodeManager;
    std::vector<uint8_t> memory(sizeof(M));
    auto* m = reinterpret_cast<M*>(memory.data());
    m->Construct();
    Script::CArray level = {}, overlay = {}, other = {}; // (no nodes: nothing hashed)
    m->m_arrays[0] = {0x1111, &level, true, {}};
    m->m_arrays[1] = {0x2222, &overlay, false, {}};
    m->m_count = 2;
    Check(m->GetLastNodeArray() == &overlay && m->GetLastNodeArrayName() == 0x2222,
          "editor create: with no overlay named, the last array is the last (the original)");
    SkateScript::SetEditorNodeArray(0x2222);
    Check(m->GetLastNodeArray() == &level && m->GetLastNodeArrayName() == 0x1111,
          "editor create: the overlay passed over by GetLastNodeArray / Name");
    m->m_arrays[2] = {0x3333, &other, false, {}};
    m->m_count = 3;
    Check(m->GetLastNodeArray() == &other && m->GetLastNodeArrayName() == 0x3333,
          "editor create: an array added after the overlay is the last");
    m->m_count = 2;
    m->m_arrays[0].m_name = 0x2222;
    m->m_arrays[1].m_name = 0x2222;
    Check(m->GetLastNodeArray() == nullptr && m->GetLastNodeArrayName() == 0,
          "editor create: only the overlay: no last array");
    SkateScript::SetEditorNodeArray(0);
    m->m_count = 0;
    Check(m->GetLastNodeArray() == nullptr && m->GetLastNodeArrayName() == 0, "editor create: no arrays: none");
}
SELFTEST(Early, TestEditorCreateNodeManager, [] {
    SkateScript::SetEditorNodeArray(0);
    SkateScript::SetNodeArrayRemovedHook(nullptr);
});

} // namespace
