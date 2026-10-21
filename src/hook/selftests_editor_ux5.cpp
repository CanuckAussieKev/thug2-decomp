// Self-tests for the level editor's problem checker, budget meter and note pins (UX5 - src/game/front/
// level_editor_checks.*, _budget.*, _notes.*, not original): the placement probes' verdicts, floating rail points, rails
// of one point, the multiplayer needs, the carried layout records, the budget's levels and texts, and the note record.

#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor_budget.h"
#include "game/front/level_editor_checks.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_notes.h"
#include "hook/selftest.h"

namespace {

namespace Checks = Front::LevelEditor::Checks;
namespace Budget = Front::LevelEditor::Budget;
namespace Notes = Front::LevelEditor::Notes;
namespace Layout = Front::LevelEditor::Layout;

Checks::Probe P(float out, float in) { return {out, in}; }

void TestEditorChecksPlacement()
{
    // A probe is behind a surface when a face facing away is nearer than any face facing the point.
    Check(Checks::Behind(P(-1, 5)) && Checks::Behind(P(20, 5)) && !Checks::Behind(P(5, 20)) && !Checks::Behind(P(-1, -1)) &&
              !Checks::Behind(P(5, -1)) && !Checks::Behind(P(5, 5)),
          "editor checks: behind a surface (a face facing away nearer than one facing the point)");
    // Inside a closed box: behind both ways along an axis; under a floor: behind upwards only; standing in a room
    // (floor, ceiling, walls all facing in): clear; under a bridge (its underside faces the point first): clear.
    Checks::Probe box[6] = {P(-1, 10), P(-1, 10), P(-1, 10), P(-1, 10), P(-1, 10), P(-1, 10)};
    Checks::Probe floor[6] = {P(-1, -1), P(-1, -1), P(-1, 30), P(-1, -1), P(-1, -1), P(-1, -1)};
    Checks::Probe floorBelow[6] = {P(-1, -1), P(-1, -1), P(-1, 30), P(400, -1), P(-1, -1), P(-1, -1)};
    Checks::Probe room[6] = {P(100, 900), P(100, 900), P(200, 400), P(12, -1), P(100, 900), P(100, 900)};
    Checks::Probe bridge[6] = {P(-1, -1), P(-1, -1), P(300, 340), P(12, -1), P(-1, -1), P(-1, -1)};
    // (a long thin wall: closed in across it, open along it)
    Checks::Probe wall[6] = {P(-1, 8), P(-1, 8), P(-1, -1), P(12, -1), P(-1, -1), P(-1, -1)};
    Check(Checks::Judge(box) == Checks::Place::InsideSolid && Checks::Judge(floor) == Checks::Place::BelowGround &&
              Checks::Judge(floorBelow) == Checks::Place::BelowGround && Checks::Judge(room) == Checks::Place::Clear &&
              Checks::Judge(bridge) == Checks::Place::Clear && Checks::Judge(wall) == Checks::Place::InsideSolid,
          "editor checks: inside solid (closed in along an axis), below the ground (behind upwards), clear otherwise");
    // Floating rail points: nothing met within the reach either way.
    Checks::Probe none[6] = {}, near[6] = {}, inNear[6] = {}, far[6] = {};
    near[3] = P(2, -1);
    inNear[0] = P(-1, 25);
    far[3] = P(31, 40);
    Check(Checks::Floating(none, 30.0f) && !Checks::Floating(near, 30.0f) && !Checks::Floating(inNear, 30.0f) &&
              Checks::Floating(far, 30.0f),
          "editor checks: a rail point floats when no face is within reach either way");
}
SELFTEST(Early, TestEditorChecksPlacement);

void TestEditorChecksRailsAndModes()
{
    // Rails of one point: linked to nothing and by nothing (links out of range ignored).
    std::vector<std::vector<int>> links = {{1}, {2}, {}, {}, {9}, {}};
    links[5] = {4};
    std::vector<int> lonely = Checks::LonelyPoints(links);
    Check(lonely == std::vector<int>{3}, "editor checks: rails of one point (no link either way)");

    // Multiplayer: a complete level has nothing to say.
    Checks::MpCounts full;
    full.player1 = 1;
    full.multiplayer = 4;
    full.horse = 2;
    full.crowns = 5;
    full.teams[0] = {true, true, true};
    full.teams[1] = {true, true, true};
    Check(Checks::MultiplayerProblems(full).empty(), "editor checks: multiplayer - a complete level");
    Checks::MpCounts c = full;
    c.player1 = 0;
    c.multiplayer = 1;
    c.horse = 0;
    c.crowns = 0;
    c.teams[1].base = false;
    c.teams[2].flag = true; // (Green started, not finished)
    std::vector<Checks::MpProblem> p = Checks::MultiplayerProblems(c);
    auto has = [&](Checks::Severity s, const char* text, int team = -1) {
        for (const Checks::MpProblem& m : p) {
            if (m.severity == s && m.team == team && m.text.find(text) != std::string::npos)
                return true;
        }
        return false;
    };
    Check(p.size() == 6 && has(Checks::Severity::Error, "Player1") && has(Checks::Severity::Info, "Only one") &&
              has(Checks::Severity::Warning, "HORSE") && has(Checks::Severity::Warning, "no crowns") &&
              has(Checks::Severity::Warning, "TRG_CTF_Blue_Base", 1) &&
              has(Checks::Severity::Warning, "TRG_CTF_Restart_Green", 2) && !has(Checks::Severity::Warning, "Yellow", 3),
          "editor checks: multiplayer - each mode's missing points (Green half there, Yellow absent: fine)");
    Checks::MpCounts noCtf = full;
    for (Checks::TeamNodes& t : noCtf.teams)
        t = {};
    p = Checks::MultiplayerProblems(noCtf);
    Check(p.size() == 1 && p[0].team == -1 && p[0].text.find("Capture the Flag") != std::string::npos,
          "editor checks: multiplayer - no CTF at all is one problem");
}
SELFTEST(Early, TestEditorChecksRailsAndModes);

void TestEditorChecksRecords()
{
    Check(Checks::RecordKey("set 0A0B0C0D pos=1,2,3 # TRG_X") == "set 0A0B0C0D" &&
              Checks::RecordKey("lock sector 12345678") == "lock sector 12345678" &&
              Checks::RecordKey("level start=1 tod=night") == "level" && Checks::RecordKey("  # just a comment").empty(),
          "editor checks: a record's kind and key");
    std::vector<std::string> unresolved = {"set 0A0B0C0D pos=1,2,3", "hide 11111111", "hide 11111111",
                                           "lock object 22222222"};
    std::string captured = "thug2-decomp layout 2\nscene 01020304\nset 0A0B0C0D pos=9,9,9  # moved again\n";
    std::vector<std::string> carried = Checks::CarriedLines(unresolved, captured);
    Check(carried == std::vector<std::string>{"hide 11111111", "lock object 22222222"},
          "editor checks: carried records - not those captured again (same kind and key), once each");
    // Issues: their keys differ by rule and subject; the list sorts errors first, the editor's before the level's own.
    Check(Checks::IssueKey(Checks::Rule::Limit, 1, "a") != Checks::IssueKey(Checks::Rule::Limit, 2, "a") &&
              Checks::IssueKey(Checks::Rule::Limit, 1, "a") != Checks::IssueKey(Checks::Rule::NoGround, 1, "a") &&
              Checks::IssueKey(Checks::Rule::Limit, 1, "a") == Checks::IssueKey(Checks::Rule::Limit, 1, "a"),
          "editor checks: issue keys");
    std::vector<Checks::Issue> issues(4);
    issues[0].severity = Checks::Severity::Info;
    issues[1].severity = Checks::Severity::Error;
    issues[1].levelOwn = true;
    issues[2].severity = Checks::Severity::Warning;
    issues[3].severity = Checks::Severity::Error;
    issues[0].text = "info";
    issues[1].text = "own error";
    issues[2].text = "warning";
    issues[3].text = "error";
    Checks::Sort(&issues);
    Check(issues[0].text == "error" && issues[1].text == "own error" && issues[2].text == "warning" &&
              issues[3].text == "info",
          "editor checks: sorted - errors, warnings, infos; the editor's first");

    // A layout with notes and carried lines: written, read back - the notes as they were, the carried lines records again.
    Layout::File file;
    file.scene = 0x01020304;
    Notes::Note note;
    note.name = "ed_note01";
    note.pos = {1.5f, -2.0f, 300.25f};
    note.colour = 0x5AC8FA;
    note.text = "raise this ledge\n100% \"now\" #1";
    file.notes.push_back(note);
    file.carried = {"hide 11111111", "set 0A0B0C0D pos=1,2,3"};
    std::string text = Layout::Write(file);
    Layout::File back;
    int line = 0;
    bool read = Layout::Read(text, &back, &line);
    Check(read && back.notes.size() == 1 && Notes::Same(back.notes[0], note) && back.hidden.size() == 1 &&
              back.hidden[0] == 0x11111111 && back.nodes.size() == 1 && back.nodes[0].name == 0x0A0B0C0D &&
              back.carried.empty(),
          "editor checks: a layout's notes and carried records written and read back");
}
SELFTEST(Early, TestEditorChecksRecords);

void TestEditorBudget()
{
    using Budget::Level;
    Check(Budget::LevelOf(7, 10) == Level::Ok && Budget::LevelOf(8, 10) == Level::Near &&
              Budget::LevelOf(10, 10) == Level::Near && Budget::LevelOf(11, 10) == Level::Over &&
              Budget::LevelOf(500, 0) == Level::Ok && Budget::Fill(5, 10) == 0.5 && Budget::Fill(5, 0) == 0.0,
          "editor budget: levels (amber from 80 %, red over the limit - at it is full, not over)");
    std::vector<Budget::Meter> meters(3);
    meters[0].used = 9;
    meters[0].limit = 10;
    meters[1].used = 5000;
    meters[1].limit = 0; // (a count only: never the worst)
    meters[2].used = 250;
    meters[2].limit = 256;
    Check(Budget::Worst(meters) == 2 && Budget::Worst({}) == -1, "editor budget: the worst meter (highest fill)");
    Budget::Meter crowns;
    crowns.used = 12;
    crowns.limit = 10;
    crowns.added = 2;
    Budget::Meter memory;
    memory.used = 3.5 * 1024 * 1024;
    memory.limit = 64.0 * 1024 * 1024;
    memory.added = 1536;
    memory.bytes = memory.approximate = true;
    Budget::Meter tiny = memory;
    tiny.added = 200;
    Check(Budget::UsedText(crowns) == "12 / 10" && Budget::AddedText(crowns) == "+2" &&
              Budget::UsedText(memory) == "3.50 MB / 64.00 MB" && Budget::AddedText(memory) == "about +1.5 KB" &&
              Budget::AddedText(tiny).empty() && Budget::Bytes(512) == "512 B",
          "editor budget: the meters' texts");
}
SELFTEST(Early, TestEditorBudget);

void TestEditorNotes()
{
    std::string raw = "a%b \"c\" #d\nline two\t\xC3\xA9";
    std::string encoded = Notes::Encode(raw), decoded;
    Check(encoded == "a%25b %22c%22 %23d%0Aline two%09\xC3\xA9" && Notes::Decode(encoded, &decoded) && decoded == raw,
          "editor notes: the text encoded for the line (% \" # control characters) and back");
    Check(!Notes::Decode("bad%2", &decoded) && !Notes::Decode("bad%zz", &decoded) && !Notes::Decode("a\"b", &decoded),
          "editor notes: bad escapes refused");
    Notes::Note n;
    n.name = "ed_note03";
    n.pos = {10.0f, 20.5f, -30.0f};
    n.colour = 0xFF7A59;
    n.text = "two words";
    std::string record = Notes::WriteRecord(n);
    std::istringstream in(record.substr(5));
    Notes::Note back;
    Check(record == "note ed_note03 pos=10,20.5,-30 color=FF7A59 text=\"two words\"" && Notes::ReadRecord(in, &back) &&
              Notes::Same(back, n),
          "editor notes: the record written and read back");
    auto reads = [](const char* text) {
        std::istringstream s(text);
        Notes::Note note;
        return Notes::ReadRecord(s, &note);
    };
    Check(reads("ed_note01 pos=1,2,3") && reads("ed_note01 pos=1,2,3 later=7 text=\"\"") && !reads("ed_note01") &&
              !reads("ed_note01 pos=1,2") && !reads("ed_note01 pos=1,2,3 pos=1,2,3") &&
              !reads("ed_note01 pos=1,2,3 color=FFF") && !reads("bad-name pos=1,2,3") &&
              !reads("ed_note01 pos=1,2,3 text=\"x\" color=FFFFFF") && !reads("ed_note01 pos=1,2,3 text=\"open") &&
              !reads("ed_note01 pos=1,2,3 text=bare"),
          "editor notes: malformed records refused (no pos, a key twice, a bad colour or name, text= not last / quoted)");
    std::vector<Notes::Note> notes(2);
    notes[0].name = "ed_note01";
    notes[1].name = "ed_note03";
    Check(Notes::FreshName(notes) == "ed_note02" && Notes::FreshName({}) == "ed_note01",
          "editor notes: fresh names (the lowest number free)");
    Check(Notes::Label("first line\nsecond", 40) == "first line ..." && Notes::Label("abcdefgh", 4) == "abcd..." &&
              Notes::Label("", 10) == "(empty note)" && Notes::Label("just one", 40) == "just one",
          "editor notes: the pin's label (the first line, cut short)");
}
SELFTEST(Early, TestEditorNotes);

} // namespace
