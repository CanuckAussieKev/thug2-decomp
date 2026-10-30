// The dynamic light component (Obj::CDynamicLightComponent) and the light's radius (Nx::CDynamicLight::SetRadius): see
// dynamic_light.h. From the disassembly of 0x0041CB90-0x0041D161. The x87 roundings are kept: values the original
// stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "game/obj/dynamic_light.h"
#include "game/gfx/light_manager.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/dynamic_light.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Nx {

// 0x0041CB90
void CDynamicLight::SetRadius(float radius)
{
    m_radius = radius;
    m_inverseRadius = radius == 0.0f ? 1e10f : 1.0f / radius;
}

} // namespace Nx

namespace Obj {

namespace {

extern void** const kVtable;

constexpr uint32_t kStartRadius = 0x3BEE67C8, kMiddleRadius = 0x8E39913D, kEndRadius = 0x3F243A3C; // StartRadius...
constexpr uint32_t kStartIntensity = 0x27A7443F, kMiddleIntensity = 0xCEF9F4FD, kEndIntensity = 0xE1CE9780;
constexpr uint32_t kStartColor = 0x7E6BCBE6, kMiddleColor = 0x2A6B6FE2, kEndColor = 0x0481113D; // StartRGBA...
constexpr uint32_t kMiddleTime = 0x856BC5E2; // midTime
constexpr uint32_t kWaitAndDie = 0x03AC8065;
// CallMemberFunction's members and parameters
constexpr uint32_t kSetRadius = 0xFAAD1C11, kSetIntensity = 0x86E1FA56, kSetColor = 0x1A0F4C50;
constexpr uint32_t kRadius = 0xC48391A5, kIntensity = 0x164D476D, kColor = 0x3F6BCDBA; // radius, intensity, rgba
constexpr uint32_t kNoMiddle = 0xBF800000; // -1.0f's bits (compared as bits)
constexpr double kTwoTo32 = 4294967296.0;   // 0x006453B4 (FILD's correction for unsigned values)

// The colour of `name` - an array of four bytes - into `color` (unchanged if there's none; `bytes` is the caller's
// one slot for the array, as in the original).
void GetColor(Script::CStruct* params, uint32_t name, uint32_t* color, int assert, Script::CArray*& bytes)
{
    if (!params->GetArray(name, &bytes, assert))
        return;
    auto* out = reinterpret_cast<uint8_t*>(color);
    for (uint32_t i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>(bytes->GetElement(i));
}

// An unsigned value as FILD (plus 2^32 when the top bit is set) loads it.
double Unsigned(uint32_t value)
{
    double d = static_cast<double>(static_cast<int32_t>(value));
    return static_cast<int32_t>(value) < 0 ? d + kTwoTo32 : d;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// 0x0048FB80 (cdecl; no source yet): a new dynamic light.
Nx::CDynamicLight* NewDynamicLight(int kind)
{
    return reinterpret_cast<Nx::CDynamicLight*>(Nx::NewDynamicLight(kind));
}

} // namespace

// 0x0041D0F0
CBaseComponent* __cdecl CDynamicLightComponent::Create()
{
    auto* component = static_cast<CDynamicLightComponent*>(Mem::Calloc(sizeof(CDynamicLightComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    component->mp_light = nullptr;
    return component;
}

// 0x0041CBD0
void CDynamicLightComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_light) {
        mp_light->m_ownerId = 0;
        mp_light = nullptr;
    }
    mp_waitAndDie = nullptr;
    CBaseComponent::DestroyBody();
}

// 0x0041D0C0
CDynamicLightComponent* CDynamicLightComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041CBF0
void CDynamicLightComponent::InitFromStructure(Script::CStruct* params)
{
    Script::CArray* bytes = nullptr;
    params->GetFloat(kStartRadius, &m_startRadius, 1);
    params->GetFloat(kStartIntensity, &m_startIntensity, 1);
    params->GetArray(kStartColor, &bytes, 1);
    auto* start = reinterpret_cast<uint8_t*>(&m_startColor);
    for (uint32_t i = 0; i < 4; ++i) // (read whether found or not, as the original does)
        start[i] = static_cast<uint8_t>(bytes->GetElement(i));
    m_middleRadius = m_startRadius;
    m_endRadius = m_startRadius;
    m_middleColor = m_startColor;
    m_endColor = m_startColor;
    m_middleIntensity = m_startIntensity;
    m_endIntensity = m_startIntensity;
    m_middleTime = -1.0f;
    params->GetFloat(kMiddleTime, &m_middleTime, 0);
    params->GetFloat(kMiddleRadius, &m_middleRadius, 0);
    params->GetFloat(kMiddleIntensity, &m_middleIntensity, 0);
    GetColor(params, kMiddleColor, &m_middleColor, 0, bytes);
    params->GetFloat(kEndRadius, &m_endRadius, 0);
    params->GetFloat(kEndIntensity, &m_endIntensity, 0);
    GetColor(params, kEndColor, &m_endColor, 0, bytes);
}

// 0x0041CDA0
void CDynamicLightComponent::Finalize() { mp_waitAndDie = reinterpret_cast<uint8_t*>(m_object->GetComponent(kWaitAndDie)); }

// 0x0041CDC0
void CDynamicLightComponent::Update()
{
    if (!mp_light) {
        mp_light = NewDynamicLight(1);
        mp_light->m_ownerId = m_object->m_id;
    }
    // The life's fraction.
    double t = Unsigned(*reinterpret_cast<uint32_t*>(mp_waitAndDie + 0x1C)) /
               Unsigned(*reinterpret_cast<uint32_t*>(mp_waitAndDie + 0x20));
    uint32_t fromColor = m_startColor, toColor = m_endColor;
    float fromRadius = m_startRadius, toRadius = m_endRadius;
    float fromIntensity = m_startIntensity, toIntensity = m_endIntensity;
    uint32_t middleBits;
    std::memcpy(&middleBits, &m_middleTime, 4);
    if (middleBits != kNoMiddle) {
        if (t <= m_middleTime) {
            toColor = m_middleColor;
            t = t / m_middleTime;
            toRadius = m_middleRadius;
            toIntensity = m_middleIntensity;
        } else {
            fromColor = m_middleColor;
            fromRadius = m_middleRadius;
            fromIntensity = m_middleIntensity;
            t = (t - m_middleTime) / (1.0 - m_middleTime);
        }
    }
    float radius = static_cast<float>((static_cast<double>(toRadius) - fromRadius) * t + fromRadius);
    uint32_t color = 0;
    for (int i = 0; i < 4; ++i) {
        int from = (fromColor >> (8 * i)) & 0xFF;
        int to = (toColor >> (8 * i)) & 0xFF;
        color |= static_cast<uint32_t>(static_cast<uint8_t>(Truncated(static_cast<double>(to - from) * t + from)))
                 << (8 * i);
    }
    Nx::CDynamicLight* light = mp_light;
    light->m_pos = m_object->m_pos;
    light->m_color = color;
    light->m_intensity = static_cast<float>((static_cast<double>(toIntensity) - fromIntensity) * t + fromIntensity);
    light->SetRadius(radius);
}

// 0x0041CFC0
int CDynamicLightComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    switch (name) {
    case kSetRadius: {
        float radius = 0.0f;
        params->GetFloat(kRadius, &radius, 1);
        mp_light->SetRadius(radius);
        return 1;
    }
    case kSetIntensity: {
        float intensity = 0.0f;
        params->GetFloat(kIntensity, &intensity, 1);
        mp_light->m_intensity = intensity;
        return 1;
    }
    case kSetColor: {
        Script::CArray* bytes = nullptr;
        params->GetArray(kColor, &bytes, 1);
        uint32_t color;
        auto* out = reinterpret_cast<uint8_t*>(&color);
        for (uint32_t i = 0; i < 4; ++i)
            out[i] = static_cast<uint8_t>(bytes->GetElement(i));
        mp_light->m_color = color;
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x0041CB90, &Nx::CDynamicLight::SetRadius);
REPLACE(0x0041D0F0, Obj::CDynamicLightComponent::Create);
REPLACE(0x0041CBD0, &Obj::CDynamicLightComponent::DestroyBody);
REPLACE(0x0041D0C0, &Obj::CDynamicLightComponent::Destroy);
REPLACE(0x0041CBF0, &Obj::CDynamicLightComponent::InitFromStructure);
REPLACE(0x0041CDA0, &Obj::CDynamicLightComponent::Finalize);
REPLACE(0x0041CDC0, &Obj::CDynamicLightComponent::Update);
REPLACE(0x0041CFC0, &Obj::CDynamicLightComponent::CallMemberFunction);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x00645F58,
    &Obj::CDynamicLightComponent::Destroy,
    &Obj::CDynamicLightComponent::Update,
    &Obj::CDynamicLightComponent::InitFromStructure,
    &Obj::CDynamicLightComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::CDynamicLightComponent::CallMemberFunction);

} // namespace
} // namespace Obj
