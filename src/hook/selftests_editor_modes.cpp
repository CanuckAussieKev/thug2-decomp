// Self-tests for the level editor's game-mode rule (src/game/front/level_editor_layout.*, not original - the user's
// rule, 2026-10-21): which game modes offer the editor and edited copies (free skate and single session only - the
// modes as gamemode.qb defines them), and UpdateRecords (0x005AEBC0, src/game/skate/records.cpp): the original's call
// as before, nothing recorded in an edited copy.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/level_editor_layout.h"
#include "game/skate/records.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

namespace Layout = Front::LevelEditor::Layout;

// A mode's flags as gamemode.qb's mode_<x> gives them (is_freeskate, is_singlesession, is_career, is_classic,
// is_creategoals, is_parkeditor, is_net).
Layout::ModeFlags Mode(bool freeskate, bool singleSession, bool career, bool classic, bool createGoals,
                       bool parkEditor, bool net)
{
    Layout::ModeFlags m;
    m.known = true;
    m.freeskate = freeskate;
    m.singleSession = singleSession;
    m.career = career;
    m.classic = classic;
    m.createGoals = createGoals;
    m.parkEditor = parkEditor;
    m.net = net;
    return m;
}

bool Offered(const Layout::ModeFlags& m) { return Layout::ModeRefusal(m) == nullptr; }
bool RefusedAs(const Layout::ModeFlags& m, const char* why)
{
    const char* r = Layout::ModeRefusal(m);
    return r && std::strcmp(r, why) == 0;
}

void TestEditorModes()
{
    //                         free   single career classic goals  park   net
    auto freeskate = Mode(true, false, false, false, false, false, false);     // mode_freeskate
    auto freeskate2p = Mode(true, false, false, false, false, false, false);   // mode_freeskate2p
    auto single = Mode(false, true, false, false, false, false, false);        // mode_singlesession
    auto story = Mode(false, false, true, false, false, false, false);         // mode_career
    auto classic = Mode(false, false, false, true, false, false, false);       // mode_classic
    auto createGoals = Mode(false, false, false, false, true, false, false);   // mode_creategoals
    auto parkEditor = Mode(false, false, false, false, true, true, false);     // mode_parkeditor
    auto netLobby = Mode(false, false, false, false, false, false, true);      // mode_netlobby
    auto trickAttack = Mode(false, false, false, false, false, false, false);  // mode_trickattack (2 player)
    auto horse = Mode(false, false, false, false, false, false, false);        // mode_horse
    Check(Offered(freeskate) && Offered(freeskate2p), "editor modes: free skate (1 and 2 players) offers the editor");
    Check(Offered(single), "editor modes: single session (HIGH SCORE / FREE SKATE) offers the editor");
    Check(RefusedAs(story, "story mode"), "editor modes: story mode refuses it");
    Check(RefusedAs(classic, "classic mode"), "editor modes: classic mode refuses it");
    Check(RefusedAs(createGoals, "create-a-goal"), "editor modes: create-a-goal refuses it");
    Check(RefusedAs(parkEditor, "the park editor"), "editor modes: the park editor refuses it");
    Check(RefusedAs(netLobby, "a network game mode"), "editor modes: the network modes refuse it");
    Check(RefusedAs(trickAttack, "a competitive game mode") && RefusedAs(horse, "a competitive game mode"),
          "editor modes: a mode not on the list (trick attack, horse) refuses it");
    Layout::ModeFlags none;
    Check(RefusedAs(none, "no game mode"), "editor modes: no game mode refuses it");
    // A career-type flag wins over a free skate one (a mode a mod might make).
    Check(RefusedAs(Mode(true, false, true, false, false, false, false), "story mode") &&
              RefusedAs(Mode(false, true, false, true, false, false, false), "classic mode") &&
              RefusedAs(Mode(true, false, false, false, false, false, true), "a network game mode"),
          "editor modes: a career-type or network flag refuses it even with free skate / single session");
}
SELFTEST(Early, TestEditorModes);

// --- UpdateRecords --------------------------------------------------------------------------------------------------

void TestEditorModesUpdateRecords()
{
    using namespace TestStandIns;
    static uint8_t skate[0x400];
    uint8_t* savedSkate = GameVariable<uint8_t*>(0x007CE478);
    std::vector<uint32_t> calls;
    auto run = [&](bool original, bool edited) {
        calls.clear();
        std::memset(skate, 0, sizeof(skate));
        skate[0x384] = 1; // (a new record from an earlier run)
        GameVariable<uint8_t*>(0x007CE478) = skate;
        bool result;
        {
            StandIns s;
            s.At({0x00566F40, AddressOf(&Mdl::Skate::UpdateRecords)}, 0, [&](Regs& r) {
                calls.push_back(r.ecx);
                reinterpret_cast<uint8_t*>(r.ecx)[0x384] = 7; // (the records' answer)
                return 0u;
            });
            result = original ? Original<bool(__cdecl*)(void*, void*)>(0x005AEBC0)(nullptr, nullptr)
                              : Records::UpdateRecords(edited);
        }
        std::vector<uint32_t> trace = calls;
        trace.push_back(result ? 1u : 0u);
        trace.push_back(skate[0x384]);
        return trace;
    };
    std::vector<uint32_t> expected = run(true, false);
    std::vector<uint32_t> actual = run(false, false);
    std::vector<uint32_t> edited = run(false, true);
    GameVariable<uint8_t*>(0x007CE478) = savedSkate;
    Check(expected == actual && expected.size() == 3 && expected[0] == reinterpret_cast<uint32_t>(skate),
          "editor modes: UpdateRecords as the original (the skate module's 0x00566F40 called, true)");
    Check(edited.size() == 2 && edited[0] == 1u && edited[1] == 0u,
          "editor modes: UpdateRecords in an edited copy records nothing (no call, no new record, true)");
}
SELFTEST(Early, TestEditorModesUpdateRecords);

} // namespace
