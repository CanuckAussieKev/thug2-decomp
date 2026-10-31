// GameModules::CLetterMatch: the HORSE game, 0x005600D0-0x005608BF (see letter_match.h).

#include "game/skate/letter_match.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/maths/random.h"
#include "game/obj/proximity.h"
#include "game/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/rider.h"
#include "game/skate/point_tally.h"

namespace GameModules {

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
constexpr uint32_t kTargetScoreText = 0xA32FE78F;      // horse_str_scoretobeat
constexpr uint32_t kSetScore = 0x695BC447;         // horse_str_setscore
constexpr uint32_t kSetScoreIs = 0x5E024658;       // horse_str_setscoreis
constexpr uint32_t kGetsTheLetter = 0xB470755A;    // horse_str_getstheletter
constexpr uint32_t kNoScoreSet = 0x7C61AA4A;       // horse_str_noscoreset
constexpr uint32_t kYouTied = 0x334622A1;          // horse_str_youtiedthetargetscore

// The statuses (0x0067F340, null-terminated): GotLetter, TieScore, BeatScore, 0xAE7D29B0 (the game over), idle,
// NoScoreSet.
uint32_t g_horseStatuses[7]{0x0FD80EBA, 0x006BE86F, 0xBAA12466, 0xAE7D29B0, 0x23DB4AEA, 0xA71DD8EB, 0x00000000}; // (zero-terminated)
const uint32_t* Statuses() { return g_horseStatuses; }
enum StatusIndex { kGotLetter = 0, kTieScore = 1, kBeatScore = 2, kMatchEnded = 3, kIdle = 4, kNoScoreSetStatus = 5 };

int HorseRestarts()
{
    uint32_t restarts[0x80];
    return GameObjects::GetRestartNodes(kHorse, restarts, 0);
}

GameObjects::CRider* Skater(uint32_t id) { return static_cast<GameObjects::CRider*>(SkateCalls::GetRiderByNumber(id)); }

char Upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 0x20) : c; }

// strcpy (no bounds, as the original).
void DupFrom(char* dest, const char* text)
{
    while ((*dest++ = *text++) != 0) {
    }
}

} // namespace

// 0x005600D0
CLetterMatch* CLetterMatch::MakeNew()
{
    std::memcpy(m_word, "HORSE", 6);
    m_restart = 0;
    return this;
}

// 0x00560100
void CLetterMatch::PrepareAll()
{
    m_pointsTarget = 0;
    m_skater = 0;
    m_lastSkater = 0;
    std::snprintf(m_panel, sizeof(m_panel), "%s", "unknown"); // "unknown"
    for (int& letters : m_letters)
        letters = 0;
    m_restart = Maths::Rnd(HorseRestarts());
}

// 0x00560180
void CLetterMatch::BeginTimedSession() { m_curStanding = Statuses()[kIdle]; }

// 0x00560190
bool CLetterMatch::StateMatches(Scripts::CStruct* params)
{
    for (const uint32_t* status = Statuses(); *status; ++status)
        if (params->HasBareFlag(*status))
            return m_curStanding == *status;
    return false;
}

// 0x005601F0
void CLetterMatch::SetLetters(TextUtil::Chars word)
{
    DupFrom(m_word, word.c_str());
    DebugOut::Printf("New word = %s\n", m_word); // "New word = %s\n"
}

// 0x00560260
TextUtil::Chars CLetterMatch::GetLetterString(int player)
{
    char word[0x100];
    DupFrom(word, m_word);
    if (std::strlen(word)) {
        uint32_t letters = static_cast<uint32_t>(m_letters[player]);
        for (uint32_t i = 0; i < std::strlen(word); ++i)
            if (i >= letters)
                word[i] = '-';
    }
    return TextUtil::Chars(word);
}

// 0x00560310
int CLetterMatch::GetRestart()
{
    HorseRestarts();
    return m_restart;
}

// 0x00560340
void CLetterMatch::ChooseNextSpawn()
{
    ++m_restart;
    if (static_cast<uint32_t>(m_restart) >= static_cast<uint32_t>(HorseRestarts()))
        m_restart = 0;
}

// 0x00560380
bool CLetterMatch::IsOver()
{
    for (int i = 0; i < kPlayers; ++i)
        if (Skater(static_cast<uint32_t>(i)) && static_cast<uint32_t>(m_letters[i]) >= std::strlen(m_word))
            return true;
    return false;
}

// 0x005603D0
int CLetterMatch::FetchNextLiveRider()
{
    int id = 0;
    for (int i = 0; i < kPlayers; ++i) {
        id = (m_skater + i + 1) % kPlayers;
        if (Skater(static_cast<uint32_t>(id)))
            break;
    }
    DebugOut::Printf("new skater id = %d\n", id); // "new skater id = %d\n"
    if (Scripts::FetchIntValue("debug_horse")) // "debug_horse"
        return 0;
    return id;
}

// 0x00560440
TextUtil::Chars CLetterMatch::FetchText(uint32_t which)
{
    char text[0x100];
    std::memcpy(text, "Unknown", 8); // "Unknown"
    GameObjects::CRider* skater = Skater(static_cast<uint32_t>(m_skater));
    switch (which) {
    case kPanelString1:
    case kPanelString2: {
        int player = which == kPanelString1 ? 0 : 1;
        TextUtil::Chars word = GetLetterString(player);
        std::snprintf(text, sizeof(text), "%s: %s", Skater(static_cast<uint32_t>(player))->FetchShownName(),
                      word.c_str()); // "%s: %s"
        break;
    }
    case kHorsePostRun: DupFrom(text, m_panel); break;
    case kYouAreA: {
        char first = Upper(m_word[0]);
        bool vowel = first == 'A' || first == 'E' || first == 'I' || first == 'O' || first == 'U';
        const char* article = Scripts::FetchText(vowel ? kIsAn : kIsA);
        TextUtil::Chars name = FetchText(kPlayerName);
        std::snprintf(text, sizeof(text), "%s %s", name.c_str(), article); // "%s %s"
        break;
    }
    case kHorsePreRun:
        if (m_pointsTarget > 0) {
            const char* points = TextUtil::FormatWithSeparators(m_pointsTarget);
            std::snprintf(text, sizeof(text), "%s %s", Scripts::FetchText(kTargetScoreText), points); // "%s %s"
        } else
            std::snprintf(text, sizeof(text), Scripts::FetchText(kSetScore));
        break;
    case kFinalWord: std::snprintf(text, sizeof(text), "%s", m_word); break;
    case kPlayerName:
        std::snprintf(text, sizeof(text), "%s",
                      Skater(*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(skater) + 0x14))->FetchShownName());
        break;
    default: break;
    }
    return TextUtil::Chars(text);
}

// 0x00560720
void CLetterMatch::EndRun()
{
    std::snprintf(m_panel, sizeof(m_panel), "%s", "unknown"); // "unknown"
    m_lastSkater = m_skater;
    void* score = Skater(static_cast<uint32_t>(m_skater))->GetPointsTotal();
    int total = static_cast<GameModules::PointTally*>(score)->GetTotalScore();
    DebugOut::Printf("End run = %d\n", total); // "End run = %d\n"
    if (total > m_pointsTarget) {
        m_pointsTarget = total;
        const char* points = TextUtil::FormatWithSeparators(total);
        std::snprintf(m_panel, sizeof(m_panel), "%s %s", Scripts::FetchText(kSetScoreIs), points); // "%s %s"
        m_curStanding = Statuses()[kBeatScore];
        return;
    }
    if (total < m_pointsTarget) {
        m_pointsTarget = 0;
        ++m_letters[m_skater];
        ChooseNextSpawn();
        if (IsOver()) {
            std::snprintf(m_panel, sizeof(m_panel), "You are a %s!", m_word); // "You are a %s!"
            m_curStanding = Statuses()[kMatchEnded];
            return;
        }
        char letter[2] = {Upper(m_word[m_letters[m_skater] - 1]), 0};
        std::snprintf(m_panel, sizeof(m_panel), "%s %s", Scripts::FetchText(kGetsTheLetter), letter); // "%s %s"
        m_curStanding = Statuses()[kGotLetter];
        return;
    }
    if (total == 0) {
        DupFrom(m_panel, Scripts::FetchText(kNoScoreSet));
        m_curStanding = Statuses()[kNoScoreSetStatus];
    } else {
        DupFrom(m_panel, Scripts::FetchText(kYouTied));
        m_curStanding = Statuses()[kTieScore];
    }
}

// 0x005608B0
void CLetterMatch::SwapRiders()
{
    int id = FetchNextLiveRider();
    m_lastSkater = id;
    m_skater = id;
}

} // namespace Mdl
