// The dynamic light component (GameObjects::DynamicLightComponent) and the light's radius (render::DynamicLightBase::set_reach): see
// dynamic_light.h. From the disassembly of 0x0041CB90-0x0041D161. The x87 roundings are kept: values the original
// stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "engine/objects/dynamic_light.h"
#include "engine/gfx/light_manager.h"

#include <bit>
#include <cstdint>
#include <gsl/pointers>

#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/core/down_cast.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/wait_and_die.h"
#include "engine/core/checked_index.h"
#include "engine/core/packed_colour.h"

namespace render {
namespace {
constexpr float kNoReachInverse = 1e10f;
} // namespace

// 0x0041CB90
void DynamicLightBase::set_reach(float reach)
{
    radius_ = reach;
    inverse_radius_ = reach == 0.0f ? kNoReachInverse : 1.0f / reach;
}

} // namespace render

namespace objects {

namespace {
constexpr int kBitsPerByte = 8;

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
    colour::Bytes out{};
    for (uint32_t i = 0; i < 4; ++i)
        core::at(out, i) = static_cast<uint8_t>(bytes->fetch_part(i));
    *color = colour::packed_of(out);
}

// An unsigned value as FILD (plus 2^32 when the top bit is set) loads it.
double as_unsigned(uint32_t value)
{
    auto d = static_cast<double>(static_cast<int32_t>(value));
    return static_cast<int32_t>(value) < 0 ? d + kTwoTo32 : d;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// 0x0048FB80 (cdecl; no source yet): a new dynamic light.
render::DynamicLightBase* new_dynamic_light(int kind) { return render::new_dynamic_light(kind); }

} // namespace

// 0x0041D0F0
gsl::owner<ObjectModule*> DynamicLightComponent::fabricate() { return new DynamicLightComponent; }

// (the constructor: inlined in fabricate in the original)
DynamicLightComponent::DynamicLightComponent() { set_kind_tag(kType); }

// 0x0041CBD0 (0x0041D0C0 the deleting one)
DynamicLightComponent::~DynamicLightComponent()
{
    if (light_) {
        light_->set_owner(0);
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
    colour::Bytes start{};
    for (uint32_t i = 0; i < 4; ++i) // (read whether found or not, as the original does)
        core::at(start, i) = static_cast<uint8_t>(bytes->fetch_part(i));
    start_color_ = colour::packed_of(start);
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
void DynamicLightComponent::seal() { wait_and_die_ = core::down_cast<WaitAndDieComponent>(entity()->fetch_module(kWaitAndDie)); }

// 0x0041CDC0
void DynamicLightComponent::tick_frame()
{
    if (!light_) {
        light_ = new_dynamic_light(1);
        light_->set_owner(entity()->ident());
    }
    // The life's fraction.
    const auto* life = wait_and_die_;
    double t = as_unsigned(life->elapsed()) / as_unsigned(life->lifespan());
    uint32_t from_color = start_color_, to_color = end_color_;
    float from_radius = start_radius_, to_radius = end_radius_;
    float from_intensity = start_intensity_, to_intensity = end_intensity_;
    auto middle_bits = std::bit_cast<uint32_t>(middle_time_);
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
    auto radius = static_cast<float>((static_cast<double>(to_radius) - from_radius) * t + from_radius);
    uint32_t color = 0;
    for (int i = 0; i < 4; ++i) {
        const int from = static_cast<int>((from_color >> (8 * i)) & 0xFF);
        const int to = static_cast<int>((to_color >> (8 * i)) & 0xFF);
        color |= static_cast<uint32_t>(static_cast<uint8_t>(get_truncated(static_cast<double>(to - from) * t + from)))
                 << (kBitsPerByte * i);
    }
    render::DynamicLightBase* light = light_;
    core::at(light->pos(), 0) = entity()->at_point().x;
    core::at(light->pos(), 1) = entity()->at_point().y;
    core::at(light->pos(), 2) = entity()->at_point().z;
    core::at(light->pos(), 3) = entity()->at_point().w;
    light->set_colour(color);
    light->set_intensity(static_cast<float>((static_cast<double>(to_intensity) - from_intensity) * t + from_intensity));
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
        light_->set_intensity(intensity);
        return 1;
    }
    case kSetColor: {
        script::Array* bytes = nullptr;
        params->fetch_list(kColor, &bytes, 1);
        colour::Bytes out{};
        for (uint32_t i = 0; i < 4; ++i)
            core::at(out, i) = static_cast<uint8_t>(bytes->fetch_part(i));
        light_->set_colour(colour::packed_of(out));
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace objects
