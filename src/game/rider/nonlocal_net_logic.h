#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"

namespace objects {
struct GrindPoint;
} // namespace Obj

// The skater's non-local network logic component (SkaterNonLocalNetLogic, type 0x850EEF66, vtable 0x0064BFAC, 0x2C0
// bytes): another player's skater on this machine, moved from the states its client sends (the state history
// component keeps the last 20): played back a little behind, interpolated between states, and - when the states run
// late - extrapolated along the ground or a rail with the skater's own collision checks; its lighting, shadow, sound,
// animation events, flip / board rotation and state flags kept in step. From the disassembly of 0x005144B0-0x0051769F.
//
// Vtable: 0 Destroy, 1 Update, 3 Finalize, 10 the shared 0x005144C0 (ignores its argument), 12 the shared
// invoke_method_by_name 0x0051ADC0.
namespace objects {

class RiderRemoteSync : public ObjectModule {
public:
    static constexpr int kHistory = 20;

    // 0x005147F0 (registered by register_component_types): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x005144E0: the base, its vtable and type, no components, the two positions (0, 0, 0, 1), no updates. Returns
    // this.
    RiderRemoteSync* make_new();
    // 0x005144B0: the destructor body (the base's).
    void destroy_body();
    // 0x00514550 (vtable slot 0)
    RiderRemoteSync* teardown(unsigned int flags);
    // 0x00514580 (vtable +0x0C): the state history, state, end run, animation blender, flip-and-rotate, model and
    // shadow components found.
    void seal();
    // 0x00517270 (vtable +0x04): the frame: the light and shadow from the ground (update_ground_lighting), the state
    // played back (Interpolate); when this machine's player plays (not observing) in a game that extrapolates
    // (0x00534340, or firefight) and the history is full, extrapolated (on a rail extrapolate_on_rail, else Extrapolate
    // and settle_onto_floor); the history's events (play_history_events); the out-of-a-lip flag; driving in or out told to
    // the scripts (0x7853F8C4 / 0x0F02E44F with skaterId / control_type); the looping sound kept to the state.
    void tick_frame();

    // 0x005144D0: the playback restarted (the synced time and the last event time cleared).
    void reset_playback();
    // 0x00514610: a feeler from just above the skater down 4800: on a hit (unless on a rail, and only on triangle
    // collision) the model's light set from the hit's vertex values; the shadow placed at the hit point and normal.
    void update_ground_lighting();
    // 0x00514850: kept to the ground: a feeler along the up row from Physics_Ground_Snap_Up above to 200 below; a
    // hit on skatable ground close enough (within the frame's movement's slope, at least Physics_Ground_Snap_Down)
    // and not too steep (Ground_stick_angle_forward) makes it the ground (the up row and the position); a hit not
    // skatable that the skater is below is left by one unit along its normal.
    void settle_onto_floor();
    // 0x00514D10: the velocity and the matrix turned (about y) towards `dir` by Wall_Bounce_Angle_Multiplier times
    // the angle between it and the right row (folded to at most pi / 2): that turn into `turn`; the angle returned.
    double turn_towards(maths::Vec4 dir, float* turn);
    // 0x00514E20: a wall met while extrapolating (`normal`, `point`): turned along it (slowed past
    // Wall_Bounce_Dont_Slow_Angle), put back at the point (Skater_First_Forward_Collision_Height up the up row, 6
    // out along the normal); if a feeler ahead (Skater_First_Forward_Collision_Length) hits again within 240, turned
    // round by what's left of a half turn and halved.
    void deflect_from_wall(const maths::Vec4* normal, const maths::Vec4* point);

    // --- nonlocal_playback.cpp ---
    // 0x00515940: the states played back. The first time, from the newest state (a second's frames behind it): put
    // there standing, shown (script 0x1011A5D6), lit. After: the playback time (the client app's frames since then)
    // kept within the history (fallen back when past its newest state, ten frames on when ten or more behind); the
    // state reached and the one before it: the matrix slerped between them (the earlier one's when their at rows
    // differ by more than 0.75), the position interpolated (put at the later one's when 600 or more apart), the
    // velocity from the move and averaged with the updates' moves (quirk kept: the sum starts from the position);
    // the state, trick, terrain, physics mode, driving, end run flags and timed flags as the state has them.
    void interpolate();
    // 0x00515360: on along the ground: the update's speed, lag (the connection's latency in frames) and alignment
    // (with this machine's skater's velocity) kept; the position moved along the last move's direction, flattened,
    // by the averages; a wall between the played and the new position (at the collision height): deflect_from_wall.
    void get_extrapolate();
    // 0x00516E00: on along the rail (the state's node, +0x80): the update's values kept as Extrapolate's; the
    // average alignment and lag times the frame's length (at most 1 s) moved along it a frame at a time
    // (move_along_rail); off its end: from the played position, Extrapolate.
    void extrapolate_on_rail();
    // 0x00516640: `seconds` along the rail segment from `node`: the rail's pull (physics value 0xD1F46992) on the
    // velocity, which way along it; past the segment's end: onto the next (active, clear - RailClear - and turning
    // less than the physics value 0x76C1DA15's angle) with the overshoot, else 0; the velocity and matrix along the
    // segment, the move. Returns the segment's node.
    GrindPoint* move_along_rail(GrindPoint* node, float seconds);
    // 0x00516180: the history's messages since the last played, up to the playback time: animations (the blender's
    // primary channel), the board's rotation, the model's parts, special items made and destroyed.
    void play_history_events();

    uint32_t sync_frame_;           // +0x18 the client app's frame when playback started (0: not yet)
    int sync_time_;                 // +0x1C the history's time then, less the app's frames since
    uint32_t last_event_time_;       // +0x20 the history's events played up to here
    int play_idx_;                    // +0x24 the history entry being played
    maths::Vec4 play_pos_;          // +0x28 the position playback put the skater at
    maths::Vec4 last_play_pos_;      // +0x38 the one before
    maths::Vec4 last_pos_;          // +0x48 the object's position before this frame
    maths::Vec4 surface_up_;     // +0x58 the last ground snapped to
    float speeds_[kHistory];       // +0x68 per update: the playback's speed
    float lags_[kHistory];         // +0xB8 per update: how far behind the connection is
    float alignments_[kHistory];   // +0x108 per update: how the move lines up with the local skater's
    maths::Vec4 moves_[kHistory];  // +0x158 per update: the playback's move
    int updates_;                  // +0x298
    float frame_seconds_;           // +0x29C this frame's length
    uint8_t was_driving_;           // +0x2A0
    uint8_t pad2a1_[3];
    uint8_t* past_frames_;             // +0x2A4 SkaterStateHistory (0x18223FD6)
    ObjectModule* anim_blender_;  // +0x2A8 (0x7149B753)
    uint8_t* mesh_set_;               // +0x2AC Model (0x286A8D26)
    uint8_t* cur_phase_;               // +0x2B0 SkaterState (0x43686585)
    uint8_t* run_over_;              // +0x2B4 SkaterEndRun (0xB1D5A3E1)
    ObjectModule* flip_and_rotate_;// +0x2B8 SkaterFlipAndRotate (0x488AF07A)
    ObjectModule* shadow_;       // +0x2BC Shadow (0x8A897DD2)
};
static_assert(offsetof(RiderRemoteSync, sync_frame_) == 0x18 &&
                  offsetof(RiderRemoteSync, play_pos_) == 0x28 &&
                  offsetof(RiderRemoteSync, last_pos_) == 0x48 &&
                  offsetof(RiderRemoteSync, surface_up_) == 0x58 &&
                  offsetof(RiderRemoteSync, speeds_) == 0x68 &&
                  offsetof(RiderRemoteSync, lags_) == 0xB8 &&
                  offsetof(RiderRemoteSync, alignments_) == 0x108 &&
                  offsetof(RiderRemoteSync, moves_) == 0x158 &&
                  offsetof(RiderRemoteSync, updates_) == 0x298 &&
                  offsetof(RiderRemoteSync, past_frames_) == 0x2A4 &&
                  offsetof(RiderRemoteSync, shadow_) == 0x2BC &&
                  sizeof(RiderRemoteSync) == 0x2C0,
              "layout must match the original (0x005147F0 allocates 0x2C0 bytes)");

} // namespace Obj
