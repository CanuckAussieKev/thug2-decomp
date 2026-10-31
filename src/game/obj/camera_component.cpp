// The camera component (GameObjects::CViewpointModule): see camera_component.h. From the disassembly of
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

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr int kCameraPriority = -1000;

} // namespace

// 0x0041B840
CObjectModule* __cdecl CViewpointModule::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CViewpointModule));
    return memory ? static_cast<CViewpointModule*>(memory)->Construct() : nullptr;
}

// 0x0041B730
CViewpointModule* CViewpointModule::Construct()
{
    CObjectModule::Construct();
    m_vtable = kVtable;
    m_type = kType;
    void* memory = MemSys::AllocZeroed(sizeof(Gfx2D::Camera));
    m_viewCamera = memory ? static_cast<Gfx2D::Camera*>(memory)->Construct() : nullptr;
    return this;
}

// 0x0041B6D0
void CViewpointModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_viewCamera)
        reinterpret_cast<void*(__thiscall*)(Gfx2D::Camera*, unsigned int)>(m_viewCamera->m_vtable[0])(m_viewCamera, 1);
    CObjectModule::DestroyBody();
}

// 0x0041B7C0
CViewpointModule* CViewpointModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0041B7F0
void CViewpointModule::ConfigureFromData(Scripts::CStruct*)
{
    CObjectHubBase* manager = reinterpret_cast<CObjectHubBase*>(CompositeObjectManager());
    reinterpret_cast<void(__thiscall*)(CObjectHubBase*, CObject*, int)>(manager->m_vtable[4])(manager, m_object,
                                                                                         kCameraPriority);
}

// 0x0041B810
void CViewpointModule::Finalize()
{
    m_viewCamera->SetPos(m_object->m_pos);
    m_viewCamera->SetMatrix(m_object->m_basisFrame);
}

// 0x0041B8A0
void CViewpointModule::Update()
{
    Maths::Vector pos = m_object->m_pos;
    Maths::Matrix matrix;
    matrix = m_object->m_basisFrame;
    m_object->m_displayMatrix = matrix;
    m_viewCamera->SetPos(pos);
    m_viewCamera->SetMatrix(matrix);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CViewpointModule::Destroy,
         &GameObjects::CViewpointModule::Update,
         &GameObjects::CViewpointModule::ConfigureFromData,
         &GameObjects::CViewpointModule::Finalize,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
