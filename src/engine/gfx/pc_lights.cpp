// Lighting on PC: see pc_lights.h. Colours follow the original's x87 code: a byte times float factors, kept in
// double precision, rounded once when stored.

#include "engine/gfx/pc_lights.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/raw_view.h"

namespace render {

namespace {
maths::Vec4 g_scene_light_direction;
uint32_t g_scene_light_direction_guard;
} // namespace
std::array<float, 28> g_light_constants;

namespace {

constexpr double kPer128 = 1.0 / 128.0;  // 0x0064A2FC
constexpr float kPer255 = 1.0f / 255.0f; // 0x00645D70 (0x3B808081)

// The renderer's light constants.
float* ambient_constant() { return g_light_constants.data(); }
float* light_direction_constant(uint32_t light) { return g_light_constants.data() + 4 + 8 * light; }
float* light_color_constant(uint32_t light) { return g_light_constants.data() + 8 + 8 * light; }

// The scene's lights.
uint32_t& scene_ambient() { return g_active_lighting.ambient; }
float scene_scale() { return g_ambient_intensity; }
maths::Vec4* scene_directions() { return g_active_lighting.directions; } // (0x0067DC9C)
uint32_t* scene_colors() { return g_active_lighting.colours; }           // (0x0067DCA0)
float* scene_intensities() { return g_intensities; }
uint32_t light_count() { return g_light_count; }
uint32_t model_light_count() { return g_model_light_count; }

// A colour's r, g, b bytes times `factor` and 1/128 into three floats.
void send_color(float* out, uint32_t rgba, float factor)
{
    for (int k = 0; k < 3; ++k)
        out[k] = static_cast<float>(static_cast<double>((rgba >> (8 * k)) & 0xFF) * factor * kPer128);
}
void send_negated(float* out, const maths::Vec4& v)
{
    out[0] = -v.x;
    out[1] = -v.y;
    out[2] = -v.z;
}

// A dynamic light's falloff at `distance` (its vtable's slot 2).
double get_falloff(DynamicLightBase* light, float distance) { return light->get_falloff(distance); }

// The dynamic lights within `radius` of `position` (the engine's 0x0048EF90): their count, pointers into `out`.
uint32_t find_dynamic_lights(const maths::Vec4* position, uint32_t radius, DynamicLightBase** out)
{
    return get_dynamic_lights_near(position, radius, out);
}

} // namespace

// 0x004B6270
PcModelLights::PcModelLights()
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    colors = static_cast<uint32_t*>(memory::alloc(4 * model_light_count()));
    directions = static_cast<maths::Vec4*>(memory::alloc(16 * model_light_count()));
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    switch_ambient_light(false);
    for (uint32_t i = 0; i < model_light_count(); ++i) {
        switch_diffuse_light(static_cast<int>(i), false);
        directions[i] = {0.0f, 1.0f, 0.0f, 1.0f};
    }
    first_dynamic_ = 2;
}

// 0x004B6360
PcModelLights::~PcModelLights()
{
    if (colors) {
        memory::Hub::get_pull()->dealloc(colors);
        colors = nullptr;
    }
    if (directions) {
        memory::Hub::get_pull()->dealloc(directions);
        directions = nullptr;
    }
}

// 0x004B6160
bool PcModelLights::put_ambient_light_tint(const uint32_t* color)
{
    ambient = *color;
    return true;
}

// 0x004B6170
uint32_t* PcModelLights::read_ambient_light_tint(uint32_t* out)
{
    *out = ambient;
    return out;
}

// 0x004B6780
bool PcModelLights::put_light_aim(int light, const maths::Vec4* direction)
{
    directions[light] = *direction;
    return true;
}

// 0x004B6180
const maths::Vec4* PcModelLights::read_light_aim(int light)
{
    if (diffuse_light_is_on(light))
        return &directions[light];
    return render::get_light_direction(light);
}

// 0x004B61B0
bool PcModelLights::put_diffuse_light_tint(int light, const uint32_t* color)
{
    colors[light] = *color;
    return true;
}

// 0x004B61D0
uint32_t* PcModelLights::read_diffuse_light_tint(uint32_t* out, int light)
{
    *out = colors[light];
    return out;
}

// 0x004B63B0
void PcModelLights::plat_apply_scene_lights(const maths::Vec4* position, uint32_t radius)
{
    uint32_t color = 0;
    if (overrides & 1)
        color = ambient;
    else
        fetch_ambient_hue(&color);
    send_color(ambient_constant(), color, size_factor_);
    for (uint32_t i = 0; i < light_count(); ++i) {
        if (overrides & (2u << (i & 31))) {
            send_color(light_color_constant(i), colors[i], intensities_[i]);
            send_negated(light_direction_constant(i), directions[i]);
        } else {
            fetch_light_tint(&color, static_cast<int>(i));
            send_color(light_color_constant(i), color, intensities_[i]);
            send_negated(light_direction_constant(i), *render::get_light_direction(static_cast<int>(i)));
        }
    }
    if (!radius)
        return;
    std::array<DynamicLightBase*, 32> dynamic{};
    const uint32_t count = find_dynamic_lights(position, radius, dynamic.data());
    if (!count) {
        float* off = light_color_constant(first_dynamic_);
        off[0] = off[1] = off[2] = 0.0f;
        return;
    }
    for (uint32_t j = 0; j < count; ++j) {
        DynamicLightBase* light = dynamic[j];
        const float* at = light->pos.data();
        double dx = static_cast<double>(position->x) - at[0];
        double dy = static_cast<double>(position->y) - at[1];
        const double dz = static_cast<double>(position->z) - at[2];
        auto distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
        const double falloff = get_falloff(light, distance);
        // the direction from the light, normalised (unless its length isn't above 0)
        dx = static_cast<double>(position->x) - at[0];
        dy = static_cast<double>(position->y) - at[1];
        auto zf = static_cast<float>(static_cast<double>(position->z) - at[2]);
        const double length = std::sqrt(dx * dx + static_cast<double>(zf) * zf + dy * dy);
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            dx *= inverse;
            dy *= inverse;
            zf = static_cast<float>(zf * inverse);
        }
        float* direction = light_direction_constant(first_dynamic_ + j);
        direction[0] = static_cast<float>(dx);
        direction[1] = static_cast<float>(dy);
        direction[2] = zf;
        if (light->type == 1) {
            const double factor = (1.0 - falloff) * light->intensity * kPer255;
            float* out = light_color_constant(first_dynamic_ + j);
            const auto* colour = raw::bytes(&light->colour); // (its r, g, b bytes)
            for (int k = 0; k < 3; ++k)
                out[k] = static_cast<float>(colour[k] * factor);
        }
    }
}

// 0x004B61F0
void PcModelLights::switch_ambient_light(bool on)
{
    if (on)
        overrides |= 1;
    else
        overrides &= ~1u;
}

// 0x004B6240
bool PcModelLights::ambient_light_is_on() { return (overrides & 1) != 0; }

// 0x004B6210
void PcModelLights::switch_diffuse_light(int light, bool on)
{
    const uint32_t bit = 2u << (light & 31);
    if (on)
        overrides |= bit;
    else
        overrides &= ~bit;
}

// 0x004B6250
bool PcModelLights::diffuse_light_is_on(int light) { return (overrides & (2u << (light & 31))) != 0; }

// 0x004B67E0
bool update_ambient_light()
{
    send_color(ambient_constant(), scene_ambient(), scene_scale());
    return true;
}

// 0x004B6850
uint32_t* fetch_ambient_hue(uint32_t* out)
{
    *out = scene_ambient();
    return out;
}

// 0x004B6860
bool update_light_color(int light)
{
    send_color(light_color_constant(light), scene_colors()[light], scene_intensities()[light]);
    return true;
}

// 0x004B68E0
uint32_t* fetch_light_tint(uint32_t* out, int light)
{
    *out = scene_colors()[light];
    return out;
}

// 0x004B6900
void update_all_lights()
{
    update_ambient_light();
    for (int i = 0; i < 31; ++i)
        update_light_color(i);
}

// 0x004B6920
bool delete_model_lights(PcModelLights* lights)
{
    delete lights;
    return true;
}

// 0x004B6940
void reset_lights()
{
    send_color(ambient_constant(), scene_ambient(), scene_scale());
    for (int i = 0; i < 2; ++i) {
        uint32_t color = 0;
        fetch_light_tint(&color, i);
        send_color(light_color_constant(i), color, scene_intensities()[i]);
        send_negated(light_direction_constant(i), *get_light_direction(i));
    }
    float* third = light_color_constant(2);
    third[0] = third[1] = third[2] = 0.0f;
}

// 0x004B6A70
bool update_light_direction(int light)
{
    send_negated(light_direction_constant(light), scene_directions()[light]);
    return true;
}

// 0x004B6AB0
maths::Vec4* get_light_direction(int light)
{
    g_scene_light_direction_guard |= 1; // (the static's initialisation guard)
    maths::Vec4& out = g_scene_light_direction;
    const maths::Vec4& direction = scene_directions()[light];
    out = {direction.x, direction.y, direction.z, 1.0f};
    return &out;
}

// 0x004B6B10
gsl::owner<PcModelLights*> spawn_model_lights() { return new PcModelLights; }

} // namespace render
