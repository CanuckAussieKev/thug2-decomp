#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace maths {
class ArcRotationBlender;
} // namespace maths

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class RiderPoseState;
class RiderBoardPhysics;

class FreeLookModule;
struct WalkCameraState;

// The skater camera component (type 0x5E43A604 "skatercamera", vtable 0x0064BA74, 0x17C bytes; made by 0x004FA800):
// the camera that follows a skating skater - behind and above it (its distances eased with a zoom), swung round
// after the skater (its own orientation, slerped through an interpolator), looking up or down in vert air or on a
// lip, with a shake, kept clear of the world. Its camera mode (Skater_Camera_Array) sets the distances, rates, zooms
// and tilts.
//
// Vtable: 0 the scalar deleting destructor (0x004FA590), 1 Update (0x004FA860), 2 configure_from_data (0x004FA5C0),
// 3 Finalize (0x004F9A20), 12 invoke_method_by_name (0x004FA680).
class RiderChaseView final : public ObjectModule {
public:
    RiderChaseView(const RiderChaseView&) = delete;
    RiderChaseView& operator=(const RiderChaseView&) = delete;
    // 0x004FA800: a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x004FA4B0: its type, the interpolator (0x98 bytes), the zooms and rates at
    // their defaults, no skater.
    RiderChaseView();
    // 0x004F99F0: the destructor body: the interpolator freed, the base's.
    ~RiderChaseView() override;
    // 0x004FA5C0 (slot 2): the camera object's matrix made the identity and its orientation the same; the skater
    // from 0x4C48DA58 (an object id, asserted).
    void configure_from_data(script::Struct* params) override;
    // 0x004F9A20 (slot 3): the camera lookaround component (0x81C7122D).
    void seal() override;
    // 0x004FA680 (slot 12): 0x75A516D3 - a shake from duration, vert_amp, horiz_amp, vert_vel, horiz_vel;
    // 0xD7867FFE - the camera mode from 0x6835B854 (asserted). 1, or 2 for anything else.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004FA860 (slot 1): one frame (skater_camera_update.cpp).
    void tick_frame() override;

    // 0x004FA3C0: the skater followed, its SkaterState and SkaterPhysicsControl components (none: all null); 3
    // instant frames.
    void bind_rider(GameEntity* skater);
    // 0x004FA420: its orientation, target, turn cosine and zoom kept in `state` (as the walk camera's).
    void save_state(WalkCameraState* state);
    // 0x004F9A40: taken over from another camera: its orientation's first three rows, target, turn cosine and zoom;
    // the settling time and the instant frames cleared, the lookaround's heading and lock too.
    void change_phase(const WalkCameraState* state);
    // 0x004F9970 (not a Ghidra function): one frame (1/60 s) of the behind / above distances' lerps.
    void update_lerps();
    // 0x004F9AE0: camera mode `mode` (Skater_Camera_Array, or the 2P split screen's arrays): its values read; the
    // behind / above distances (feet) lerped over `time` seconds (none: at once); the field of view (half with a
    // vertical split); the lookaround's heading by the mode (5/6 behind, 7/8 a quarter turn, 11/12 the other, else 0).
    void change_regime(int mode, float time);
    // 0x004FA7C0 / 0x004FA7D0: the current camera mode set again / the next one (1 to 4, round again; nothing from 5
    // up) - change_regime with a blend of 1.
    void restore_default_state();
    void next_mode();
    // 0x004F9EB0: whether to look straight down: a skater whose state's +0x1C is clear, on a lip (state 3), in vert air
    // or with the state's +0xF4 set.
    bool get_look_down();
    // 0x004F9F00 (RET 8): the shake applied to `position` along the orientation's first two rows; its time run down
    // by the frame.
    void apply_shake(maths::Vec4* position, const maths::Mat4* orientation);
    // 0x004FA020 (RET 8): the point followed: the skater's position (at once with `instant` or when the state's +0xA0
    // says so), else eased toward it - across at lerp_xz_ (in vert air vert_air_lerp_xz_), up at lerp_y_
    // (halfpipe_up_blend_; at once when the state's +0x98), both scaled by rate_scale_ (rising back to 1 over rate_time_
    // seconds at rate_rise_). Into `out`.
    void fetch_goal(maths::Vec4* out, bool instant);
    // 0x004FA230 (RET 8): the height above the skater (above_, on a lip lip_trick_above_, from 3 while zoomed in) and
    // the distance behind (behind_ x the zoom, which eases at zoom_ease_rate_ toward 1 - in vert air big_air_trick_zoom_, on
    // a rail rail_zoom_factor_, on a lip lip_trick_zoom_ - times the look-around's zoom; nearer as the look-around tilts
    // down). Nothing without a skater.
    void get_zoomed_offsets(float* height, float* distance);

    // The fields other code reads and sets.
    void set_skater_matrix(const maths::Mat4& value) { skater_matrix_ = value; }
    [[nodiscard]] float behind() const { return behind_; }
    [[nodiscard]] float above() const { return above_; }
    [[nodiscard]] float lerp_xz() const { return lerp_xz_; }
    [[nodiscard]] float lerp_y() const { return lerp_y_; }
    [[nodiscard]] float zoom_ease_rate() const { return zoom_ease_rate_; }
    [[nodiscard]] float lean_angle() const { return lean_angle_; }

private:
    std::array<uint8_t, 0x24 - 0x18> unknown18_{};
    int mode_sel_{0};            // +0x24 the camera mode (an index of Skater_Camera_Array)
    int instant_frames_{};       // +0x28 frames that jump straight to the target
    float turn_cosine_;          // +0x2C the last turn's cosine (the new facing against the old)
    maths::Vec4 aim_point_{};    // +0x30 the point followed
    std::array<maths::Vec4, 3> orientation_{}; // +0x40 (the view's first three rows)
    int wall_ride_frames_{};       // +0x70
    maths::Mat4 skater_matrix_{};  // +0x74 the skater's orientation last frame
    maths::Vec4 wall_facing_{};    // +0xB4 the wall's facing when the wall ride began (flat)
    float behind_{};               // +0xC4 behind (inches)
    float behind_time_{};          // +0xC8 left to lerp
    float behind_step_{};          // +0xCC a frame's
    float above_{};                // +0xD0 above (inches)
    float above_time_{};           // +0xD4
    float above_step_{};           // +0xD8
    float settle_time_{};          // +0xDC the slower slerp after vert air, left (seconds)
    float horiz_fov_{};            // +0xE0 horiz_fov
    float lerp_xz_;              // +0xE4 lerp_xz
    float lerp_y_;               // +0xE8 lerp_y
    float vert_air_lerp_xz_;     // +0xEC vert_air_lerp_xz
    float halfpipe_up_blend_;    // +0xF0 vert_air_lerp_y
    float look_rate_;            // +0xF4 the roll's ease (the mode's 0xBEF0D195; name not recovered)
    float rate_time_{};          // +0xF8 seconds left of the rates' rise back to 1
    float rate_rise_{};          // +0xFC
    float rate_scale_{};         // +0x100
    uint8_t vert_zoom_on_{};     // +0x104 the vert-air zoom on (0 / 1)
    std::array<uint8_t, 3> pad105{};
    float zoom_;                  // +0x108 the zoom now
    float zoom_target_{};         // +0x10C
    float zoom_ease_rate_;        // +0x110 zoom_lerp
    float big_air_trick_zoom_{};  // +0x114 big_air_trick_zoom (in vert air)
    float rail_zoom_factor_{};    // +0x118 grind_zoom
    float lip_trick_zoom_{};      // +0x11C lip_trick_zoom
    int lip_state_{};             // +0x120 0 none, 1 looking along the lip, 2 blocked: the other way
    float wallride_offset_angle_{}; // +0x124 wallride_offset_angle
    float roll_{};                  // +0x128
    float lean_angle_{};            // +0x12C tilt
    float air_tilt_{};              // +0x130
    float lip_trick_tilt_{};        // +0x134 lip_trick_tilt
    float lip_trick_above_{};       // +0x138 lip_trick_above
    std::array<uint8_t, 4> unknown13c_{};
    float slerp_{};                          // +0x140 slerp
    float vert_air_slerp_{};                 // +0x144 vert_air_slerp
    float vert_air_landed_slerp_{};          // +0x148 vert_air_landed_slerp (while settling)
    float origin_offset_{};                  // +0x14C origin_offset (inches)
    float shake_left_{};                     // +0x150 the shake's time left
    float shake_length_{};                   // +0x154 its length
    float shake_amp_x_{};                    // +0x158 amplitudes
    float shake_amp_y_{};                    // +0x15C
    float shake_rate_x_{};                   // +0x160 rates
    float shake_rate_y_{};                   // +0x164
    FreeLookModule* free_look_module_{};     // +0x168
    maths::ArcRotationBlender* arc_blender_; // +0x16C
    GameEntity* rider_ref_;                  // +0x170
    RiderPoseState* state_ref_;              // +0x174 the skater's SkaterState
    RiderBoardPhysics* control_;             // +0x178 the skater's SkaterPhysicsControl
};

// The skater's cleanup-state component (type 0xD8CC3428 "SkaterCleanupState", vtable 0x0064BB48, 0x1C bytes; made
// by 0x004FBE20): each frame the skater's shadow and light follow it (Rider::update_shadow_light).
class RiderStateReset final : public ObjectModule {
public:
    RiderStateReset(const RiderStateReset&) = delete;
    RiderStateReset& operator=(const RiderStateReset&) = delete;
    // 0x004FBE20: a new one (the constructor inlined: the base's, its vtable and type; 0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderStateReset();
    // 0x004FBD90: the destructor body (the base's). (0x004FBDB0 the deleting one)
    ~RiderStateReset() override;
    // 0x004FBDA0 (slot 2; `params` isn't used): the state cleared. (Two other components' vtables, 0x0064BEF8 and
    // 0x0064C054, share this function.)
    void configure_from_data(script::Struct* params) override;
    // 0x004FBDE0 (slot 3): the object's SkaterState component.
    void seal() override;
    // 0x004FBE00 (slot 1): Rider::update_shadow_light with the object's position and matrix.
    void tick_frame() override;

    ObjectModule* state_ref_{}; // +0x18 the object's SkaterState
};

// 0x0064BA74: skaterCameraVtable (defined where the class's functions are).

} // namespace objects
