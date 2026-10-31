// The camera component (GameObjects::CViewpointModule): see camera_component.h. From the disassembly of
// 0x0041B6D0-0x0041B903.

#include "game/obj/camera_component.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/view_cam.h"
#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity_hub.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr int kCameraPriority = -1000;

} // namespace

// 0x0041B840
CObjectModule* __cdecl CViewpointModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CViewpointModule));
    return memory ? static_cast<CViewpointModule*>(memory)->MakeNew() : nullptr;
}

// 0x0041B730
CViewpointModule* CViewpointModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kType;
    void* memory = MemSys::AllocZeroed(sizeof(Gfx2D::ViewCam));
    m_viewCamera = memory ? static_cast<Gfx2D::ViewCam*>(memory)->MakeNew() : nullptr;
    return this;
}

// 0x0041B6D0
void CViewpointModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_viewCamera)
        reinterpret_cast<void*(__thiscall*)(Gfx2D::ViewCam*, unsigned int)>(m_viewCamera->m_vtable[0])(m_viewCamera, 1);
    CObjectModule::DestroyBody();
}

// 0x0041B7C0
CViewpointModule* CViewpointModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041B7F0
void CViewpointModule::ConfigureFromData(Scripts::CStruct*)
{
    CObjectHubBase* manager = reinterpret_cast<CObjectHubBase*>(CompositeObjectManager());
    reinterpret_cast<void(__thiscall*)(CObjectHubBase*, CEntity*, int)>(manager->m_vtable[4])(manager, m_entity,
                                                                                         kCameraPriority);
}

// 0x0041B810
void CViewpointModule::Seal()
{
    m_viewCamera->ChangeLocation(m_entity->m_atPoint);
    m_viewCamera->ChangeMat(m_entity->m_basisFrame);
}

// 0x0041B8A0
void CViewpointModule::TickFrame()
{
    Maths::Vec4 pos = m_entity->m_atPoint;
    Maths::Mat4 matrix;
    matrix = m_entity->m_basisFrame;
    m_entity->m_displayMatrix = matrix;
    m_viewCamera->ChangeLocation(pos);
    m_viewCamera->ChangeMat(matrix);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CViewpointModule::Teardown,
         &GameObjects::CViewpointModule::TickFrame,
         &GameObjects::CViewpointModule::ConfigureFromData,
         &GameObjects::CViewpointModule::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
