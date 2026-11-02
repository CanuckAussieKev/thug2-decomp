// Lighting on PC: see pc_lights.h. Colours follow the original's x87 code: a byte times float factors, kept in
// double precision, rounded once when stored.

#include "engine/gfx/pc_lights.h"

#include <cmath>
#include <span>
#include <utility>
#include <cstdint>
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"
#include "engine/core/packed_colour.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/light_sets.h"

namespace render {

namespace {
// NOT ORIGINAL grouping - pc_lights.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_lights_state()).
struct PcLightsState {
    maths::Vec4 scene_light_direction_{};
    uint32_t scene_light_direction_guard_{};
};

PcLightsState& pc_lights_state()
{
    static core::NoDestroy<PcLightsState> state;
    return state.get();
}

} // namespace

namespace {

constexpr double kPer128 = 1.0 / 128.0;  // 0x0064A2FC
constexpr float kPer255 = 1.0f / 255.0f; // 0x00645D70 (0x3B808081)

// The renderer's light constants.
std::array<float, 4 + 8 * (2 + 32)>& ambient_constant() { return light_sets().light_constants(); }
std::span<float, 3> light_direction_constant(uint32_t light)
{
    return std::span<float>(light_sets().light_constants()).subspan(4 + 8 * light).first<3>();
}
std::span<float, 3> light_color_constant(uint32_t light)
{
    return std::span<float>(light_sets().light_constants()).subspan(8 + 8 * light).first<3>();
}

// The scene's lights.
uint32_t& scene_ambient() { return light_sets().get_world().ambient; }

std::span<maths::Vec4> scene_directions() { return light_sets().get_world().directions; } // (0x0067DC9C)
std::span<uint32_t> scene_colors() { return light_sets().get_world().colours; }           // (0x0067DCA0)

// A colour's r, g, b bytes times `factor` and 1/128 into three floats.
void send_color(std::span<float, 3> out, uint32_t rgba, float factor)
{
    for (int k = 0; k < 3; ++k)
        core::at(out, k) = static_cast<float>(static_cast<double>((rgba >> (8 * k)) & 0xFF) * factor * kPer128);
}
void send_negated(std::span<float, 3> out, const maths::Vec4& v)
{
    core::at(out, 0) = -v.x;
    core::at(out, 1) = -v.y;
    core::at(out, 2) = -v.z;
}

// NOT ORIGINAL - a model's colours and directions as runs of the model light count they were made for.
std::span<uint32_t> colors_of(const PcModelLights& lights) { return {lights.colors(), light_sets().model_light_count()}; }
std::span<maths::Vec4> directions_of(const PcModelLights& lights) { return {lights.directions(), light_sets().model_light_count()}; }

// A dynamic light's falloff at `distance` (its vtable's slot 2).
double get_falloff(DynamicLightBase* light, float distance) { return light->get_falloff(distance); }

// The dynamic lights within `radius` of `position` (the engine's 0x0048EF90): their count, pointers into `out`.
uint32_t find_dynamic_lights(const maths::Vec4* position, uint32_t radius, std::span<DynamicLightBase*> out)
{
    return get_dynamic_lights_near(position, radius, out);
}

} // namespace

// 0x004B6270
PcModelLights::PcModelLights()
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    colors_ = static_cast<uint32_t*>(memory::alloc(4 * light_sets().model_light_count()));
    directions_ = static_cast<maths::Vec4*>(memory::alloc(16 * light_sets().model_light_count()));
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    switch_ambient_light(false);
    for (uint32_t i = 0; i < light_sets().model_light_count(); ++i) {
        switch_diffuse_light(static_cast<int>(i), false);
        core::at(directions_of(*this), i) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
    }
    first_dynamic_ = 2;
}

// 0x004B6360
PcModelLights::~PcModelLights()
{
    if (colors_) {
        memory::Hub::get_pull()->dealloc(colors_);
        colors_ = nullptr;
    }
    if (directions_) {
        memory::Hub::get_pull()->dealloc(directions_);
        directions_ = nullptr;
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
    core::at(directions_of(*this), light) = *direction;
    return true;
}

// 0x004B6180
const maths::Vec4* PcModelLights::read_light_aim(int light)
{
    if (diffuse_light_is_on(light))
        return &core::at(directions_of(*this), light);
    return render::get_light_direction(light);
}

// 0x004B61B0
bool PcModelLights::put_diffuse_light_tint(int light, const uint32_t* color)
{
    core::at(colors_of(*this), light) = *color;
    return true;
}

// 0x004B61D0
uint32_t* PcModelLights::read_diffuse_light_tint(uint32_t* out, int light)
{
    *out = core::at(colors_of(*this), light);
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
    send_color(std::span<float>(ambient_constant()).first<3>(), color, size_factor());
    for (uint32_t i = 0; i < light_sets().light_count(); ++i) {
        if (overrides & (2u << (i & 31))) {
            send_color(light_color_constant(i), core::at(colors_of(*this), i), core::at(intensities(), i));
            send_negated(light_direction_constant(i), core::at(directions_of(*this), i));
        } else {
            fetch_light_tint(&color, static_cast<int>(i));
            send_color(light_color_constant(i), color, core::at(intensities(), i));
            send_negated(light_direction_constant(i), *render::get_light_direction(static_cast<int>(i)));
        }
    }
    if (!radius)
        return;
    std::array<DynamicLightBase*, 32> dynamic{};
    const uint32_t count = find_dynamic_lights(position, radius, dynamic);
    if (!count) {
        const std::span<float, 3> off = light_color_constant(first_dynamic_);
        core::at(off, 0) = core::at(off, 1) = core::at(off, 2) = 0.0f;
        return;
    }
    for (uint32_t j = 0; j < count; ++j) {
        DynamicLightBase* light = core::at(dynamic, j);
        const auto& at = light->pos();
        double dx = static_cast<double>(position->x) - core::at(at, 0);
        double dy = static_cast<double>(position->y) - core::at(at, 1);
        const double dz = static_cast<double>(position->z) - core::at(at, 2);
        auto distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
        const double falloff = get_falloff(light, distance);
        // the direction from the light, normalised (unless its length isn't above 0)
        dx = static_cast<double>(position->x) - core::at(at, 0);
        dy = static_cast<double>(position->y) - core::at(at, 1);
        auto zf = static_cast<float>(static_cast<double>(position->z) - core::at(at, 2));
        const double length = std::sqrt(dx * dx + static_cast<double>(zf) * zf + dy * dy);
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            dx *= inverse;
            dy *= inverse;
            zf = static_cast<float>(zf * inverse);
        }
        const std::span<float, 3> direction = light_direction_constant(first_dynamic_ + j);
        core::at(direction, 0) = static_cast<float>(dx);
        core::at(direction, 1) = static_cast<float>(dy);
        core::at(direction, 2) = zf;
        if (light->type() == 1) {
            const double factor = (1.0 - falloff) * light->intensity() * kPer255;
            const std::span<float, 3> out = light_color_constant(first_dynamic_ + j);
            const colour::Bytes colour = colour::bytes_of(light->colour()); // (its r, g, b bytes)
            for (int k = 0; k < 3; ++k)
                core::at(out, k) = static_cast<float>(core::at(colour, k) * factor);
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
    send_color(std::span<float>(ambient_constant()).first<3>(), scene_ambient(), light_sets().scene_scale());
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
    send_color(light_color_constant(light), core::at(scene_colors(), light), core::at(light_sets().scene_intensities(), light));
    return true;
}

// 0x004B68E0
uint32_t* fetch_light_tint(uint32_t* out, int light)
{
    *out = core::at(scene_colors(), light);
    return out;
}

// 0x004B6900
void update_all_lights()
{
    update_ambient_light();
    // (lights 0-30 in the original, past the arrays' end for any world with fewer: the constants of lights nobody
    // has are never read, so only the world's own are sent)
    for (int i = 0; std::cmp_less(i, 31) && std::cmp_less(i, light_sets().light_count()); ++i)
        update_light_color(i);
}

// 0x004B6920
bool delete_model_lights(gsl::owner<PcModelLights*> lights)
{
    delete lights;
    return true;
}

// 0x004B6940
void reset_lights()
{
    send_color(std::span<float>(ambient_constant()).first<3>(), scene_ambient(), light_sets().scene_scale());
    for (int i = 0; i < 2; ++i) {
        uint32_t color = 0;
        fetch_light_tint(&color, i);
        send_color(light_color_constant(i), color, core::at(light_sets().scene_intensities(), i));
        send_negated(light_direction_constant(i), *get_light_direction(i));
    }
    const std::span<float, 3> third = light_color_constant(2);
    core::at(third, 0) = core::at(third, 1) = core::at(third, 2) = 0.0f;
}

// 0x004B6A70
bool update_light_direction(int light)
{
    send_negated(light_direction_constant(light), core::at(scene_directions(), light));
    return true;
}

// 0x004B6AB0
maths::Vec4* get_light_direction(int light)
{
    pc_lights_state().scene_light_direction_guard_ |= 1; // (the static's initialisation guard)
    maths::Vec4& out = pc_lights_state().scene_light_direction_;
    const maths::Vec4& direction = core::at(scene_directions(), light);
    out = {.x = direction.x, .y = direction.y, .z = direction.z, .w = 1.0f};
    return &out;
}

// 0x004B6B10
gsl::owner<PcModelLights*> spawn_model_lights() { return new PcModelLights; }

} // namespace render
