// Obj::CCompositeObject - the object made of components (skaters, peds, cars, level objects) - and the base of its
// components, Obj::CBaseComponent (object.h). From the disassembly of 0x0045D720-0x0045EE20 (methods __thiscall,
// RET n = 4 * arguments; the member functions scripts call are entries of the table at 0x00683268, filled in by
// 0x006425D0, all `bool (CCompositeObject::*)(CStruct* params, CScript* script)`).

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

using Script::CArray;
using Script::CMemberFunctionEntry;
using Script::CScript;
using Script::CStruct;

namespace {

void** const kComponentVtable = reinterpret_cast<void**>(0x00647618);
void** const kCompositeObjectVtable = reinterpret_cast<void**>(0x00647650);

// Names (checksums).
constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kPermanent = 0x23627FD7;      // permanent
constexpr uint32_t kComponent = 0xB6015EA8;      // component
constexpr uint32_t kPos = 0x7F261953;            // pos
constexpr uint32_t kPosition = 0xB9D31B0A;       // position
constexpr uint32_t kVel = 0x0C4C809E;            // vel
constexpr uint32_t kSpeed = 0xF0D90109;          // speed
constexpr uint32_t kX = 0x7323E97C;              // x
constexpr uint32_t kY = 0x0424D9EA;              // y
constexpr uint32_t kZ = 0x9D2D8850;              // z
constexpr uint32_t kDistanceResult = 0x976B04A3; // (name unknown): Obj_GetDistanceToObject's result
constexpr uint32_t kTypeResult = 0x24495505;     // (name unknown): Obj_GetType's result

// The constants the original reads (0x006461F4: inches to feet; 0x00645D74).
constexpr float kInchesToFeet = 1.0f / 12.0f;
constexpr float kSpeedTolerance = 0.1f;

// The composite object manager (0x006F0FB8; wt/coim's range 0x0045EFD0-0x0045F7A0, not rewritten yet).
void* CompositeObjectManager() { return GameVariable<void*>(0x006F0FB8); }
// -> CCompositeObjectManager::RegisterComponent 0x0045F3E0 (wt/coim): onto the list of the components of its type.
void RegisterComponent(CBaseComponent* component)
{
    GameFunction<void(__thiscall*)(void*, CBaseComponent*)>(0x0045F3E0)(CompositeObjectManager(), component);
}
// -> CCompositeObjectManager::UnregisterComponent 0x0045F420 (wt/coim).
void UnregisterComponent(CBaseComponent* component)
{
    GameFunction<void(__thiscall*)(void*, CBaseComponent*)>(0x0045F420)(CompositeObjectManager(), component);
}
// -> CCompositeObjectManager::CreateComponent 0x0045F2A0 (wt/coim): a new component of `type`.
CBaseComponent* CreateComponent(uint32_t type)
{
    return GameFunction<CBaseComponent*(__thiscall*)(void*, uint32_t)>(0x0045F2A0)(CompositeObjectManager(), type);
}
// The position (pos=) / orientation in a structure, if it has them (cdecl; not rewritten yet).
void GetPositionFromStructure(CStruct* params, Mth::Vector* pos)
{
    GameFunction<void(__cdecl*)(CStruct*, Mth::Vector*)>(0x0045C050)(params, pos);
}
void GetOrientationFromStructure(CStruct* params, Mth::Matrix* matrix)
{
    GameFunction<void(__cdecl*)(CStruct*, Mth::Matrix*)>(0x0045C0B0)(params, matrix);
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

} // namespace

// --- CObject: a member function table entry -------------------------------------------------------------------

// 0x0045D850
bool CObject::CallMemberFunction(CMemberFunctionEntry* entry, CStruct* params, CScript* script)
{
    if (entry->mpCode) {
        void* self = reinterpret_cast<uint8_t*>(this) + entry->mThisAdjustment;
        return reinterpret_cast<bool(__thiscall*)(void*, CStruct*, CScript*)>(entry->mpCode)(self, params, script);
    }
    return VirtualCallMemberFunction(this, entry->unknown0, params, script);
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
    return CallMemberFunction(entry->unknown0, params, script);
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

} // namespace Obj

REPLACE(0x0045D850, static_cast<bool (Obj::CObject::*)(Script::CMemberFunctionEntry*, Script::CStruct*,
                                                       Script::CScript*)>(&Obj::CObject::CallMemberFunction));
REPLACE(0x0045D730, &Obj::CBaseComponent::Construct);
REPLACE(0x0045D740, &Obj::CBaseComponent::DestroyBody);
REPLACE(0x0045D780, &Obj::CBaseComponent::Destroy);
REPLACE(0x0045D760, &Obj::CBaseComponent::Suspend);
REPLACE(0x0045D720, static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*,
                                                            Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction));
REPLACE(0x0045E510, &Obj::CCompositeObject::Construct);
REPLACE(0x0045D890, &Obj::CCompositeObject::DestroyBody);
REPLACE(0x0045E620, &Obj::CCompositeObject::Destroy);
REPLACE(0x0045D920, &Obj::CCompositeObject::Update);
REPLACE(0x0045D990, &Obj::CCompositeObject::Hide);
REPLACE(0x0045D9E0, &Obj::CCompositeObject::Suspend);
REPLACE(0x0045E4C0, &Obj::CCompositeObject::SetTeleported);
REPLACE(0x0045E500, &Obj::CCompositeObject::ShouldUpdateScripts);
REPLACE(0x0045D7C0, &Obj::CCompositeObject::MarkAsDead);
REPLACE(0x0045DA30, &Obj::CCompositeObject::AddComponent);
REPLACE(0x0045DA90, &Obj::CCompositeObject::CreateComponentsFromArray);
REPLACE(0x0045EE20, &Obj::CCompositeObject::CreateComponentFromStructure);
REPLACE(0x0045DAD0, &Obj::CCompositeObject::InitFromStructure);
REPLACE(0x0045DB30, &Obj::CCompositeObject::Finalize);
REPLACE(0x0045DB90, &Obj::CCompositeObject::GetComponent);
REPLACE(0x0045E450, static_cast<bool (Obj::CCompositeObject::*)(uint32_t, Script::CStruct*, Script::CScript*)>(
                        &Obj::CCompositeObject::CallMemberFunction));
REPLACE(0x0045D7F0, &Obj::CCompositeObject::Obj_PrintDetails);
REPLACE(0x0045D810, &Obj::CCompositeObject::Obj_CreateComponent);
REPLACE(0x0045D830, &Obj::CCompositeObject::Obj_SetPosition);
REPLACE(0x0045DBC0, &Obj::CCompositeObject::Obj_GetPosition);
REPLACE(0x0045DBF0, &Obj::CCompositeObject::Obj_ComponentExists);
REPLACE(0x0045DC40, &Obj::CCompositeObject::Obj_ForceUpdate);
REPLACE(0x0045DC50, &Obj::CCompositeObject::Obj_GetVelocity);
REPLACE(0x0045DC80, &Obj::CCompositeObject::GetSpeed);
REPLACE(0x0045DCD0, &Obj::CCompositeObject::Obj_Hide);
REPLACE(0x0045DD20, &Obj::CCompositeObject::Obj_Unhide);
REPLACE(0x0045DD70, &Obj::CCompositeObject::Obj_IsHidden);
REPLACE(0x0045DD80, &Obj::CCompositeObject::Obj_MoveLocal);
REPLACE(0x0045DED0, &Obj::CCompositeObject::Obj_Suspend);
REPLACE(0x0045DF20, &Obj::CCompositeObject::Obj_Unsuspend);
REPLACE(0x0045E0B0, &Obj::CCompositeObject::Obj_GetDistanceToObject);
REPLACE(0x0045E260, &Obj::CCompositeObject::Obj_MovingBackward);
REPLACE(0x0045E2A0, &Obj::CCompositeObject::Obj_SpeedNear);
REPLACE(0x0045E310, &Obj::CCompositeObject::Obj_SpeedAbove);
REPLACE(0x0045E380, &Obj::CCompositeObject::Obj_SpeedBelow);
REPLACE(0x0045E3F0, &Obj::CCompositeObject::Obj_GetType);
REPLACE(0x0045E410, &Obj::CCompositeObject::Obj_HasComponent);
REPLACE(0x0045E9B0, &Obj::CCompositeObject::Obj_Pause);
REPLACE(0x0045E9D0, &Obj::CCompositeObject::Obj_Unpause);
