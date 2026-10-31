// The camera component (Obj::CCameraComponent): see camera_component.h. From the disassembly of
// 0x0041B6D0-0x0041B903.

#include "game/obj/camera_component.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

namespace {

extern void** const kVtable;
constexpr int kCameraPriority = -1000;

} // namespace

// 0x0041B840
CBaseComponent* __cdecl CCameraComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CCameraComponent));
    return memory ? static_cast<CCameraComponent*>(memory)->Construct() : nullptr;
}

// 0x0041B730
CCameraComponent* CCameraComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    void* memory = Mem::Calloc(sizeof(Gfx::Camera));
    mp_camera = memory ? static_cast<Gfx::Camera*>(memory)->Construct() : nullptr;
    return this;
}

// 0x0041B6D0
void CCameraComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_camera)
        reinterpret_cast<void*(__thiscall*)(Gfx::Camera*, unsigned int)>(mp_camera->m_vtable[0])(mp_camera, 1);
    CBaseComponent::DestroyBody();
}

// 0x0041B7C0
CCameraComponent* CCameraComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041B7F0
void CCameraComponent::InitFromStructure(Script::CStruct*)
{
    CBaseManager* manager = reinterpret_cast<CBaseManager*>(CompositeObjectManager());
    reinterpret_cast<void(__thiscall*)(CBaseManager*, CObject*, int)>(manager->m_vtable[4])(manager, m_object,
                                                                                         kCameraPriority);
}

// 0x0041B810
void CCameraComponent::Finalize()
{
    mp_camera->SetPos(m_object->m_pos);
    mp_camera->SetMatrix(m_object->m_matrix);
}

// 0x0041B8A0
void CCameraComponent::Update()
{
    Mth::Vector pos = m_object->m_pos;
    Mth::Matrix matrix;
    matrix = m_object->m_matrix;
    m_object->m_displayMatrix = matrix;
    mp_camera->SetPos(pos);
    mp_camera->SetMatrix(matrix);
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CCameraComponent::Destroy,
         &Obj::CCameraComponent::Update,
         &Obj::CCameraComponent::InitFromStructure,
         &Obj::CCameraComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
