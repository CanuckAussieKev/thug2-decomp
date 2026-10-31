// The dynamic light component (GameObjects::DynamicLightComponent) and the light's radius (render::DynamicLight::set_reach): see
// dynamic_light.h. From the disassembly of 0x0041CB90-0x0041D161. The x87 roundings are kept: values the original
// stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "engine/objects/dynamic_light.h"
#include "engine/gfx/light_manager.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/dynamic_light.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace render {

// 0x0041CB90
void DynamicLight::set_reach(float radius)
{
    radius_ = radius;
    inverse_radius_ = radius == 0.0f ? 1e10f : 1.0f / radius;
}

} // namespace render

namespace objects {

namespace {

constexpr uint32_t kStartRadius = 0x3BEE67C8, kMiddleRadius = 0x8E39913D, kEndRadius = 0x3F243A3C; // StartRadius...
constexpr uint32_t kStartIntensity = 0x27A7443F, kMiddleIntensity = 0xCEF9F4FD, kEndIntensity = 0xE1CE9780;
constexpr uint32_t kStartColor = 0x7E6BCBE6, kMiddleColor = 0x2A6B6FE2, kEndColor = 0x0481113D; // StartRGBA...
constexpr uint32_t kMiddleTime = 0x856BC5E2;                                                    // midTime
constexpr uint32_t kWaitAndDie = 0x03AC8065;
// invoke_method_by_name's members and parameters
constexpr uint32_t kSetRadius = 0xFAAD1C11, kSetIntensity = 0x86E1FA56, kSetColor = 0x1A0F4C50;
constexpr uint32_t kRadius = 0xC48391A5, kIntensity = 0x164D476D, kColor = 0x3F6BCDBA; // radius, intensity, rgba
constexpr uint32_t kNoMiddle = 0xBF800000; // -1.0f's bits (compared as bits)
constexpr double kTwoTo32 = 4294967296.0;  // 0x006453B4 (FILD's correction for unsigned values)

// The colour of `name` - an array of four bytes - into `color` (unchanged if there's none; `bytes` is the caller's
// one slot for the array, as in the original).
void fetch_tint(script::Struct* params, uint32_t name, uint32_t* color, int assert, script::Array*& bytes)
{
    if (!params->fetch_list(name, &bytes, assert))
        return;
    auto* out = reinterpret_cast<uint8_t*>(color);
    for (uint32_t i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>(bytes->fetch_part(i));
}

// An unsigned value as FILD (plus 2^32 when the top bit is set) loads it.
double Unsigned(uint32_t value)
{
    double d = static_cast<double>(static_cast<int32_t>(value));
    return static_cast<int32_t>(value) < 0 ? d + kTwoTo32 : d;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// 0x0048FB80 (cdecl; no source yet): a new dynamic light.
render::DynamicLight* new_dynamic_light(int kind)
{
    return reinterpret_cast<render::DynamicLight*>(render::new_dynamic_light(kind));
}

} // namespace

// 0x0041D0F0
ObjectModule* __cdecl DynamicLightComponent::fabricate() { return new DynamicLightComponent; }

// (the constructor: inlined in fabricate in the original)
DynamicLightComponent::DynamicLightComponent()
{
    kind_tag_ = kType;
    light_ = nullptr;
}

// 0x0041CBD0 (0x0041D0C0 the deleting one)
DynamicLightComponent::~DynamicLightComponent()
{
    if (light_) {
        light_->holder_key_ = 0;
        light_ = nullptr;
    }
    wait_and_die_ = nullptr;
}

// 0x0041CBF0
void DynamicLightComponent::configure_from_data(script::Struct* params)
{
    script::Array* bytes = nullptr;
    params->fetch_float(kStartRadius, &start_radius_, 1);
    params->fetch_float(kStartIntensity, &start_intensity_, 1);
    params->fetch_list(kStartColor, &bytes, 1);
    auto* start = reinterpret_cast<uint8_t*>(&start_color_);
    for (uint32_t i = 0; i < 4; ++i) // (read whether found or not, as the original does)
        start[i] = static_cast<uint8_t>(bytes->fetch_part(i));
    middle_radius_ = start_radius_;
    end_radius_ = start_radius_;
    middle_color_ = start_color_;
    end_color_ = start_color_;
    middle_intensity_ = start_intensity_;
    end_intensity_ = start_intensity_;
    middle_time_ = -1.0f;
    params->fetch_float(kMiddleTime, &middle_time_, 0);
    params->fetch_float(kMiddleRadius, &middle_radius_, 0);
    params->fetch_float(kMiddleIntensity, &middle_intensity_, 0);
    fetch_tint(params, kMiddleColor, &middle_color_, 0, bytes);
    params->fetch_float(kEndRadius, &end_radius_, 0);
    params->fetch_float(kEndIntensity, &end_intensity_, 0);
    fetch_tint(params, kEndColor, &end_color_, 0, bytes);
}

// 0x0041CDA0
void DynamicLightComponent::seal() { wait_and_die_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kWaitAndDie)); }

// 0x0041CDC0
void DynamicLightComponent::tick_frame()
{
    if (!light_) {
        light_ = new_dynamic_light(1);
        light_->holder_key_ = entity_->ident_;
    }
    // The life's fraction.
    double t = Unsigned(*reinterpret_cast<uint32_t*>(wait_and_die_ + 0x1C)) /
               Unsigned(*reinterpret_cast<uint32_t*>(wait_and_die_ + 0x20));
    uint32_t from_color = start_color_, to_color = end_color_;
    float from_radius = start_radius_, to_radius = end_radius_;
    float from_intensity = start_intensity_, to_intensity = end_intensity_;
    uint32_t middle_bits;
    std::memcpy(&middle_bits, &middle_time_, 4);
    if (middle_bits != kNoMiddle) {
        if (t <= middle_time_) {
            to_color = middle_color_;
            t = t / middle_time_;
            to_radius = middle_radius_;
            to_intensity = middle_intensity_;
        } else {
            from_color = middle_color_;
            from_radius = middle_radius_;
            from_intensity = middle_intensity_;
            t = (t - middle_time_) / (1.0 - middle_time_);
        }
    }
    float radius = static_cast<float>((static_cast<double>(to_radius) - from_radius) * t + from_radius);
    uint32_t color = 0;
    for (int i = 0; i < 4; ++i) {
        int from = (from_color >> (8 * i)) & 0xFF;
        int to = (to_color >> (8 * i)) & 0xFF;
        color |= static_cast<uint32_t>(static_cast<uint8_t>(get_truncated(static_cast<double>(to - from) * t + from)))
                 << (8 * i);
    }
    render::DynamicLight* light = light_;
    light->at_point_ = entity_->at_point_;
    light->tint_ = color;
    light->strength_ = static_cast<float>((static_cast<double>(to_intensity) - from_intensity) * t + from_intensity);
    light->set_reach(radius);
}

// 0x0041CFC0
int DynamicLightComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    switch (name) {
    case kSetRadius: {
        float radius = 0.0f;
        params->fetch_float(kRadius, &radius, 1);
        light_->set_reach(radius);
        return 1;
    }
    case kSetIntensity: {
        float intensity = 0.0f;
        params->fetch_float(kIntensity, &intensity, 1);
        light_->strength_ = intensity;
        return 1;
    }
    case kSetColor: {
        script::Array* bytes = nullptr;
        params->fetch_list(kColor, &bytes, 1);
        uint32_t color;
        auto* out = reinterpret_cast<uint8_t*>(&color);
        for (uint32_t i = 0; i < 4; ++i)
            out[i] = static_cast<uint8_t>(bytes->fetch_part(i));
        light_->tint_ = color;
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace objects
