// The skater's non-local network logic component (GameObjects::RiderRemoteSync, nonlocal_net_logic.h): the
// playback of the states and messages its client sent - interpolation between states, extrapolation when they run
// late, the messages' events. The movement code follows the original's x87 maths (double until stored as floats).

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "engine/render/probe_ray.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/model_component.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/end_run.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/nonlocal_net_logic.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/rider/special_item.h"
#include "game/rider/state_history.h"
#include "platform/game_clock.h"
#include "engine/gfx/anim_blend.h"
#include "engine/objects/control_pad.h"
#include "game/skate/skate_module.h"
#include "engine/sys/game_config.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"

namespace objects {

namespace {
int& nonlocal_lag()
{
    static int nonlocal_lag{};
    return nonlocal_lag;
}

// The model component's three spin axes (its twist_rate_, about X, Y, Z) the messages 0x76 / 0x8C start and stop.
// Message 0x77: the made item's structure and component
constexpr uint32_t kItemName = 0xCAB94088, kItemOwner = 0x40C698AF, kItemComponent = 0x5CB5CB7E;
constexpr uint32_t kSkaterAppeared = 0x1011A5D6; // the script run when a skater's playback starts (name unknown)

constexpr float kTurnSnap = 0.75f;          // 0x0064A4EC: the at rows this far apart: no slerp, the earlier one's
constexpr float kTeleportDistance = 600.0f; // 0x0064B81C: moved further than this between states: put there
constexpr float kOne = 1.0f;                // 0x006453A8
constexpr float kMsPerSecond = 1000.0f;     // 0x006453C0 (also: playback starts this over the frame rate behind)
constexpr float kHalf = 0.5f;               // 0x006453BC
constexpr float kSpeedScale = 16.0f;        // 0x00645D68: a move's length over the frame's ms, times this
constexpr float kWallCheckReach = 120.0f;   // 0x0064BD2C: extrapolated further than this: no wall check
constexpr uint32_t kFirstForwardCollisionHeight = 0xD4205C9B; // Skater_First_Forward_Collision_Height
constexpr uint32_t kRailGravity = 0xD1F46992;             // (a physics float: the rail's pull downhill; name unknown)
constexpr uint32_t kRailCornerLeaveAngle = 0x76C1DA15;    // (the angle a rail may turn and still be followed)
constexpr uint32_t kRailActive = 0x4;                     // GrindPoint::flag_bits_
constexpr float kRailEndSlack = 0.1f;                     // 0x00645D74: past a segment's end by more than this
constexpr float kRadiansPerDegree = 0.01745329238474369f; // 0x00645464

// A state the history keeps (0x84 bytes, 20 at the history's +0x1C): +0x08 the matrix (+0x28 its at row), +0x48 the
// position, +0x68 the timed flags as bits, +0x6C the end run flags, +0x70 the state, +0x74 doing a trick, +0x75 the
// physics mode, +0x76 driving, +0x78 the time, +0x7C the terrain.
SkaterStateHistoryFrame* history_frame(RiderFrameLog* history, int index) { return &core::at(history->states_, index); }
uint32_t history_frame_stamp(SkaterStateHistoryFrame* frame) { return frame->get_time(); }
maths::Mat4& snapshot_orientation(SkaterStateHistoryFrame* frame) { return frame->matrix; }
maths::Vec4& frame_position(SkaterStateHistoryFrame* frame) { return frame->position; }

online::Hub* net_session_slot() { return online::net_game_state().online_hub(); }
// The network manager's client app: +8 its frame count.
net::AppHost* client_app() { return net_session_slot()->fetch_patron(0); }
int frame_rate() { return game_config::frame_rate(); }
int get_now() { return frame_clock::game_clock_elapsed() + clock_offset(); }
// The level's rails (the skate module's +0x24).
GrindRailHub* get_level_rails() { return modules::Skate::sole()->rail_manager().get(); }

// How far behind the connection is, in frames of `frame_ms`: the latency - the server's connection to the skater's
// player, a client's first (to the server) - less a frame, over a frame, as unsigned numbers (0 without a frame).
// (Quirk kept: a latency under a frame wraps round to a huge lag. The original's check for a negative result is
// left out: it never is.)
float connection_lag(net::AppHost* app, GameEntity* skater, int frame_ms)
{
    online::Hub* net = net_session_slot();
    net::Conn* conn = nullptr;
    if (net->get_on_server()) {
        const online::PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident_));
        conn = player->conn_;
    } else {
        net::ConnSearch search;
        conn = app->begin_conn_walk(&search);
    }
    const int latency = conn->is_host_local() ? 0 : conn->ping_probe_.average_latency_;
    if (frame_ms == 0)
        return 0.0f;
    return static_cast<float>(static_cast<double>(static_cast<uint32_t>(latency - frame_ms)) /
                              static_cast<double>(static_cast<uint32_t>(frame_ms)));
}

} // namespace

// 0x00515940
void RiderRemoteSync::interpolate()
{
    const net::AppHost* app = client_app();
    if (!app || past_frames_->state_count_ < kHistory)
        return;
    auto* skater = core::down_cast<Rider>(entity_);
    if (sync_frame_ == 0) {
        // Playback starts at the newest state, a fixed time behind it.
        const int newest = (past_frames_->state_count_ - 1) % kHistory;
        sync_frame_ = app->clock_;
        const int delay = static_cast<int>(static_cast<double>(kMsPerSecond) / frame_rate());
        SkaterStateHistoryFrame* frame = history_frame(past_frames_, newest);
        play_idx_ = newest;
        sync_time_ = static_cast<int>(history_frame_stamp(frame) - delay);
        skater->basis_frame_ = snapshot_orientation(frame);
        skater->display_matrix_ = snapshot_orientation(frame);
        skater->at_point_ = frame_position(frame);
        skater->mark_jumped_ahead(true);
        skater->vel_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        if (skater->get_flag_2c_4())
            return;
        skater->show_skater();
        script::launch_script(kSkaterAppeared, nullptr, skater, false, nullptr);
        net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident_)); // (its result unused)
        update_ground_lighting();
        return;
    }

    // (the original's slerp interpolator is made here: its constructor 0x005902E0 does nothing)
    auto* player = (net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident_)));
    if (!player->has_finished_joining())
        player->set_finished_joining();
    const int count = past_frames_->state_count_;
    const int oldest = count % kHistory; // (the next to be written)
    uint32_t now = app->clock_ - sync_frame_ + sync_time_;
    SkaterStateHistoryFrame* last = history_frame(past_frames_, (count - 1) % kHistory);
    if (now > history_frame_stamp(last)) {
        // Past the newest state: the playback falls back (by a second's frames, or to the state when further).
        int adjust = 0;
        if (now - history_frame_stamp(last) > 1000)
            adjust = static_cast<int>(history_frame_stamp(last) - now);
        else
            adjust = -1000 / frame_rate();
        sync_time_ += adjust;
        now = app->clock_ - sync_frame_ + sync_time_;
    }
    // The first state not yet reached, from the oldest (none: nothing played this frame).
    int entry = oldest;
    while (now > history_frame_stamp(history_frame(past_frames_, entry))) {
        entry = (entry + 1) % kHistory;
        if (entry == oldest)
            return;
    }
    const int lag = (oldest - entry + kHistory) % kHistory;
    play_idx_ = entry;
    nonlocal_lag() = lag;
    if (lag >= 10)
        sync_time_ += 1000 / frame_rate() * 10; // (far behind: ten frames on)
    else if (lag == 0)
        skater->reset_27c();
    SkaterStateHistoryFrame* from = history_frame(past_frames_, (entry + kHistory - 1) % kHistory);
    SkaterStateHistoryFrame* to = history_frame(past_frames_, entry);
    auto elapsed = static_cast<float>(static_cast<int>(now - history_frame_stamp(from)));
    const int span = static_cast<int>(history_frame_stamp(to) - history_frame_stamp(from));
    auto t = static_cast<float>(static_cast<double>(elapsed) / span);
    maths::Vec4 move = frame_position(to) - frame_position(from);
    last_play_pos_ = play_pos_;

    // The orientation: slerped, unless the at rows are far apart (then the earlier state's).
    const maths::Vec4 turn = core::at(to->matrix.row, 2) - core::at(from->matrix.row, 2);
    const double turned = std::sqrt((static_cast<double>(turn.x) * turn.x + static_cast<double>(turn.z) * turn.z) +
                                    static_cast<double>(turn.y) * turn.y);
    if (turned > kTurnSnap) {
        skater->basis_frame_ = snapshot_orientation(from);
        skater->display_matrix_ = snapshot_orientation(from);
    } else {
        const maths::Mat4 start = snapshot_orientation(from);
        const maths::Mat4 end = snapshot_orientation(to);
        maths::Mat4 between{};
        const maths::ArcRotationBlender slerp(start, end);
        slerp.fetch_mat(between, t);
        skater->basis_frame_ = between;
        skater->display_matrix_ = between;
    }

    // The position: interpolated, unless it moved too far (then put at the later state's, standing).
    const double distance = std::sqrt((static_cast<double>(move.x) * move.x + static_cast<double>(move.y) * move.y) +
                                      static_cast<double>(move.z) * move.z);
    if (distance > kTeleportDistance) {
        skater->at_point_ = frame_position(to);
        skater->vel_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        skater->mark_jumped_ahead(true);
    } else {
        move = frame_position(from) + move * t; // (the move's storage now holds the position)
        skater->at_point_ = move;
        skater->vel_ = (skater->at_point_ - last_play_pos_) / frame_seconds_;
        core::at(moves_, updates_ % kHistory) = skater->vel_;
        // The velocity: the average of the moves kept - quirk kept: the sum starts from the position, not zero.
        std::array<double, 4> sum = {{move.x, move.y, move.z, move.w}};
        int n = 0;
        for (; n < updates_ && n < kHistory; ++n) {
            core::at(sum, 0) += core::at(moves_, n).x;
            core::at(sum, 1) += core::at(moves_, n).y;
            core::at(sum, 2) += core::at(moves_, n).z;
            core::at(sum, 3) += core::at(moves_, n).w;
        }
        if (n > 0) {
            const double inverse = static_cast<double>(kOne) / n;
            auto rounded = static_cast<float>(inverse);
            skater->vel_.x = static_cast<float>(inverse * core::at(sum, 0));
            skater->vel_.y = static_cast<float>(core::at(sum, 1) * rounded);
            skater->vel_.z = static_cast<float>(core::at(sum, 2) * rounded);
            skater->vel_.w = static_cast<float>(core::at(sum, 3) * rounded);
        }
    }

    // The state as the later state has it.
    play_pos_ = skater->at_point_;
    auto* state = pose_;
    state->change_phase(to->state);
    pose_->in_trick_move_ = to->flag74 != 0;
    state->set_surface_kind(to->value_7c_);
    state->set_physics_mode(to->flag75 != 0);
    pose_->in_vehicle_ = to->flag76 != 0;
    core::down_cast<RiderSessionEnd>(run_over_)->flag_bits_ = to->value_6c_; // (0x00407A70)
    // The timed flags (each a bool and the time it last changed) from the state's bits.
    const uint32_t bits = to->value68;
    for (int i = 0; i < kPoseFlagCount; ++i)
        pose_->set_flag(static_cast<PoseFlag>(i), (bits & (1u << i)) != 0);
    if (pose_->flag(PoseFlag::kAcidDrop).on)
        pose_->drop_velocity_ = skater->vel_;
    else
        pose_->drop_velocity_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// 0x00515360
void RiderRemoteSync::get_extrapolate()
{
    online::Hub* net = net_session_slot();
    net::AppHost* app = client_app();
    const int frame_ms = static_cast<int>(static_cast<double>(frame_seconds_) * kMsPerSecond);
    const float lag = connection_lag(app, entity_, frame_ms);
    const int slot = updates_ % kHistory;
    const float dw = play_pos_.w - last_play_pos_.w;
    const float dz = play_pos_.z - last_play_pos_.z;
    const float dy = play_pos_.y - last_play_pos_.y;
    const float dx = play_pos_.x - last_play_pos_.x;
    float dir_x = dx, dir_z = dz;
    // How well the move lines up with this machine's skater's (0 to 1; 0 without one).
    double alignment = 0.0;
    const online::PlayerRecord* local = net->get_own_player();
    if (local && local->skater_) {
        maths::Vec4 theirs = local->skater_->vel_;
        maths::Vec4 mine = {.x = dx, .y = dy, .z = dz, .w = dw};
        theirs.make_unit();
        mine.make_unit();
        alignment = ((static_cast<double>(mine.x) * theirs.x + static_cast<double>(mine.z) * theirs.z) +
                     static_cast<double>(mine.y) * theirs.y + kOne) *
                    kHalf;
    }
    const double squared = (static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) + static_cast<double>(dz) * dz;
    auto squared_rounded = static_cast<float>(squared);
    core::at(speeds_, slot) =
        static_cast<float>(std::sqrt(squared) / static_cast<double>(static_cast<uint32_t>(frame_ms)) * kSpeedScale);
    core::at(lags_, slot) = lag;
    core::at(alignments_, slot) = static_cast<float>(alignment);
    ++updates_;

    // The averages over the updates kept.
    double speeds = 0.0, lags = 0.0, alignments = 0.0;
    int n = 0;
    for (; n < updates_ && n < kHistory; ++n) {
        speeds += core::at(speeds_, n);
        lags += core::at(lags_, n);
        alignments += core::at(alignments_, n);
    }
    const double inverse = static_cast<double>(kOne) / n;
    auto rounded = static_cast<float>(inverse);
    auto speed = static_cast<float>(inverse * speeds);
    const double scale = (rounded * lags) * (alignments * rounded);
    // On along the last move's direction (flattened: y isn't moved) by the average speed, times the average lag and
    // alignment.
    const double length = std::sqrt(static_cast<double>(squared_rounded));
    if (length > 0.0) {
        const double to_unit = static_cast<double>(kOne) / length;
        dir_x = static_cast<float>(dx * to_unit);
        dir_z = static_cast<float>(to_unit * dz);
    }
    auto move_w = static_cast<float>(static_cast<double>(dw) * speed * scale);
    auto move_z = static_cast<float>(static_cast<double>(dir_z) * speed * scale);
    auto move_x = static_cast<float>(static_cast<double>(dir_x) * speed * scale);
    maths::Vec4& pos = entity_->at_point_;
    pos.w += move_w;
    pos.z += move_z;
    pos.x += move_x;

    // A wall between the played position and the new one (both the collision height up the up row): bounced off.
    GameEntity* skater = entity_;
    auto height = static_cast<float>(read_tuning_float(kFirstForwardCollisionHeight, 1, skater));
    const maths::Vec4& up = core::at(skater->basis_frame_.row, 1);
    const float raise_w = height * up.w;
    const float raise_z = height * up.z;
    const float raise_y = height * up.y;
    const float raise_x = height * up.x;
    const maths::Vec4 from = {.x = raise_x + play_pos_.x, .y = raise_y + play_pos_.y, .z = raise_z + play_pos_.z,
                              .w = raise_w + play_pos_.w};
    const maths::Vec4 to = {.x = raise_x + pos.x, .y = raise_y + pos.y, .z = raise_z + pos.z, .w = raise_w + pos.w};
    render::ProbeRay feeler;
    const double ex = static_cast<double>(from.x) - to.x;
    const double ey = static_cast<double>(from.y) - to.y;
    const double ez = static_cast<double>(from.z) - to.z;
    if (std::sqrt((ez * ez + ey * ey) + ex * ex) > kWallCheckReach)
        return;
    feeler.change_strand(&from, &to);
    feeler.set_excluded(0x10, 0);
    if (feeler.get_hit_shape(true, false))
        deflect_from_wall(&feeler.surface_dir_, &feeler.contact_pos_);
}

// 0x00516640
GrindPoint* RiderRemoteSync::move_along_rail(GrindPoint* node, float seconds)
{
    GrindRailHub* rails = get_level_rails();
    GrindPoint* next = node->succ_;
    if (!next)
        return nullptr;
    GameEntity* skater = entity_;
    maths::Vec4 a{}, b{}, p1{}, p2{};
    rails->get_absolute_pos(&a, node);
    rails->get_absolute_pos(&b, next);
    const double sx = static_cast<double>(b.x) - a.x;
    const double sy = static_cast<double>(b.y) - a.y;
    const double sz = static_cast<double>(b.z) - a.z;
    const double sw = static_cast<double>(b.w) - a.w;
    auto length = static_cast<float>(std::sqrt((sx * sx + sy * sy) + sz * sz));
    const double inverse = static_cast<double>(kOne) / length;
    auto rounded = static_cast<float>(inverse);
    const maths::Vec4 along = {.x = static_cast<float>(inverse * sx), .y = static_cast<float>(rounded * sy),
                               .z = static_cast<float>(rounded * sz), .w = static_cast<float>(rounded * sw)};
    // The rail's gravity along it.
    const float gravity = read_tuning_float(kRailGravity, 1, skater);
    maths::Vec4 pull = {.x = 0.0f, .y = gravity, .z = 0.0f, .w = 1.0f};
    pull.project_onto(along);
    maths::Vec4& vel = skater->vel_;
    vel.x = static_cast<float>(static_cast<double>(pull.x) * seconds + vel.x);
    vel.y = static_cast<float>(static_cast<double>(pull.y) * seconds + vel.y);
    vel.z = static_cast<float>(pull.z * seconds) + vel.z;
    vel.w = static_cast<float>(pull.w * seconds) + vel.w;
    float sign = -1.0f;
    if (!((static_cast<double>(along.y) * vel.y + static_cast<double>(along.z) * vel.z) +
              static_cast<double>(along.x) * vel.x <
          0.0))
        sign = 1.0f;

    // Whether the rail goes on round the corner ahead (else the skater stops at its end): the next segment there
    // active and turning less than Rail_Corner_Leave_Angle (both flattened).
    bool stopped = false;
    GrindPoint* beyond = sign > 0.0f ? next->succ_ : node->last_value_;
    if (!beyond || !(beyond->flag_bits_ & kRailActive)) {
        stopped = true;
    } else {
        maths::Vec4 here{}, there{};
        if (sign > 0.0f) {
            rails->get_absolute_pos(&p1, next);
            rails->get_absolute_pos(&p2, node);
            here = p2 - p1;
            rails->get_absolute_pos(&p1, beyond);
            rails->get_absolute_pos(&p2, next);
            there = p2 - p1;
        } else {
            rails->get_absolute_pos(&p1, node);
            rails->get_absolute_pos(&p2, next);
            here = p2 - p1;
            rails->get_absolute_pos(&p1, beyond);
            rails->get_absolute_pos(&p2, node);
            there = p2 - p1;
        }
        here.y = 0.0f;
        there.y = 0.0f;
        here.make_unit();
        there.make_unit();
        auto cosine =
            static_cast<float>((static_cast<double>(there.z) * here.z + static_cast<double>(there.y) * here.y) +
                               static_cast<double>(there.x) * here.x);
        const double limit =
            std::cos(static_cast<double>(read_tuning_float(kRailCornerLeaveAngle, 1, skater)) * kRadiansPerDegree);
        if (limit > cosine)
            stopped = true;
    }

    // Past the segment's end (further from its other end than its length): onto the next segment, if it's there,
    // clear and not round too sharp a corner - the overshoot carried on along it.
    float overshoot = 0.0f;
    rails->get_absolute_pos(&p1, sign > 0.0f ? node : next);
    const maths::Vec4& pos = skater->at_point_;
    const double dx = static_cast<double>(pos.x) - p1.x;
    const double dy = static_cast<double>(pos.y) - p1.y;
    const double dz = static_cast<double>(pos.z) - p1.z;
    const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
    if (distance > static_cast<double>(length) + kRailEndSlack) {
        overshoot = static_cast<float>(distance - length);
        // (quirk kept: going forward, the segment's own end is checked for being active, not the node beyond it)
        if (!beyond || !((sign > 0.0f ? next : beyond)->flag_bits_ & kRailActive))
            return nullptr;
        if (sign > 0.0f) {
            maths::Vec4 to{}, from{};
            rails->get_absolute_pos(&to, beyond);
            rails->get_absolute_pos(&from, next);
            if (!rail_clear(from, to) || stopped)
                return nullptr;
            rails->get_absolute_pos(&p1, next);
            skater->at_point_ = p1;
            node = next;
        } else {
            maths::Vec4 to{}, from{};
            rails->get_absolute_pos(&to, beyond);
            rails->get_absolute_pos(&from, node);
            if (!rail_clear(from, to) || stopped)
                return nullptr;
            rails->get_absolute_pos(&p1, node);
            skater->at_point_ = p1;
            node = beyond;
        }
    }

    // Along the segment it's on: the velocity and the matrix turned along it (the way it's going), the move.
    const GrindPoint* end = node->succ_;
    rails->get_absolute_pos(&p1, node);
    rails->get_absolute_pos(&p2, end);
    maths::Vec4 segment = {.x = p2.x - p1.x, .y = p2.y - p1.y, .z = p2.z - p1.z, .w = p2.w - p1.w};
    segment.make_unit();
    sign = -1.0f;
    if (!((static_cast<double>(segment.z) * vel.z + static_cast<double>(segment.y) * vel.y) +
              static_cast<double>(segment.x) * vel.x <
          0.0))
        sign = 1.0f;
    vel.change_heading(segment);
    vel.x *= sign;
    vel.y *= sign;
    vel.z *= sign;
    vel.w *= sign;
    const maths::Vec4 heading = {.x = segment.x * sign, .y = segment.y * sign, .z = segment.z * sign, .w = segment.w * sign};
    maths::Mat4& m = skater->basis_frame_;
    core::at(m.row, 2) = heading;
    core::at(m.row, 2).make_unit();
    core::at(m.row, 0) = {.x = core::at(m.row, 2).z, .y = 0.0f, .z = -core::at(m.row, 2).x, .w = 0.0f};
    core::at(m.row, 0).make_unit();
    const maths::Vec4& r = core::at(m.row, 0);
    const maths::Vec4& at = core::at(m.row, 2);
    auto up_x = static_cast<float>(static_cast<double>(r.z) * at.y - static_cast<double>(r.y) * at.z);
    auto up_y = static_cast<float>(static_cast<double>(r.x) * at.z - static_cast<double>(at.x) * r.z);
    auto up_z = static_cast<float>(static_cast<double>(at.x) * r.y - static_cast<double>(r.x) * at.y);
    core::at(m.row, 1) = {.x = up_x, .y = up_y, .z = up_z, .w = 0.0f};
    core::at(m.row, 1).make_unit();
    maths::Vec4& p = skater->at_point_;
    p.x = static_cast<float>(static_cast<double>(seconds) * vel.x + p.x);
    p.y = static_cast<float>(static_cast<double>(seconds) * vel.y + p.y);
    p.z = static_cast<float>(seconds * vel.z) + p.z;
    p.w = static_cast<float>(seconds * vel.w) + p.w;
    p.x = static_cast<float>(static_cast<double>(heading.x) * overshoot + p.x);
    p.y = static_cast<float>(static_cast<double>(heading.y) * overshoot + p.y);
    p.z = static_cast<float>(heading.z * overshoot) + p.z;
    p.w = static_cast<float>(heading.w * overshoot) + p.w;
    return node;
}

// 0x00516E00
void RiderRemoteSync::extrapolate_on_rail()
{
    GrindRailHub* rails = get_level_rails();
    online::Hub* net = net_session_slot();
    net::AppHost* app = client_app();
    const int frame_ms = static_cast<int>(static_cast<double>(frame_seconds_) * kMsPerSecond);
    const float lag = connection_lag(app, entity_, frame_ms);
    const int slot = updates_ % kHistory;
    const float dx = play_pos_.x - last_play_pos_.x;
    const float dy = play_pos_.y - last_play_pos_.y;
    const float dz = play_pos_.z - last_play_pos_.z;
    const float dw = play_pos_.w - last_play_pos_.w;
    double alignment = 0.0;
    const online::PlayerRecord* local = net->get_own_player();
    if (local) { // (its skater isn't checked here)
        maths::Vec4 theirs = local->skater_->vel_;
        maths::Vec4 mine = {.x = dx, .y = dy, .z = dz, .w = dw};
        theirs.make_unit();
        mine.make_unit();
        alignment = ((static_cast<double>(mine.z) * theirs.z + static_cast<double>(mine.y) * theirs.y) +
                     static_cast<double>(mine.x) * theirs.x + kOne) *
                    kHalf;
    }
    const double squared = (static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
    core::at(speeds_, slot) =
        static_cast<float>(std::sqrt(squared) / static_cast<double>(static_cast<uint32_t>(frame_ms)) * kSpeedScale);
    core::at(lags_, slot) = lag;
    core::at(alignments_, slot) = static_cast<float>(alignment);
    ++updates_;
    double lags = 0.0, alignments = 0.0;
    int n = 0;
    for (; n < updates_ && n < kHistory; ++n) {
        lags += core::at(lags_, n);
        alignments += core::at(alignments_, n);
    }
    const double inverse = static_cast<double>(kOne) / n;
    // How far along the rail: the frame's length times the average alignment and lag (at most one second's worth).
    const double ahead = ((inverse * alignments) * (inverse * lags)) * frame_seconds_;
    auto left = static_cast<float>(ahead);
    if (ahead > kOne)
        left = kOne;

    GrindPoint* node = rails->find_node(history_frame(past_frames_, play_idx_)->value80);
    if (!node || !(left > 0.0f))
        return;
    for (bool again = true; again; again = left > 0.0f) {
        node = move_along_rail(node, left > frame_seconds_ ? frame_seconds_ : left);
        if (!node) {
            // Off the rail's end: from the played position, as on the ground.
            entity_->at_point_ = play_pos_;
            get_extrapolate();
            return;
        }
        left = left - frame_seconds_;
    }
}

// 0x00516180 (its EAX - left over from the loop - isn't read)
void RiderRemoteSync::play_history_events()
{
    const net::AppHost* app = client_app();
    if (!app || past_frames_->state_count_ < kHistory)
        return;
    const int first = past_frames_->message_count_ % kHistory;
    const uint32_t now = app->clock_ - sync_frame_ + sync_time_;
    auto& messages = past_frames_->messages_;
    auto* blender = core::down_cast<AnimBlenderComponent>(anim_blender_);
    int i = first;
    for (bool again = true; again; again = i != first) {
        StateHistoryMessage* msg = &core::at(messages, i);
        if (msg->get_time() > last_event_time_ && msg->get_time() <= now) {
            switch (msg->ident_) {
            case 0x29: // an animation played (the primary channel)
                if (gfx::BlendChannel* channel = blender->find_channel(0))
                    channel->play_anim(msg->name_, false, msg->from_, msg->to_, static_cast<int8_t>(msg->loop_type_),
                                       msg->blend_period_, msg->speed_, nullptr);
                break;
            case 0x2A: // the board rotated (the byte passed as it was sent)
                core::down_cast<RiderSpinAndFlip>(flip_and_rotate_)
                    ->apply_board_turned(static_cast<uint32_t>(static_cast<int8_t>(msg->object_id_)), msg->rotated_, 0,
                                         false);
                break;
            case 0x2B:
                if (gfx::BlendChannel* channel = blender->find_channel(0))
                    channel->choose_wobble_goal(msg->wobble_target_, false);
                break;
            case 0x2C:
                if (gfx::BlendChannel* channel = blender->find_channel(0))
                    channel->set_flipped(static_cast<uint32_t>(static_cast<int8_t>(msg->object_id_)), msg->flipped_, 0,
                                         false); // (the byte passed as it was sent)
                break;
            case 0x3C: // (the five details passed by address)
                if (gfx::BlendChannel* channel = blender->find_channel(0))
                    channel->set_wobble_details(msg->wobble_details_, false);
                break;
            case 0x3D:
                if (gfx::BlendChannel* channel = blender->find_channel(0))
                    channel->change_repeat_mode(static_cast<int8_t>(msg->loop_type_), false);
                break;
            case 0x3E:
                mesh_set_->conceal_mesh(msg->name_, msg->hidden_, false);
                break;
            case 0x41:
                if (gfx::BlendChannel* channel = blender->find_channel(0))
                    channel->set_speed(msg->speed_, false, false, false);
                break;
            case 0x76: { // the model's three parts set going, by the message's bits
                mesh_set_->pivot_ = {.x = 0.0f, .y = 30.0f, .z = 0.0f, .w = 1.0f};
                const int time = get_now();
                for (uint32_t bit = 0; bit < 3; ++bit) {
                    if (msg->spin_axes_ & (1u << bit))
                        core::at(mesh_set_->twist_rate_, bit).append_one(
                            static_cast<float>(msg->spin_period_), static_cast<uint32_t>(time), msg->spin_start_,
                            msg->spin_range_, msg->spin_power_, msg->spin_hold_, false);
                }
                break;
            }
            case 0x77: { // a special item made: its +0x5CB5CB7E component told its name and owner
                auto* items = core::down_cast<PickupItemHandler>(entity_->fetch_module(PickupItemHandler::kType));
                script::Struct* structure = script::read_sub_struct(msg->name_); // (the original also passes 1)
                GameEntity* item = items->create_item(msg->item_slot_, structure);
                gsl::owner<script::Struct*> params = script::spawn_param_bag();
                if (params)
                    params = new (params) script::Struct;
                params->put_name_key(kItemName, msg->bone_);
                params->put_name_key(kItemOwner, entity_->ident_);
                item->fetch_module(kItemComponent)->configure_from_data(params);
                if (params) {
                    params->get_clear();
                    script::delete_struct(params);
                }
                break;
            }
            case 0x78: // the special item gone
                core::down_cast<PickupItemHandler>(entity_->fetch_module(PickupItemHandler::kType))
                    ->destroy_item(msg->item_slot_);
                break;
            case 0x8C:
                for (ModelSpinAxis& axis : mesh_set_->twist_rate_)
                    axis.get_clear();
                break;
            default:
                break;
            }
        }
        i = (i + 1) % kHistory;
    }
    last_event_time_ = now;
}

} // namespace objects
