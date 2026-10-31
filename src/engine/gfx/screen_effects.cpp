// The engine's screen effects (see screen_effects.h).

#include <cstdint>

#include "engine/gfx/screen_effects.h"

#include "engine/core/mem.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"

namespace render {

namespace {

extern void** const kScreenEffectsVtable;

constexpr uint32_t kOn = 0xF649D637;
constexpr uint32_t kBloomAlpha1 = 0xF68014D9, kBloomAlpha2 = 0x6F894563;
constexpr uint32_t kColour = 0x29461E27, kBloomCutOff = 0x99A9B716;
constexpr uint32_t kDofFloatA = 0xE568F442, kDofFloatB = 0x3B909E50, kDofFloatD = 0xB7462B1D;
constexpr uint32_t kDofColourA = 0x0F3BF443, kDofColourB = 0xFC5C7D61;

// The platform's slot `slot` with one argument.
template <typename A>
void os_layer(ScreenEffects* effects, int slot, A argument)
{
    reinterpret_cast<void(__thiscall*)(ScreenEffects*, A)>(effects->vtable[slot])(effects, argument);
}

// An [r g b] array as RGBA bytes, alpha 0 (each element's low byte).
uint32_t array_colour(script::Array* array)
{
    uint8_t r = static_cast<uint8_t>(array->fetch_part(0));
    uint8_t g = static_cast<uint8_t>(array->fetch_part(1));
    uint8_t b = static_cast<uint8_t>(array->fetch_part(2));
    return r | (g << 8) | (b << 16);
}

} // namespace

// 0x0049E2F0
ScreenEffects* ScreenEffects::make_new()
{
    vtable = reinterpret_cast<void**>(kScreenEffectsVtable);
    on_ = true;
    return this;
}

// 0x0049E300
void ScreenEffects::destroy_body() { vtable = reinterpret_cast<void**>(kScreenEffectsVtable); }

// 0x0049E330
ScreenEffects* ScreenEffects::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x0049E320
bool ScreenEffects::set_on(script::Struct*, script::Script*)
{
    on_ = true;
    return true;
}

// 0x0049E310
bool ScreenEffects::set_off(script::Struct*, script::Script*)
{
    on_ = false;
    return true;
}

// 0x0049E360
bool ScreenEffects::set_bloom_params(script::Struct* params, int)
{
    int on = -1;
    int alpha1 = -1;
    int alpha2 = -1;
    if (params->fetch_int_value(kOn, &on, 0))
        os_layer(this, 2, on);
    params->fetch_int_value(kBloomAlpha1, &alpha1, 0);
    params->fetch_int_value(kBloomAlpha2, &alpha2, 0);
    if (alpha1 != -1 || alpha2 != -1)
        reinterpret_cast<void(__thiscall*)(ScreenEffects*, int, int)>(vtable[4])(this, alpha1, alpha2);
    script::Array* array = nullptr;
    if (params->fetch_list(kColour, &array, 0))
        os_layer(this, 5, array_colour(array));
    array = nullptr;
    if (params->fetch_list(kBloomCutOff, &array, 0))
        os_layer(this, 3, array_colour(array));
    return true;
}

// 0x0049E4B0 (the first float is uninitialised in the original when it isn't given; 0 here - only slot 9, which
// ignores its argument on PC, would see it)
bool ScreenEffects::set_dof_params(script::Struct* params, script::Script*)
{
    int on;
    if (params->fetch_int_value(kOn, &on, 0))
        os_layer(this, 6, on);
    float a = 0.0f;
    if (params->fetch_float(kDofFloatA, &a, 0))
        os_layer(this, 10, a);
    float b;
    if (params->fetch_float(kDofFloatB, &b, 0))
        os_layer(this, 9, a); // (the first float's: the original passes the wrong variable)
    float c;
    if (params->fetch_float(kColour, &c, 0))
        os_layer(this, 11, c);
    float d;
    if (params->fetch_float(kDofFloatD, &d, 0))
        os_layer(this, 12, d);
    script::Array* array;
    if (params->fetch_list(kDofColourA, &array, 0))
        os_layer(this, 8, array_colour(array));
    if (params->fetch_list(kDofColourB, &array, 0))
        os_layer(this, 7, array_colour(array));
    return true;
}

namespace {
static VtableSlot g_screen_effects_vtable_slots[] = {&render::ScreenEffects::teardown, &_purecall,
                &_purecall, &_purecall, &_purecall,
                &_purecall, &_purecall, &_purecall,
                &_purecall, &_purecall, &_purecall,
                &_purecall, &_purecall};
void** const kScreenEffectsVtable = reinterpret_cast<void**>(g_screen_effects_vtable_slots);
} // namespace

} // namespace Nx
