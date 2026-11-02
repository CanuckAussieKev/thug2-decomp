// The engine's light manager: see light_manager.h. The maths follows the original's x87 code (53-bit precision:
// double where it keeps values in registers, float where it stores them).

#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/pc_lights.h"
#include "engine/maths/vec4.h"
#include "engine/sys/startup_init.h"
#include "engine/core/checked_index.h"

namespace render {

int g_dynamic_light_count;

std::array<DynamicLightBase*, 256> g_dynamic_lights;
std::span<float> g_intensities;
uint32_t g_model_light_count;
float g_ambient_intensity{1.0f};
LightingState g_active_lighting{.ambient = 0, .directions = {}, .colours = {}, .ambient_low_ = 0.5f, .ambient_high_ = 0.0f, .light_low_ = {}, .light_high_ = {}};
namespace {
std::array<LightingState, 4> g_world_light_stack; // (the saved ones: PushLights / PopLights)

constexpr int kSavedLightLevels = 4;
constexpr int kMaxDynamicLights = 256;

LightingState& get_world() { return g_active_lighting; }
std::array<LightingState, 4>& pile_up() { return g_world_light_stack; }
int g_light_stack_top{-1};
float g_light_brightness{1.0f};
float g_light_brightness_target{1.0f};
std::array<int, 32> g_near_light_indexes{{-1}};      // get_dynamic_lights_near's indexes
std::array<float, 31> g_near_light_falloffs{{1.0f}}; // get_dynamic_lights_near's falloffs
float g_min_intensity;

int& stack_top() { return g_light_stack_top; }
uint32_t& light_count() { return g_light_count; }
float& get_brightness() { return g_light_brightness; }
float& light_gain_goal() { return g_light_brightness_target; }
float& ambient_intensity() { return g_ambient_intensity; }
float min_intensity() { return g_min_intensity; }
std::span<float>& get_intensities() { return g_intensities; }

std::array<DynamicLightBase*, 256>& dynamic_lights() { return g_dynamic_lights; }
int& dynamic_light_count() { return g_dynamic_light_count; }
// get_dynamic_lights_near's falloffs and the lights' indexes (32 each).
std::array<float, 31>& near_falloffs() { return g_near_light_falloffs; }
std::array<int, 32>& near_indexes() { return g_near_light_indexes; }

// 2b - 1 times a range, plus 1 (the high range above 0.5 - or NaN -, the low one at and below).
float get_scaled(float brightness, float low, float high)
{
    const double range = brightness <= 0.5f ? low : high;
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
    directions = {};
    colours = {};
    ambient_low_ = 0.5f;
    light_low_ = {};
    light_high_ = {};
    return this;
}

// 0x0048EB00
void LightingState::free_arrays()
{
    if (directions.data()) {
        memory::Hub::get_pull()->dealloc(directions.data());
        directions = {};
    }
    if (colours.data()) {
        memory::Hub::get_pull()->dealloc(colours.data());
        colours = {};
    }
    if (light_low_.data()) {
        memory::Hub::get_pull()->dealloc(light_low_.data());
        light_low_ = {};
    }
    if (light_high_.data()) {
        memory::Hub::get_pull()->dealloc(light_high_.data());
        light_high_ = {};
    }
}

// 0x0048EB70
void init_light_manager(uint32_t max_lights, uint32_t max_model_lights)
{
    debug::printf("Initializing Light Manager...");
    g_model_light_count = max_model_lights;
    light_count() = max_lights;
    LightingState& world = get_world();
    world.directions = {static_cast<maths::Vec4*>(memory::alloc(max_lights * 16)), max_lights};
    world.colours = {static_cast<uint32_t*>(memory::alloc(max_lights * 4)), max_lights};
    world.light_low_ = {static_cast<float*>(memory::alloc(max_lights * 4)), max_lights};
    world.light_high_ = {static_cast<float*>(memory::alloc(max_lights * 4)), max_lights};
    for (int i = 0; i < kSavedLightLevels; ++i) {
        LightingState& saved = core::at(pile_up(), i);
        saved.directions = {static_cast<maths::Vec4*>(memory::alloc(max_lights * 16)), max_lights};
        saved.colours = {static_cast<uint32_t*>(memory::alloc(max_lights * 4)), max_lights};
        saved.light_low_ = {static_cast<float*>(memory::alloc(max_lights * 4)), max_lights};
        saved.light_high_ = {static_cast<float*>(memory::alloc(max_lights * 4)), max_lights};
        std::ranges::fill(saved.directions, maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f});
        std::ranges::fill(saved.colours, 0u);
        std::ranges::fill(saved.light_low_, 0.0f);
        std::ranges::fill(saved.light_high_, 0.0f);
    }
    get_intensities() = {static_cast<float*>(memory::alloc(max_lights * 4)), max_lights};
    std::ranges::fill(get_intensities(), 1.0f);
}

// 0x0048FB60
void uninit_light_manager()
{
    debug::printf("Uninitializing Light Manager...");
    delete_dynamic_lights();
}

// 0x0048E8F0
bool set_light_ambient_color(uint32_t rgba)
{
    get_world().ambient = rgba;
    return update_ambient_light();
}

// 0x0048E900
uint32_t* get_light_ambient_color(uint32_t* out)
{
    fetch_ambient_hue(out);
    return out;
}

// 0x0048E930
bool set_light_diffuse_color(int light, uint32_t rgba)
{
    core::at(get_world().colours, light) = rgba;
    return update_light_color(light);
}

// 0x0048E950
uint32_t* get_light_diffuse_color(uint32_t* out, int light)
{
    fetch_light_tint(out, light);
    return out;
}

// 0x0048ED70
bool set_light_direction(int light, const maths::Vec4* direction)
{
    core::at(get_world().directions, light) = *direction;
    return update_light_direction(light);
}

// 0x0048E970
void change_ambient_scale(float low, float high)
{
    get_world().ambient_low_ = low;
    get_world().ambient_high_ = high;
}

// 0x0048E990
void change_diffuse_scale(int light, float low, float high)
{
    core::at(get_world().light_low_, light) = low;
    core::at(get_world().light_high_, light) = high;
}

// 0x0048E9B0
float fetch_ambient_scale(bool low) { return low ? get_world().ambient_low_ : get_world().ambient_high_; }

// 0x0048E9D0 (both branches read the low ranges, as the original)
float fetch_diffuse_scale(int light, bool low)
{
    if (low)
        return core::at(get_world().light_low_, light);
    return core::at(get_world().light_low_, light);
}

// 0x0048EA00
void push_world_lights()
{
    if (++stack_top() < kSavedLightLevels)
        core::at(pile_up(), stack_top()) = get_world();
    else
        --stack_top();
}

// 0x0048EA40
bool pop_world_lights()
{
    if (stack_top() < 0)
        return false;
    get_world() = core::at(pile_up(), stack_top());
    --stack_top();
    compiled_out();
    return true;
}

// 0x0048EDB0
bool set_world_brightness(float target)
{
    constexpr float kStep = 0.02f; // 0x006492BC
    light_gain_goal() = target;
    float& brightness = get_brightness();
    if (brightness < target) {
        const double moved = static_cast<double>(brightness) + kStep; // (compared before it's rounded)
        brightness = static_cast<float>(moved);
        if (moved > target)
            brightness = target;
    } else if (brightness > target) {
        const double moved = static_cast<double>(brightness) - kStep;
        brightness = static_cast<float>(moved);
        if (moved < target)
            brightness = target;
    }
    ambient_intensity() = get_scaled(brightness, get_world().ambient_low_, get_world().ambient_high_);
    if (ambient_intensity() < min_intensity())
        ambient_intensity() = min_intensity();
    for (uint32_t i = 0; i < light_count(); ++i) {
        float& intensity = core::at(get_intensities(), i);
        intensity = get_scaled(brightness, core::at(get_world().light_low_, i), core::at(get_world().light_high_, i));
        if (intensity < min_intensity())
            intensity = min_intensity();
    }
    update_all_lights();
    return true;
}

// 0x0048EF00
void delete_dynamic_lights()
{
    for (int i = 0; i < dynamic_light_count(); ++i) {
        if (core::at(dynamic_lights(), i)) {
            memory::Hub::get_pull()->dealloc(core::at(dynamic_lights(), i));
            core::at(dynamic_lights(), i) = nullptr;
        }
    }
    dynamic_light_count() = 0;
}

// 0x0048EF50
DynamicLightBase** find_dynamic_light(uint32_t owner)
{
    for (int i = 0; i < dynamic_light_count(); ++i) {
        const DynamicLightBase* light = core::at(dynamic_lights(), i);
        if (light && light->owner == owner)
            return &core::at(dynamic_lights(), i);
    }
    return nullptr;
}

// 0x0048EF90
uint32_t get_dynamic_lights_near(const maths::Vec4* position, uint32_t max, std::span<DynamicLightBase*> out)
{
    auto& falloffs = near_falloffs();
    auto& indexes = near_indexes();
    uint32_t count = 0;
    for (int i = 0; i < dynamic_light_count(); ++i) {
        DynamicLightBase* light = core::at(dynamic_lights(), i); // (not checked for null, as the original)
        if (!(light->intensity > 0.0f) || !light->owner)
            continue;
        const double dx = static_cast<double>(position->x) - core::at(light->pos, 0);
        const double dy = static_cast<double>(position->y) - core::at(light->pos, 1);
        const double dz = static_cast<double>(position->z) - core::at(light->pos, 2);
        const double distance_squared = dz * dz + dy * dy + dx * dx;
        const double radius = light->radius;
        if (distance_squared > radius * radius) // (NaN: within reach)
            continue;
        auto distance = static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(distance_squared))));
        const double falloff = call_falloff(light, distance);
        if (count == 0) {
            core::at(falloffs, 0) = static_cast<float>(falloff);
            core::at(indexes, 0) = i;
            core::at(out, 0) = light;
            count = 1;
            continue;
        }
        if (count >= max && !(falloff < core::at(falloffs, count - 1))) // (full: only below the last; NaN: not)
            continue;
        // The original's walk: inserted before the first it's greater than; each one it isn't greater than appends it
        // at the end (while there's room) - the end moving on with it.
        for (uint32_t j = 0; j < count; ++j) {
            if (falloff > core::at(falloffs, j)) {
                // the entries from j on moved up one by swaps (the one past the end - out[count] too - written)
                for (uint32_t k = count; k > j; --k) {
                    std::swap(core::at(out, k), core::at(out, k - 1));
                    const float f = core::at(falloffs, k);
                    core::at(falloffs, k) = core::at(falloffs, k - 1);
                    core::at(falloffs, k - 1) = f;
                    const int n = core::at(indexes, k);
                    core::at(indexes, k) = core::at(indexes, k - 1);
                    core::at(indexes, k - 1) = n;
                }
                core::at(falloffs, j) = static_cast<float>(falloff);
                core::at(indexes, j) = i;
                core::at(out, j) = light;
                if (count < max)
                    ++count;
                break;
            }
            if (count < max) {
                core::at(falloffs, count) = static_cast<float>(falloff);
                core::at(indexes, count) = i;
                core::at(out, count) = light;
                ++count;
            }
        }
    }
    return count;
}

// 0x0048F160
DynamicLightBase* create_dynamic_light(int type)
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
DynamicLightBase* new_dynamic_light(int type)
{
    const int count = dynamic_light_count();
    for (int i = 0; i < count; ++i) {
        const DynamicLightBase* light = core::at(dynamic_lights(), i);
        if (!light) {
            core::at(dynamic_lights(), i) = create_dynamic_light(type);
            return core::at(dynamic_lights(), i);
        }
        if (!light->owner)
            return core::at(dynamic_lights(), i);
    }
    if (count >= kMaxDynamicLights || core::at(dynamic_lights(), count))
        return nullptr;
    dynamic_light_count() = count + 1;
    core::at(dynamic_lights(), count) = create_dynamic_light(type);
    return core::at(dynamic_lights(), count);
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
