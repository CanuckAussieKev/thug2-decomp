#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "hook/game_global.h"

namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

namespace Nx {
class CFeeler;
}

namespace Obj {

class CSoundComponent;

// The rigid body component (type 0xFFB39248, "rigidbody"; vtable 0x006463F8, 0x1A0 bytes; made by 0x0042EF00): an
// object that falls, bounces, slides and tumbles - its shape a set of contact points (a box, pyramid, cylinder,
// triangle, or the contacts= given) and spheres, held by springs, pushed by the skater. Asleep (rigidbody_state=
// asleep, the default), awake or disabled. Its script member functions are in the table at 0x00683F28 (filled by
// 0x00643530; registered by 0x0042BB00).
//
// Vtable: 0 the scalar deleting destructor; 1 Update 0x00430E20; 2 InitFromStructure; 3 Finalize 0x0042BD90; 4 / 9
// the empty 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11
// 0x0042BB20 (shared); 12 CallMemberFunction 0x0051ADC0 (none by name).
class CRigidBodyComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xFFB39248;

    // A spring (0x24 bytes): its point on the body (w 0), where it's held (+0x10, world), and whether it's on.
    struct Spring {
        Mth::Vector point;
        Mth::Vector anchor;
        bool active;
        uint8_t pad21[3];
    };
    // A sphere of its shape (0x14 bytes): its centre and radius.
    struct Sphere {
        Mth::Vector center;
        float radius;
    };
    // A contact's directed friction (0x14 bytes): the direction (body space) and whether there is any.
    struct DirectedFriction {
        Mth::Vector direction;
        bool active;
        uint8_t pad11[3];
    };

    enum State { STATE_ASLEEP = 0, STATE_AWAKE = 1, STATE_DISABLED = 2 };
    // m_flags (+0xF4)
    static constexpr uint32_t FLAG_DIE_UPON_SETTLING = 0x1;
    static constexpr uint32_t FLAG_NO_PLAYER_COLLISION = 0x2;

    // 0x0042EF00 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0042E3F0: the base's constructor, its vtable and type, the capsule (+0x64) constructed, the defaults (see the
    // fields). Returns this.
    CRigidBodyComponent* Construct();
    // 0x0042EF60: the shape's, the friction's, the spheres' and the springs' arrays freed, the callback and sound
    // structures deleted; the base's body.
    void DestroyBody();
    // 0x00430DF0 (vtable slot 0)
    CRigidBodyComponent* Destroy(unsigned int flags);
    // 0x0042F050 (vtable slot 2): everything from the structure - the state, velocities, the physical constants, the
    // scripts and sounds, springs=, the shape (contacts= / spheres - 0x5B6A8D18 - / Box (dimensions or top_ /
    // bottom_dimensions), Pyramid (height, depth), Cylinder (radius, top_ / bottom_radius, height, edges), Triangle
    // (Dimensions); Box without any), directed_friction=, Angles= (the shape turned), the centre of mass (given, or
    // the average of the points) the shape is moved to, model_offset=, mass_over_moment (else scaled down for big
    // shapes), disable_player_collision; the bounding radius; the position moved by the offset.
    void InitFromStructure(Script::CStruct* params);

    // The shapes (each replaces the contact points; half sizes; w 1):
    // 0x0042D4B0: the vectors of `points` (w 0).
    void SetContacts(Script::CArray* points);
    // 0x0042D560: a box of 8 corners, the top four from `top` (y up), the bottom four from `bottom` (y down).
    void SetBox(const Mth::Vector* top, const Mth::Vector* bottom);
    // 0x0042D690: a pyramid of 5: a square base (half side `halfDepth`) at -0.6 `halfHeight`, its tip at 1.6.
    void SetPyramid(float halfHeight, float halfDepth);
    // 0x0042D790: a cylinder of two rings of `edges` points each: the bottom (y -`halfHeight`, `bottomRadius`) then
    // the top (`topRadius`), from the last point round.
    void SetCylinder(float topRadius, float bottomRadius, float halfHeight, int edges);
    // 0x0042D8A0: a triangular prism of 6 from `dimensions`.
    void SetTriangle(const Mth::Vector* dimensions);
    // 0x0042ED60: the spheres from the structures of `spheres` (the unnamed vector, radius=; both asserted).
    void SetSpheres(Script::CArray* spheres);
    // 0x0042EB10: the sounds of `type` (RigidBodySounds' structure): CollideSound / BounceSound (copied), and with a
    // BounceSound its CollideMuteDelay / GlobalCollideMuteDelay / BounceVelocityCallbackThreshold /
    // BounceVelocityFullSpeed.
    void SetSounds(uint32_t type);

    // --- script member functions (the table at 0x00683F28) ---
    bool RigidBody_Wake(Script::CStruct* params, Script::CScript* script);            // 0x0042DCA0
    bool RigidBody_Sleep(Script::CStruct* params, Script::CScript* script);           // 0x0042DCF0 (0x0042BE90)
    // 0x0042DD00 Rigidbody_Kick: vel=, rotvel= and pos= added to its own (the object's velocity and position too);
    // awake.
    bool Rigidbody_Kick(Script::CStruct* params, Script::CScript* script);
    // 0x0042DEB0 RigidBody_SetVelocities: vel= / rotvel= set; awake.
    bool RigidBody_SetVelocities(Script::CStruct* params, Script::CScript* script);
    // 0x0042EE30 RigidBody_Enable: awake where the object is now: its position, orientation and velocity taken.
    bool RigidBody_Enable(Script::CStruct* params, Script::CScript* script);
    bool RigidBody_DisablePlayerCollision(Script::CStruct* params, Script::CScript* script); // 0x0042DFB0
    bool RigidBody_EnablePlayerCollision(Script::CStruct* params, Script::CScript* script);  // 0x0042DFC0
    // 0x0042DFD0 [0xD4A0946B]: CenterOfMassPos (its position) added to the script's parameters.
    bool GetCenterOfMassPos(Script::CStruct* params, Script::CScript* script);
    // 0x0042E020 Rigidbody_MatchVelocityTo: the velocity (and, if it's a rigid body, the rotational velocity) of the
    // object whose id is the first parameter - with ApplyRandomAdjustment, scaled by 1 +- 0.4 (rotation: 0 +- 0.4);
    // asleep, a vertical speed under 1 becomes 1; awake.
    bool Rigidbody_MatchVelocityTo(Script::CStruct* params, Script::CScript* script);

    // Awake from now (where the object is) unless it already is (the inlined end of the member functions).
    void Wake();

    // --- the rest of its script member functions (rigidbody_support.cpp) ---
    // 0x0042BB00 (cdecl): its member function table (0x00683F28) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x0042BB60 RigidBody_IgnoreSkater: the pushers aren't felt for the unnamed float's seconds (asserted; with
    // frames / frame, that many sixtieths).
    bool RigidBody_IgnoreSkater(Script::CStruct* params, Script::CScript* script);
    bool RigidBody_Disable(Script::CStruct* params, Script::CScript* script); // 0x0042BBB0: disabled
    bool RigidBody_Reset(Script::CStruct* params, Script::CScript* script);   // 0x0042BBD0: InitFromStructure again
    // 0x0042BD90 (vtable slot 3): the sound component found; each spring's anchor is where its point is now.
    void Finalize();
    // 0x0042BE90: asleep (SettleScript run first, CallbackParams its parameters): no velocity, no turning.
    void Sleep();

    // --- the physics (rigidbody_physics.cpp) ---
    // A contact with the world this frame (0x30 bytes; found by 0x0042C950 into the array at 0x006949B0, its count
    // m_contactsHit): where (from the centre of mass, world axes), the surface's normal, the impulse a unit change of
    // speed along the normal takes (worked out once), the impulse given so far, how deep it is, and its directed
    // friction.
    struct Contact {
        Mth::Vector offset;
        Mth::Vector normal;
        float impulseScale; // +0x20
        float impulse;      // +0x24
        float depth;        // +0x28
        bool active;        // +0x2C
        bool scaled;        // +0x2D impulseScale worked out
        int8_t friction;    // +0x2E its directed friction (an index into mp_directedFriction)
        uint8_t pad2F;
    };
    // The shape turned to the world this frame (from the centre of mass): the contact points (0x00694E30), the
    // spheres' centres (0x00694FB0), the springs' points (0x00695130); and the contacts (0x006949B0).
    static Mth::Vector* WorldContactPoints() { return reinterpret_cast<Mth::Vector*>(0x00694E30); }
    static Mth::Vector* WorldSphereCenters() { return reinterpret_cast<Mth::Vector*>(0x00694FB0); }
    static Mth::Vector* WorldSpringPoints() { return reinterpret_cast<Mth::Vector*>(0x00695130); }
    static Contact* Contacts() { return reinterpret_cast<Contact*>(0x006949B0); }

    // 0x00430E20 (vtable slot 1): disabled: nothing. The push cooldown counted down, else the pushers felt
    // (0x0042C140); the die timer (dead when it runs out). Asleep: only the display matrix set. Awake: the world
    // points (0x0042BF00), then up to 5 steps of the frame, each a line from the centre of mass along the path to
    // the frame's end - a hit cuts the step short (6 back from the hit) and is collided with; the fifth puts it to
    // sleep (0x0042BE90). Then the contacts (0x0042C950 finds them) resolved, the springs, the damping (0x0042C6D0)
    // and sleeping (0x0042C890); the object's position (by the turned model offset), matrices and velocity set.
    void Update();
    // 0x0042E540: `dt` seconds: the position moved (gravity's half step in it, unless m_unknown191 >= 3), the
    // velocity accelerated (the same), the orientation turned by the rotational velocity and normalised, its matrix,
    // and the world points.
    void Integrate(float dt);
    // 0x00430BE0: a world hit (the feeler's point and normal): the velocity along the normal bounced back by the
    // restitution, the velocity across it slowed by friction (at most what stops it), then OnBounce.
    void Collide(Nx::CFeeler* feeler);
    // 0x00430140: the contacts resolved: impulses until none is approaching (at most 20 rounds - then the stuck
    // script, or no velocity at all), friction (along the directed friction where there is one), each pushed out
    // by its depth (a spring), then OnBounce at the last contact with the fastest approach.
    void ResolveContacts();
    // 0x0042CF90: the springs: their points' velocities taken out by impulses (up to 20 rounds), then each pulled to
    // its anchor.
    void ApplySprings();
    // 0x0042CEE0: the impulse that changes the speed along `normal` at `offset` by 1 (thiscall, the result left
    // unrounded in ST0).
    double ImpulseScale(const Mth::Vector* normal, const Mth::Vector* offset) const;
    // 0x0042E980: a bounce at `speed` (faster than the threshold): BounceScript run (CallbackParams and the point,
    // 0x43F454D0, in its parameters), the bounce sound played at Percent of the full speed while 12 voices are free.
    void OnBounce(float speed, const Mth::Vector* point);
    // 0x0042D9D0: the shape, spheres and springs drawn (compiled out of this build: nothing is drawn).
    void DebugDraw();

    // (rigidbody_support.cpp)
    // 0x0042BF00: the contact points, the spheres' centres and the active springs' points turned to the world
    // (WorldContactPoints / WorldSphereCenters / WorldSpringPoints; w as the body's).
    void UpdateWorldPoints();
    // 0x0042C140: the first pusher (AddRigidBodyPusher's) touching it and moving at it pushes it.
    void FeelPushers();
    // 0x0042C950 (rigidbody_contacts.cpp): the contacts with the world this frame (Contacts(), m_contactsHit of
    // them, m_unknown191 touching): the contact points' lines and the spheres' queries. Whether any is touching.
    bool FindContacts();
    // 0x0042C6D0: the velocity and the rotational velocity slowed by m_unknown38 / m_unknown3C (each a speed taken
    // off; slower than that: stopped - w 1).
    void Damp();
    // 0x0042C890: settled 3 frames or more and slower than both sleep points: asleep - and, dying upon settling, the
    // die timer started (1.5 s) once 48 or more from where it woke.
    void CheckSleep();

    float m_massOverMoment;                // +0x18 mass_over_moment (0.003)
    Mth::Vector m_constAcc;                // +0x1C const_acc (gravity: (0, -386.4, 0, 1))
    float m_restitution;                   // +0x2C coeff_restitution (0.4)
    float m_friction;                      // +0x30 coeff_friction (0.5)
    float m_springConst;                   // +0x34 spring_const (10)
    float m_unknown38;                     // +0x38 (0xDE76C3A7): the linear damping (0x0042C6D0)
    float m_unknown3C;                     // +0x3C (0x9DA897CF): the angular damping (0x0042C6D0)
    float m_skaterRadius;                  // +0x40 skater_collision_radius (48)
    float m_skaterApplicationRadius;       // +0x44 skater_collision_application_radius (12)
    float m_skaterAssentCos;               // +0x48 skater_collision_assent (25 degrees)
    float m_skaterAssentSin;               // +0x4C
    float m_skaterImpulseFactor;           // +0x50 skater_collision_impulse_factor (1)
    float m_skaterRotationFactor;          // +0x54 skater_collision_rotation_factor (1)
    float m_linearSleepSquared;            // +0x58 linear_velocity_sleep_point squared (100)
    float m_angularSleepSquared;           // +0x5C angular_velocity_sleep_point squared (0.16)
    Mth::Vector* mp_contacts;              // +0x60 the shape's points (body space)
    Mth::Capsule m_capsule;                // +0x64
    DirectedFriction* mp_directedFriction; // +0x88 one per contact (directed_friction=)
    uint16_t m_contactCount;               // +0x8C
    uint16_t pad8E;
    Sphere* mp_spheres;                    // +0x90
    uint16_t m_sphereCount;                // +0x94
    uint16_t pad96;
    Spring* mp_springs;                    // +0x98 (springs=)
    uint16_t m_springCount;                // +0x9C
    uint16_t pad9E;
    Mth::Vector m_centerOfMass;            // +0xA0 center_of_mass
    Mth::Vector m_modelOffset;             // +0xB0 the model's offset from the centre of mass
    float m_pushCooldownTime;              // +0xC0 (0x1F4D0CA5; 1/12): seconds the pushers aren't felt after a push
    uint32_t m_collideScript;              // +0xC4 CollideScript
    uint32_t m_bounceScript;               // +0xC8 BounceScript
    uint32_t m_settleScript;               // +0xCC SettleScript
    uint32_t m_scriptD0;                   // +0xD0 (0xCCB84047): run when the contacts can't be resolved
    Script::CStruct* mp_callbackParams;    // +0xD4 CallbackParams (a copy)
    Script::CStruct* mp_collideSound;      // +0xD8 the sound type's CollideSound (a copy)
    Script::CStruct* mp_bounceSound;       // +0xDC ... BounceSound
    int m_collideMuteDelay;                // +0xE0 (1000)
    int m_globalCollideMuteDelay;          // +0xE4 (100)
    float m_bounceCallbackThreshold;       // +0xE8 BounceVelocityCallbackThreshold (20)
    float m_bounceFullSpeed;               // +0xEC BounceVelocityFullSpeed (300)
    uint32_t m_unknownF0;                  // +0xF0
    uint32_t m_flags;                      // +0xF4 FLAG_*
    Mth::Vector m_wakePos;                 // +0xF8 where it last woke
    float m_dieTimer;                      // +0x108 (-1: none)
    int m_state;                           // +0x10C State (rigidbody_state=)
    Mth::Vector m_pos;                     // +0x110 its centre of mass
    Mth::Vector m_vel;                     // +0x120
    Mth::Quat m_orientation;               // +0x130
    Mth::Vector m_rotVel;                  // +0x140
    Mth::Matrix m_matrix;                  // +0x150 the orientation as a matrix
    int8_t m_contactsHit;                  // +0x190 contacts touching this frame
    int8_t m_unknown191;                   // +0x191 (3 or more: Integrate leaves gravity out)
    uint8_t pad192[2];
    float m_pushCooldown;                  // +0x194 the time left of m_pushCooldownTime
    float m_radius;                        // +0x198 the bounding radius
    CSoundComponent* mp_soundComponent;    // +0x19C the object's sound component (for the bounce sound)
};
static_assert(offsetof(CRigidBodyComponent, m_constAcc) == 0x1C && offsetof(CRigidBodyComponent, mp_contacts) == 0x60 &&
                  offsetof(CRigidBodyComponent, m_capsule) == 0x64 &&
                  offsetof(CRigidBodyComponent, mp_directedFriction) == 0x88 &&
                  offsetof(CRigidBodyComponent, m_contactCount) == 0x8C &&
                  offsetof(CRigidBodyComponent, m_centerOfMass) == 0xA0 &&
                  offsetof(CRigidBodyComponent, mp_callbackParams) == 0xD4 &&
                  offsetof(CRigidBodyComponent, m_flags) == 0xF4 && offsetof(CRigidBodyComponent, m_state) == 0x10C &&
                  offsetof(CRigidBodyComponent, m_pos) == 0x110 && offsetof(CRigidBodyComponent, m_matrix) == 0x150 &&
                  offsetof(CRigidBodyComponent, m_contactsHit) == 0x190 &&
                  offsetof(CRigidBodyComponent, m_radius) == 0x198 && sizeof(CRigidBodyComponent) == 0x1A0,
              "layout must match the original");
static_assert(sizeof(CRigidBodyComponent::Spring) == 0x24 && sizeof(CRigidBodyComponent::Sphere) == 0x14 &&
                  sizeof(CRigidBodyComponent::DirectedFriction) == 0x14 &&
                  sizeof(CRigidBodyComponent::Contact) == 0x30 &&
                  offsetof(CRigidBodyComponent::Contact, impulseScale) == 0x20 &&
                  offsetof(CRigidBodyComponent::Contact, active) == 0x2C,
              "layout must match the original");

// 0x0042E380: `object` (a skater) added to the objects rigid bodies feel (8 places, 0x00695150: the last free one
// from the end; none free: nothing), its extra reach (0x006939B0) back to 0.
void __cdecl AddRigidBodyPusher(CCompositeObject* object);
// 0x0042BD60 (cdecl; skater.cpp's 0x005802C0): `object`'s extra reach as a pusher (0x006939B0, one float for each
// of the 8) - nothing if it isn't one.
void __cdecl SetRigidBodyPusherReach(CCompositeObject* object, float reach);
// 0x0042BAC0 ToggleRigidBodyDebug: the rigid bodies' debug drawing stepped: off, the shapes, the details too, off.
bool __cdecl CFunc_ToggleRigidBodyDebug(Script::CStruct* params, Script::CScript* script);

GAME_GLOBAL_EXTERN(float, g_rigidBodyPusherHeight);
GAME_GLOBAL_EXTERN(bool, g_rigidBodyDrawShape);
GAME_GLOBAL_EXTERN(bool, g_rigidBodyDrawDetails);

} // namespace Obj
