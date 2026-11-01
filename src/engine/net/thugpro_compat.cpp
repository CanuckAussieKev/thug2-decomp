// NOT ORIGINAL - agreeing with THUGPRO in network games, at run time. See thugpro_compat.h.

#include "engine/net/thugpro_compat.h"

#include <cstdint>

#include "engine/net/gamenet_manager.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"

namespace online::thugpro {

namespace {

constexpr uint32_t kLevel = 0x651533EC;       // level
constexpr uint32_t kIsBigLevel = 0x3D70ED3F;  // IsBigLevel
constexpr uint32_t kIsTallLevel = 0x7C793877; // IsTallLevel

// The level select's lists: THUG2's own, then the ported levels' (tools/pipeline/menu_level/level_select.ts sections()).
constexpr const char* kLevelLists[] = {
    "level_select_menu_level_info",
    "level_select_menu_thug1_level_info",
    "level_select_menu_thps4_level_info",
    "level_select_menu_thps3_level_info",
    "level_select_menu_thaw_level_info",
    // (not THUGPRO's: our THP8 / Proving Ground levels wider than half units reach - thug2-thp8-level-converter/thp8_levels,
    // NET_HALF_REACH; thug2-thpg-level-converter/thpg_levels)
    "level_select_menu_thps2_level_info",
    "level_select_menu_thp8_level_info",
    "level_select_menu_thpg_level_info",
    "level_select_menu_misc_level_info", // (the MISCELLANEOUS section: skate shops, tutorials, test levels)
};

PositionScale s_factor;
uint32_t s_scale_level; // the level s_factor was found for (0: none)
bool s_scale_net_game;

} // namespace

const PositionScale& current_position_scale() { return s_factor; }

void set_position_scale(const PositionScale& scale) { s_factor = scale; }

PositionScale level_position_scale(uint32_t level)
{
    PositionScale scale;
    for (const char* list : kLevelLists) {
        script::Array* levels = script::fetch_list(list);
        if (!levels)
            continue;
        for (uint32_t i = 0; i < levels->extent_; ++i) {
            auto* entry = reinterpret_cast<script::Struct*>(levels->fetch_part(i));
            uint32_t entry_level = 0;
            if (!entry || !entry->read_hash_value(kLevel, &entry_level, 0) || entry_level != level)
                continue;
            scale.big = entry->has_bare_flag(kIsBigLevel);
            scale.tall = entry->has_bare_flag(kIsTallLevel);
            return scale;
        }
    }
    return scale;
}

void refresh_position_scale()
{
    auto* manager = g_online_hub;
    const bool net_game = manager && manager->in_net_game();
    const uint32_t level = manager ? manager->level_ : 0;
    if (net_game == s_scale_net_game && level == s_scale_level)
        return;
    s_scale_net_game = net_game;
    s_scale_level = level;
    const PositionScale scale = net_game ? level_position_scale(level) : PositionScale{};
    if (scale.big != s_factor.big || scale.tall != s_factor.tall)
        get_log("THUGPRO big level (TP-R6): network positions %s%s", scale.big ? "x/z in whole units" : "x/z in halves",
                scale.tall ? ", y in whole units" : "");
    s_factor = scale;
}

// (the original: FLD the float, FMUL its constant - FADD ST0,ST0 for the doubling -, _ftol, the low 16 bits kept)
int16_t pack_horizontal(float value)
{
    const double v = s_factor.big ? static_cast<double>(value) : static_cast<double>(value) * 2.0;
    return static_cast<int16_t>(static_cast<int>(v));
}

int16_t pack_height(float value)
{
    const double v = s_factor.tall ? static_cast<double>(value) : static_cast<double>(value) * 4.0;
    return static_cast<int16_t>(static_cast<int>(v));
}

// (FILD, FMUL 0.5 / 0.25, FSTP: exact)
float unpack_horizontal(int value)
{
    return s_factor.big ? static_cast<float>(value) : static_cast<float>(static_cast<double>(value) * 0.5);
}

float unpack_height(int value)
{
    return s_factor.tall ? static_cast<float>(value) : static_cast<float>(static_cast<double>(value) * 0.25);
}

} // namespace online::thugpro
