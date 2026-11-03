// The particle component (GameObjects::EffectsEmitterModule, particle_component.h): its life and its script member functions.
// The Nx particle systems themselves have no source yet: their methods are called as the game's.

#include "engine/objects/particle_component.h"
#include "platform/d3dx.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include "engine/gfx/particle_sets.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/collide_and_die.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "engine/objects/model_component.h"
#include <gsl/pointers>
#include <array>
#include <utility>
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace objects {

std::array<script::MemberFunctionEntry, 21>& particle_component_member_functions()
{
    static std::array<script::MemberFunctionEntry, 21> particle_component_member_functions{{
        {.callsign_ = 0x88BE2262, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0xBB6018F4, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0xE5134689, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0xF71D696E, .owner_kind_ = 0xA9DB601E},
        {.callsign_ = 0x0AAB006D, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0xE2B99038, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0xC036F4D0, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0xC6525C4E, .owner_kind_ = 0xA9DB601E},
        {.callsign_ = 0x31FCC8ED, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x19AF74D1, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x4AA40FB5, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x234CEABB, .owner_kind_ = 0xA9DB601E},
        {.callsign_ = 0x9E100F60, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x2F0DA461, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0xEEFFAE18, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x0FE869E8, .owner_kind_ = 0xA9DB601E},
        {.callsign_ = 0x19AA4CD3, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x5446AF14, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x55835CF2, .owner_kind_ = 0xA9DB601E}, {.callsign_ = 0x3D05AFE0, .owner_kind_ = 0xA9DB601E},
        {.callsign_ = 0, .owner_kind_ = 0xA9DB601E}}};
    return particle_component_member_functions;
}

namespace {

std::unique_ptr<render::EffectsDirector>& stored_particle_hub()
{
    static core::NoDestroy<std::unique_ptr<render::EffectsDirector>> particle_hub;
    return particle_hub.get();
}

} // namespace

render::EffectsDirector* particle_hub() { return stored_particle_hub().get(); }
void set_particle_hub(std::unique_ptr<render::EffectsDirector> owned) { stored_particle_hub() = std::move(owned); }

namespace {

constexpr uint32_t kSuspend = 0xCE0CA665; // the suspend component

// The particle system updated (the engine's 0x00494E20).
void get_refresh(render::Sparkle* particle) { particle->tick_frame(); }
// The unnamed float (required). The original reads it into its own `params` argument's slot: not given, the
// value is the pointer's bits.
float unnamed_float(script::Struct* params)
{
    float value = 0.0f;
    std::memcpy(&value, static_cast<const void*>(&params), 4);
    params->fetch_float(0u, &value, 1);
    return value;
}

// The parameters a particle system is made from (render::SparkleConfig, gfx/particles.h; constructed by 0x00496240,
// given to the particle manager's 0x00496510, kept as the system's +4).
using ParticleParams = render::SparkleConfig;

// D3DXMatrixMultiply (the library's, 0x005BFA8C): out = a * b.
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// An array's first three integers' low bytes into `rgb`; `last` the third.
void get_rgb(script::Array* array, std::span<uint8_t> rgb, uint32_t* last)
{
    for (uint32_t i = 0; i < 3; ++i) {
        *last = array->fetch_part(i);
        core::at(rgb, i) = static_cast<uint8_t>(*last);
    }
}

constexpr uint32_t kEmitting = 0x2299A9F2;   // (its name unknown): the particle system's vtable slot 7
constexpr uint32_t kGroundTest = 0x96896315; // (its name unknown)

// The unnamed array's first four integers' low bytes (r, g, b, a).
render::Rgba colour_of(script::Array* array)
{
    render::Rgba colour{};
    for (uint32_t i = 0; i < 4; ++i)
        core::at(colour, i) = static_cast<uint8_t>(array->fetch_part(i));
    return colour;
}

} // namespace

// --- life ---

// 0x0042A000
gsl::owner<ObjectModule*> EffectsEmitterModule::fabricate() { return new EffectsEmitterModule; }

// 0x00429940
void EffectsEmitterModule::publish_method_table()
{
    script::register_member_function_table(particle_component_member_functions());
}

// 0x00429B20
EffectsEmitterModule::EffectsEmitterModule()
{
    set_kind_tag(kType);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    start_time_ = frame_clock::game_clock_elapsed() + clock_offset();
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00429B90 (0x0042A060 the deleting one)
EffectsEmitterModule::~EffectsEmitterModule()
{
    if (effect_handle_)
        stored_particle_hub()->retire_particle(effect_handle_);
}

// 0x00429BF0
void EffectsEmitterModule::seal() { suspend_ = entity()->fetch_module(kSuspend); }

// 0x00429960
void EffectsEmitterModule::conceal(bool hide) { effect_handle_->conceal(hide); }

// --- script member functions ---

// 0x00429970
bool EffectsEmitterModule::set_emit_rate(script::Struct* params, script::Script*)
{
    const float value = unnamed_float(params);
    if (effect_handle_)
        effect_handle_->set_rate(value);
    return true;
}

// 0x004299A0
bool EffectsEmitterModule::set_lifespan(script::Struct* params, script::Script*)
{
    const float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->set_lifespan(value);
        get_refresh(effect_handle_);
    }
    return true;
}

// 0x004299E0
bool EffectsEmitterModule::set_float_19a_f_74d_1(script::Struct* params, script::Script*)
{
    const float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->set_colour_mid_percent(value);
        get_refresh(effect_handle_);
    }
    return true;
}

// 0x00429A20
bool EffectsEmitterModule::change_midpoint_fraction(script::Struct* params, script::Script*)
{
    const float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->args().mid_percent() = value;
        get_refresh(effect_handle_);
    }
    return true;
}

namespace {
bool set_reach(EffectsEmitterModule* component, script::Struct* params, int which)
{
    const float value = unnamed_float(params);
    if (component->effect_handle()) {
        component->effect_handle()->change_extent(which, value);
        get_refresh(component->effect_handle());
    }
    return true;
}
bool change_location(EffectsEmitterModule* component, script::Struct* params, int which)
{
    maths::Vec4 pos{};
    params->fetch_vec(0u, &pos, 1);
    if (component->effect_handle()) {
        component->effect_handle()->change_place(which, &pos);
        get_refresh(component->effect_handle());
    }
    return true;
}
bool set_box(EffectsEmitterModule* component, script::Struct* params, int which)
{
    maths::Vec4 box{};
    params->fetch_vec(0u, &box, 1);
    if (component->effect_handle())
        component->effect_handle()->set_box(which, &box);
    return true;
}
bool change_colour(EffectsEmitterModule* component, script::Struct* params, int which)
{
    script::Array* array = nullptr;
    params->fetch_list(0u, &array, 1);
    if (component->effect_handle()) {
        const render::Rgba colour = colour_of(array);
        component->effect_handle()->change_colour(which, colour);
    }
    return true;
}
} // namespace

bool EffectsEmitterModule::set_start_radius(script::Struct* params, script::Script*)
{
    return set_reach(this, params, 0);
} // 0x00429A60
bool EffectsEmitterModule::set_mid_radius(script::Struct* params, script::Script*)
{
    return set_reach(this, params, 1);
} // 0x00429AA0
bool EffectsEmitterModule::set_end_radius(script::Struct* params, script::Script*)
{
    return set_reach(this, params, 2);
} // 0x00429AE0
bool EffectsEmitterModule::set_start_pos(script::Struct* params, script::Script*)
{
    return change_location(this, params, 0);
} // 0x00429C10
bool EffectsEmitterModule::set_mid_pos(script::Struct* params, script::Script*)
{
    return change_location(this, params, 1);
} // 0x00429C50
bool EffectsEmitterModule::set_end_pos(script::Struct* params, script::Script*)
{
    return change_location(this, params, 2);
} // 0x00429C90
bool EffectsEmitterModule::set_start_box(script::Struct* params, script::Script*)
{
    return set_box(this, params, 0);
} // 0x00429CD0
bool EffectsEmitterModule::set_mid_box(script::Struct* params, script::Script*)
{
    return set_box(this, params, 1);
} // 0x00429D10
bool EffectsEmitterModule::set_end_box(script::Struct* params, script::Script*)
{
    return set_box(this, params, 2);
} // 0x00429D50
bool EffectsEmitterModule::set_start_color(script::Struct* params, script::Script*)
{
    return change_colour(this, params, 0);
} // 0x00429DD0
bool EffectsEmitterModule::set_mid_color(script::Struct* params, script::Script*)
{
    return change_colour(this, params, 1);
} // 0x00429E50
bool EffectsEmitterModule::set_end_color(script::Struct* params, script::Script*)
{
    return change_colour(this, params, 2);
} // 0x00429ED0

// 0x00429D90
bool EffectsEmitterModule::set_emit_target(script::Struct* params, script::Script*)
{
    params->read_hash_value(0u, &emit_target_, 1);
    if (effect_handle_)
        effect_handle_->set_emit_script(emit_target_, entity()->ident());
    return true;
}

// 0x00429F50
bool EffectsEmitterModule::set_flag_5446a_f14(script::Struct* params, script::Script*)
{
    int value = 0;
    params->fetch_int_value(0u, &value, 0);
    if (effect_handle_)
        effect_handle_->args().set_cull_outside(value == 1);
    return true;
}

// 0x00429F90
bool EffectsEmitterModule::set_color_55835c_f2(script::Struct* params, script::Script*)
{
    script::Array* array = nullptr;
    params->fetch_list(0u, &array, 1);
    if (effect_handle_) {
        const render::Rgba colour = colour_of(array);
        effect_handle_->change_colour(&colour);
    }
    return true;
}

// 0x0042A090
void EffectsEmitterModule::configure_from_data(script::Struct* params)
{
    ParticleParams p;
    float scale = 1.0f;
    p.set_emit_script(0);
    if (params->read_hash_value(0x092276DBu, &emit_target_, 0)) {
        p.set_emit_script(emit_target_);
        p.set_entity_key(entity()->ident());
    }
    params->fetch_int_value(0x5A235299u, &lifespan_, 0);
    params->read_hash_value(0xA1DC81F9u, &p.label(), 1);
    params->read_hash_value("GroupID", &p.group_id(), 0);
    params->read_hash_value(0x7321A8D6u, &p.unknown04(), 0);
    p.set_use_mid(params->has_bare_flag(0xA7A5BFFBu));
    p.set_cull_outside(params->has_bare_flag(0x5446AF14u));
    params->fetch_float(0x13B9DA7Bu, &scale, 0);
    params->fetch_float(0x3BEE67C8u, &core::at(p.extent(), 0), 0);
    params->fetch_float(0x8E39913Du, &core::at(p.extent(), 1), 0);
    params->fetch_float(0x3F243A3Cu, &core::at(p.extent(), 2), 0);

    // The orientation: the identity, or turned to face 0x0C4C809E (normalised).
    maths::Mat4 orient{};
    core::at(orient.row, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(orient.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(orient.row, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(orient.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    if (params->has_bare_flag(0x7045843Au)) {
        maths::Vec4 at{};
        params->fetch_vec(0x0C4C809Eu, &at, 1);
        at.make_unit();
        core::at(orient.row, 2) = at;
        const maths::Vec4 up = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
        core::at(orient.row, 0).w = 0.0f;
        if (core::at(orient.row, 2) == up) {
            // (straight up: across from (0, 0, -1))
            auto t = static_cast<float>(static_cast<double>(at.z) * 0.0);
            core::at(orient.row, 0).x = static_cast<float>(static_cast<double>(at.y) * -1.0 - t);
            core::at(orient.row, 0).y = static_cast<float>(t - static_cast<double>(at.x) * -1.0);
            core::at(orient.row, 0).z = static_cast<float>(static_cast<double>(at.x) * 0.0 - static_cast<double>(at.y) * 0.0);
        } else {
            // (across from (0, 1, 0))
            const double y0 = static_cast<double>(at.y) * 0.0;
            core::at(orient.row, 0).x = static_cast<float>(y0 - at.z);
            core::at(orient.row, 0).y = static_cast<float>(static_cast<double>(at.z) * 0.0 - static_cast<double>(at.x) * 0.0);
            core::at(orient.row, 0).z = static_cast<float>(at.x - y0);
        }
        const maths::Vec4& r = core::at(orient.row, 0);
        core::at(orient.row, 1).w = 0.0f;
        core::at(orient.row, 1).x = static_cast<float>(static_cast<double>(at.y) * r.z - static_cast<double>(at.z) * r.y);
        core::at(orient.row, 1).y = static_cast<float>(static_cast<double>(at.z) * r.x - static_cast<double>(at.x) * r.z);
        core::at(orient.row, 1).z = static_cast<float>(static_cast<double>(at.x) * r.y - static_cast<double>(at.y) * r.x);
    }
    for (float& size : p.extent())
        size = static_cast<float>(static_cast<double>(size) * scale);
    params->fetch_float(0x05400A25u, &core::at(p.radius(), 0), 0);
    params->fetch_float(0xFF7D9AA9u, &core::at(p.radius(), 1), 0);
    params->fetch_float(0x5539FCEEu, &core::at(p.radius(), 2), 0);
    for (float& radius : p.radius())
        radius = static_cast<float>(static_cast<double>(radius) * scale);
    params->fetch_int_value(0x9E52A20Fu, &p.unknown24(), 0);
    params->fetch_float(0x35B65BD4u, &p.rate(), 0);
    params->fetch_float(0xC218CF77u, &p.lifetime(), 0);
    params->fetch_float(0x520682C6u, &p.mid_percent(), 0);

    // The positions and boxes: scaled, then turned by the orientation.
    auto scale_and_turn = [&](std::span<maths::Vec4, 3> v) {
        for (maths::Vec4& element : v) {
            element = {.x = static_cast<float>(static_cast<double>(element.x) * scale),
                       .y = static_cast<float>(static_cast<double>(element.y) * scale),
                       .z = static_cast<float>(static_cast<double>(element.z) * scale),
                       .w = static_cast<float>(static_cast<double>(element.w) * scale)};
        }
        for (maths::Vec4& element : v) {
            maths::Vec4 turned{};
            element = orient.xform(turned, element);
        }
    };
    auto vector = [&](uint32_t name, maths::Vec4* out) {
        params->fetch_vec(name, out, 0);
    };
    vector(params->has_bare_flag(0x64F194EDu) ? 0x3D5EA6A4u : 0x7F261953u, &core::at(p.at_point(), 0));
    vector(0x004B51A2u, &core::at(p.at_point(), 1));
    vector(0x5854AB9Eu, &core::at(p.at_point(), 2));
    scale_and_turn(p.at_point());
    vector(0x9656F775u, &core::at(p.box(), 0));
    vector(0xC3CD20A2u, &core::at(p.box(), 1));
    vector(0x829FE7DDu, &core::at(p.box(), 2));
    scale_and_turn(p.box());
    p.set_colour_use_mid(params->has_bare_flag(0x454962EFu));
    params->fetch_float(0x59463C93u, &p.mid_percent(), 0);

    // The colours (rgb from three arrays; the alphas at most 128).
    uint32_t value = 0;
    script::Array* array = nullptr;
    static const std::array<uint32_t, 3> kColours = {{0xDAB75B6Cu, 0x0444769Au, 0x5A3728E7u}};
    for (int i = 0; i < 3; ++i)
        if (params->fetch_list(core::at(kColours, i), &array, 0))
            get_rgb(array, core::at(p.shade(), i), &value);
    static const std::array<uint32_t, 3> kAlphas = {{0x4D83DB4Cu, 0x4ABA1FF1u, 0x5CF83ACAu}};
    for (int i = 0; i < 3; ++i) {
        if (params->fetch_int_value(core::at(kAlphas, i), &value, 0)) {
            auto alpha = static_cast<uint8_t>(value);
            core::at(core::at(p.shade(), i), 3) = alpha > 0x80 ? 0x80 : alpha;
        }
    }
    params->read_hash_value(0xDD6BB3D5u, &p.blend_mode(), 0);
    params->fetch_int_value(0x9F677D4Eu, &p.unknown13c(), 0);
    params->fetch_int_value(0x13D0BE1Du, &p.alpha_cut_off(), 0);
    params->read_hash_value(0x7D99F28Du, &p.texture(), 0);
    params->fetch_int_value(0x4EEDFAE7u, &p.unknown144(), 0);
    params->fetch_int_value(0xD7E4AB5Du, &p.unknown148(), 0);
    params->fetch_int_value(0xE4279BA2u, &p.unknown14c(), 0);
    int byte_value = 0;
    params->fetch_int_value(0x9D5923D8u, &byte_value, 0);
    p.set_unknown150(static_cast<uint8_t>(byte_value));

    // The rotation: X, Y, then Z.
    core::at(p.orientation_turn().row, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(p.orientation_turn().row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(p.orientation_turn().row, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(p.orientation_turn().row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::Vec4 angles = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    params->fetch_vec(0x9D2D0915u, &angles, 0);
    maths::Mat4 rotation{};
    matrix_multiply(&p.orientation_turn(), &p.orientation_turn(), &maths::make_pitch_matrix(rotation, angles.x));
    matrix_multiply(&p.orientation_turn(), &p.orientation_turn(), &maths::make_yaw_matrix(rotation, angles.y));
    matrix_multiply(&p.orientation_turn(), &p.orientation_turn(), &maths::make_roll_matrix(rotation, angles.z));

    p.set_local_positions(params->has_bare_flag(0xB7B7240Du));
    if (p.local_positions())
        for (int i = 0; i < 3; ++i)
            core::at(p.offset_pos(), i) = core::at(p.at_point(), i);
    p.set_orient_with_object(params->has_bare_flag(0xA2487CC3u));
    p.set_flag158(params->has_bare_flag(0xA0E585B0u));
    effect_handle_ = stored_particle_hub()->spawn_particle(&p);

    // The model it follows (0x5E2A8D9F: an object's; no such object - a crash, as the original).
    uint32_t model = 0;
    if (params->read_hash_value(0x5E2A8D9Fu, &model, 0) && model)
        mesh_instance_ = core::down_cast<GameEntity>(find_object(model))->fetch_module(0x286A8D26);
    if (p.emit_script()) {
        script::Struct* script_params = nullptr;
        params->read_sub_struct(0x7031F10Cu, &script_params, 0);
        entity()->start_script_task(p.emit_script(), 0, false, false, script_params);
    }
    uint32_t value54 = 0xE3714DC1;
    params->read_hash_value(0x7E4813A7u, &value54, 0);
    effect_handle_->args().set_unknown50(value54);
    if (p.flag158())
        effect_handle_->change_colour(nullptr);
}

// 0x0042AD90
void EffectsEmitterModule::tick_frame()
{
    // (suspended: nothing - unless a network game runs and it has a lifetime)
    if (suspend_ && core::down_cast<DormancyModule>(suspend_)->is_far()) {
        void* net = static_cast<void*>(net_hub());
        if (!static_cast<online::Hub*>(net)->in_net_game() || lifespan_ == 0)
            return;
    }
    if (lifespan_ != 0) {
        auto age = static_cast<uint32_t>(frame_clock::game_clock_elapsed() + (clock_offset() - start_time_));
        if (age > lifespan_) {
            entity()->flag_as_dead();
            return;
        }
    }
    if (mesh_instance_)
        effect_handle_->set_brightness(core::down_cast<MeshInstanceModule>(mesh_instance_)->brightness());
    const render::SparkleConfig& args = effect_handle_->args();
    if (!args.local_positions())
        return;
    for (int i = 0; i < 3; ++i) {
        const maths::Vec4& local = core::at(args.offset_pos(), i);
        const maths::Vec4& at = entity()->at_point();
        const maths::Vec4 pos = {.x = local.x + at.x, .y = local.y + at.y, .z = local.z + at.z, .w = local.w + at.w};
        effect_handle_->change_place(i, &pos);
        if (args.orient_with_object()) {
            maths::Mat4 orientation{};
            orientation = entity()->basis_frame();
            core::at(orientation.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
            effect_handle_->spin_by(i, &orientation);
        }
    }
}

// 0x0042AEE0
int EffectsEmitterModule::invoke_method_by_name(uint32_t name, script::Struct*, script::Script*)
{
    if (name == kEmitting) {
        return effect_handle_->is_emitting() ? 1 : 0;
    }
    if (name == kGroundTest) {
        // A line from just above the particle system's position to 4800 below it: the face hit's vertex value,
        // doubled (1 without a hit).
        float value = 1.0f;
        render::ProbeRay feeler;
        const maths::Vec4& at = core::at(effect_handle_->args().at_point(), 0);
        feeler.line().start = {.x = at.x, .y = static_cast<float>(at.y + 1.0f), .z = at.z, .w = at.w};
        feeler.line().end = {.x = at.x, .y = static_cast<float>(static_cast<double>(at.y) - 4800.0), .z = at.z, .w = at.w};
        if (feeler.get_hit_shape(true, false))
            value = static_cast<float>(feeler.get_hit_vertex_value() * 2.0);
        if (effect_handle_)
            effect_handle_->set_brightness(value);
        return 1;
    }
    return 2;
}

// 0x0042B040
bool EffectsEmitterModule::get_particle_params(script::Struct*, script::Script* script)
{
    gsl::owner<script::Struct*> result = script::spawn_param_bag();
    if (result)
        result = new (result) script::Struct;
    // (the name, two floats and the colours read straight from the system's +4; the rest through the getter, once
    // for each value)
    const ParticleParams* own = &effect_handle_->args();
    result->put_name_key(checksums::Name{0xA1DC81F9u}, own->label());
    result->push_float(checksums::Name{0x35B65BD4u}, own->rate());
    result->push_float(checksums::Name{0xC218CF77u}, own->lifetime());
    std::array<script::Array*, 3> colours{};
    for (script::Array*& array : colours) {
        gsl::owner<script::Array*> made = script::new_array();
        array = made;
    }
    std::array<std::array<uint8_t, 4>, 3> colour{};
    std::memcpy(colour.data(), own->shade().data(), sizeof(colour));
    for (script::Array* array : colours)
        array->configure_size_kind(4, script::kQbvalInteger);
    for (int i = 0; i < 3; ++i)
        for (uint32_t j = 0; j < 4; ++j)
            core::at(colours, i)->set_element(j, core::at(core::at(colour, i), j));
    result->append_array_value(0x7E6BCBE6u, core::at(colours, 0));
    result->append_array_value(0x2A6B6FE2u, core::at(colours, 1));
    result->append_array_value(0x0481113Du, core::at(colours, 2));
    auto get = [this] { return &effect_handle_->args(); };
    auto add_vector = [&](uint32_t name, const maths::Vec4& v) {
        result->put_vec3(checksums::Name{name}, script::VectorValue{.x = v.x, .y = v.y, .z = v.z, .w = v.w});
    };
    add_vector(0x7B5A25F8u, core::at(get()->at_point(), 0));
    add_vector(0xA5A9080Eu, core::at(get()->at_point(), 1));
    add_vector(0xFBDA5673u, core::at(get()->at_point(), 2));
    result->push_float(checksums::Name{0x3BEE67C8u}, core::at(get()->extent(), 0));
    result->push_float(checksums::Name{0x8E39913Du}, core::at(get()->extent(), 1));
    result->push_float(checksums::Name{0x3F243A3Cu}, core::at(get()->extent(), 2));
    add_vector(0x9656F775u, core::at(get()->box(), 0));
    add_vector(0xC3CD20A2u, core::at(get()->box(), 1));
    add_vector(0x829FE7DDu, core::at(get()->box(), 2));
    script->args()->put_sub_struct(0xB91EE381u, result);
    for (script::Array* array : colours) {
        if (array) {
            array->get_clear();
            script::delete_array(array);
        }
    }
    if (result) {
        result->get_clear();
        script::delete_struct(result);
    }
    return true;
}

} // namespace objects

