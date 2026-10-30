// The colour cycle component (Obj::CColorCycleComponent): see color_cycle.h. From the disassembly of
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
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

GAME_GLOBAL(uint32_t[30], g_colorCycleMemberFunctions, 0x006845D0,
            0x4AF8B9AE, 0xCDC76A22, 0, 0, 0, 0,
            0x1DE2F35B, 0xCDC76A22, 0, 0, 0, 0,
            0x4A752AAE, 0xCDC76A22, 0, 0, 0, 0,
            0x80D5F732, 0xCDC76A22, 0, 0, 0, 0,
            0, 0xCDC76A22, 0, 0, 0, 0);

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00645E00);
constexpr uint32_t kName = 0xA1DC81F9;  // name
constexpr uint32_t kColor = 0x99A9B716; // color
constexpr uint32_t kNeutralColor = 0x808080;
constexpr float kMilliseconds = 1000.0f; // 0x006453C0

// 0x005A3E80 (cdecl; no source yet): the colour of the object `id` from `params` (color=), as the script command
// SetObjectColor does.
void SetObjectColor(uint32_t id, Script::CStruct* params)
{
    CFuncs::SetNodeColor(id, params);
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

Script::CStruct* NewParams()
{
    Script::CStruct* params = Script::NewStruct();
    return params ? params->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear(); // (the destructor)
        Script::DeleteStruct(params);
    }
}

} // namespace

// 0x0041B3C0
CBaseComponent* __cdecl CColorCycleComponent::Create()
{
    auto* component = static_cast<CColorCycleComponent*>(Mem::Calloc(sizeof(CColorCycleComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_colorCount = 0;
    component->m_current = 0;
    component->m_time = 0.0f;
    component->m_on = false;
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x0041B280
void __cdecl CColorCycleComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_colorCycleMemberFunctions, 0x18);
}

// 0x0041B2A0
void CColorCycleComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0041B2E0
CColorCycleComponent* CColorCycleComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041B310
void CColorCycleComponent::SetColors(Script::CArray* colors)
{
    int count = static_cast<int>(colors->m_size);
    m_colorCount = count;
    for (int i = 0; i < count; ++i) {
        auto* vector = reinterpret_cast<const float*>(colors->GetElement(static_cast<uint32_t>(i)));
        m_colors[i] = {vector[1], vector[2], vector[3], 1.0f}; // (a CVector: its coordinates from +4)
    }
    m_current = 0;
}

// 0x0041B440
void CColorCycleComponent::SetOn(bool on)
{
    if (m_on && !on) {
        Script::CStruct* params = NewParams();
        params->AddChecksum(kName, m_object->m_id);
        params->AddInteger(kColor, kNeutralColor);
        SetObjectColor(m_object->m_id, params);
        DeleteParams(params);
    }
    if (m_on) {
        m_on = on;
        return;
    }
    m_on = on;
    if (on) {
        m_time = 0.0f;
        m_current = 0;
    }
}

// 0x0041B580
void CColorCycleComponent::Update()
{
    if (!m_on)
        return;
    Script::CStruct* params = NewParams();
    const Mth::Vector& color = m_colors[m_current];
    uint32_t rgb = static_cast<uint32_t>(Truncated(color.z)) << 8;
    rgb = (rgb + static_cast<uint32_t>(Truncated(color.y))) << 8;
    rgb += static_cast<uint32_t>(Truncated(color.x));
    params->AddInteger(kColor, static_cast<int>(rgb));
    params->AddChecksum(kName, m_object->m_id);
    Script::PrintContents(params, 0);
    SetObjectColor(m_object->m_id, params);
    DeleteParams(params);
    // (compared before rounding, as the original keeps the sum on the FPU stack)
    double time = Tmr::GetSlomoFrameSeconds() * kMilliseconds + m_time;
    m_time = static_cast<float>(time);
    if (time >= m_period) {
        m_time = 0.0f;
        if (++m_current >= m_colorCount)
            m_current = 0;
    }
}

// 0x0041B560
int CColorCycleComponent::On(Script::CStruct*, Script::CScript*)
{
    SetOn(true);
    return 1;
}

// 0x0041B570
int CColorCycleComponent::Off(Script::CStruct*, Script::CScript*)
{
    SetOn(false);
    return 1;
}

// 0x0041B2B0
int CColorCycleComponent::SetPeriodFromScript(Script::CStruct* params, Script::CScript*)
{
    float period;
    std::memcpy(&period, &params, sizeof(period)); // (the original's slot for it: the params argument)
    params->GetFloat(0u, &period, 1);
    m_period = period;
    return 1;
}

// 0x0041B390
int CColorCycleComponent::SetColorsFromScript(Script::CStruct* params, Script::CScript*)
{
    auto* colors = reinterpret_cast<Script::CArray*>(params); // (the original's slot for it: the params argument)
    params->GetArray(0u, &colors, 1);
    SetColors(colors);
    return 1;
}

} // namespace Obj

REPLACE(0x0041B3C0, Obj::CColorCycleComponent::Create);
REPLACE(0x0041B280, Obj::CColorCycleComponent::RegisterMemberFunctions);
REPLACE(0x0041B2A0, &Obj::CColorCycleComponent::DestroyBody);
REPLACE(0x0041B2E0, &Obj::CColorCycleComponent::Destroy);
REPLACE(0x0041B310, &Obj::CColorCycleComponent::SetColors);
REPLACE(0x0041B440, &Obj::CColorCycleComponent::SetOn);
REPLACE(0x0041B580, &Obj::CColorCycleComponent::Update);
REPLACE(0x0041B560, &Obj::CColorCycleComponent::On);
REPLACE(0x0041B570, &Obj::CColorCycleComponent::Off);
REPLACE(0x0041B2B0, &Obj::CColorCycleComponent::SetPeriodFromScript);
REPLACE(0x0041B390, &Obj::CColorCycleComponent::SetColorsFromScript);
