// Script commands asking about the skaters, 0x005AC5F0-0x005AD82F (see skater_query_cfuncs.h).

#include "game/rider/skater_query_cfuncs.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "game/rider/skater_score.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/rider/skater_camera_component.h"

namespace commands {

namespace {

using namespace skate_calls;
using online::PlayerRecord;

constexpr uint32_t kLastScoreLanded = 0xB9072B65;     // last_score_landed
constexpr uint32_t kPointScale = 0xB08C5AE8;          // point_scale
constexpr uint32_t kScorePot = 0x95E84391;            // scorepot
constexpr uint32_t kTotalScore = 0xEE5B2B48;          // totalscore
constexpr uint32_t kLastScoreLandedInfo = 0xEA60AC69; // lastscorelanded
constexpr uint32_t kSkaterScore = 0xD262802F;         // SkaterScore (the component)
constexpr uint32_t kSkaterCamera = 0x5E43A604;        // skatercamera (the component)
constexpr uint32_t kSkater = 0x5B8AB877;              // skater
constexpr uint32_t kPlayer = 0x67E6859A;              // player
constexpr uint32_t kIsMale = 0x3F813177;              // is_male
constexpr uint32_t kObjId = 0x0830ECAF;               // objID
constexpr uint32_t kIntId = 0xDB1E28CD;               // intId
constexpr uint8_t kMsgEliminated = 0x8D;              // the player of an object id eliminated (-1: none, a tie)
constexpr uint32_t kVelX = 0x6187906D, kVelY = 0x1680A0FB, kVelZ = 0x8F89F141; // vel_x, vel_y, vel_z

// The network manager and its player lists (lists::Hunt<PlayerRecord>, vtable 0x0064C1A8; JoinerRecord's 0x0064C82C).
online::Hub* net_session_slot() { return online::g_online_hub; }
struct PlayerSearch {
    void** vtable_ = modules::kPlayerSearchVtable;
    void* list_link_ = nullptr;
};
struct NewPlayerSearch {
    void** vtable_ = online::kNewPlayerSearchVtable;
    void* list_link_ = nullptr;
};
PlayerRecord* start_roster_sweep(online::Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<PlayerRecord*>(manager->begin_player_walk(search, observers));
}
PlayerRecord* step_roster_sweep(online::Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<PlayerRecord*>(manager->step_player_walk(search, observers));
}

// 0x005535F0: a team game.
bool is_squad_match() { return static_cast<goals::MatchRules*>(get_game_mode())->is_squad_match() != 0; }

// A skater's score (modules::PointTally, not rewritten yet): 0x00561B40 its total; +0x14 the trick's base score, +0x60 its
// multiplier, +0x20 the last trick's score.
uint8_t* get_score_of(objects::GameEntity* skater)
{
    return static_cast<uint8_t*>(static_cast<objects::Rider*>(skater)->get_points_total());
}
// (a player's skater's score through its SkaterScore component)
uint8_t* component_score_of(objects::GameEntity* skater)
{
    return static_cast<objects::RiderPointKeeper*>(skater->fetch_module(kSkaterScore))->points_held_;
}
int total_score(uint8_t* score) { return reinterpret_cast<modules::PointTally*>(score)->get_total_score(); }
int last_score_landed(uint8_t* score) { return *reinterpret_cast<int*>(score + 0x20); }
// The score pot: the base score times the multiplier, truncated (x87 at 53 bits, then the original's _ftol).
int score_pot(uint8_t* score)
{
    double pot = static_cast<double>(*reinterpret_cast<int*>(score + 0x14)) * *reinterpret_cast<float*>(score + 0x60);
    return static_cast<int>(static_cast<int64_t>(pot));
}
// The local skater's speed: the length of its velocity (as the original's x87: in double).
double speed_of(objects::GameEntity* skater)
{
    double x = skater->vel_.x, y = skater->vel_.y, z = skater->vel_.z;
    return std::sqrt(x * x + y * y + z * z);
}
// An integer parameter, 0 if missing.
int integer_or_zero(script::Struct* params)
{
    int value = 0;
    return params->fetch_int_value(0u, &value, 0) ? value : 0;
}

// Message 0x8D (the eliminated player's object id, -1 for a tie) to the players.
void prepare_eliminated(net::NetMessageInfo* desc, uint32_t* data)
{
    desc->ident_ = kMsgEliminated;
    desc->byte_count_ = 4;
    desc->content_ = data;
    desc->queue_ = net::NetMessageInfo::kLaneOrdered;
    desc->group_id_ = 8;
}
void send_to_players(online::Hub* manager, net::NetMessageInfo* desc, bool observers)
{
    PlayerSearch search;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, observers); player;
         player = step_roster_sweep(manager, &search, observers))
        manager->host_->put_on_outbox(player->get_link_handle(), desc);
}

} // namespace

// 0x005AC5F0 / 0x005AC660: SkaterSpeedGreaterThan / SkaterSpeedLessThan <speed> - the local skater's (none, or no
// speed given: false).
bool __cdecl cfunc_skater_speed_greater_than(script::Struct* params, script::Script*)
{
    float speed;
    if (!params->fetch_float(0u, &speed, 0))
        return false;
    objects::GameEntity* skater = get_home_rider();
    return skater && speed_of(skater) > speed;
}

bool __cdecl cfunc_skater_speed_less_than(script::Struct* params, script::Script*)
{
    float speed;
    if (!params->fetch_float(0u, &speed, 0))
        return false;
    objects::GameEntity* skater = get_home_rider();
    return skater && speed_of(skater) < speed;
}

// 0x005AC6D0 / 0x005AC720: SkaterLastScoreLandedGreaterThan / less_than <score> (0 if missing) - the local skater's
// last trick's score.
bool __cdecl cfunc_skater_last_score_landed_greater_than(script::Struct* params, script::Script*)
{
    int value = integer_or_zero(params);
    objects::GameEntity* skater = get_home_rider();
    if (!skater)
        return false;
    uint8_t* score = get_score_of(skater);
    return score && last_score_landed(score) > value;
}

bool __cdecl cfunc_skater_last_score_landed_less_than(script::Struct* params, script::Script*)
{
    int value = integer_or_zero(params);
    objects::GameEntity* skater = get_home_rider();
    if (!skater)
        return false;
    uint8_t* score = get_score_of(skater);
    return score && last_score_landed(score) < value;
}

// 0x005AC770: SkaterGetLastScoreLanded - the local skater's last trick's score as last_score_landed (false without).
bool __cdecl cfunc_skater_get_last_score_landed(script::Struct*, script::Script* script)
{
    objects::GameEntity* skater = get_home_rider();
    if (!skater)
        return false;
    uint8_t* score = get_score_of(skater);
    if (!score)
        return false;
    script->args_->put_int(kLastScoreLanded, last_score_landed(score));
    return true;
}

// 0x005AC7B0: AccumulateScores - each player's total score added to the score accumulated over the session.
bool __cdecl cfunc_accumulate_scores(script::Struct*, script::Script*)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    PlayerSearch unused; // (made and dropped by the original)
    (void)unused;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false))
        player->add_to_accumulated_score(total_score(get_score_of(player->skater_)));
    return true;
}

// 0x005AC850: EliminateLastPlacePlayer - with more than one player still in: if all have the same score, everyone is
// told nobody goes (-1); otherwise each player on the lowest score is marked eliminated and everyone (observers too)
// told its skater's id.
bool __cdecl cfunc_eliminate_last_place_player(script::Struct*, script::Script*)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    PlayerSearch others;
    int lowest = -1;
    int highest = 0;
    int in_game = 0;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false)) {
        if (player->is_eliminated())
            continue;
        ++in_game;
        uint8_t* score = get_score_of(player->skater_);
        if (lowest == -1 || total_score(score) < lowest)
            lowest = total_score(score);
        if (total_score(score) > highest)
            highest = total_score(score);
    }
    if (in_game <= 1)
        return true;
    net::NetMessageInfo desc;
    uint32_t data;
    if (highest == lowest) {
        desc.make_new();
        data = 0xFFFFFFFF;
        prepare_eliminated(&desc, &data);
        send_to_players(manager, &desc, false);
        return true;
    }
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false)) {
        if (player->is_eliminated() || total_score(get_score_of(player->skater_)) != lowest)
            continue;
        desc.make_new();
        player->mark_eliminated();
        data = player->skater_->ident_;
        prepare_eliminated(&desc, &data);
        send_to_players(manager, &desc, true);
    }
    return true;
}

// 0x005ACAA0: OnlyOnePlayerLeft - a team game: whether at most one team has players not eliminated; otherwise
// whether at most one player isn't (then num_players: how many).
bool __cdecl cfunc_only_one_player_left(script::Struct*, script::Script* script)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    if (is_squad_match()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
             player = step_roster_sweep(manager, &search, false)) {
            if (!player->is_eliminated())
                ++teams[player->team_];
        }
        int with_players = 0;
        for (int count : teams)
            with_players += count > 0 ? 1 : 0;
        return with_players <= 1;
    }
    int players = 0;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false)) {
        if (!player->is_eliminated())
            ++players;
    }
    if (players > 1)
        return false;
    script->args_->put_int("num_players", players);
    return true;
}

// 0x005ACC20: AnySkaterTotalScoreAtLeast <score> (0 if missing) - whether any player's (a team game: any team's)
// total score reaches it.
bool __cdecl cfunc_any_skater_total_score_at_least(script::Struct* params, script::Script*)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    int target = integer_or_zero(params);
    if (is_squad_match()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
             player = step_roster_sweep(manager, &search, false)) {
            uint8_t* score = get_score_of(player->skater_);
            teams[player->team_] += total_score(score);
        }
        for (int total : teams) {
            if (total >= target)
                return true;
        }
        return false;
    }
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false)) {
        if (player->skater_ && total_score(get_score_of(player->skater_)) >= target)
            return true;
    }
    return false;
}

// 0x005ACD80: OnlyOneSkaterLeft - how many skaters (a team game: teams) have scored: false while players are still
// joining; with none scored the server in host mode 1 or 2 says true; otherwise whether exactly one has. (Not a team
// game: also false with exactly one skater.)
bool __cdecl cfunc_only_one_skater_left(script::Struct*, script::Script*)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    NewPlayerSearch joining;
    int scored = 0;
    if (is_squad_match()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
             player = step_roster_sweep(manager, &search, false)) {
            uint8_t* score = get_score_of(player->skater_);
            teams[player->team_] += total_score(score);
        }
        for (int total : teams)
            scored += total > 0 ? 1 : 0;
        if (manager->begin_joiner_walk(&joining))
            return false;
    } else {
        int skaters = 0;
        for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
             player = step_roster_sweep(manager, &search, false)) {
            if (!player->skater_)
                continue;
            ++skaters;
            if (total_score(get_score_of(player->skater_)) > 0)
                ++scored;
        }
        if (manager->begin_joiner_walk(&joining) || skaters == 1)
            return false;
    }
    if (manager->get_on_server() && (manager->get_hosting_role() == 1 || manager->get_hosting_role() == 2) &&
        scored == 0)
        return true;
    return scored == 1;
}

// 0x005ACF50 / 0x005ACFA0: SkaterTotalScoreGreaterThan / less_than <score> (0 if missing) - the local skater's total.
bool __cdecl cfunc_skater_total_score_greater_than(script::Struct* params, script::Script*)
{
    int value = integer_or_zero(params);
    objects::GameEntity* skater = get_home_rider();
    if (!skater)
        return false;
    uint8_t* score = get_score_of(skater);
    return score && total_score(score) > value;
}

bool __cdecl cfunc_skater_total_score_less_than(script::Struct* params, script::Script*)
{
    int value = integer_or_zero(params);
    objects::GameEntity* skater = get_home_rider();
    if (!skater)
        return false;
    uint8_t* score = get_score_of(skater);
    return score && total_score(score) < value;
}

// 0x005ACFF0: SkaterCurrentScorePotGreaterThan <score> [point_scale=<n>] - the local skater's score pot above score
// times point_scale (1). 0x005AD070: SkaterCurrentScorePotLessThan <score> (no scale).
bool __cdecl cfunc_skater_current_score_pot_greater_than(script::Struct* params, script::Script*)
{
    int scale = 1;
    int value = integer_or_zero(params);
    params->fetch_int_value(kPointScale, &scale, 0);
    value *= scale;
    objects::GameEntity* skater = get_home_rider();
    if (!skater)
        return false;
    uint8_t* score = get_score_of(skater);
    return score && score_pot(score) > value;
}

bool __cdecl cfunc_skater_current_score_pot_less_than(script::Struct* params, script::Script*)
{
    int value = integer_or_zero(params);
    objects::GameEntity* skater = get_home_rider();
    if (!skater)
        return false;
    uint8_t* score = get_score_of(skater);
    return score && score_pot(score) < value;
}

// 0x005AD0D0: SkaterGetScoreInfo - the local skater's scorepot, totalscore and lastscorelanded. (Without a local
// skater the original reads a null score: a crash, kept.)
bool __cdecl cfunc_skater_get_score_info(script::Struct*, script::Script* script)
{
    objects::GameEntity* skater = get_home_rider();
    uint8_t* score = skater ? get_score_of(skater) : nullptr;
    script->args_->put_int(kScorePot, score_pot(score));
    script->args_->put_int(kTotalScore, total_score(score));
    script->args_->put_int(kLastScoreLandedInfo, last_score_landed(score));
    return true;
}

// 0x005AD140: InFirstPlace - whether no other player still in has a higher total than the local player (who must be
// in the game - not eliminated, surveying or observing - with a skater, and with at least one other player in);
// points: the best other player's total.
bool __cdecl cfunc_in_first_place(script::Struct*, script::Script* script)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    int better = 0;
    int best = 0;
    auto* local = static_cast<PlayerRecord*>(manager->get_own_player());
    if (!local || local->is_eliminated() || local->is_scouting_level() || local->is_observer() || !local->skater_)
        return false;
    int mine = total_score(component_score_of(local->skater_));
    int in_game = 0;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false)) {
        if (player->is_eliminated())
            continue;
        ++in_game;
        uint8_t* score = component_score_of(player->skater_);
        if (total_score(score) > mine)
            ++better;
        if (!player->is_own_player() && total_score(score) > best)
            best = total_score(score);
    }
    if (in_game < 2)
        return false;
    script->args_->put_int("points", best);
    return better == 0;
}

// 0x005AD2D0: InLastPlace - whether no other player still in has a lower total than the local player (as
// InFirstPlace); then points: the lowest other player's total.
bool __cdecl cfunc_in_last_place(script::Struct*, script::Script* script)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    int worse = 0;
    int lowest = -1;
    auto* local = static_cast<PlayerRecord*>(manager->get_own_player());
    if (!local || local->is_eliminated() || local->is_scouting_level() || local->is_observer() || !local->skater_)
        return false;
    int mine = total_score(component_score_of(local->skater_));
    int in_game = 0;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false)) {
        if (player->is_eliminated())
            continue;
        ++in_game;
        uint8_t* score = component_score_of(player->skater_);
        if (total_score(score) < mine)
            ++worse;
        if (player->is_own_player())
            continue;
        int total = total_score(score);
        if (lowest == -1)
            lowest = total;
        else if (total < lowest)
            lowest = total_score(score);
    }
    if (in_game < 2 || worse != 0)
        return false;
    script->args_->put_int("points", lowest);
    return true;
}

// 0x005AD450: InSecondToLastPlace - whether exactly one player still in has a lower total than the local player (as
// InFirstPlace); then points: the local total less the lowest total (the local player's own counted).
bool __cdecl cfunc_in_second_to_last_place(script::Struct*, script::Script* script)
{
    online::Hub* manager = net_session_slot();
    PlayerSearch search;
    int worse = 0;
    int lowest = -1;
    auto* local = static_cast<PlayerRecord*>(manager->get_own_player());
    if (!local || local->is_eliminated() || local->is_scouting_level() || local->is_observer() || !local->skater_)
        return false;
    int mine = total_score(component_score_of(local->skater_));
    int in_game = 0;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, false); player;
         player = step_roster_sweep(manager, &search, false)) {
        if (player->is_eliminated())
            continue;
        ++in_game;
        uint8_t* score = component_score_of(player->skater_);
        if (total_score(score) < mine)
            ++worse;
        if (lowest == -1)
            lowest = total_score(score);
        else if (total_score(score) < lowest)
            lowest = total_score(score);
    }
    if (in_game < 2 || worse != 1)
        return false;
    script->args_->put_int("points", mine - lowest);
    return true;
}

// 0x005AD5C0 / 0x005AD610: GoalsGreaterThan / GoalsEqualTo <n> - the career's goals got in all (false without n).
bool __cdecl cfunc_goals_greater_than(script::Struct* params, script::Script*)
{
    float goals;
    if (!params->fetch_float(0u, &goals, 0))
        return false;
    return static_cast<double>(get_career()->count_total_goals()) > goals;
}

bool __cdecl cfunc_goals_equal_to(script::Struct* params, script::Script*)
{
    float goals;
    if (!params->fetch_float(0u, &goals, 0))
        return false;
    return static_cast<double>(get_career()->count_total_goals()) == goals;
}

// 0x005AD660: ToggleSkaterCamMode [skater=<n>] - skater n's (none or 0: the local player's) skater camera's mode
// toggled (0x004FA7D0).
bool __cdecl cfunc_toggle_skater_cam_mode(script::Struct* params, script::Script*)
{
    int index = 0; // (uninitialised in the original when missing: the game's scripts always give skater=0)
    params->fetch_int_value(kSkater, &index, 0);
    objects::GameEntity* skater;
    if (index == 0) {
        auto* local = static_cast<PlayerRecord*>(net_session_slot()->get_own_player());
        if (!local)
            return true;
        skater = local->skater_;
    } else {
        skater = get_rider_at(index);
    }
    if (skater) {
        auto* camera = static_cast<objects::GameEntity*>(static_cast<objects::Rider*>(skater)->get_skater_camera());
        static_cast<objects::RiderChaseView*>(camera->fetch_module(kSkaterCamera))->next_mode();
    }
    return true;
}

// 0x005AD6C0: GetSkaterID [skater=<n>] - skater n's (none: the local skater's) object id as objID, if it's local.
bool __cdecl cfunc_get_skater_id(script::Struct* params, script::Script* script)
{
    int index = 0;
    objects::GameEntity* skater = params->fetch_int_value(kSkater, &index, 0) ? get_rider_at(index) : get_home_rider();
    if (!skater || !skater->unknownbc_)
        return false;
    script->args_->put_name_key(kObjId, skater->ident_);
    return true;
}

// 0x005AD730: SkaterExists skater=<n> - whether skater n exists; its object id as objID.
bool __cdecl cfunc_skater_exists(script::Struct* params, script::Script* script)
{
    int index = 0; // (uninitialised in the original)
    if (!params->fetch_int_value(kSkater, &index, 1))
        return false;
    objects::GameEntity* skater = get_rider_at(index);
    if (!skater)
        return false;
    script->args_->put_name_key(kObjId, skater->ident_);
    return true;
}

// 0x005AD780: GetSkaterSex player=<n> - player n's profile's is_male.
bool __cdecl cfunc_get_skater_sex(script::Struct* params, script::Script* script)
{
    int player = 0; // (uninitialised in the original, as is_male)
    params->fetch_int_value(kPlayer, &player, 1);
    objects::RiderIdentity* profile = objects::g_profile_roster->fetch_account(player);
    int male = 0;
    profile->details_params()->fetch_int_value(kIsMale, &male, 1);
    script->args_->put_int(kIsMale, male);
    return true;
}

// 0x005AD7E0: GetCurrentSkaterID - the script's object's id as objID and intId, if it's a local skater (any object
// with the composite object's +0xBC set).
bool __cdecl cfunc_get_current_skater_id(script::Struct*, script::Script* script)
{
    auto* object = static_cast<objects::GameEntity*>(script->entity_.get_pull());
    if (!object || !object->unknownbc_)
        return false;
    uint32_t id = object->ident_;
    script->args_->put_name_key(kObjId, id);
    script->args_->put_int(kIntId, static_cast<int>(id));
    return true;
}

// 0x005B0070: GetSkaterPosition - the script's object's position as integers x, y, z (truncated); false without an
// object.
bool __cdecl cfunc_get_skater_position(script::Struct*, script::Script* script)
{
    auto* object = static_cast<objects::GameEntity*>(script->entity_.get_pull());
    if (!object)
        return false;
    float y = object->at_point_.y, z = object->at_point_.z;
    script->args_->put_int("x", static_cast<int>(static_cast<int64_t>(object->at_point_.x))); // (the original's _ftol)
    script->args_->put_int("y", static_cast<int>(static_cast<int64_t>(y)));
    script->args_->put_int("z", static_cast<int>(static_cast<int64_t>(z)));
    return true;
}

// 0x005B00F0: SetSkaterVelocity <speed> | vel_x= vel_y= vel_z= - the script's object's velocity: its direction kept
// at the speed given (all four components scaled), or the vector given (w 1; built on the heap and freed, as the
// original's new maths::Vec4). False without an object.
bool __cdecl cfunc_set_skater_velocity(script::Struct* params, script::Script* script)
{
    auto* object = static_cast<objects::GameEntity*>(script->entity_.get_pull());
    if (!object)
        return false;
    float speed = 0.0f;
    if (params->fetch_float(0u, &speed, 0)) {
        maths::Vec4 direction = object->vel_;
        direction.make_unit();
        object->vel_.x = direction.x * speed;
        object->vel_.y = direction.y * speed;
        object->vel_.z = direction.z * speed;
        object->vel_.w = direction.w * speed;
        return true;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    params->fetch_float(kVelX, &x, 1);
    params->fetch_float(kVelY, &y, 1);
    params->fetch_float(kVelZ, &z, 1);
    if (!memory::g_manager_ready)
        memory::ensure_manager();
    auto* velocity = static_cast<maths::Vec4*>(memory::Hub::get_pull()->carve(sizeof(maths::Vec4), 1, nullptr));
    velocity->x = x;
    velocity->y = y;
    velocity->z = z;
    velocity->w = 1.0f;
    object->vel_ = *velocity;
    memory::Hub::get_pull()->dealloc(velocity);
    return true;
}

} // namespace commands
