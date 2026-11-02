// The engine's model lights: see engine_model_lights.h. The brightness maths follows the original's x87 code
// (53-bit precision: double where it keeps values in registers, float where it stores them).

#include "engine/gfx/engine_model_lights.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

#include "engine/core/checked_index.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/light_manager.h"
#include "engine/maths/vec4.h"
#include "engine/core/no_destroy.h"

namespace render {

uint32_t g_light_count;

namespace {

// NOT ORIGINAL grouping - engine_model_lights.cpp's state (the original's globals). Made on first use, never destroyed
// (engine_model_lights_state()).
struct EngineModelLightsState {
    maths::Vec4 light_direction_{};
    uint32_t light_direction_guard_{};
};

EngineModelLightsState& engine_model_lights_state()
{
    static core::NoDestroy<EngineModelLightsState> state;
    return state.get();
}

// The light manager's: how many lights, the ambient ranges below / above the middle brightness, each light's.
uint32_t light_count() { return g_light_count; }
// (the world lights' fields: ambient_low_ 0x0067DCA4, ambient_high_, light_low_, light_high_)
float dark_range() { return g_active_lighting.ambient_low_; }
float bright_range() { return g_active_lighting.ambient_high_; }
std::span<float> dark_ranges() { return g_active_lighting.light_low_; }
std::span<float> bright_ranges() { return g_active_lighting.light_high_; }

void get_stub(const char* message) { debug::printf_text(message); }

// 2b - 1 times a range, plus 1 (the bright range above 0.5 - or NaN -, the dark one at and below).
float get_scaled(float brightness, float dark, float bright)
{
    const double range = brightness <= 0.5f ? dark : bright;
    return static_cast<float>((static_cast<double>(brightness) * 2.0 - 1.0) * range + 1.0);
}

} // namespace

// 0x0048E5B0
ModelLights::ModelLights()
    : intensities_(static_cast<float*>(memory::alloc(light_count() * 4)))
{
    std::ranges::fill(intensities(), 1.0f);
}

std::span<float> ModelLights::intensities() const { return {intensities_, light_count()}; }

// 0x0048E630
ModelLights::~ModelLights()
{
    if (intensities_) {
        memory::Hub::get_pull()->dealloc(intensities_);
        intensities_ = nullptr;
    }
}

// 0x0048E450
bool ModelLights::change_ambient_tint(const uint32_t* color)
{
    const bool result = put_ambient_light_tint(color);
    apply_light_brightness(true, false);
    return result;
}

// 0x0048E480
bool ModelLights::change_diffuse_hue(int light, const uint32_t* color)
{
    const bool result = put_diffuse_light_tint(light, color);
    apply_light_brightness(false, true);
    return result;
}

// 0x0048E4D0
bool ModelLights::bind_position_source(const maths::Vec4* position)
{
    place_ref_ = position;
    return true;
}

// 0x0048E660
void ModelLights::get_update_brightness()
{
    size_factor_ = get_scaled(brightness_, dark_range(), bright_range());
    if (size_factor_ < 0.0f)
        size_factor_ = 0.0f;
    for (uint32_t i = 0; i < light_count(); ++i) {
        float& intensity = core::at(intensities(), i);
        intensity = get_scaled(brightness_, core::at(dark_ranges(), i), core::at(bright_ranges(), i));
        if (intensity < 0.0f)
            intensity = 0.0f;
    }
    apply_light_brightness(true, true);
}

// 0x0048E830
bool ModelLights::move_brightness(float target, float step)
{
    intensity_goal_ = target;
    if (brightness_ < target) {
        const double moved = static_cast<double>(step) + brightness_; // (compared before it's rounded)
        brightness_ = static_cast<float>(moved);
        if (moved > target)
            brightness_ = target;
    } else if (brightness_ > target) {
        const double moved = static_cast<double>(brightness_) - step;
        brightness_ = static_cast<float>(moved);
        if (moved < target)
            brightness_ = target;
    }
    get_update_brightness();
    return true;
}

// 0x0048E8A0
bool ModelLights::change_brightness(float brightness)
{
    intensity_goal_ = brightness;
    brightness_ = brightness;
    get_update_brightness();
    return true;
}

// --- the base's platform calls ----------------------------------------------------------------------------------

// 0x0048E4E0
bool ModelLights::put_ambient_light_tint(const uint32_t*)
{
    get_stub("STUB: CModelLights::SetLightAmbientColor\n"); // "STUB: CModelLights::SetLightAmbientColor\n"
    return false;
}
// 0x0048E500
bool ModelLights::put_light_aim(int, const maths::Vec4*)
{
    get_stub("STUB: CModelLights::SetLightDirection\n"); // "STUB: CModelLights::SetLightDirection\n"
    return false;
}
// 0x0048E520
bool ModelLights::put_diffuse_light_tint(int, const uint32_t*)
{
    get_stub("STUB: CModelLights::SetLightDiffuseColor\n"); // "STUB: CModelLights::SetLightDiffuseColor\n"
    return false;
}
// 0x0048E730
uint32_t* ModelLights::read_ambient_light_tint(uint32_t* out)
{
    get_stub("STUB: CModelLights::GetLightAmbientColor\n"); // "STUB: CModelLights::GetLightAmbientColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048E760 (the vector 0x006FAACC, its guard 0x006FAADC: zeroed the first time)
const maths::Vec4* ModelLights::read_light_aim(int)
{
    get_stub("STUB: CModelLights::GetLightDirection\n"); // "STUB: CModelLights::GetLightDirection\n"
    uint32_t& guard = engine_model_lights_state().light_direction_guard_;
    auto* direction = &engine_model_lights_state().light_direction_;
    if (!(guard & 1)) {
        guard |= 1;
        std::memset(direction, 0, sizeof(maths::Vec4));
    }
    return direction;
}
// 0x0048E7C0
uint32_t* ModelLights::read_diffuse_light_tint(uint32_t* out, int)
{
    get_stub("STUB: CModelLights::GetLightDiffuseColor\n"); // "STUB: CModelLights::GetLightDiffuseColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048E5A0
void ModelLights::plat_apply_scene_lights(const maths::Vec4*, uint32_t)
{
    get_stub("STUB: CModelLights::plat_apply_scene_lights\n"); // "STUB: CModelLights::plat_apply_scene_lights\n"
}
// 0x0048E540
void ModelLights::apply_light_brightness(bool, bool)
{
    get_stub("STUB: CModelLights::UpdateBrightness\n");
} // "STUB: CModelLights::UpdateBrightness\n"
// 0x0048E550
void ModelLights::switch_ambient_light(bool)
{
    get_stub("STUB: CModelLights::EnableAmbientLight\n");
} // "STUB: CModelLights::EnableAmbientLight\n"
// 0x0048E560
void ModelLights::switch_diffuse_light(int, bool)
{
    get_stub("STUB: CModelLights::EnableDiffuseLight\n");
} // "STUB: CModelLights::EnableDiffuseLight\n"
// 0x0048E570
bool ModelLights::ambient_light_is_on()
{
    get_stub("STUB: CModelLights::IsAmbientLightEnabled\n"); // "STUB: CModelLights::IsAmbientLightEnabled\n"
    return false;
}
// 0x0048E580
bool ModelLights::diffuse_light_is_on(int)
{
    get_stub("STUB: CModelLights::IsDiffuseLightEnabled\n"); // "STUB: CModelLights::IsDiffuseLightEnabled\n"
    return false;
}

} // namespace render
