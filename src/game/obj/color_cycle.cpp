// The colour cycle component (GameObjects::CColorCycleComponent): see color_cycle.h. From the disassembly of
// 0x0041B280-0x0041B57F.

#include "game/obj/color_cycle.h"
#include "game/script/game_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_colorCycleMemberFunctions[30]{
            0x4AF8B9AE, 0xCDC76A22, 0, 0, 0, 0,
            0x1DE2F35B, 0xCDC76A22, 0, 0, 0, 0,
            0x4A752AAE, 0xCDC76A22, 0, 0, 0, 0,
            0x80D5F732, 0xCDC76A22, 0, 0, 0, 0,
            0, 0xCDC76A22, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kName = 0xA1DC81F9;  // name
constexpr uint32_t kColor = 0x99A9B716; // color
constexpr uint32_t kNeutralColor = 0x808080;
constexpr float kMilliseconds = 1000.0f; // 0x006453C0

// 0x005A3E80 (cdecl; no source yet): the colour of the object `id` from `params` (color=), as the script command
// SetObjectColor does.
void SetObjectColor(uint32_t id, Scripts::CStruct* params)
{
    ScriptCommands::SetNodeColor(id, params);
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* params = Scripts::NewStruct();
    return params ? params->PrepareAll() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear(); // (the destructor)
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// 0x0041B3C0
CObjectModule* __cdecl CColorCycleComponent::Fabricate()
{
    auto* component = static_cast<CColorCycleComponent*>(MemSys::AllocZeroed(sizeof(CColorCycleComponent)));
    if (!component)
        return nullptr;
    component->CObjectModule::MakeNew();
    component->m_colorCount = 0;
    component->m_ongoing = 0;
    component->m_clock = 0.0f;
    component->m_on = false;
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    return component;
}

// 0x0041B280
void __cdecl CColorCycleComponent::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_colorCycleMemberFunctions, 0x18);
}

// 0x0041B2A0
void CColorCycleComponent::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0041B2E0
CColorCycleComponent* CColorCycleComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041B310
void CColorCycleComponent::ChangeColors(Scripts::CArray* colors)
{
    int count = static_cast<int>(colors->m_extent);
    m_colorCount = count;
    for (int i = 0; i < count; ++i) {
        auto* vector = reinterpret_cast<const float*>(colors->FetchPart(static_cast<uint32_t>(i)));
        m_colors[i] = {vector[1], vector[2], vector[3], 1.0f}; // (a CVector: its coordinates from +4)
    }
    m_ongoing = 0;
}

// 0x0041B440
void CColorCycleComponent::SetOn(bool on)
{
    if (m_on && !on) {
        Scripts::CStruct* params = NewParams();
        params->PutNameKey(kName, m_entity->m_ident);
        params->PutInt(kColor, kNeutralColor);
        SetObjectColor(m_entity->m_ident, params);
        DeleteParams(params);
    }
    if (m_on) {
        m_on = on;
        return;
    }
    m_on = on;
    if (on) {
        m_clock = 0.0f;
        m_ongoing = 0;
    }
}

// 0x0041B580
void CColorCycleComponent::TickFrame()
{
    if (!m_on)
        return;
    Scripts::CStruct* params = NewParams();
    const Maths::Vec4& color = m_colors[m_ongoing];
    uint32_t rgb = static_cast<uint32_t>(Truncated(color.z)) << 8;
    rgb = (rgb + static_cast<uint32_t>(Truncated(color.y))) << 8;
    rgb += static_cast<uint32_t>(Truncated(color.x));
    params->PutInt(kColor, static_cast<int>(rgb));
    params->PutNameKey(kName, m_entity->m_ident);
    Scripts::DumpToLog(params, 0);
    SetObjectColor(m_entity->m_ident, params);
    DeleteParams(params);
    // (compared before rounding, as the original keeps the sum on the FPU stack)
    double time = FrameClock::GetSlomoFrameSeconds() * kMilliseconds + m_clock;
    m_clock = static_cast<float>(time);
    if (time >= m_period) {
        m_clock = 0.0f;
        if (++m_ongoing >= m_colorCount)
            m_ongoing = 0;
    }
}

// 0x0041B560
int CColorCycleComponent::On(Scripts::CStruct*, Scripts::CScript*)
{
    SetOn(true);
    return 1;
}

// 0x0041B570
int CColorCycleComponent::Disengaged(Scripts::CStruct*, Scripts::CScript*)
{
    SetOn(false);
    return 1;
}

// 0x0041B2B0
int CColorCycleComponent::SetPeriodFromScript(Scripts::CStruct* params, Scripts::CScript*)
{
    float period;
    std::memcpy(&period, &params, sizeof(period)); // (the original's slot for it: the params argument)
    params->FetchFloat(0u, &period, 1);
    m_period = period;
    return 1;
}

// 0x0041B390
int CColorCycleComponent::SetColorsFromScript(Scripts::CStruct* params, Scripts::CScript*)
{
    auto* colors = reinterpret_cast<Scripts::CArray*>(params); // (the original's slot for it: the params argument)
    params->FetchList(0u, &colors, 1);
    ChangeColors(colors);
    return 1;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CColorCycleComponent::Teardown,
         &GameObjects::CColorCycleComponent::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
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
