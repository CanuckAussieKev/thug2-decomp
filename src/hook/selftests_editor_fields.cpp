// Self-tests for the level editor's full Inspector and level properties (src/game/front/level_editor_fields.* and
// level_editor_level_props.*, not original - phase 6): typed values parsed, formatted and written to / read from layout
// records (every kind, escaping, unknown names), the field edit tokens, when a field's change shows, links checked,
// a node's own component put back in its place, the layout's `set` records with fields and its `level` record, and the
// level properties' record and level structure values.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_level_props.h"
#include "game/script/struct.h"
#include "hook/selftest.h"

namespace {

namespace Fields = Front::LevelEditor::Fields;
namespace LevelProps = Front::LevelEditor::LevelProps;
namespace Layout = Front::LevelEditor::Layout;
namespace Nodes = Front::LevelEditor::Nodes;
using Fields::Kind;
using Fields::Value;

// Names for the tests (the game's table isn't up offline).
const char* TestNames(uint32_t checksum)
{
    static const char* const kNames[] = {"TriggerScript", "TRG_Restart01", "TRG_Restart02", "Player1", "Multiplayer",
                                         "CreatedAtStart", "Radius", "TRG_Ped01"};
    for (const char* name : kNames) {
        if (Fields::NameChecksum(name) == checksum)
            return name;
    }
    return nullptr;
}

Value Parsed(Kind kind, const char* text, bool* ok)
{
    Value v;
    std::string error;
    *ok = Fields::Parse(kind, text, &v, &error);
    return v;
}

// --- typed values ---------------------------------------------------------------------------------------------------

void TestEditorFieldsParse()
{
    bool ok = false;
    Value v = Parsed(Kind::Integer, " -42 ", &ok);
    Check(ok && v.integer == -42, "editor fields: an integer (spaces around)");
    Parsed(Kind::Integer, "4.5", &ok);
    bool bad = !ok;
    Parsed(Kind::Integer, "3000000000", &ok);
    bad = bad && !ok;
    Parsed(Kind::Integer, "", &ok);
    Check(bad && !ok, "editor fields: not an integer - a fraction, out of range, empty");
    v = Parsed(Kind::Float, "1.5e2", &ok);
    Check(ok && v.numbers[0] == 150.0f, "editor fields: a float");
    Parsed(Kind::Float, "inf", &ok);
    bad = !ok;
    Parsed(Kind::Float, "1.2.3", &ok);
    Check(bad && !ok, "editor fields: not a float - infinite, malformed");
    v = Parsed(Kind::Vector, "1, -2.5 3", &ok);
    Check(ok && v.numbers[0] == 1.0f && v.numbers[1] == -2.5f && v.numbers[2] == 3.0f,
          "editor fields: a vector (commas and spaces)");
    Parsed(Kind::Vector, "1, 2", &ok);
    bad = !ok;
    Parsed(Kind::Pair, "1, 2, 3", &ok);
    bad = bad && !ok;
    v = Parsed(Kind::Pair, "4,5", &ok);
    Check(bad && ok && v.numbers[0] == 4.0f && v.numbers[1] == 5.0f, "editor fields: pairs and vectors count");
    v = Parsed(Kind::Name, "TRG_Restart01", &ok);
    bool named = ok && v.checksum == Fields::NameChecksum("trg_restart01");
    v = Parsed(Kind::Name, "#1A2B3C4D", &ok);
    named = named && ok && v.checksum == 0x1A2B3C4D;
    v = Parsed(Kind::Name, "0x1a2b3c4d", &ok);
    named = named && ok && v.checksum == 0x1A2B3C4D;
    Parsed(Kind::Name, "2fast", &ok);
    bool notNamed = !ok;
    Parsed(Kind::Name, "a b", &ok);
    notNamed = notNamed && !ok;
    Parsed(Kind::Name, "#12345", &ok);
    Check(named && notNamed && !ok, "editor fields: names - identifiers (case-free checksums), #hex / 0xhex; not others");
    v = Parsed(Kind::Flag, "On", &ok);
    bool flags = ok && v.on;
    v = Parsed(Kind::Flag, "0", &ok);
    flags = flags && ok && !v.on;
    Parsed(Kind::Flag, "maybe", &ok);
    Check(flags && !ok, "editor fields: flags on / off");
    v = Parsed(Kind::Names, "Player1, Multiplayer", &ok);
    bool lists = ok && v.list.size() == 2 && v.list[1] == Fields::NameChecksum("Multiplayer");
    v = Parsed(Kind::Names, "", &ok);
    lists = lists && ok && v.list.empty();
    v = Parsed(Kind::Integers, "1 -2 3", &ok);
    lists = lists && ok && v.list.size() == 3 && static_cast<int32_t>(v.list[1]) == -2;
    v = Parsed(Kind::Floats, "0.5,1", &ok);
    lists = lists && ok && v.floats.size() == 2 && v.floats[0] == 0.5f;
    Parsed(Kind::Names, "Player1, 9lives", &ok);
    Check(lists && !ok, "editor fields: arrays of names / integers / floats (empty too); a bad element refuses");
    std::string error;
    Check(!Fields::Parse(Kind::Other, "x", &v, &error) && !error.empty(), "editor fields: other values aren't typed");

    // Formatted as typed back (floats as short as they read back).
    Value f;
    f.kind = Kind::Float;
    f.numbers[0] = 0.1f;
    Value names;
    names.kind = Kind::Names;
    names.list = {Fields::NameChecksum("Player1"), 0xDEADBEEF};
    Check(Fields::Format(f, TestNames) == "0.1" && Fields::Format(names, TestNames) == "Player1, #DEADBEEF",
          "editor fields: formatted - short floats, names or #hex");
    bool back = Fields::Parse(Kind::Names, Fields::Format(names, TestNames), &v, &error) && v.list == names.list;
    Check(back, "editor fields: a formatted value parses back to itself");
}
SELFTEST(Early, TestEditorFieldsParse);

void TestEditorFieldsRecords()
{
    // Every kind written as a record's value and read back the same.
    std::vector<Value> values;
    Value v;
    v.kind = Kind::Integer;
    v.integer = -7;
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Float;
    v.numbers[0] = 3.14159274f;
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Vector;
    v.numbers[0] = 1.0f / 3.0f;
    v.numbers[1] = -1e-7f;
    v.numbers[2] = 12345.678f;
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Pair;
    v.numbers[0] = 0.5f;
    v.numbers[1] = 2.0f;
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Name;
    v.checksum = Fields::NameChecksum("TriggerScript");
    values.push_back(v);
    v.checksum = 0x0BADF00D; // (unknown: 0x form)
    values.push_back(v);
    v = Value{};
    v.kind = Kind::String;
    v.text = "Multi: Restart #2 = 100% a,b";
    values.push_back(v);
    v.kind = Kind::LocalString;
    v.text = "";
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Flag;
    v.on = false;
    values.push_back(v);
    v.on = true;
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Names;
    v.list = {Fields::NameChecksum("Player1"), 0x12345678};
    values.push_back(v);
    v.list.clear();
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Integers;
    v.list = {1, static_cast<uint32_t>(-5)};
    values.push_back(v);
    v = Value{};
    v.kind = Kind::Floats;
    v.floats = {0.25f, -8.0f};
    values.push_back(v);
    bool all = true;
    for (const Value& value : values) {
        std::string text = Fields::EncodeValue(value, TestNames);
        Value back;
        bool plain = text.find(' ') == std::string::npos && text.find('#') == std::string::npos &&
                     text.find('=') == std::string::npos;
        all = all && plain && Fields::DecodeValue(text, &back) && Fields::SameValue(back, value);
    }
    Check(all, "editor fields: every kind written to a record and read back the same (no spaces, '#' or '=')");
    Value back;
    Check(!Fields::DecodeValue("x:1", &back) && !Fields::DecodeValue("i:1.5", &back) &&
              !Fields::DecodeValue("flag:2", &back) && !Fields::DecodeValue("s:%4", &back) &&
              !Fields::DecodeValue("s:%00", &back) && !Fields::DecodeValue("12", &back) &&
              !Fields::DecodeValue("flag:on", &back),
          "editor fields: malformed record values refused (a record's flag is 0 or 1)");
    // A name table entry whose checksum isn't that name's (a collision, a stale entry): written as 0x hex.
    auto wrong = [](uint32_t c) -> const char* { return c == 0xDEADBEEF ? "Player1" : nullptr; };
    Check(Fields::RecordName(0xDEADBEEF, wrong) == "0xDEADBEEF" &&
              Fields::RecordName(Fields::NameChecksum("Player1"), TestNames) == "Player1",
          "editor fields: records write a name only when it is that checksum's");

    // The f.<field>= tokens: a name when known, else 0x hex; del for a field taken out.
    Fields::Edit edit;
    edit.key = Fields::NameChecksum("Radius");
    edit.value.kind = Kind::Float;
    edit.value.numbers[0] = 250.0f;
    std::string token = Fields::Token(edit, TestNames);
    Fields::Edit removed;
    removed.key = 0xCAFE0001;
    removed.removed = true;
    std::string gone = Fields::Token(removed, TestNames);
    Fields::Edit e1, e2;
    size_t eq1 = token.find('='), eq2 = gone.find('=');
    bool read = Fields::ReadToken(token.substr(0, eq1), token.substr(eq1 + 1), &e1) &&
                Fields::ReadToken(gone.substr(0, eq2), gone.substr(eq2 + 1), &e2);
    Check(token == "f.Radius=f:250" && gone == "f.0xCAFE0001=del" && read && e1.key == edit.key &&
              Fields::SameValue(e1.value, edit.value) && e2.removed && e2.key == removed.key,
          "editor fields: field tokens (f.Radius=f:250, f.0xCAFE0001=del) and read back");
    Check(!Fields::IsFieldKey("pos") && !Fields::IsFieldKey("f.") && Fields::IsFieldKey("f.x") &&
              !Fields::ReadToken("f.9x", "i:1", &e1) && !Fields::ReadToken("f.Radius", "", &e1),
          "editor fields: only f.<name> keys with a value are field edits");
    std::string out;
    Check(Fields::Escape("a b#c=%") == "a%20b%23c%3D%25" && Fields::Unescape("a%20b%23c%3D%25", &out) && out == "a b#c=%",
          "editor fields: %-escaping");
}
SELFTEST(Early, TestEditorFieldsRecords);

void TestEditorFieldsRules()
{
    const char* note = nullptr;
    uint32_t radius = Fields::NameChecksum("Radius");
    Check(Fields::WhenApplied(Nodes::Family::Trigger, radius, &note) == Fields::When::Reload && note && *note,
          "editor fields: an ordinary field shows on reload");
    Check(Fields::WhenApplied(Nodes::Family::Spawn, Fields::KeyRestartTypes(), nullptr) == Fields::When::Live &&
              Fields::WhenApplied(Nodes::Family::Other, Fields::KeyRestartTypes(), nullptr) == Fields::When::Reload,
          "editor fields: a restart's restart_types are live (read at every respawn)");
    Check(Fields::WhenApplied(Nodes::Family::Path, Fields::KeyLinks(), nullptr) == Fields::When::Live,
          "editor fields: links are live (through the chains)");
    Check(Fields::WhenApplied(Nodes::Family::Spawn, Fields::KeyPos(), nullptr) == Fields::When::Elsewhere &&
              Fields::WhenApplied(Nodes::Family::Spawn, Fields::KeyAngles(), nullptr) == Fields::When::Elsewhere &&
              Fields::WhenApplied(Nodes::Family::Rail, Fields::KeyTerrain(), nullptr) == Fields::When::Elsewhere &&
              Fields::WhenApplied(Nodes::Family::Object, Fields::KeyTerrain(), nullptr) == Fields::When::Reload,
          "editor fields: pos / angles and a rail's terrain are edited elsewhere");
    Check(Fields::WhenApplied(Nodes::Family::Light, Fields::KeyName(), nullptr) == Fields::When::ReadOnly &&
              Fields::WhenApplied(Nodes::Family::Light, Fields::KeyClass(), nullptr) == Fields::When::ReadOnly &&
              Fields::WhenApplied(Nodes::Family::Light, Fields::KeyType(), nullptr) == Fields::When::ReadOnly,
          "editor fields: name, Class and Type are read-only");
    // Links: existing nodes only, not itself, none twice.
    uint32_t a = Fields::NameChecksum("TRG_Restart01"), b = Fields::NameChecksum("TRG_Restart02"), c = 0x99;
    auto exists = [&](uint32_t n) { return n == a || n == b; };
    std::string error;
    bool good = Fields::ValidLinks({a, b}, c, exists, TestNames, &error) && error.empty() &&
                Fields::ValidLinks({}, c, exists, TestNames, &error);
    bool missing = !Fields::ValidLinks({a, 0x1234}, c, exists, TestNames, &error) &&
                   error.find("#00001234") != std::string::npos;
    bool self = !Fields::ValidLinks({a}, a, exists, TestNames, &error);
    bool twice = !Fields::ValidLinks({a, b, a}, c, exists, TestNames, &error) &&
                 error.find("TRG_Restart01") != std::string::npos;
    Check(good && missing && self && twice, "editor fields: links to existing nodes only, not itself, none twice");
}
SELFTEST(Early, TestEditorFieldsRules);

// A node's own component put back where it was (the list relinked: no allocation) - components on the stack.
void TestEditorFieldsPlace()
{
    Script::CComponent c[4] = {};
    Script::CStruct s;
    std::memset(&s, 0, sizeof(s));
    for (int i = 0; i < 4; ++i) {
        c[i].typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_INTEGER << 1);
        c[i].name = 0x100u + static_cast<uint32_t>(i);
        c[i].integer = i;
        c[i].next = i < 3 ? &c[i + 1] : nullptr;
    }
    s.m_head = &c[0];
    // (c[3] stands for a component just added at the end: moved to after 1 of the others)
    Fields::MoveLastTo(&s, 1);
    bool order = s.m_head == &c[0] && c[0].next == &c[3] && c[3].next == &c[1] && c[1].next == &c[2] && !c[2].next;
    Fields::Own own = Fields::OwnIn(&s, 0x103, false);
    bool read = own.present && own.index == 1 && own.value.kind == Kind::Integer && own.value.integer == 3;
    // To the front, and an index past the end leaves it last.
    Fields::MoveLastTo(&s, 0); // (c[2] is last: to the front)
    bool front = s.m_head == &c[2] && c[2].next == &c[0] && c[1].next == nullptr;
    Fields::MoveLastTo(&s, 9);
    bool stays = c[1].next == nullptr && Fields::OwnIn(&s, 0x101, false).index == 3;
    Fields::Own none = Fields::OwnIn(&s, 0x999, false);
    Check(order && read && front && stays && !none.present, "editor fields: a node's own component back in its place");
}
SELFTEST(Early, TestEditorFieldsPlace);

// --- layouts and level properties -----------------------------------------------------------------------------------

void TestEditorFieldsLayout()
{
    Layout::File file;
    file.scene = 0x072538EB;
    Layout::SetRecord moved = {};
    moved.name = 0x11111111;
    moved.hasPos = true;
    moved.pos[0] = 1.0f;
    moved.pos[1] = 2.0f;
    moved.pos[2] = 3.0f;
    Fields::Edit e;
    e.key = 0x22222222;
    e.value.kind = Kind::String;
    e.value.text = "Team: CTF red";
    moved.fields.push_back(e);
    file.nodes.push_back(moved);
    Layout::SetRecord onlyFields = {};
    onlyFields.name = 0x33333333;
    Fields::Edit flag;
    flag.key = 0x44444444;
    flag.value.kind = Kind::Flag;
    flag.value.on = false;
    Fields::Edit gone;
    gone.key = 0x55555555;
    gone.removed = true;
    onlyFields.fields = {flag, gone};
    file.nodes.push_back(onlyFields);
    file.level.name = "Training (night)";
    file.level.start = 0x66666666;
    file.level.tod = 3;
    file.level.time = 0;
    file.level.hasFog = true;
    int fog[5] = {10, 20, 30, 40, 1500};
    std::memcpy(file.level.fog, fog, sizeof(fog));
    std::string text = Layout::Write(file);
    Layout::File back;
    int line = 0;
    bool read = Layout::Read(text, &back, &line);
    bool nodes = read && back.nodes.size() == 2 && back.nodes[0].hasPos && !back.nodes[0].hasAngles &&
                 back.nodes[0].fields.size() == 1 && Fields::SameValue(back.nodes[0].fields[0].value, e.value) &&
                 back.nodes[1].name == 0x33333333 && !back.nodes[1].hasPos && back.nodes[1].fields.size() == 2 &&
                 back.nodes[1].fields[0].value.kind == Kind::Flag && !back.nodes[1].fields[0].value.on &&
                 back.nodes[1].fields[1].removed;
    Check(nodes, "editor fields: set records carry field edits (a node not moved gets a record of its own)");
    Check(read && LevelProps::Same(back.level, file.level), "editor fields: the level record read back the same");

    // An older layout (phase 2's set records) still reads; a field twice isn't a record we wrote; a level record of a
    // later version's key is read (the key skipped).
    const char* older = "thug2-decomp layout 2\nscene 072538EB\nset 11111111 pos=1,2,3 angles=0,1,0  # TRG_Restart01\n";
    bool old = Layout::Read(older, &back, &line) && back.nodes.size() == 1 && back.nodes[0].fields.empty();
    const char* twice = "thug2-decomp layout 2\nscene 072538EB\nset 11111111 f.Radius=f:1 f.Radius=f:2\n";
    const char* later = "thug2-decomp layout 2\nscene 072538EB\nlevel tod=evening weather=rain\n";
    bool laterOk = Layout::Read(later, &back, &line) && back.level.tod == 2;
    Check(old && !Layout::Read(twice, &back, &line) && laterOk,
          "editor fields: older set records read; a repeated field refused; unknown level keys skipped");
}
SELFTEST(Early, TestEditorFieldsLayout);

void TestEditorLevelProps()
{
    LevelProps::Props empty;
    Check(LevelProps::Empty(empty) && LevelProps::WriteRecord(empty).empty(), "level props: none - no record");
    LevelProps::Props p;
    p.name = "My Park";
    p.tod = 0;
    p.hasAmbient = true;
    p.ambient[0] = 54;
    p.ambient[1] = 63;
    p.ambient[2] = 64;
    std::string line = LevelProps::WriteRecord(p);
    std::istringstream in(line.substr(6));
    LevelProps::Props back;
    bool read = line.rfind("level ", 0) == 0 && LevelProps::ReadRecord(in, &back) && LevelProps::Same(back, p);
    Check(line == "level name=My%20Park tod=morning ambient=54,63,64\n" && read, "level props: the record and back");
    auto bad = [](const char* text) {
        std::istringstream s(text);
        LevelProps::Props x;
        return !LevelProps::ReadRecord(s, &x);
    };
    Check(bad("tod=noon") && bad("time=2") && bad("ambient=1,2") && bad("fog=1,2,3,4") && bad("start=12") &&
              bad("name=") && bad("loose"),
          "level props: malformed records refused");
    int tod = -1;
    Check(LevelProps::TodFromName("Evening", &tod) && tod == 2 && !LevelProps::TodFromName("dusk", &tod) &&
              LevelProps::TodName(3) && std::strcmp(LevelProps::TodName(3), "night") == 0 && !LevelProps::TodName(4),
          "level props: the time-of-day states");
    // The level structure's values: the keys load_level reads, clamped.
    LevelProps::Props s;
    s.tod = 2;
    s.time = 1;
    s.hasFog = true;
    int fog[5] = {300, -4, 128, 50, 999999};
    std::memcpy(s.fog, fog, sizeof(fog));
    auto values = LevelProps::StructureValues(s);
    bool keys = values.size() == 7 && std::strcmp(values[0].first, "tod_state") == 0 &&
                values[0].second.checksum == Fields::NameChecksum("evening") &&
                std::strcmp(values[1].first, "tod_manager_active") == 0 && values[1].second.integer == 1 &&
                std::strcmp(values[2].first, "fog_red") == 0 && values[2].second.integer == 255 &&
                values[3].second.integer == 0 && std::strcmp(values[6].first, "fog_dist") == 0 &&
                values[6].second.integer == 100000;
    Check(keys && LevelProps::StructureValues(empty).empty(), "level props: the level structure's keys and values");
}
SELFTEST(Early, TestEditorLevelProps);

} // namespace
