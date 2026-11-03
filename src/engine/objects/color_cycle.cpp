// The colour cycle component (GameObjects::ColorCycleComponent): see color_cycle.h. From the disassembly of
// 0x0041B280-0x0041B57F.

#include "engine/objects/color_cycle.h"
#include "engine/script/game_cfuncs.h"

#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/checked_index.h"

namespace objects {
namespace {
constexpr uint32_t kHash4AF8B9AE = 0x4AF8B9AE; // (text unknown)
constexpr uint32_t kHashCDC76A22 = 0xCDC76A22; // (text unknown)
constexpr uint32_t kHash1DE2F35B = 0x1DE2F35B; // (text unknown)
constexpr uint32_t kHash4A752AAE = 0x4A752AAE; // (text unknown)
constexpr uint32_t kHash80D5F732 = 0x80D5F732; // (text unknown)
} // namespace

std::array<script::MemberFunctionEntry, 5>& color_cycle_member_functions()
{
    static std::array<script::MemberFunctionEntry, 5> color_cycle_member_functions{{
        {.callsign_ = kHash4AF8B9AE, .owner_kind_ = kHashCDC76A22}, {.callsign_ = kHash1DE2F35B, .owner_kind_ = kHashCDC76A22}, {.callsign_ = kHash4A752AAE, .owner_kind_ = kHashCDC76A22}, {.callsign_ = kHash80D5F732, .owner_kind_ = kHashCDC76A22},
        {.callsign_ = 0, .owner_kind_ = kHashCDC76A22}}};
    return color_cycle_member_functions;
}

namespace {

constexpr uint32_t kName = 0xA1DC81F9;  // name
constexpr uint32_t kColor = 0x99A9B716; // color
constexpr uint32_t kNeutralColor = 0x808080;
constexpr float kMilliseconds = 1000.0f; // 0x006453C0

// 0x005A3E80 (cdecl; no source yet): the colour of the object `id` from `params` (color=), as the script command
// SetObjectColor does.
void set_object_color(uint32_t id, script::Struct* params) { commands::set_node_color(id, params); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear(); // (the destructor)
        script::delete_struct(params);
    }
}

} // namespace

// 0x0041B3C0
gsl::owner<ObjectModule*> ColorCycleComponent::fabricate() { return new ColorCycleComponent; }

// (the constructor: inlined in fabricate in the original)
ColorCycleComponent::ColorCycleComponent() { set_kind_tag(kType); }

// 0x0041B280
void ColorCycleComponent::publish_method_table()
{
    script::register_member_function_table(color_cycle_member_functions());
}

// 0x0041B2A0 (0x0041B2E0 the deleting one)
ColorCycleComponent::~ColorCycleComponent() = default;

// 0x0041B310
void ColorCycleComponent::change_colors(script::Array* colors)
{
    const int count = static_cast<int>(colors->extent());
    color_count_ = count;
    for (int i = 0; i < count; ++i) {
        const script::Vector* vector = colors->vector_at(static_cast<uint32_t>(i));
        core::at(colors_, i) = {.x = vector->x(), .y = vector->y(), .z = vector->z(), .w = 1.0f};
    }
    ongoing_ = 0;
}

// 0x0041B440
void ColorCycleComponent::set_on(bool on)
{
    if (on_ && !on) {
        gsl::owner<script::Struct*> params = script::new_struct();
        params->put_name_key(checksums::Name{kName}, entity()->ident());
        params->put_int(checksums::Name{kColor}, kNeutralColor);
        set_object_color(entity()->ident(), params);
        delete_params(params);
    }
    if (on_) {
        on_ = on;
        return;
    }
    on_ = on;
    if (on) {
        clock_ = 0.0f;
        ongoing_ = 0;
    }
}

// 0x0041B580
void ColorCycleComponent::tick_frame()
{
    if (!on_)
        return;
    gsl::owner<script::Struct*> params = script::new_struct();
    const maths::Vec4& color = core::at(colors_, ongoing_);
    uint32_t rgb = static_cast<uint32_t>(get_truncated(color.z)) << 8;
    rgb = (rgb + static_cast<uint32_t>(get_truncated(color.y))) << 8;
    rgb += static_cast<uint32_t>(get_truncated(color.x));
    params->put_int(checksums::Name{kColor}, static_cast<int>(rgb));
    params->put_name_key(checksums::Name{kName}, entity()->ident());
    script::dump_to_log(params, 0);
    set_object_color(entity()->ident(), params);
    delete_params(params);
    // (compared before rounding, as the original keeps the sum on the FPU stack)
    const double time = frame_clock::get_slomo_frame_seconds() * kMilliseconds + clock_;
    clock_ = static_cast<float>(time);
    if (time >= period_) {
        clock_ = 0.0f;
        if (++ongoing_ >= color_count_)
            ongoing_ = 0;
    }
}

// 0x0041B560
int ColorCycleComponent::get_on(script::Struct*, script::Script*)
{
    set_on(true);
    return 1;
}

// 0x0041B570
int ColorCycleComponent::disengaged(script::Struct*, script::Script*)
{
    set_on(false);
    return 1;
}

// 0x0041B2B0
int ColorCycleComponent::set_period_from_script(script::Struct* params, script::Script*)
{
    float period = 0.0f;
    std::memcpy(&period, static_cast<const void*>(&params),
                sizeof(period)); // (the original's slot for it: the params argument)
    params->fetch_float(0u, &period, 1);
    period_ = period;
    return 1;
}

// 0x0041B390
int ColorCycleComponent::set_colors_from_script(script::Struct* params, script::Script*)
{
    script::Array* colors = nullptr; // (the original's default was the parameters' address, read as an array)
    params->fetch_list(0u, &colors, 1);
    if (colors)
        change_colors(colors);
    return 1;
}

} // namespace objects
