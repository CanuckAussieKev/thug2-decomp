// The engine's screen effects (see screen_effects.h).

#include <cstdint>

#include "engine/gfx/screen_effects.h"

#include "engine/script/param_struct.h"

namespace render {

constexpr int kGreenShift = 8;
constexpr int kBlueShift = 16;

namespace {

constexpr uint32_t kOn = 0xF649D637;
constexpr uint32_t kBloomAlpha1 = 0xF68014D9, kBloomAlpha2 = 0x6F894563;
constexpr uint32_t kColour = 0x29461E27, kBloomCutOff = 0x99A9B716;
constexpr uint32_t kDofFloatA = 0xE568F442, kDofFloatB = 0x3B909E50, kDofFloatD = 0xB7462B1D;
constexpr uint32_t kDofColourA = 0x0F3BF443, kDofColourB = 0xFC5C7D61;

// An [r g b] array as RGBA bytes, alpha 0 (each element's low byte).
uint32_t array_colour(script::Array* array)
{
    auto r = static_cast<uint8_t>(array->fetch_part(0));
    auto g = static_cast<uint8_t>(array->fetch_part(1));
    auto b = static_cast<uint8_t>(array->fetch_part(2));
    return r | (g << kGreenShift) | (b << kBlueShift);
}

} // namespace

// 0x0049E320
bool ScreenEffects::set_on(script::Struct* /*params*/, script::Script* /*script*/)
{
    on_ = true;
    return true;
}

// 0x0049E310
bool ScreenEffects::set_off(script::Struct* /*params*/, script::Script* /*script*/)
{
    on_ = false;
    return true;
}

// 0x0049E360
bool ScreenEffects::set_bloom_params(script::Struct* params, int /*unused*/)
{
    int on = -1;
    int alpha1 = -1;
    int alpha2 = -1;
    if (params->fetch_int_value(kOn, &on, 0))
        set_bloom(on);
    params->fetch_int_value(kBloomAlpha1, &alpha1, 0);
    params->fetch_int_value(kBloomAlpha2, &alpha2, 0);
    if (alpha1 != -1 || alpha2 != -1)
        set_bloom_alphas(alpha1, alpha2);
    script::Array* array = nullptr;
    if (params->fetch_list(kColour, &array, 0))
        set_bloom_colour(array_colour(array));
    array = nullptr;
    if (params->fetch_list(kBloomCutOff, &array, 0))
        set_bloom_cut_off(array_colour(array));
    return true;
}

// 0x0049E4B0 (the first float is uninitialised in the original when it isn't given; 0 here - only slot 9, which
// ignores its argument on PC, would see it)
bool ScreenEffects::set_dof_params(script::Struct* params, script::Script* /*script*/)
{
    int on = 0;
    if (params->fetch_int_value(kOn, &on, 0))
        set_dof_on(on);
    float a = 0.0F;
    if (params->fetch_float(kDofFloatA, &a, 0))
        set_dof_value_a(a);
    float b = 0.0F;
    if (params->fetch_float(kDofFloatB, &b, 0))
        set_dof_value_b(a); // (the first float's: the original passes the wrong variable)
    float c = 0.0F;
    if (params->fetch_float(kColour, &c, 0))
        set_dof_value_c(c);
    float d = 0.0F;
    if (params->fetch_float(kDofFloatD, &d, 0))
        set_dof_value_d(d);
    script::Array* array = nullptr;
    if (params->fetch_list(kDofColourA, &array, 0))
        set_dof_colour_a(array_colour(array));
    if (params->fetch_list(kDofColourB, &array, 0))
        set_dof_colour_b(array_colour(array));
    return true;
}

} // namespace render
