// Self-tests for the level editor's goals (src/game/front/level_editor_goals.* - not original): Create-a-Goal's types and
// their points against the game's own SetType (0x004E6DA0), the created-goal record filled here against the game's own
// ReadFromStructure (0x004E96D0) reading the same goal as a script structure (the keys WriteIntoStructure 0x004EB260
// writes), the `goal` records (round trips, refusals, cleaning), and a layout carrying goals read back.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/ed/goal_editor_util.h"
#include "game/front/level_editor_goals.h"
#include "game/front/level_editor_layout.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

namespace Goals = Front::LevelEditor::Goals;
namespace Layout = Front::LevelEditor::Layout;
using Goals::Type;

// For the test's length: the goal type's script structure (0x004E67A0) is none - offline there are no scripts (SetType
// then keeps InitEditedGoal's time / score / name, as for a type without one).
struct NoTypeStructures {
    TestStandIns::Patch patch{0x004E67A0, {0x31, 0xC0, 0xC3}}; // xor eax, eax; ret
};

// SetType (0x004E6DA0, __thiscall goal, type; RET 4) on a record InitEditedGoal made.
void GameSetType(uint8_t* record, uint32_t type)
{
    Ed::InitEditedGoal(record);
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x004E6DA0)(record, type);
}

void TestEditorGoalsTypes()
{
    NoTypeStructures none;
    bool names = true, kinds = true, counts = true, spins = true;
    for (int t = 0; t < Goals::kTypes; ++t) {
        Type type = static_cast<Type>(t);
        Type back;
        names = names && Goals::TypeChecksum(type) == Crc::GenerateCRCFromString(Goals::TypeName(type)) &&
                Goals::TypeOf(Goals::TypeName(type), &back) && back == type &&
                Goals::TypeOfChecksum(Goals::TypeChecksum(type), &back) && back == type;
        alignas(4) static uint8_t record[Goals::kImageSize];
        std::memset(record, 0xCD, sizeof(record));
        GameSetType(record, Goals::TypeChecksum(type));
        for (int i = 0; i < Goals::kMaxPoints; ++i) {
            uint32_t kind;
            std::memcpy(&kind, record + 0x118 + 0x1C * i, 4);
            kinds = kinds && kind == static_cast<uint32_t>(Goals::PointKind(type, i));
            float height;
            std::memcpy(&height, record + 0x12C + 0x1C * i, 4);
            kinds = kinds && height == 0.0f;
        }
        counts = counts && record[0x285] == Goals::MinPoints(type) && record[0x284] == Goals::MaxPoints(type) &&
                 Goals::MinPoints(type) <= Goals::MaxPoints(type);
        uint32_t spin;
        std::memcpy(&spin, record + 0x2B8, 4);
        spins = spins && static_cast<int>(spin) == Goals::Defaults(type, "ed_goal01").spin;
    }
    Check(names, "editor goals: the types' names and checksums (Skate 0x54166ACD .. Gap 0x61C5D092) both ways");
    Check(std::string(Goals::TypeTitle(Type::Skate)) == "SKATE letters" &&
              std::string(Goals::TypeTitle(Type::HighCombo)) == "High Combo" &&
              std::string(Goals::TypeTitle(Type::Race)) == "Checkpoint / Race" && Goals::TypeChecksum(Type::Gap) == 0x61C5D092,
          "editor goals: the types in Create-a-Goal's menu order (goals_menu_info)");
    Check(kinds, "editor goals: each point's kind as SetType 0x004E6DA0 makes it (pro, start, letters, checkpoints)");
    Check(counts, "editor goals: the fewest / most points as SetType sets them (SKATE / COMBO 7, race 4..12, others 2)");
    Check(spins, "editor goals: the trick goals' spin default (Trick to the Beat 0, SetType)");
    Type unknown;
    Check(!Goals::TypeOf("Horse", &unknown) && !Goals::TypeOfChecksum(0x12345678, &unknown),
          "editor goals: an unknown type refused");
    // Labels and node suffixes.
    Check(Goals::PointLabel(Type::Skate, 0) == "Pro" && Goals::PointLabel(Type::Skate, 1) == "Start" &&
              Goals::PointLabel(Type::Skate, 2) == "S" && Goals::PointLabel(Type::Skate, 6) == "E" &&
              Goals::PointLabel(Type::Combo, 3) == "O" && Goals::PointLabel(Type::Combo, 6) == "O" &&
              Goals::PointLabel(Type::Race, 2) == "Checkpoint 1" && Goals::PointLabel(Type::Race, 11) == "Checkpoint 10",
          "editor goals: point labels");
    Check(Goals::PointSuffix(Type::Skate, 2) == "s" && Goals::PointSuffix(Type::Skate, 6) == "e" &&
              Goals::PointSuffix(Type::Combo, 3) == "o" && Goals::PointSuffix(Type::Combo, 6) == "o2" &&
              Goals::PointSuffix(Type::Race, 3) == "cp2" && Goals::PointSuffix(Type::Gap, 1) == "start",
          "editor goals: point node suffixes (unique within a goal)");
    Check(Goals::DefaultHeight(Type::Skate, 2) == 27.0f && Goals::DefaultHeight(Type::Combo, 6) == 27.0f &&
              Goals::DefaultHeight(Type::Race, 2) == 0.0f && Goals::DefaultHeight(Type::Skate, 0) == 0.0f,
          "editor goals: letters float GoalEditor_ObjectHeight (27) above their node");
    Check(Goals::ComboSetCount(Type::Skatetris) == 8 && Goals::ComboSetCount(Type::TrickBeat) == 4 &&
              Goals::ComboSetCount(Type::Race) == 0 && std::string(Goals::ComboSetName(7)) == "Double Tap Diagonal Grabs",
          "editor goals: key combo sets (cag_key_combos; Trick to the Beat the first four)");
    Check(Goals::PlayNodeName(0, 20) == "TRG_EditedGoal20_Pro" && Goals::PlayNodeName(1, 23) == "TRG_EditedGoal23_Restart" &&
              Goals::PlayNodeName(4, 21) == "TRG_EditedGoal21_Object_2" && Goals::GoalId(29) == "CreatedGoal29" &&
              Goals::PlayIndex(0) == 20 && Goals::PlayIndex(9) == 29,
          "editor goals: play names (EditedGoalNodeName's formats; numbers 20..29, never Create-a-Goal's 0..9)");
    // The names agree with the game's own EditedGoalNodeName (0x004E6730 - ours, its formats).
    Check(Ed::EditedGoalNodeName(0, 20) == Crc::GenerateCRCFromString(Goals::PlayNodeName(0, 20).c_str()) &&
              Ed::EditedGoalNodeName(1, 24) == Crc::GenerateCRCFromString(Goals::PlayNodeName(1, 24).c_str()) &&
              Ed::EditedGoalNodeName(12, 27) == Crc::GenerateCRCFromString(Goals::PlayNodeName(12, 27).c_str()),
          "editor goals: play node names as EditedGoalNodeName makes them");
    float heading = 0.75f;
    Check(std::fabs(Goals::HeadingOf(Goals::ImageAngle(heading)) - heading) < 1e-6f &&
              std::fabs(Goals::ImageAngle(0.0f) - 3.14159265f) < 1e-6f,
          "editor goals: a point's heading and the record's angle (the node faces angle - pi)");
}

// A script structure as the game's getters answer for it (offline there are no structure pools): the getters
// ReadFromStructure and its helpers call (GetChecksum 0x00476950, GetInteger 0x00476630, GetString 0x00476590, GetFloat
// 0x004766D0, GetVector 0x00476FB0, GetArray 0x00476860, ContainsFlag 0x00476B40) are stood in for and answer from
// this, keyed by the structure's address and the name.
struct FakeStruct {
    std::map<uint32_t, uint32_t> words; // integers and checksums
    std::map<uint32_t, std::string> strings;
    std::map<uint32_t, float> floats;
    std::map<uint32_t, Front::LevelEditor::V3> vectors;
    std::map<uint32_t, Script::CArray*> arrays;
    std::set<uint32_t> flags;
};
// Arrays and their elements live as long as the test.
struct FakeArrays {
    std::vector<std::unique_ptr<FakeStruct>> structs;
    std::vector<std::unique_ptr<Script::CArray>> arrays;
    std::vector<std::unique_ptr<std::vector<uint32_t>>> elements;
    Script::CArray* Make(const std::vector<uint32_t>& values, Script::ESymbolType type)
    {
        elements.push_back(std::make_unique<std::vector<uint32_t>>(values));
        auto array = std::make_unique<Script::CArray>();
        std::memset(array.get(), 0, sizeof(Script::CArray));
        array->m_type = type;
        array->m_size = static_cast<uint32_t>(values.size());
        if (values.size() == 1)
            array->m_single = values[0];
        else
            array->m_elements = elements.back()->data();
        arrays.push_back(std::move(array));
        return arrays.back().get();
    }
    FakeStruct* New()
    {
        structs.push_back(std::make_unique<FakeStruct>());
        return structs.back().get();
    }
};

void StandInGetters(TestStandIns::StandIns& s)
{
    using TestStandIns::AddressOf;
    using TestStandIns::Regs;
    using Script::CStruct;
    auto fake = [](Regs& r) { return reinterpret_cast<FakeStruct*>(r.ecx); };
    auto word = [fake](Regs& r) {
        auto it = fake(r)->words.find(r.args[0]);
        if (it == fake(r)->words.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         word);
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12, word);
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [fake](Regs& r) {
             auto it = fake(r)->strings.find(r.args[0]);
             if (it == fake(r)->strings.end())
                 return 0u;
             *reinterpret_cast<const char**>(r.args[1]) = it->second.c_str();
             return 1u;
         });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [fake](Regs& r) {
             auto it = fake(r)->floats.find(r.args[0]);
             if (it == fake(r)->floats.end())
                 return 0u;
             *reinterpret_cast<float*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476FB0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [fake](Regs& r) {
             auto it = fake(r)->vectors.find(r.args[0]);
             if (it == fake(r)->vectors.end())
                 return 0u;
             auto* out = reinterpret_cast<Script::VectorValue*>(r.args[1]);
             *out = {it->second.x, it->second.y, it->second.z, 1.0f}; // (as GetVector: w 1)
             return 1u;
         });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [fake](Regs& r) {
             auto it = fake(r)->arrays.find(r.args[0]);
             if (it == fake(r)->arrays.end())
                 return 0u;
             *reinterpret_cast<Script::CArray**>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4,
         [fake](Regs& r) { return fake(r)->flags.count(r.args[0]) ? 1u : 0u; });
}

// A goal as a script structure, with WriteIntoStructure's keys (0x004EB260 / 0x004E9E00 mode 0 / 0x004E9BC0).
FakeStruct* AsStructure(FakeArrays* store, const Goals::Goal& g, uint32_t level, int index, const std::vector<int>& gaps)
{
    FakeStruct* s = store->New();
    s->words[0x651533ECu] = level;                                         // level
    s->words[0x60CE75AEu] = static_cast<uint32_t>(index);                  // level_goal_index
    s->words[0x6D11ED74u] = Goals::TypeChecksum(g.type);                   // Goal_Type
    s->words[0xCD66C8AEu] = static_cast<uint32_t>(g.score);                // score
    s->words[0xC51BF6E3u] = static_cast<uint32_t>(g.time);                 // time_limit
    s->words[0x81CFF663u] = g.walk ? 0x726E85AAu : 0x54166ACDu;            // control_type
    s->strings[0x4BC5229Du] = g.name;                                      // view_goals_text
    s->strings[0x0C5D7E6Bu] = g.text;                                      // goal_description
    s->strings[0x5DE57284u] = g.win;                                       // win_message
    s->strings[0xB9426608u] = g.ped;                                       // ped_name
    std::vector<uint32_t> points;
    for (const Goals::Point& p : g.points) {
        FakeStruct* f = store->New();
        f->vectors[0x7F261953u] = p.pos;                       // Pos
        f->floats[0x0AB21AF0u] = p.height;                     // height
        f->floats[0xFF7EBAF6u] = Goals::ImageAngle(p.heading); // angle
        points.push_back(reinterpret_cast<uint32_t>(f));
    }
    s->arrays[0x32DF12E0u] = store->Make(points, Script::ESYMBOLTYPE_STRUCTURE); // node_positions
    if (g.type == Type::Race) {
        s->words[0x1DAAE95Bu] = static_cast<uint32_t>(g.laps);           // race_total_laps
        s->words[0xD9426372u] = static_cast<uint32_t>(g.checkpointTime); // race_per_checkpoint_time
    }
    if (g.type == Type::Gap) {
        s->strings[0x730844A3u] = g.trick; // required_trick_name
        std::vector<uint32_t> numbers(gaps.begin(), gaps.end());
        s->arrays[0xD76C173Eu] = store->Make(numbers, Script::ESYMBOLTYPE_INTEGER); // gaps
    }
    if (Goals::TrickType(g.type)) {
        s->words[0x01181B29u] = static_cast<uint32_t>(g.accelInterval); // acceleration_interval
        s->floats[0x076F65C8u] = g.accelPercent;                         // acceleration_percent
        s->words[0xDED8540Au] = static_cast<uint32_t>(g.trickTime);     // trick_time
        s->words[0xA6D96603u] = static_cast<uint32_t>(g.stopAt);        // time_to_stop_adding_tricks
        s->words[0x89473DB7u] = static_cast<uint32_t>(g.maxTricks);     // max_tricks
        s->words[0xEDF5DB70u] = static_cast<uint32_t>(g.spin);          // spin
        s->words[0x67DD90CAu] = static_cast<uint32_t>(g.comboSize);     // combo_size
        if (g.singleCombo)
            s->flags.insert(0xDF47B144u); // single_combo
        s->words[0x45A375C9u] = static_cast<uint32_t>(g.blockSize); // tricktris_block_size
        s->words[0xBFD3A831u] = static_cast<uint32_t>(g.toWin);     // tricktris_total_to_win
        std::vector<uint32_t> combos;
        for (int i = 0; i < Goals::ComboSetCount(g.type); ++i) {
            if (!(g.sets & (1u << i)))
                continue;
            FakeStruct* c = store->New();
            c->words[0xEB7D6498u] = static_cast<uint32_t>(i); // set_index
            c->words[0xEDF5DB70u] = 0;                        // spin
            c->words[0xA4BEE6A1u] = 0;                        // num_taps
            combos.push_back(reinterpret_cast<uint32_t>(c));
        }
        s->arrays[0x0490F171u] = store->Make(combos, Script::ESYMBOLTYPE_STRUCTURE); // combo_sets
    }
    return s;
}

Goals::Goal Sample(Type type)
{
    Goals::Goal g = Goals::Defaults(type, "ed_goal03");
    g.name = "My goal";
    g.ped = "Kenzo";
    g.text = "Get the letters - don't bail";
    g.win = "Nice one!";
    g.time = 95;
    g.walk = type == Type::HighCombo;
    for (int i = 0; i < Goals::MaxPoints(type); ++i) {
        float f = static_cast<float>(i);
        g.points.push_back({{100.5f + 30.0f * f, -12.25f + f, 2000.0f - 7.0f * f}, 0.3f * f - 1.0f,
                            Goals::DefaultHeight(type, i) + (i == 3 ? 5.0f : 0.0f)});
    }
    // (each type's own settings - a record keeps only those)
    if (type == Type::Race) {
        g.laps = 3;
        g.checkpointTime = 7;
    }
    if (type == Type::Gap) {
        g.trick = "Kickflip";
        g.gaps = {"Over the Wall", "Rail 2"};
    }
    if (Goals::TrickType(type)) {
        g.sets = type == Type::TrickBeat ? 0x9u : 0xA5u;
        g.spin = type == Type::TrickBeat ? 0 : 540;
        g.accelInterval = 4;
        g.accelPercent = 0.25f;
        g.trickTime = 2500;
        g.stopAt = 60;
        g.maxTricks = 9;
        g.singleCombo = type == Type::ComboSkatetris;
        g.comboSize = 4;
        g.blockSize = 5;
        g.toWin = 12;
    }
    return g;
}

void TestEditorGoalsImage()
{
    TestStandIns::StandIns& s = *new TestStandIns::StandIns();
    StandInGetters(s);
    s.Bytes(0x004E67A0, {0x31, 0xC0, 0xC3}); // (no type structures offline: xor eax, eax; ret)
    FakeArrays store;
    bool all = true;
    std::string failed;
    for (int t = 0; t < Goals::kTypes; ++t) {
        Type type = static_cast<Type>(t);
        for (int variant = 0; variant < 2; ++variant) {
            Goals::Goal g = Sample(type);
            if (type == Type::Race && variant == 0)
                g.points.resize(6); // (four checkpoints of the most ten)
            if (variant == 1) {
                // (the other control, a single combo off / on, no sets, empty texts)
                g.walk = !g.walk;
                g.singleCombo = !g.singleCombo;
                g.sets = Goals::TrickType(type) ? 0x2u : 0u;
                g.ped.clear();
                g.win.clear();
            }
            std::vector<int> gaps = variant == 0 ? std::vector<int>{4, 0, 17} : std::vector<int>{9};
            alignas(4) static uint8_t game[Goals::kImageSize];
            std::memset(game, 0, sizeof(game));
            FakeStruct* info = AsStructure(&store, g, 0xE8B4B836u, 22 + variant,
                                           type == Type::Gap ? gaps : std::vector<int>{});
            // ReadFromStructure (0x004E96D0, __thiscall record, structure; RET 4)
            GameFunction<void(__thiscall*)(void*, void*)>(0x004E96D0)(game, info);
            Goals::GoalImage ours;
            Goals::FillImage(g, 0xE8B4B836u, 22 + variant, gaps, &ours);
            if (std::memcmp(game, ours.bytes, Goals::kImageSize) != 0) {
                all = false;
                for (size_t i = 0; i < Goals::kImageSize; ++i) {
                    if (game[i] != ours.bytes[i]) {
                        char at[64];
                        std::snprintf(at, sizeof(at), " %s/%d@+0x%X", Goals::TypeName(type), variant,
                                      static_cast<unsigned>(i));
                        failed += at;
                        break;
                    }
                }
            }
        }
    }
    Check(all, ("editor goals: the record filled here as the game's ReadFromStructure 0x004E96D0 fills it from the "
                "goal's structure, every type" + failed).c_str());
    // A gap goal with no gaps found, a long name cut, the most points of a race.
    Goals::Goal g = Sample(Type::Race);
    Goals::GoalImage image;
    Goals::FillImage(g, 1, 20, {}, &image);
    Check(image.bytes[0x286] == 12 && image.bytes[0x28A] == 1 && image.bytes[0x289] == 0 &&
              *reinterpret_cast<uint32_t*>(image.bytes + 0x28C) == 20,
          "editor goals: a race's twelve points, used, unbeaten, its number");
    g.name = std::string(80, 'x');
    Goals::FillImage(g, 1, 20, {}, &image);
    Check(std::strlen(reinterpret_cast<const char*>(image.bytes + 4)) == 0x31 && image.bytes[0x35] == 0 && image.bytes[0x36] == 'K',
          "editor goals: a name kept within its 50 bytes (+0x4..+0x35)");
}

bool ReadLine(const std::string& line, Goals::Goal* out)
{
    // (the record after the word "goal")
    if (line.compare(0, 5, "goal ") != 0)
        return false;
    return Goals::ReadRecord(line.substr(5), out);
}

void TestEditorGoalsRecords()
{
    bool trips = true;
    for (int t = 0; t < Goals::kTypes; ++t) {
        Goals::Goal g = Sample(static_cast<Type>(t));
        Goals::Goal back;
        std::string text = Goals::WriteRecord(g);
        trips = trips && Goals::ReadRecord(text, &back) && Goals::Same(g, back);
    }
    Check(trips, "editor goals: every type's record read back the same");
    Goals::Goal g;
    Check(ReadLine("goal ed_goal01 type=HighScore name=\"Big one\" p0=1,2,3,0.5,0 p1=4,5,6,1,0", &g) &&
              g.type == Type::HighScore && g.name == "Big one" && g.points.size() == 2 && g.points[1].pos.z == 6.0f &&
              g.points[0].heading == 0.5f && g.time == 120 && g.score == 1000 && !g.walk,
          "editor goals: a short record (defaults, a score clamped to 1000..)");
    Check(ReadLine("goal ed_goal01 type=Gap gaps=\"Over the Wall|Rail 2\" trick=\"Kickflip\" later=1 p0=0,0,0,0,0", &g) &&
              g.gaps.size() == 2 && g.gaps[1] == "Rail 2" && g.trick == "Kickflip",
          "editor goals: gaps and the required trick; an unknown key skipped");
    Goals::Goal bad;
    Check(!ReadLine("goal ed_goal01 name=\"x\" p0=0,0,0,0,0", &bad), "editor goals: no type refused");
    Check(!ReadLine("goal ed_goal01 type=Skate time=1 time=2", &bad), "editor goals: a key twice refused");
    Check(!ReadLine("goal ed_goal01 type=Race p0=0,0,0,0,0 p2=0,0,0,0,0", &bad), "editor goals: a point missing refused");
    Check(!ReadLine("goal ed_goal01 type=HighScore p0=0,0,0,0,0 p1=0,0,0,0,0 p2=0,0,0,0,0", &bad),
          "editor goals: more points than the type has refused");
    Check(!ReadLine("goal ed_goal01 type=Skate time=12x", &bad) && !ReadLine("goal ed_goal01 type=Skate p0=1,2,3", &bad) &&
              !ReadLine("goal ed_goal01 type=Skate name=\"open", &bad) &&
              !ReadLine("goal ed_goal01 type=Skate name=\"a\"b", &bad) &&
              !ReadLine("goal ed_goal01 type=Skate name=\"a\"x=1", &bad) && !ReadLine("goal ed-goal type=Skate", &bad) &&
              !ReadLine("goal ed_goal01 type=Skate control=Fly", &bad) &&
              !ReadLine("goal ed_goal01 type=Skatetris sets=0,9", &bad),
          "editor goals: bad numbers, points, quotes, names, controls and sets refused");
    Check(Goals::CleanText("  a\"b#c|d\te  ", 99) == "abcde" && Goals::CleanText("abcdef", 3) == "abc" &&
              Goals::CleanText("   ", 9).empty(),
          "editor goals: texts cleaned (no quotes, '#', '|', control characters; trimmed; cut)");
    // Limits as Create-a-Goal's menus.
    Check(ReadLine("goal ed_goal01 type=Race time=99999 laps=0 cptime=99 p0=0,0,0,0,0", &g) && g.time == 3600 &&
              g.laps == 1 && g.checkpointTime == 60,
          "editor goals: settings kept within the menus' limits");
    Check(ReadLine("goal ed_goal01 type=Skatetris spin=700 sets=1,3", &g) && g.spin == 540 && g.sets == 0xA &&
              ReadLine("goal ed_goal01 type=TrickBeat sets=1,6", &g) == true && g.sets == 0x2,
          "editor goals: spin in halves, Trick to the Beat's sets the first four");
    // Complete: points, gaps, sets.
    Goals::Goal skate = Sample(Type::Skate);
    std::string why;
    Goals::Goal partial = skate;
    partial.points.resize(6);
    Goals::Goal gap = Sample(Type::Gap);
    gap.gaps.clear();
    Goals::Goal tricks = Sample(Type::TrickTris);
    tricks.sets = 0;
    Check(Goals::Complete(skate) && !Goals::Complete(partial, &why) && !why.empty() && !Goals::Complete(gap) &&
              !Goals::Complete(tricks) && Goals::Complete(Sample(Type::Race)),
          "editor goals: complete only with every point, a gap (Gap), a key combo set (trick goals)");
    Goals::Goal d = Goals::Defaults(Type::HighScore, "ed_goal02");
    Check(d.score == 15000 && Goals::Defaults(Type::HighCombo, "x").score == 10000 &&
              Goals::Defaults(Type::Skate, "x").score == 100 && d.name == "High Score" && d.time == 120 &&
              d.points.empty(),
          "editor goals: Create-a-Goal's defaults (SetType's scores, the menu's names)");
}

void TestEditorGoalsLayout()
{
    Layout::File file;
    file.scene = 0x1234ABCD;
    file.goals = {Sample(Type::Combo), Sample(Type::Race)};
    file.goals[1].id = "ed_goal07";
    std::string text = Layout::Write(file);
    Layout::File back;
    int line = 0;
    bool ok = Layout::Read(text, &back, &line) && back.goals.size() == 2 && Goals::Same(back.goals[0], file.goals[0]) &&
              Goals::Same(back.goals[1], file.goals[1]);
    Check(ok, "editor goals: a layout's goal records read back");
    // A '#' in a text never reaches the file (the layout's comment mark would cut the record).
    Goals::Goal hash = Sample(Type::HighScore);
    hash.name = Goals::CleanText("No #1", 49);
    Layout::File withHash;
    withHash.scene = 1;
    withHash.goals = {hash};
    Layout::File hashBack;
    Check(Layout::Read(Layout::Write(withHash), &hashBack, &line) && hashBack.goals.size() == 1 &&
              hashBack.goals[0].name == "No 1",
          "editor goals: a cleaned name survives the layout");
}

} // namespace

SELFTEST(Early, TestEditorGoalsTypes);
SELFTEST(Early, TestEditorGoalsImage, TestStandIns::RemoveStandIns);
SELFTEST(Early, TestEditorGoalsRecords);
SELFTEST(Early, TestEditorGoalsLayout);
