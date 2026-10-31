// The skater's state history component and its network collisions. See state_history.h.

#include "game/rider/state_history.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "game/rider/walk_physics.h"
#include "platform/stopwatch.h"
#include "engine/script/game_util_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/rider/core_physics.h"
#include "game/rider/state_history.h"

namespace objects {

namespace {

// Components of the skater this code asks for.
constexpr uint32_t kSkaterState = 0x43686585;          // +0x70 in a bail, +0x118 driving
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // +0x25 a flag; +0x2C its state component
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kWalk = 0x726E85AA;
constexpr uint32_t kInput = 0x27D7CD28;

// Game types (the game mode's 0x005532A0).
constexpr uint32_t kGameTag = 0x6EF8FDA0;
constexpr uint32_t kGameTag2 = 0x5D32129C;
constexpr uint32_t kGameTeamTag = 0x6C5FF266;

// Network messages.
constexpr uint8_t kMsgRanInto = 0x43;  // to the player run into: 0x14 bytes (the other's id | driving, the push)
constexpr uint8_t kMsgRanIntoAck = 0x44; // to the one running into it: 1 byte (the other's id | driving)
constexpr uint8_t kMsgHit = 0x84;
constexpr uint8_t kMsgHitAck = 0x85;
constexpr uint8_t kMsgTagged = 0x55;     // a game of tag: who was tagged (1 byte), to every other player
constexpr uint8_t kMsgTeamTagged = 0x73; // team tag, the same team: who (and the team), 2 bytes
constexpr uint8_t kMsgTeamConverted = 0x74; // team tag, another team

// The default name in the events: "Someone" (0x0064C1C4); the names the code looks up.
const char* const kSomeone = "Someone";
const char* const kUnbindFireballTricks = "unbind_fireball_tricks"; // "unbind_fireball_tricks"
const char* const kDrivingRadiusBoost = "driving_radius_boost";   // "driving_radius_boost"

// a - b as the original's x87 code rounds it when it stores it.
float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// --- the network game manager (0x007CCE14), its players and the game mode ---
online::Hub* net_session_slot() { return online::g_online_hub; }
void* get_game_mode() { return static_cast<void*>(goals::g_game_mode); }
void* skater_manager() { return static_cast<void*>(modules::g_skate_module); }

uint8_t* find_player_slot_by_obj(int id) { return net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(id)); }
uint8_t* start_roster_sweep(PlayerSearchDestructors* search)
{
    return static_cast<uint8_t*>(net_session_slot()->begin_player_walk(search, true));
}
uint8_t* step_roster_sweep(PlayerSearchDestructors* search)
{
    return static_cast<uint8_t*>(net_session_slot()->step_player_walk(search, true));
}
bool get_on_server() { return net_session_slot()->get_on_server(); }
bool manager_flag4() { return net_session_slot()->in_internet_mode(); }
bool manager_may_collide() { return net_session_slot()->players_can_collide(); }
bool manager_flag_3a() { return net_session_slot()->has_match_ended(); }
// The manager's +0x6C (the network code's get_crown_holder, 0x00533210).
GameEntity* local_skater() { return static_cast<GameEntity*>(net_session_slot()->get_crown_holder()); }
// The manager's server app (+0x04: the folded getter 0x005480C0).
net::AppHost* manager_app() { return net_session_slot()->host_; }

// A player (PlayerRecord): its object (+0x14), team field (+0x30) and flags (+0x1D0: 0x1 local, 0x80 "it" - holds_crown,
// 0x7800 a team - is_carrying_flag / get_flag_team). Its announcements: give_crown makes it "it", note_flag_returned tags it
// on its own team, note_flag_steal converts it to another.
online::PlayerRecord* details_of(uint8_t* player) { return reinterpret_cast<online::PlayerRecord*>(player); }
GameEntity* player_object(uint8_t* player) { return prop<GameEntity*>(player, 0x14); }
bool is_observing(uint8_t* player) { return details_of(player)->is_invulnerable(); }
bool is_it(uint8_t* player) { return details_of(player)->holds_crown() != 0; }
bool on_a_team(uint8_t* player) { return details_of(player)->is_carrying_flag() != 0; }
int get_team(uint8_t* player) { return details_of(player)->get_flag_team(); }
bool is_host_local(uint8_t* player) { return details_of(player)->is_own_player() != 0; }
int get_connection(uint8_t* player) { return details_of(player)->get_link_handle(); }
void mark_tag_time(uint8_t* player) { details_of(player)->mark_just_joined(); }
void make_it(uint8_t* player, bool it) { details_of(player)->give_crown(it); }
void tag_on_team(uint8_t* player) { details_of(player)->note_flag_returned(); }
void convert_to_team(uint8_t* player, int team) { details_of(player)->note_flag_steal(team); }

uint32_t get_game_type() { return static_cast<goals::MatchRules*>(get_game_mode())->get_name_key(); }
bool is_squad_play() { return static_cast<goals::MatchRules*>(get_game_mode())->is_squad_match() != 0; }
bool tag_game_over() { return static_cast<goals::MatchRules*>(get_game_mode())->finish_rules_reached(); }
bool in_slap_game() { return commands::cfunc_in_slap_game(nullptr, nullptr); }

GameEntity* skater_by_index(int index)
{
    return static_cast<modules::Skate*>(skater_manager())->get_rider_at(index);
}
const char* get_skater_name(GameEntity* skater) { return static_cast<Rider*>(skater)->fetch_shown_name(); }
// A skater's score (its +0x278's +0x18).
void* skater_score(GameEntity* skater) { return prop<void*>(prop<void*>(skater, 0x278), 0x18); }
int get_points_total(void* score) { return static_cast<modules::PointTally*>(score)->get_total_score(); }
void set_points_total(void* score, int value) { static_cast<modules::PointTally*>(score)->set_grand_total(value); }

// Knocked over by `other`: a bail on the core physics, or the walk component's when walking or driving.
void knock_down(ObjectModule* physics, GameEntity* other)
{
    reinterpret_cast<skater::SkaterPhysicsModule*>(physics)->reset_to_air(other);
}
void knock_down_walking(ObjectModule* walk, GameEntity* other)
{
    static_cast<FootMovement*>(walk)->go_to_air_for(other);
}

// A vector's length as 0x004023C0 leaves it on the x87 stack (x, y, then z; unrounded).
double length_of(const maths::Vec4& v)
{
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
}

// The knock-back velocity both handlers give: the unit direction away from the other times 300, then 1.5, each
// component rounded to a float after each (except where noted), and 200 up.
constexpr float kKnockBack = 300.0f;  // 0x0064675C
constexpr float kKnockScale = 1.5f;   // 0x00646744
constexpr float kKnockUp = 200.0f;
constexpr float kTooClose = 0.0001f;  // 0x006453D4

// strncpy(name, from, 0x20) (the game's C runtime's 0x006239E0): up to 0x20 characters, the rest zeroed.
void copy_name(char (&name)[0x20], const char* from)
{
    size_t i = 0;
    for (; i < sizeof(name) && from[i]; ++i)
        name[i] = from[i];
    for (; i < sizeof(name); ++i)
        name[i] = 0;
}

// Both handlers' event: the other's name (and what the caller adds) to the skater.
void send_event(GameEntity* skater, uint32_t type, const char* name, uint32_t flag, bool with_flag,
               const script::VectorValue* push)
{
    script::Struct params;
    params.prepare_all();
    params.append_word(0xA4B08520, name); // name
    if (push)
        params.put_vec3(0xA6F5352F, *push);
    if (with_flag)
        params.put_name_key(0u, flag);
    skater->post_own_event(type, &params);
    params.get_clear();
}

} // namespace

// --- StateHistoryMessage ---

// 0x00519800
uint32_t StateHistoryMessage::get_time() { return static_cast<uint32_t>(time_high_) << 16 | clock_; }

// 0x00519810
void StateHistoryMessage::change_moment(uint32_t time)
{
    clock_ = static_cast<uint16_t>(time);
    time_high_ = static_cast<uint16_t>(time >> 16);
}

// --- PlayerSearchDestructors ---

// 0x00519830
void PlayerSearchDestructors::destroy_body() { vtable_ = modules::kPlayerSearchVtable; }

// 0x0051A120
PlayerSearchDestructors* PlayerSearchDestructors::teardown(unsigned int flags)
{
    vtable_ = modules::kPlayerSearchVtable;
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// --- RiderFrameLog ---

// 0x0051AD30
ObjectModule* __cdecl RiderFrameLog::fabricate()
{
    void* memory = memory::alloc_zeroed(0x1108);
    return memory ? static_cast<RiderFrameLog*>(memory)->make_new() : nullptr;
}

// 0x0051ACB0
RiderFrameLog* RiderFrameLog::make_new()
{
    ObjectModule::make_new();
    vtable_ = kStateHistoryVtable;
    // (The original clears each state entry's +0x68 / +0x6C and the entry, the messages, the counts, then the
    // entries and the messages again.)
    std::memset(reinterpret_cast<uint8_t*>(this) + 0x1C, 0, kEntries * 0x84);
    std::memset(reinterpret_cast<uint8_t*>(this) + 0xA70, 0, kEntries * sizeof(StateHistoryMessage));
    prop<uint32_t>(this, 0x1100) = 0;
    prop<int>(this, 0x18) = 0;  // the states received
    prop<int>(this, 0xA6C) = 0; // the messages received
    kind_tag_ = kType;
    return this;
}

// 0x00519840
RiderFrameLog* RiderFrameLog::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00519FA0
void RiderFrameLog::get_frame_movement(maths::Vec4* out)
{
    const maths::Vec4& vel = entity_->vel_;
    double frame = frame_clock::get_frame_seconds();
    float rounded = static_cast<float>(frame);
    out->x = rounded * vel.x;
    out->y = rounded * vel.y;
    out->z = rounded * vel.z;
    out->w = static_cast<float>(frame * vel.w);
}

// 0x00519FF0
void RiderFrameLog::fetch_place(maths::Vec4* out) { *out = entity_->at_point_; }

// 0x0051A020
double RiderFrameLog::get_collision_reach(bool driving)
{
    float reach = script::fetch_float(entity_->unknownbc_ ? 0x0D24273Bu : 0x25F78481u);
    if (!driving)
        return reach;
    return (script::fetch_float(0x0A49FF23u) + 1.0) * reach;
}

// 0x0051A080
double RiderFrameLog::fetch_hit_radius(bool my_driving, bool their_driving)
{
    uint32_t name;
    if (entity_->unknownbc_)
        name = manager_flag4() ? 0xF2E35FA3u : 0xECB41860u;
    else
        name = manager_flag4() ? 0xDBD6D58Au : 0xC5819249u;
    float radius = script::fetch_float(name);
    float drivers = my_driving ? 1.0f : 0.0f;
    if (their_driving)
        drivers = static_cast<float>(drivers + 1.0);
    return (script::fetch_float(0xEEF4AFB3u) * static_cast<double>(drivers) + 1.0) * radius;
}

// 0x0051A150
bool RiderFrameLog::is_near_local_skater()
{
    GameEntity* local = local_skater();
    const maths::Vec4& pos = entity_->at_point_;
    float dx = get_difference(pos.x, local->at_point_.x);
    double dy = static_cast<double>(pos.y) - local->at_point_.y;
    double dz = static_cast<double>(pos.z) - local->at_point_.z;
    double distance = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx);
    return distance < 96.0f; // 0x0064EC40
}

// 0x0051A1C0
void RiderFrameLog::check_for_collisions(int first)
{
    uint8_t* me = find_player_slot_by_obj(static_cast<int>(entity_->ident_));
    if (!me || is_observing(me))
        return;
    if (!manager_may_collide() && !is_it(me) && !on_a_team(me))
        return;
    ObjectModule* my_state = entity_->fetch_module(kSkaterState);
    // Whether this machine tells the players of a hit (not when the skater is in a bail, outside a slap game).
    bool tell_players;
    if (!prop<bool>(my_state, 0x70) || in_slap_game()) {
        tell_players = true;
    } else {
        tell_players = false;
        if (!is_it(me) && !on_a_team(me))
            return;
    }
    bool my_driving = prop<bool>(my_state, 0x118);
    maths::Vec4 my_pos = entity_->at_point_;
    maths::Vec4 my_move;
    get_frame_movement(&my_move);
    maths::Vec4 my_dir = my_move;
    my_dir.make_unit();
    maths::Seg mine;
    mine.start = my_pos;

    for (int i = first; i < 8; ++i) {
        GameEntity* skater = skater_by_index(i);
        if (!skater || skater == entity_)
            continue;
        ObjectModule* their_state = skater->fetch_module(kSkaterState);
        if (prop<bool>(their_state, 0x70))
            continue;
        uint8_t* them = find_player_slot_by_obj(static_cast<int>(skater->ident_));
        if (is_observing(them) || is_it(them))
            continue;
        if (on_a_team(me) && on_a_team(them) && get_team(me) != prop<int>(them, 0x30))
            continue;
        if (is_squad_play() && prop<int>(them, 0x30) == prop<int>(me, 0x30))
            continue;

        auto* their_history = prop<RiderFrameLog*>(skater, 0x27C);
        bool their_driving = prop<bool>(their_state, 0x118);
        maths::Vec4 their_pos;
        their_history->fetch_place(&their_pos);
        maths::Vec4 their_move;
        their_history->get_frame_movement(&their_move);
        maths::Vec4 their_dir = their_move;
        their_dir.make_unit();
        if (length_of(their_move) > 100.0f) // 0x006463D4 (NaN carries on)
            continue;

        float radius = static_cast<float>(fetch_hit_radius(my_driving, their_driving));
        // (The original looks driving_radius_boost up for each driver and drops it.)
        if (my_driving)
            script::fetch_float(kDrivingRadiusBoost);
        if (their_driving)
            script::fetch_float(kDrivingRadiusBoost);
        mine.end = mine.start + my_dir * static_cast<float>(get_collision_reach(my_driving));
        maths::Seg theirs;
        theirs.start = their_pos;
        theirs.end = theirs.start + their_dir * static_cast<float>(their_history->get_collision_reach(their_driving));

        // Where they meet: the segments' closest points, else the first of the four pairs of ends close enough.
        maths::Vec4 my_point, their_point;
        float scratch;
        if (maths::closest_points_between_lines(mine, theirs, my_point, their_point, scratch, scratch, true)) {
            if (!(length_of(my_point - their_point) < radius))
                continue;
        } else if (length_of(theirs.start - mine.start) < radius) {
            their_point = theirs.start;
            my_point = mine.start;
        } else if (length_of(theirs.start - mine.end) < radius) {
            their_point = theirs.start;
            my_point = mine.end;
        } else if (length_of(theirs.end - mine.start) < radius) {
            their_point = theirs.end;
            my_point = mine.start;
        } else if (length_of(theirs.end - mine.end) < radius) {
            their_point = theirs.end;
            my_point = mine.end;
        } else {
            continue;
        }

        // Who hits whom: the faster skater (a driver beats one on foot); otherwise only the game's "it" does.
        bool must_be_it;
        if (my_driving == their_driving) {
            float my_speed = static_cast<float>(length_of(my_move));
            must_be_it = length_of(their_move) < my_speed;
        } else {
            must_be_it = my_driving && !their_driving;
        }
        if (must_be_it && !is_it(me))
            continue;

        net::AppHost* app = manager_app();
        if (tell_players) {
            net::NetMessageInfo msg;
            msg.make_new();
            struct {
                uint8_t id;
                uint8_t pad[3];
                maths::Vec4 push;
            } ran_into = {};
            ran_into.id = static_cast<uint8_t>(their_driving * 0x80 | static_cast<uint8_t>(skater->ident_));
            ran_into.push = my_point - their_point;
            if (ObjectModule* input = entity_->fetch_module(kInput))
                prop<uint8_t>(input, 0x498) = 1;
            mark_tag_time(me);
            msg.ident_ = kMsgRanInto;
            msg.content_ = &ran_into;
            msg.byte_count_ = 0x14;
            msg.queue_ = net::NetMessageInfo::kLaneOrdered;
            msg.group_id_ = 8;
            app->put_on_outbox(get_connection(me), &msg);
            uint8_t ran_into_by = static_cast<uint8_t>(my_driving * 0x80 | static_cast<uint8_t>(entity_->ident_));
            msg.ident_ = kMsgRanIntoAck;
            msg.content_ = &ran_into_by;
            msg.byte_count_ = 1;
            app->put_on_outbox(get_connection(them), &msg);
            if (in_slap_game()) {
                void* score = skater_score(skater);
                set_points_total(score, get_points_total(score) + 1);
            }
        }

        uint32_t type = get_game_type();
        if (type == kGameTag || get_game_type() == kGameTag2) {
            if (tag_game_over())
                return;
            if (!is_it(me))
                continue;
            make_it(them, true);
            PlayerSearchDestructors search{modules::kPlayerSearchVtable, nullptr};
            for (uint8_t* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search)) {
                if (is_host_local(player))
                    continue;
                net::NetMessageInfo msg;
                msg.make_new();
                uint8_t tagged = static_cast<uint8_t>(skater->ident_);
                msg.content_ = &tagged;
                msg.ident_ = kMsgTagged;
                msg.byte_count_ = 1;
                msg.queue_ = net::NetMessageInfo::kLaneOrdered;
                msg.group_id_ = 8;
                app->put_on_outbox(get_connection(player), &msg);
            }
            search.vtable_ = modules::kPlayerSearchVtable;
        } else if (get_game_type() == kGameTeamTag) {
            if (manager_flag_3a())
                return;
            if (!on_a_team(me))
                continue;
            net::NetMessageInfo msg;
            msg.make_new();
            uint8_t tagged[2];
            tagged[0] = static_cast<uint8_t>(skater->ident_);
            tagged[1] = static_cast<uint8_t>(get_team(me));
            bool same_team = get_team(me) == prop<int>(them, 0x30);
            PlayerSearchDestructors search{modules::kPlayerSearchVtable, nullptr};
            for (uint8_t* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search)) {
                if (is_host_local(player)) {
                    if (same_team)
                        tag_on_team(them);
                    else
                        convert_to_team(them, get_team(me));
                    continue;
                }
                msg.ident_ = same_team ? kMsgTeamTagged : kMsgTeamConverted;
                msg.content_ = tagged;
                msg.group_id_ = 8;
                msg.queue_ = net::NetMessageInfo::kLaneOrdered;
                msg.byte_count_ = 2;
                app->put_on_outbox(get_connection(player), &msg);
            }
            search.vtable_ = modules::kPlayerSearchVtable;
        }
    }
}

// 0x00519870
int __cdecl RiderFrameLog::handle_hit_messages(net::InboundMsgCtx* context)
{
    uint8_t* msg = reinterpret_cast<uint8_t*>(context->body_);
    bool other_driving = msg[1] != 0;
    auto* skater = static_cast<GameEntity*>(context->content_);
    char name[0x20] = {};
    std::memcpy(name, kSomeone, 8);
    if (context->type_code_ == kMsgHit) {
        if (uint8_t* player = find_player_slot_by_obj(static_cast<int8_t>(msg[0]))) {
            GameEntity* other = player_object(player);
            // Away from the other skater (straight along x and z if they're on top of each other).
            double dx = static_cast<double>(skater->at_point_.x) - other->at_point_.x;
            float dy = get_difference(skater->at_point_.y, other->at_point_.y);
            float dz = get_difference(skater->at_point_.z, other->at_point_.z);
            float dw = get_difference(skater->at_point_.w, other->at_point_.w);
            double length = std::sqrt((dx * dx + static_cast<double>(dy) * dy) + static_cast<double>(dz) * dz);
            double x = 1.0, y = 0.0, z = 1.0, w = 1.0;
            if (!(length < kTooClose)) {
                float inverse = static_cast<float>(1.0 / length);
                x = dx * inverse;
                y = static_cast<double>(inverse) * dy;
                z = static_cast<double>(inverse) * dz;
                w = static_cast<double>(inverse) * dw;
            }
            float wk = static_cast<float>(w * kKnockBack), zk = static_cast<float>(z * kKnockBack);
            float yk = static_cast<float>(y * kKnockBack), xk = static_cast<float>(x * kKnockBack);
            skater->vel_.x = xk * kKnockScale;
            skater->vel_.y = yk * kKnockScale;
            skater->vel_.z = zk * kKnockScale;
            skater->vel_.w = wk * kKnockScale;
            skater->vel_.y = kKnockUp;
            ObjectModule* control = skater->fetch_module(kSkaterPhysicsControl);
            uint8_t* state = prop<uint8_t*>(control, 0x2C);
            if (!prop<bool>(state, 0x118) && prop<int>(state, 0x1C) == 0)
                knock_down(skater->fetch_module(kSkaterCorePhysics), other);
            else
                knock_down_walking(skater->fetch_module(kWalk), other);
            copy_name(name, get_skater_name(player_object(player)));
            if (!get_on_server()) {
                void* score = skater_score(skater);
                int lost = prop<int>(msg, 4);
                if (get_points_total(score) - lost < 0) {
                    set_points_total(score, 0);
                    script::launch_script(kUnbindFireballTricks, nullptr, nullptr, false);
                } else {
                    set_points_total(score, get_points_total(score) - lost);
                }
            }
        }
        send_event(skater, 0x915E5E39, name, other_driving ? 0xD039432Cu : 0xC304BD92u, true, nullptr);
    } else if (context->type_code_ == kMsgHitAck) {
        if (uint8_t* player = find_player_slot_by_obj(static_cast<int8_t>(msg[0])))
            copy_name(name, get_skater_name(player_object(player)));
        send_event(skater, 0xA1021AF0, name, other_driving ? 0xD039432Cu : 0xC304BD92u, true, nullptr);
    }
    return net::kResultConsumed;
}

// 0x00519C10
int __cdecl RiderFrameLog::handle_collision_messages(net::InboundMsgCtx* context)
{
    auto* skater = static_cast<GameEntity*>(context->content_);
    uint8_t id = context->type_code_;
    if (id != kMsgRanInto && id != kMsgRanIntoAck)
        return net::kResultPassOn;
    uint8_t* msg = reinterpret_cast<uint8_t*>(context->body_);
    char name[0x20] = {};
    std::memcpy(name, kSomeone, 8);
    bool other_driving = (msg[0] >> 7) & 1;
    msg[0] &= 0x7F;
    uint8_t* player = find_player_slot_by_obj(static_cast<int8_t>(msg[0]));
    if (id == kMsgRanIntoAck) {
        if (player)
            copy_name(name, get_skater_name(player_object(player)));
        send_event(skater, 0xA1021AF0, name, 0x7FD0663D, other_driving, nullptr);
        return net::kResultPassOn;
    }
    if (player) {
        ObjectModule* control = skater->fetch_module(kSkaterPhysicsControl);
        if (!prop<bool>(prop<uint8_t*>(control, 0x2C), 0x118)) {
            GameEntity* other = player_object(player);
            double dw = static_cast<double>(skater->at_point_.w) - other->at_point_.w;
            double dz = static_cast<double>(skater->at_point_.z) - other->at_point_.z;
            double dy = static_cast<double>(skater->at_point_.y) - other->at_point_.y;
            float away[4] = {get_difference(skater->at_point_.x, other->at_point_.x), static_cast<float>(dy), static_cast<float>(dz),
                             static_cast<float>(dw)};
            double length = std::sqrt((static_cast<double>(away[0]) * away[0] + dy * dy) + dz * dz);
            double w, z;
            if (!(length < kTooClose)) {
                // (0x00423650: each component times 1 / the length rounded to a float)
                double inverse = 1.0 / static_cast<float>(length);
                for (float& c : away)
                    c = static_cast<float>(inverse * c);
                z = away[2];
                w = away[3];
            } else {
                away[0] = 1.0f;
                away[1] = 0.0f;
                z = 1.0;
                w = 1.0;
            }
            double wk = w * kKnockBack; // (kept unrounded)
            float zk = static_cast<float>(z * kKnockBack);
            float yk = away[1] * kKnockBack;
            float xk = away[0] * kKnockBack;
            skater->vel_.x = xk * kKnockScale;
            skater->vel_.y = yk * kKnockScale;
            skater->vel_.z = zk * kKnockScale;
            skater->vel_.w = static_cast<float>(wk * kKnockScale);
            skater->vel_.y = kKnockUp;
            uint8_t* state = prop<uint8_t*>(control, 0x2C);
            if (!prop<bool>(state, 0x118) && prop<int>(state, 0x1C) == 0 && !prop<bool>(control, 0x25))
                knock_down(skater->fetch_module(kSkaterCorePhysics), other);
            else
                knock_down_walking(skater->fetch_module(kWalk), other);
        }
        copy_name(name, get_skater_name(player_object(player)));
    }
    script::VectorValue push;
    std::memcpy(&push, msg + 4, sizeof(push));
    send_event(skater, 0x915E5E39, name, 0x2F679251, other_driving, &push);
    return net::kResultPassOn;
}

} // namespace Obj

namespace objects {

// 0x00569680
uint8_t* RiderFrameLog::fetch_newest_snapshot()
{
    int count = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(this) + 0x18);
    return reinterpret_cast<uint8_t*>(this) + 0x1C + ((count + 19) % kEntries) * 0x84;
}

// 0x005696A0
StateHistoryMessage* RiderFrameLog::get_latest_message()
{
    int count = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(this) + 0xA6C);
    return reinterpret_cast<StateHistoryMessage*>(reinterpret_cast<uint8_t*>(this) + 0xA70 + (count % kEntries) * 0x54);
}

} // namespace Obj
