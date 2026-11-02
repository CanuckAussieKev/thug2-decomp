// modules::LetterMatch: the HORSE game, 0x005600D0-0x005608BF (see letter_match.h).

#include "game/skate/letter_match.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/str.h"
#include "engine/maths/random.h"
#include "engine/objects/proximity.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "game/skate/point_tally.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace modules {

namespace {

constexpr uint32_t kHorse = 0x9D65D0E7;           // Horse (the restarts' type)
constexpr uint32_t kHorsePreRun = 0x5078BC7C;     // horseprerun
constexpr uint32_t kYouAreA = 0x2523853D;         // youarea
constexpr uint32_t kHorsePostRun = 0x2A13F850;    // horsepostrun
constexpr uint32_t kPanelString2 = 0x478970CD;    // panelString2
constexpr uint32_t kFinalWord = 0x5F7D73E9;       // finalword
constexpr uint32_t kPlayerName = 0x9196D920;      // playerName
constexpr uint32_t kPanelString1 = 0xDE802177;    // panelString1
constexpr uint32_t kIsAn = 0x8DF39CE4;            // horse_str_isan
constexpr uint32_t kIsA = 0xFB911171;             // horse_str_isa
constexpr uint32_t kTargetScoreText = 0xA32FE78F; // horse_str_scoretobeat
constexpr uint32_t kSetScore = 0x695BC447;        // horse_str_setscore
constexpr uint32_t kSetScoreIs = 0x5E024658;      // horse_str_setscoreis
constexpr uint32_t kGetsTheLetter = 0xB470755A;   // horse_str_getstheletter
constexpr uint32_t kNoScoreSet = 0x7C61AA4A;      // horse_str_noscoreset
constexpr uint32_t kYouTied = 0x334622A1;         // horse_str_youtiedthetargetscore

// The statuses (0x0067F340, null-terminated): GotLetter, TieScore, BeatScore, 0xAE7D29B0 (the game over), idle,
// NoScoreSet.
std::array<uint32_t, 7> g_horse_statuses{{0x0FD80EBA, 0x006BE86F, 0xBAA12466, 0xAE7D29B0,
                             0x23DB4AEA, 0xA71DD8EB, 0x00000000}}; // (zero-terminated)
const std::array<uint32_t, 7>& statuses() { return g_horse_statuses; }
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): indexes into tables and arrays
enum StatusIndex : uint8_t {
    kGotLetter = 0,
    kTieScore = 1,
    kBeatScore = 2,
    kMatchEnded = 3,
    kIdle = 4,
    kNoScoreSetStatus = 5
};

int horse_restarts()
{
    std::array<uint32_t, 0x80> restarts{};
    return objects::get_restart_nodes(kHorse, restarts, 0);
}

objects::Rider* get_skater(uint32_t id) { return core::down_cast<objects::Rider>(skate_calls::get_rider_by_number(id)); }

char get_upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 0x20) : c; }

// strcpy (no bounds, as the original).
void dup_from(char* dest, const char* text)
{
    text::copy_unbounded(dest, text);
}

} // namespace

// 0x005600D0
LetterMatch::LetterMatch() { std::memcpy(word_.data(), "HORSE", 6); }

// 0x00560100
void LetterMatch::prepare_all()
{
    points_target_ = 0;
    skater_ = 0;
    last_skater_ = 0;
    std::snprintf(panel_.data(), sizeof(panel_), "%s", "unknown"); // "unknown"
    for (int& letters : letters_)
        letters = 0;
    restart_ = maths::rnd(horse_restarts());
}

// 0x00560180
void LetterMatch::begin_timed_session() { cur_standing_ = core::at(statuses(), kIdle); }

// 0x00560190
bool LetterMatch::state_matches(script::Struct* params)
{
    for (const uint32_t status : statuses()) {
        if (!status)
            break;
        if (params->has_bare_flag(status))
            return cur_standing_ == status;
    }
    return false;
}

// 0x005601F0
void LetterMatch::set_letters(const text::Chars& word)
{
    dup_from(word_.data(), word.c_str());
    debug::printf("New word = %s\n", word_.data()); // "New word = %s\n"
}

// 0x00560260
text::Chars LetterMatch::get_letter_string(int player)
{
    std::array<char, 0x100> word{};
    dup_from(word.data(), word_.data());
    if (std::strlen(word.data())) {
        auto letters = static_cast<uint32_t>(core::at(letters_, player));
        for (uint32_t i = 0; i < std::strlen(word.data()); ++i)
            if (i >= letters)
                core::at(word, i) = '-';
    }
    return {word.data()};
}

// 0x00560310
int LetterMatch::get_restart()
{
    horse_restarts();
    return restart_;
}

// 0x00560340
void LetterMatch::choose_next_spawn()
{
    ++restart_;
    if (static_cast<uint32_t>(restart_) >= static_cast<uint32_t>(horse_restarts()))
        restart_ = 0;
}

// 0x00560380
bool LetterMatch::is_over()
{
    for (int i = 0; i < kPlayers; ++i)
        if (get_skater(static_cast<uint32_t>(i)) && static_cast<uint32_t>(core::at(letters_, i)) >= std::strlen(word_.data()))
            return true;
    return false;
}

// 0x005603D0
int LetterMatch::fetch_next_live_rider()
{
    int id = 0;
    for (int i = 0; i < kPlayers; ++i) {
        id = (skater_ + i + 1) % kPlayers;
        if (get_skater(static_cast<uint32_t>(id)))
            break;
    }
    debug::printf("new skater id = %d\n", id);  // "new skater id = %d\n"
    if (script::fetch_int_value("debug_horse")) // "debug_horse"
        return 0;
    return id;
}

// 0x00560440
text::Chars LetterMatch::fetch_text(uint32_t which)
{
    std::array<char, 0x100> text{};
    std::memcpy(text.data(), "Unknown", 8); // "Unknown"
    const objects::Rider* skater = get_skater(static_cast<uint32_t>(skater_));
    switch (which) {
    case kPanelString1:
    case kPanelString2: {
        const int player = which == kPanelString1 ? 0 : 1;
        const text::Chars word = get_letter_string(player);
        std::snprintf(text.data(), sizeof(text), "%s: %s", get_skater(static_cast<uint32_t>(player))->fetch_shown_name(),
                      word.c_str()); // "%s: %s"
        break;
    }
    case kHorsePostRun:
        dup_from(text.data(), panel_.data());
        break;
    case kYouAreA: {
        const char first = get_upper(core::at(word_, 0));
        const bool vowel = first == 'A' || first == 'E' || first == 'I' || first == 'O' || first == 'U';
        const char* article = script::fetch_text(vowel ? kIsAn : kIsA);
        const text::Chars name = fetch_text(kPlayerName);
        std::snprintf(text.data(), sizeof(text), "%s %s", name.c_str(), article); // "%s %s"
        break;
    }
    case kHorsePreRun:
        if (points_target_ > 0) {
            const char* points = text::format_with_separators(points_target_);
            std::snprintf(text.data(), sizeof(text), "%s %s", script::fetch_text(kTargetScoreText), points); // "%s %s"
        } else
            std::snprintf(text.data(), sizeof(text), script::fetch_text(kSetScore));
        break;
    case kFinalWord:
        std::snprintf(text.data(), sizeof(text), "%s", word_.data());
        break;
    case kPlayerName:
        std::snprintf(text.data(), sizeof(text), "%s", get_skater(skater->ident_)->fetch_shown_name());
        break;
    default:
        break;
    }
    return {text.data()};
}

// 0x00560720
void LetterMatch::end_run()
{
    std::snprintf(panel_.data(), sizeof(panel_), "%s", "unknown"); // "unknown"
    last_skater_ = skater_;
    void* score = get_skater(static_cast<uint32_t>(skater_))->get_points_total();
    const int total = static_cast<modules::PointTally*>(score)->get_total_score();
    debug::printf("End run = %d\n", total); // "End run = %d\n"
    if (total > points_target_) {
        points_target_ = total;
        const char* points = text::format_with_separators(total);
        std::snprintf(panel_.data(), sizeof(panel_), "%s %s", script::fetch_text(kSetScoreIs), points); // "%s %s"
        cur_standing_ = core::at(statuses(), kBeatScore);
        return;
    }
    if (total < points_target_) {
        points_target_ = 0;
        ++core::at(letters_, skater_);
        choose_next_spawn();
        if (is_over()) {
            std::snprintf(panel_.data(), sizeof(panel_), "You are a %s!", word_.data()); // "You are a %s!"
            cur_standing_ = core::at(statuses(), kMatchEnded);
            return;
        }
        std::array<char, 2> letter = {{get_upper(core::at(word_, core::at(letters_, skater_) - 1)), 0}};
        std::snprintf(panel_.data(), sizeof(panel_), "%s %s", script::fetch_text(kGetsTheLetter), letter.data()); // "%s %s"
        cur_standing_ = core::at(statuses(), kGotLetter);
        return;
    }
    if (total == 0) {
        dup_from(panel_.data(), script::fetch_text(kNoScoreSet));
        cur_standing_ = core::at(statuses(), kNoScoreSetStatus);
    } else {
        dup_from(panel_.data(), script::fetch_text(kYouTied));
        cur_standing_ = core::at(statuses(), kTieScore);
    }
}

// 0x005608B0
void LetterMatch::swap_riders()
{
    const int id = fetch_next_live_rider();
    last_skater_ = id;
    skater_ = id;
}

} // namespace modules
