#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/engine_model_lights.h"
#include "base/maths/vec4.h"
#include <gsl/pointers>
#include <array>

// Lighting on PC (Nx engine): the scene's lights (the engine's ambient colour 0x0067DC98, its scale 0x0067DC94, the
// directional lights' directions 0x0067DC9C and colours 0x0067DCA0, their intensities 0x006FAAEC) and the PC model
// lights - a model's own ambient / light colours and directions where it overrides the scene's, plus the nearby
// dynamic lights - all sent to the renderer's light constants: the ambient colour (0x00786F38) and 0x20 bytes per
// light (0x00786F48: a direction, then a colour at +0x10), colours as bytes times the scale times 1/128.
namespace render {

constexpr std::size_t kPcModelLightsOverridesOffset = 0x1C;
constexpr std::size_t kPcModelLightsDirectionsOffset = 0x2C;
constexpr std::size_t kPcModelLightsBytes = 0x30;

// The PC model lights (0x30 bytes, vtable 0x0064A440; the engine's ModelLights - its scale and each light's
// intensity - with the model's own colours and directions).
class PcModelLights final : public ModelLights {
public:
    PcModelLights(const PcModelLights&) = delete;
    PcModelLights& operator=(const PcModelLights&) = delete;
    // 0x004B6270: nothing overridden, every direction (0, 1, 0).
    PcModelLights();
    // 0x004B6360 (0x004B67B0 the scalar deleting destructor): the colours and directions freed, then the engine's.
    ~PcModelLights() override;
    // 0x004B6160 / 0x004B6170 (vtable slots 1 / 4): the ambient colour. Set returns true; Get returns out.
    bool put_ambient_light_tint(const uint32_t* color) override;
    uint32_t* read_ambient_light_tint(uint32_t* out) override;
    // 0x004B6780 / 0x004B6180 (slots 2 / 5): a light's direction (the scene's unless overridden). Set returns true.
    bool put_light_aim(int light, const maths::Vec4* direction) override;
    const maths::Vec4* read_light_aim(int light) override;
    // 0x004B61B0 / 0x004B61D0 (slots 3 / 6): a light's colour. Set returns true; Get returns out.
    bool put_diffuse_light_tint(int light, const uint32_t* color) override;
    uint32_t* read_diffuse_light_tint(uint32_t* out, int light) override;
    // 0x004B63B0 (slot 7): the renderer's light constants for something at `position`: the ambient colour, each
    // light (overridden or the scene's; directions negated), then - with `radius` - the dynamic lights near it
    // (0x0048EF90; their directions from them to the position, type 1 coloured by 1 - their falloff), or the first
    // dynamic light's colour cleared when there are none.
    void plat_apply_scene_lights(const maths::Vec4* position, uint32_t radius) override;
    // (Slot 8: a shared empty function.)
    void apply_light_brightness(bool /*ambient*/, bool /*diffuse*/) override {}
    // 0x004B61F0 / 0x004B6240 (slots 9 / 11): whether the ambient colour is overridden.
    void switch_ambient_light(bool on) override;
    bool ambient_light_is_on() override;
    // 0x004B6210 / 0x004B6250 (slots 10 / 12): whether a light is overridden.
    void switch_diffuse_light(int light, bool on) override;
    bool diffuse_light_is_on(int light) override;

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t* colors() const { return colors_; }
    [[nodiscard]] maths::Vec4* directions() const { return directions_; }

private:
    uint32_t overrides{0};   // +0x1C: bit 0 the ambient colour, bit 1 + n light n's
    uint32_t first_dynamic_; // +0x20: the renderer light the dynamic lights start at (2)
    uint32_t ambient{};      // +0x24: RGBA
    uint32_t* colors_;        // +0x28: one per light (0x006FAAE4 of them), RGBA
    maths::Vec4* directions_; // +0x2C
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PcModelLights, overrides) == kPcModelLightsOverridesOffset && offsetof(PcModelLights, directions_) == kPcModelLightsDirectionsOffset &&
                          sizeof(PcModelLights) == kPcModelLightsBytes,
                      "layout must match the original");
    }
};

// 0x004B67E0: the scene's ambient colour sent to the renderer. True.
bool update_ambient_light();
// 0x004B6850: the scene's ambient colour into *out. Returns out.
uint32_t* fetch_ambient_hue(uint32_t* out);
// 0x004B6860: a scene light's colour (times its intensity) sent to the renderer. True.
bool update_light_color(int light);
// 0x004B68E0: a scene light's colour into *out. Returns out.
uint32_t* fetch_light_tint(uint32_t* out, int light);
// 0x004B6900: the ambient colour and the colours of lights 0-30 sent.
void update_all_lights();
// 0x004B6920: model lights deleted (virtually; none: nothing). True.
bool delete_model_lights(gsl::owner<PcModelLights*> lights);
// 0x004B6940: the renderer's lights back to the scene's - ambient, lights 0 and 1 - and light 2's colour cleared.
void reset_lights();
// 0x004B6A70: a scene light's direction (negated) sent to the renderer. True.
bool update_light_direction(int light);
// 0x004B6AB0: a scene light's direction (w 1; a static vector, 0x0070166C).
maths::Vec4* get_light_direction(int light);
// 0x004B6B10: new model lights (0x30 bytes).
gsl::owner<PcModelLights*> spawn_model_lights();

// The ambient colour (4 floats), then 8 per light: a direction and a colour (the two scene lights the dynamic lights
// start at and up to 32 dynamic ones).

} // namespace render
