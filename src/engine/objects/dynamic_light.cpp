// The dynamic light component (GameObjects::CDynamicLightComponent) and the light's radius (Engine3D::CDynamicLight::SetReach): see
// dynamic_light.h. From the disassembly of 0x0041CB90-0x0041D161. The x87 roundings are kept: values the original
// stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "engine/objects/dynamic_light.h"
#include "engine/gfx/light_manager.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/dynamic_light.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace Engine3D {

// 0x0041CB90
void CDynamicLight::SetReach(float radius)
{
    m_radius = radius;
    m_inverseRadius = radius == 0.0f ? 1e10f : 1.0f / radius;
}

} // namespace Nx

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kStartRadius = 0x3BEE67C8, kMiddleRadius = 0x8E39913D, kEndRadius = 0x3F243A3C; // StartRadius...
constexpr uint32_t kStartIntensity = 0x27A7443F, kMiddleIntensity = 0xCEF9F4FD, kEndIntensity = 0xE1CE9780;
constexpr uint32_t kStartColor = 0x7E6BCBE6, kMiddleColor = 0x2A6B6FE2, kEndColor = 0x0481113D; // StartRGBA...
constexpr uint32_t kMiddleTime = 0x856BC5E2; // midTime
constexpr uint32_t kWaitAndDie = 0x03AC8065;
// InvokeMethodByName's members and parameters
constexpr uint32_t kSetRadius = 0xFAAD1C11, kSetIntensity = 0x86E1FA56, kSetColor = 0x1A0F4C50;
constexpr uint32_t kRadius = 0xC48391A5, kIntensity = 0x164D476D, kColor = 0x3F6BCDBA; // radius, intensity, rgba
constexpr uint32_t kNoMiddle = 0xBF800000; // -1.0f's bits (compared as bits)
constexpr double kTwoTo32 = 4294967296.0;   // 0x006453B4 (FILD's correction for unsigned values)

// The colour of `name` - an array of four bytes - into `color` (unchanged if there's none; `bytes` is the caller's
// one slot for the array, as in the original).
void FetchTint(Scripts::CStruct* params, uint32_t name, uint32_t* color, int assert, Scripts::CArray*& bytes)
{
    if (!params->FetchList(name, &bytes, assert))
        return;
    auto* out = reinterpret_cast<uint8_t*>(color);
    for (uint32_t i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>(bytes->FetchPart(i));
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
Engine3D::CDynamicLight* NewDynamicLight(int kind)
{
    return reinterpret_cast<Engine3D::CDynamicLight*>(Engine3D::NewDynamicLight(kind));
}

} // namespace

// 0x0041D0F0
CObjectModule* __cdecl CDynamicLightComponent::Fabricate()
{
    auto* component = static_cast<CDynamicLightComponent*>(MemSys::AllocZeroed(sizeof(CDynamicLightComponent)));
    if (!component)
        return nullptr;
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    component->m_light = nullptr;
    return component;
}

// 0x0041CBD0
void CDynamicLightComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (m_light) {
        m_light->m_holderKey = 0;
        m_light = nullptr;
    }
    m_waitAndDie = nullptr;
    CObjectModule::DestroyBody();
}

// 0x0041D0C0
CDynamicLightComponent* CDynamicLightComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041CBF0
void CDynamicLightComponent::ConfigureFromData(Scripts::CStruct* params)
{
    Scripts::CArray* bytes = nullptr;
    params->FetchFloat(kStartRadius, &m_startRadius, 1);
    params->FetchFloat(kStartIntensity, &m_startIntensity, 1);
    params->FetchList(kStartColor, &bytes, 1);
    auto* start = reinterpret_cast<uint8_t*>(&m_startColor);
    for (uint32_t i = 0; i < 4; ++i) // (read whether found or not, as the original does)
        start[i] = static_cast<uint8_t>(bytes->FetchPart(i));
    m_middleRadius = m_startRadius;
    m_endRadius = m_startRadius;
    m_middleColor = m_startColor;
    m_endColor = m_startColor;
    m_middleIntensity = m_startIntensity;
    m_endIntensity = m_startIntensity;
    m_middleTime = -1.0f;
    params->FetchFloat(kMiddleTime, &m_middleTime, 0);
    params->FetchFloat(kMiddleRadius, &m_middleRadius, 0);
    params->FetchFloat(kMiddleIntensity, &m_middleIntensity, 0);
    FetchTint(params, kMiddleColor, &m_middleColor, 0, bytes);
    params->FetchFloat(kEndRadius, &m_endRadius, 0);
    params->FetchFloat(kEndIntensity, &m_endIntensity, 0);
    FetchTint(params, kEndColor, &m_endColor, 0, bytes);
}

// 0x0041CDA0
void CDynamicLightComponent::Seal() { m_waitAndDie = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kWaitAndDie)); }

// 0x0041CDC0
void CDynamicLightComponent::TickFrame()
{
    if (!m_light) {
        m_light = NewDynamicLight(1);
        m_light->m_holderKey = m_entity->m_ident;
    }
    // The life's fraction.
    double t = Unsigned(*reinterpret_cast<uint32_t*>(m_waitAndDie + 0x1C)) /
               Unsigned(*reinterpret_cast<uint32_t*>(m_waitAndDie + 0x20));
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
    Engine3D::CDynamicLight* light = m_light;
    light->m_atPoint = m_entity->m_atPoint;
    light->m_tint = color;
    light->m_strength = static_cast<float>((static_cast<double>(toIntensity) - fromIntensity) * t + fromIntensity);
    light->SetReach(radius);
}

// 0x0041CFC0
int CDynamicLightComponent::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    switch (name) {
    case kSetRadius: {
        float radius = 0.0f;
        params->FetchFloat(kRadius, &radius, 1);
        m_light->SetReach(radius);
        return 1;
    }
    case kSetIntensity: {
        float intensity = 0.0f;
        params->FetchFloat(kIntensity, &intensity, 1);
        m_light->m_strength = intensity;
        return 1;
    }
    case kSetColor: {
        Scripts::CArray* bytes = nullptr;
        params->FetchList(kColor, &bytes, 1);
        uint32_t color;
        auto* out = reinterpret_cast<uint8_t*>(&color);
        for (uint32_t i = 0; i < 4; ++i)
            out[i] = static_cast<uint8_t>(bytes->FetchPart(i));
        m_light->m_tint = color;
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CDynamicLightComponent::Teardown,
         &GameObjects::CDynamicLightComponent::TickFrame,
         &GameObjects::CDynamicLightComponent::ConfigureFromData,
         &GameObjects::CDynamicLightComponent::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CDynamicLightComponent::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
