// Script commands (CFunctions) 0x005B5770-0x005B5BE0: skater set-up, scores, the nearest skater (see
// skater_score_cfuncs.h).

#include "game/rider/skater_score_cfuncs.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "frontend/screen_element_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace commands {

namespace {

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
bool is_host_local(objects::Rider* skater) { return skater->is_local_skater_ != 0; }

// The skater's score (the SkaterScore component's object): 0x00564A10 resets it, 0x00561B40 / 0x00561A80 get and set
// its total, 0x00564860 resets its pot (bail style or not). None has source yet.
void* get_score_of(objects::Rider* skater) { return skater->get_points_total(); }

// The game mode's name (0x005532A0, no source yet).
uint32_t active_ruleset_label() { return goals::g_game_mode->get_name_key(); }

} // namespace

// 0x005B5770: InitializeSkaters: each skater - a local one's input reset first (vtable slot 14, reset_input) - taken
// out of the world (the skate module's 0x00564C60 with false). Always true.
bool cfunc_initialize_skaters(script::Struct*, script::Script*)
{
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (is_host_local(skater)) {
            skater->reset_input();
        }
        get_skate()->conceal_skater(skater, false);
    }
    return true;
}

// 0x005B57E0: ReinsertSkaters: each skater shown again, in its own skater heap. Always true.
bool cfunc_reinsert_skaters(script::Struct*, script::Script*)
{
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(core::at(manager->skater_heaps_, skater->get_skater_number()));
        skater->show_skater();
        memory::Hub::get_pull()->leave_heap_scope();
    }
    return true;
}

// 0x005B5860: ResetScore [reset_all]: the local skaters' scores (with reset_all, every skater's) reset and their
// score text shown as "0" - "0:00" in king of the hill. Always true.
bool cfunc_reset_score(script::Struct* params, script::Script*)
{
    const bool all = params->has_entry_named(kResetAll);
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!is_host_local(skater) && !all)
            continue;
        static_cast<modules::PointTally*>(get_score_of(skater))->get_reset();
        if (active_ruleset_label() == kNetKing || active_ruleset_label() == kKing)
            frontend::set_score_text("0:00", skater->get_skater_number()); // "0:00"
        else
            frontend::set_score_text("0", skater->get_skater_number()); // "0"
    }
    return true;
}

// 0x005B5950: UpdateScore: each local skater's score total set again to itself (so it's shown). Always true.
bool cfunc_update_score(script::Struct*, script::Script*)
{
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!is_host_local(skater))
            continue;
        void* score = get_score_of(skater);
        static_cast<modules::PointTally*>(score)->set_grand_total(
            static_cast<modules::PointTally*>(score)->get_total_score());
    }
    return true;
}

// 0x005B59C0: ResetScorePot [UseBailStyle]: each local skater's score pot reset (0x00564860 with true unless
// UseBailStyle). Always true.
bool cfunc_reset_score_pot(script::Struct* params, script::Script*)
{
    const bool bail_style = params->has_bare_flag(kUseBailStyle);
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (is_host_local(skater))
            static_cast<modules::PointTally*>(get_score_of(skater))->get_bail(!bail_style);
    }
    return true;
}

// 0x005B5A40: GetCollidingPlayerAndTeam exclude_team=t radius=feet: player, the skater nearest the script's object
// within the radius (in inches: * 12) that is in another team without the flag or in team t with it, and team, its
// team (-1 for both if none). Always true.
bool cfunc_get_colliding_player_and_team(script::Struct* params, script::Script* script)
{
    script::Struct* results = script->args_;
    auto* object = core::down_cast<objects::GameEntity>(script->entity_.get_pull());
    int team = -1;
    uint32_t player = 0xFFFFFFFF;
    float nearest = 1000000.0f;
    int exclude_team = 0;
    params->fetch_int_value(kSkipTeam, &exclude_team, 1);
    float radius = 0.0f;
    params->fetch_float(kRadius, &radius, 1);
    const double inches = static_cast<double>(radius) * 12.0f; // x87: the square taken before rounding
    auto radius_squared = static_cast<float>(inches * inches);
    online::Hub* net = online::g_online_hub;
    for (int i = 0; i < get_skate()->count_riders(); ++i) {
        const objects::Rider* skater = get_skater_at(i);
        if (!skater)
            continue;
        auto* info = (net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident_)));
        if (!info)
            continue;
        if (info->team_ == exclude_team && !info->is_carrying_flag())
            continue;
        if (info->team_ != exclude_team && info->is_carrying_flag())
            continue;
        const double dx = static_cast<double>(skater->at_point_.x) - object->at_point_.x;
        const double dy = static_cast<double>(skater->at_point_.y) - object->at_point_.y;
        const double dz = static_cast<double>(skater->at_point_.z) - object->at_point_.z;
        const double distance_squared = dz * dz + dx * dx + dy * dy;
        if (distance_squared < radius_squared && distance_squared < nearest) {
            nearest = static_cast<float>(distance_squared);
            player = skater->ident_;
        }
    }
    if (static_cast<int>(player) > -1)
        team =
            (net->find_player_slot_by_obj(static_cast<uint16_t>(player)))->team_;
    results->put_int(kTeam, team);
    results->put_int(kPlayer, static_cast<int>(player));
    return true;
}

} // namespace commands
