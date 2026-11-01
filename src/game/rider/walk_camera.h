#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class RiderBoardPhysics;
class RiderChaseView;
class ViewpointModule;

class FreeLookModule;
class FootMovement;

// What the camera keeps for a later restore (WalkCamera's save_state / restore_state: 0x58 bytes).
struct WalkCameraState {
    maths::Mat4 orientation; // +0x00
    maths::Vec4 target;      // +0x40
    float turn_cosine_;      // +0x50
    float zoom;              // +0x54
};

// 0x00524D70 (cdecl): `a` toward `b` by where `t` lies from `low` to `high` (x through a float step, y with its
// step unrounded, z and w as floats). Returns `out`.
maths::Vec4* lerp_by_range(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float t, float low,
                           float high);

// The WalkCamera component (type 0xC7D37EC2, vtable 0x0064C5E0, 0xA8 bytes; made by 0x00525DC0): the camera
// behind a walking skater. Each frame it swings round toward the skater's facing (when the right stick isn't
// turning it, the skater moves within lock_angle of where it looks and fast enough - min_slerp_speed to
// full_slerp_speed, or the d-pad's pair), turns with the right stick (facing_control), slerps its orientation
// toward that at matrix_slerp_rate (scaled for running, hanging and the stick; flushed at once on request),
// follows the skater's position (eased on two rates of the skater camera's), sits behind and above it (the
// skater camera's distances, zoomed by the look-around), looks at a point above the skater and keeps clear of the
// world. The parameters are the global structure walk_camera_parameters.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize, 12 invoke_method_by_name.
class FootView final : public ObjectModule {
public:
    FootView(const FootView&) = delete;
    FootView& operator=(const FootView&) = delete;
    // 0x00525DC0: a new one (0 if out of memory): the base's constructor, its vtable and type, the target (0, 0,
    // 0, 1).
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    FootView();
    // 0x00524C20: the destructor body (the base's). 0x00524F20 (slot 0): the scalar deleting destructor.
    ~FootView() override;
    // 0x00524F50 (slot 2): its skater from 0x4C48DA58 (an object id, asserted; name not recovered), the zoom and
    // the turn cosine 1, its orientation from the camera object's matrix.
    void configure_from_data(script::Struct* params) override;
    // 0x00524FB0 (slot 3): the object's CameraLookAround, Camera and skatercamera components.
    void seal() override;
    // 0x00525D40 (slot 12): WalkCamera_FlushRequest (apply_queued_change's work) and WalkCamera_Reset (reset_now); 2
    // for anything else.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x005251D0 (slot 1): one frame (see the class).
    void tick_frame() override;

    // 0x00524C50: the skater followed, its Walk and SkaterPhysicsControl components (none: all null).
    void bind_rider(GameEntity* skater);
    // 0x00524C30: an Update (through the vtable) that jumps straight to where it should be.
    void reset_now();
    // 0x005250A0: the next updates turn straight to the skater's facing (the Walk component told too), the
    // look-around back to straight ahead, unlocked, its factor 1.
    void apply_queued_change();
    // 0x00525F70: look along `facing` (turned by the look-around heading) at `rate`, until restore_state.
    void change_forced_heading(const maths::Vec4* facing, float rate);
    // 0x00525060 / 0x00524FF0: its orientation, target, turn cosine and zoom saved / restored (restoring also
    // ends a flush, the instant frames, an override and the look-around's heading and lock).
    void save_state(WalkCameraState* state);
    void restore_state(const WalkCameraState* state);
    // 0x00524EF0 (stdcall; `this` isn't used): the float `name` of walk_camera_parameters (asserted).
    static float get_param(uint32_t name);
    // 0x005250E0: the point followed: the skater's position (at once when `instant`), else eased toward it at the
    // skater camera's rates (+0xE4 across, +0xE8 up) over the frame. Written to `out`.
    void fetch_goal(maths::Vec4* out, bool instant);
    // 0x00524CB0: the height above the skater (the skater camera's +0xD0, from 3 when zoomed in) and the
    // distance behind (+0xC4 x the zoom, nearer when the look-around tilts up). The zoom eases toward the
    // look-around's factor while it turns (else 1) at the skater camera's +0x110.
    void get_zoomed_offsets(float* height, float* distance);

    int instant_frames_{}; // +0x18 frames left that jump straight to the target (3 after a teleport)
    bool flush_{};         // +0x1C a flush requested
    bool snap_next_{};     // +0x1D (reset_now)
    uint8_t pad1e_[2]{};
    float zoom_{};              // +0x20
    maths::Vec4 aim_point_{};   // +0x24
    maths::Mat4 orientation_{}; // +0x34
    float turn_cosine_{};       // +0x74 the last turn's cosine (the new at-row against the old)
    bool override_{};           // +0x78 (change_forced_heading)
    uint8_t pad79[3]{};
    maths::Vec4 forced_heading_{};         // +0x7C
    float override_rate_{};                // +0x8C
    FreeLookModule* free_look_{};          // +0x90
    ViewpointModule* viewer_{};            // +0x94 the object's camera component
    RiderChaseView* skater_camera_{};      // +0x98 (the parameters it shares)
    GameEntity* skater_{};                 // +0x9C
    FootMovement* walk_{};                 // +0xA0
    RiderBoardPhysics* physics_control_{}; // +0xA4
};

} // namespace objects
