// NOT ORIGINAL - agreeing with THUGPRO in network games, at run time. See thugpro_compat.h.

#include "game/net/thugpro_compat.h"

#include <cstdint>

#include "game/net/gamenet_manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "game/sys/log.h"

namespace GameNet::Thugpro {

namespace {

constexpr uint32_t kLevel = 0x651533EC;       // level
constexpr uint32_t kIsBigLevel = 0x3D70ED3F;  // IsBigLevel
constexpr uint32_t kIsTallLevel = 0x7C793877; // IsTallLevel

// The level select's lists: THUG2's own, then the ported levels' (tools/pipeline/menu_level/level_select.ts sections()).
constexpr const char* kLevelLists[] = {
    "level_select_menu_level_info",       "level_select_menu_thug1_level_info", "level_select_menu_thps4_level_info",
    "level_select_menu_thps3_level_info", "level_select_menu_thaw_level_info",
    // (not THUGPRO's: our THP8 levels wider than half units reach - tools/pipeline/thp8_levels, NET_HALF_REACH)
    "level_select_menu_thps2_level_info", "level_select_menu_thp8_level_info",
    "level_select_menu_misc_level_info", // (the MISCELLANEOUS section: skate shops, tutorials, test levels)
};

PositionScale s_scale;
uint32_t s_scaleLevel;  // the level s_scale was found for (0: none)
bool s_scaleNetGame;

} // namespace

const PositionScale& CurrentPositionScale() { return s_scale; }

void SetPositionScale(const PositionScale& scale) { s_scale = scale; }

PositionScale LevelPositionScale(uint32_t level)
{
    PositionScale scale;
    for (const char* list : kLevelLists) {
        Script::CArray* levels = Script::GetArray(list);
        if (!levels)
            continue;
        for (uint32_t i = 0; i < levels->m_size; ++i) {
            auto* entry = reinterpret_cast<Script::CStruct*>(levels->GetElement(i));
            uint32_t entryLevel = 0;
            if (!entry || !entry->GetChecksum(kLevel, &entryLevel, 0) || entryLevel != level)
                continue;
            scale.big = entry->ContainsFlag(kIsBigLevel);
            scale.tall = entry->ContainsFlag(kIsTallLevel);
            return scale;
        }
    }
    return scale;
}

void RefreshPositionScale()
{
    auto* manager = g_gameNetManager;
    bool netGame = manager && manager->InNetGame();
    uint32_t level = manager ? manager->m_level : 0;
    if (netGame == s_scaleNetGame && level == s_scaleLevel)
        return;
    s_scaleNetGame = netGame;
    s_scaleLevel = level;
    PositionScale scale = netGame ? LevelPositionScale(level) : PositionScale{};
    if (scale.big != s_scale.big || scale.tall != s_scale.tall)
        Log("THUGPRO big level (TP-R6): network positions %s%s", scale.big ? "x/z in whole units" : "x/z in halves",
            scale.tall ? ", y in whole units" : "");
    s_scale = scale;
}

// (the original: FLD the float, FMUL its constant - FADD ST0,ST0 for the doubling -, _ftol, the low 16 bits kept)
int16_t PackHorizontal(float value)
{
    double v = s_scale.big ? static_cast<double>(value) : static_cast<double>(value) * 2.0;
    return static_cast<int16_t>(static_cast<int>(v));
}

int16_t PackHeight(float value)
{
    double v = s_scale.tall ? static_cast<double>(value) : static_cast<double>(value) * 4.0;
    return static_cast<int16_t>(static_cast<int>(v));
}

// (FILD, FMUL 0.5 / 0.25, FSTP: exact)
float UnpackHorizontal(int value)
{
    return s_scale.big ? static_cast<float>(value) : static_cast<float>(static_cast<double>(value) * 0.5);
}

float UnpackHeight(int value)
{
    return s_scale.tall ? static_cast<float>(value) : static_cast<float>(static_cast<double>(value) * 0.25);
}

} // namespace GameNet::Thugpro
