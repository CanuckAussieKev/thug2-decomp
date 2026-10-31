// The engine's light manager: see light_manager.h. The maths follows the original's x87 code (53-bit precision:
// double where it keeps values in registers, float where it stores them).

#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/pc_lights.h"
#include "engine/maths/vec4.h"
#include "engine/sys/startup_init.h"

namespace render {

int g_dynamic_light_count;

DynamicLightBase* g_dynamic_lights[256];
float* g_intensities;
uint32_t g_model_light_count;
float g_ambient_intensity{1.0f};
LightingState g_active_lighting{0, nullptr, nullptr, 0.5f, 0.0f, nullptr, nullptr};
LightingState g_world_light_stack[4]; // (the saved ones: PushLights / PopLights)
namespace {

constexpr int kSavedLightLevels = 4;
constexpr int kMaxDynamicLights = 256;

LightingState& get_world() { return g_active_lighting; }
LightingState* pile_up() { return g_world_light_stack; }
int g_light_stack_top{-1};
float g_light_brightness{1.0f};
float g_light_brightness_target{1.0f};
int g_near_light_indexes[32]{-1};      // get_dynamic_lights_near's indexes
float g_near_light_falloffs[31]{1.0f}; // get_dynamic_lights_near's falloffs
float g_min_intensity;

int& stack_top() { return g_light_stack_top; }
uint32_t& light_count() { return g_light_count; }
float& get_brightness() { return g_light_brightness; }
float& light_gain_goal() { return g_light_brightness_target; }
float& ambient_intensity() { return g_ambient_intensity; }
float min_intensity() { return g_min_intensity; }
float*& get_intensities() { return g_intensities; }

DynamicLightBase** dynamic_lights() { return g_dynamic_lights; }
int& dynamic_light_count() { return g_dynamic_light_count; }
// get_dynamic_lights_near's falloffs and the lights' indexes (32 each).
float* near_falloffs() { return g_near_light_falloffs; }
int* near_indexes() { return g_near_light_indexes; }

// 2b - 1 times a range, plus 1 (the high range above 0.5 - or NaN -, the low one at and below).
float get_scaled(float brightness, float low, float high)
{
    double range = brightness <= 0.5f ? low : high;
    return static_cast<float>((static_cast<double>(brightness) * 2.0 - 1.0) * range + 1.0);
}

// A compiled-out call (the empty 0x005480A0) after the stack is popped.
void compiled_out() {}

// A light's falloff (its vtable's slot 2): returned in ST0 unrounded.
double call_falloff(DynamicLightBase* light, float distance) { return light->get_falloff(distance); }

} // namespace

// 0x0048EAC0
LightingState* LightingState::make_new()
{
    ambient = 0x80484848;
    directions = nullptr;
    colours = nullptr;
    ambient_low_ = 0.5f;
    light_low_ = nullptr;
    light_high_ = nullptr;
    return this;
}

// 0x0048EB00
void LightingState::free_arrays()
{
    if (directions) {
        memory::Hub::get_pull()->dealloc(directions);
        directions = nullptr;
    }
    if (colours) {
        memory::Hub::get_pull()->dealloc(colours);
        colours = nullptr;
    }
    if (light_low_) {
        memory::Hub::get_pull()->dealloc(light_low_);
        light_low_ = nullptr;
    }
    if (light_high_) {
        memory::Hub::get_pull()->dealloc(light_high_);
        light_high_ = nullptr;
    }
}

// 0x0048EB70
void __cdecl init_light_manager(uint32_t max_lights, uint32_t max_model_lights)
{
    debug::printf("Initializing Light Manager...");
    g_model_light_count = max_model_lights;
    light_count() = max_lights;
    LightingState& world = get_world();
    world.directions = static_cast<maths::Vec4*>(memory::alloc(max_lights * 16));
    world.colours = static_cast<uint32_t*>(memory::alloc(max_lights * 4));
    world.light_low_ = static_cast<float*>(memory::alloc(max_lights * 4));
    world.light_high_ = static_cast<float*>(memory::alloc(max_lights * 4));
    for (int i = 0; i < kSavedLightLevels; ++i) {
        LightingState& saved = pile_up()[i];
        saved.directions = static_cast<maths::Vec4*>(memory::alloc(max_lights * 16));
        saved.colours = static_cast<uint32_t*>(memory::alloc(max_lights * 4));
        saved.light_low_ = static_cast<float*>(memory::alloc(max_lights * 4));
        saved.light_high_ = static_cast<float*>(memory::alloc(max_lights * 4));
        std::memset(saved.directions, 0, max_lights * 16);
        std::memset(saved.colours, 0, max_lights * 4);
        std::memset(saved.light_low_, 0, max_lights * 4);
        std::memset(saved.light_high_, 0, max_lights * 4);
    }
    get_intensities() = static_cast<float*>(memory::alloc(max_lights * 4));
    for (uint32_t i = 0; i < max_lights; ++i)
        get_intensities()[i] = 1.0f;
}

// 0x0048FB60
void __cdecl uninit_light_manager()
{
    debug::printf("Uninitializing Light Manager...");
    delete_dynamic_lights();
}

// 0x0048E8F0
bool __cdecl set_light_ambient_color(uint32_t rgba)
{
    get_world().ambient = rgba;
    return update_ambient_light();
}

// 0x0048E900
uint32_t* __cdecl get_light_ambient_color(uint32_t* out)
{
    fetch_ambient_hue(out);
    return out;
}

// 0x0048E930
bool __cdecl set_light_diffuse_color(int light, uint32_t rgba)
{
    get_world().colours[light] = rgba;
    return update_light_color(light);
}

// 0x0048E950
uint32_t* __cdecl get_light_diffuse_color(uint32_t* out, int light)
{
    fetch_light_tint(out, light);
    return out;
}

// 0x0048ED70
bool __cdecl set_light_direction(int light, const maths::Vec4* direction)
{
    get_world().directions[light] = *direction;
    return update_light_direction(light);
}

// 0x0048E970
void __cdecl change_ambient_scale(float low, float high)
{
    get_world().ambient_low_ = low;
    get_world().ambient_high_ = high;
}

// 0x0048E990
void __cdecl change_diffuse_scale(int light, float low, float high)
{
    get_world().light_low_[light] = low;
    get_world().light_high_[light] = high;
}

// 0x0048E9B0
float __cdecl fetch_ambient_scale(bool low) { return low ? get_world().ambient_low_ : get_world().ambient_high_; }

// 0x0048E9D0 (both branches read the low ranges, as the original)
float __cdecl fetch_diffuse_scale(int light, bool low)
{
    if (low)
        return get_world().light_low_[light];
    return get_world().light_low_[light];
}

// 0x0048EA00
void __cdecl push_world_lights()
{
    if (++stack_top() < kSavedLightLevels)
        pile_up()[stack_top()] = get_world();
    else
        --stack_top();
}

// 0x0048EA40
bool __cdecl pop_world_lights()
{
    if (stack_top() < 0)
        return false;
    get_world() = pile_up()[stack_top()];
    --stack_top();
    compiled_out();
    return true;
}

// 0x0048EDB0
bool __cdecl set_world_brightness(float target)
{
    constexpr float kStep = 0.02f; // 0x006492BC
    light_gain_goal() = target;
    float& brightness = get_brightness();
    if (brightness < target) {
        double moved = static_cast<double>(brightness) + kStep; // (compared before it's rounded)
        brightness = static_cast<float>(moved);
        if (moved > target)
            brightness = target;
    } else if (brightness > target) {
        double moved = static_cast<double>(brightness) - kStep;
        brightness = static_cast<float>(moved);
        if (moved < target)
            brightness = target;
    }
    ambient_intensity() = get_scaled(brightness, get_world().ambient_low_, get_world().ambient_high_);
    if (ambient_intensity() < min_intensity())
        ambient_intensity() = min_intensity();
    for (uint32_t i = 0; i < light_count(); ++i) {
        get_intensities()[i] = get_scaled(brightness, get_world().light_low_[i], get_world().light_high_[i]);
        if (get_intensities()[i] < min_intensity())
            get_intensities()[i] = min_intensity();
    }
    update_all_lights();
    return true;
}

// 0x0048EF00
void __cdecl delete_dynamic_lights()
{
    for (int i = 0; i < dynamic_light_count(); ++i) {
        if (dynamic_lights()[i]) {
            memory::Hub::get_pull()->dealloc(dynamic_lights()[i]);
            dynamic_lights()[i] = nullptr;
        }
    }
    dynamic_light_count() = 0;
}

// 0x0048EF50
DynamicLightBase** __cdecl find_dynamic_light(uint32_t owner)
{
    for (int i = 0; i < dynamic_light_count(); ++i) {
        DynamicLightBase* light = dynamic_lights()[i];
        if (light && light->owner == owner)
            return &dynamic_lights()[i];
    }
    return nullptr;
}

// 0x0048EF90
uint32_t __cdecl get_dynamic_lights_near(const maths::Vec4* position, uint32_t max, DynamicLightBase** out)
{
    float* falloffs = near_falloffs();
    int* indexes = near_indexes();
    uint32_t count = 0;
    for (int i = 0; i < dynamic_light_count(); ++i) {
        DynamicLightBase* light = dynamic_lights()[i]; // (not checked for null, as the original)
        if (!(light->intensity > 0.0f) || !light->owner)
            continue;
        double dx = static_cast<double>(position->x) - light->pos[0];
        double dy = static_cast<double>(position->y) - light->pos[1];
        double dz = static_cast<double>(position->z) - light->pos[2];
        double distance_squared = dz * dz + dy * dy + dx * dx;
        double radius = light->radius;
        if (distance_squared > radius * radius) // (NaN: within reach)
            continue;
        auto distance = static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(distance_squared))));
        double falloff = call_falloff(light, distance);
        if (count == 0) {
            falloffs[0] = static_cast<float>(falloff);
            indexes[0] = i;
            out[0] = light;
            count = 1;
            continue;
        }
        if (count >= max && !(falloff < falloffs[count - 1])) // (full: only below the last; NaN: not)
            continue;
        // The original's walk: inserted before the first it's greater than; each one it isn't greater than appends it
        // at the end (while there's room) - the end moving on with it.
        for (uint32_t j = 0; j < count; ++j) {
            if (falloff > falloffs[j]) {
                // the entries from j on moved up one by swaps (the one past the end - out[count] too - written)
                for (uint32_t k = count; k > j; --k) {
                    DynamicLightBase* l = out[k];
                    out[k] = out[k - 1];
                    out[k - 1] = l;
                    float f = falloffs[k];
                    falloffs[k] = falloffs[k - 1];
                    falloffs[k - 1] = f;
                    int n = indexes[k];
                    indexes[k] = indexes[k - 1];
                    indexes[k - 1] = n;
                }
                falloffs[j] = static_cast<float>(falloff);
                indexes[j] = i;
                out[j] = light;
                if (count < max)
                    ++count;
                break;
            }
            if (count < max) {
                falloffs[count] = static_cast<float>(falloff);
                indexes[count] = i;
                out[count] = light;
                ++count;
            }
        }
    }
    return count;
}

// 0x0048F160
DynamicLightBase* __cdecl create_dynamic_light(int type)
{
    if (type != 1)
        return nullptr;
    auto* light = static_cast<DynamicLightBase*>(memory::alloc_zeroed(sizeof(DynamicLightBase)));
    if (!light)
        return nullptr;
    light->unknown2c_ = false;
    light->type = 1;
    light->radius = 50.0f;
    light->inverse_radius_ = 50.0f; // (as the original: not 1 / 50)
    return light;
}

// 0x0048FB80
DynamicLightBase* __cdecl new_dynamic_light(int type)
{
    int count = dynamic_light_count();
    for (int i = 0; i < count; ++i) {
        DynamicLightBase* light = dynamic_lights()[i];
        if (!light) {
            dynamic_lights()[i] = create_dynamic_light(type);
            return dynamic_lights()[i];
        }
        if (!light->owner)
            return dynamic_lights()[i];
    }
    if (count >= kMaxDynamicLights || dynamic_lights()[count])
        return nullptr;
    dynamic_light_count() = count + 1;
    dynamic_lights()[count] = create_dynamic_light(type);
    return dynamic_lights()[count];
}

// 0x0048E8C0
int DynamicLightBase::is_on() { return intensity > 0.0f ? 1 : 0; }

// 0x0048EA90
int DynamicLightBase::in_reach(float distance) { return distance < radius ? 1 : 0; }

// 0x0048EAB0
double DynamicLightBase::get_falloff(float distance) { return static_cast<double>(distance) * inverse_radius_; }

} // namespace render

namespace render {

// 0x00640B60 (a static initialiser): the world lights' ambient colour (the rest of them is the image's data).
void set_world_lights_ambient() { g_active_lighting.ambient = 0x80484848; }

// 0x00640B90 (a static initialiser): the four saved light sets.
void construct_world_light_stack()
{
    for (LightingState& lights : g_world_light_stack)
        lights.make_new();
}

} // namespace render
