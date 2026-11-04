// NOT ORIGINAL - agreeing with THUGPRO in network games, at run time. See thugpro_compat.h.

#include "game/online/thugpro_compat.h"

#include <cstdint>
#include <array>

#include "game/online/gamenet_manager.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "base/core/no_destroy.h"
#include "game/online/net_game_state.h"

namespace online::thugpro {

namespace {
constexpr double kHalfSize = 0.5;
constexpr double kQuarterSize = 0.25;

constexpr uint32_t kLevel = 0x651533EC;       // level
constexpr uint32_t kIsBigLevel = 0x3D70ED3F;  // IsBigLevel
constexpr uint32_t kIsTallLevel = 0x7C793877; // IsTallLevel

// The level select's lists: THUG2's own, then the ported levels' (tools/pipeline/menu_level/level_select.ts sections()).
constexpr auto kLevelLists = std::to_array<const char*>({
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
});

// NOT ORIGINAL grouping - thugpro_compat.cpp's state (the original's globals). Made on first use, never destroyed
// (thugpro_compat_state()).
struct ThugproCompatState {
    PositionScale factor_{};
    uint32_t scale_level_{}; // the level thugpro_compat_state().factor_ was found for (0: none)
    bool scale_net_game_{};
};

ThugproCompatState& thugpro_compat_state()
{
    static core::NoDestroy<ThugproCompatState> state;
    return state.get();
}

} // namespace

const PositionScale& current_position_scale() { return thugpro_compat_state().factor_; }

void set_position_scale(const PositionScale& scale) { thugpro_compat_state().factor_ = scale; }

PositionScale level_position_scale(uint32_t level)
{
    PositionScale scale;
    for (const char* list : kLevelLists) {
        script::Array* levels = script::fetch_list(list);
        if (!levels) {
            continue;
        }
        for (uint32_t i = 0; i < levels->extent(); ++i) {
            auto* entry = levels->struct_at(i);
            uint32_t entry_level = 0;
            if (!entry || !entry->read_hash_value(kLevel, &entry_level, 0) || entry_level != level) {
                continue;
            }
            scale.big = entry->has_bare_flag(kIsBigLevel);
            scale.tall = entry->has_bare_flag(kIsTallLevel);
            return scale;
        }
    }
    return scale;
}

void refresh_position_scale()
{
    auto* manager = net_game_state().online_hub();
    const bool net_game = (manager != nullptr) && manager->in_net_game();
    const uint32_t level = manager ? manager->level() : 0;
    if (net_game == thugpro_compat_state().scale_net_game_ && level == thugpro_compat_state().scale_level_) {
        return;
    }
    thugpro_compat_state().scale_net_game_ = net_game;
    thugpro_compat_state().scale_level_ = level;
    const PositionScale scale = net_game ? level_position_scale(level) : PositionScale{};
    if (scale.big != thugpro_compat_state().factor_.big || scale.tall != thugpro_compat_state().factor_.tall) {
        get_log("THUGPRO big level (TP-R6): network positions %s%s", scale.big ? "x/z in whole units" : "x/z in halves",
                scale.tall ? ", y in whole units" : "");
    }
    thugpro_compat_state().factor_ = scale;
}

// (the original: FLD the float, FMUL its constant - FADD ST0,ST0 for the doubling -, _ftol, the low 16 bits kept)
int16_t pack_horizontal(float value)
{
    const double v = thugpro_compat_state().factor_.big ? static_cast<double>(value) : static_cast<double>(value) * 2.0;
    return static_cast<int16_t>(static_cast<int>(v));
}

int16_t pack_height(float value)
{
    const double v = thugpro_compat_state().factor_.tall ? static_cast<double>(value) : static_cast<double>(value) * 4.0;
    return static_cast<int16_t>(static_cast<int>(v));
}

// (FILD, FMUL 0.5 / 0.25, FSTP: exact)
float unpack_horizontal(int value)
{
    return thugpro_compat_state().factor_.big ? static_cast<float>(value) : static_cast<float>(static_cast<double>(value) * kHalfSize);
}

float unpack_height(int value)
{
    return thugpro_compat_state().factor_.tall ? static_cast<float>(value) : static_cast<float>(static_cast<double>(value) * kQuarterSize);
}

} // namespace online::thugpro
