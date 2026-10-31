// The engine's model lights: see engine_model_lights.h. The brightness maths follows the original's x87 code
// (53-bit precision: double where it keeps values in registers, float where it stores them).

#include "game/gfx/engine_model_lights.h"

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/light_manager.h"
#include "game/mth/vector.h"
#include "game/core/code_address.h"

namespace Nx {

uint32_t g_lightCount;

namespace {

extern void** const kModelLightsVtable;

Mth::Vector g_lightDirection;
uint32_t g_lightDirectionGuard;

// The light manager's: how many lights, the ambient ranges below / above the middle brightness, each light's.
uint32_t LightCount() { return g_lightCount; }
// (the world lights' fields: ambientLow 0x0067DCA4, ambientHigh, lightLow, lightHigh)
float DarkRange() { return g_worldLights.ambientLow; }
float BrightRange() { return g_worldLights.ambientHigh; }
float* DarkRanges() { return g_worldLights.lightLow; }
float* BrightRanges() { return g_worldLights.lightHigh; }

template <typename R = void, typename... A>
R Virtual(CModelLights* lights, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CModelLights*, A...)>(lights->m_vtable[slot])(lights, args...);
}

void Stub(const char* message) { Dbg::Printf(message); }

// 2b - 1 times a range, plus 1 (the bright range above 0.5 - or NaN -, the dark one at and below).
float Scaled(float brightness, float dark, float bright)
{
    double range = brightness <= 0.5f ? dark : bright;
    return static_cast<float>((static_cast<double>(brightness) * 2.0 - 1.0) * range + 1.0);
}

} // namespace

// 0x0048E5B0
CModelLights* CModelLights::Construct()
{
    m_vtable = reinterpret_cast<void**>(kModelLightsVtable);
    m_brightnessTarget = 0.0f;
    m_brightness = 0.0f;
    m_scale = 1.0f;
    mp_intensities = static_cast<float*>(Mem::New(LightCount() * 4));
    for (uint32_t i = 0; i < LightCount(); ++i)
        mp_intensities[i] = 1.0f;
    m_unknown4 = 0;
    mp_position = nullptr;
    return this;
}

// 0x0048E630
void CModelLights::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kModelLightsVtable);
    if (mp_intensities) {
        Mem::Manager::Get()->Free(mp_intensities);
        mp_intensities = nullptr;
    }
}

// 0x0048E7F0
CModelLights* CModelLights::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0048E450
bool CModelLights::SetAmbientColor(const uint32_t* color)
{
    bool result = Virtual<bool>(this, 1, color);
    Virtual(this, 8, true, false);
    return result;
}

// 0x0048E480
bool CModelLights::SetDiffuseColor(int light, const uint32_t* color)
{
    bool result = Virtual<bool>(this, 3, light, color);
    Virtual(this, 8, false, true);
    return result;
}

// 0x0048E4D0
bool CModelLights::SetPositionPointer(const Mth::Vector* position)
{
    mp_position = position;
    return true;
}

// 0x0048E660
void CModelLights::UpdateBrightness()
{
    m_scale = Scaled(m_brightness, DarkRange(), BrightRange());
    if (m_scale < 0.0f)
        m_scale = 0.0f;
    for (uint32_t i = 0; i < LightCount(); ++i) {
        mp_intensities[i] = Scaled(m_brightness, DarkRanges()[i], BrightRanges()[i]);
        if (mp_intensities[i] < 0.0f)
            mp_intensities[i] = 0.0f;
    }
    Virtual(this, 8, true, true);
}

// 0x0048E830
bool CModelLights::MoveBrightness(float target, float step)
{
    m_brightnessTarget = target;
    if (m_brightness < target) {
        double moved = static_cast<double>(step) + m_brightness; // (compared before it's rounded)
        m_brightness = static_cast<float>(moved);
        if (moved > target)
            m_brightness = target;
    } else if (m_brightness > target) {
        double moved = static_cast<double>(m_brightness) - step;
        m_brightness = static_cast<float>(moved);
        if (moved < target)
            m_brightness = target;
    }
    UpdateBrightness();
    return true;
}

// 0x0048E8A0
bool CModelLights::SetBrightness(float brightness)
{
    m_brightnessTarget = brightness;
    m_brightness = brightness;
    UpdateBrightness();
    return true;
}

// --- the base's platform calls ----------------------------------------------------------------------------------

// 0x0048E4E0
bool CModelLights::PlatSetLightAmbientColor(const uint32_t*)
{
    Stub("STUB: CModelLights::SetLightAmbientColor\n"); // "STUB: CModelLights::SetLightAmbientColor\n"
    return false;
}
// 0x0048E500
bool CModelLights::PlatSetLightDirection(int, const Mth::Vector*)
{
    Stub("STUB: CModelLights::SetLightDirection\n"); // "STUB: CModelLights::SetLightDirection\n"
    return false;
}
// 0x0048E520
bool CModelLights::PlatSetLightDiffuseColor(int, const uint32_t*)
{
    Stub("STUB: CModelLights::SetLightDiffuseColor\n"); // "STUB: CModelLights::SetLightDiffuseColor\n"
    return false;
}
// 0x0048E730
uint32_t* CModelLights::PlatGetLightAmbientColor(uint32_t* out)
{
    Stub("STUB: CModelLights::GetLightAmbientColor\n"); // "STUB: CModelLights::GetLightAmbientColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048E760 (the vector 0x006FAACC, its guard 0x006FAADC: zeroed the first time)
Mth::Vector* CModelLights::PlatGetLightDirection(int)
{
    Stub("STUB: CModelLights::GetLightDirection\n"); // "STUB: CModelLights::GetLightDirection\n"
    uint32_t& guard = g_lightDirectionGuard;
    auto* direction = &g_lightDirection;
    if (!(guard & 1)) {
        guard |= 1;
        std::memset(direction, 0, sizeof(Mth::Vector));
    }
    return direction;
}
// 0x0048E7C0
uint32_t* CModelLights::PlatGetLightDiffuseColor(uint32_t* out, int)
{
    Stub("STUB: CModelLights::GetLightDiffuseColor\n"); // "STUB: CModelLights::GetLightDiffuseColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048E5A0
void CModelLights::PlatApplySceneLights(const Mth::Vector*, uint32_t)
{
    Stub("STUB: CModelLights::plat_apply_scene_lights\n"); // "STUB: CModelLights::plat_apply_scene_lights\n"
}
// 0x0048E540
void CModelLights::PlatUpdateBrightness(bool, bool) { Stub("STUB: CModelLights::UpdateBrightness\n"); } // "STUB: CModelLights::UpdateBrightness\n"
// 0x0048E550
void CModelLights::PlatEnableAmbientLight(bool) { Stub("STUB: CModelLights::EnableAmbientLight\n"); } // "STUB: CModelLights::EnableAmbientLight\n"
// 0x0048E560
void CModelLights::PlatEnableDiffuseLight(int, bool) { Stub("STUB: CModelLights::EnableDiffuseLight\n"); } // "STUB: CModelLights::EnableDiffuseLight\n"
// 0x0048E570
bool CModelLights::PlatIsAmbientLightEnabled()
{
    Stub("STUB: CModelLights::IsAmbientLightEnabled\n"); // "STUB: CModelLights::IsAmbientLightEnabled\n"
    return false;
}
// 0x0048E580
bool CModelLights::PlatIsDiffuseLightEnabled(int)
{
    Stub("STUB: CModelLights::IsDiffuseLightEnabled\n"); // "STUB: CModelLights::IsDiffuseLightEnabled\n"
    return false;
}

namespace {
static VtableSlot kModelLightsVtable_slots[] = {&Nx::CModelLights::Destroy, &Nx::CModelLights::PlatSetLightAmbientColor,
                &Nx::CModelLights::PlatSetLightDirection, &Nx::CModelLights::PlatSetLightDiffuseColor,
                &Nx::CModelLights::PlatGetLightAmbientColor, &Nx::CModelLights::PlatGetLightDirection,
                &Nx::CModelLights::PlatGetLightDiffuseColor, &Nx::CModelLights::PlatApplySceneLights,
                &Nx::CModelLights::PlatUpdateBrightness, &Nx::CModelLights::PlatEnableAmbientLight,
                &Nx::CModelLights::PlatEnableDiffuseLight, &Nx::CModelLights::PlatIsAmbientLightEnabled,
                &Nx::CModelLights::PlatIsDiffuseLightEnabled};
void** const kModelLightsVtable = reinterpret_cast<void**>(kModelLightsVtable_slots);
} // namespace

} // namespace Nx
