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
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"

namespace render {

maths::Vec4 g_scene_light_direction;
uint32_t g_scene_light_direction_guard;
float g_light_constants[28];

namespace {

extern void** const kModelLightsVtable;
constexpr double kPer128 = 1.0 / 128.0;  // 0x0064A2FC
constexpr float kPer255 = 1.0f / 255.0f; // 0x00645D70 (0x3B808081)

// The renderer's light constants.
float* ambient_constant() { return g_light_constants; }
float* light_direction_constant(uint32_t light) { return g_light_constants + 4 + 8 * light; }
float* light_color_constant(uint32_t light) { return g_light_constants + 8 + 8 * light; }

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

template <typename R, typename... A>
R call_virtual(PcModelLights* lights, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(PcModelLights*, A...)>(lights->vtable[slot])(lights, args...);
}

// A dynamic light's falloff at `distance` (its virtual [2]; thiscall) as the x87 leaves it, unrounded.
__declspec(naked) double __stdcall get_falloff(void* /*light*/, float /*distance*/)
{
    __asm {
        mov ecx, [esp + 4]
        push dword ptr [esp + 8]
        mov eax, [ecx]
        call dword ptr [eax + 8]
        ret 8
    }
}

// The dynamic lights within `radius` of `position` (the engine's 0x0048EF90): their count, pointers into `out`.
uint32_t find_dynamic_lights(const maths::Vec4* position, uint32_t radius, uint8_t** out)
{
    return get_dynamic_lights_near(position, radius, reinterpret_cast<DynamicLightBase**>(out));
}

} // namespace

// 0x004B6270
PcModelLights* PcModelLights::make_new()
{
    reinterpret_cast<ModelLights*>(this)->ModelLights::make_new();
    vtable = reinterpret_cast<void**>(kModelLightsVtable);
    overrides = 0;
    colors = static_cast<uint32_t*>(memory::alloc(4 * model_light_count()));
    directions = static_cast<maths::Vec4*>(memory::alloc(16 * model_light_count()));
    call_virtual<void>(this, 9, false); // enable_ambient_override
    for (uint32_t i = 0; i < model_light_count(); ++i) {
        call_virtual<void>(this, 10, static_cast<int>(i), false); // enable_light_override
        directions[i] = {0.0f, 1.0f, 0.0f, 1.0f};
    }
    first_dynamic_ = 2;
    return this;
}

// 0x004B6360
void PcModelLights::destroy_body()
{
    vtable = reinterpret_cast<void**>(kModelLightsVtable);
    if (colors) {
        memory::Hub::get_pull()->dealloc(colors);
        colors = nullptr;
    }
    if (directions) {
        memory::Hub::get_pull()->dealloc(directions);
        directions = nullptr;
    }
    reinterpret_cast<ModelLights*>(this)->ModelLights::destroy_body();
}

// 0x004B67B0
PcModelLights* PcModelLights::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004B6160
bool PcModelLights::change_ambient_tint(const uint32_t* color)
{
    ambient = *color;
    return true;
}

// 0x004B6170
uint32_t* PcModelLights::fetch_ambient_tint(uint32_t* out)
{
    *out = ambient;
    return out;
}

// 0x004B6780
bool PcModelLights::set_light_direction(int light, const maths::Vec4* direction)
{
    directions[light] = *direction;
    return true;
}

// 0x004B6180
const maths::Vec4* PcModelLights::get_light_direction(int light)
{
    if (call_virtual<bool>(this, 12, light)) // is_light_overridden
        return &directions[light];
    return render::get_light_direction(light);
}

// 0x004B61B0
bool PcModelLights::set_light_diffuse_color(int light, const uint32_t* color)
{
    colors[light] = *color;
    return true;
}

// 0x004B61D0
uint32_t* PcModelLights::get_light_diffuse_color(uint32_t* out, int light)
{
    *out = colors[light];
    return out;
}

// 0x004B63B0
void PcModelLights::tick_frame(const maths::Vec4* position, uint32_t radius)
{
    uint32_t color;
    if (overrides & 1)
        color = ambient;
    else
        fetch_ambient_hue(&color);
    send_color(ambient_constant(), color, scale);
    for (uint32_t i = 0; i < light_count(); ++i) {
        if (overrides & (2u << (i & 31))) {
            send_color(light_color_constant(i), colors[i], intensities[i]);
            send_negated(light_direction_constant(i), directions[i]);
        } else {
            fetch_light_tint(&color, static_cast<int>(i));
            send_color(light_color_constant(i), color, intensities[i]);
            send_negated(light_direction_constant(i), *render::get_light_direction(static_cast<int>(i)));
        }
    }
    if (!radius)
        return;
    uint8_t* dynamic[32];
    uint32_t count = find_dynamic_lights(position, radius, dynamic);
    if (!count) {
        float* off = light_color_constant(first_dynamic_);
        off[0] = off[1] = off[2] = 0.0f;
        return;
    }
    for (uint32_t j = 0; j < count; ++j) {
        uint8_t* light = dynamic[j];
        const float* at = reinterpret_cast<const float*>(light + 4);
        double dx = static_cast<double>(position->x) - at[0];
        double dy = static_cast<double>(position->y) - at[1];
        double dz = static_cast<double>(position->z) - at[2];
        float distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
        double falloff = get_falloff(light, distance);
        // the direction from the light, normalised (unless its length isn't above 0)
        dx = static_cast<double>(position->x) - at[0];
        dy = static_cast<double>(position->y) - at[1];
        float zf = static_cast<float>(static_cast<double>(position->z) - at[2]);
        double length = std::sqrt(dx * dx + static_cast<double>(zf) * zf + dy * dy);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            dx *= inverse;
            dy *= inverse;
            zf = static_cast<float>(zf * inverse);
        }
        float* direction = light_direction_constant(first_dynamic_ + j);
        direction[0] = static_cast<float>(dx);
        direction[1] = static_cast<float>(dy);
        direction[2] = zf;
        if (*reinterpret_cast<int*>(light + 0x14) == 1) {
            double factor = (1.0 - falloff) * *reinterpret_cast<const float*>(light + 0x20) * kPer255;
            float* out = light_color_constant(first_dynamic_ + j);
            for (int k = 0; k < 3; ++k)
                out[k] = static_cast<float>(light[0x24 + k] * factor);
        }
    }
}

// 0x004B61F0
void PcModelLights::enable_ambient_override(bool on)
{
    if (on)
        overrides |= 1;
    else
        overrides &= ~1u;
}

// 0x004B6240
bool PcModelLights::is_ambient_overridden() { return (overrides & 1) != 0; }

// 0x004B6210
void PcModelLights::enable_light_override(int light, bool on)
{
    uint32_t bit = 2u << (light & 31);
    if (on)
        overrides |= bit;
    else
        overrides &= ~bit;
}

// 0x004B6250
bool PcModelLights::is_light_overridden(int light) { return (overrides & (2u << (light & 31))) != 0; }

// 0x004B67E0
bool __cdecl update_ambient_light()
{
    send_color(ambient_constant(), scene_ambient(), scene_scale());
    return true;
}

// 0x004B6850
uint32_t* __cdecl fetch_ambient_hue(uint32_t* out)
{
    *out = scene_ambient();
    return out;
}

// 0x004B6860
bool __cdecl update_light_color(int light)
{
    send_color(light_color_constant(light), scene_colors()[light], scene_intensities()[light]);
    return true;
}

// 0x004B68E0
uint32_t* __cdecl fetch_light_tint(uint32_t* out, int light)
{
    *out = scene_colors()[light];
    return out;
}

// 0x004B6900
void __cdecl update_all_lights()
{
    update_ambient_light();
    for (int i = 0; i < 31; ++i)
        update_light_color(i);
}

// 0x004B6920
bool __cdecl delete_model_lights(PcModelLights* lights)
{
    if (lights)
        call_virtual<void*>(lights, 0, 1u);
    return true;
}

// 0x004B6940
void __cdecl reset_lights()
{
    send_color(ambient_constant(), scene_ambient(), scene_scale());
    for (int i = 0; i < 2; ++i) {
        uint32_t color;
        fetch_light_tint(&color, i);
        send_color(light_color_constant(i), color, scene_intensities()[i]);
        send_negated(light_direction_constant(i), *get_light_direction(i));
    }
    float* third = light_color_constant(2);
    third[0] = third[1] = third[2] = 0.0f;
}

// 0x004B6A70
bool __cdecl update_light_direction(int light)
{
    send_negated(light_direction_constant(light), scene_directions()[light]);
    return true;
}

// 0x004B6AB0
maths::Vec4* __cdecl get_light_direction(int light)
{
    g_scene_light_direction_guard |= 1; // (the static's initialisation guard)
    maths::Vec4& out = g_scene_light_direction;
    const maths::Vec4& direction = scene_directions()[light];
    out = {direction.x, direction.y, direction.z, 1.0f};
    return &out;
}

// 0x004B6B10
PcModelLights* __cdecl spawn_model_lights()
{
    void* memory = memory::alloc(sizeof(PcModelLights));
    return memory ? static_cast<PcModelLights*>(memory)->make_new() : nullptr;
}

namespace {
static VtableSlot g_model_lights_vtable_slots[] = {&render::PcModelLights::teardown,
                                                   &render::PcModelLights::change_ambient_tint,
                                                   &render::PcModelLights::set_light_direction,
                                                   &render::PcModelLights::set_light_diffuse_color,
                                                   &render::PcModelLights::fetch_ambient_tint,
                                                   &render::PcModelLights::get_light_direction,
                                                   &render::PcModelLights::get_light_diffuse_color,
                                                   &render::PcModelLights::tick_frame,
                                                   stubs::ignore_arguments2,
                                                   &render::PcModelLights::enable_ambient_override,
                                                   &render::PcModelLights::enable_light_override,
                                                   &render::PcModelLights::is_ambient_overridden,
                                                   &render::PcModelLights::is_light_overridden};
void** const kModelLightsVtable = reinterpret_cast<void**>(g_model_lights_vtable_slots);
} // namespace

} // namespace render
