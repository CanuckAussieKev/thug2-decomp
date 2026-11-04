#pragma once

// What the score's files (point_tally.cpp, score_combo.cpp, score_net.cpp) share: the skater, the game mode, the screen
// elements, the trick tables and the checksums they use.

#include <cstdint>
#include <array>

#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/trick_objects.h"
#include "game/skate/game_mode.h"
#include "engine/script/param_struct.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/net/net_game_state.h"

namespace modules::score_internal {
constexpr int kRepeatsReduced = 5;       // repeats beyond the table all score the same reduction
constexpr int kMinimumReduction = 10;
constexpr int kSpinMultiplierCount = 6;
constexpr int kMaxSpinMultiplier = 7;

constexpr uint32_t kTheTrickText = 0x44727DAE;        // the_trick_text (+ the skater's number)
constexpr uint32_t kTheScorePotText = 0xF4D3A70E;     // the_score_pot_text
constexpr uint32_t kTrickTextContainer = 0x1E55886E;  // trick_text_container
constexpr uint32_t kTheBalanceMeter = 0xA4DB8A4B;     // the_balance_meter
constexpr uint32_t kTheSpecialBarSprite = 0xFE1DC544; // the_special_bar_sprite
constexpr uint32_t kSkaterEnterSpecial = 0xD0CD8081;  // SkaterEnterSpecial
constexpr uint32_t kSkaterExitSpecial = 0x7CCF5EE2;   // SkaterExitSpecial
constexpr uint32_t kShouldModulateColor = 0x11941568; // should_modulate_color
constexpr uint32_t kScoreByClusters = 0x2F507903;     // (name unknown: an integer - the score is the clusters held)
constexpr uint32_t kNewSpecial = 0xB23D1FB5;          // NewSpecial
constexpr uint32_t kTrick = 0x270F56E1;               // trick (the component)
constexpr uint32_t kStatsManager = 0x4FF8DAC6;        // StatsManager
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;   // SkaterCorePhysics
constexpr uint32_t kSkaterRunTimer = 0x8BCE7E66;      // SkaterRunTimer
constexpr uint32_t kSkaterAdjustPhysics = 0x9F9CC949; // SkaterAdjustPhysics
constexpr uint32_t kAnimBlender = 0x7149B753;         // animblender

// A trick's points by its repeats (percent: 100, 75, 50, 25, 10) and its spin (half the multiplier: 2 none ... 7),
// read from the exe as the original does (an index outside the tables reads past them, as there).
inline constexpr std::array<int, 5> kTrickReduction{{100, 75, 50, 25, 10}}; // (0x0064E280)
inline constexpr std::array<int, 6> kSpinMultiplier{{2, 3, 4, 5, 6, 7}}; // (0x0064E268)
inline int get_reduction(int repeats) { return repeats < kRepeatsReduced ? core::at(kTrickReduction, repeats) : kMinimumReduction; }
inline int spin_multiplier(int spin) { return spin < kSpinMultiplierCount ? core::at(kSpinMultiplier, spin) : kMaxSpinMultiplier; }

// The C runtime's _ftol (0x006236FC): truncated toward zero, the low 32 bits of the 64-bit result.
inline int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

inline goals::MatchRules* get_game_mode() { return goals::game_mode(); }
inline objects::TrickSpotHub* get_trick_objects() { return objects::trick_spot_hub(); }
inline objects::Rider* rider_for_slot(int id)
{
    return core::down_cast<objects::Rider>(skate_calls::get_rider_by_number(static_cast<uint32_t>(id)));
}

// The screen element with this id (its smart pointer let go at once: the raw pointer kept, as the original).
inline frontend::UiWidget* find_element(uint32_t id)
{
    frontend::ElementPtr found;
    return frontend::UiTree::fetch_part(&found, id, 0)->get_pull();
}

} // namespace modules::score_internal
