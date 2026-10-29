// The engine's light manager: see light_manager.h. The maths follows the original's x87 code (53-bit precision:
// double where it keeps values in registers, float where it stores them).

#include "game/gfx/light_manager.h"

#include <cmath>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/pc_lights.h"
#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kDynamicLightVtable = 0x00649290;
constexpr int kStackDepth = 4;
constexpr int kMaxDynamicLights = 256;

sWorldLights& World() { return GameVariable<sWorldLights>(0x0067DC98); }
sWorldLights* Stack() { return &GameVariable<sWorldLights>(0x006FAF10); }
int& StackTop() { return GameVariable<int>(0x0067DC88); }
uint32_t& LightCount() { return GameVariable<uint32_t>(0x006FAAE0); }
float& Brightness() { return GameVariable<float>(0x0067DC8C); }
float& BrightnessTarget() { return GameVariable<float>(0x0067DC90); }
float& AmbientIntensity() { return GameVariable<float>(0x0067DC94); }
float MinIntensity() { return GameVariable<float>(0x006FAAE8); }
float*& Intensities() { return GameVariable<float*>(0x006FAAEC); }

CDynamicLightBase** DynamicLights() { return &GameVariable<CDynamicLightBase*>(0x006FAAF0); }
int& DynamicLightCount() { return GameVariable<int>(0x006FAEF0); }
// GetDynamicLightsNear's falloffs and the lights' indexes (32 each).
float* NearFalloffs() { return &GameVariable<float>(0x0067DD38); }
int* NearIndexes() { return &GameVariable<int>(0x0067DCB8); }

// 2b - 1 times a range, plus 1 (the high range above 0.5 - or NaN -, the low one at and below).
float Scaled(float brightness, float low, float high)
{
    double range = brightness <= 0.5f ? low : high;
    return static_cast<float>((static_cast<double>(brightness) * 2.0 - 1.0) * range + 1.0);
}

// A compiled-out call (the empty 0x005480A0) after the stack is popped.
void CompiledOut() {}

// A light's falloff (virtual [2]): returned in ST0 unrounded.
double CallFalloff(CDynamicLightBase* light, float distance)
{
    return reinterpret_cast<double(__thiscall*)(CDynamicLightBase*, float)>(light->vtable[2])(light, distance);
}

} // namespace

// 0x0048EAC0
sWorldLights* sWorldLights::Construct()
{
    ambient = 0x80484848;
    directions = nullptr;
    colours = nullptr;
    ambientLow = 0.5f;
    lightLow = nullptr;
    lightHigh = nullptr;
    return this;
}

// 0x0048EB00
void sWorldLights::FreeArrays()
{
    if (directions) {
        Mem::Manager::Get()->Free(directions);
        directions = nullptr;
    }
    if (colours) {
        Mem::Manager::Get()->Free(colours);
        colours = nullptr;
    }
    if (lightLow) {
        Mem::Manager::Get()->Free(lightLow);
        lightLow = nullptr;
    }
    if (lightHigh) {
        Mem::Manager::Get()->Free(lightHigh);
        lightHigh = nullptr;
    }
}

// 0x0048EB70
void __cdecl InitLightManager(uint32_t maxLights, uint32_t maxModelLights)
{
    Dbg::Printf("Initializing Light Manager...");
    GameVariable<uint32_t>(0x006FAAE4) = maxModelLights;
    LightCount() = maxLights;
    sWorldLights& world = World();
    world.directions = static_cast<Mth::Vector*>(Mem::New(maxLights * 16));
    world.colours = static_cast<uint32_t*>(Mem::New(maxLights * 4));
    world.lightLow = static_cast<float*>(Mem::New(maxLights * 4));
    world.lightHigh = static_cast<float*>(Mem::New(maxLights * 4));
    for (int i = 0; i < kStackDepth; ++i) {
        sWorldLights& saved = Stack()[i];
        saved.directions = static_cast<Mth::Vector*>(Mem::New(maxLights * 16));
        saved.colours = static_cast<uint32_t*>(Mem::New(maxLights * 4));
        saved.lightLow = static_cast<float*>(Mem::New(maxLights * 4));
        saved.lightHigh = static_cast<float*>(Mem::New(maxLights * 4));
        std::memset(saved.directions, 0, maxLights * 16);
        std::memset(saved.colours, 0, maxLights * 4);
        std::memset(saved.lightLow, 0, maxLights * 4);
        std::memset(saved.lightHigh, 0, maxLights * 4);
    }
    Intensities() = static_cast<float*>(Mem::New(maxLights * 4));
    for (uint32_t i = 0; i < maxLights; ++i)
        Intensities()[i] = 1.0f;
}

// 0x0048FB60
void __cdecl UninitLightManager()
{
    Dbg::Printf("Uninitializing Light Manager...");
    DeleteDynamicLights();
}

// 0x0048E8F0
bool __cdecl SetLightAmbientColor(uint32_t rgba)
{
    World().ambient = rgba;
    return UpdateAmbientLight();
}

// 0x0048E900
uint32_t* __cdecl GetLightAmbientColor(uint32_t* out)
{
    GetAmbientLightColor(out);
    return out;
}

// 0x0048E930
bool __cdecl SetLightDiffuseColor(int light, uint32_t rgba)
{
    World().colours[light] = rgba;
    return UpdateLightColor(light);
}

// 0x0048E950
uint32_t* __cdecl GetLightDiffuseColor(uint32_t* out, int light)
{
    GetLightColor(out, light);
    return out;
}

// 0x0048ED70
bool __cdecl SetLightDirection(int light, const Mth::Vector* direction)
{
    World().directions[light] = *direction;
    return UpdateLightDirection(light);
}

// 0x0048E970
void __cdecl SetAmbientLightModulationFactor(float low, float high)
{
    World().ambientLow = low;
    World().ambientHigh = high;
}

// 0x0048E990
void __cdecl SetDiffuseLightModulationFactor(int light, float low, float high)
{
    World().lightLow[light] = low;
    World().lightHigh[light] = high;
}

// 0x0048E9B0
float __cdecl GetAmbientLightModulationFactor(bool low)
{
    return low ? World().ambientLow : World().ambientHigh;
}

// 0x0048E9D0 (both branches read the low ranges, as the original)
float __cdecl GetDiffuseLightModulationFactor(int light, bool low)
{
    if (low)
        return World().lightLow[light];
    return World().lightLow[light];
}

// 0x0048EA00
void __cdecl PushWorldLights()
{
    if (++StackTop() < kStackDepth)
        Stack()[StackTop()] = World();
    else
        --StackTop();
}

// 0x0048EA40
bool __cdecl PopWorldLights()
{
    if (StackTop() < 0)
        return false;
    World() = Stack()[StackTop()];
    --StackTop();
    CompiledOut();
    return true;
}

// 0x0048EDB0
bool __cdecl SetWorldBrightness(float target)
{
    constexpr float kStep = 0.02f; // 0x006492BC
    BrightnessTarget() = target;
    float& brightness = Brightness();
    if (brightness < target) {
        double moved = static_cast<double>(brightness) + kStep; // (compared before it's rounded)
        brightness = static_cast<float>(moved);
        if (moved > target)
            brightness = target;
    } else if (brightness > target) {
        double moved = static_cast<double>(brightness) - kStep;
        brightness = static_cast<float>(moved);
        if (moved < target)
            brightness = target;
    }
    AmbientIntensity() = Scaled(brightness, World().ambientLow, World().ambientHigh);
    if (AmbientIntensity() < MinIntensity())
        AmbientIntensity() = MinIntensity();
    for (uint32_t i = 0; i < LightCount(); ++i) {
        Intensities()[i] = Scaled(brightness, World().lightLow[i], World().lightHigh[i]);
        if (Intensities()[i] < MinIntensity())
            Intensities()[i] = MinIntensity();
    }
    UpdateAllLights();
    return true;
}

// 0x0048EF00
void __cdecl DeleteDynamicLights()
{
    for (int i = 0; i < DynamicLightCount(); ++i) {
        if (DynamicLights()[i]) {
            Mem::Manager::Get()->Free(DynamicLights()[i]);
            DynamicLights()[i] = nullptr;
        }
    }
    DynamicLightCount() = 0;
}

// 0x0048EF50
CDynamicLightBase** __cdecl FindDynamicLight(uint32_t owner)
{
    for (int i = 0; i < DynamicLightCount(); ++i) {
        CDynamicLightBase* light = DynamicLights()[i];
        if (light && light->owner == owner)
            return &DynamicLights()[i];
    }
    return nullptr;
}

// 0x0048EF90
uint32_t __cdecl GetDynamicLightsNear(const Mth::Vector* position, uint32_t max, CDynamicLightBase** out)
{
    float* falloffs = NearFalloffs();
    int* indexes = NearIndexes();
    uint32_t count = 0;
    for (int i = 0; i < DynamicLightCount(); ++i) {
        CDynamicLightBase* light = DynamicLights()[i]; // (not checked for null, as the original)
        if (!(light->intensity > 0.0f) || !light->owner)
            continue;
        double dx = static_cast<double>(position->x) - light->pos[0];
        double dy = static_cast<double>(position->y) - light->pos[1];
        double dz = static_cast<double>(position->z) - light->pos[2];
        double distanceSquared = dz * dz + dy * dy + dx * dx;
        double radius = light->radius;
        if (distanceSquared > radius * radius) // (NaN: within reach)
            continue;
        auto distance = static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(distanceSquared))));
        double falloff = CallFalloff(light, distance);
        if (count == 0) {
            falloffs[0] = static_cast<float>(falloff);
            indexes[0] = i;
            out[0] = light;
            count = 1;
            continue;
        }
        if (count >= max && !(falloff < falloffs[count - 1])) // (full: only below the last; NaN: not)
            continue;
        // The original's walk: inserted before the first it's greater than; each one it isn't greater than appends it
        // at the end (while there's room) - the end moving on with it.
        for (uint32_t j = 0; j < count; ++j) {
            if (falloff > falloffs[j]) {
                // the entries from j on moved up one by swaps (the one past the end - out[count] too - written)
                for (uint32_t k = count; k > j; --k) {
                    CDynamicLightBase* l = out[k];
                    out[k] = out[k - 1];
                    out[k - 1] = l;
                    float f = falloffs[k];
                    falloffs[k] = falloffs[k - 1];
                    falloffs[k - 1] = f;
                    int n = indexes[k];
                    indexes[k] = indexes[k - 1];
                    indexes[k - 1] = n;
                }
                falloffs[j] = static_cast<float>(falloff);
                indexes[j] = i;
                out[j] = light;
                if (count < max)
                    ++count;
                break;
            }
            if (count < max) {
                falloffs[count] = static_cast<float>(falloff);
                indexes[count] = i;
                out[count] = light;
                ++count;
            }
        }
    }
    return count;
}

// 0x0048F160
CDynamicLightBase* __cdecl CreateDynamicLight(int type)
{
    if (type != 1)
        return nullptr;
    auto* light = static_cast<CDynamicLightBase*>(Mem::Calloc(sizeof(CDynamicLightBase)));
    if (!light)
        return nullptr;
    light->unknown2C = false;
    light->vtable = reinterpret_cast<void**>(kDynamicLightVtable);
    light->type = 1;
    light->radius = 50.0f;
    light->inverseRadius = 50.0f; // (as the original: not 1 / 50)
    return light;
}

// 0x0048FB80
CDynamicLightBase* __cdecl NewDynamicLight(int type)
{
    int count = DynamicLightCount();
    for (int i = 0; i < count; ++i) {
        CDynamicLightBase* light = DynamicLights()[i];
        if (!light) {
            DynamicLights()[i] = CreateDynamicLight(type);
            return DynamicLights()[i];
        }
        if (!light->owner)
            return DynamicLights()[i];
    }
    if (count >= kMaxDynamicLights || DynamicLights()[count])
        return nullptr;
    DynamicLightCount() = count + 1;
    DynamicLights()[count] = CreateDynamicLight(type);
    return DynamicLights()[count];
}

// 0x0048E8C0
int CDynamicLightBase::IsOn()
{
    return intensity > 0.0f ? 1 : 0;
}

// 0x0048EA90
int CDynamicLightBase::InReach(float distance)
{
    return distance < radius ? 1 : 0;
}

// 0x0048EAB0
double CDynamicLightBase::Falloff(float distance)
{
    return static_cast<double>(distance) * inverseRadius;
}

} // namespace Nx

REPLACE_SOURCE_ONLY(0x0048EAC0, &Nx::sWorldLights::Construct); // (not a Ghidra function; a static initialiser's)
REPLACE(0x0048EB00, &Nx::sWorldLights::FreeArrays);
REPLACE(0x0048EB70, Nx::InitLightManager);
REPLACE(0x0048FB60, Nx::UninitLightManager);
REPLACE(0x0048E8F0, Nx::SetLightAmbientColor);
REPLACE(0x0048E900, Nx::GetLightAmbientColor);
REPLACE(0x0048E930, Nx::SetLightDiffuseColor);
REPLACE(0x0048E950, Nx::GetLightDiffuseColor);
REPLACE(0x0048ED70, Nx::SetLightDirection);
REPLACE(0x0048E970, Nx::SetAmbientLightModulationFactor);
REPLACE(0x0048E990, Nx::SetDiffuseLightModulationFactor);
REPLACE(0x0048E9B0, Nx::GetAmbientLightModulationFactor);
REPLACE(0x0048E9D0, Nx::GetDiffuseLightModulationFactor);
REPLACE(0x0048EA00, Nx::PushWorldLights);
REPLACE(0x0048EA40, Nx::PopWorldLights);
REPLACE(0x0048EDB0, Nx::SetWorldBrightness);
REPLACE(0x0048EF00, Nx::DeleteDynamicLights);
REPLACE(0x0048EF50, Nx::FindDynamicLight);
REPLACE(0x0048EF90, Nx::GetDynamicLightsNear);
REPLACE(0x0048F160, Nx::CreateDynamicLight);
REPLACE(0x0048FB80, Nx::NewDynamicLight);
REPLACE(0x0048E8C0, &Nx::CDynamicLightBase::IsOn);
REPLACE(0x0048EA90, &Nx::CDynamicLightBase::InReach);
REPLACE(0x0048EAB0, &Nx::CDynamicLightBase::Falloff);
REPLACE_SOURCE_ONLY(0x0048E8E0, Nx::ResetLights);      // a jump to 0x004B6940
REPLACE_SOURCE_ONLY(0x0048EA80, Nx::CreateModelLights); // a jump to 0x004B6B10
