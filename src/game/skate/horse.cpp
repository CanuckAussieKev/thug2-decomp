// Mdl::CHorse: the HORSE game, 0x005600D0-0x005608BF (see horse.h).

#include "game/skate/horse.h"

#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/mth/random.h"
#include "game/obj/proxim.h"
#include "game/script/struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "game/skate/score.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace Mdl {

namespace {

constexpr uint32_t kHorse = 0x9D65D0E7;            // Horse (the restarts' type)
constexpr uint32_t kHorsePreRun = 0x5078BC7C;      // horseprerun
constexpr uint32_t kYouAreA = 0x2523853D;          // youarea
constexpr uint32_t kHorsePostRun = 0x2A13F850;     // horsepostrun
constexpr uint32_t kPanelString2 = 0x478970CD;     // panelString2
constexpr uint32_t kFinalWord = 0x5F7D73E9;        // finalword
constexpr uint32_t kPlayerName = 0x9196D920;       // playerName
constexpr uint32_t kPanelString1 = 0xDE802177;     // panelString1
constexpr uint32_t kIsAn = 0x8DF39CE4;             // horse_str_isan
constexpr uint32_t kIsA = 0xFB911171;              // horse_str_isa
constexpr uint32_t kScoreToBeat = 0xA32FE78F;      // horse_str_scoretobeat
constexpr uint32_t kSetScore = 0x695BC447;         // horse_str_setscore
constexpr uint32_t kSetScoreIs = 0x5E024658;       // horse_str_setscoreis
constexpr uint32_t kGetsTheLetter = 0xB470755A;    // horse_str_getstheletter
constexpr uint32_t kNoScoreSet = 0x7C61AA4A;       // horse_str_noscoreset
constexpr uint32_t kYouTied = 0x334622A1;          // horse_str_youtiedthetargetscore


// The statuses (0x0067F340, null-terminated): GotLetter, TieScore, BeatScore, 0xAE7D29B0 (the game over), idle,
// NoScoreSet.
GAME_GLOBAL(uint32_t[7], g_horseStatuses, 0x0067F340, 0x0FD80EBA, 0x006BE86F, 0xBAA12466, 0xAE7D29B0, 0x23DB4AEA, 0xA71DD8EB, 0x00000000); // (zero-terminated)
const uint32_t* Statuses() { return g_horseStatuses; }
enum StatusIndex { kGotLetter = 0, kTieScore = 1, kBeatScore = 2, kGameOver = 3, kIdle = 4, kNoScoreSetStatus = 5 };

int HorseRestarts()
{
    uint32_t restarts[0x80];
    return Obj::GetRestartNodes(kHorse, restarts, 0);
}

Obj::CSkater* Skater(uint32_t id) { return static_cast<Obj::CSkater*>(SkateCalls::GetSkaterById(id)); }

char Upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 0x20) : c; }

// strcpy (no bounds, as the original).
void Copy(char* dest, const char* text)
{
    while ((*dest++ = *text++) != 0) {
    }
}

} // namespace

// 0x005600D0
CHorse* CHorse::Construct()
{
    std::memcpy(m_word, "HORSE", 6);
    m_restart = 0;
    return this;
}

// 0x00560100
void CHorse::Init()
{
    m_scoreToBeat = 0;
    m_skater = 0;
    m_lastSkater = 0;
    std::snprintf(m_panel, sizeof(m_panel), "%s", GAME_TEXT(0x0064E080, "unknown")); // "unknown"
    for (int& letters : m_letters)
        letters = 0;
    m_restart = Mth::Rnd(HorseRestarts());
}

// 0x00560180
void CHorse::StartRun() { m_status = Statuses()[kIdle]; }

// 0x00560190
bool CHorse::StatusEquals(Script::CStruct* params)
{
    for (const uint32_t* status = Statuses(); *status; ++status)
        if (params->ContainsFlag(*status))
            return m_status == *status;
    return false;
}

// 0x005601F0
void CHorse::SetWord(Str::String word)
{
    Copy(m_word, word.c_str());
    Dbg::Printf(GAME_TEXT(0x0064E088, "New word = %s\n"), m_word); // "New word = %s\n"
}

// 0x00560260
Str::String CHorse::GetWordForPlayer(int player)
{
    char word[0x100];
    Copy(word, m_word);
    if (std::strlen(word)) {
        uint32_t letters = static_cast<uint32_t>(m_letters[player]);
        for (uint32_t i = 0; i < std::strlen(word); ++i)
            if (i >= letters)
                word[i] = '-';
    }
    return Str::String(word);
}

// 0x00560310
int CHorse::GetRestart()
{
    HorseRestarts();
    return m_restart;
}

// 0x00560340
void CHorse::SetNextRestartPoint()
{
    ++m_restart;
    if (static_cast<uint32_t>(m_restart) >= static_cast<uint32_t>(HorseRestarts()))
        m_restart = 0;
}

// 0x00560380
bool CHorse::Ended()
{
    for (int i = 0; i < kPlayers; ++i)
        if (Skater(static_cast<uint32_t>(i)) && static_cast<uint32_t>(m_letters[i]) >= std::strlen(m_word))
            return true;
    return false;
}

// 0x005603D0
int CHorse::GetNextValidSkaterId()
{
    int id = 0;
    for (int i = 0; i < kPlayers; ++i) {
        id = (m_skater + i + 1) % kPlayers;
        if (Skater(static_cast<uint32_t>(id)))
            break;
    }
    Dbg::Printf(GAME_TEXT(0x0064E0A4, "new skater id = %d\n"), id); // "new skater id = %d\n"
    if (Script::GetInteger(GAME_TEXT(0x0064E098, "debug_horse"))) // "debug_horse"
        return 0;
    return id;
}

// 0x00560440
Str::String CHorse::GetString(uint32_t which)
{
    char text[0x100];
    std::memcpy(text, GAME_TEXT(0x00648460, "Unknown"), 8); // "Unknown"
    Obj::CSkater* skater = Skater(static_cast<uint32_t>(m_skater));
    switch (which) {
    case kPanelString1:
    case kPanelString2: {
        int player = which == kPanelString1 ? 0 : 1;
        Str::String word = GetWordForPlayer(player);
        std::snprintf(text, sizeof(text), GAME_TEXT(0x0064CBE4, "%s: %s"), Skater(static_cast<uint32_t>(player))->GetDisplayName(),
                      word.c_str()); // "%s: %s"
        break;
    }
    case kHorsePostRun: Copy(text, m_panel); break;
    case kYouAreA: {
        char first = Upper(m_word[0]);
        bool vowel = first == 'A' || first == 'E' || first == 'I' || first == 'O' || first == 'U';
        const char* article = Script::GetString(vowel ? kIsAn : kIsA);
        Str::String name = GetString(kPlayerName);
        std::snprintf(text, sizeof(text), GAME_TEXT(0x0064C914, "%s %s"), name.c_str(), article); // "%s %s"
        break;
    }
    case kHorsePreRun:
        if (m_scoreToBeat > 0) {
            const char* points = Str::FormatWithSeparators(m_scoreToBeat);
            std::snprintf(text, sizeof(text), GAME_TEXT(0x0064C914, "%s %s"), Script::GetString(kScoreToBeat), points); // "%s %s"
        } else
            std::snprintf(text, sizeof(text), Script::GetString(kSetScore));
        break;
    case kFinalWord: std::snprintf(text, sizeof(text), "%s", m_word); break;
    case kPlayerName:
        std::snprintf(text, sizeof(text), "%s",
                      Skater(*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(skater) + 0x14))->GetDisplayName());
        break;
    default: break;
    }
    return Str::String(text);
}

// 0x00560720
void CHorse::EndRun()
{
    std::snprintf(m_panel, sizeof(m_panel), "%s", GAME_TEXT(0x0064E080, "unknown")); // "unknown"
    m_lastSkater = m_skater;
    void* score = Skater(static_cast<uint32_t>(m_skater))->GetScore();
    int total = static_cast<Mdl::Score*>(score)->GetTotalScore();
    Dbg::Printf(GAME_TEXT(0x0064E0C8, "End run = %d\n"), total); // "End run = %d\n"
    if (total > m_scoreToBeat) {
        m_scoreToBeat = total;
        const char* points = Str::FormatWithSeparators(total);
        std::snprintf(m_panel, sizeof(m_panel), GAME_TEXT(0x0064C914, "%s %s"), Script::GetString(kSetScoreIs), points); // "%s %s"
        m_status = Statuses()[kBeatScore];
        return;
    }
    if (total < m_scoreToBeat) {
        m_scoreToBeat = 0;
        ++m_letters[m_skater];
        SetNextRestartPoint();
        if (Ended()) {
            std::snprintf(m_panel, sizeof(m_panel), GAME_TEXT(0x0064E0B8, "You are a %s!"), m_word); // "You are a %s!"
            m_status = Statuses()[kGameOver];
            return;
        }
        char letter[2] = {Upper(m_word[m_letters[m_skater] - 1]), 0};
        std::snprintf(m_panel, sizeof(m_panel), GAME_TEXT(0x0064C914, "%s %s"), Script::GetString(kGetsTheLetter), letter); // "%s %s"
        m_status = Statuses()[kGotLetter];
        return;
    }
    if (total == 0) {
        Copy(m_panel, Script::GetString(kNoScoreSet));
        m_status = Statuses()[kNoScoreSetStatus];
    } else {
        Copy(m_panel, Script::GetString(kYouTied));
        m_status = Statuses()[kTieScore];
    }
}

// 0x005608B0
void CHorse::SwitchPlayers()
{
    int id = GetNextValidSkaterId();
    m_lastSkater = id;
    m_skater = id;
}

} // namespace Mdl

REPLACE(0x005600D0, &Mdl::CHorse::Construct);
REPLACE(0x00560100, &Mdl::CHorse::Init);
REPLACE(0x00560180, &Mdl::CHorse::StartRun);
REPLACE(0x00560190, &Mdl::CHorse::StatusEquals);
REPLACE(0x005601F0, &Mdl::CHorse::SetWord);
REPLACE(0x00560260, &Mdl::CHorse::GetWordForPlayer);
REPLACE(0x00560310, &Mdl::CHorse::GetRestart);
REPLACE(0x00560340, &Mdl::CHorse::SetNextRestartPoint);
REPLACE(0x00560380, &Mdl::CHorse::Ended);
REPLACE(0x005603D0, &Mdl::CHorse::GetNextValidSkaterId);
REPLACE(0x00560440, &Mdl::CHorse::GetString);
REPLACE(0x00560720, &Mdl::CHorse::EndRun);
REPLACE(0x005608B0, &Mdl::CHorse::SwitchPlayers);
