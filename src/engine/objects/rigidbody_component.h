#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/render/coll_cache.h"

namespace script {
class Array;
class Script;
class Struct;
} // namespace script

namespace render {
class ProbeRay;
}

namespace objects {

class AudioEmitterModule;

// The rigid body component (type 0xFFB39248, "rigidbody"; vtable 0x006463F8, 0x1A0 bytes; made by 0x0042EF00): an
// object that falls, bounces, slides and tumbles - its shape a set of contact points (a box, pyramid, cylinder,
// triangle, or the contacts= given) and spheres, held by springs, pushed by the skater. Asleep (rigidbody_state=
// asleep, the default), awake or disabled. Its script member functions are in the table at 0x00683F28 (filled by
// 0x00643530; registered by 0x0042BB00).
//
// Vtable: 0 the scalar deleting destructor; 1 Update 0x00430E20; 2 configure_from_data; 3 Finalize 0x0042BD90; 4 / 9
// the empty 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11
// 0x0042BB20 (shared); 12 invoke_method_by_name 0x0051ADC0 (none by name).
class BodyDynamicsModule : public ObjectModule {
public:
    static constexpr uint32_t kType = 0xFFB39248;

    // A spring (0x24 bytes): its point on the body (w 0), where it's held (+0x10, world), and whether it's on.
    struct Spring {
        maths::Vec4 point;
        maths::Vec4 anchor;
        bool active;
        uint8_t pad21[3];
    };
    // A sphere of its shape (0x14 bytes): its centre and radius.
    struct Sphere {
        maths::Vec4 center;
        float radius;
    };
    // A contact's directed friction (0x14 bytes): the direction (body space) and whether there is any.
    struct DirectedFriction {
        maths::Vec4 direction;
        bool active;
        uint8_t pad11[3];
    };

    enum State { kStateAsleep = 0, kStateAwake = 1, kStateDisabled = 2 };
    // flag_bits_ (+0xF4)
    static constexpr uint32_t kFlagDieUponSettling = 0x1;
    static constexpr uint32_t kFlagNoPlayerCollision = 0x2;

    // 0x0042EF00 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x0042E3F0: the base's constructor, its vtable and type, the capsule (+0x64) constructed, the defaults (see the
    // fields). Returns this.
    BodyDynamicsModule* make_new();
    // 0x0042EF60: the shape's, the friction's, the spheres' and the springs' arrays freed, the callback and sound
    // structures deleted; the base's body.
    void destroy_body();
    // 0x00430DF0 (vtable slot 0)
    BodyDynamicsModule* teardown(unsigned int flags);
    // 0x0042F050 (vtable slot 2): everything from the structure - the state, velocities, the physical constants, the
    // scripts and sounds, springs=, the shape (contacts= / spheres - 0x5B6A8D18 - / Box (dimensions or top_ /
    // bottom_dimensions), Pyramid (height, depth), Cylinder (radius, top_ / bottom_radius, height, edges), Triangle
    // (Dimensions); Box without any), directed_friction=, Angles= (the shape turned), the centre of mass (given, or
    // the average of the points) the shape is moved to, model_offset=, mass_over_moment (else scaled down for big
    // shapes), disable_player_collision; the bounding radius; the position moved by the offset.
    void configure_from_data(script::Struct* params);

    // The shapes (each replaces the contact points; half sizes; w 1):
    // 0x0042D4B0: the vectors of `points` (w 0).
    void set_contacts(script::Array* points);
    // 0x0042D560: a box of 8 corners, the top four from `top` (y up), the bottom four from `bottom` (y down).
    void set_box(const maths::Vec4* top, const maths::Vec4* bottom);
    // 0x0042D690: a pyramid of 5: a square base (half side `half_depth`) at -0.6 `half_height`, its tip at 1.6.
    void set_pyramid(float half_height, float half_depth);
    // 0x0042D790: a cylinder of two rings of `edges` points each: the bottom (y -`half_height`, `bottom_radius`) then
    // the top (`top_radius`), from the last point round.
    void set_cylinder(float top_radius, float bottom_radius, float half_height, int edges);
    // 0x0042D8A0: a triangular prism of 6 from `dimensions`.
    void set_triangle(const maths::Vec4* dimensions);
    // 0x0042ED60: the spheres from the structures of `spheres` (the unnamed vector, radius=; both asserted).
    void set_spheres(script::Array* spheres);
    // 0x0042EB10: the sounds of `type` (RigidBodySounds' structure): CollideSound / BounceSound (copied), and with a
    // BounceSound its CollideMuteDelay / GlobalCollideMuteDelay / BounceVelocityCallbackThreshold /
    // BounceVelocityFullSpeed.
    void set_sounds(uint32_t type);

    // --- script member functions (the table at 0x00683F28) ---
    bool rigid_body_wake(script::Struct* params, script::Script* script);  // 0x0042DCA0
    bool rigid_body_sleep(script::Struct* params, script::Script* script); // 0x0042DCF0 (0x0042BE90)
    // 0x0042DD00 Rigidbody_Kick: vel=, rotvel= and pos= added to its own (the object's velocity and position too);
    // awake.
    bool rigidbody_kick(script::Struct* params, script::Script* script);
    // 0x0042DEB0 rigid_body_set_velocities: vel= / rotvel= set; awake.
    bool rigid_body_set_velocities(script::Struct* params, script::Script* script);
    // 0x0042EE30 rigid_body_enable: awake where the object is now: its position, orientation and velocity taken.
    bool rigid_body_enable(script::Struct* params, script::Script* script);
    bool rigid_body_disable_player_collision(script::Struct* params, script::Script* script); // 0x0042DFB0
    bool rigid_body_enable_player_collision(script::Struct* params, script::Script* script);  // 0x0042DFC0
    // 0x0042DFD0 [0xD4A0946B]: CenterOfMassPos (its position) added to the script's parameters.
    bool get_center_of_mass_pos(script::Struct* params, script::Script* script);
    // 0x0042E020 Rigidbody_MatchVelocityTo: the velocity (and, if it's a rigid body, the rotational velocity) of the
    // object whose id is the first parameter - with ApplyRandomAdjustment, scaled by 1 +- 0.4 (rotation: 0 +- 0.4);
    // asleep, a vertical speed under 1 becomes 1; awake.
    bool rigidbody_match_velocity_to(script::Struct* params, script::Script* script);

    // Awake from now (where the object is) unless it already is (the inlined end of the member functions).
    void wake();

    // --- the rest of its script member functions (rigidbody_support.cpp) ---
    // 0x0042BB00 (cdecl): its member function table (0x00683F28) registered.
    static void __cdecl publish_method_table();
    // 0x0042BB60 RigidBody_IgnoreSkater: the pushers aren't felt for the unnamed float's seconds (asserted; with
    // frames / frame, that many sixtieths).
    bool rigid_body_ignore_skater(script::Struct* params, script::Script* script);
    bool rigid_body_disable(script::Struct* params, script::Script* script); // 0x0042BBB0: disabled
    bool rigid_body_reset(script::Struct* params, script::Script* script);   // 0x0042BBD0: configure_from_data again
    // 0x0042BD90 (vtable slot 3): the sound component found; each spring's anchor is where its point is now.
    void seal();
    // 0x0042BE90: asleep (SettleScript run first, CallbackParams its parameters): no velocity, no turning.
    void sleep();

    // --- the physics (rigidbody_physics.cpp) ---
    // A contact with the world this frame (0x30 bytes; found by 0x0042C950 into the array at 0x006949B0, its count
    // contacts_hit_): where (from the centre of mass, world axes), the surface's normal, the impulse a unit change of
    // speed along the normal takes (worked out once), the impulse given so far, how deep it is, and its directed
    // friction.
    struct Contact {
        maths::Vec4 offset;
        maths::Vec4 normal;
        float impulse_scale_; // +0x20
        float impulse;        // +0x24
        float depth;          // +0x28
        bool active;          // +0x2C
        bool scaled;          // +0x2D impulse_scale_ worked out
        int8_t friction;      // +0x2E its directed friction (an index into steer_friction_)
        uint8_t pad2f_;
    };
    // The shape turned to the world this frame (from the centre of mass): the contact points (0x00694E30), the
    // spheres' centres (0x00694FB0), the springs' points (0x00695130); and the contacts (0x006949B0).
    static maths::Vec4* world_contact_points();
    static maths::Vec4* world_sphere_centers();
    static maths::Vec4* world_spring_points();
    static Contact* get_contacts();

    // 0x00430E20 (vtable slot 1): disabled: nothing. The push cooldown counted down, else the pushers felt
    // (0x0042C140); the die timer (dead when it runs out). Asleep: only the display matrix set. Awake: the world
    // points (0x0042BF00), then up to 5 steps of the frame, each a line from the centre of mass along the path to
    // the frame's end - a hit cuts the step short (6 back from the hit) and is collided with; the fifth puts it to
    // sleep (0x0042BE90). Then the contacts (0x0042C950 finds them) resolved, the springs, the damping (0x0042C6D0)
    // and sleeping (0x0042C890); the object's position (by the turned model offset), matrices and velocity set.
    void tick_frame();
    // 0x0042E540: `dt` seconds: the position moved (gravity's half step in it, unless unknown191_ >= 3), the
    // velocity accelerated (the same), the orientation turned by the rotational velocity and normalised, its matrix,
    // and the world points.
    void integrate(float dt);
    // 0x00430BE0: a world hit (the feeler's point and normal): the velocity along the normal bounced back by the
    // restitution, the velocity across it slowed by friction (at most what stops it), then on_bounce.
    void collide(render::ProbeRay* feeler);
    // 0x00430140: the contacts resolved: impulses until none is approaching (at most 20 rounds - then the stuck
    // script, or no velocity at all), friction (along the directed friction where there is one), each pushed out
    // by its depth (a spring), then on_bounce at the last contact with the fastest approach.
    void resolve_contacts();
    // 0x0042CF90: the springs: their points' velocities taken out by impulses (up to 20 rounds), then each pulled to
    // its anchor.
    void apply_springs();
    // 0x0042CEE0: the impulse that changes the speed along `normal` at `offset` by 1 (thiscall, the result left
    // unrounded in ST0).
    double impulse_scale(const maths::Vec4* normal, const maths::Vec4* offset) const;
    // 0x0042E980: a bounce at `speed` (faster than the threshold): BounceScript run (CallbackParams and the point,
    // 0x43F454D0, in its parameters), the bounce sound played at Percent of the full speed while 12 voices are free.
    void on_bounce(float speed, const maths::Vec4* point);
    // 0x0042D9D0: the shape, spheres and springs drawn (compiled out of this build: nothing is drawn).
    void debug_draw();

    // (rigidbody_support.cpp)
    // 0x0042BF00: the contact points, the spheres' centres and the active springs' points turned to the world
    // (world_contact_points / world_sphere_centers / world_spring_points; w as the body's).
    void update_world_points();
    // 0x0042C140: the first pusher (add_rigid_body_pusher's) touching it and moving at it pushes it.
    void feel_pushers();
    // 0x0042C950 (rigidbody_contacts.cpp): the contacts with the world this frame (Contacts(), contacts_hit_ of
    // them, unknown191_ touching): the contact points' lines and the spheres' queries. Whether any is touching.
    bool find_contacts();
    // 0x0042C6D0: the velocity and the rotational velocity slowed by unknown38_ / unknown3c_ (each a speed taken
    // off; slower than that: stopped - w 1).
    void damp();
    // 0x0042C890: settled 3 frames or more and slower than both sleep points: asleep - and, dying upon settling, the
    // die timer started (1.5 s) once 48 or more from where it woke.
    void check_sleep();

    float mass_over_moment_;           // +0x18 mass_over_moment (0.003)
    maths::Vec4 steady_accel_;         // +0x1C const_acc (gravity: (0, -386.4, 0, 1))
    float restitution_;                // +0x2C coeff_restitution (0.4)
    float drag_factor_;                // +0x30 coeff_friction (0.5)
    float spring_const_;               // +0x34 spring_const (10)
    float unknown38_;                  // +0x38 (0xDE76C3A7): the linear damping (0x0042C6D0)
    float unknown3c_;                  // +0x3C (0x9DA897CF): the angular damping (0x0042C6D0)
    float skater_radius_;              // +0x40 skater_collision_radius (48)
    float skater_application_radius_;  // +0x44 skater_collision_application_radius (12)
    float skater_assent_cos_;          // +0x48 skater_collision_assent (25 degrees)
    float skater_assent_sin_;          // +0x4C
    float skater_impulse_factor_;      // +0x50 skater_collision_impulse_factor (1)
    float skater_rotation_factor_;     // +0x54 skater_collision_rotation_factor (1)
    float linear_sleep_squared_;       // +0x58 linear_velocity_sleep_point squared (100)
    float angular_sleep_squared_;      // +0x5C angular_velocity_sleep_point squared (0.16)
    maths::Vec4* contacts_;            // +0x60 the shape's points (body space)
    maths::Capsule capsule_;           // +0x64
    DirectedFriction* steer_friction_; // +0x88 one per contact (directed_friction=)
    uint16_t contact_count_;           // +0x8C
    uint16_t pad8e_;
    Sphere* spheres_;       // +0x90
    uint16_t sphere_count_; // +0x94
    uint16_t pad96;
    Spring* springs_;       // +0x98 (springs=)
    uint16_t spring_count_; // +0x9C
    uint16_t pad9e_;
    maths::Vec4 balance_point_;           // +0xA0 center_of_mass
    maths::Vec4 model_offset_;            // +0xB0 the model's offset from the centre of mass
    float push_cooldown_time_;            // +0xC0 (0x1F4D0CA5; 1/12): seconds the pushers aren't felt after a push
    uint32_t collide_script_;             // +0xC4 CollideScript
    uint32_t bounce_script_;              // +0xC8 BounceScript
    uint32_t settle_script_;              // +0xCC SettleScript
    uint32_t script_d0_;                  // +0xD0 (0xCCB84047): run when the contacts can't be resolved
    script::Struct* callback_params_ptr_; // +0xD4 CallbackParams (a copy)
    script::Struct* collide_sound_ptr_;   // +0xD8 the sound type's CollideSound (a copy)
    script::Struct* bounce_sfx_;          // +0xDC ... BounceSound
    int impact_quiet_ms_;                 // +0xE0 (1000)
    int global_collide_mute_delay_;       // +0xE4 (100)
    float bounce_callback_threshold_;     // +0xE8 BounceVelocityCallbackThreshold (20)
    float bounce_full_speed_;             // +0xEC BounceVelocityFullSpeed (300)
    uint32_t unknownf0_;                  // +0xF0
    uint32_t flag_bits_;                  // +0xF4 FLAG_*
    maths::Vec4 wake_pos_;                // +0xF8 where it last woke
    float die_timer_;                     // +0x108 (-1: none)
    int cur_phase_;                       // +0x10C State (rigidbody_state=)
    maths::Vec4 at_point_;                // +0x110 its centre of mass
    maths::Vec4 vel_;                     // +0x120
    maths::QuatRotation orientation_;     // +0x130
    maths::Vec4 rot_vel_;                 // +0x140
    maths::Mat4 basis_frame_;             // +0x150 the orientation as a matrix
    int8_t contacts_hit_;                 // +0x190 contacts touching this frame
    int8_t unknown191_;                   // +0x191 (3 or more: Integrate leaves gravity out)
    uint8_t pad192[2];
    float push_cooldown_;              // +0x194 the time left of push_cooldown_time_
    float radius_;                     // +0x198 the bounding radius
    AudioEmitterModule* audio_module_; // +0x19C the object's sound component (for the bounce sound)
};
static_assert(
    offsetof(BodyDynamicsModule, steady_accel_) == 0x1C && offsetof(BodyDynamicsModule, contacts_) == 0x60 &&
        offsetof(BodyDynamicsModule, capsule_) == 0x64 && offsetof(BodyDynamicsModule, steer_friction_) == 0x88 &&
        offsetof(BodyDynamicsModule, contact_count_) == 0x8C && offsetof(BodyDynamicsModule, balance_point_) == 0xA0 &&
        offsetof(BodyDynamicsModule, callback_params_ptr_) == 0xD4 &&
        offsetof(BodyDynamicsModule, flag_bits_) == 0xF4 && offsetof(BodyDynamicsModule, cur_phase_) == 0x10C &&
        offsetof(BodyDynamicsModule, at_point_) == 0x110 && offsetof(BodyDynamicsModule, basis_frame_) == 0x150 &&
        offsetof(BodyDynamicsModule, contacts_hit_) == 0x190 && offsetof(BodyDynamicsModule, radius_) == 0x198 &&
        sizeof(BodyDynamicsModule) == 0x1A0,
    "layout must match the original");
static_assert(sizeof(BodyDynamicsModule::Spring) == 0x24 && sizeof(BodyDynamicsModule::Sphere) == 0x14 &&
                  sizeof(BodyDynamicsModule::DirectedFriction) == 0x14 && sizeof(BodyDynamicsModule::Contact) == 0x30 &&
                  offsetof(BodyDynamicsModule::Contact, impulse_scale_) == 0x20 &&
                  offsetof(BodyDynamicsModule::Contact, active) == 0x2C,
              "layout must match the original");

// 0x0042E380: `object` (a skater) added to the objects rigid bodies feel (8 places, 0x00695150: the last free one
// from the end; none free: nothing), its extra reach (0x006939B0) back to 0.
void __cdecl add_rigid_body_pusher(GameEntity* object);
// 0x0042BD60 (cdecl; rider.cpp's 0x005802C0): `object`'s extra reach as a pusher (0x006939B0, one float for each
// of the 8) - nothing if it isn't one.
void __cdecl set_rigid_body_pusher_reach(GameEntity* object, float reach);
// 0x0042BAC0 ToggleRigidBodyDebug: the rigid bodies' debug drawing stepped: off, the shapes, the details too, off.
bool __cdecl cfunc_toggle_rigid_body_debug(script::Struct* params, script::Script* script);

extern maths::Vec4 g_rigid_body_world_contact_points[24];     // 0x00694E30
extern maths::Vec4 g_rigid_body_world_sphere_centers[24];     // 0x00694FB0
extern maths::Vec4 g_rigid_body_world_spring_points[2];       // 0x00695130
extern BodyDynamicsModule::Contact g_rigid_body_contacts[24]; // 0x006949B0
extern SharedRef<GameEntity> g_rigid_body_pushers[8];         // 0x00695150
// (the cache is raw zeroed storage: the cache fills it before it looks)
struct RigidBodyCacheStorage {
    alignas(8) uint8_t bytes[sizeof(render::HitMeshCache)];
};
extern RigidBodyCacheStorage g_rigid_body_cache; // 0x006939E0
extern float g_rigid_body_pusher_reach[8];
extern float g_rigid_body_pusher_height;
extern bool g_rigid_body_draw_shape;
extern bool g_rigid_body_draw_details;

} // namespace objects
