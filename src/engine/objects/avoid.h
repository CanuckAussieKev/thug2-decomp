#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// The Avoid component (type 0x6EB050ED, vtable 0x0064B220, 0xDC bytes; made by 0x004E39E0): a ped that jumps out
// of the local skater's way - to its side, or ahead / back, or away from a herd point - playing a jump animation
// (JumpLeft / JumpRight / JumpBack / JumpForward of the parameters) while it moves along a slerp from where it was
// to where it lands. Its script member functions are in the table at 0x006839E8 (filled by 0x00642ED0; their
// names' checksums aren't known): the jump, the step along it, its end, the stored and herd points, and the
// motion component's "don't move" flag (0x4) kept / given back.
//
// Vtable: 0 the scalar deleting destructor; Update 0x0051B970, the empty 0x005BAB90 / 0x005480A0, 0x0042BB20 and
// invoke_method_by_name 0x0051ADC0 (shared with other classes, not rewritten here); the base's Suspend.
class ObstacleDodgeModule final : public ObjectModule {
public:
    // 0x004E39E0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // 0x004E3310 (not a Ghidra function; registered with Create): its script member functions (the table at
    // 0x006839E8) registered.
    static void publish_method_table();
    // 0x004E3660: its type. The rest isn't initialised.
    ObstacleDodgeModule();
    // 0x004E3330: the destructor body (the base's).
    ~ObstacleDodgeModule() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }

    // 0x004E34C0: the angle (degrees, as get_angle_in_plane of the display matrix) to the side of the object
    // further from the local skater, across the skater's horizontal velocity.
    float side_away_from_skater();
    // 0x004E3A40: `out` = `start` turned by `angle` degrees about y; where `distance` (+15 up the turned matrix's
    // at, 10 up) can be reached from `start`'s position without a collision, the end `distance` along the turned
    // at becomes `out`'s position. True if it's at least 30 from the local skater ("Too close to skater" printed
    // otherwise) and within `max_distance` of the stored point (in x and z).
    bool try_jump(const maths::Mat4& start, maths::Mat4& out, float angle, float distance, float max_distance);
    // 0x004E3340: the jump animation for the quarter `angle` (radians: 0 / 4 ahead, 1 left, 2 back, 3 right)
    // played on the animation blender, flipped when only the other side's is given; its duration kept.
    void play_jump(float angle, script::Struct* params);

    // --- script member functions (the table at 0x006839E8) ---
    // 0x004E3BD0 [0x6157CBC0]: jump away from the skater (to the far side, then the other side, then back, then
    // away from the stored point - each tried with its distance distLeft / distBack / distRight / distForward and
    // maxHerdDistance, 100 by default). False if nowhere is free.
    bool get_jump(script::Struct* params, script::Script* script);
    // 0x004E36F0 [0xD2AA4D2D]: reads dist (asserting it's there); always false (a debug box is worked out and
    // dropped).
    bool debug_box(script::Struct* params, script::Script* script);
    // 0x004E3EC0 [0x41226942]: one frame of the jump: along the slerp by the frame's time over the animation's
    // duration; the object placed and turned there (its orientation's fourth row 0, 0, 0, 1). True.
    bool step_jump(script::Struct* params, script::Script* script);
    // 0x004E37D0 [0x3C7F8F3E]: the jump is over (the slerp at 1 or more and the animation blender's 0x00419CD0).
    bool jump_finished(script::Struct* params, script::Script* script);
    // 0x004E3810 [0x6A4BCA91]: the herd point = the object's position. True.
    bool store_position(script::Struct* params, script::Script* script);
    // 0x004E3850 [0x3FB422E8]: the second point = the motion component's +0x74 if it has 0x00423510's, else the
    // object's position. True.
    bool store_target(script::Struct* params, script::Script* script);
    // 0x004E38C0 [0x539A32DC]: the motion component's flag 0x4 set (and remembered as set). True.
    bool hold_motion(script::Struct* params, script::Script* script);
    // 0x004E38F0 [0x53BC3B4E]: the motion component's flag 0x4 back as remembered. True.
    bool restore_motion(script::Struct* params, script::Script* script);

    float lasts_for_{};                 // +0x18 the jump animation's
    maths::ArcRotationBlender slerp_{}; // +0x1C from where it was to where it lands
    float progress_{};                  // +0xB4 along the slerp
    maths::Vec4 herd_point_{};          // +0xB8
    maths::Vec4 aim_point_{};           // +0xC8
    bool motion_held_{};                // +0xD8
    uint8_t padd9_[3]{};
};
static_assert(sizeof(maths::ArcRotationBlender) == 0x98, "layout must match the original");
static_assert(offsetof(ObstacleDodgeModule, lasts_for_) == 0x18 && offsetof(ObstacleDodgeModule, slerp_) == 0x1C &&
                  offsetof(ObstacleDodgeModule, progress_) == 0xB4 &&
                  offsetof(ObstacleDodgeModule, herd_point_) == 0xB8 &&
                  offsetof(ObstacleDodgeModule, aim_point_) == 0xC8 &&
                  offsetof(ObstacleDodgeModule, motion_held_) == 0xD8 && sizeof(ObstacleDodgeModule) == 0xDC,
              "layout must match the original");

// 0x004E3270 (cdecl, `params` in ESI): the distance for the quarter `angle` falls in (as play_jump's quarters, but
// distLeft / distBack / distRight / distForward), 0 if not given.
float avoid_distance(float angle, script::Struct* params);
// 0x004E3930 (`start` in EDI, `end` in ESI): whether the collision feeler hits anything from `start` to `end`.
bool line_collides(const maths::Vec4& start, const maths::Vec4& end);

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_avoid_member_functions[54];
} // namespace objects
