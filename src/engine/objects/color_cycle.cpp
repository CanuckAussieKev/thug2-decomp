// The colour cycle component (GameObjects::ColorCycleComponent): see color_cycle.h. From the disassembly of
// 0x0041B280-0x0041B57F.

#include "engine/objects/color_cycle.h"
#include "engine/script/game_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

uint32_t g_color_cycle_member_functions[30]{0x4AF8B9AE, 0xCDC76A22, 0, 0, 0, 0, 0x1DE2F35B, 0xCDC76A22, 0, 0, 0, 0,
                                            0x4A752AAE, 0xCDC76A22, 0, 0, 0, 0, 0x80D5F732, 0xCDC76A22, 0, 0, 0, 0,
                                            0,          0xCDC76A22, 0, 0, 0, 0};

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

script::Struct* new_params()
{
    script::Struct* params = script::spawn_param_bag();
    return params ? new (params) script::Struct : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear(); // (the destructor)
        script::delete_struct(params);
    }
}

} // namespace

// 0x0041B3C0
memory::owner<ObjectModule*> __cdecl ColorCycleComponent::fabricate() { return new ColorCycleComponent; }

// (the constructor: inlined in fabricate in the original)
ColorCycleComponent::ColorCycleComponent()
{
    color_count_ = 0;
    ongoing_ = 0;
    clock_ = 0.0f;
    on_ = false;
    kind_tag_ = kType;
}

// 0x0041B280
void __cdecl ColorCycleComponent::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_color_cycle_member_functions, 0x18);
}

// 0x0041B2A0 (0x0041B2E0 the deleting one)
ColorCycleComponent::~ColorCycleComponent() {}

// 0x0041B310
void ColorCycleComponent::change_colors(script::Array* colors)
{
    int count = static_cast<int>(colors->extent_);
    color_count_ = count;
    for (int i = 0; i < count; ++i) {
        auto* vector = reinterpret_cast<const float*>(colors->fetch_part(static_cast<uint32_t>(i)));
        colors_[i] = {vector[1], vector[2], vector[3], 1.0f}; // (a CVector: its coordinates from +4)
    }
    ongoing_ = 0;
}

// 0x0041B440
void ColorCycleComponent::set_on(bool on)
{
    if (on_ && !on) {
        script::Struct* params = new_params();
        params->put_name_key(kName, entity_->ident_);
        params->put_int(kColor, kNeutralColor);
        set_object_color(entity_->ident_, params);
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
    script::Struct* params = new_params();
    const maths::Vec4& color = colors_[ongoing_];
    uint32_t rgb = static_cast<uint32_t>(get_truncated(color.z)) << 8;
    rgb = (rgb + static_cast<uint32_t>(get_truncated(color.y))) << 8;
    rgb += static_cast<uint32_t>(get_truncated(color.x));
    params->put_int(kColor, static_cast<int>(rgb));
    params->put_name_key(kName, entity_->ident_);
    script::dump_to_log(params, 0);
    set_object_color(entity_->ident_, params);
    delete_params(params);
    // (compared before rounding, as the original keeps the sum on the FPU stack)
    double time = frame_clock::get_slomo_frame_seconds() * kMilliseconds + clock_;
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
    float period;
    std::memcpy(&period, &params, sizeof(period)); // (the original's slot for it: the params argument)
    params->fetch_float(0u, &period, 1);
    period_ = period;
    return 1;
}

// 0x0041B390
int ColorCycleComponent::set_colors_from_script(script::Struct* params, script::Script*)
{
    auto* colors = reinterpret_cast<script::Array*>(params); // (the original's slot for it: the params argument)
    params->fetch_list(0u, &colors, 1);
    change_colors(colors);
    return 1;
}

} // namespace objects
