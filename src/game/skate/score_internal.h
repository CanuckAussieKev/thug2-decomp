#pragma once

// What the score's files (score.cpp, score_combo.cpp, score_net.cpp) share: the skater, the game mode, the screen
// elements, the trick tables and the checksums they use.

#include <cstdint>

#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/trick_objects.h"
#include "game/skate/game_mode.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "hook/game.h"

namespace Mdl {
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
inline int Reduction(int repeats) { return repeats < 5 ? reinterpret_cast<const int*>(0x0064E280)[repeats] : 10; }
inline int SpinMultiplier(int spin) { return spin < 6 ? reinterpret_cast<const int*>(0x0064E268)[spin] : 7; }

// The C runtime's _ftol (0x006236FC): truncated toward zero, the low 32 bits of the 64-bit result.
inline int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

inline Game::CGameMode* GameMode() { return Game::g_gameMode; }
inline GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
inline Obj::CTrickObjectManager* TrickObjects() { return Obj::g_trickObjectManager; }
inline Obj::CSkater* SkaterById(int id)
{
    return static_cast<Obj::CSkater*>(SkateCalls::GetSkaterById(static_cast<uint32_t>(id)));
}
inline const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }

// The screen element with this id (its smart pointer let go at once: the raw pointer kept, as the original).
inline Front::CScreenElement* FindElement(uint32_t id)
{
    Front::ElementPtr found;
    return Front::CScreenElementManager::Instance()->GetElement(&found, id, 0)->mp_object;
}

// A structure from the pool (null if that fails: the original goes on with it) / deleted.
Script::CStruct* NewStructure();
void DeleteStructure(Script::CStruct* structure);

} // namespace ScoreInternal
} // namespace Mdl
