// Lighting on PC: see pc_lights.h. Colours follow the original's x87 code: a byte times float factors, kept in
// double precision, rounded once when stored.

#include "game/gfx/pc_lights.h"

#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/light_manager.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/core/stubs.h"

namespace Nx {

GAME_GLOBAL(Mth::Vector, g_sceneLightDirection, 0x0070166C);
GAME_GLOBAL(uint32_t, g_sceneLightDirectionGuard, 0x0070167C);
GAME_GLOBAL(float[28], g_lightConstants, 0x00786F38);


namespace {

extern void** const kModelLightsVtable;
constexpr double kPer128 = 1.0 / 128.0;           // 0x0064A2FC
constexpr float kPer255 = 1.0f / 255.0f;          // 0x00645D70 (0x3B808081)

// The renderer's light constants.
float* AmbientConstant() { return g_lightConstants; }
float* LightDirectionConstant(uint32_t light) { return g_lightConstants + 4 + 8 * light; }
float* LightColorConstant(uint32_t light) { return g_lightConstants + 8 + 8 * light; }

// The scene's lights.
uint32_t& SceneAmbient() { return g_worldLights.ambient; }
float SceneScale() { return g_ambientIntensity; }
Mth::Vector* SceneDirections() { return g_worldLights.directions; } // (0x0067DC9C)
uint32_t* SceneColors() { return g_worldLights.colours; }           // (0x0067DCA0)
float* SceneIntensities() { return g_intensities; }
uint32_t LightCount() { return g_lightCount; }
uint32_t ModelLightCount() { return g_modelLightCount; }

// A colour's r, g, b bytes times `factor` and 1/128 into three floats.
void SendColor(float* out, uint32_t rgba, float factor)
{
    for (int k = 0; k < 3; ++k)
        out[k] = static_cast<float>(static_cast<double>((rgba >> (8 * k)) & 0xFF) * factor * kPer128);
}
void SendNegated(float* out, const Mth::Vector& v)
{
    out[0] = -v.x;
    out[1] = -v.y;
    out[2] = -v.z;
}

template <typename R, typename... A>
R CallVirtual(CPcModelLights* lights, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CPcModelLights*, A...)>(lights->vtable[slot])(lights, args...);
}

// A dynamic light's falloff at `distance` (its virtual [2]; thiscall) as the x87 leaves it, unrounded.
__declspec(naked) double __stdcall Falloff(void* /*light*/, float /*distance*/)
{
    __asm {
        mov ecx, [esp + 4]
        push dword ptr [esp + 8]
        mov eax, [ecx]
        call dword ptr [eax + 8]
        ret 8
    }
}

// The dynamic lights within `radius` of `position` (the engine's 0x0048EF90): their count, pointers into `out`.
uint32_t FindDynamicLights(const Mth::Vector* position, uint32_t radius, uint8_t** out)
{
    return GetDynamicLightsNear(position, radius, reinterpret_cast<CDynamicLightBase**>(out));
}

} // namespace

// 0x004B6270
CPcModelLights* CPcModelLights::Construct()
{
    reinterpret_cast<CModelLights*>(this)->CModelLights::Construct();
    vtable = reinterpret_cast<void**>(kModelLightsVtable);
    overrides = 0;
    colors = static_cast<uint32_t*>(Mem::New(4 * ModelLightCount()));
    directions = static_cast<Mth::Vector*>(Mem::New(16 * ModelLightCount()));
    CallVirtual<void>(this, 9, false); // EnableAmbientOverride
    for (uint32_t i = 0; i < ModelLightCount(); ++i) {
        CallVirtual<void>(this, 10, static_cast<int>(i), false); // EnableLightOverride
        directions[i] = {0.0f, 1.0f, 0.0f, 1.0f};
    }
    firstDynamic = 2;
    return this;
}

// 0x004B6360
void CPcModelLights::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kModelLightsVtable);
    if (colors) {
        Mem::Manager::Get()->Free(colors);
        colors = nullptr;
    }
    if (directions) {
        Mem::Manager::Get()->Free(directions);
        directions = nullptr;
    }
    reinterpret_cast<CModelLights*>(this)->CModelLights::DestroyBody();
}

// 0x004B67B0
CPcModelLights* CPcModelLights::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B6160
bool CPcModelLights::SetAmbientColor(const uint32_t* color)
{
    ambient = *color;
    return true;
}

// 0x004B6170
uint32_t* CPcModelLights::GetAmbientColor(uint32_t* out)
{
    *out = ambient;
    return out;
}

// 0x004B6780
bool CPcModelLights::SetLightDirection(int light, const Mth::Vector* direction)
{
    directions[light] = *direction;
    return true;
}

// 0x004B6180
const Mth::Vector* CPcModelLights::GetLightDirection(int light)
{
    if (CallVirtual<bool>(this, 12, light)) // IsLightOverridden
        return &directions[light];
    return Nx::GetLightDirection(light);
}

// 0x004B61B0
bool CPcModelLights::SetLightDiffuseColor(int light, const uint32_t* color)
{
    colors[light] = *color;
    return true;
}

// 0x004B61D0
uint32_t* CPcModelLights::GetLightDiffuseColor(uint32_t* out, int light)
{
    *out = colors[light];
    return out;
}

// 0x004B63B0
void CPcModelLights::Update(const Mth::Vector* position, uint32_t radius)
{
    uint32_t color;
    if (overrides & 1)
        color = ambient;
    else
        GetAmbientLightColor(&color);
    SendColor(AmbientConstant(), color, scale);
    for (uint32_t i = 0; i < LightCount(); ++i) {
        if (overrides & (2u << (i & 31))) {
            SendColor(LightColorConstant(i), colors[i], intensities[i]);
            SendNegated(LightDirectionConstant(i), directions[i]);
        } else {
            GetLightColor(&color, static_cast<int>(i));
            SendColor(LightColorConstant(i), color, intensities[i]);
            SendNegated(LightDirectionConstant(i), *Nx::GetLightDirection(static_cast<int>(i)));
        }
    }
    if (!radius)
        return;
    uint8_t* dynamic[32];
    uint32_t count = FindDynamicLights(position, radius, dynamic);
    if (!count) {
        float* off = LightColorConstant(firstDynamic);
        off[0] = off[1] = off[2] = 0.0f;
        return;
    }
    for (uint32_t j = 0; j < count; ++j) {
        uint8_t* light = dynamic[j];
        const float* at = reinterpret_cast<const float*>(light + 4);
        double dx = static_cast<double>(position->x) - at[0];
        double dy = static_cast<double>(position->y) - at[1];
        double dz = static_cast<double>(position->z) - at[2];
        float distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
        double falloff = Falloff(light, distance);
        // the direction from the light, normalised (unless its length isn't above 0)
        dx = static_cast<double>(position->x) - at[0];
        dy = static_cast<double>(position->y) - at[1];
        float zf = static_cast<float>(static_cast<double>(position->z) - at[2]);
        double length = std::sqrt(dx * dx + static_cast<double>(zf) * zf + dy * dy);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            dx *= inverse;
            dy *= inverse;
            zf = static_cast<float>(zf * inverse);
        }
        float* direction = LightDirectionConstant(firstDynamic + j);
        direction[0] = static_cast<float>(dx);
        direction[1] = static_cast<float>(dy);
        direction[2] = zf;
        if (*reinterpret_cast<int*>(light + 0x14) == 1) {
            double factor = (1.0 - falloff) * *reinterpret_cast<const float*>(light + 0x20) * kPer255;
            float* out = LightColorConstant(firstDynamic + j);
            for (int k = 0; k < 3; ++k)
                out[k] = static_cast<float>(light[0x24 + k] * factor);
        }
    }
}

// 0x004B61F0
void CPcModelLights::EnableAmbientOverride(bool on)
{
    if (on)
        overrides |= 1;
    else
        overrides &= ~1u;
}

// 0x004B6240
bool CPcModelLights::IsAmbientOverridden()
{
    return (overrides & 1) != 0;
}

// 0x004B6210
void CPcModelLights::EnableLightOverride(int light, bool on)
{
    uint32_t bit = 2u << (light & 31);
    if (on)
        overrides |= bit;
    else
        overrides &= ~bit;
}

// 0x004B6250
bool CPcModelLights::IsLightOverridden(int light)
{
    return (overrides & (2u << (light & 31))) != 0;
}

// 0x004B67E0
bool __cdecl UpdateAmbientLight()
{
    SendColor(AmbientConstant(), SceneAmbient(), SceneScale());
    return true;
}

// 0x004B6850
uint32_t* __cdecl GetAmbientLightColor(uint32_t* out)
{
    *out = SceneAmbient();
    return out;
}

// 0x004B6860
bool __cdecl UpdateLightColor(int light)
{
    SendColor(LightColorConstant(light), SceneColors()[light], SceneIntensities()[light]);
    return true;
}

// 0x004B68E0
uint32_t* __cdecl GetLightColor(uint32_t* out, int light)
{
    *out = SceneColors()[light];
    return out;
}

// 0x004B6900
void __cdecl UpdateAllLights()
{
    UpdateAmbientLight();
    for (int i = 0; i < 31; ++i)
        UpdateLightColor(i);
}

// 0x004B6920
bool __cdecl DeleteModelLights(CPcModelLights* lights)
{
    if (lights)
        CallVirtual<void*>(lights, 0, 1u);
    return true;
}

// 0x004B6940
void __cdecl ResetLights()
{
    SendColor(AmbientConstant(), SceneAmbient(), SceneScale());
    for (int i = 0; i < 2; ++i) {
        uint32_t color;
        GetLightColor(&color, i);
        SendColor(LightColorConstant(i), color, SceneIntensities()[i]);
        SendNegated(LightDirectionConstant(i), *GetLightDirection(i));
    }
    float* third = LightColorConstant(2);
    third[0] = third[1] = third[2] = 0.0f;
}

// 0x004B6A70
bool __cdecl UpdateLightDirection(int light)
{
    SendNegated(LightDirectionConstant(light), SceneDirections()[light]);
    return true;
}

// 0x004B6AB0
Mth::Vector* __cdecl GetLightDirection(int light)
{
    g_sceneLightDirectionGuard |= 1; // (the static's initialisation guard)
    Mth::Vector& out = g_sceneLightDirection;
    const Mth::Vector& direction = SceneDirections()[light];
    out = {direction.x, direction.y, direction.z, 1.0f};
    return &out;
}

// 0x004B6B10
CPcModelLights* __cdecl CreateModelLights()
{
    void* memory = Mem::New(sizeof(CPcModelLights));
    return memory ? static_cast<CPcModelLights*>(memory)->Construct() : nullptr;
}

namespace {
GAME_VTABLE(kModelLightsVtable, 0x0064A440, &Nx::CPcModelLights::Destroy, &Nx::CPcModelLights::SetAmbientColor,
            &Nx::CPcModelLights::SetLightDirection, &Nx::CPcModelLights::SetLightDiffuseColor,
            &Nx::CPcModelLights::GetAmbientColor, &Nx::CPcModelLights::GetLightDirection,
            &Nx::CPcModelLights::GetLightDiffuseColor, &Nx::CPcModelLights::Update, Stubs::IgnoreArguments2,
            &Nx::CPcModelLights::EnableAmbientOverride, &Nx::CPcModelLights::EnableLightOverride,
            &Nx::CPcModelLights::IsAmbientOverridden, &Nx::CPcModelLights::IsLightOverridden);
} // namespace

} // namespace Nx

REPLACE(0x004B6270, &Nx::CPcModelLights::Construct);
REPLACE(0x004B6360, &Nx::CPcModelLights::DestroyBody);
REPLACE(0x004B67B0, &Nx::CPcModelLights::Destroy);
REPLACE(0x004B6160, &Nx::CPcModelLights::SetAmbientColor);
REPLACE(0x004B6170, &Nx::CPcModelLights::GetAmbientColor);
REPLACE(0x004B6780, &Nx::CPcModelLights::SetLightDirection);
REPLACE(0x004B6180, &Nx::CPcModelLights::GetLightDirection);
REPLACE(0x004B61B0, &Nx::CPcModelLights::SetLightDiffuseColor);
REPLACE(0x004B61D0, &Nx::CPcModelLights::GetLightDiffuseColor);
REPLACE(0x004B63B0, &Nx::CPcModelLights::Update);
REPLACE(0x004B61F0, &Nx::CPcModelLights::EnableAmbientOverride);
REPLACE(0x004B6240, &Nx::CPcModelLights::IsAmbientOverridden);
REPLACE(0x004B6210, &Nx::CPcModelLights::EnableLightOverride);
REPLACE(0x004B6250, &Nx::CPcModelLights::IsLightOverridden);
REPLACE(0x004B67E0, Nx::UpdateAmbientLight);
REPLACE(0x004B6850, Nx::GetAmbientLightColor);
REPLACE(0x004B6860, Nx::UpdateLightColor);
REPLACE(0x004B68E0, Nx::GetLightColor);
REPLACE(0x004B6900, Nx::UpdateAllLights);
REPLACE(0x004B6920, Nx::DeleteModelLights);
REPLACE(0x004B6940, Nx::ResetLights);
REPLACE(0x004B6A70, Nx::UpdateLightDirection);
REPLACE(0x004B6AB0, Nx::GetLightDirection);
REPLACE(0x004B6B10, Nx::CreateModelLights);
REPLACE_SOURCE_ONLY(0x0048E920, Nx::GetLightDirection); // a jump to 0x004B6AB0
