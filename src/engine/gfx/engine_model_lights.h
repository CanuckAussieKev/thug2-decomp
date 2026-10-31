#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"

namespace maths {
class Vec4;
}

// The engine's model lights (render::CModelLights - the platform-independent part of the PC model lights, PcModelLights
// in pc_lights.h, its first 0x1C bytes; vtable 0x006491DC): a brightness moving towards a target, from which a scale
// for the ambient colour and an intensity for each light follow (the light manager's ranges, 0x0067DCA4 - 0x0067DCB0:
// one below and one above the middle brightness 0.5), and the position the lights are for. The base's slots [1] -
// [12] are stubs ("STUB: CModelLights::...") that PcModelLights replaces.
namespace render {

class ModelLights : public memory::HeapObject {
public:
    // 0x0048E5B0: brightness 0, scale 1, an intensity of 1 for each of the manager's lights (0x006FAAE0).
    ModelLights();
    // 0x0048E630 (0x0048E7F0 the scalar deleting destructor, vtable slot 0): the intensities freed.
    virtual ~ModelLights();
    ModelLights(const ModelLights&) = delete;
    ModelLights& operator=(const ModelLights&) = delete;

    // The platform's (vtable slots 1-12; the base's print their "STUB: CModelLights::...\n"): the ambient colour, a
    // light's direction and colour set and read, the renderer's lights for a position, the brightness applied, and
    // whether the ambient colour / a light is the model's own.
    virtual bool put_ambient_light_tint(const uint32_t* color);                         // 0x0048E4E0 [1]: false
    virtual bool put_light_aim(int light, const maths::Vec4* direction);                // 0x0048E500 [2]: false
    virtual bool put_diffuse_light_tint(int light, const uint32_t* color);              // 0x0048E520 [3]: false
    virtual uint32_t* read_ambient_light_tint(uint32_t* out);                           // 0x0048E730 [4]: 0
    virtual const maths::Vec4* read_light_aim(int light);                               // 0x0048E760 [5]: a 0 vector
    virtual uint32_t* read_diffuse_light_tint(uint32_t* out, int light);                // 0x0048E7C0 [6]: 0
    virtual void plat_apply_scene_lights(const maths::Vec4* position, uint32_t radius); // 0x0048E5A0 [7]
    virtual void apply_light_brightness(bool ambient, bool diffuse);                    // 0x0048E540 [8]
    virtual void switch_ambient_light(bool on);                                         // 0x0048E550 [9]
    virtual void switch_diffuse_light(int light, bool on);                              // 0x0048E560 [10]
    virtual bool ambient_light_is_on();                                                 // 0x0048E570 [11]: false
    virtual bool diffuse_light_is_on(int light); // 0x0048E580 [12]: false (not a Ghidra function)

    // 0x0048E450: the ambient colour set (slot 1), then UpdateBrightness(1, 0) (slot 8). [1]'s result.
    bool change_ambient_tint(const uint32_t* color);
    // 0x0048E480: a light's colour set (slot 3), then UpdateBrightness(0, 1). [3]'s result.
    bool change_diffuse_hue(int light, const uint32_t* color);
    // 0x0048E4D0: the position the lights are for. True.
    bool bind_position_source(const maths::Vec4* position);
    // 0x0048E660: the scale and the intensities from the brightness (0 to 1: 2b - 1 times the dark range below 0.5 and
    // at it, else the bright one, plus 1; nothing below 0), then UpdateBrightness(1, 1).
    void get_update_brightness();
    // 0x0048E830: the brightness moved `step` towards `target` (not past it); the target kept. True.
    bool move_brightness(float target, float step);
    // 0x0048E8A0: the brightness (and the target) set. True.
    bool change_brightness(float brightness);

    uint32_t dynamic_radius_;      // +0x04: how far dynamic lights reach it (the instance's lights pass it)
    float brightness_;             // +0x08
    float intensity_goal_;         // +0x0C
    float size_factor_;            // +0x10: the ambient colour's
    float* intensities_;           // +0x14: each light's
    const maths::Vec4* place_ref_; // +0x18
};
static_assert(offsetof(ModelLights, intensities_) == 0x14 && sizeof(ModelLights) == 0x1C,
              "layout must match the original");

extern uint32_t g_light_count;

} // namespace render
