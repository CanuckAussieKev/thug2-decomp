// The skater's state history component and its network collisions. See state_history.h.

#include "game/rider/state_history.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>
#include <span>
#include <string_view>

#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/objects/game_objects.h"
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
#include "game/rider/skater_state.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_score.h"
#include "engine/objects/input_component.h"
#include "wire/play_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

namespace objects {

namespace {
constexpr std::size_t kNameBytes = 0x20;
constexpr int kWrapShift = 16;
constexpr float kNearbyDistance = 96.0f;
constexpr int kDrivingBit = 0x80;                   // an object id's high bit: that skater was driving
constexpr int kObjectIdMask = 0x7F;
constexpr std::size_t kSomeoneBytes = 8;            // the stand-in name with its terminator
constexpr int kEntriesBack = 19;                    // one back in the ring of states (plus its size, modulo)
constexpr uint32_t kString0 = 0xA4B08520; // String0
constexpr uint32_t kOffset = 0xA6F5352F; // Offset
constexpr uint32_t kDrivingCoefficientBoostFactor = 0x0A49FF23u; // DrivingCoefficientBoostFactor
constexpr uint32_t kInternetServerCollRadius = 0xF2E35FA3u; // InternetServerCollRadius
constexpr uint32_t kLanServerCollRadius = 0xECB41860u; // LanServerCollRadius
constexpr uint32_t kInternetClientCollRadius = 0xDBD6D58Au; // InternetClientCollRadius
constexpr uint32_t kLanClientCollRadius = 0xC5819249u; // LanClientCollRadius
constexpr uint32_t kDrivingRadiusBoostFactor = 0xEEF4AFB3u; // DrivingRadiusBoostFactor
constexpr uint32_t kSkaterCollideBail = 0x915E5E39; // SkaterCollideBail
constexpr uint32_t kFireBall = 0xD039432Cu; // FireBall
constexpr uint32_t kProjectile = 0xC304BD92u; // Projectile
constexpr uint32_t kMadeOtherSkaterBail = 0xA1021AF0; // MadeOtherSkaterBail
constexpr uint32_t kLoserIsDriving = 0x7FD0663D; // LoserIsDriving
constexpr uint32_t kWinnerIsDriving = 0x2F679251; // WinnerIsDriving

// Components of the skater this code asks for.
constexpr uint32_t kSkaterState = 0x43686585;          // SkaterState
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // +0x25 a flag; +0x2C its state component
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kWalk = 0x726E85AA;
constexpr uint32_t kInput = 0x27D7CD28;

// Game types (the game mode's 0x005532A0).
constexpr uint32_t kGameTag = 0x6EF8FDA0;
constexpr uint32_t kGameTag2 = 0x5D32129C;
constexpr uint32_t kGameTeamTag = 0x6C5FF266;

// Network messages.

// The default name in the events: "Someone" (0x0064C1C4); the names the code looks up.
const char* const kSomeone = "Someone";
const char* const kUnbindFireballTricks = "unbind_fireball_tricks"; // "unbind_fireball_tricks"
const char* const kDrivingRadiusBoost = "driving_radius_boost";     // "driving_radius_boost"

// a - b as the original's x87 code rounds it when it stores it.
float get_difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

// --- the network game manager (0x007CCE14), its players and the game mode ---
void* get_game_mode() { return static_cast<void*>(goals::game_mode()); }
void* skater_manager() { return static_cast<void*>(modules::Skate::sole()); }

online::PlayerRecord* find_player_slot_by_obj(int id)
{
    return online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(id));
}
online::PlayerRecord* start_roster_sweep(online::PlayerSearch* search)
{
    return online::online_hub()->begin_player_walk(search, true);
}
online::PlayerRecord* step_roster_sweep(online::PlayerSearch* search)
{
    return online::online_hub()->step_player_walk(search, true);
}
bool get_on_server() { return online::online_hub()->get_on_server(); }
bool manager_flag4() { return online::online_hub()->in_internet_mode(); }
bool manager_may_collide() { return online::online_hub()->players_can_collide(); }
bool manager_flag_3a() { return online::online_hub()->has_match_ended(); }
// The manager's +0x6C (the network code's get_crown_holder, 0x00533210).
GameEntity* local_skater() { return static_cast<GameEntity*>(online::online_hub()->get_crown_holder()); }
// The manager's server app (+0x04: the folded getter 0x005480C0).
net::AppHost* manager_app() { return online::online_hub()->host(); }

// A player (PlayerRecord): its object (+0x14), team field (+0x30) and flags (+0x1D0: 0x1 local, 0x80 "it" - holds_crown,
// 0x7800 a team - is_carrying_flag / get_flag_team). Its announcements: give_crown makes it "it", note_flag_returned tags it
// on its own team, note_flag_steal converts it to another.
online::PlayerRecord* details_of(online::PlayerRecord* player) { return player; }
GameEntity* player_object(online::PlayerRecord* player) { return player->skater(); }
bool is_observing(online::PlayerRecord* player) { return details_of(player)->is_invulnerable(); }
bool is_it(online::PlayerRecord* player) { return details_of(player)->holds_crown() != 0; }
bool on_a_team(online::PlayerRecord* player) { return details_of(player)->is_carrying_flag() != 0; }
int get_team(online::PlayerRecord* player) { return details_of(player)->get_flag_team(); }
bool is_host_local(online::PlayerRecord* player) { return details_of(player)->is_own_player() != 0; }
int get_connection(online::PlayerRecord* player) { return details_of(player)->get_link_handle(); }
void mark_tag_time(online::PlayerRecord* player) { details_of(player)->mark_just_joined(); }
void make_it(online::PlayerRecord* player, bool it) { details_of(player)->give_crown(it); }
void tag_on_team(online::PlayerRecord* player) { details_of(player)->note_flag_returned(); }
void convert_to_team(online::PlayerRecord* player, int team) { details_of(player)->note_flag_steal(team); }

uint32_t get_game_type() { return static_cast<goals::MatchRules*>(get_game_mode())->get_name_key(); }
bool is_squad_play() { return static_cast<goals::MatchRules*>(get_game_mode())->is_squad_match() != 0; }
bool tag_game_over() { return static_cast<goals::MatchRules*>(get_game_mode())->finish_rules_reached(); }
bool in_slap_game() { return commands::cfunc_in_slap_game(nullptr, nullptr); }

GameEntity* skater_by_index(int index) { return static_cast<modules::Skate*>(skater_manager())->get_rider_at(index); }
const char* get_skater_name(GameEntity* skater) { return core::down_cast<Rider>(skater)->fetch_shown_name(); }
// A skater's score (its +0x278's +0x18).
void* skater_score(GameEntity* skater) { return core::down_cast<Rider>(skater)->score_holder()->points_held(); }
int get_points_total(void* score) { return static_cast<modules::PointTally*>(score)->get_total_score(); }
void set_points_total(void* score, int value) { static_cast<modules::PointTally*>(score)->set_grand_total(value); }

// Knocked over by `other`: a bail on the core physics, or the walk component's when walking or driving.
void knock_down(ObjectModule* physics, GameEntity* other)
{
    core::down_cast<skater::SkaterPhysicsModule>(physics)->reset_to_air(other);
}
void knock_down_walking(ObjectModule* walk, GameEntity* other)
{
    core::down_cast<FootMovement>(walk)->go_to_air_for(other);
}

// A vector's length as 0x004023C0 leaves it on the x87 stack (x, y, then z; unrounded).
double length_of(const maths::Vec4& v)
{
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
}

// The knock-back velocity both handlers give: the unit direction away from the other times 300, then 1.5, each
// component rounded to a float after each (except where noted), and 200 up.
constexpr float kKnockBack = 300.0f; // 0x0064675C
constexpr float kKnockScale = 1.5f;  // 0x00646744
constexpr float kKnockUp = 200.0f;
constexpr float kTooClose = 0.0001f; // 0x006453D4

// strncpy(name, from, 0x20) (the game's C runtime's 0x006239E0): up to 0x20 characters, the rest zeroed.
void copy_name(std::array<char, kNameBytes>& name, const char* from)
{
    size_t i = 0;
    const std::string_view text(from);
    for (; i < sizeof(name) && i < text.size(); ++i)
        core::at(name, i) = core::at(text, i);
    for (; i < sizeof(name); ++i)
        core::at(name, i) = 0;
}

// Both handlers' event: the other's name (and what the caller adds) to the skater.
void send_event(GameEntity* skater, uint32_t type, const char* name, uint32_t flag, bool with_flag,
                const script::VectorValue* push)
{
    script::Struct params;
    params.append_word(kString0, name); // name
    if (push)
        params.put_vec3(checksums::Name{kOffset}, *push);
    if (with_flag)
        params.put_name_key(checksums::Name{0u}, flag);
    skater->post_own_event(type, &params);
    params.get_clear();
}

} // namespace

// --- StateHistoryMessage ---

// 0x00519800
uint32_t get_time(StateHistoryMessage& message)
{
    return static_cast<uint32_t>(message.time_high_) << kWrapShift | message.clock_;
}

// 0x00519810
void change_moment(StateHistoryMessage& message, uint32_t time)
{
    message.clock_ = static_cast<uint16_t>(time);
    message.time_high_ = static_cast<uint16_t>(time >> kWrapShift);
}

// --- RiderFrameLog ---

// 0x0051AD30
gsl::owner<ObjectModule*> RiderFrameLog::fabricate() { return new RiderFrameLog; }

// 0x0051ACB0
RiderFrameLog::RiderFrameLog()
{
    // (The original clears each state entry's +0x68 / +0x6C and the entry, the messages, the counts, then the
    // entries and the messages again.)
    std::memset(states_.data(), 0, sizeof(states_));
    std::memset(messages_.data(), 0, sizeof(messages_));
    set_kind_tag(kType);
}

// 0x00519FA0
void RiderFrameLog::get_frame_movement(maths::Vec4* out)
{
    const maths::Vec4& vel = entity()->vel();
    const double frame = frame_clock::get_frame_seconds();
    auto rounded = static_cast<float>(frame);
    out->x = rounded * vel.x;
    out->y = rounded * vel.y;
    out->z = rounded * vel.z;
    out->w = static_cast<float>(frame * vel.w);
}

// 0x00519FF0
void RiderFrameLog::fetch_place(maths::Vec4* out) { *out = entity()->at_point(); }

// 0x0051A020
double RiderFrameLog::get_collision_reach(bool driving)
{
    const float reach = script::fetch_float(entity()->is_local_skater() ? 0x0D24273Bu : 0x25F78481u);
    if (!driving)
        return reach;
    return (script::fetch_float(kDrivingCoefficientBoostFactor) + 1.0) * reach;
}

// 0x0051A080
double RiderFrameLog::fetch_hit_radius(bool my_driving, bool their_driving)
{
    uint32_t name = 0;
    if (entity()->is_local_skater())
        name = manager_flag4() ? kInternetServerCollRadius : kLanServerCollRadius;
    else
        name = manager_flag4() ? kInternetClientCollRadius : kLanClientCollRadius;
    const float radius = script::fetch_float(name);
    float drivers = my_driving ? 1.0f : 0.0f;
    if (their_driving)
        drivers = static_cast<float>(drivers + 1.0);
    return (script::fetch_float(kDrivingRadiusBoostFactor) * static_cast<double>(drivers) + 1.0) * radius;
}

// 0x0051A150
bool RiderFrameLog::is_near_local_skater()
{
    const GameEntity* local = local_skater();
    const maths::Vec4& pos = entity()->at_point();
    const float dx = get_difference(pos.x, local->at_point().x);
    const double dy = static_cast<double>(pos.y) - local->at_point().y;
    const double dz = static_cast<double>(pos.z) - local->at_point().z;
    const double distance = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx);
    return distance < kNearbyDistance; // 0x0064EC40
}

// 0x0051A1C0
void RiderFrameLog::check_for_collisions(int first)
{
    online::PlayerRecord* me = find_player_slot_by_obj(static_cast<int>(entity()->ident()));
    if (!me || is_observing(me))
        return;
    if (!manager_may_collide() && !is_it(me) && !on_a_team(me))
        return;
    auto* my_state = core::down_cast<RiderPoseState>(entity()->fetch_module(kSkaterState));
    // Whether this machine tells the players of a hit (not when the skater is in a bail, outside a slap game).
    bool tell_players = false;
    if (!my_state->flag(PoseFlag::kBailing).on || in_slap_game()) {
        tell_players = true;
    } else {
        tell_players = false;
        if (!is_it(me) && !on_a_team(me))
            return;
    }
    const bool my_driving = my_state->in_vehicle();
    const maths::Vec4 my_pos = entity()->at_point();
    maths::Vec4 my_move{};
    get_frame_movement(&my_move);
    maths::Vec4 my_dir = my_move;
    my_dir.make_unit();
    maths::Seg mine{};
    mine.start = my_pos;

    for (int i = first; i < online::kPlayerSlotCount; ++i) {
        GameEntity* skater = skater_by_index(i);
        if (!skater || skater == entity())
            continue;
        auto* their_state = core::down_cast<RiderPoseState>(skater->fetch_module(kSkaterState));
        if (their_state->flag(PoseFlag::kBailing).on)
            continue;
        online::PlayerRecord* them = find_player_slot_by_obj(static_cast<int>(skater->ident()));
        if (is_observing(them) || is_it(them))
            continue;
        if (on_a_team(me) && on_a_team(them) && get_team(me) != them->team())
            continue;
        if (is_squad_play() && them->team() == me->team())
            continue;

        RiderFrameLog* their_history = core::down_cast<Rider>(skater)->state_history();
        const bool their_driving = their_state->in_vehicle();
        maths::Vec4 their_pos{};
        their_history->fetch_place(&their_pos);
        maths::Vec4 their_move{};
        their_history->get_frame_movement(&their_move);
        maths::Vec4 their_dir = their_move;
        their_dir.make_unit();
        if (length_of(their_move) > 100.0f) // 0x006463D4 (NaN carries on)
            continue;

        auto radius = static_cast<float>(fetch_hit_radius(my_driving, their_driving));
        // (The original looks driving_radius_boost up for each driver and drops it.)
        if (my_driving)
            script::fetch_float(kDrivingRadiusBoost);
        if (their_driving)
            script::fetch_float(kDrivingRadiusBoost);
        mine.end = mine.start + my_dir * static_cast<float>(get_collision_reach(my_driving));
        maths::Seg theirs{};
        theirs.start = their_pos;
        theirs.end = theirs.start + their_dir * static_cast<float>(their_history->get_collision_reach(their_driving));

        // Where they meet: the segments' closest points, else the first of the four pairs of ends close enough.
        maths::Vec4 my_point{}, their_point{};
        float scratch = 0.0f;
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
        bool must_be_it = false;
        if (my_driving == their_driving) {
            auto my_speed = static_cast<float>(length_of(my_move));
            must_be_it = length_of(their_move) < my_speed;
        } else {
            must_be_it = my_driving && !their_driving;
        }
        if (must_be_it && !is_it(me))
            continue;

        net::AppHost* app = manager_app();
        if (tell_players) {
            net::NetMessageInfo msg;
            online::RanInto ran_into; // (wire/play_messages.h)
            ran_into.object_id = static_cast<uint8_t>(their_driving * kDrivingBit | static_cast<uint8_t>(skater->ident()));
            const maths::Vec4 push = my_point - their_point;
            core::at(ran_into.push, 0) = push.x;
            core::at(ran_into.push, 1) = push.y;
            core::at(ran_into.push, 2) = push.z;
            core::at(ran_into.push, 3) = push.w;
            std::array<uint8_t, online::kRanIntoSize> ran_into_bytes{};
            util::ByteWriter ran_into_writer(ran_into_bytes.data(), sizeof(ran_into_bytes));
            online::write_ran_into(ran_into_writer, ran_into);
            if (ObjectModule* input = entity()->fetch_module(kInput))
                core::down_cast<PadInputModule>(input)->set_disabled(true);
            mark_tag_time(me);
            msg.set_ident(online::MessageId::kRanInto);
            msg.set_content(ran_into_bytes.data());
            msg.set_byte_count(sizeof(ran_into_bytes));
            msg.set_queue(net::NetMessageInfo::kLaneOrdered);
            msg.set_group_id(online::MessageGroup::kGame);
            app->put_on_outbox(get_connection(me), &msg);
            auto ran_into_by = static_cast<uint8_t>(my_driving * kDrivingBit | static_cast<uint8_t>(entity()->ident()));
            msg.set_ident(online::MessageId::kRanIntoAck);
            msg.set_content(&ran_into_by);
            msg.set_byte_count(1);
            app->put_on_outbox(get_connection(them), &msg);
            if (in_slap_game()) {
                void* score = skater_score(skater);
                set_points_total(score, get_points_total(score) + 1);
            }
        }

        const uint32_t type = get_game_type();
        if (type == kGameTag || get_game_type() == kGameTag2) {
            if (tag_game_over())
                return;
            if (!is_it(me))
                continue;
            make_it(them, true);
            online::PlayerSearch search;
            for (online::PlayerRecord* player = start_roster_sweep(&search); player;
                 player = step_roster_sweep(&search)) {
                if (is_host_local(player))
                    continue;
                net::NetMessageInfo msg;
                auto tagged = static_cast<uint8_t>(skater->ident());
                msg.set_content(&tagged);
                msg.set_ident(online::MessageId::kKing);
                msg.set_byte_count(1);
                msg.set_queue(net::NetMessageInfo::kLaneOrdered);
                msg.set_group_id(online::MessageGroup::kGame);
                app->put_on_outbox(get_connection(player), &msg);
            }
        } else if (get_game_type() == kGameTeamTag) {
            if (manager_flag_3a())
                return;
            if (!on_a_team(me))
                continue;
            net::NetMessageInfo msg;
            std::array<uint8_t, 2> tagged{};
            core::at(tagged, 0) = static_cast<uint8_t>(skater->ident());
            core::at(tagged, 1) = static_cast<uint8_t>(get_team(me));
            const bool same_team = get_team(me) == them->team();
            online::PlayerSearch search;
            for (online::PlayerRecord* player = start_roster_sweep(&search); player;
                 player = step_roster_sweep(&search)) {
                if (is_host_local(player)) {
                    if (same_team)
                        tag_on_team(them);
                    else
                        convert_to_team(them, get_team(me));
                    continue;
                }
                msg.set_ident(same_team ? online::MessageId::kTeamTagged : online::MessageId::kTeamConverted);
                msg.set_content(tagged.data());
                msg.set_group_id(online::MessageGroup::kGame);
                msg.set_queue(net::NetMessageInfo::kLaneOrdered);
                msg.set_byte_count(2);
                app->put_on_outbox(get_connection(player), &msg);
            }
        }
    }
}

// 0x00519870
int RiderFrameLog::handle_hit_messages(net::InboundMsgCtx* context)
{
    // (the message: wire/play_messages.h; NOT ORIGINAL: one too short is dropped - the original read past it)
    util::ByteReader reader(context->body(), context->body_size());
    online::Hit hit;
    if (!online::read_hit(reader, &hit))
        return net::kResultConsumed;
    const bool other_driving = hit.fireball;
    auto* skater = static_cast<GameEntity*>(context->content());
    std::array<char, kNameBytes> name = {};
    std::memcpy(name.data(), kSomeone, kSomeoneBytes);
    if (context->type_code() == online::MessageId::kHit) {
        if (online::PlayerRecord* player = find_player_slot_by_obj(static_cast<int8_t>(hit.object_id))) {
            GameEntity* other = player_object(player);
            // Away from the other skater (straight along x and z if they're on top of each other).
            const double dx = static_cast<double>(skater->at_point().x) - other->at_point().x;
            const float dy = get_difference(skater->at_point().y, other->at_point().y);
            const float dz = get_difference(skater->at_point().z, other->at_point().z);
            const float dw = get_difference(skater->at_point().w, other->at_point().w);
            const double length = std::sqrt((dx * dx + static_cast<double>(dy) * dy) + static_cast<double>(dz) * dz);
            double x = 1.0, y = 0.0, z = 1.0, w = 1.0;
            if (!(length < kTooClose)) {
                auto inverse = static_cast<float>(1.0 / length);
                x = dx * inverse;
                y = static_cast<double>(inverse) * dy;
                z = static_cast<double>(inverse) * dz;
                w = static_cast<double>(inverse) * dw;
            }
            auto wk = static_cast<float>(w * kKnockBack), zk = static_cast<float>(z * kKnockBack);
            auto yk = static_cast<float>(y * kKnockBack), xk = static_cast<float>(x * kKnockBack);
            skater->vel().x = xk * kKnockScale;
            skater->vel().y = yk * kKnockScale;
            skater->vel().z = zk * kKnockScale;
            skater->vel().w = wk * kKnockScale;
            skater->vel().y = kKnockUp;
            ObjectModule* control = skater->fetch_module(kSkaterPhysicsControl);
            const RiderPoseState* state = core::down_cast<RiderBoardPhysics>(control)->pose();
            if (!state->in_vehicle() && state->physics_mode() == 0)
                knock_down(skater->fetch_module(kSkaterCorePhysics), other);
            else
                knock_down_walking(skater->fetch_module(kWalk), other);
            copy_name(name, get_skater_name(player_object(player)));
            if (!get_on_server()) {
                void* score = skater_score(skater);
                const int lost = hit.score;
                if (get_points_total(score) - lost < 0) {
                    set_points_total(score, 0);
                    script::launch_script(kUnbindFireballTricks, nullptr, nullptr, false);
                } else {
                    set_points_total(score, get_points_total(score) - lost);
                }
            }
        }
        send_event(skater, kSkaterCollideBail, name.data(), other_driving ? kFireBall : kProjectile, true, nullptr);
    } else if (context->type_code() == online::MessageId::kHitOwner) {
        if (online::PlayerRecord* player = find_player_slot_by_obj(static_cast<int8_t>(hit.object_id)))
            copy_name(name, get_skater_name(player_object(player)));
        send_event(skater, kMadeOtherSkaterBail, name.data(), other_driving ? kFireBall : kProjectile, true, nullptr);
    }
    return net::kResultConsumed;
}

// 0x00519C10
int RiderFrameLog::handle_collision_messages(net::InboundMsgCtx* context)
{
    auto* skater = static_cast<GameEntity*>(context->content());
    const auto id = context->type_code();
    if (id != online::MessageId::kRanInto && id != online::MessageId::kRanIntoAck)
        return net::kResultPassOn;
    // (the messages: wire/play_messages.h - 0x44 its first byte only; NOT ORIGINAL: one too short is dropped,
    // the original read past it)
    util::ByteReader reader(context->body(), context->body_size());
    online::RanInto ran_into;
    bool read = false;
    if (id == online::MessageId::kRanIntoAck) {
        ran_into.object_id = reader.u8();
        read = reader.ok();
    } else {
        read = online::read_ran_into(reader, &ran_into);
    }
    if (!read)
        return net::kResultPassOn;
    std::array<char, kNameBytes> name = {};
    std::memcpy(name.data(), kSomeone, kSomeoneBytes);
    const bool other_driving = (ran_into.object_id >> 7) & 1;
    // (the driving bit cleared in the message itself, as the original: the next local skater's handler sees it so)
    core::at(std::span<uint8_t>(context->body(), context->body_size()), 0) = static_cast<uint8_t>(ran_into.object_id & kObjectIdMask);
    online::PlayerRecord* player = find_player_slot_by_obj(static_cast<int8_t>(ran_into.object_id & kObjectIdMask));
    if (id == online::MessageId::kRanIntoAck) {
        if (player)
            copy_name(name, get_skater_name(player_object(player)));
        send_event(skater, kMadeOtherSkaterBail, name.data(), kLoserIsDriving, other_driving, nullptr);
        return net::kResultPassOn;
    }
    if (player) {
        ObjectModule* control = skater->fetch_module(kSkaterPhysicsControl);
        if (!core::down_cast<RiderBoardPhysics>(control)->pose()->in_vehicle()) {
            GameEntity* other = player_object(player);
            const double dw = static_cast<double>(skater->at_point().w) - other->at_point().w;
            const double dz = static_cast<double>(skater->at_point().z) - other->at_point().z;
            const double dy = static_cast<double>(skater->at_point().y) - other->at_point().y;
            std::array<float, 4> away = {{get_difference(skater->at_point().x, other->at_point().x), static_cast<float>(dy),
                             static_cast<float>(dz), static_cast<float>(dw)}};
            const double length = std::sqrt((static_cast<double>(core::at(away, 0)) * core::at(away, 0) + dy * dy) + dz * dz);
            double w = 0.0, z = 0.0;
            if (!(length < kTooClose)) {
                // (0x00423650: each component times 1 / the length rounded to a float)
                const double inverse = 1.0 / static_cast<float>(length);
                for (float& c : away)
                    c = static_cast<float>(inverse * c);
                z = core::at(away, 2);
                w = core::at(away, 3);
            } else {
                core::at(away, 0) = 1.0f;
                core::at(away, 1) = 0.0f;
                z = 1.0;
                w = 1.0;
            }
            const double wk = w * kKnockBack; // (kept unrounded)
            auto zk = static_cast<float>(z * kKnockBack);
            const float yk = core::at(away, 1) * kKnockBack;
            const float xk = core::at(away, 0) * kKnockBack;
            skater->vel().x = xk * kKnockScale;
            skater->vel().y = yk * kKnockScale;
            skater->vel().z = zk * kKnockScale;
            skater->vel().w = static_cast<float>(wk * kKnockScale);
            skater->vel().y = kKnockUp;
            const RiderPoseState* state = core::down_cast<RiderBoardPhysics>(control)->pose();
            if (!state->in_vehicle() && state->physics_mode() == 0 &&
                !core::down_cast<RiderBoardPhysics>(control)->force_walk())
                knock_down(skater->fetch_module(kSkaterCorePhysics), other);
            else
                knock_down_walking(skater->fetch_module(kWalk), other);
        }
        copy_name(name, get_skater_name(player_object(player)));
    }
    const script::VectorValue push = {.x = core::at(ran_into.push, 0), .y = core::at(ran_into.push, 1), .z = core::at(ran_into.push, 2), .w = core::at(ran_into.push, 3)};
    send_event(skater, kSkaterCollideBail, name.data(), kWinnerIsDriving, other_driving, &push);
    return net::kResultPassOn;
}

} // namespace objects

namespace objects {

// 0x00569680
SkaterStateHistoryFrame* RiderFrameLog::fetch_newest_snapshot() { return &core::at(states_, (state_count_ + kEntriesBack) % kEntries); }

// 0x005696A0
StateHistoryMessage* RiderFrameLog::get_latest_message() { return &core::at(messages_, message_count_ % kEntries); }

} // namespace objects
