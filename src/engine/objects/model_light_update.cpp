// The model light update component (GameObjects::CMeshLightingModule): see model_light_update.h. From the disassembly of
// 0x00422D00-0x00423127. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "engine/objects/model_light_update.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/model_component_nx.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kOnce = 0xE7375C4C;     // (its name isn't in THUG2's scripts)
constexpr uint32_t kInterval = 0x470B8AE9; // (nor this one's)
constexpr uint32_t kModel = 0x286A8D26;

constexpr float kUpNudge = 0.001f;       // 0x006461D8: a thousandth of the object's up vector
constexpr float kAbove = 8.0f;           // 0x006461D4: the first feeler starts this far above
constexpr float kDepth = 4800.0f;        // 0x006461D0: and goes this far down
constexpr float kSecondAbove = 48.0f;    // 0x006461CC: the second starts this far above the first's start
constexpr float kLerpRate = 0.02f;       // 0x3CA3D70A

} // namespace

// 0x00422DC0
CObjectModule* __cdecl CMeshLightingModule::Fabricate()
{
    auto* component = static_cast<CMeshLightingModule*>(MemSys::AllocZeroed(sizeof(CMeshLightingModule)));
    if (!component)
        return nullptr;
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_lightFlags = 0;
    component->m_gapTime = 0;
    component->m_counter = 0;
    component->m_brightness = 0.0f;
    component->m_kindTag = kType;
    return component;
}

// 0x00422D00
void CMeshLightingModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00422D10
CMeshLightingModule* CMeshLightingModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00422D40
void CMeshLightingModule::ConfigureFromData(Scripts::CStruct* params)
{
    if (params->HasBareFlag(kOnce))
        m_lightFlags |= MODEL_LIGHT_ONCE;
    else
        m_lightFlags &= ~MODEL_LIGHT_ONCE;
    m_gapTime = 0;
    m_counter = 0;
    if (params->FetchIntValue(kInterval, reinterpret_cast<int*>(&m_gapTime), 0))
        m_counter = reinterpret_cast<uint32_t>(this) % m_gapTime + 1;
}

// 0x00422DA0
void CMeshLightingModule::Seal()
{
    m_meshInstance = static_cast<CMeshInstanceModule*>(m_entity->FetchModule(kModel));
}

// 0x00422E40
void CMeshLightingModule::Sample(bool lerp, float rate)
{
    // Down from just above the object (nudged along its up vector).
    Engine3D::CProbeRay down;
    down.MakeNew();
    CGameEntity* object = m_entity;
    Maths::Vec4& start = down.m_line.start;
    Maths::Vec4& end = down.m_line.end;
    start = object->m_atPoint;
    end = object->m_atPoint;
    const Maths::Vec4& up = object->m_basisFrame.row[1];
    double stepX = static_cast<double>(up.x) * kUpNudge;
    double stepY = static_cast<double>(up.y) * kUpNudge;
    double stepZ = static_cast<double>(up.z) * kUpNudge;
    double stepW = static_cast<double>(up.w) * kUpNudge;
    start.x = static_cast<float>(stepX + start.x);
    start.y = static_cast<float>(start.y + stepY);
    start.z = static_cast<float>(start.z + stepZ);
    start.w = static_cast<float>(start.w + stepW);
    start.y = start.y + kAbove;
    end.y = end.y - kDepth;
    bool hit = down.GetHitShape(false, false) != 0;
    // Again from higher up, to just under what it hit (or as far down), faces flagged 0x20 only.
    Engine3D::CProbeRay again;
    again.MakeNew();
    again.SetExcluded(0, 0x20);
    again.m_line.start = {start.x, start.y + kSecondAbove, start.z, start.w};
    if (hit)
        again.m_line.end = {down.m_contactPos.x, down.m_contactPos.y - 1.0f, down.m_contactPos.z, down.m_contactPos.w};
    else
        again.m_line.end = {start.x, start.y - kDepth, start.z, start.w};
    if (again.GetHitShape(false, false)) {
        m_meshInstance->ApplyBrightness(&again, lerp, rate);
        m_brightness = static_cast<float>(again.GetHitVertexValue());
    } else if (hit) {
        m_meshInstance->ApplyBrightness(&down, lerp, 1.0f);
        m_brightness = static_cast<float>(down.GetHitVertexValue());
    }
    again.Destruct();
    down.Destruct();
}

// 0x004230B0
void CMeshLightingModule::TickFrame()
{
    if (!(m_lightFlags & MODEL_LIGHT_SAMPLED)) {
        Sample(false, 1.0f);
        m_lightFlags |= MODEL_LIGHT_SAMPLED;
        return;
    }
    if (m_gapTime && m_counter++ < m_gapTime) {
        static_cast<Engine3D::CModelLights*>(ModelNx::ModelLights(m_meshInstance->m_meshInstance))->MoveBrightness(m_brightness, kLerpRate);
        return;
    }
    m_counter = 0;
    if (m_lightFlags & MODEL_LIGHT_ONCE)
        VirtualSuspend(true);
    else
        Sample(true, 1.0f);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CMeshLightingModule::Teardown,
         &GameObjects::CMeshLightingModule::TickFrame,
         &GameObjects::CMeshLightingModule::ConfigureFromData,
         &GameObjects::CMeshLightingModule::Seal,
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
