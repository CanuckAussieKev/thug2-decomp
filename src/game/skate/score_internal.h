#pragma once

// What the score's files (point_tally.cpp, score_combo.cpp, score_net.cpp) share: the skater, the game mode, the screen
// elements, the trick tables and the checksums they use.

#include <cstdint>

#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/trick_objects.h"
#include "game/skate/game_mode.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"

namespace GameModules {
namespace ScoreInternal {

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
extern int g_trickReduction[5]; // (0x0064E280)
extern int g_spinMultiplier[6]; // (0x0064E268)
inline int Reduction(int repeats) { return repeats < 5 ? g_trickReduction[repeats] : 10; }
inline int SpinMultiplier(int spin) { return spin < 6 ? g_spinMultiplier[spin] : 7; }

// The C runtime's _ftol (0x006236FC): truncated toward zero, the low 32 bits of the 64-bit result.
inline int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

inline GoalSystem::CMatchRules* GameMode() { return GoalSystem::g_gameMode; }
inline OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }
inline GameObjects::CTrickSpotHub* TrickObjects() { return GameObjects::g_trickSpotHub; }
inline GameObjects::CRider* RiderForSlot(int id)
{
    return static_cast<GameObjects::CRider*>(SkateCalls::GetRiderByNumber(static_cast<uint32_t>(id)));
}

// The screen element with this id (its smart pointer let go at once: the raw pointer kept, as the original).
inline MenuSystem::CUiWidget* FindElement(uint32_t id)
{
    MenuSystem::ElementPtr found;
    return MenuSystem::CUiTree::Sole()->FetchPart(&found, id, 0)->m_ownerObject;
}

// A structure from the pool (null if that fails: the original goes on with it) / deleted.
Scripts::CStruct* SpawnParamTable();
void DeleteStructure(Scripts::CStruct* structure);

} // namespace ScoreInternal
} // namespace Mdl
