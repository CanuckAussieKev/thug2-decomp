// Obj::CCompositeObject - the object made of components (skaters, peds, cars, level objects) - and the base of its
// components, Obj::CBaseComponent (object.h). From the disassembly of 0x0045D720-0x0045EE20 (methods __thiscall,
// RET n = 4 * arguments; the member functions scripts call are entries of the table at 0x00683268, filled in by
// 0x006425D0, all `bool (CCompositeObject::*)(CStruct* params, CScript* script)`).

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/mth/quat.h"
#include "game/nx/feeler.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/objman.h"

namespace Obj {

float g_pi{3.1415927f};

using Script::CArray;
using Script::CMemberFunctionEntry;
using Script::CScript;
using Script::CStruct;

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
void RegisterComponent(CBaseComponent* component) { CompositeObjectManager()->AddComponentByType(component); }
void UnregisterComponent(CBaseComponent* component) { CompositeObjectManager()->RemoveComponentByType(component); }
CBaseComponent* CreateComponent(uint32_t type) { return CompositeObjectManager()->CreateComponent(type); }
// The position (pos=) / orientation in a structure, if it has them (SkateScript, nodes.h).
void GetPositionFromStructure(CStruct* params, Mth::Vector* pos) { SkateScript::GetPosition(params, pos); }
void GetOrientationFromStructure(CStruct* params, Mth::Matrix* matrix) { SkateScript::GetOrientation(params, matrix); }

// The node named `name` (SkateScript).
CStruct* GetNode(uint32_t name, int assert) { return SkateScript::GetNode(name, assert != 0); }
// Quaternions (x, y, z, w): to a rotation matrix (0x0042BC40, on the quaternion), from one (0x0042E1F0, constructing
// the quaternion).
void QuatToMatrix(const float* quat, Mth::Matrix* matrix) { reinterpret_cast<const Mth::Quat*>(quat)->GetMatrix(matrix); }
void MatrixToQuat(float* quat, const Mth::Matrix* matrix) { reinterpret_cast<Mth::Quat*>(quat)->FromMatrix(*matrix); }
// A manager's object list (its vtable +0x18).
Mem::ListNode* ManagerObjectList(CBaseManager* manager)
{
    return reinterpret_cast<Mem::ListNode*(__thiscall*)(CBaseManager*)>(manager->m_vtable[6])(manager);
}
// Mth::FastACos (0x00403EF0) as its callers here see it: the x87 result is used before it is rounded to a float.
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
Mth::Vector Cross(const Mth::Vector& a, const Mth::Vector& b)
{
    return {static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y),
            static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z),
            static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x), 0.0f};
}

// The object's vtable slot 3 (a member function by name).
bool VirtualCallMemberFunction(CObject* object, uint32_t name, CStruct* params, CScript* script)
{
    return reinterpret_cast<bool(__thiscall*)(CObject*, uint32_t, CStruct*, CScript*)>(object->m_vtable[3])(
        object, name, params, script);
}

// |v| as the original's x87 code works it out: (x*x + y*y) + z*z in double precision, not rounded to a float.
double Length(float x, float y, float z)
{
    double x2 = static_cast<double>(x) * x;
    double y2 = static_cast<double>(y) * y;
    double z2 = static_cast<double>(z) * z;
    return std::sqrt(x2 + y2 + z2);
}

// pos += row * scale, as the original rounds it: the x and y products stay in double precision, the z and w ones
// are rounded to floats first.
void AddScaled(Mth::Vector& pos, const Mth::Vector& row, float scale)
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
    CollisionFeeler() { reinterpret_cast<Nx::CFeeler*>(m_bytes)->Construct(); }
    ~CollisionFeeler()
    {
        reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>*>(m_bytes + 0x98)->~CSmtPtr();
    }
    CollisionFeeler(const CollisionFeeler&) = delete;
    CollisionFeeler& operator=(const CollisionFeeler&) = delete;
    Mth::Vector& Start() { return *reinterpret_cast<Mth::Vector*>(m_bytes); }
    Mth::Vector& End() { return *reinterpret_cast<Mth::Vector*>(m_bytes + 0x10); }
    bool GetCollision(bool a, bool b)
    {
        return reinterpret_cast<Nx::CFeeler*>(m_bytes)->GetCollision(a, b) != 0;
    }

private:
    alignas(16) uint8_t m_bytes[0xB0];
};

float FeetToInches() { return g_compositeFeetToInches; } // 12

} // namespace

// --- CObject: a member function table entry -------------------------------------------------------------------

// 0x0045D850
bool CObject::CallMemberFunction(CMemberFunctionEntry* entry, CStruct* params, CScript* script)
{
    if (entry->mCode) {
        void* self = reinterpret_cast<uint8_t*>(this) + entry->mThisAdjustment;
        return reinterpret_cast<bool(__thiscall*)(void*, CStruct*, CScript*)>(entry->mCode)(self, params, script);
    }
    return VirtualCallMemberFunction(this, entry->mName, params, script);
}

// --- CBaseComponent -------------------------------------------------------------------------------------------

// 0x0045D730
CBaseComponent* CBaseComponent::Construct()
{
    m_vtable = kComponentVtable;
    m_flags = 0;
    return this;
}

// 0x0045D740
void CBaseComponent::DestroyBody()
{
    m_vtable = kComponentVtable;
    if (m_flags & COMPONENT_FLAG_SCRIPT_WAITING)
        Script::ComponentDestroyedAllScripts(this);
}

// 0x0045D780
CBaseComponent* CBaseComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0045D760
void CBaseComponent::Suspend(bool suspend)
{
    if (suspend)
        m_flags |= COMPONENT_FLAG_SUSPENDED;
    else
        m_flags &= ~COMPONENT_FLAG_SUSPENDED;
}

// 0x0045D720 (a jump to vtable +0x30 with the entry's name in place of the entry)
int CBaseComponent::CallMemberFunction(CMemberFunctionEntry* entry, CStruct* params, CScript* script)
{
    return CallMemberFunction(entry->mName, params, script);
}

// --- CCompositeObject: life -----------------------------------------------------------------------------------

// 0x0045E510
CCompositeObject* CCompositeObject::Construct()
{
    CObject::Construct();
    m_vtable = kCompositeObjectVtable;
    m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
    m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
    m_matrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_matrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_displayMatrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_displayMatrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_displayMatrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_displayMatrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    mp_components = nullptr;
    m_compositeFlags = 0;
    m_unknownBC = 0;
    m_objectFlags |= OBJECT_FLAG_HAS_POSITION;
    return this;
}

// 0x0045D890
void CCompositeObject::DestroyBody()
{
    m_vtable = kCompositeObjectVtable;
    m_compositeFlags |= COMPOSITE_FLAG_DESTROYING;
    while (mp_components) {
        CBaseComponent* component = mp_components;
        mp_components = component->mp_next;
        UnregisterComponent(component);
        component->VirtualDestroy(1);
    }
    CObject::DestroyBody();
}

// 0x0045E620
CCompositeObject* CCompositeObject::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// --- CCompositeObject: the frame ------------------------------------------------------------------------------

// 0x0045D920
void CCompositeObject::Update()
{
    if (m_compositeFlags & COMPOSITE_FLAG_PAUSED)
        return;
    if (!(m_compositeFlags & COMPOSITE_FLAG_SUSPENDED)) {
        if (m_script)
            m_script->Update();
        if (m_objectFlags & OBJECT_FLAG_DEAD)
            return;
        VirtualOwnUpdate();
    }
    for (CBaseComponent* component = mp_components; component; component = component->mp_next) {
        if (component->m_flags & COMPONENT_FLAG_SUSPENDED)
            continue;
        component->Update();
        if (m_objectFlags & OBJECT_FLAG_DEAD)
            break;
    }
    m_pos.w = 1.0f;
    m_compositeFlags &= ~COMPOSITE_FLAG_TELEPORTED;
}

// 0x0045D990
void CCompositeObject::Hide(bool hide)
{
    if (hide == ((m_compositeFlags & COMPOSITE_FLAG_HIDDEN) != 0))
        return;
    if (hide)
        m_compositeFlags |= COMPOSITE_FLAG_HIDDEN;
    else
        m_compositeFlags &= ~COMPOSITE_FLAG_HIDDEN;
    for (CBaseComponent* component = mp_components; component; component = component->mp_next)
        component->Hide(hide);
}

// 0x0045D9E0
void CCompositeObject::Suspend(bool suspend)
{
    if (suspend == ((m_compositeFlags & COMPOSITE_FLAG_SUSPENDED) != 0))
        return;
    if (suspend)
        m_compositeFlags |= COMPOSITE_FLAG_SUSPENDED;
    else
        m_compositeFlags &= ~COMPOSITE_FLAG_SUSPENDED;
    for (CBaseComponent* component = mp_components; component; component = component->mp_next)
        component->VirtualSuspend(suspend);
}

// 0x0045E4C0
void CCompositeObject::SetTeleported(bool teleportComponents)
{
    m_compositeFlags |= COMPOSITE_FLAG_TELEPORTED;
    if (teleportComponents) {
        for (CBaseComponent* component = mp_components; component; component = component->mp_next)
            component->Teleport();
    }
}

// 0x0045E500
bool CCompositeObject::ShouldUpdateScripts() { return !(m_compositeFlags & COMPOSITE_FLAG_PAUSED); }

// 0x0045D7C0
void CCompositeObject::MarkAsDead()
{
    for (CBaseComponent* component = mp_components; component; component = component->mp_next)
        component->ObjectMarkedDead();
    CObject::MarkAsDead();
}

// --- CCompositeObject: components -----------------------------------------------------------------------------

// 0x0045DA30 (the component's +0x10 isn't cleared: it is expected to be null already)
void CCompositeObject::AddComponent(CBaseComponent* component)
{
    if (!mp_components) {
        mp_components = component;
    } else {
        CBaseComponent* last = mp_components;
        while (last->mp_next)
            last = last->mp_next;
        last->mp_next = component;
    }
    component->m_object = this;
    RegisterComponent(component);
}

// 0x0045DA90
void CCompositeObject::CreateComponentsFromArray(CArray* components, CStruct* extra)
{
    uint32_t* elements = components->GetArrayPointer();
    int count = static_cast<int>(components->m_size);
    for (int i = 0; i < count; ++i)
        VirtualCreateComponentFromStructure(reinterpret_cast<CStruct*>(elements[i]), extra);
}

// 0x0045EE20. With no component= the type looked up is `params`' own address [quirk]; a type the manager can't
// make gives a null component, added all the same [bug: crash]; so does a failed allocation of the merged copy.
void CCompositeObject::CreateComponentFromStructure(CStruct* params, CStruct* extra)
{
    uint32_t type = reinterpret_cast<uint32_t>(params);
    params->GetChecksum(kComponent, &type, 1);
    CBaseComponent* component = CreateComponent(type);
    AddComponent(component);
    if (!extra) {
        component->InitFromStructure(params);
        return;
    }
    CStruct* memory = Script::NewStruct();
    CStruct* merged = memory ? memory->Init() : nullptr;
    merged->AppendStructure(params);
    merged->AppendStructure(extra);
    component->InitFromStructure(merged);
    if (merged) {
        merged->Clear();
        Script::DeleteStruct(merged);
    }
}

// 0x0045DAD0
void CCompositeObject::InitFromStructure(CStruct* params)
{
    uint32_t id = 0;
    if (params->GetChecksum(kName, &id, 0))
        SetID(id);
    GetPositionFromStructure(params, &m_pos);
    GetOrientationFromStructure(params, &m_matrix);
    if (params->ContainsFlag(kPermanent))
        m_objectFlags |= OBJECT_FLAG_LOCKED;
}

// 0x0045DB30
void CCompositeObject::Finalize()
{
    for (CBaseComponent* component = mp_components; component; component = component->mp_next)
        component->Finalize();
    m_compositeFlags |= COMPOSITE_FLAG_FINALIZED;
    m_compositeFlags |= COMPOSITE_FLAG_FINALIZED2;
    for (CBaseComponent* component = mp_components; component; component = component->mp_next)
        component->Teleport();
}

// 0x0045DB90
CBaseComponent* CCompositeObject::GetComponent(uint32_t type)
{
    for (CBaseComponent* component = mp_components; component; component = component->mp_next) {
        if (component->m_type == type)
            return component;
    }
    return nullptr;
}

// 0x0045E450
bool CCompositeObject::CallMemberFunction(uint32_t name, CStruct* params, CScript* script)
{
    for (CBaseComponent* component = mp_components; component; component = component->mp_next) {
        int result = component->CallMemberFunction(name, params, script);
        if (result == 0)
            return false;
        if (result == 1)
            return true;
    }
    return CObject::CallMemberFunction(name, params, script);
}

// --- CCompositeObject: the member functions scripts call ------------------------------------------------------

// 0x0045D7F0: the unnamed string (the details' title) is read for the debug output.
bool CCompositeObject::Obj_PrintDetails(CStruct* params, CScript* /*script*/)
{
    const char* text = reinterpret_cast<const char*>(params);
    params->GetString(kUnnamed, &text, 0);
    PrintObjectDetails(text);
    return true;
}

// 0x0045D810: a component from the parameters (CreateComponentFromStructure, through the vtable).
bool CCompositeObject::Obj_CreateComponent(CStruct* params, CScript* /*script*/)
{
    VirtualCreateComponentFromStructure(params, nullptr);
    return true;
}

// 0x0045D830: position= (if given) as the position.
bool CCompositeObject::Obj_SetPosition(CStruct* params, CScript* /*script*/)
{
    params->GetVector(kPosition, reinterpret_cast<Script::VectorValue*>(&m_pos), 0);
    return true;
}

// 0x0045DBC0: pos = the position, in the calling script's parameters.
bool CCompositeObject::Obj_GetPosition(CStruct* /*params*/, CScript* script)
{
    script->m_params->AddVector(kPos, m_pos.x, m_pos.y, m_pos.z);
    return true;
}

// 0x0045DBF0: whether it has a component of the type name= names (required).
bool CCompositeObject::Obj_ComponentExists(CStruct* params, CScript* /*script*/)
{
    uint32_t type = 0;
    params->GetChecksum(kName, &type, 1);
    return GetComponent(type) != nullptr;
}

// 0x0045DC40: an extra update now.
bool CCompositeObject::Obj_ForceUpdate(CStruct* /*params*/, CScript* /*script*/)
{
    Update();
    return true;
}

// 0x0045DC50: vel = the velocity.
bool CCompositeObject::Obj_GetVelocity(CStruct* /*params*/, CScript* script)
{
    script->m_params->AddVector(kVel, m_vel.x, m_vel.y, m_vel.z);
    return true;
}

// 0x0045DC80: speed = the velocity's length.
bool CCompositeObject::GetSpeed(CStruct* /*params*/, CScript* script)
{
    script->m_params->AddFloat(kSpeed, static_cast<float>(Length(m_vel.x, m_vel.y, m_vel.z)));
    return true;
}

// 0x0045DCD0 / 0x0045DD20 / 0x0045DD70
bool CCompositeObject::Obj_Hide(CStruct* /*params*/, CScript* /*script*/)
{
    Hide(true);
    return true;
}
bool CCompositeObject::Obj_Unhide(CStruct* /*params*/, CScript* /*script*/)
{
    Hide(false);
    return true;
}
bool CCompositeObject::Obj_IsHidden(CStruct* /*params*/, CScript* /*script*/)
{
    return (m_compositeFlags & COMPOSITE_FLAG_HIDDEN) != 0;
}

// 0x0045DD80: moves along its own axes by x=, y= and z= (each 0 if missing), all four components of each row.
bool CCompositeObject::Obj_MoveLocal(CStruct* params, CScript* /*script*/)
{
    float distance = 0.0f;
    params->GetFloat(kX, &distance, 0);
    AddScaled(m_pos, m_matrix.row[0], distance);
    distance = 0.0f;
    params->GetFloat(kY, &distance, 0);
    AddScaled(m_pos, m_matrix.row[1], distance);
    distance = 0.0f;
    params->GetFloat(kZ, &distance, 0);
    AddScaled(m_pos, m_matrix.row[2], distance);
    return true;
}

// 0x0045DED0 / 0x0045DF20
bool CCompositeObject::Obj_Suspend(CStruct* /*params*/, CScript* /*script*/)
{
    Suspend(true);
    return true;
}
bool CCompositeObject::Obj_Unsuspend(CStruct* /*params*/, CScript* /*script*/)
{
    Suspend(false);
    return true;
}

// 0x0045E0B0: the distance in feet to the object the unnamed checksum names (required; none: 0).
bool CCompositeObject::Obj_GetDistanceToObject(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kUnnamed, &name, 1);
    auto* other = static_cast<CCompositeObject*>(FindObject(name));
    float distance = 0.0f;
    if (other) {
        double dx = static_cast<double>(m_pos.x) - other->m_pos.x;
        double dy = static_cast<double>(m_pos.y) - other->m_pos.y;
        double dz = static_cast<double>(m_pos.z) - other->m_pos.z;
        distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx) * kInchesToFeet);
    }
    script->m_params->AddFloat(kDistanceResult, distance);
    return true;
}

// 0x0045E260: whether the velocity points backwards (against its matrix's at row).
bool CCompositeObject::Obj_MovingBackward(CStruct* /*params*/, CScript* /*script*/)
{
    const Mth::Vector& at = m_matrix.row[2];
    double along = static_cast<double>(m_vel.z) * at.z + static_cast<double>(m_vel.y) * at.y;
    along += static_cast<double>(m_vel.x) * at.x;
    return along < 0.0;
}

// 0x0045E2A0: whether the speed is within 0.1 of the unnamed float (0 if missing).
bool CCompositeObject::Obj_SpeedNear(CStruct* params, CScript* /*script*/)
{
    float speed = 0.0f;
    params->GetFloat(kUnnamed, &speed, 0);
    return std::fabs(Length(m_vel.x, m_vel.y, m_vel.z) - speed) < kSpeedTolerance;
}

// 0x0045E310 / 0x0045E380: whether the speed is above / below the unnamed float (0 if missing).
bool CCompositeObject::Obj_SpeedAbove(CStruct* params, CScript* /*script*/)
{
    float speed = 0.0f;
    params->GetFloat(kUnnamed, &speed, 0);
    return Length(m_vel.x, m_vel.y, m_vel.z) > speed;
}
bool CCompositeObject::Obj_SpeedBelow(CStruct* params, CScript* /*script*/)
{
    float speed = 0.0f;
    params->GetFloat(kUnnamed, &speed, 0);
    return Length(m_vel.x, m_vel.y, m_vel.z) < speed;
}

// 0x0045E3F0: its type, in the calling script's parameters.
bool CCompositeObject::Obj_GetType(CStruct* /*params*/, CScript* script)
{
    script->m_params->AddChecksum(kTypeResult, m_type);
    return true;
}

// 0x0045E410: whether it has a component of the type the unnamed checksum names. With none, the type looked for
// is `params`' own address [quirk].
bool CCompositeObject::Obj_HasComponent(CStruct* params, CScript* /*script*/)
{
    uint32_t type = reinterpret_cast<uint32_t>(params);
    params->GetChecksum(kUnnamed, &type, 1);
    return GetComponent(type) != nullptr;
}

// 0x0045E9E0
bool CCompositeObject::Obj_GetCollision(CStruct* params, CScript* /*script*/)
{
    CollisionFeeler feeler;
    float length = 3.0f;
    params->GetFloat(kLength, &length, 0);
    length *= FeetToInches();
    float height = 3.0f;
    params->GetFloat(kHeight, &height, 0);
    height *= FeetToInches();
    float distance = 0.0f;
    params->GetFloat(kDistance, &distance, 0);
    distance *= FeetToInches();
    float shift = 0.0f;
    params->GetFloat(kShift, &shift, 0);
    shift *= FeetToInches();
    const Mth::Vector& right = m_matrix.row[0];
    const Mth::Vector& up = m_matrix.row[1];
    const Mth::Vector& at = m_matrix.row[2];
    // The start: pos + up * height + at * distance + right * shift (x87: some sums kept at 53 bits, as below).
    float shiftX = shift * right.x, shiftY = shift * right.y, shiftZ = shift * right.z;
    double shiftW = static_cast<double>(shift) * right.w;
    float atX = distance * at.x, atY = distance * at.y;
    double atZ = static_cast<double>(distance) * at.z, atW = static_cast<double>(distance) * at.w;
    float upX = height * up.x, upY = height * up.y;
    double upZ = static_cast<double>(height) * up.z, upW = static_cast<double>(height) * up.w;
    float w1 = static_cast<float>(upW + m_pos.w);
    double z1 = upZ + m_pos.z;
    float y1 = upY + m_pos.y;
    float x1 = upX + m_pos.x;
    w1 = static_cast<float>(w1 + atW);
    float z2 = static_cast<float>(z1 + atZ);
    y1 = y1 + atY;
    x1 = x1 + atX;
    Mth::Vector start = {x1 + shiftX, y1 + shiftY, z2 + shiftZ, static_cast<float>(shiftW + w1)};
    feeler.Start() = start;
    Mth::Vector& end = feeler.End();
    end = start;
    // The end: `length` along the right (side), down (down) or ahead.
    if (params->ContainsFlag(kSide)) {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>(static_cast<double>(length) * (&right.x)[i] + (&end.x)[i]);
    } else if (params->ContainsFlag(kDown)) {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>((&end.x)[i] - static_cast<double>(length) * (&up.x)[i]);
    } else {
        for (int i = 0; i < 4; ++i)
            (&end.x)[i] = static_cast<float>(static_cast<double>(length) * (&at.x)[i] + (&end.x)[i]);
    }
    return feeler.GetCollision(true, false);
}

// 0x0045E9B0 / 0x0045E9D0
bool CCompositeObject::Obj_Pause(CStruct* /*params*/, CScript* /*script*/)
{
    m_compositeFlags |= COMPOSITE_FLAG_PAUSED;
    return true;
}
bool CCompositeObject::Obj_Unpause(CStruct* /*params*/, CScript* /*script*/)
{
    m_compositeFlags &= ~COMPOSITE_FLAG_PAUSED;
    return true;
}

// 0x0045DF70: x, y, z = its display matrix's at row, made unit length (left as it is if its length is 0).
bool CCompositeObject::Obj_GetOrientation(CStruct* /*params*/, CScript* script)
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
    script->m_params->AddFloat(kX, x);
    script->m_params->AddFloat(kY, y);
    script->m_params->AddFloat(kZ, z);
    return true;
}

// 0x0045E020: dist = the distance to the node the unnamed checksum names (none: 0).
bool CCompositeObject::Obj_GetDistToNode(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kUnnamed, &name, 0);
    Mth::Vector nodePos;
    GetPositionFromStructure(GetNode(name, 1), &nodePos);
    double dx = static_cast<double>(m_pos.x) - nodePos.x;
    double dy = static_cast<double>(m_pos.y) - nodePos.y;
    double dz = static_cast<double>(m_pos.z) - nodePos.z;
    script->m_params->AddFloat(kDist, static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx)));
    return true;
}

// 0x0045E140: dotprod = how much the object the unnamed checksum names (required) lies along its right axis (the
// cosine: both made unit length), and the angle that makes in degrees, less 90; none: both 0.
bool CCompositeObject::Obj_GetOrientationToObject(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kUnnamed, &name, 1);
    auto* other = static_cast<CCompositeObject*>(FindObject(name));
    float dot = 0.0f;
    float angle = 0.0f;
    if (other) {
        Mth::Vector right = {m_matrix.row[0].x, m_matrix.row[0].y, m_matrix.row[0].z, 0.0f};
        right.Normalize();
        Mth::Vector toOther = {other->m_pos.x - m_pos.x, other->m_pos.y - m_pos.y, other->m_pos.z - m_pos.z, 0.0f};
        toOther.Normalize();
        double sum = static_cast<double>(toOther.z) * right.z + static_cast<double>(toOther.y) * right.y;
        dot = static_cast<float>(sum + static_cast<double>(toOther.x) * right.x);
        angle = static_cast<float>(FastACosUnrounded(dot) * kRadiansToDegrees - kRightAngle);
    }
    script->m_params->AddFloat(kDotProd, dot);
    script->m_params->AddFloat(kAngleResult, angle);
    return true;
}

// 0x0045E650: a new orientation (also as the display matrix): dir= (looking along it, the up axis kept as near
// world up as it can be: world x when dir is within 0.99 of vertical), else quat= (x, y, z; w from them), else
// the angles x=, y=, z= in degrees (each 0 if missing).
bool CCompositeObject::Obj_SetOrientation(CStruct* params, CScript* /*script*/)
{
    Script::VectorValue value = {0.0f, 0.0f, 1.0f, 1.0f};
    if (params->GetVector(kDir, &value, 0)) {
        Mth::Matrix m;
        m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
        m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (std::fabs(value.y) > kNearlyVertical)
            m.row[1] = {1.0f, 0.0f, 0.0f, 1.0f};
        m.row[2] = {value.x, value.y, value.z, 0.0f};
        m.row[2].Normalize();
        m.row[0] = Cross(m.row[1], m.row[2]);
        m.row[0].Normalize();
        m.row[1] = Cross(m.row[2], m.row[0]);
        m.row[1].Normalize();
        m_matrix = m;
        m_displayMatrix = m;
        return true;
    }
    if (params->GetVector(kQuat, &value, 0)) {
        double sum = static_cast<double>(value.z) * value.z + static_cast<double>(value.y) * value.y;
        sum += static_cast<double>(value.x) * value.x;
        float quat[4] = {value.x, value.y, value.z, static_cast<float>(std::sqrt(1.0 - sum))};
        QuatToMatrix(quat, &m_matrix);
        m_displayMatrix = m_matrix;
        return true;
    }
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    params->GetFloat(kX, &x, 0);
    params->GetFloat(kY, &y, 0);
    params->GetFloat(kZ, &z, 0);
    x *= kDegreesToRadians;
    y *= kDegreesToRadians;
    z *= kDegreesToRadians;
    Mth::Matrix m;
    m.SetFromEulerAngles(x, y, z);
    m_matrix = m;
    m_displayMatrix = m;
    return true;
}

// 0x0045E940: quat = its orientation as a quaternion's x, y, z (negated when w < 0, so that w >= 0).
bool CCompositeObject::Obj_GetQuat(CStruct* /*params*/, CScript* script)
{
    float quat[4];
    MatrixToQuat(quat, &m_matrix);
    if (quat[3] < 0.0f) {
        quat[0] = -quat[0];
        quat[1] = -quat[1];
        quat[2] = -quat[2];
    }
    script->m_params->AddVector(kQuat, quat[0], quat[1], quat[2]);
    return true;
}

// 0x0045ED30: the nearest other object of `type` in its manager's list (the first one met when none is nearer
// than 5322672), or null.
CCompositeObject* CCompositeObject::FindNearestObjectOfType(uint32_t type)
{
    CObjectSearch search;
    search.m_vtable = g_objectSearchVtable;
    search.m_node = nullptr;
    auto* object = static_cast<CCompositeObject*>(search.FirstItemOfType(ManagerObjectList(mp_manager), type));
    if (object == this)
        object = static_cast<CCompositeObject*>(search.NextItemOfType());
    if (!object)
        return nullptr;
    CCompositeObject* nearest = object;
    float nearestDistance = kFarAway;
    for (; object; object = static_cast<CCompositeObject*>(search.NextItemOfType())) {
        double dx = static_cast<double>(object->m_pos.x) - m_pos.x;
        double dy = static_cast<double>(object->m_pos.y) - m_pos.y;
        double dz = static_cast<double>(object->m_pos.z) - m_pos.z;
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
bool CCompositeObject::Obj_NearestOutsideAngle(CStruct* params, CScript* /*script*/)
{
    CCompositeObject* other = FindNearestObjectOfType(kNearestType);
    if (!other)
        return false;
    float angle = 0.0f; // (uninitialised in the original: a missing angle compares with stack garbage)
    params->GetFloat(kUnnamed, &angle, 1);
    angle *= kDegreesToRadians;
    Mth::Vector offset = {other->m_pos.x - m_pos.x, other->m_pos.y - m_pos.y, other->m_pos.z - m_pos.z,
                          other->m_pos.w - m_pos.w};
    return std::fabs(Mth::GetAngleInPlane(m_matrix, offset, 2, 1)) > angle;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kComponentVtable_slots[] = {&Obj::CBaseComponent::Destroy,
         &_purecall,
         &_purecall,
         Stubs::DoNothing,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kComponentVtable = reinterpret_cast<void**>(kComponentVtable_slots);
static VtableSlot kCompositeObjectVtable_slots[] = {&Obj::CCompositeObject::Destroy,
         &Obj::CObject::SetProperties,
         static_cast<bool (Obj::CObject::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CObject::CallMemberFunction),
         static_cast<bool (Obj::CCompositeObject::*)(uint32_t, Script::CStruct*, Script::CScript*)>(&Obj::CCompositeObject::CallMemberFunction),
         Stubs::IgnoreArgument,
         &Obj::CObject::PassEventToScript,
         &Obj::CCompositeObject::MarkAsDead,
         &Obj::CCompositeObject::ShouldUpdateScripts,
         &Obj::CCompositeObject::Finalize,
         &Obj::CCompositeObject::CreateComponentFromStructure,
         Stubs::DoNothing};
void** const kCompositeObjectVtable = reinterpret_cast<void**>(kCompositeObjectVtable_slots);

} // namespace
} // namespace Obj
