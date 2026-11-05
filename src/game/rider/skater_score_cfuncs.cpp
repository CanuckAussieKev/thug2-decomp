// Script commands (CFunctions) 0x005B5770-0x005B5BE0: skater set-up, scores, the nearest skater (see
// skater_score_cfuncs.h).

#include "game/rider/skater_score_cfuncs.h"

#include <cstdint>

#include "engine/memory/manager.h"
#include "engine/ui/screen_element_manager.h"
#include "game/online/gamenet_manager.h"
#include "game/online/player_info.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "game/online/net_game_state.h"

namespace commands {

namespace {
constexpr float kNoHitDistance = 1000000.0F;
constexpr uint32_t kAllBitsSet = 0xFFFFFFFF; // (text unknown)

constexpr uint32_t kResetAll = 0x81740BB6;     // "reset_all"
constexpr uint32_t kNetKing = 0x6EF8FDA0;      // "netking"
constexpr uint32_t kKing = 0x5D32129C;         // "king"
constexpr uint32_t kUseBailStyle = 0x929EB4D5; // "UseBailStyle"
constexpr uint32_t kSkipTeam = 0x6C2A140F;     // "exclude_team"
constexpr uint32_t kRadius = 0xC48391A5;       // "radius"
constexpr uint32_t kTeam = 0x3B1F59E0;         // "team"
constexpr uint32_t kPlayer = 0x67E6859A;       // "player"

modules::Skate* get_skate() { return modules::Skate::sole(); }
objects::Rider* get_skater_at(int index) { return core::down_cast<objects::Rider>(get_skate()->get_rider_at(index)); }


// The game mode's name (0x005532A0, no source yet).
uint32_t active_ruleset_label() { return goals::game_mode()->get_name_key(); }

} // namespace

// 0x005B5770: InitializeSkaters: each skater - a local one's input reset first (vtable slot 14, reset_input) - taken
// out of the world (the skate module's 0x00564C60 with false). Always true.
bool cfunc_initialize_skaters(script::Struct* /*params*/, script::Context /*context*/)
{
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (skater->is_local_skater() != 0) {
            skater->reset_input();
        }
        modules::Skate::conceal_skater(skater, false);
    }
    return true;
}

// 0x005B57E0: ReinsertSkaters: each skater shown again, in its own skater heap. Always true.
bool cfunc_reinsert_skaters(script::Struct* /*params*/, script::Context /*context*/)
{
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(core::at(manager->skater_heaps(), skater->get_skater_number()));
        skater->show_skater();
        memory::Hub::get_pull()->leave_heap_scope();
    }
    return true;
}

// 0x005B5860: ResetScore [reset_all]: the local skaters' scores (with reset_all, every skater's) reset and their
// score text shown as "0" - "0:00" in king of the hill. Always true.
bool cfunc_reset_score(script::Struct* params, script::Context /*context*/)
{
    const bool all = params->has_entry_named(kResetAll);
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!(skater->is_local_skater() != 0) && !all) {
            continue;
        }
        static_cast<modules::PointTally*>(skater->get_points_total())->get_reset();
        if (active_ruleset_label() == kNetKing || active_ruleset_label() == kKing) {
            frontend::set_score_text("0:00", skater->get_skater_number()); // "0:00"
        } else {
            frontend::set_score_text("0", skater->get_skater_number()); // "0"
        }
    }
    return true;
}

// 0x005B5950: UpdateScore: each local skater's score total set again to itself (so it's shown). Always true.
bool cfunc_update_score(script::Struct* /*params*/, script::Context /*context*/)
{
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!(skater->is_local_skater() != 0)) {
            continue;
        }
        void* score = skater->get_points_total();
        static_cast<modules::PointTally*>(score)->set_grand_total(
            static_cast<modules::PointTally*>(score)->get_total_score());
    }
    return true;
}

// 0x005B59C0: ResetScorePot [UseBailStyle]: each local skater's score pot reset (0x00564860 with true unless
// UseBailStyle). Always true.
bool cfunc_reset_score_pot(script::Struct* params, script::Context /*context*/)
{
    const bool bail_style = params->has_bare_flag(kUseBailStyle);
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (skater->is_local_skater() != 0) {
            static_cast<modules::PointTally*>(skater->get_points_total())->get_bail(!bail_style);
        }
    }
    return true;
}

// 0x005B5A40: GetCollidingPlayerAndTeam exclude_team=t radius=feet: player, the skater nearest the script's object
// within the radius (in inches: * 12) that is in another team without the flag or in team t with it, and team, its
// team (-1 for both if none). Always true.
bool cfunc_get_colliding_player_and_team(script::Struct* params, script::Context context)
{
    script::Struct* results = context.args().get();
    auto* object = core::down_cast<objects::GameEntity>(context.entity().get_pull());
    int team = -1;
    uint32_t player = kAllBitsSet;
    float nearest = kNoHitDistance;
    int exclude_team = 0;
    params->fetch_int_value(kSkipTeam, &exclude_team, 1);
    float radius = 0.0F;
    params->fetch_float(kRadius, &radius, 1);
    const double inches = static_cast<double>(radius) * 12.0F; // x87: the square taken before rounding
    auto radius_squared = static_cast<float>(inches * inches);
    online::Hub* net = online::net_game_state().online_hub();
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        const objects::Rider* skater = get_skater_at(i);
        if (!skater) {
            continue;
        }
        auto* info = (net->roster().find_player_slot_by_obj(static_cast<uint16_t>(skater->ident())));
        if (!info) {
            continue;
        }
        if (info->team() == exclude_team && (info->is_carrying_flag() == 0)) {
            continue;
        }
        if (info->team() != exclude_team && (info->is_carrying_flag() != 0)) {
            continue;
        }
        const double dx = static_cast<double>(skater->at_point().x) - object->at_point().x;
        const double dy = static_cast<double>(skater->at_point().y) - object->at_point().y;
        const double dz = static_cast<double>(skater->at_point().z) - object->at_point().z;
        const double distance_squared = (dz * dz) + (dx * dx) + (dy * dy);
        if (distance_squared < radius_squared && distance_squared < nearest) {
            nearest = static_cast<float>(distance_squared);
            player = skater->ident();
        }
    }
    if (static_cast<int>(player) > -1) {
        team = (net->roster().find_player_slot_by_obj(static_cast<uint16_t>(player)))->team();
    }
    results->put_int(checksums::Name{kTeam}, team);
    results->put_int(checksums::Name{kPlayer}, static_cast<int>(player));
    return true;
}

} // namespace commands
