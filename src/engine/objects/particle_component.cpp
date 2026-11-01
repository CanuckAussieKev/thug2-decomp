// The particle component (GameObjects::EffectsEmitterModule, particle_component.h): its life and its script member functions.
// The Nx particle systems themselves have no source yet: their methods are called as the game's.

#include "engine/objects/particle_component.h"
#include "engine/gfx/d3dx.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_particles.h"
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
#include "engine/core/owner.h"

namespace objects {

uint32_t g_particle_component_member_functions[126]{
    0x88BE2262, 0xA9DB601E, 0, 0, 0, 0, 0xBB6018F4, 0xA9DB601E, 0, 0, 0, 0, 0xE5134689, 0xA9DB601E, 0, 0, 0, 0,
    0xF71D696E, 0xA9DB601E, 0, 0, 0, 0, 0x0AAB006D, 0xA9DB601E, 0, 0, 0, 0, 0xE2B99038, 0xA9DB601E, 0, 0, 0, 0,
    0xC036F4D0, 0xA9DB601E, 0, 0, 0, 0, 0xC6525C4E, 0xA9DB601E, 0, 0, 0, 0, 0x31FCC8ED, 0xA9DB601E, 0, 0, 0, 0,
    0x19AF74D1, 0xA9DB601E, 0, 0, 0, 0, 0x4AA40FB5, 0xA9DB601E, 0, 0, 0, 0, 0x234CEABB, 0xA9DB601E, 0, 0, 0, 0,
    0x9E100F60, 0xA9DB601E, 0, 0, 0, 0, 0x2F0DA461, 0xA9DB601E, 0, 0, 0, 0, 0xEEFFAE18, 0xA9DB601E, 0, 0, 0, 0,
    0x0FE869E8, 0xA9DB601E, 0, 0, 0, 0, 0x19AA4CD3, 0xA9DB601E, 0, 0, 0, 0, 0x5446AF14, 0xA9DB601E, 0, 0, 0, 0,
    0x55835CF2, 0xA9DB601E, 0, 0, 0, 0, 0x3D05AFE0, 0xA9DB601E, 0, 0, 0, 0, 0,          0xA9DB601E, 0, 0, 0, 0};
memory::owner<render::EffectsDirector*> g_particle_hub;

namespace {

constexpr uint32_t kSuspend = 0xCE0CA665; // the suspend component

// The particle system updated (the engine's 0x00494E20).
void get_refresh(render::Sparkle* particle) { particle->tick_frame(); }
// The unnamed float (required). The original reads it into its own `params` argument's slot: not given, the
// value is the pointer's bits.
float unnamed_float(script::Struct* params)
{
    float value;
    std::memcpy(&value, &params, 4);
    params->fetch_float(0u, &value, 1);
    return value;
}

// The parameters a particle system is made from (render::SparkleConfig, gfx/particles.h; constructed by 0x00496240,
// given to the particle manager's 0x00496510, kept as the system's +4).
using ParticleParams = render::SparkleConfig;

// D3DXMatrixMultiply (the library's, 0x005BFA8C): out = a * b.
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// An array's first three integers' low bytes into `rgb`; `last` the third.
void get_rgb(script::Array* array, uint8_t* rgb, uint32_t* last)
{
    for (uint32_t i = 0; i < 3; ++i) {
        *last = array->fetch_part(i);
        rgb[i] = static_cast<uint8_t>(*last);
    }
}

constexpr uint32_t kEmitting = 0x2299A9F2;   // (its name unknown): the particle system's vtable slot 7
constexpr uint32_t kGroundTest = 0x96896315; // (its name unknown)

// The unnamed array's first four integers' low bytes (r, g, b, a).
struct Colour {
    uint8_t rgba[4];
};
Colour colour_of(script::Array* array)
{
    Colour colour;
    for (uint32_t i = 0; i < 4; ++i)
        colour.rgba[i] = static_cast<uint8_t>(array->fetch_part(i));
    return colour;
}

} // namespace

// --- life ---

// 0x0042A000
memory::owner<ObjectModule*> __cdecl EffectsEmitterModule::fabricate() { return new EffectsEmitterModule; }

// 0x00429940
void __cdecl EffectsEmitterModule::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_particle_component_member_functions, 0x18);
}

// 0x00429B20
EffectsEmitterModule::EffectsEmitterModule()
{
    kind_tag_ = kType;
    emit_target_ = 0;
    effect_handle_ = nullptr;
    mesh_instance_ = nullptr;
    lifespan_ = 0;
    start_time_ = frame_clock::game_clock_elapsed() + g_clock_offset;
}

// 0x00429B90 (0x0042A060 the deleting one)
EffectsEmitterModule::~EffectsEmitterModule()
{
    if (effect_handle_)
        g_particle_hub->retire_particle(effect_handle_);
}

// 0x00429BF0
void EffectsEmitterModule::seal() { suspend_ = entity_->fetch_module(kSuspend); }

// 0x00429960
void EffectsEmitterModule::conceal(bool hide) { effect_handle_->conceal(hide); }

// --- script member functions ---

// 0x00429970
bool EffectsEmitterModule::set_emit_rate(script::Struct* params, script::Script*)
{
    float value = unnamed_float(params);
    if (effect_handle_)
        effect_handle_->set_rate(value);
    return true;
}

// 0x004299A0
bool EffectsEmitterModule::set_lifespan(script::Struct* params, script::Script*)
{
    float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->set_lifespan(value);
        get_refresh(effect_handle_);
    }
    return true;
}

// 0x004299E0
bool EffectsEmitterModule::set_float_19a_f_74d_1(script::Struct* params, script::Script*)
{
    float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->set_colour_mid_percent(value);
        get_refresh(effect_handle_);
    }
    return true;
}

// 0x00429A20
bool EffectsEmitterModule::change_midpoint_fraction(script::Struct* params, script::Script*)
{
    float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->args_.mid_percent_ = value;
        get_refresh(effect_handle_);
    }
    return true;
}

namespace {
bool set_reach(EffectsEmitterModule* component, script::Struct* params, int which)
{
    float value = unnamed_float(params);
    if (component->effect_handle_) {
        component->effect_handle_->change_extent(which, value);
        get_refresh(component->effect_handle_);
    }
    return true;
}
bool change_location(EffectsEmitterModule* component, script::Struct* params, int which)
{
    maths::Vec4 pos;
    params->fetch_vec(0u, reinterpret_cast<script::VectorValue*>(&pos), 1);
    if (component->effect_handle_) {
        component->effect_handle_->change_place(which, &pos);
        get_refresh(component->effect_handle_);
    }
    return true;
}
bool set_box(EffectsEmitterModule* component, script::Struct* params, int which)
{
    maths::Vec4 box;
    params->fetch_vec(0u, reinterpret_cast<script::VectorValue*>(&box), 1);
    if (component->effect_handle_)
        component->effect_handle_->set_box(which, &box);
    return true;
}
bool change_colour(EffectsEmitterModule* component, script::Struct* params, int which)
{
    script::Array* array;
    params->fetch_list(0u, &array, 1);
    if (component->effect_handle_) {
        Colour colour = colour_of(array);
        component->effect_handle_->change_colour(which, reinterpret_cast<const uint32_t*>(&colour));
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
        effect_handle_->set_emit_script(emit_target_, entity_->ident_);
    return true;
}

// 0x00429F50
bool EffectsEmitterModule::set_flag_5446a_f14(script::Struct* params, script::Script*)
{
    int value = 0;
    params->fetch_int_value(0u, &value, 0);
    if (effect_handle_)
        effect_handle_->args_.cull_outside_ = value == 1;
    return true;
}

// 0x00429F90
bool EffectsEmitterModule::set_color_55835c_f2(script::Struct* params, script::Script*)
{
    script::Array* array;
    params->fetch_list(0u, &array, 1);
    if (effect_handle_) {
        Colour colour = colour_of(array);
        effect_handle_->change_colour(reinterpret_cast<const uint8_t*>(&colour));
    }
    return true;
}

// 0x0042A090
void EffectsEmitterModule::configure_from_data(script::Struct* params)
{
    ParticleParams p;
    p.make_new();
    float scale = 1.0f;
    p.emit_script_ = 0;
    if (params->read_hash_value(0x092276DBu, &emit_target_, 0)) {
        p.emit_script_ = emit_target_;
        p.entity_key_ = entity_->ident_;
    }
    params->fetch_int_value(0x5A235299u, reinterpret_cast<int*>(&lifespan_), 0);
    params->read_hash_value(0xA1DC81F9u, &p.label_, 1);
    params->read_hash_value("GroupID", &p.group_id_, 0);
    params->read_hash_value(0x7321A8D6u, &p.unknown04_, 0);
    p.use_mid_ = params->has_bare_flag(0xA7A5BFFBu);
    p.cull_outside_ = params->has_bare_flag(0x5446AF14u);
    params->fetch_float(0x13B9DA7Bu, &scale, 0);
    params->fetch_float(0x3BEE67C8u, &p.extent_[0], 0);
    params->fetch_float(0x8E39913Du, &p.extent_[1], 0);
    params->fetch_float(0x3F243A3Cu, &p.extent_[2], 0);

    // The orientation: the identity, or turned to face 0x0C4C809E (normalised).
    maths::Mat4 orient;
    orient.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    orient.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    orient.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    orient.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->has_bare_flag(0x7045843Au)) {
        maths::Vec4 at;
        params->fetch_vec(0x0C4C809Eu, reinterpret_cast<script::VectorValue*>(&at), 1);
        at.make_unit();
        orient.row[2] = at;
        const maths::Vec4 up = {0.0f, 1.0f, 0.0f, 1.0f};
        orient.row[0].w = 0.0f;
        if (orient.row[2] == up) {
            // (straight up: across from (0, 0, -1))
            float t = static_cast<float>(static_cast<double>(at.z) * 0.0);
            orient.row[0].x = static_cast<float>(static_cast<double>(at.y) * -1.0 - t);
            orient.row[0].y = static_cast<float>(t - static_cast<double>(at.x) * -1.0);
            orient.row[0].z = static_cast<float>(static_cast<double>(at.x) * 0.0 - static_cast<double>(at.y) * 0.0);
        } else {
            // (across from (0, 1, 0))
            double y0 = static_cast<double>(at.y) * 0.0;
            orient.row[0].x = static_cast<float>(y0 - at.z);
            orient.row[0].y = static_cast<float>(static_cast<double>(at.z) * 0.0 - static_cast<double>(at.x) * 0.0);
            orient.row[0].z = static_cast<float>(at.x - y0);
        }
        const maths::Vec4& r = orient.row[0];
        orient.row[1].w = 0.0f;
        orient.row[1].x = static_cast<float>(static_cast<double>(at.y) * r.z - static_cast<double>(at.z) * r.y);
        orient.row[1].y = static_cast<float>(static_cast<double>(at.z) * r.x - static_cast<double>(at.x) * r.z);
        orient.row[1].z = static_cast<float>(static_cast<double>(at.x) * r.y - static_cast<double>(at.y) * r.x);
    }
    for (float& size : p.extent_)
        size = static_cast<float>(static_cast<double>(size) * scale);
    params->fetch_float(0x05400A25u, &p.radius_[0], 0);
    params->fetch_float(0xFF7D9AA9u, &p.radius_[1], 0);
    params->fetch_float(0x5539FCEEu, &p.radius_[2], 0);
    for (float& radius : p.radius_)
        radius = static_cast<float>(static_cast<double>(radius) * scale);
    params->fetch_int_value(0x9E52A20Fu, &p.unknown24_, 0);
    params->fetch_float(0x35B65BD4u, &p.rate_, 0);
    params->fetch_float(0xC218CF77u, &p.lifetime_, 0);
    params->fetch_float(0x520682C6u, &p.mid_percent_, 0);

    // The positions and boxes: scaled, then turned by the orientation.
    auto scale_and_turn = [&](maths::Vec4* v) {
        for (int i = 0; i < 3; ++i) {
            v[i] = {static_cast<float>(static_cast<double>(v[i].x) * scale),
                    static_cast<float>(static_cast<double>(v[i].y) * scale),
                    static_cast<float>(static_cast<double>(v[i].z) * scale),
                    static_cast<float>(static_cast<double>(v[i].w) * scale)};
        }
        for (int i = 0; i < 3; ++i) {
            maths::Vec4 turned;
            v[i] = orient.xform(turned, v[i]);
        }
    };
    auto vector = [&](uint32_t name, maths::Vec4* out) {
        params->fetch_vec(name, reinterpret_cast<script::VectorValue*>(out), 0);
    };
    vector(params->has_bare_flag(0x64F194EDu) ? 0x3D5EA6A4u : 0x7F261953u, &p.at_point_[0]);
    vector(0x004B51A2u, &p.at_point_[1]);
    vector(0x5854AB9Eu, &p.at_point_[2]);
    scale_and_turn(p.at_point_);
    vector(0x9656F775u, &p.box_[0]);
    vector(0xC3CD20A2u, &p.box_[1]);
    vector(0x829FE7DDu, &p.box_[2]);
    scale_and_turn(p.box_);
    p.colour_use_mid_ = params->has_bare_flag(0x454962EFu);
    params->fetch_float(0x59463C93u, &p.mid_percent_, 0);

    // The colours (rgb from three arrays; the alphas at most 128).
    uint32_t value = 0;
    script::Array* array;
    static const uint32_t kColours[3] = {0xDAB75B6Cu, 0x0444769Au, 0x5A3728E7u};
    for (int i = 0; i < 3; ++i)
        if (params->fetch_list(kColours[i], &array, 0))
            get_rgb(array, p.shade_[i], &value);
    static const uint32_t kAlphas[3] = {0x4D83DB4Cu, 0x4ABA1FF1u, 0x5CF83ACAu};
    for (int i = 0; i < 3; ++i) {
        if (params->fetch_int_value(kAlphas[i], reinterpret_cast<int*>(&value), 0)) {
            uint8_t alpha = static_cast<uint8_t>(value);
            p.shade_[i][3] = alpha > 0x80 ? 0x80 : alpha;
        }
    }
    params->read_hash_value(0xDD6BB3D5u, &p.blend_mode_, 0);
    params->fetch_int_value(0x9F677D4Eu, &p.unknown13c_, 0);
    params->fetch_int_value(0x13D0BE1Du, &p.alpha_cut_off_, 0);
    params->read_hash_value(0x7D99F28Du, &p.texture_, 0);
    params->fetch_int_value(0x4EEDFAE7u, &p.unknown144_, 0);
    params->fetch_int_value(0xD7E4AB5Du, &p.unknown148_, 0);
    params->fetch_int_value(0xE4279BA2u, &p.unknown14c_, 0);
    int byte_value = 0;
    params->fetch_int_value(0x9D5923D8u, &byte_value, 0);
    p.unknown150_ = static_cast<uint8_t>(byte_value);

    // The rotation: X, Y, then Z.
    p.orientation_turn_.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    p.orientation_turn_.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    p.orientation_turn_.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    p.orientation_turn_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Vec4 angles = {0.0f, 0.0f, 0.0f, 1.0f};
    params->fetch_vec(0x9D2D0915u, reinterpret_cast<script::VectorValue*>(&angles), 0);
    maths::Mat4 rotation;
    matrix_multiply(&p.orientation_turn_, &p.orientation_turn_, &maths::make_pitch_matrix(rotation, angles.x));
    matrix_multiply(&p.orientation_turn_, &p.orientation_turn_, &maths::make_yaw_matrix(rotation, angles.y));
    matrix_multiply(&p.orientation_turn_, &p.orientation_turn_, &maths::make_roll_matrix(rotation, angles.z));

    p.local_positions_ = params->has_bare_flag(0xB7B7240Du);
    if (p.local_positions_)
        for (int i = 0; i < 3; ++i)
            p.offset_pos_[i] = p.at_point_[i];
    p.orient_with_object_ = params->has_bare_flag(0xA2487CC3u);
    p.flag158_ = params->has_bare_flag(0xA0E585B0u);
    effect_handle_ = g_particle_hub->spawn_particle(&p);

    // The model it follows (0x5E2A8D9F: an object's; no such object - a crash, as the original).
    uint32_t model = 0;
    if (params->read_hash_value(0x5E2A8D9Fu, &model, 0) && model)
        mesh_instance_ = static_cast<GameEntity*>(find_object(model))->fetch_module(0x286A8D26);
    if (p.emit_script_) {
        script::Struct* script_params = nullptr;
        params->read_sub_struct(0x7031F10Cu, &script_params, 0);
        entity_->start_script_task(p.emit_script_, 0, false, false, script_params);
    }
    uint32_t value54 = 0xE3714DC1;
    params->read_hash_value(0x7E4813A7u, &value54, 0);
    effect_handle_->args_.unknown50_ = value54;
    if (p.flag158_)
        effect_handle_->change_colour(reinterpret_cast<const uint8_t*>(static_cast<void*>(nullptr)));
}

// 0x0042AD90
void EffectsEmitterModule::tick_frame()
{
    // (suspended: nothing - unless a network game runs and it has a lifetime)
    if (suspend_ && static_cast<DormancyModule*>(suspend_)->is_far()) {
        void* net = static_cast<void*>(g_online_hub);
        if (!static_cast<online::Hub*>(net)->in_net_game() || lifespan_ == 0)
            return;
    }
    if (lifespan_ != 0) {
        uint32_t age = static_cast<uint32_t>(frame_clock::game_clock_elapsed() + (g_clock_offset - start_time_));
        if (age > lifespan_) {
            entity_->flag_as_dead();
            return;
        }
    }
    if (mesh_instance_)
        effect_handle_->set_brightness(static_cast<MeshInstanceModule*>(mesh_instance_)->brightness_);
    const render::SparkleConfig& args = effect_handle_->args_;
    if (!args.local_positions_)
        return;
    for (int i = 0; i < 3; ++i) {
        const float* local = &args.offset_pos_[i].x;
        const maths::Vec4& at = entity_->at_point_;
        maths::Vec4 pos = {local[0] + at.x, local[1] + at.y, local[2] + at.z, local[3] + at.w};
        effect_handle_->change_place(i, &pos);
        if (args.orient_with_object_) {
            maths::Mat4 orientation;
            orientation = entity_->basis_frame_;
            orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
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
        const float* at = &effect_handle_->args_.at_point_[0].x;
        feeler.line_.start = {at[0], static_cast<float>(at[1] + 1.0f), at[2], at[3]};
        feeler.line_.end = {at[0], static_cast<float>(static_cast<double>(at[1]) - 4800.0), at[2], at[3]};
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
    script::Struct* result = script::spawn_param_bag();
    if (result)
        result = result->prepare_all();
    // (the name, two floats and the colours read straight from the system's +4; the rest through the getter, once
    // for each value)
    const ParticleParams* own = &effect_handle_->args_;
    result->put_name_key(0xA1DC81F9u, own->label_);
    result->push_float(0x35B65BD4u, own->rate_);
    result->push_float(0xC218CF77u, own->lifetime_);
    script::Array* colours[3];
    for (script::Array*& array : colours) {
        array = script::alloc_script_array();
        if (array)
            array = array->prepare_all();
    }
    uint8_t colour[3][4];
    std::memcpy(colour, own->shade_, sizeof(colour));
    for (script::Array* array : colours)
        array->configure_size_kind(4, script::kQbvalInteger);
    for (int i = 0; i < 3; ++i)
        for (uint32_t j = 0; j < 4; ++j)
            colours[i]->set_element(j, colour[i][j]);
    result->append_array_value(0x7E6BCBE6u, colours[0]);
    result->append_array_value(0x2A6B6FE2u, colours[1]);
    result->append_array_value(0x0481113Du, colours[2]);
    auto get = [this] { return &effect_handle_->args_; };
    auto add_vector = [&](uint32_t name, const maths::Vec4& v) {
        result->put_vec3(name, script::VectorValue{v.x, v.y, v.z, v.w});
    };
    add_vector(0x7B5A25F8u, get()->at_point_[0]);
    add_vector(0xA5A9080Eu, get()->at_point_[1]);
    add_vector(0xFBDA5673u, get()->at_point_[2]);
    result->push_float(0x3BEE67C8u, get()->extent_[0]);
    result->push_float(0x8E39913Du, get()->extent_[1]);
    result->push_float(0x3F243A3Cu, get()->extent_[2]);
    add_vector(0x9656F775u, get()->box_[0]);
    add_vector(0xC3CD20A2u, get()->box_[1]);
    add_vector(0x829FE7DDu, get()->box_[2]);
    script->args_->put_sub_struct(0xB91EE381u, result);
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

using objects::EffectsEmitterModule;
