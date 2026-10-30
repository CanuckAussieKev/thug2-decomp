// The model light update component (Obj::CModelLightUpdateComponent): see model_light_update.h. From the disassembly of
// 0x00422D00-0x00423127. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "game/obj/model_light_update.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_model_lights.h"
#include "game/nx/feeler.h"
#include "game/obj/model_component.h"
#include "game/obj/model_component_nx.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

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
CBaseComponent* __cdecl CModelLightUpdateComponent::Create()
{
    auto* component = static_cast<CModelLightUpdateComponent*>(Mem::Calloc(sizeof(CModelLightUpdateComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_lightFlags = 0;
    component->m_interval = 0;
    component->m_counter = 0;
    component->m_brightness = 0.0f;
    component->m_type = kType;
    return component;
}

// 0x00422D00
void CModelLightUpdateComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00422D10
CModelLightUpdateComponent* CModelLightUpdateComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00422D40
void CModelLightUpdateComponent::InitFromStructure(Script::CStruct* params)
{
    if (params->ContainsFlag(kOnce))
        m_lightFlags |= MODEL_LIGHT_ONCE;
    else
        m_lightFlags &= ~MODEL_LIGHT_ONCE;
    m_interval = 0;
    m_counter = 0;
    if (params->GetInteger(kInterval, reinterpret_cast<int*>(&m_interval), 0))
        m_counter = reinterpret_cast<uint32_t>(this) % m_interval + 1;
}

// 0x00422DA0
void CModelLightUpdateComponent::Finalize()
{
    mp_model = static_cast<CModelComponent*>(m_object->GetComponent(kModel));
}

// 0x00422E40
void CModelLightUpdateComponent::Sample(bool lerp, float rate)
{
    // Down from just above the object (nudged along its up vector).
    Nx::CFeeler down;
    down.Construct();
    CCompositeObject* object = m_object;
    Mth::Vector& start = down.m_line.start;
    Mth::Vector& end = down.m_line.end;
    start = object->m_pos;
    end = object->m_pos;
    const Mth::Vector& up = object->m_matrix.row[1];
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
    bool hit = down.GetCollision(false, false) != 0;
    // Again from higher up, to just under what it hit (or as far down), faces flagged 0x20 only.
    Nx::CFeeler again;
    again.Construct();
    again.SetIgnore(0, 0x20);
    again.m_line.start = {start.x, start.y + kSecondAbove, start.z, start.w};
    if (hit)
        again.m_line.end = {down.m_point.x, down.m_point.y - 1.0f, down.m_point.z, down.m_point.w};
    else
        again.m_line.end = {start.x, start.y - kDepth, start.z, start.w};
    if (again.GetCollision(false, false)) {
        mp_model->ApplyBrightness(&again, lerp, rate);
        m_brightness = static_cast<float>(again.GetHitVertexValue());
    } else if (hit) {
        mp_model->ApplyBrightness(&down, lerp, 1.0f);
        m_brightness = static_cast<float>(down.GetHitVertexValue());
    }
    again.Destruct();
    down.Destruct();
}

// 0x004230B0
void CModelLightUpdateComponent::Update()
{
    if (!(m_lightFlags & MODEL_LIGHT_SAMPLED)) {
        Sample(false, 1.0f);
        m_lightFlags |= MODEL_LIGHT_SAMPLED;
        return;
    }
    if (m_interval && m_counter++ < m_interval) {
        static_cast<Nx::CModelLights*>(ModelNx::ModelLights(mp_model->mp_model))->MoveBrightness(m_brightness, kLerpRate);
        return;
    }
    m_counter = 0;
    if (m_lightFlags & MODEL_LIGHT_ONCE)
        VirtualSuspend(true);
    else
        Sample(true, 1.0f);
}

} // namespace Obj

REPLACE(0x00422DC0, Obj::CModelLightUpdateComponent::Create);
REPLACE(0x00422D00, &Obj::CModelLightUpdateComponent::DestroyBody);
REPLACE(0x00422D10, &Obj::CModelLightUpdateComponent::Destroy);
REPLACE(0x00422D40, &Obj::CModelLightUpdateComponent::InitFromStructure);
REPLACE(0x00422DA0, &Obj::CModelLightUpdateComponent::Finalize);
REPLACE(0x00422E40, &Obj::CModelLightUpdateComponent::Sample);
REPLACE(0x004230B0, &Obj::CModelLightUpdateComponent::Update);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x00646198,
    &Obj::CModelLightUpdateComponent::Destroy,
    &Obj::CModelLightUpdateComponent::Update,
    &Obj::CModelLightUpdateComponent::InitFromStructure,
    &Obj::CModelLightUpdateComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
