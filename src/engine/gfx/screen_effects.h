#pragma once

#include <cstdint>
#include <array>

#include "engine/core/mem.h"

namespace script {
class Struct;
class Script;
} // namespace script

// The engine's screen effects (render::ScreenEffects, 8 bytes; vtable 0x00649A0C - the PC's PcScreenEffects,
// pc_scene.h, fills in its slots): on / off, and the bloom and depth-of-field settings from script structures, each
// handed to the platform through a virtual slot.
namespace render {

class ScreenEffects : public memory::HeapObject {
public:
    // 0x0049E2F0: on.
    ScreenEffects() = default;
    // 0x0049E300 (0x0049E330 the scalar deleting destructor, vtable slot 0)
    virtual ~ScreenEffects() = default;
    ScreenEffects(const ScreenEffects&) = delete;
    ScreenEffects& operator=(const ScreenEffects&) = delete;

    // The platform's (vtable slots 2-12, pure in the engine's table; slot 1 - pure, the PC's empty - is never called
    // and left out): the bloom on, its cut-off colour, its passes' alphas, its colour; the depth of field on, its
    // colours and values (named by the script parameters they come from: a 0x0F3BF443 / 0xE568F442, b 0xFC5C7D61 /
    // 0x3B909E50, c 0x29461E27, d 0xB7462B1D).
    virtual void set_bloom(int on) = 0;
    virtual void set_bloom_cut_off(uint32_t colour) = 0;
    virtual void set_bloom_alphas(int first, int second) = 0;
    virtual void set_bloom_colour(uint32_t colour) = 0;
    virtual void set_dof_on(int on) = 0;
    virtual void set_dof_colour_b(uint32_t colour) = 0;
    virtual void set_dof_colour_a(uint32_t colour) = 0;
    virtual void set_dof_value_b(float value) = 0;
    virtual void set_dof_value_a(float value) = 0;
    virtual void set_dof_value_c(float value) = 0;
    virtual void set_dof_value_d(float value) = 0;

    // 0x0049E320 / 0x0049E310: the SetScreenEffectsOn / Off script commands' (the structure and script unused).
    // True.
    bool set_on(script::Struct* params, script::Script* script);
    bool set_off(script::Struct* params, script::Script* script);
    // 0x0049E360: the bloom from a structure (the time of day's): 0xF649D637 on (slot 2), 0xF68014D9 / 0x6F894563
    // the passes' alphas (slot 4, when either is given; -1 for the other), 0x29461E27 a colour (slot 5) and
    // 0x99A9B716 the cut-off colour (slot 3) - colours as [r g b] arrays, alpha 0. The second argument is unused.
    // True.
    bool set_bloom_params(script::Struct* params, int unused);
    // 0x0049E4B0: the depth of field (SetScreenEffectsDOFParams): 0xF649D637 on (slot 6), floats 0xE568F442 (slot
    // 10), 0x3B909E50 (slot 9 - given the first float's value, as the original), 0x29461E27 (slot 11), 0xB7462B1D
    // (slot 12), colours 0x0F3BF443 (slot 8) and 0xFC5C7D61 (slot 7). True.
    bool set_dof_params(script::Struct* params, script::Script* script);

private:
    bool on_{true}; // +0x04
    std::array<uint8_t, 3> spare_slot5_{};
};
static_assert(sizeof(ScreenEffects) == 8, "layout must match the original");

} // namespace render
