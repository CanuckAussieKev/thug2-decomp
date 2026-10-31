#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_rect.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The Vehicle component (GameObjects::DrivableModule, type 0xE47F1B79 "Vehicle", vtable 0x00646770, 0x1334 bytes; made by
// 0x0043AA30): the driveable cars' physics. A rigid body (position, linear and angular momentum, orientation as a
// quaternion; inverse mass and inverse moments of inertia) on wheels, each a suspension spring along the body's up
// axis with a spinning tyre: the tyres' grip (static, dynamic, sliding, handbrake states), the engine's torque through
// the gears, brakes and handbrake, and the steering (Ackermann: the inner wheel turned further). Two boxes ("colliders",
// each a corner and two edges) are tested against the world's collision with a rect feeler; their contact points get
// impulses (restitution, friction) and a depth correction. Each frame the update steps the physics in up to six equal
// sub-steps of at most 1/30 s. From the skater's input when driven; asleep (awake_ 0) once it has stood still for
// three frames. With a skater (rider_shown_), its animation follows the steering.
//
// Vtable: 0 Destroy 0x0043C1A0, 1 Update 0x0043C1D0, 2 configure_from_data 0x00437D10, 3 Finalize 0x0043BA20, the empty
// 0x005480A0 / 0x005BAB90, 0x0048DCD0, the base's Suspend, 11 0x0042BB20 (member function entries by table: the table
// at 0x00684060, registered by 0x00434F00, its pointers written by the static initialiser 0x006436B0), 12
// invoke_method_by_name 0x0051ADC0 (not mine).
namespace objects {

class DrivableModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0xE47F1B79;
    static constexpr int kMaxGears = 6;

    // The wheels' grip states (Wheel::state).
    enum WheelState : int {
        kWheelOnGround = 0, // touching, grip not worked out yet this step
        kWheelInAir = 1,
        kWheelStaticSlow = 2,      // static friction below min_static_grip_velocity (scaled down to 0)
        kWheelStatic = 3,          // static friction
        kWheelStaticToDynamic = 4, // between max_static_grip_velocity and min_dynamic_grip_velocity
        kWheelDynamic = 5,         // dynamic friction
        kWheelHandbrakeThrottle = 6,
        kWheelHandbrakeLocked = 7,
    };

    // A wheel (0x1C8 bytes; the array at wheels_, made by configure_from_data; its parameters read by 0x00434F20).
    struct Wheel {
        float y;                   // +0x00 the suspension's extension (the wheel's height in the body)
        int state;                 // +0x04 WheelState
        float spin;                // +0x08 the tyre's angular velocity (radians a second)
        float rotation;            // +0x0C the tyre's roll angle (kept within -pi..pi)
        float steering;            // +0x10 the steering angle (Ackermann)
        float display_steering_;   // +0x14 the same from the smoothed steering (the model's)
        maths::Vec4 pos;           // +0x18 the contact point (world)
        maths::Vec4 vel;           // +0x28 the body's velocity there
        float load;                // +0x38 the suspension's force, averaged over the last ten steps
        float load_history_[10];   // +0x3C
        maths::Vec4 normal;        // +0x64 the ground's normal under it
        float friction;            // +0x74 the tyre's friction coefficient this step (0x00435250)
        maths::Vec4 feeler_start_; // +0x78 the suspension's line (world): top
        maths::Vec4 feeler_end_;   // +0x88 bottom
        float slip;                // +0x98 the tyre's sideways / spinning slip speed
        float torque;              // +0x9C the torques on the tyre this step (summed)
        maths::Vec4 offset;        // +0xA0 the wheel's position in the body (the model's, less the centre of mass)
        int steering_side_;        // +0xB0 steering (0x07560E63): 0 Fixed, 1 left, 2 right
        bool drive;                // +0xB4 drive (0x97E20A70): yes / no
        uint8_t padb5_[3];
        float rest_y_;               // +0xB8 the suspension's rest extension (Finalize)
        float max_drawn_y_;          // +0xBC (0xD87D0183) the model's wheel drawn no lower than this
        float radius;                // +0xC0 radius
        float inv_moment_;           // +0xC4 386.4 / moment
        float spring_rate_;          // +0xC8 spring_rate
        float damping_rate_;         // +0xCC damping_rate
        float min_static_grip_vel_;  // +0xD0 min_static_grip_velocity (mph -> inches a second)
        float max_static_grip_vel_;  // +0xD4 max_static_grip_velocity
        float min_dynamic_grip_vel_; // +0xD8 min_dynamic_grip_velocity
        float static_friction_;      // +0xDC static_friction
        float dynamic_friction_;     // +0xE0 dynamic_friction
        float parking_grip_powered_; // +0xE4 handbrake_throttle_friction
        float parking_grip_locked_;  // +0xE8 handbrake_locked_friction
        float brake_excess_;         // +0xEC the brake torque left over once the tyre stopped
        float brake_torque_;         // +0xF0 brake_torque (* 12)
        float handbrake_torque_;     // +0xF4 handbrake_torque (* 12)
        maths::Vec4 sideways;        // +0xF8 the tyre's sideways direction (world, unit, w = 1)
        maths::Vec4 contact_vel_;    // +0x108 the body's velocity at the wheel, before the suspension (world)
        render::ProbeRay feeler;     // +0x118 the suspension's line against the world

        // 0x00435870: the feeler constructed. Returns this.
        Wheel* make_new();
        // 0x00437CF0 (not a Ghidra function): the destructor (the feeler's smart pointer let go).
        void destruct();
    };

    // A collision box's face (0x88 bytes): a corner and two edges in the body, the same in the world, the edges as
    // unit vectors and their lengths.
    struct Collider {
        maths::Vec4 corner;          // +0x00 (body)
        maths::Vec4 edges[2];        // +0x10
        maths::Vec4 world_corner_;   // +0x30
        maths::Vec4 world_edges_[2]; // +0x40
        maths::Vec4 directions[2];   // +0x60 the world edges, unit
        float lengths[2];            // +0x80
    };

    // A contact point of the colliders with the world this step (0x2C bytes; in the array at 0x006951C8).
    struct Contact {
        maths::Vec4 point;  // +0x00 (world)
        maths::Vec4 normal; // +0x10 the world's, out of it
        float depth;        // +0x20 how far in
        float impulse;      // +0x24 the impulses at it this step (summed)
        bool from_feeler_;  // +0x28 (set on the record after a feeler's - as the original)
        uint8_t pad29[3];
    };
    // The contacts (one array for every vehicle; contact_count_ of them this step).
    static Contact* get_contacts();

    // --- the component ---
    // 0x0043AA30 (registered with the composite object manager): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x00437C00: identity orientation, the collision cache, no input, no debug drawing, no
    // wheels, no contacts, awake; the sounds' name 0x1CA1FF20.
    DrivableModule();
    // 0x0043B990: the destructor body: the wheels destroyed and freed, the collision cache unregistered, the base's.
    ~DrivableModule() override;
    void reapply_from_data(script::Struct* /*params*/) override {} // (slot 5: nothing)
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x00437D10 (vtable slot 2): the vehicle's parameters (see vehicle_component.cpp).
    void configure_from_data(script::Struct* params) override;
    // 0x0043BA20 (vtable slot 3): the components, the wheels' and the skater's positions from the model, the
    // wheelbase and track, the centre of mass, the skater's components; the body's state into the object.
    void seal() override;
    // 0x0043C1D0 (vtable slot 1): the controls (none when passive); asleep, only the steering, the wheels' roll, the
    // model and the skater; else in up to six sub-steps of at most 1/30 s: Integrate, Collide (not when passive), the
    // wheels' contacts, steering, damping and forces, the air and free-fall times; then (not when passive) the body
    // into the object, check_sleep, the model and the skater. Stops as soon as a trigger's script took over or the
    // object died. A gravity override ends when its time is up or two wheels touch.
    void tick_frame() override;
    // 0x0043A070: the model's bones: 1 the skater's place, 2 on the wheels (steered, rolled, at their suspension's
    // extension but no lower than max_drawn_y_); nothing in debug mode 2 (all zero). Skipped while rendering is off.
    void refresh_bone_rig();
    // 0x0043A2C0: a point on a tyre's rim in the world: `spoke` of twelve around (at its roll), on its outer or inner
    // face, steered. Returns out.
    maths::Vec4* get_wheel_rim_point(maths::Vec4* out, const Wheel* wheel, int spoke, bool outer);
    // 0x0043A400: the wheels and the centre of mass drawn (with debug_mode_; compiled out of this build).
    void debug_draw();
    // 0x0043AA90 (not a Ghidra function): moved to a node: its fixed axle at the node, on the ground below it (from
    // 24 above to 240 below) at the wheels' rest height, facing the node's way; settled by sixty passive updates;
    // asleep, still, teleported (and the skater too).
    void glide_to_node(script::Struct* node);

    // --- the body ---
    // 0x00435520: the velocity (the momentum times the inverse mass, w 0) and the angular velocity (the angular
    // momentum through the world's inverse inertia).
    void step_vehicle_speeds();
    // 0x004355B0: the velocity at `offset` from the body's position: the angular velocity across it, plus the
    // velocity (w the velocity's). Returns out.
    maths::Vec4* get_point_velocity(maths::Vec4* out, const maths::Vec4* offset);
    // 0x00435640: `force` added to the force, and its torque about the centre of mass at `point` (relative to the
    // body's position) to the torque. (`colour`: a debug line's, compiled out.)
    void add_force_at_point(const maths::Vec4* force, const maths::Vec4* point, uint32_t colour);
    // 0x00435710: the same with the lever `offset` as it is.
    void add_force_at_offset(const maths::Vec4* force, const maths::Vec4* offset);
    // 0x004357C0: `impulse` added to the momentum, its moment at `offset` to the angular momentum; the velocities
    // updated.
    void apply_impulse_at_offset(const maths::Vec4* impulse, const maths::Vec4* offset);
    // 0x00435850: whether a random kick is still going (kick_time_ > 0).
    bool is_kicked();
    // 0x00435890: a random kick along `along` (flattened, unit): pushed forward (0x3DB3FA83 * (1 +- 0.5)),
    // sideways (0x39E99FEC * +-1.5) and up (0x738500FD * (1 +- 0.2)); spun about the up axis (0x14DDB3EF * +-1.5)
    // and the sideways one (0x4088E6EC * (1 +- 0.8)) - the spin through the first moment of inertia for every axis
    // (as the original). Kicked for 0x771922A6 seconds; awake.
    void kick_randomly_along(const maths::Vec4* along);

    // --- the wheels ---
    // 0x004351C0: each wheel's roll angle on by its spin (less half its torque over the step), wrapped by 100 pi
    // above 200 pi and below 0.
    void update_wheel_rotations();
    // 0x00435250: the tyre's friction coefficient at `slip` (its slip speed), and its grip state: with the handbrake
    // the handbrake's friction (with or without throttle); else static grip below max_static_grip_velocity, blending
    // to dynamic by min_dynamic_grip_velocity, doubled while braking; below min_static_grip_velocity scaled down to 0.
    // Returned unrounded (in ST0).
    double get_tyre_friction(Wheel* wheel, float slip);
    // 0x00435380: the gear for a wheel spinning at `spin`: the first whose engine speed (ratio * differential *
    // spin) isn't over upshift_rpm, else the top one.
    int get_gear(float spin);

    // --- a physics step (vehicle_physics.cpp) ---
    // 0x0043AE10: what follows from the position and orientation: the matrix (row 3 the identity's), the centre of
    // mass's offset, the inverse inertia, the velocities, the wheels' suspension lines, the colliders in the world,
    // and the collision cache.
    void update_dependents();
    // 0x00438A20: the inverse inertia tensor in the world: R * diag(inverse moments) * R^T (D3DXMatrixMultiply).
    void update_inertia();
    // 0x00435DA0: the collision cache refilled for the box around the wheels' lines and the colliders' corners.
    void refresh_hit_cache();
    // 0x0043B240: each wheel's suspension line against the world (faces flagged 0x10 ignored): touching or in the
    // air, the trigger component told when a wheel lands, leaves or moves onto another collision object (and,
    // outside the park editor, of trigger faces along the line, flagged 0x50); then the wheels' contact points and
    // velocities there. Stops at once if a trigger's script took over or the object died.
    void update_wheel_contacts();
    // 0x00435F00: the smoothed steering eased towards the input (10 a second); the steered wheels' angles for both
    // (Ackermann: the inner wheel - steering 1 - turned further than the outer one).
    void update_steering();
    // 0x00438AE0: gravity, the springs, the tyres' grip, then drive, engine braking, brakes and the tyres' forces;
    // with the handbrake and throttle the handbrake (by 1 - |steering|) instead of the others; with the handbrake
    // alone the wheels locked.
    void apply_wheel_forces();
    // 0x00436280: gravity on the force (scaled by Vehicle_AdjustGravity; weaker after 300 ms of righting).
    void apply_gravity();
    // 0x00436340: each touching wheel's spring and damper along the ground's normal, at its contact point; the
    // load averaged over ten steps (in the air: an even share of the weight).
    void apply_suspension();
    // 0x00435B60: each touching wheel's sideways direction (steered, along the ground), its ground velocity, and its
    // tyre friction coefficient for its slip.
    void update_wheel_friction();
    // 0x004364B0: each touching wheel's tyre friction against its slip: the smaller of the torque that would stop the
    // slip and the friction's own acts on the wheel; the force (and what's left of the stopping one) pushes the body.
    void apply_tyre_forces();
    // 0x00436900: the engine's torque on the driven wheels (by gear, differential and the wheels' share; less on
    // slopes; reversed by reverse_torque_ratio; doubled on four-wheel drive with the handbrake).
    void apply_drive_torque();
    // 0x004367E0: without throttle, reverse or brake, engine braking on the driven touching wheels (at most what
    // stops them this step).
    void apply_engine_braking();
    // 0x00436A90: braking each wheel (at most what stops it; the rest kept as its brake excess).
    void apply_brakes();
    // 0x00436B60: the handbrake on each wheel, its torque times `scale`, as the brakes.
    void apply_handbrake(float scale);
    // 0x00436C20: while braking, each wheel's brake excess applied again (at most what stops it).
    void apply_brake_excess();
    // 0x004360C0: the spin damped (by its square, unless driving, steering or kicked), and on three or more wheels
    // upright and not steering, the turning about up damped towards 0.
    void apply_damping();
    // 0x00437550: three steps hardly moving on all wheels (or none) and not driving: the momenta, forces and the
    // wheels' torques cleared, asleep.
    void check_sleep();
    // 0x004376B0: the controls from the input component: steering (the stick squared, or the d-pad), throttle,
    // handbrake (unless no_handbrake), brake or reverse (by the forward speed), Exitable's other brake button; the
    // handbrake flag forces the brake. Driving wakes it.
    void read_controls();
    // 0x004378B0: the skater carried: at skater_pos on the vehicle, its matrix and velocity; with a visible skater,
    // its steering animation's frame from the smoothed steering (in a network game sent on every fifteenth time).
    void update_skater();

    // --- collision and integration (vehicle_collision.cpp) ---
    // 0x0043BEE0: a sub-step: the momenta from the forces, the move (through move_by) and the turn (the orientation
    // by its derivative) from the velocities plus half the accelerations; the wheels' roll; turning in the air
    // (no wheel touching and no impulse); righting; the dependents.
    void integrate();
    // 0x00437120: the position moved by `move` - not through the world: a hit puts it at the surface with the rest of
    // the move reflected and the momentum into it bounced (1.5); then, with nothing at all within 4800 below, put
    // back where it was ("uber frig"), else the model lit from the ground.
    void move_by(const maths::Vec4* move);
    // 0x00438770: righting itself: stuck upside down or on its side (slow, after a collision, the throttle held) it
    // stops and, from 300 ms to 5.3 s on, turns upright (a slerp, 3 a second). Upright (up.y > 0.999): not.
    void update_righting();
    // 0x00439D90: in the air (not righting or kicked), turned level towards its velocity (fast) or facing (slow, with
    // align_when_slow_), ramped in over align_time_ of free fall.
    void align_in_air();
    // 0x00438BB0: the colliders against the world (rect queries and lines from the centre to their corners): the
    // contacts, impulses at those closing in (restitution; up to six passes), pushes out by their depth, friction
    // against sliding, the strongest impulse, and steering against walls. Stops as soon as a trigger's script took
    // over or the object died.
    void collide();
    // 0x00436CF0 (stdcall): how deep a contact on a collider's face is: its distance in from the nearest of the
    // face's edges, along the normal (0 under 0.01).
    static double __stdcall get_penetration(const Contact* contact, const Collider* collider);
    // 0x00436E50: a contact on a triangle edge whose normal faces away: if another joined hit of the rect has an end
    // there with the opposite normal, the contact's normal across the edge and its depth. Whether there was.
    bool find_edge_contact(Contact* contact, render::BoxProbe* rect, const Collider* collider, int hit, int end);
    // 0x00437060: whether a contact's normal points away from the centre of mass (not a contact).
    bool normal_faces_away(const Contact* contact);
    // 0x00437450: 1 / (n . (n / m + (I^-1 (r x n)) x r)) - the impulse for a unit velocity change along `direction`
    // at `offset`. Unrounded.
    double get_impulse_denominator(const maths::Vec4* offset, const maths::Vec4* direction);

    // --- script member functions (the table at 0x00684060) ---
    // 0x00437A60 Vehicle_Kick (0x93B713A6): `linear` and `angular` (vectors, rotated into the world) added to the
    // momenta. Awake. True.
    bool get_kick(script::Struct* params, script::Script* script);
    // 0x004353F0 Vehicle_Wake: awake. True.
    bool wake(script::Struct* params, script::Script* script);
    // 0x0043B670 Vehicle_MoveToRestart
    bool teleport_to_spawn(script::Struct* params, script::Script* script);
    // 0x0043B720 Vehicle_PlaceBeforeCamera: moved in front of the camera, facing away from it. True.
    bool place_before_camera(script::Struct* params, script::Script* script);
    // 0x00435410 Vehicle_AdjustGravity: gravity scaled by percent (0x9E497FC6), for duration (0x79A07F3F) seconds.
    // True.
    bool adjust_gravity(script::Struct* params, script::Script* script);
    // 0x00435480 Vehicle_HandbrakeActive: handbrake_active_.
    bool handbrake_active(script::Struct* params, script::Script* script);
    // 0x00435490 Vehicle_AllWheelsAreInContact
    bool all_wheels_are_in_contact(script::Struct* params, script::Script* script);
    // 0x00437BC0 (0x4C73526B): an offset (0xA6F5352F) kicked about (0x00435890). True.
    bool kick_randomly(script::Struct* params, script::Script* script);
    // 0x004354B0 Vehicle_IsSkaterVisible: rider_shown_.
    bool is_rider_shown(script::Struct* params, script::Script* script);

    // --- the fields ---
    maths::Vec4 at_point_;        // +0x18 the centre of mass (world)
    maths::Vec4 linear_momentum_; // +0x28 linear momentum
    maths::Vec4 orientation_;     // +0x38 a quaternion (x, y, z, w)
    maths::Vec4 rotmom_;          // +0x48 angular momentum
    int awake_;                   // +0x58 0: asleep (only the model kept up to date)
    int still_frames_;            // +0x5C frames it has hardly moved (asleep after three)
    int contact_count_;           // +0x60 the colliders' contact points this step (in s_contacts)
    maths::Mat4 basis_frame_;     // +0x64 the orientation as a matrix
    maths::Vec4 vel_;             // +0xA4 linear_momentum_ * inv_mass_
    maths::Vec4 rotvel_;          // +0xB4 the angular velocity
    maths::Mat4 inv_inertia_;     // +0xC4 the inverse inertia tensor in the world
    maths::Vec4 com_offset_;      // +0x104 balance_point_ turned into the world
    int wheels_in_contact_;       // +0x114
    int8_t load_index_;           // +0x118 the wheels' load history slot (0-9)
    bool cut_short_;              // +0x119 a trigger's script took over during the step
    bool righting_;               // +0x11A turning itself upright
    uint8_t pad11b_;
    int righting_start_;        // +0x11C the game clock (with its offset) when it started righting itself
    float kick_time_;           // +0x120 seconds left of a random kick (0x00435890)
    maths::Vec4 force_;         // +0x124 the force this step
    maths::Vec4 twist_force_;   // +0x134 the torque this step
    float step_length_;         // +0x144 the sub-step's length (seconds)
    maths::Vec4 balance_point_; // +0x148 (body; y from 0x93346B8C)
    float inv_mass_;            // +0x158 386.4 / mass
    float inv_moments_[3];      // +0x15C 386.4 / moment_of_inertia
    uint32_t unknown168_;
    float restitution_;      // +0x16C body_restitution (0x79C3B862)
    float drag_factor_;      // +0x170 body_friction (0xF476273C)
    float depth_correction_; // +0x174 (0x01DD4890) the contacts' depth correction
    float tipped_friction_;  // +0x178 (0x4A23A53C) the body's friction when tipped over (up.y <= 0.707)
    float wall_steering_;    // +0x17C (0xB9F026ED) the steering's turning against walls
    float align_speed_;      // +0x180 (0x8C14709E) below this horizontal speed the body turns towards its
                             //        facing, above it towards its velocity (0x00439D90)
    float align_time_;       // +0x184 (0x2C91E49B) the turning's ramp-in time in the air
    float align_rate_;       // +0x188 (0x280F0E0B) the turning's rate
    bool align_when_slow_;   // +0x18C (0x5341806A) turn towards the facing when slow
    uint8_t pad18d_[3];
    int wheel_count_;          // +0x190 Wheels
    int drive_wheel_count_;    // +0x194 wheels with drive
    float max_steering_angle_; // +0x198 max_steering_angle (radians)
    float wheelbase_;          // +0x19C (Finalize: the model's front to back wheels)
    float track_;              // +0x1A0 (Finalize: the model's left to right wheels)
    float yaw_damping_;        // +0x1A4 (0x386837DB) the turning damped when not steering
    float spin_damping_;       // +0x1A8 (0x469CF79A) the angular momentum damped
    Collider colliders_[2];    // +0x1AC colliders (0xFC8C4AC6)
    maths::Vec4 skater_bone_;  // +0x2BC the skater's place on the model (Finalize; the skeleton's bone 1)
    bool can_dismount_;        // +0x2CC Exitable
    bool handbrake_disabled_;  // +0x2CD no_handbrake
    uint8_t pad2ce_[2];
    Wheel* wheels_;                        // +0x2D0
    float engine_torque_;                  // +0x2D4 drive_torque (engine; * 12)
    float engine_braking_;                 // +0x2D8 (0xB2268648, * 12) engine braking without throttle
    int gear_count_;                       // +0x2DC gear_ratios
    float transmission_ratios_[kMaxGears]; // +0x2E0
    float reverse_ratio_;                  // +0x2F8 reverse_torque_ratio
    float final_drive_ratio_;              // +0x2FC differential_ratio
    float upshift_speed_;                  // +0x300 upshift_rpm (rpm -> radians a second)
    float steering_;                       // +0x304 the input: -1 (left) .. 1 (right), squared with its sign
    bool throttle_;                        // +0x308
    bool brake_;                           // +0x309
    bool handbrake_;                       // +0x30A
    bool reverse_;                         // +0x30B
    render::HitMeshCache contact_memo_;    // +0x30C the feelers' collision cache
    bool rider_shown_;                     // +0x12D8 (0x2ED67657) a skater is shown driving
    uint8_t pad12d9_[3];
    maths::Vec4 skater_pos_;   // +0x12DC skater_pos (made relative to the centre of mass by Finalize)
    uint32_t rider_anim_name_; // +0x12EC skater_anim (0xDA75A33E)
    float display_steering_;   // +0x12F0 steering_ smoothed (the model's wheels, the skater's animation)
    uint32_t sounds_name_;     // +0x12F4 sounds (0x0EDCF90E): the VehicleSound component's settings
    float gravity_scale_;      // +0x12F8 (Vehicle_AdjustGravity)
    float gravity_time_;       // +0x12FC seconds left of it
    bool handbrake_active_;    // +0x1300 (forces the brake)
    uint8_t pad1301[3];
    float air_time_;              // +0x1304 seconds without a wheel on the ground
    float free_fall_time_;        // +0x1308 seconds in the air without touching anything
    float collision_impulse_;     // +0x130C the strongest contact impulse this step
    ObjectModule* bone_rig_;      // +0x1310 the object's skeleton (0x222756D5)
    uint8_t* input_ptr_;          // +0x1314 the input component (0x27D7CD28)
    ObjectModule* mesh_instance_; // +0x1318 the model (0x286A8D26)
    int debug_mode_;              // +0x131C 0-2 (debug: the next one, 0xE6B5FCD9: the previous, 0x751DA48B: off)
    bool passive_;                // +0x1320 no input, collision or debug drawing (set from outside)
    uint8_t pad1321[3];
    GameEntity* rider_ref_;             // +0x1324 the local skater (modules::Skate's 0x00566520)
    ObjectModule* anim_blender_ptr_;    // +0x1328 its animation blender (with rider_shown_)
    ObjectModule* skater_core_physics_; // +0x132C its core physics (0x5BD63B29)
    ObjectModule* trigger_ptr_;         // +0x1330 the object's trigger component (0xE594F0A2)
};
// 0x004354C0 (cdecl): the quaternion product a * b into `out` (0x00420140, a quaternion's *=, on a copy of a).
// Returns out.
maths::Vec4* quat_product(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b);

static_assert(sizeof(DrivableModule::Wheel) == 0x1C8 && offsetof(DrivableModule::Wheel, load) == 0x38 &&
                  offsetof(DrivableModule::Wheel, normal) == 0x64 &&
                  offsetof(DrivableModule::Wheel, feeler_start_) == 0x78 &&
                  offsetof(DrivableModule::Wheel, offset) == 0xA0 && offsetof(DrivableModule::Wheel, rest_y_) == 0xB8 &&
                  offsetof(DrivableModule::Wheel, brake_torque_) == 0xF0 &&
                  offsetof(DrivableModule::Wheel, sideways) == 0xF8 && offsetof(DrivableModule::Wheel, feeler) == 0x118,
              "layout must match the original");
static_assert(sizeof(DrivableModule::Collider) == 0x88, "layout must match the original");
static_assert(offsetof(DrivableModule, at_point_) == 0x18 && offsetof(DrivableModule, awake_) == 0x58 &&
                  offsetof(DrivableModule, basis_frame_) == 0x64 && offsetof(DrivableModule, vel_) == 0xA4 &&
                  offsetof(DrivableModule, inv_inertia_) == 0xC4 &&
                  offsetof(DrivableModule, wheels_in_contact_) == 0x114 &&
                  offsetof(DrivableModule, kick_time_) == 0x120 && offsetof(DrivableModule, step_length_) == 0x144 &&
                  offsetof(DrivableModule, inv_mass_) == 0x158 && offsetof(DrivableModule, restitution_) == 0x16C &&
                  offsetof(DrivableModule, wheel_count_) == 0x190 && offsetof(DrivableModule, colliders_) == 0x1AC &&
                  offsetof(DrivableModule, skater_bone_) == 0x2BC && offsetof(DrivableModule, wheels_) == 0x2D0 &&
                  offsetof(DrivableModule, transmission_ratios_) == 0x2E0 &&
                  offsetof(DrivableModule, steering_) == 0x304 && offsetof(DrivableModule, contact_memo_) == 0x30C &&
                  offsetof(DrivableModule, rider_shown_) == 0x12D8 &&
                  offsetof(DrivableModule, sounds_name_) == 0x12F4 &&
                  offsetof(DrivableModule, collision_impulse_) == 0x130C &&
                  offsetof(DrivableModule, input_ptr_) == 0x1314 && offsetof(DrivableModule, trigger_ptr_) == 0x1330 &&
                  sizeof(DrivableModule) == 0x1334,
              "layout must match the original");

} // namespace objects
