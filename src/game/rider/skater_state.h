#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/mat4.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The skater's state component (SkaterState, type 0x43686585, vtable 0x0064C140, 0x128 bytes): where the skater is
// (on the ground, in the air, on a wall / lip / rail, in a wallplant), whether it skates, walks or drives, its
// terrain, the powerups it picked up and where its sparks come from - read by the physics components and the scripts
// (OnGround ... GetSparksPos), each change of state, physics or terrain sent to the skater as an event. Most of its
// fields are written by the core physics code (not rewritten yet). From the disassembly of 0x00519430-0x005197B4
// (0x00519430-0x00519600, 0x00519650 and 0x005196D0-0x00519720 aren't Ghidra functions). Also here: three
// functions of the skater's state history (0x005197C0-0x005197EF).
//
// Vtable: 0 Destroy, 1 the shared empty 0x0051B970, 2 configure_from_data, 11 the shared 0x0042BB20, 12 the shared
// invoke_method_by_name 0x0051ADC0.
namespace objects {

// A flag of the skater's state and the time it last changed (the game clock plus g_clock_offset).
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct TimedFlag {
    uint8_t on;
    int changed_at; // +4
};
// NOLINTEND(readability-identifier-naming)

// The skater's state flags (RiderPoseState::timed_flags_), by what sets them.
enum class PoseFlag : int {
    kCrouched = 0,         // +0x20 the jump button held (crouching to jump)
    kFlipped = 1,          // +0x28 riding the other way round (switch)
    kVertAir = 2,          // +0x30 in the air off a vert ramp
    kVertWall = 3,         // +0x38 the vert air tracking its wall (cleared when it breaks away)
    kOnVert = 4,           // +0x40 on a vert face (the ground's hit was vert)
    kLeftVert = 5,         // +0x48 just left a vert ramp (cleared by the jump button's wait or a timeout)
    kLipToAir = 6,         // +0x50 out of a lip or a rail into the air (rail re-grab wait)
    kSlide = 7,            // +0x58 sliding (SetSlide / the reverts)
    kDescended = 8,        // +0x60 on the ground or moving down (the adjust physics component's)
    kSpin = 9,             // +0x68 a spin allowed (the air's turn to face the ramp)
    kBailing = 10,         // +0x70 in a bail
    kAcidDrop = 11,        // +0x78 in an acid drop
    kRecoverInAir = 12,    // +0x80 righting itself in the air over a landing (or both triggers)
    kSkitching = 13,       // +0x88
    kCancelGround = 14,    // +0x90 the ground checks cancelled (a script's)
    kGroundSnapped = 15,   // +0x98 snapped onto the ground this frame
    kWallplanted = 16,     // +0xA0 wallplanted this frame
    kFromAcidDrop = 17,    // +0xA8 this air came from an acid drop
    kNoAcidDrop = 18,      // +0xB0 no acid drop in this air (vert, a drop, a bail, a ceiling or wall hit)
    kCancelWallpush = 19,  // +0xB8
    kNoSecondSpin = 20,    // +0xC0
    kFirstTimeOnRail = 21, // +0xC8 a new rail's script (not the last one's)
    kJumpedOffRail = 22,   // +0xD0
    kJumped = 23,          // +0xD8 jumped off the ground, a rail, a lip or a wall
};
constexpr int kPoseFlagCount = 24;

class RiderPoseState final : public ObjectModule {
public:
    // The skater's state (+0x18).
    enum State : int { kGround = 0, kAir = 1, kWall = 2, kLip = 3, kRail = 4, kWallplant = 5 };
    // The physics running it (+0x1C).
    enum PhysicsMode : int { kSkating = 0, kWalking = 1 };

    // 0x00519740 (registered by register_component_types): a new one (0 if out of memory), no powerups.
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderPoseState();
    // 0x00519430 (registered with Create): its script member functions (the table at 0x00684A80) registered.
    static void publish_method_table();
    // 0x00519450: the destructor body (the base's).
    ~RiderPoseState() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x00519460 (vtable +0x08): in the air. `params` isn't read.
    void configure_from_data(script::Struct* params) override;

    // 0x00519660 / 0x00519680 / 0x005196A0: the state / the physics mode / the terrain; when it changes the skater
    // gets OnSkaterStateChange / OnSkaterPhysicsStateChange / OnSkaterTerrainChange.
    void change_phase(int state);
    void set_physics_mode(int mode);
    void set_surface_kind(uint8_t terrain);
    // 0x00519470: the powerups dropped (called by a script command).
    void clear_powerups();
    // 0x00519650: +0xE0 (doingtrick) cleared (called by the skater).
    void clear_doing_trick();

    // --- script member functions (the table at 0x00684A80; params, script) ---
    // (OnGround is 0x00525E90's, not here)
    bool in_air(script::Struct* params, script::Script* script);        // 0x00519480
    bool get_on_wall(script::Struct* params, script::Script* script);   // 0x00519490
    bool on_lip(script::Struct* params, script::Script* script);        // 0x005194A0
    bool get_on_rail(script::Struct* params, script::Script* script);   // 0x005194B0
    bool in_wallplant(script::Struct* params, script::Script* script);  // 0x005194C0
    bool in_bail_state(script::Struct* params, script::Script* script); // 0x005196D0 (kBailing)
    bool on_foot(script::Struct* params, script::Script* script);       // 0x005194D0 (not driving)
    bool skating(script::Struct* params, script::Script* script);       // 0x005194F0 (not driving)
    bool get_driving(script::Struct* params, script::Script* script);   // 0x00519510
    bool set_driving(script::Struct* params, script::Script* script);   // 0x00519520
    bool unset_driving(script::Struct* params, script::Script* script); // 0x00519530
    bool in_air_move(script::Struct* params, script::Script* script);   // 0x005196E0 (+0xE0)
    // 0x00519540 / 0x005195B0: the powerup the unnamed checksum names (the one in the table at 0x0067F09C) - picked
    // up or not / picked up. Unknown names: false / nothing.
    bool has_powerup(script::Struct* params, script::Script* script);
    bool picked_up_powerup(script::Struct* params, script::Script* script);
    bool get_surface_kind(script::Struct* params, script::Script* script); // 0x005196F0 (terrain)
    bool get_sparks_pos(script::Struct* params, script::Script* script);   // 0x00519720 (SparksPos)
    bool set_sparks_pos(script::Struct* params, script::Script* script);   // 0x00519600 (the unnamed checksum)

    // The timed flags (+0x20): a byte each and the time (the game clock plus g_clock_offset) it last changed.
    TimedFlag& flag(PoseFlag f) { return timed_flags_[static_cast<int>(f)]; }
    const TimedFlag& flag(PoseFlag f) const { return timed_flags_[static_cast<int>(f)]; }
    uint8_t is_on(PoseFlag f) const { return flag(f).on; }
    // The flag set to `value`; its time noted if it changes.
    void set_flag(PoseFlag f, uint8_t value);

    int cur_phase_;                         // +0x18 State
    int physics_mode_;                      // +0x1C PhysicsMode (-1 until the physics control component sets it)
    TimedFlag timed_flags_[kPoseFlagCount]; // +0x20 (PoseFlag; kBailing is +0x70)
    uint8_t in_trick_move_;                 // +0xE0
    uint8_t surface_type_;                  // +0xE1
    maths::Vec4 drop_velocity_;             // +0xE4 an acid drop's horizontal velocity, kept through the drop
    uint8_t lip_straight_up_;               // +0xF4 out of a lip trick straight up (the skater camera's)
    maths::Vec4 drawn_normal_;              // +0xF8 the ground's normal as drawn (the core's, each frame)
    maths::Vec4 floor_normal_;              // +0x108 the ground's normal under the skater (the core's)
    uint8_t in_vehicle_;                    // +0x118
    float height_above_ground_;             // +0x11C (the height it took off from / is over the ground)
    uint8_t boost_items_[1];                // +0x120 (one per name in 0x0067F09C)
    uint32_t sparks_pos_;                   // +0x124 a checksum
};

// The skater's state history (SkaterStateHistory, state_history.h): a state a non-local skater was sent (0x84 bytes;
// 20 of them from the component's +0x1C), its time as two halves. (Its destructor body 0x005197C0 is here too.)
struct SkaterStateHistoryFrame {
    // 0x005197D0 / 0x005197E0: the frame's time (time the low 16 bits, wraps the high).
    uint32_t get_time();
    void change_moment(uint32_t time);

    int16_t pos[3]; // +0x00 the position, in half units (y in quarters)
    uint16_t pad06;
    maths::Mat4 matrix;   // +0x08 from the angles
    maths::Vec4 position; // +0x48 (w 0)
    float angles[3];      // +0x58
    uint32_t unknown64;
    uint32_t value68;   // +0x68 (5 bits: the state flags' bits are its bits... the original's)
    uint32_t value_6c_; // +0x6C (3 bits)
    int state;          // +0x70 (3 bits; kept as a signed byte)
    uint8_t flag74;     // +0x74 (always 0 from a message: bit 7 of a 4-bit value)
    uint8_t flag75;     // +0x75
    uint8_t flag76;     // +0x76
    uint8_t pad77;
    uint16_t time;     // +0x78 the message's time stamp
    uint16_t wraps;    // +0x7A the times the time stamp went back
    uint8_t value_7c_; // +0x7C (6 bits)
    uint8_t pad7d_[3];
    uint32_t value80; // +0x80 (32 bits)
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_skater_state_member_functions[114];
} // namespace objects
