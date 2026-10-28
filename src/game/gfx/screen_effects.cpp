// The engine's screen effects (see screen_effects.h).

#include "game/gfx/screen_effects.h"

#include "game/core/mem.h"
#include "game/script/struct.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kScreenEffectsVtable = 0x00649A0C;

constexpr uint32_t kOn = 0xF649D637;
constexpr uint32_t kBloomAlpha1 = 0xF68014D9, kBloomAlpha2 = 0x6F894563;
constexpr uint32_t kColour = 0x29461E27, kBloomCutOff = 0x99A9B716;
constexpr uint32_t kDofFloatA = 0xE568F442, kDofFloatB = 0x3B909E50, kDofFloatD = 0xB7462B1D;
constexpr uint32_t kDofColourA = 0x0F3BF443, kDofColourB = 0xFC5C7D61;

// The platform's slot `slot` with one argument.
template <typename A>
void Platform(CScreenEffects* effects, int slot, A argument)
{
    reinterpret_cast<void(__thiscall*)(CScreenEffects*, A)>(effects->vtable[slot])(effects, argument);
}

// An [r g b] array as RGBA bytes, alpha 0 (each element's low byte).
uint32_t ArrayColour(Script::CArray* array)
{
    uint8_t r = static_cast<uint8_t>(array->GetElement(0));
    uint8_t g = static_cast<uint8_t>(array->GetElement(1));
    uint8_t b = static_cast<uint8_t>(array->GetElement(2));
    return r | (g << 8) | (b << 16);
}

} // namespace

// 0x0049E2F0
CScreenEffects* CScreenEffects::Construct()
{
    vtable = reinterpret_cast<void**>(kScreenEffectsVtable);
    m_on = true;
    return this;
}

// 0x0049E300
void CScreenEffects::DestroyBody() { vtable = reinterpret_cast<void**>(kScreenEffectsVtable); }

// 0x0049E330
CScreenEffects* CScreenEffects::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049E320
bool CScreenEffects::SetOn(Script::CStruct*, Script::CScript*)
{
    m_on = true;
    return true;
}

// 0x0049E310
bool CScreenEffects::SetOff(Script::CStruct*, Script::CScript*)
{
    m_on = false;
    return true;
}

// 0x0049E360
bool CScreenEffects::SetBloomParams(Script::CStruct* params, int)
{
    int on = -1;
    int alpha1 = -1;
    int alpha2 = -1;
    if (params->GetInteger(kOn, &on, 0))
        Platform(this, 2, on);
    params->GetInteger(kBloomAlpha1, &alpha1, 0);
    params->GetInteger(kBloomAlpha2, &alpha2, 0);
    if (alpha1 != -1 || alpha2 != -1)
        reinterpret_cast<void(__thiscall*)(CScreenEffects*, int, int)>(vtable[4])(this, alpha1, alpha2);
    Script::CArray* array = nullptr;
    if (params->GetArray(kColour, &array, 0))
        Platform(this, 5, ArrayColour(array));
    array = nullptr;
    if (params->GetArray(kBloomCutOff, &array, 0))
        Platform(this, 3, ArrayColour(array));
    return true;
}

// 0x0049E4B0 (the first float is uninitialised in the original when it isn't given; 0 here - only slot 9, which
// ignores its argument on PC, would see it)
bool CScreenEffects::SetDOFParams(Script::CStruct* params, Script::CScript*)
{
    int on;
    if (params->GetInteger(kOn, &on, 0))
        Platform(this, 6, on);
    float a = 0.0f;
    if (params->GetFloat(kDofFloatA, &a, 0))
        Platform(this, 10, a);
    float b;
    if (params->GetFloat(kDofFloatB, &b, 0))
        Platform(this, 9, a); // (the first float's: the original passes the wrong variable)
    float c;
    if (params->GetFloat(kColour, &c, 0))
        Platform(this, 11, c);
    float d;
    if (params->GetFloat(kDofFloatD, &d, 0))
        Platform(this, 12, d);
    Script::CArray* array;
    if (params->GetArray(kDofColourA, &array, 0))
        Platform(this, 8, ArrayColour(array));
    if (params->GetArray(kDofColourB, &array, 0))
        Platform(this, 7, ArrayColour(array));
    return true;
}

} // namespace Nx

REPLACE(0x0049E2F0, &Nx::CScreenEffects::Construct);
REPLACE(0x0049E300, &Nx::CScreenEffects::DestroyBody);
REPLACE(0x0049E330, &Nx::CScreenEffects::Destroy);
REPLACE(0x0049E320, &Nx::CScreenEffects::SetOn);
REPLACE(0x0049E310, &Nx::CScreenEffects::SetOff);
REPLACE(0x0049E360, &Nx::CScreenEffects::SetBloomParams);
REPLACE(0x0049E4B0, &Nx::CScreenEffects::SetDOFParams);
