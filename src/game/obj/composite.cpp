// GameObjects::CGameEntity - the object made of components (skaters, peds, cars, level objects) - and the base of its
// components, GameObjects::CObjectModule (entity.h). From the disassembly of 0x0045D720-0x0045EE20 (methods __thiscall,
// RET n = 4 * arguments; the member functions scripts call are entries of the table at 0x00683268, filled in by
// 0x006425D0, all `bool (CGameEntity::*)(CStruct* params, CScript* script)`).

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/maths/quat_rotation.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity_hub.h"
#include "game/obj/entity.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/entity_hub.h"

namespace GameObjects {

float g_pi{3.1415927f};

using Scripts::CArray;
using Scripts::CMemberFunctionEntry;
using Scripts::CScript;
using Scripts::CStruct;

float g_compositeAcosA0{1.5707288f};
float g_compositeAcosA1{0.2121144f};
float g_compositeAcosA2{0.074261f};
float g_compositeAcosA3{-0.0187293f};
float g_compositeFeetToInches{12.0f};

namespace {

extern void** const kComponentVtable;
extern void** const kCompositeObjectVtable;

// Names (checksums).
constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kPermanent = 0x23627FD7;      // permanent
constexpr uint32_t kComponent = 0xB6015EA8;      // component
constexpr uint32_t kPos = 0x7F261953;            // pos
constexpr uint32_t kLength = 0xFE82614D;         // length
constexpr uint32_t kHeight = 0x0AB21AF0;         // height
constexpr uint32_t kDistance = 0xE36D657E;       // distance
constexpr uint32_t kShift = 0x5AF4C4BA;          // shift
constexpr uint32_t kSide = 0xDC7EE44A;           // side
constexpr uint32_t kDown = 0xE3006FC4;           // down
constexpr uint32_t kPosition = 0xB9D31B0A;       // position
constexpr uint32_t kVel = 0x0C4C809E;            // vel
constexpr uint32_t kSpeed = 0xF0D90109;          // speed
constexpr uint32_t kX = 0x7323E97C;              // x
constexpr uint32_t kY = 0x0424D9EA;              // y
constexpr uint32_t kZ = 0x9D2D8850;              // z
constexpr uint32_t kDistanceResult = 0x976B04A3; // (name unknown): Obj_GetDistanceToObject's result
constexpr uint32_t kTypeResult = 0x24495505;     // (name unknown): Obj_GetType's result
constexpr uint32_t kDist = 0x7E832F08;           // dist
constexpr uint32_t kDotProd = 0x7C6A7D7C;        // dotprod
constexpr uint32_t kAngleResult = 0xC97F3AA9;    // (name unknown): Obj_GetOrientationToObject's angle
constexpr uint32_t kDir = 0x455485EF;            // dir
constexpr uint32_t kQuat = 0x748CA262;           // quat
constexpr uint32_t kNearestType = 0xBAF9B341;    // (name unknown): the object type 0x0045EF30 looks for

// The constants the original reads (0x006461F4: inches to feet; 0x00645D74).
constexpr float kInchesToFeet = 1.0f / 12.0f;
constexpr float kSpeedTolerance = 0.1f;
constexpr float kRadiansToDegrees = 57.2957763671875f; // 0x00645468
constexpr float kRightAngle = 90.0f;                  // 0x00645EB0
constexpr float kDegreesToRadians = 0.01745329238474369f; // 0x00645464
constexpr float kNearlyVertical = 0.99f;              // 0x00645450
constexpr float kFarAway = 5322672.0f;                // (0x4AA26F60)

// The composite object manager (0x006F0FB8, compositemgr.cpp): its lists of the components of each type, and the
// component makers.
void EnrolModule(CObjectModule* component) { CompositeObjectManager()->AttachModuleOfKind(component); }
void UnregisterComponent(CObjectModule* component) { CompositeObjectManager()->DetachModuleOfKind(component); }
CObjectModule* SpawnModule(uint32_t type) { return CompositeObjectManager()->SpawnModule(type); }
// The position (pos=) / orientation in a structure, if it has them (LevelNodes, nodes.h).
void GetPositionFromStructure(CStruct* params, Maths::Vec4* pos) { LevelNodes::FetchPlace(params, pos); }
void GetOrientationFromStructure(CStruct* params, Maths::Mat4* matrix) { LevelNodes::FetchFacing(params, matrix); }

// The node named `name` (LevelNodes).
CStruct* FetchVertex(uint32_t name, int assert) { return LevelNodes::FetchVertex(name, assert != 0); }
// Quaternions (x, y, z, w): to a rotation matrix (0x0042BC40, on the quaternion), from one (0x0042E1F0, constructing
// the quaternion).
void QuatToMatrix(const float* quat, Maths::Mat4* matrix) { reinterpret_cast<const Maths::QuatRotation*>(quat)->FetchMat(matrix); }
void MatrixToQuat(float* quat, const Maths::Mat4* matrix) { reinterpret_cast<Maths::QuatRotation*>(quat)->FromMatrix(*matrix); }
// A manager's object list (its vtable +0x18).
MemSys::ListNode* ManagerObjectList(CObjectHubBase* manager)
{
    return reinterpret_cast<MemSys::ListNode*(__thiscall*)(CObjectHubBase*)>(manager->m_vtable[6])(manager);
}
// Maths::FastACos (0x00403EF0) as its callers here see it: the x87 result is used before it is rounded to a float.
double FastACosUnrounded(float value)
{
    const double a0 = g_compositeAcosA0;
    const double a1 = g_compositeAcosA1;
    const double a2 = g_compositeAcosA2;
    const double a3 = g_compositeAcosA3;
    const double pi = g_pi;
    if (value < 0.0f) {
        double x = -static_cast<double>(value);
        double root = std::sqrt(1.0 - x);
        return pi - (((x * a3 + a2) * x - a1) * x + a0) * root;
    }
    double x = value;
    double root = std::sqrt(1.0 - x);
    return (((x * a3 + a2) * x - a1) * x + a0) * root;
}
// a x b in double precision, each component rounded once; w = 0.
Maths::Vec4 Perpendicular(const Maths::Vec4& a, const Maths::Vec4& b)
{
    return {static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y),
            static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z),
            static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x), 0.0f};
}

// The object's vtable slot 3 (a member function by name).
bool VirtualCallMemberFunction(CEntity* object, uint32_t name, CStruct* params, CScript* script)
{
    return reinterpret_cast<bool(__thiscall*)(CEntity*, uint32_t, CStruct*, CScript*)>(object->m_vtable[3])(
        object, name, params, script);
}

// |v| as the original's x87 code works it out: (x*x + y*y) + z*z in double precision, not rounded to a float.
double LenOf(float x, float y, float z)
{
    double x2 = static_cast<double>(x) * x;
    double y2 = static_cast<double>(y) * y;
    double z2 = static_cast<double>(z) * z;
    return std::sqrt(x2 + y2 + z2);
}

// pos += row * scale, as the original rounds it: the x and y products stay in double precision, the z and w ones
// are rounded to floats first.
void AddScaled(Maths::Vec4& pos, const Maths::Vec4& row, float scale)
{
    double x = static_cast<double>(scale) * row.x;
    double y = static_cast<double>(scale) * row.y;
    float z = scale * row.z;
    float w = scale * row.w;
    pos.x = static_cast<float>(x + pos.x);
    pos.y = static_cast<float>(y + pos.y);
    pos.z = z + pos.z;
    pos.w = w + pos.w;
}

// Debug output of an object's details: compiled out of this build (the text is still read).
inline void PrintObjectDetails(const char* /*text*/) {}

// The collision feeler (Nx's ray, 0xB0 bytes; its code isn't in this file): constructed by 0x00417760, its line
// from +0x00 to +0x10, tested by 0x004172F0; what it hit is held by a smart pointer at +0x98, its only destructor.
class CollisionFeeler {
public:
    CollisionFeeler() { reinterpret_cast<Engine3D::CProbeRay*>(m_bytes)->MakeNew(); }
    ~CollisionFeeler()
    {
        reinterpret_cast<GameObjects::CSharedRef<GameObjects::CSharedBase>*>(m_bytes + 0x98)->~CSharedRef();
    }
    CollisionFeeler(const CollisionFeeler&) = delete;
    CollisionFeeler& operator=(const CollisionFeeler&) = delete;
    Maths::Vec4& Kickoff() { return *reinterpret_cast<Maths::Vec4*>(m_bytes); }
    Maths::Vec4& End() { return *reinterpret_cast<Maths::Vec4*>(m_bytes + 0x10); }
    bool GetHitShape(bool a, bool b)
    {
        return reinterpret_cast<Engine3D::CProbeRay*>(m_bytes)->GetHitShape(a, b) != 0;
    }

private:
    alignas(16) uint8_t m_bytes[0xB0];
};

float FeetAsInches() { return g_compositeFeetToInches; } // 12

} // namespace

// --- CEntity: a member function table entry -------------------------------------------------------------------

// 0x0045D850
bool CEntity::InvokeMethodByName(CMemberFunctionEntry* entry, CStruct* params, CScript* script)
{
    if (entry->mCode) {
        void* self = reinterpret_cast<uint8_t*>(this) + entry->mThisAdjustment;
        return reinterpret_cast<bool(__thiscall*)(void*, CStruct*, CScript*)>(entry->mCode)(self, params, script);
    }
    return VirtualCallMemberFunction(this, entry->m_callsign, params, script);
}

// --- CObjectModule -------------------------------------------------------------------------------------------

// 0x0045D730
CObjectModule* CObjectModule::MakeNew()
{
    m_vtable = kComponentVtable;
    m_flagBits = 0;
    return this;
}

// 0x0045D740
void CObjectModule::DestroyBody()
{
    m_vtable = kComponentVtable;
    if (m_flagBits & COMPONENT_FLAG_SCRIPT_WAITING)
        Scripts::ComponentDestroyedAllScripts(this);
}

// 0x0045D780
CObjectModule* CObjectModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0045D760
void CObjectModule::HoldOff(bool suspend)
{
    if (suspend)
        m_flagBits |= COMPONENT_FLAG_SUSPENDED;
    else
        m_flagBits &= ~COMPONENT_FLAG_SUSPENDED;
}

// 0x0045D720 (a jump to vtable +0x30 with the entry's name in place of the entry)
int CObjectModule::InvokeMethodByName(CMemberFunctionEntry* entry, CStruct* params, CScript* script)
{
    return InvokeMethodByName(entry->m_callsign, params, script);
}

// --- CGameEntity: life -----------------------------------------------------------------------------------

// 0x0045E510
CGameEntity* CGameEntity::MakeNew()
{
    CEntity::MakeNew();
    m_vtable = kCompositeObjectVtable;
    m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
    m_atPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_basisFrame.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_basisFrame.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_basisFrame.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_displayMatrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_displayMatrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_displayMatrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_displayMatrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_moduleList = nullptr;
    m_compositeFlags = 0;
    m_unknownBC = 0;
    m_objectFlags |= OBJECT_FLAG_HAS_POSITION;
    return this;
}

// 0x0045D890
void CGameEntity::DestroyBody()
{
    m_vtable = kCompositeObjectVtable;
    m_compositeFlags |= COMPOSITE_FLAG_DESTROYING;
    while (m_moduleList) {
        CObjectModule* component = m_moduleList;
        m_moduleList = component->m_following;
        UnregisterComponent(component);
        component->VirtualDestroy(1);
    }
    CEntity::DestroyBody();
}

// 0x0045E620
CGameEntity* CGameEntity::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// --- CGameEntity: the frame ------------------------------------------------------------------------------

// 0x0045D920
void CGameEntity::TickFrame()
{
    if (m_compositeFlags & COMPOSITE_FLAG_PAUSED)
        return;
    if (!(m_compositeFlags & COMPOSITE_FLAG_SUSPENDED)) {
        if (m_runScript)
            m_runScript->TickFrame();
        if (m_objectFlags & OBJECT_FLAG_DEAD)
            return;
        VirtualOwnUpdate();
    }
    for (CObjectModule* component = m_moduleList; component; component = component->m_following) {
        if (component->m_flagBits & COMPONENT_FLAG_SUSPENDED)
            continue;
        component->TickFrame();
        if (m_objectFlags & OBJECT_FLAG_DEAD)
            break;
    }
    m_atPoint.w = 1.0f;
    m_compositeFlags &= ~COMPOSITE_FLAG_TELEPORTED;
}

// 0x0045D990
void CGameEntity::Conceal(bool hide)
{
    if (hide == ((m_compositeFlags & COMPOSITE_FLAG_HIDDEN) != 0))
        return;
    if (hide)
        m_compositeFlags |= COMPOSITE_FLAG_HIDDEN;
    else
        m_compositeFlags &= ~COMPOSITE_FLAG_HIDDEN;
    for (CObjectModule* component = m_moduleList; component; component = component->m_following)
        component->Conceal(hide);
}

// 0x0045D9E0
void CGameEntity::HoldOff(bool suspend)
{
    if (suspend == ((m_compositeFlags & COMPOSITE_FLAG_SUSPENDED) != 0))
        return;
    if (suspend)
        m_compositeFlags |= COMPOSITE_FLAG_SUSPENDED;
    else
        m_compositeFlags &= ~COMPOSITE_FLAG_SUSPENDED;
    for (CObjectModule* component = m_moduleList; component; component = component->m_following)
        component->VirtualSuspend(suspend);
}

// 0x0045E4C0
void CGameEntity::MarkJumpedAhead(bool teleportComponents)
{
    m_compositeFlags |= COMPOSITE_FLAG_TELEPORTED;
    if (teleportComponents) {
        for (CObjectModule* component = m_moduleList; component; component = component->m_following)
            component->JumpTo();
    }
}

// 0x0045E500
bool CGameEntity::ShouldUpdateScripts() { return !(m_compositeFlags & COMPOSITE_FLAG_PAUSED); }

// 0x0045D7C0
void CGameEntity::FlagAsDead()
{
    for (CObjectModule* component = m_moduleList; component; component = component->m_following)
        component->ObjectMarkedDead();
    CEntity::FlagAsDead();
}

// --- CGameEntity: components -----------------------------------------------------------------------------

// 0x0045DA30 (the component's +0x10 isn't cleared: it is expected to be null already)
void CGameEntity::AttachModule(CObjectModule* component)
{
    if (!m_moduleList) {
        m_moduleList = component;
    } else {
        CObjectModule* last = m_moduleList;
        while (last->m_following)
            last = last->m_following;
        last->m_following = component;
    }
    component->m_entity = this;
    EnrolModule(component);
}

// 0x0045DA90
void CGameEntity::BuildModulesFromList(CArray* components, CStruct* extra)
{
    uint32_t* elements = components->FindArrayValue();
    int count = static_cast<int>(components->m_extent);
    for (int i = 0; i < count; ++i)
        VirtualCreateComponentFromStructure(reinterpret_cast<CStruct*>(elements[i]), extra);
}

// 0x0045EE20. With no component= the type looked up is `params`' own address [quirk]; a type the manager can't
// make gives a null component, added all the same [bug: crash]; so does a failed allocation of the merged copy.
void CGameEntity::CreateComponentFromStructure(CStruct* params, CStruct* extra)
{
    uint32_t type = reinterpret_cast<uint32_t>(params);
    params->ReadHashValue(kComponent, &type, 1);
    CObjectModule* component = SpawnModule(type);
    AttachModule(component);
    if (!extra) {
        component->ConfigureFromData(params);
        return;
    }
    CStruct* memory = Scripts::NewStruct();
    CStruct* merged = memory ? memory->PrepareAll() : nullptr;
    merged->MergeStructure(params);
    merged->MergeStructure(extra);
    component->ConfigureFromData(merged);
    if (merged) {
        merged->Clear();
        Scripts::DeleteStruct(merged);
    }
}

// 0x0045DAD0
void CGameEntity::ConfigureFromData(CStruct* params)
{
    uint32_t id = 0;
    if (params->ReadHashValue(kName, &id, 0))
        ChangeSerial(id);
    GetPositionFromStructure(params, &m_atPoint);
    GetOrientationFromStructure(params, &m_basisFrame);
    if (params->HasBareFlag(kPermanent))
        m_objectFlags |= OBJECT_FLAG_LOCKED;
}

// 0x0045DB30
void CGameEntity::Seal()
{
    for (CObjectModule* component = m_moduleList; component; component = component->m_following)
        component->Seal();
    m_compositeFlags |= COMPOSITE_FLAG_FINALIZED;
    m_compositeFlags |= COMPOSITE_FLAG_FINALIZED2;
    for (CObjectModule* component = m_moduleList; component; component = component->m_following)
        component->JumpTo();
}

// 0x0045DB90
CObjectModule* CGameEntity::FetchModule(uint32_t type)
{
    for (CObjectModule* component = m_moduleList; component; component = component->m_following) {
        if (component->m_kindTag == type)
            return component;
    }
    return nullptr;
}

// 0x0045E450
bool CGameEntity::InvokeMethodByName(uint32_t name, CStruct* params, CScript* script)
{
    for (CObjectModule* component = m_moduleList; component; component = component->m_following) {
        int result = component->InvokeMethodByName(name, params, script);
        if (result == 0)
            return false;
        if (result == 1)
            return true;
    }
    return CEntity::InvokeMethodByName(name, params, script);
}

// --- CGameEntity: the member functions scripts call ------------------------------------------------------

// 0x0045D7F0: the unnamed string (the details' title) is read for the debug output.
bool CGameEntity::Obj_PrintDetails(CStruct* params, CScript* /*script*/)
{
    const char* text = reinterpret_cast<const char*>(params);
    params->FetchText(kUnnamed, &text, 0);
    PrintObjectDetails(text);
    return true;
}

// 0x0045D810: a component from the parameters (CreateComponentFromStructure, through the vtable).
bool CGameEntity::Obj_CreateComponent(CStruct* params, CScript* /*script*/)
{
    VirtualCreateComponentFromStructure(params, nullptr);
    return true;
}

// 0x0045D830: position= (if given) as the position.
bool CGameEntity::Obj_SetPosition(CStruct* params, CScript* /*script*/)
{
    params->FetchVec(kPosition, reinterpret_cast<Scripts::VectorValue*>(&m_atPoint), 0);
    return true;
}

// 0x0045DBC0: pos = the position, in the calling script's parameters.
bool CGameEntity::Obj_GetPosition(CStruct* /*params*/, CScript* script)
{
    script->m_args->PutVec3(kPos, m_atPoint.x, m_atPoint.y, m_atPoint.z);
    return true;
}

// 0x0045DBF0: whether it has a component of the type name= names (required).
bool CGameEntity::Obj_ComponentExists(CStruct* params, CScript* /*script*/)
{
    uint32_t type = 0;
    params->ReadHashValue(kName, &type, 1);
    return FetchModule(type) != nullptr;
}

// 0x0045DC40: an extra update now.
bool CGameEntity::Obj_ForceUpdate(CStruct* /*params*/, CScript* /*script*/)
{
    TickFrame();
    return true;
}

// 0x0045DC50: vel = the velocity.
bool CGameEntity::Obj_GetVelocity(CStruct* /*params*/, CScript* script)
{
    script->m_args->PutVec3(kVel, m_vel.x, m_vel.y, m_vel.z);
    return true;
}

// 0x0045DC80: speed = the velocity's length.
bool CGameEntity::GetSpeed(CStruct* /*params*/, CScript* script)
{
    script->m_args->PushFloat(kSpeed, static_cast<float>(LenOf(m_vel.x, m_vel.y, m_vel.z)));
    return true;
}

// 0x0045DCD0 / 0x0045DD20 / 0x0045DD70
bool CGameEntity::Obj_Hide(CStruct* /*params*/, CScript* /*script*/)
{
    Conceal(true);
    return true;
}
bool CGameEntity::Obj_Unhide(CStruct* /*params*/, CScript* /*script*/)
{
    Conceal(false);
    return true;
}
bool CGameEntity::Obj_IsHidden(CStruct* /*params*/, CScript* /*script*/)
{
    return (m_compositeFlags & COMPOSITE_FLAG_HIDDEN) != 0;
}

// 0x0045DD80: moves along its own axes by x=, y= and z= (each 0 if missing), all four components of each row.
bool CGameEntity::Obj_MoveLocal(CStruct* params, CScript* /*script*/)
{
    float distance = 0.0f;
    params->FetchFloat(kX, &distance, 0);
    AddScaled(m_atPoint, m_basisFrame.row[0], distance);
    distance = 0.0f;
    params->FetchFloat(kY, &distance, 0);
    AddScaled(m_atPoint, m_basisFrame.row[1], distance);
    distance = 0.0f;
    params->FetchFloat(kZ, &distance, 0);
    AddScaled(m_atPoint, m_basisFrame.row[2], distance);
    return true;
}

// 0x0045DED0 / 0x0045DF20
bool CGameEntity::Obj_Suspend(CStruct* /*params*/, CScript* /*script*/)
{
    HoldOff(true);
    return true;
}
bool CGameEntity::Obj_Unsuspend(CStruct* /*params*/, CScript* /*script*/)
{
    HoldOff(false);
    return true;
}

// 0x0045E0B0: the distance in feet to the object the unnamed checksum names (required; none: 0).
bool CGameEntity::Obj_GetDistanceToObject(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(kUnnamed, &name, 1);
    auto* other = static_cast<CGameEntity*>(FindObject(name));
    float distance = 0.0f;
    if (other) {
        double dx = static_cast<double>(m_atPoint.x) - other->m_atPoint.x;
        double dy = static_cast<double>(m_atPoint.y) - other->m_atPoint.y;
        double dz = static_cast<double>(m_atPoint.z) - other->m_atPoint.z;
        distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx) * kInchesToFeet);
    }
    script->m_args->PushFloat(kDistanceResult, distance);
    return true;
}

// 0x0045E260: whether the velocity points backwards (against its matrix's at row).
bool CGameEntity::Obj_MovingBackward(CStruct* /*params*/, CScript* /*script*/)
{
    const Maths::Vec4& at = m_basisFrame.row[2];
    double along = static_cast<double>(m_vel.z) * at.z + static_cast<double>(m_vel.y) * at.y;
    along += static_cast<double>(m_vel.x) * at.x;
    return along < 0.0;
}

// 0x0045E2A0: whether the speed is within 0.1 of the unnamed float (0 if missing).
bool CGameEntity::Obj_SpeedNear(CStruct* params, CScript* /*script*/)
{
    float speed = 0.0f;
    params->FetchFloat(kUnnamed, &speed, 0);
    return std::fabs(LenOf(m_vel.x, m_vel.y, m_vel.z) - speed) < kSpeedTolerance;
}

// 0x0045E310 / 0x0045E380: whether the speed is above / below the unnamed float (0 if missing).
bool CGameEntity::Obj_SpeedAbove(CStruct* params, CScript* /*script*/)
{
    float speed = 0.0f;
    params->FetchFloat(kUnnamed, &speed, 0);
    return LenOf(m_vel.x, m_vel.y, m_vel.z) > speed;
}
bool CGameEntity::Obj_SpeedBelow(CStruct* params, CScript* /*script*/)
{
    float speed = 0.0f;
    params->FetchFloat(kUnnamed, &speed, 0);
    return LenOf(m_vel.x, m_vel.y, m_vel.z) < speed;
}

// 0x0045E3F0: its type, in the calling script's parameters.
bool CGameEntity::Obj_GetType(CStruct* /*params*/, CScript* script)
{
    script->m_args->PutNameKey(kTypeResult, m_kindTag);
    return true;
}

// 0x0045E410: whether it has a component of the type the unnamed checksum names. With none, the type looked for
// is `params`' own address [quirk].
bool CGameEntity::Obj_HasComponent(CStruct* params, CScript* /*script*/)
{
    uint32_t type = reinterpret_cast<uint32_t>(params);
    params->ReadHashValue(kUnnamed, &type, 1);
    return FetchModule(type) != nullptr;
}

// 0x0045E9E0
bool CGameEntity::Obj_GetCollision(CStruct* params, CScript* /*script*/)
{
    CollisionFeeler feeler;
    float length = 3.0f;
    params->FetchFloat(kLength, &length, 0);
    length *= FeetAsInches();
    float height = 3.0f;
    params->FetchFloat(kHeight, &height, 0);
    height *= FeetAsInches();
    float distance = 0.0f;
    params->FetchFloat(kDistance, &distance, 0);
    distance *= FeetAsInches();
    float shift = 0.0f;
    params->FetchFloat(kShift, &shift, 0);
    shift *= FeetAsInches();
    const Maths::Vec4& right = m_basisFrame.row[0];
    const Maths::Vec4& up = m_basisFrame.row[1];
    const Maths::Vec4& at = m_basisFrame.row[2];
    // The start: pos + up * height + at * distance + right * shift (x87: some sums kept at 53 bits, as below).
    float offsetCellX = shift * right.x, shiftY = shift * right.y, offsetCellZ = shift * right.z;
    double shiftW = static_cast<double>(shift) * right.w;
    float atX = distance * at.x, atY = distance * at.y;
    double atZ = static_cast<double>(distance) * at.z, atW = static_cast<double>(distance) * at.w;
    float upX = height * up.x, upY = height * up.y;
    double upZ = static_cast<double>(height) * up.z, upW = static_cast<double>(height) * up.w;
    float w1 = static_cast<float>(upW + m_atPoint.w);
    double z1 = upZ + m_atPoint.z;
    float y1 = upY + m_atPoint.y;
    float x1 = upX + m_atPoint.x;
    w1 = static_cast<float>(w1 + atW);
    float z2 = static_cast<float>(z1 + atZ);
    y1 = y1 + atY;
    x1 = x1 + atX;
    Maths::Vec4 start = {x1 + offsetCellX, y1 + shiftY, z2 + offsetCellZ, static_cast<float>(shiftW + w1)};
    feeler.Kickoff() = start;
    Maths::Vec4& end = feeler.End();
    end = start;
    // The end: `length` along the right (side), down (down) or ahead.
    if (params->HasBareFlag(kSide)) {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>(static_cast<double>(length) * (&right.x)[i] + (&end.x)[i]);
    } else if (params->HasBareFlag(kDown)) {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>((&end.x)[i] - static_cast<double>(length) * (&up.x)[i]);
    } else {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>(static_cast<double>(length) * (&at.x)[i] + (&end.x)[i]);
    }
    return feeler.GetHitShape(true, false);
}

// 0x0045E9B0 / 0x0045E9D0
bool CGameEntity::Obj_Pause(CStruct* /*params*/, CScript* /*script*/)
{
    m_compositeFlags |= COMPOSITE_FLAG_PAUSED;
    return true;
}
bool CGameEntity::Obj_Unpause(CStruct* /*params*/, CScript* /*script*/)
{
    m_compositeFlags &= ~COMPOSITE_FLAG_PAUSED;
    return true;
}

// 0x0045DF70: x, y, z = its display matrix's at row, made unit length (left as it is if its length is 0).
bool CGameEntity::Obj_GetOrientation(CStruct* /*params*/, CScript* script)
{
    float x = m_displayMatrix.row[2].x;
    float y = m_displayMatrix.row[2].y;
    float z = m_displayMatrix.row[2].z;
    double length = std::sqrt(static_cast<double>(z) * z + static_cast<double>(y) * y + static_cast<double>(x) * x);
    if (length > 0.0) {
        double scale = 1.0 / length;
        x = static_cast<float>(x * scale);
        y = static_cast<float>(y * scale);
        z = static_cast<float>(z * scale);
    }
    script->m_args->PushFloat(kX, x);
    script->m_args->PushFloat(kY, y);
    script->m_args->PushFloat(kZ, z);
    return true;
}

// 0x0045E020: dist = the distance to the node the unnamed checksum names (none: 0).
bool CGameEntity::Obj_GetDistToNode(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(kUnnamed, &name, 0);
    Maths::Vec4 nodePos;
    GetPositionFromStructure(FetchVertex(name, 1), &nodePos);
    double dx = static_cast<double>(m_atPoint.x) - nodePos.x;
    double dy = static_cast<double>(m_atPoint.y) - nodePos.y;
    double dz = static_cast<double>(m_atPoint.z) - nodePos.z;
    script->m_args->PushFloat(kDist, static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx)));
    return true;
}

// 0x0045E140: dotprod = how much the object the unnamed checksum names (required) lies along its right axis (the
// cosine: both made unit length), and the angle that makes in degrees, less 90; none: both 0.
bool CGameEntity::Obj_GetOrientationToObject(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(kUnnamed, &name, 1);
    auto* other = static_cast<CGameEntity*>(FindObject(name));
    float dot = 0.0f;
    float angle = 0.0f;
    if (other) {
        Maths::Vec4 right = {m_basisFrame.row[0].x, m_basisFrame.row[0].y, m_basisFrame.row[0].z, 0.0f};
        right.MakeUnit();
        Maths::Vec4 toOther = {other->m_atPoint.x - m_atPoint.x, other->m_atPoint.y - m_atPoint.y, other->m_atPoint.z - m_atPoint.z, 0.0f};
        toOther.MakeUnit();
        double sum = static_cast<double>(toOther.z) * right.z + static_cast<double>(toOther.y) * right.y;
        dot = static_cast<float>(sum + static_cast<double>(toOther.x) * right.x);
        angle = static_cast<float>(FastACosUnrounded(dot) * kRadiansToDegrees - kRightAngle);
    }
    script->m_args->PushFloat(kDotProd, dot);
    script->m_args->PushFloat(kAngleResult, angle);
    return true;
}

// 0x0045E650: a new orientation (also as the display matrix): dir= (looking along it, the up axis kept as near
// world up as it can be: world x when dir is within 0.99 of vertical), else quat= (x, y, z; w from them), else
// the angles x=, y=, z= in degrees (each 0 if missing).
bool CGameEntity::Obj_SetOrientation(CStruct* params, CScript* /*script*/)
{
    Scripts::VectorValue value = {0.0f, 0.0f, 1.0f, 1.0f};
    if (params->FetchVec(kDir, &value, 0)) {
        Maths::Mat4 m;
        m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
        m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (std::fabs(value.y) > kNearlyVertical)
            m.row[1] = {1.0f, 0.0f, 0.0f, 1.0f};
        m.row[2] = {value.x, value.y, value.z, 0.0f};
        m.row[2].MakeUnit();
        m.row[0] = Perpendicular(m.row[1], m.row[2]);
        m.row[0].MakeUnit();
        m.row[1] = Perpendicular(m.row[2], m.row[0]);
        m.row[1].MakeUnit();
        m_basisFrame = m;
        m_displayMatrix = m;
        return true;
    }
    if (params->FetchVec(kQuat, &value, 0)) {
        double sum = static_cast<double>(value.z) * value.z + static_cast<double>(value.y) * value.y;
        sum += static_cast<double>(value.x) * value.x;
        float quat[4] = {value.x, value.y, value.z, static_cast<float>(std::sqrt(1.0 - sum))};
        QuatToMatrix(quat, &m_basisFrame);
        m_displayMatrix = m_basisFrame;
        return true;
    }
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    params->FetchFloat(kX, &x, 0);
    params->FetchFloat(kY, &y, 0);
    params->FetchFloat(kZ, &z, 0);
    x *= kDegreesToRadians;
    y *= kDegreesToRadians;
    z *= kDegreesToRadians;
    Maths::Mat4 m;
    m.SetFromEulerAngles(x, y, z);
    m_basisFrame = m;
    m_displayMatrix = m;
    return true;
}

// 0x0045E940: quat = its orientation as a quaternion's x, y, z (negated when w < 0, so that w >= 0).
bool CGameEntity::Obj_GetQuat(CStruct* /*params*/, CScript* script)
{
    float quat[4];
    MatrixToQuat(quat, &m_basisFrame);
    if (quat[3] < 0.0f) {
        quat[0] = -quat[0];
        quat[1] = -quat[1];
        quat[2] = -quat[2];
    }
    script->m_args->PutVec3(kQuat, quat[0], quat[1], quat[2]);
    return true;
}

// 0x0045ED30: the nearest other object of `type` in its manager's list (the first one met when none is nearer
// than 5322672), or null.
CGameEntity* CGameEntity::FindNearestObjectOfType(uint32_t type)
{
    CObjectSearch search;
    search.m_vtable = g_objectSearchVtable;
    search.m_listLink = nullptr;
    auto* object = static_cast<CGameEntity*>(search.FirstItemOfType(ManagerObjectList(m_parkOwner), type));
    if (object == this)
        object = static_cast<CGameEntity*>(search.NextItemOfType());
    if (!object)
        return nullptr;
    CGameEntity* nearest = object;
    float nearestDistance = kFarAway;
    for (; object; object = static_cast<CGameEntity*>(search.NextItemOfType())) {
        double dx = static_cast<double>(object->m_atPoint.x) - m_atPoint.x;
        double dy = static_cast<double>(object->m_atPoint.y) - m_atPoint.y;
        double dz = static_cast<double>(object->m_atPoint.z) - m_atPoint.z;
        double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
        if (object != this && distance < nearestDistance) {
            nearestDistance = static_cast<float>(distance);
            nearest = object;
        }
    }
    return nearest;
}

// 0x0045EF30: whether the nearest object of type 0xBAF9B341 lies more than the unnamed angle (degrees, required)
// off its at axis, measured about its up axis. None: false.
bool CGameEntity::Obj_NearestOutsideAngle(CStruct* params, CScript* /*script*/)
{
    CGameEntity* other = FindNearestObjectOfType(kNearestType);
    if (!other)
        return false;
    float angle = 0.0f; // (uninitialised in the original: a missing angle compares with stack garbage)
    params->FetchFloat(kUnnamed, &angle, 1);
    angle *= kDegreesToRadians;
    Maths::Vec4 offset = {other->m_atPoint.x - m_atPoint.x, other->m_atPoint.y - m_atPoint.y, other->m_atPoint.z - m_atPoint.z,
                          other->m_atPoint.w - m_atPoint.w};
    return std::fabs(Maths::GetAngleInPlane(m_basisFrame, offset, 2, 1)) > angle;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kComponentVtable_slots[] = {&GameObjects::CObjectModule::Teardown,
         &_purecall,
         &_purecall,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kComponentVtable = reinterpret_cast<void**>(kComponentVtable_slots);
static VtableSlot kCompositeObjectVtable_slots[] = {&GameObjects::CGameEntity::Teardown,
         &GameObjects::CEntity::ChangeTraits,
         static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
         static_cast<bool (GameObjects::CGameEntity::*)(uint32_t, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CGameEntity::InvokeMethodByName),
         Stubs::IgnoreArgument,
         &GameObjects::CEntity::PassEventToScript,
         &GameObjects::CGameEntity::FlagAsDead,
         &GameObjects::CGameEntity::ShouldUpdateScripts,
         &GameObjects::CGameEntity::Seal,
         &GameObjects::CGameEntity::CreateComponentFromStructure,
         Stubs::DoNothing};
void** const kCompositeObjectVtable = reinterpret_cast<void**>(kCompositeObjectVtable_slots);

} // namespace
} // namespace Obj
