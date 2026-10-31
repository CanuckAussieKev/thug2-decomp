// The rigid body component: its life, shapes, set-up and script member functions (game/obj/rigidbody_component.h).

#include "game/obj/rigidbody_component.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/maths/mat4.h"
#include "game/maths/random.h"
#include "game/obj/compositemgr.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/engine3d/coll_cache.h"
#include "game/sys/startup_init.h"

namespace GameObjects {

float g_rigidBodyPusherReach[8];
RigidBodyCacheStorage g_rigidBodyCache;
CBodyDynamicsModule::Contact g_rigidBodyContacts[24];
Maths::Vec4 g_rigidBodyWorldContactPoints[24];
Maths::Vec4 g_rigidBodyWorldSphereCenters[24];
Maths::Vec4 g_rigidBodyWorldSpringPoints[2];
CSharedRef<CGameEntity> g_rigidBodyPushers[8];

Maths::Vec4* CBodyDynamicsModule::WorldContactPoints() { return g_rigidBodyWorldContactPoints; }
Maths::Vec4* CBodyDynamicsModule::WorldSphereCenters() { return g_rigidBodyWorldSphereCenters; }
Maths::Vec4* CBodyDynamicsModule::WorldSpringPoints() { return g_rigidBodyWorldSpringPoints; }
CBodyDynamicsModule::Contact* CBodyDynamicsModule::Contacts() { return g_rigidBodyContacts; }
float g_rigidBodyPusherHeight;

namespace {

extern void** const kVtable;

constexpr float kHalf = 0.5f;                              // 0x006453BC
constexpr float kDegreesToRadians = 0.01745329238474369f;  // 0x00645464
constexpr double kAssent = 0.4363323251406352;             // 0x006463F0 (25 degrees)
constexpr float kPyramidBase = -0.6000000238418579f;       // 0x006463E8
constexpr float kPyramidTip = 1.600000023841858f;          // 0x006463E4
constexpr float kAlmostTwoPi = 6.2829999923706055f;        // 0x006463EC
constexpr float kBigShape = 30.0f;                         // 0x00646240
constexpr float kShapeScale = 0.03333333507180214f;        // 0x00646430

// The x87's own FCOS / FSIN (the original's; the C runtime's round differently), the result stored as a float.
float X87Cos(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
float X87Sin(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
// cos(angle) * value / sin(angle) * value, the cosine or sine kept at the x87's precision.
float X87CosTimes(double angle, float value)
{
    float result;
    __asm {
        fld angle
        fcos
        fmul value
        fstp result
    }
    return result;
}
float X87SinTimes(double angle, float value)
{
    float result;
    __asm {
        fld angle
        fsin
        fmul value
        fstp result
    }
    return result;
}

// The quaternion's matrix.
void QuatToMatrix(const Maths::QuatRotation* quat, Maths::Mat4* matrix) { quat->FetchMat(matrix); }

// A structure copied into a new one (from the script pools), and one deleted.
Scripts::CStruct* CopyOf(Scripts::CStruct* source)
{
    Scripts::CStruct* copy = Scripts::NewStruct();
    if (copy)
        copy->PrepareAll();
    copy->MergeStructure(source); // (out of memory: through null, as the original)
    return copy;
}
void EraseOne(Scripts::CStruct* structure)
{
    structure->Clear();
    Scripts::DeleteStruct(structure);
}

// An array of `size` bytes from the default heap in place of `old` (freed).
template <typename T>
T* Reallocate(T* old, uint32_t size)
{
    if (old)
        MemSys::Hub::Pull()->Dealloc(old);
    return static_cast<T*>(MemSys::Alloc(size));
}

// A script vector's coordinates (x at +4).
const float* VectorOf(uint32_t element) { return reinterpret_cast<const float*>(element) + 1; }

} // namespace

// 0x0042EF00
CObjectModule* __cdecl CBodyDynamicsModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CBodyDynamicsModule));
    return memory ? static_cast<CBodyDynamicsModule*>(memory)->MakeNew() : nullptr;
}

// 0x0042E3F0
CBodyDynamicsModule* CBodyDynamicsModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    new (&m_capsule) Maths::Capsule();
    m_flagBits = 0;
    m_orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    m_kindTag = kType;
    m_constAcc = {0.0f, -386.3999938964844f, 0.0f, 1.0f};
    m_skaterImpulseFactor = 1.0f;
    m_skaterRotationFactor = 1.0f;
    m_massOverMoment = 0.003000000026077032f;
    m_restitution = 0.4000000059604645f;
    m_dragFactor = 0.5f;
    m_springConst = 10.0f;
    m_linearSleepSquared = 100.0f;
    m_angularSleepSquared = 0.1600000113248825f; // (0x3E23D70B)
    m_skaterRadius = 48.0f;
    m_skaterApplicationRadius = 12.0f;
    m_pushCooldownTime = 0.0833333358168602f;
    m_skaterAssentCos = X87Cos(kAssent);
    m_skaterAssentSin = X87Sin(kAssent);
    m_modelOffset = {0.0f, 0.0f, 0.0f, 1.0f};
    m_scriptD0 = 0;
    m_settleScript = 0;
    m_bounceScript = 0;
    m_collideScript = 0;
    m_collideMuteDelay = 1000;
    m_globalCollideMuteDelay = 100;
    m_bounceCallbackThreshold = 20.0f;
    m_bounceFullSpeed = 300.0f;
    m_dieTimer = -1.0f;
    return this;
}

// 0x0042EF60
void CBodyDynamicsModule::DestroyBody()
{
    m_vtable = kVtable;
    for (void* array : {static_cast<void*>(mp_contacts), static_cast<void*>(mp_directedFriction),
                        static_cast<void*>(mp_spheres), static_cast<void*>(mp_springs)}) {
        if (array)
            MemSys::Hub::Pull()->Dealloc(array);
    }
    for (Scripts::CStruct* structure : {mp_callbackParams, mp_bounceSound, mp_collideSound}) {
        if (structure)
            EraseOne(structure);
    }
    CObjectModule::DestroyBody();
}

// 0x00430DF0
CBodyDynamicsModule* CBodyDynamicsModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0042D4B0
void CBodyDynamicsModule::SetContacts(Scripts::CArray* points)
{
    m_contactCount = static_cast<uint16_t>(points->m_extent);
    mp_contacts = Reallocate(mp_contacts, m_contactCount * sizeof(Maths::Vec4));
    const uint32_t* elements = points->FindArrayValue();
    for (uint32_t i = 0; i < m_contactCount; ++i) {
        const float* v = VectorOf(elements[i]);
        mp_contacts[i] = {v[0], v[1], v[2], 0.0f};
    }
}

// 0x0042D560
void CBodyDynamicsModule::SetBox(const Maths::Vec4* top, const Maths::Vec4* bottom)
{
    m_contactCount = 8;
    mp_contacts = Reallocate(mp_contacts, m_contactCount * sizeof(Maths::Vec4));
    int n = 0;
    for (bool right : {false, true}) {
        for (bool upper : {false, true}) {
            const Maths::Vec4& half = upper ? *top : *bottom;
            for (bool front : {false, true}) {
                mp_contacts[n++] = {half.x * (right ? 1.0f : -1.0f), upper ? top->y : -bottom->y,
                                    half.z * (front ? 1.0f : -1.0f), 1.0f};
            }
        }
    }
}

// 0x0042D690
void CBodyDynamicsModule::SetPyramid(float halfHeight, float halfDepth)
{
    m_contactCount = 5;
    mp_contacts = Reallocate(mp_contacts, m_contactCount * sizeof(Maths::Vec4));
    const float base = halfHeight * kPyramidBase;
    mp_contacts[0] = {halfDepth, base, halfDepth, 1.0f};
    mp_contacts[1] = {-halfDepth, base, halfDepth, 1.0f};
    mp_contacts[2] = {-halfDepth, base, -halfDepth, 1.0f};
    mp_contacts[3] = {halfDepth, base, -halfDepth, 1.0f};
    mp_contacts[4] = {0.0f, halfHeight * kPyramidTip, 0.0f, 1.0f};
}

// 0x0042D790 (the angle's cosine taken unrounded, its sine from the angle rounded to a float, as the original)
void CBodyDynamicsModule::SetCylinder(float topRadius, float bottomRadius, float halfHeight, int edges)
{
    m_contactCount = static_cast<uint16_t>(edges * 2);
    mp_contacts = Reallocate(mp_contacts, m_contactCount * sizeof(Maths::Vec4));
    int n = 0;
    for (bool upper : {false, true}) {
        const float radius = upper ? topRadius : bottomRadius;
        for (int i = edges; i != 0;) {
            --i;
            double angle = static_cast<double>(i) * kAlmostTwoPi / static_cast<double>(edges);
            float rounded = static_cast<float>(angle);
            mp_contacts[n++] = {X87CosTimes(angle, radius), upper ? halfHeight : -halfHeight,
                                X87SinTimes(rounded, radius), 1.0f};
        }
    }
}

// 0x0042D8A0
void CBodyDynamicsModule::SetTriangle(const Maths::Vec4* dimensions)
{
    m_contactCount = 6;
    mp_contacts = Reallocate(mp_contacts, m_contactCount * sizeof(Maths::Vec4));
    const Maths::Vec4& d = *dimensions;
    int n = 0;
    for (bool back : {false, true}) {
        float z = (back ? -1.0f : 1.0f) * d.z;
        mp_contacts[n++] = {d.x, -d.y, z, 1.0f};
        mp_contacts[n++] = {-d.x, -d.y, z, 1.0f};
        mp_contacts[n++] = {0.0f, d.y, z, 1.0f};
    }
}

// 0x0042ED60
void CBodyDynamicsModule::SetSpheres(Scripts::CArray* spheres)
{
    m_sphereCount = static_cast<uint16_t>(spheres->m_extent);
    mp_spheres = Reallocate(mp_spheres, m_sphereCount * sizeof(Sphere));
    const uint32_t* elements = spheres->FindArrayValue();
    for (uint32_t i = 0; i < m_sphereCount; ++i) {
        auto* settings = reinterpret_cast<Scripts::CStruct*>(elements[i]);
        settings->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&mp_spheres[i].center), 1);
        settings->FetchFloat(0xC48391A5, &mp_spheres[i].radius, 1); // radius
    }
}

// 0x0042EB10
void CBodyDynamicsModule::SetSounds(uint32_t type)
{
    Scripts::CStruct* sounds = nullptr;
    Scripts::ReadSubStruct(0xB9738EBA)->ReadSubStruct(type, &sounds, 0); // RigidBodySounds
    // (no such type: through null, as the original)
    Scripts::CStruct* sound;
    if (sounds->HasEntryNamed(0xBF8E0ACE)) { // CollideSound
        sounds->ReadSubStruct(0xBF8E0ACE, &sound, 1);
        if (mp_collideSound)
            EraseOne(mp_collideSound);
        mp_collideSound = CopyOf(sound);
    }
    if (!sounds->HasEntryNamed(0x1FB2F60C)) // BounceSound
        return;
    sounds->ReadSubStruct(0x1FB2F60C, &sound, 1);
    if (mp_bounceSound)
        EraseOne(mp_bounceSound);
    mp_bounceSound = CopyOf(sound);
    if (sounds->HasEntryNamed(0x3818EE47)) { // CollideMuteDelay
        // (read into the argument slot, which then holds the copy's allocation)
        int delay = static_cast<int>(reinterpret_cast<uint32_t>(mp_bounceSound));
        sounds->FetchIntValue(0x3818EE47, &delay, 0);
        m_collideMuteDelay = delay;
    }
    if (sounds->HasEntryNamed(0xAD67A34D)) { // GlobalCollideMuteDelay
        int delay = 0; // (uninitialised in the original: only read when it's given)
        sounds->FetchIntValue(0xAD67A34D, &delay, 0);
        m_globalCollideMuteDelay = delay;
    }
    sounds->FetchFloat(0x4511CA8D, &m_bounceCallbackThreshold, 0); // BounceVelocityCallbackThreshold
    sounds->FetchFloat(0x8FC6519F, &m_bounceFullSpeed, 0);         // BounceVelocityFullSpeed
}

// 0x0042F050
void CBodyDynamicsModule::ConfigureFromData(Scripts::CStruct* params)
{
    m_curPhase = STATE_ASLEEP;
    uint32_t state;
    if (params->ReadHashValue(0x61FBB62C, &state, 0)) { // rigidbody_state
        if (state == 0x4484B712)                       // asleep
            m_curPhase = STATE_ASLEEP;
        else if (state == 0x7B01911C)                  // disabled
            m_curPhase = STATE_DISABLED;
        else if (state == 0x99A34896)                  // awake
            m_curPhase = STATE_AWAKE;
    }
    m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
    m_rotVel = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int r = 0; r < 4; ++r)
        m_basisFrame.row[r] = {r == 0 ? 1.0f : 0.0f, r == 1 ? 1.0f : 0.0f, r == 2 ? 1.0f : 0.0f, r == 3 ? 1.0f : 0.0f};
    m_atPoint = m_entity->m_atPoint;
    Maths::QuatRotation orientation;
    orientation.FromMatrix(m_entity->m_basisFrame);
    m_orientation = orientation;
    m_orientation.MakeUnit(1.0f);
    QuatToMatrix(&m_orientation, &m_basisFrame);

    auto vector = [params](uint32_t name, Maths::Vec4* out) {
        return params->FetchVec(name, reinterpret_cast<Scripts::VectorValue*>(out), 0);
    };
    vector(0x0C4C809E, &m_vel);          // vel
    vector(0xFB1A83B2, &m_rotVel);       // rotvel
    vector(0x737612C6, &m_centerOfMass); // center_of_mass
    if (!params->FetchFloat(0x6FAE5C9A, &m_constAcc.y, 0)) // const_acc: a number is the y
        vector(0x6FAE5C9A, &m_constAcc);
    params->FetchFloat(0x60B4860D, &m_restitution, 0); // coeff_restitution
    params->FetchFloat(0xF9D75930, &m_dragFactor, 0);    // coeff_friction
    params->FetchFloat(0xF1ECB30F, &m_springConst, 0); // spring_const
    params->FetchFloat(0xDE76C3A7, &m_unknown38, 0);
    params->FetchFloat(0x9DA897CF, &m_unknown3C, 0);
    if (params->HasEntryNamed(0xC11E4DD8)) { // linear_velocity_sleep_point
        params->FetchFloat(0xC11E4DD8, &m_linearSleepSquared, 0);
        m_linearSleepSquared = m_linearSleepSquared * m_linearSleepSquared;
    }
    if (params->HasEntryNamed(0xA457ECE3)) { // angular_velocity_sleep_point
        params->FetchFloat(0xA457ECE3, &m_angularSleepSquared, 0);
        m_angularSleepSquared = m_angularSleepSquared * m_angularSleepSquared;
    }
    params->FetchFloat(0x3390D1E5, &m_skaterImpulseFactor, 0);     // skater_collision_impulse_factor
    params->FetchFloat(0x5A18F61F, &m_skaterRotationFactor, 0);    // skater_collision_rotation_factor
    params->FetchFloat(0x85F112C1, &m_skaterRadius, 0);            // skater_collision_radius
    params->FetchFloat(0x8AB2DB68, &m_skaterApplicationRadius, 0); // skater_collision_application_radius
    if (params->HasEntryNamed(0xE9156E6D)) {            // skater_collision_assent (degrees)
        float degrees;
        params->FetchFloat(0xE9156E6D, &degrees, 0);
        double radians = static_cast<double>(degrees) * kDegreesToRadians;
        m_skaterAssentCos = X87Cos(radians);
        m_skaterAssentSin = X87Sin(radians);
    }
    params->FetchFloat(0x1F4D0CA5, &m_pushCooldownTime, 0);
    params->ReadHashValue(0x58CDC0F0, &m_collideScript, 0); // CollideScript
    params->ReadHashValue(0x2D075F90, &m_bounceScript, 0);  // BounceScript
    params->ReadHashValue(0x571917FA, &m_settleScript, 0);  // SettleScript
    params->ReadHashValue(0xCCB84047, &m_scriptD0, 0);
    if (params->HasEntryNamed(0x0DD85BDA)) { // CallbackParams (the old copy is kept - leaked)
        Scripts::CStruct* callback;
        params->ReadSubStruct(0x0DD85BDA, &callback, 1);
        mp_callbackParams = CopyOf(callback);
    }
    if (params->HasEntryNamed(0x94956E48)) { // SoundType
        uint32_t type;
        params->ReadHashValue(0x94956E48, &type, 1);
        SetSounds(type);
    }
    if (params->HasBareFlag(0x5E51924F)) // DieUponSettling
        m_flagBits |= FLAG_DIE_UPON_SETTLING;
    else
        m_flagBits &= ~FLAG_DIE_UPON_SETTLING;

    if (params->HasEntryNamed(0xB77BE94A)) { // springs
        Scripts::CArray* springs;
        params->FetchList(0xB77BE94A, &springs, 1);
        m_springCount = static_cast<uint16_t>(springs->m_extent);
        mp_springs = Reallocate(mp_springs, m_springCount * sizeof(Spring));
        // (each built by the vector constructor iterator 0x00401020 with 0x005902E0 - which does nothing)
        const uint32_t* elements = springs->FindArrayValue();
        for (int i = 0; i < m_springCount; ++i) {
            const float* v = VectorOf(elements[i]);
            mp_springs[i].point = {v[0], v[1], v[2], 0.0f};
            mp_springs[i].active = true;
        }
    }

    // The shape: the contacts or spheres given, else Box (the default), Pyramid, Cylinder or Triangle.
    int shape = m_contactCount ? 4 : 0;
    if (params->HasEntryNamed(0xCCBFEA8C)) { // contacts
        Scripts::CArray* contacts;
        params->FetchList(0xCCBFEA8C, &contacts, 0);
        SetContacts(contacts);
        shape = 4;
    }
    if (params->HasEntryNamed(0x5B6A8D18)) { // (the spheres)
        Scripts::CArray* spheres;
        params->FetchList(0x5B6A8D18, &spheres, 0);
        SetSpheres(spheres);
        shape = 4;
    }
    if (params->HasBareFlag(0xF756B7C5))       // Box
        shape = 0;
    else if (params->HasBareFlag(0x1CB1A2B6))  // Pyramid
        shape = 1;
    else if (params->HasBareFlag(0x64FBA415))  // Cylinder
        shape = 2;
    else if (params->HasBareFlag(0x20689278))  // Triangle
        shape = 3;
    switch (shape) {
    case 0: {
        Maths::Vec4 top = {32.0f, 32.0f, 16.0f, 1.0f};
        params->FetchVec("dimensions", reinterpret_cast<Scripts::VectorValue*>(&top), 0);
        Maths::Vec4 bottom = top;
        params->FetchVec("top_dimensions", reinterpret_cast<Scripts::VectorValue*>(&top), 0);
        params->FetchVec("bottom_dimensions", reinterpret_cast<Scripts::VectorValue*>(&bottom), 0);
        top = {top.x * kHalf, top.y * kHalf, top.z * kHalf, top.w * kHalf};
        bottom = {bottom.x * kHalf, bottom.y * kHalf, bottom.z * kHalf, bottom.w * kHalf};
        SetBox(&top, &bottom);
        break;
    }
    case 1: {
        float height = 42.0f, depth = 32.0f;
        params->FetchFloat(0x0AB21AF0, &height, 0); // height
        params->FetchFloat(0x055CE396, &depth, 0);  // depth
        height = height * kHalf;
        depth = depth * kHalf;
        SetPyramid(height, depth);
        break;
    }
    case 2: {
        float radius = 12.0f, height = 60.0f;
        int edges = 6;
        params->FetchFloat(0xC48391A5, &radius, 0); // radius
        float bottomRadius = radius;
        params->FetchFloat(0x937E3B29, &radius, 0);       // top_radius
        params->FetchFloat(0xE2AC8C3A, &bottomRadius, 0); // bottom_radius
        params->FetchFloat(0x0AB21AF0, &height, 0);       // height
        params->FetchIntValue(0x4055F24A, &edges, 0);      // edges
        height = height * kHalf;
        SetCylinder(radius, bottomRadius, height, edges);
        break;
    }
    case 3: {
        Maths::Vec4 dimensions = {32.0f, 32.0f, 16.0f, 1.0f};
        params->FetchVec(0x1D82745A, reinterpret_cast<Scripts::VectorValue*>(&dimensions), 0); // Dimensions
        dimensions = {dimensions.x * kHalf, dimensions.y * kHalf, dimensions.z * kHalf, dimensions.w * kHalf};
        SetTriangle(&dimensions);
        break;
    }
    default:
        break;
    }

    if (params->HasEntryNamed(0xC35CAC0D)) { // directed_friction: one structure per contact
        Scripts::CArray* friction;
        params->FetchList(0xC35CAC0D, &friction, 0);
        mp_directedFriction = Reallocate(mp_directedFriction, m_contactCount * sizeof(DirectedFriction));
        const uint32_t* elements = friction->FindArrayValue();
        for (int i = m_contactCount; i != 0;) {
            --i;
            auto* settings = reinterpret_cast<Scripts::CStruct*>(elements[i]);
            if (settings->HasEntryNamed(0x806FFF30)) { // none
                mp_directedFriction[i].active = false;
            } else {
                mp_directedFriction[i].active = true;
                settings->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&mp_directedFriction[i].direction), 1);
            }
        }
    }

    if (params->HasEntryNamed(0x9D2D0915)) { // Angles: the shape turned
        Maths::Vec4 angles;
        LevelNodes::ReadTurnAngles(params, &angles);
        Maths::Mat4 turn;
        turn.InitFromAngles(angles);
        auto m = [&turn](int r, int c) { return static_cast<double>(reinterpret_cast<const float*>(&turn.row[r])[c]); };
        for (int i = m_contactCount; i != 0;) {
            Maths::Vec4& p = mp_contacts[--i];
            double x = p.x, y = p.y, z = p.z;
            p.z = static_cast<float>((m(0, 2) * x + m(1, 2) * y) + m(2, 2) * z);
            p.x = static_cast<float>((m(0, 0) * x + m(1, 0) * y) + m(2, 0) * z);
            p.y = static_cast<float>((m(0, 1) * x + m(1, 1) * y) + m(2, 1) * z);
        }
        for (int i = m_sphereCount; i != 0;) {
            Maths::Vec4& p = mp_spheres[--i].center;
            double x = p.x, y = p.y, z = p.z;
            p.z = static_cast<float>((m(1, 2) * y + m(2, 2) * z) + m(0, 2) * x);
            p.x = static_cast<float>((m(1, 0) * y + m(2, 0) * z) + m(0, 0) * x);
            p.y = static_cast<float>((m(1, 1) * y + m(2, 1) * z) + m(0, 1) * x);
        }
    }

    // The centre of mass: given, else the average of the points.
    Maths::Vec4 center = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->HasEntryNamed(0x737612C6) && params->HasEntryNamed(0x737612C6))
        vector(0x737612C6, &center);
    if (center.x == 0.0f && center.y == 0.0f && center.z == 0.0f) {
        for (int i = m_contactCount; i != 0;) {
            const Maths::Vec4& p = mp_contacts[--i];
            center = {center.x + p.x, center.y + p.y, center.z + p.z, center.w + p.w};
        }
        for (int i = m_sphereCount; i != 0;) {
            const Maths::Vec4& p = mp_spheres[--i].center;
            center = {center.x + p.x, center.y + p.y, center.z + p.z, center.w + p.w};
        }
        double share = 1.0 / static_cast<double>(m_contactCount + m_sphereCount);
        center = {static_cast<float>(center.x * share), static_cast<float>(center.y * share),
                  static_cast<float>(center.z * share), static_cast<float>(center.w * share)};
    }
    Maths::Vec4 modelOffset = {0.0f, 0.0f, 0.0f, 1.0f};
    vector(0x851211B0, &modelOffset); // model_offset
    m_modelOffset = {modelOffset.x - center.x, modelOffset.y - center.y, modelOffset.z - center.z,
                     modelOffset.w - center.w};
    // The shape about the centre of mass (w too).
    auto recentre = [&center](Maths::Vec4& p) {
        p = {p.x - center.x, p.y - center.y, p.z - center.z, p.w - center.w};
    };
    for (int i = m_contactCount; i != 0;)
        recentre(mp_contacts[--i]);
    for (int i = m_sphereCount; i != 0;)
        recentre(mp_spheres[--i].center);
    for (int i = m_springCount; i != 0;)
        recentre(mp_springs[--i].point);

    // mass_over_moment, else the default scaled down by the size of a big shape.
    if (!params->FetchFloat(0x967CD3AE, &m_massOverMoment, 0)) {
        float extent = 0.0f;
        for (int i = m_contactCount; i != 0;) {
            const Maths::Vec4& p = mp_contacts[--i];
            double x = p.x, y = p.y, z = p.z;
            float length = static_cast<float>(std::sqrt((z * z + x * x) + y * y));
            if (!(extent > length))
                extent = length;
        }
        for (int i = m_sphereCount; i != 0;) {
            const Sphere& s = mp_spheres[--i];
            double x = s.center.x, y = s.center.y, z = s.center.z;
            float length = static_cast<float>(std::sqrt((z * z + x * x) + y * y) + s.radius);
            if (!(extent > length))
                extent = length;
        }
        if (extent > kBigShape) {
            double scale = static_cast<double>(extent) * kShapeScale;
            m_massOverMoment = static_cast<float>(m_massOverMoment / (scale * scale));
        }
    }
    if (params->HasBareFlag(0xA92B9954)) // disable_player_collision
        m_flagBits |= FLAG_NO_PLAYER_COLLISION;
    g_rigidBodyPusherHeight = 77.0f;

    // The bounding radius (squared lengths compared unrounded).
    m_radius = 0.0f;
    for (int i = m_contactCount; i != 0;) {
        const Maths::Vec4& p = mp_contacts[--i];
        double x = p.x, y = p.y, z = p.z;
        double lengthSquared = (z * z + x * x) + y * y;
        if (lengthSquared > m_radius)
            m_radius = static_cast<float>(lengthSquared);
    }
    for (int i = m_sphereCount; i != 0;) {
        const Sphere& s = mp_spheres[--i];
        double x = s.center.x, y = s.center.y, z = s.center.z;
        double reach = std::sqrt((z * z + x * x) + y * y) + s.radius;
        if (reach * reach > m_radius)
            m_radius = static_cast<float>(reach * reach);
    }
    m_pushCooldown = 0.0f;
    m_radius = static_cast<float>(std::sqrt(static_cast<double>(m_radius)) + 1.0f);

    // The position is the centre of mass: moved by the (turned) model offset.
    auto r = [this](int row, int c) { return static_cast<double>(reinterpret_cast<const float*>(&m_basisFrame.row[row])[c]); };
    const double ox = m_modelOffset.x, oy = m_modelOffset.y, oz = m_modelOffset.z;
    double tx = (r(2, 0) * oz + r(1, 0) * oy) + ox * r(0, 0);
    double ty = (r(2, 1) * oz + r(1, 1) * oy) + r(0, 1) * ox;
    float tz = static_cast<float>((r(2, 2) * oz + r(1, 2) * oy) + r(0, 2) * ox);
    float tw = m_modelOffset.w;
    m_atPoint.x = static_cast<float>(m_atPoint.x - tx);
    m_atPoint.y = static_cast<float>(m_atPoint.y - ty);
    m_atPoint.z = m_atPoint.z - tz;
    m_atPoint.w = m_atPoint.w - tw;
}

// Awake (the inlined end of the member functions): unless it is already, where the object is now kept.
void CBodyDynamicsModule::Wake()
{
    if (m_curPhase == STATE_AWAKE)
        return;
    m_wakePos = m_entity->m_atPoint;
    m_curPhase = STATE_AWAKE;
}

// 0x0042DCA0
bool CBodyDynamicsModule::RigidBody_Wake(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    Wake();
    return true;
}

// 0x0042DCF0
bool CBodyDynamicsModule::RigidBody_Sleep(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    Sleep();
    return true;
}

// 0x0042DD00
bool CBodyDynamicsModule::Rigidbody_Kick(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    Scripts::VectorValue v;
    if (params->FetchVec(0x0C4C809E, &v, 0)) { // vel
        m_vel = {v.x + m_vel.x, v.y + m_vel.y, v.z + m_vel.z, v.w + m_vel.w};
        m_entity->m_vel = m_vel;
    }
    if (params->FetchVec(0xFB1A83B2, &v, 0)) // rotvel
        m_rotVel = {v.x + m_rotVel.x, v.y + m_rotVel.y, v.z + m_rotVel.z, v.w + m_rotVel.w};
    if (params->FetchVec(0x7F261953, &v, 0)) { // pos
        m_atPoint = {v.x + m_atPoint.x, v.y + m_atPoint.y, v.z + m_atPoint.z, v.w + m_atPoint.w};
        m_entity->m_atPoint = m_atPoint;
    }
    Wake();
    return true;
}

// 0x0042DEB0
bool CBodyDynamicsModule::RigidBody_SetVelocities(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    Scripts::VectorValue v;
    if (params->FetchVec(0x0C4C809E, &v, 0)) { // vel
        m_vel = {v.x, v.y, v.z, v.w};
        m_entity->m_vel = m_vel;
    }
    if (params->FetchVec(0xFB1A83B2, &v, 0)) // rotvel
        m_rotVel = {v.x, v.y, v.z, v.w};
    Wake();
    return true;
}

// 0x0042EE30
bool CBodyDynamicsModule::RigidBody_Enable(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    m_curPhase = STATE_AWAKE;
    m_atPoint = m_entity->m_atPoint;
    Maths::QuatRotation orientation;
    orientation.FromMatrix(m_entity->m_basisFrame);
    m_orientation = orientation;
    m_orientation.MakeUnit(1.0f);
    QuatToMatrix(&m_orientation, &m_basisFrame);
    m_vel = m_entity->m_vel;
    return true;
}

// 0x0042DFB0
bool CBodyDynamicsModule::RigidBody_DisablePlayerCollision(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    m_flagBits |= FLAG_NO_PLAYER_COLLISION;
    return true;
}

// 0x0042DFC0
bool CBodyDynamicsModule::RigidBody_EnablePlayerCollision(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    m_flagBits &= ~FLAG_NO_PLAYER_COLLISION;
    return true;
}

// 0x0042DFD0
bool CBodyDynamicsModule::GetCenterOfMassPos(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->PutVec3(0x77808D3C, {m_atPoint.x, m_atPoint.y, m_atPoint.z, m_atPoint.w}); // CenterOfMassPos
    return true;
}

// 0x0042E020
bool CBodyDynamicsModule::Rigidbody_MatchVelocityTo(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t id = params->StepToEntry(nullptr)->checksum;
    auto* other = reinterpret_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(id));
    bool adjust = params->HasBareFlag(0x8ACE8A0F); // ApplyRandomAdjustment
    m_vel = other->m_vel;
    if (adjust) {
        double factor = Maths::PlusOrMinusExact(0.4000000059604645f) + 1.0f;
        m_vel = {static_cast<float>(factor * m_vel.x), static_cast<float>(factor * m_vel.y),
                 static_cast<float>(factor * m_vel.z), static_cast<float>(factor * m_vel.w)};
    }
    m_entity->m_vel = m_vel;
    if (m_curPhase == STATE_ASLEEP && std::fabs(m_vel.y) < 1.0f)
        m_vel.y = 1.0f;
    if (auto* body = static_cast<CBodyDynamicsModule*>(other->FetchModule(kType))) {
        m_rotVel = body->m_rotVel;
        if (adjust) {
            double factor = Maths::PlusOrMinusExact(0.4000000059604645f);
            m_rotVel = {static_cast<float>(factor * m_rotVel.x), static_cast<float>(factor * m_rotVel.y),
                        static_cast<float>(factor * m_rotVel.z), static_cast<float>(factor * m_rotVel.w)};
        }
    }
    Wake();
    return true;
}

// 0x0042E380
void __cdecl AddRigidBodyPusher(CGameEntity* object)
{
    auto* pushers = g_rigidBodyPushers;
    for (int i = 7; i >= 0; --i) {
        if (pushers[i].m_ownerObject)
            continue;
        pushers[i].AssignValue(object);
        g_rigidBodyPusherReach[i] = 0.0f; // (its extra reach: SetRigidBodyPusherReach)
        return;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CBodyDynamicsModule::Teardown,
         &GameObjects::CBodyDynamicsModule::TickFrame,
         &GameObjects::CBodyDynamicsModule::ConfigureFromData,
         &CBodyDynamicsModule::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {

// 0x00640900 (a static initialiser): the rigid bodies' collision cache (registered among the caches, empty).
void ConstructRigidBodyCache() { reinterpret_cast<Engine3D::CHitMeshCache*>(&g_rigidBodyCache)->MakeNew(); }

} // namespace Obj
