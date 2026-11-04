// The particle component (GameObjects::EffectsEmitterModule, particle_component.h): its life and its script member functions.
// The Nx particle systems themselves have no source yet: their methods are called as the game's.

#include "engine/objects/particle_component.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include "engine/gfx/particle_sets.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/net/session_query.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "engine/objects/model_component.h"
#include <gsl/pointers>
#include <array>
#include <utility>
#include <gsl/assert>
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/core/checksums.h"

namespace objects {
namespace {
constexpr int kMaxShadeAlpha = 0x80;
constexpr double kGroundProbeDepth = 4800.0;
constexpr double kVertexValueFactor = 2.0;
constexpr uint32_t kSetStartPos = 0x88BE2262; // SetStartPos
constexpr uint32_t kParticle = 0xA9DB601E; // Particle
constexpr uint32_t kSetMidPos = 0xBB6018F4; // SetMidPos
constexpr uint32_t kSetEndPos = 0xE5134689; // SetEndPos
constexpr uint32_t kSetStartBoxDimensions = 0xF71D696E; // SetStartBoxDimensions
constexpr uint32_t kSetMidBoxDimensions = 0x0AAB006D; // SetMidBoxDimensions
constexpr uint32_t kSetEndBoxDimensions = 0xE2B99038; // SetEndBoxDimensions
constexpr uint32_t kSetUpdateScript = 0xC036F4D0; // SetUpdateScript
constexpr uint32_t kSetEmitRate = 0xC6525C4E; // SetEmitRate
constexpr uint32_t kSetLifetime = 0x31FCC8ED; // SetLifetime
constexpr uint32_t kSetMidpointColorPct = 0x19AF74D1; // SetMidpointColorPct
constexpr uint32_t kSetMidPointPct = 0x4AA40FB5; // SetMidPointPct
constexpr uint32_t kSetStartRadius = 0x234CEABB; // SetStartRadius
constexpr uint32_t kSetMidRadius = 0x9E100F60; // SetMidRadius
constexpr uint32_t kSetEndRadius = 0x2F0DA461; // SetEndRadius
constexpr uint32_t kSetStartColor = 0xEEFFAE18; // SetStartColor
constexpr uint32_t kSetMidColor = 0x0FE869E8; // setMidColor
constexpr uint32_t kSetEndColor = 0x19AA4CD3; // SetEndColor
constexpr uint32_t kSetCullOutside = 0x5446AF14; // text unknown (the integer that sets the particle system cull-outside flag)
constexpr uint32_t kModulateColor = 0x55835CF2; // modulatecolor (letter case unknown)
constexpr uint32_t kGetParticleParams = 0x3D05AFE0; // GetParticleParams
constexpr uint32_t kUpdateScript = 0x092276DBU;     // UpdateScript
constexpr uint32_t kSystemLifetime = 0x5A235299U;   // SystemLifetime
constexpr uint32_t kName = 0xA1DC81F9U;             // Name
constexpr uint32_t kTypeName = 0x7321A8D6U;         // Type
constexpr uint32_t kUseMidPoint = 0xA7A5BFFBU;      // UseMidPoint
constexpr uint32_t kCullOutsideFlag = 0x5446AF14U;  // text unknown (the bare flag that turns culling outside on)
constexpr uint32_t kScale = 0x13B9DA7BU;            // Scale
constexpr uint32_t kStartRadius = 0x3BEE67C8U;      // StartRadius
constexpr uint32_t kMidRadius = 0x8E39913DU;        // MidRadius
constexpr uint32_t kEndRadius = 0x3F243A3CU;        // EndRadius
constexpr uint32_t kOrientToVel = 0x7045843AU;      // orient_to_vel
constexpr uint32_t kVel = 0x0C4C809EU;              // Vel
constexpr uint32_t kStartRadiusSpread = 0x05400A25U;        // StartRadiusSpread
constexpr uint32_t kMidRadiusSpread = 0xFF7D9AA9U;          // MidRadiusSpread
constexpr uint32_t kEndRadiusSpread = 0x5539FCEEU;          // EndRadiusSpread
constexpr uint32_t kMaxStreams = 0x9E52A20FU;               // MaxStreams
constexpr uint32_t kEmitRate = 0x35B65BD4U;                 // EmitRate
constexpr uint32_t kLifeTime = 0xC218CF77U;                 // LifeTime
constexpr uint32_t kMidPointPCT = 0x520682C6U;              // MidPointPCT
constexpr uint32_t kUseStartPosition = 0x64F194EDU;         // UseStartPosition
constexpr uint32_t kStartPosition = 0x3D5EA6A4U;            // StartPosition
constexpr uint32_t kPos = 0x7F261953U;                      // Pos
constexpr uint32_t kMidPosition = 0x004B51A2U;              // MidPosition
constexpr uint32_t kEndPosition = 0x5854AB9EU;              // EndPosition
constexpr uint32_t kBoxDimsStart = 0x9656F775U;             // BoxDimsStart
constexpr uint32_t kBoxDimsMid = 0xC3CD20A2U;               // BoxDimsMid
constexpr uint32_t kBoxDimsEnd = 0x829FE7DDU;               // BoxDimsEnd
constexpr uint32_t kUseColorMidTime = 0x454962EFU;          // UseColorMidTime
constexpr uint32_t kColorMidTime = 0x59463C93U;             // ColorMidTime
constexpr uint32_t kBlendMode = 0xDD6BB3D5U;                // BlendMode
constexpr uint32_t kAlphaCutoff = 0x9F677D4EU;              // AlphaCutoff
constexpr uint32_t kFixedAlpha = 0x13D0BE1DU;               // FixedAlpha
constexpr uint32_t kTexture = 0x7D99F28DU;                  // Texture
constexpr uint32_t kLODDist1 = 0x4EEDFAE7U;                 // LOD_Dist1
constexpr uint32_t kLODDist2 = 0xD7E4AB5DU;                 // LOD_Dist2
constexpr uint32_t kSuspendDistance = 0xE4279BA2U;          // SuspendDistance
constexpr uint32_t kPriority = 0x9D5923D8U;                 // Priority
constexpr uint32_t kAngles = 0x9D2D0915U;                   // Angles
constexpr uint32_t kLocalSpace = 0xB7B7240DU;               // LocalSpace
constexpr uint32_t kEnableRotate = 0xA2487CC3U;             // EnableRotate
constexpr uint32_t kUseGlobalColorModulation = 0xA0E585B0U; // UseGlobalColorModulation
constexpr uint32_t kUpdateBrightnessFrom = 0x5E2A8D9FU;     // Update_Brightness_From
constexpr uint32_t kModel = 0x286A8D26; // Model
constexpr uint32_t kParams = 0x7031F10CU; // Params
constexpr uint32_t kOutdoor = 0xE3714DC1; // outdoor
constexpr uint32_t kLightGroup = 0x7E4813A7U;     // LightGroup
constexpr uint32_t kStartRGBA = 0x7E6BCBE6U;      // StartRGBA
constexpr uint32_t kMidRGBA = 0x2A6B6FE2U;        // MidRGBA
constexpr uint32_t kEndRGBA = 0x0481113DU;        // EndRGBA
constexpr uint32_t kStartPos = 0x7B5A25F8U;       // StartPos
constexpr uint32_t kMidPos = 0xA5A9080EU;         // MidPos
constexpr uint32_t kEndPos = 0xFBDA5673U;         // EndPos
constexpr uint32_t kParticleParams = 0xB91EE381U; // ParticleParams
} // namespace

std::array<script::MemberFunctionEntry, kParticleComponentMemberFunctionCount>& particle_component_member_functions()
{
    static std::array<script::MemberFunctionEntry, kParticleComponentMemberFunctionCount> particle_component_member_functions{{
        {.callsign_ = kSetStartPos, .owner_kind_ = kParticle}, {.callsign_ = kSetMidPos, .owner_kind_ = kParticle}, {.callsign_ = kSetEndPos, .owner_kind_ = kParticle}, {.callsign_ = kSetStartBoxDimensions, .owner_kind_ = kParticle},
        {.callsign_ = kSetMidBoxDimensions, .owner_kind_ = kParticle}, {.callsign_ = kSetEndBoxDimensions, .owner_kind_ = kParticle}, {.callsign_ = kSetUpdateScript, .owner_kind_ = kParticle}, {.callsign_ = kSetEmitRate, .owner_kind_ = kParticle},
        {.callsign_ = kSetLifetime, .owner_kind_ = kParticle}, {.callsign_ = kSetMidpointColorPct, .owner_kind_ = kParticle}, {.callsign_ = kSetMidPointPct, .owner_kind_ = kParticle}, {.callsign_ = kSetStartRadius, .owner_kind_ = kParticle},
        {.callsign_ = kSetMidRadius, .owner_kind_ = kParticle}, {.callsign_ = kSetEndRadius, .owner_kind_ = kParticle}, {.callsign_ = kSetStartColor, .owner_kind_ = kParticle}, {.callsign_ = kSetMidColor, .owner_kind_ = kParticle},
        {.callsign_ = kSetEndColor, .owner_kind_ = kParticle}, {.callsign_ = kSetCullOutside, .owner_kind_ = kParticle}, {.callsign_ = kModulateColor, .owner_kind_ = kParticle}, {.callsign_ = kGetParticleParams, .owner_kind_ = kParticle},
        {.callsign_ = 0, .owner_kind_ = kParticle}}};
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
    float value = 0.0F;
    std::memcpy(&value, static_cast<const void*>(&params), 4);
    params->fetch_float(0U, &value, 1);
    return value;
}

// The parameters a particle system is made from (render::SparkleConfig, gfx/particles.h; constructed by 0x00496240,
// given to the particle manager's 0x00496510, kept as the system's +4).
using ParticleParams = render::SparkleConfig;


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
    for (uint32_t i = 0; i < 4; ++i) {
        core::at(colour, i) = static_cast<uint8_t>(array->fetch_part(i));
    }
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
EffectsEmitterModule::EffectsEmitterModule() : start_time_(frame_clock::game_clock_elapsed() + clock_offset())
{
    set_kind_tag(kType);
}

// 0x00429B90 (0x0042A060 the deleting one)
EffectsEmitterModule::~EffectsEmitterModule()
{
    if (effect_handle_) {
        stored_particle_hub()->retire_particle(effect_handle_);
    }
}

// 0x00429BF0
void EffectsEmitterModule::seal() { suspend_ = entity()->fetch_module(kSuspend); }

// 0x00429960
void EffectsEmitterModule::conceal(bool hide) { effect_handle_->conceal(hide); }

// --- script member functions ---

// 0x00429970
bool EffectsEmitterModule::set_emit_rate(script::Struct* params, script::Script* /*script*/)
{
    const float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->set_rate(value);
    }
    return true;
}

// 0x004299A0
bool EffectsEmitterModule::set_lifespan(script::Struct* params, script::Script* /*script*/)
{
    const float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->set_lifespan(value);
        get_refresh(effect_handle_);
    }
    return true;
}

// 0x004299E0
bool EffectsEmitterModule::set_float_19a_f_74d_1(script::Struct* params, script::Script* /*script*/)
{
    const float value = unnamed_float(params);
    if (effect_handle_) {
        effect_handle_->set_colour_mid_percent(value);
        get_refresh(effect_handle_);
    }
    return true;
}

// 0x00429A20
bool EffectsEmitterModule::change_midpoint_fraction(script::Struct* params, script::Script* /*script*/)
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
    params->fetch_vec(0U, &pos, 1);
    if (component->effect_handle()) {
        component->effect_handle()->change_place(which, &pos);
        get_refresh(component->effect_handle());
    }
    return true;
}
bool set_box(EffectsEmitterModule* component, script::Struct* params, int which)
{
    maths::Vec4 box{};
    params->fetch_vec(0U, &box, 1);
    if (component->effect_handle()) {
        component->effect_handle()->set_box(which, &box);
    }
    return true;
}
bool change_colour(EffectsEmitterModule* component, script::Struct* params, int which)
{
    script::Array* array = nullptr;
    params->fetch_list(0U, &array, 1);
    if (component->effect_handle()) {
        const render::Rgba colour = colour_of(array);
        component->effect_handle()->change_colour(which, colour);
    }
    return true;
}
} // namespace

bool EffectsEmitterModule::set_start_radius(script::Struct* params, script::Script* /*script*/)
{
    return set_reach(this, params, 0);
} // 0x00429A60
bool EffectsEmitterModule::set_mid_radius(script::Struct* params, script::Script* /*script*/)
{
    return set_reach(this, params, 1);
} // 0x00429AA0
bool EffectsEmitterModule::set_end_radius(script::Struct* params, script::Script* /*script*/)
{
    return set_reach(this, params, 2);
} // 0x00429AE0
bool EffectsEmitterModule::set_start_pos(script::Struct* params, script::Script* /*script*/)
{
    return change_location(this, params, 0);
} // 0x00429C10
bool EffectsEmitterModule::set_mid_pos(script::Struct* params, script::Script* /*script*/)
{
    return change_location(this, params, 1);
} // 0x00429C50
bool EffectsEmitterModule::set_end_pos(script::Struct* params, script::Script* /*script*/)
{
    return change_location(this, params, 2);
} // 0x00429C90
bool EffectsEmitterModule::set_start_box(script::Struct* params, script::Script* /*script*/)
{
    return set_box(this, params, 0);
} // 0x00429CD0
bool EffectsEmitterModule::set_mid_box(script::Struct* params, script::Script* /*script*/)
{
    return set_box(this, params, 1);
} // 0x00429D10
bool EffectsEmitterModule::set_end_box(script::Struct* params, script::Script* /*script*/)
{
    return set_box(this, params, 2);
} // 0x00429D50
bool EffectsEmitterModule::set_start_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(this, params, 0);
} // 0x00429DD0
bool EffectsEmitterModule::set_mid_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(this, params, 1);
} // 0x00429E50
bool EffectsEmitterModule::set_end_color(script::Struct* params, script::Script* /*script*/)
{
    return change_colour(this, params, 2);
} // 0x00429ED0

// 0x00429D90
bool EffectsEmitterModule::set_emit_target(script::Struct* params, script::Script* /*script*/)
{
    params->read_hash_value(0U, &emit_target_, 1);
    if (effect_handle_) {
        effect_handle_->set_emit_script(checksums::Name{emit_target_}, entity()->ident());
    }
    return true;
}

// 0x00429F50
bool EffectsEmitterModule::set_flag_5446a_f14(script::Struct* params, script::Script* /*script*/)
{
    int value = 0;
    params->fetch_int_value(0U, &value, 0);
    if (effect_handle_) {
        effect_handle_->args().set_cull_outside(value == 1);
    }
    return true;
}

// 0x00429F90
bool EffectsEmitterModule::set_color_55835c_f2(script::Struct* params, script::Script* /*script*/)
{
    script::Array* array = nullptr;
    params->fetch_list(0U, &array, 1);
    if (effect_handle_) {
        const render::Rgba colour = colour_of(array);
        effect_handle_->change_colour(&colour);
    }
    return true;
}

// 0x0042A090
void EffectsEmitterModule::configure_from_data(script::Struct* params)
{
    ParticleParams particle_params;
    float scale = 1.0F;
    particle_params.set_emit_script(0);
    if (params->read_hash_value(kUpdateScript, &emit_target_, 0)) {
        particle_params.set_emit_script(emit_target_);
        particle_params.set_entity_key(entity()->ident());
    }
    params->fetch_int_value(kSystemLifetime, &lifespan_, 0);
    params->read_hash_value(kName, &particle_params.label(), 1);
    params->read_hash_value("GroupID", &particle_params.group_id(), 0);
    params->read_hash_value(kTypeName, &particle_params.unknown04(), 0);
    particle_params.set_use_mid(params->has_bare_flag(kUseMidPoint));
    particle_params.set_cull_outside(params->has_bare_flag(kCullOutsideFlag));
    params->fetch_float(kScale, &scale, 0);
    params->fetch_float(kStartRadius, &core::at(particle_params.extent(), 0), 0);
    params->fetch_float(kMidRadius, &core::at(particle_params.extent(), 1), 0);
    params->fetch_float(kEndRadius, &core::at(particle_params.extent(), 2), 0);

    // The orientation: the identity, or turned to face 0x0C4C809E (normalised).
    maths::Mat4 orient{};
    core::at(orient.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(orient.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(orient.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(orient.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    if (params->has_bare_flag(kOrientToVel)) {
        maths::Vec4 at{};
        params->fetch_vec(kVel, &at, 1);
        at.make_unit();
        core::at(orient.row, 2) = at;
        const maths::Vec4 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
        core::at(orient.row, 0).w = 0.0F;
        if (core::at(orient.row, 2) == up) {
            // (straight up: across from (0, 0, -1))
            auto scaled = static_cast<float>(static_cast<double>(at.z) * 0.0);
            core::at(orient.row, 0).x = static_cast<float>((static_cast<double>(at.y) * -1.0) - scaled);
            core::at(orient.row, 0).y = static_cast<float>(scaled - (static_cast<double>(at.x) * -1.0));
            core::at(orient.row, 0).z =
                static_cast<float>((static_cast<double>(at.x) * 0.0) - (static_cast<double>(at.y) * 0.0));
        } else {
            // (across from (0, 1, 0))
            const double y0 = static_cast<double>(at.y) * 0.0;
            core::at(orient.row, 0).x = static_cast<float>(y0 - at.z);
            core::at(orient.row, 0).y =
                static_cast<float>((static_cast<double>(at.z) * 0.0) - (static_cast<double>(at.x) * 0.0));
            core::at(orient.row, 0).z = static_cast<float>(at.x - y0);
        }
        const maths::Vec4& right = core::at(orient.row, 0);
        core::at(orient.row, 1).w = 0.0F;
        core::at(orient.row, 1).x =
            static_cast<float>((static_cast<double>(at.y) * right.z) - (static_cast<double>(at.z) * right.y));
        core::at(orient.row, 1).y =
            static_cast<float>((static_cast<double>(at.z) * right.x) - (static_cast<double>(at.x) * right.z));
        core::at(orient.row, 1).z =
            static_cast<float>((static_cast<double>(at.x) * right.y) - (static_cast<double>(at.y) * right.x));
    }
    for (float& size : particle_params.extent()) {
        size = static_cast<float>(static_cast<double>(size) * scale);
    }
    params->fetch_float(kStartRadiusSpread, &core::at(particle_params.radius(), 0), 0);
    params->fetch_float(kMidRadiusSpread, &core::at(particle_params.radius(), 1), 0);
    params->fetch_float(kEndRadiusSpread, &core::at(particle_params.radius(), 2), 0);
    for (float& radius : particle_params.radius()) {
        radius = static_cast<float>(static_cast<double>(radius) * scale);
    }
    params->fetch_int_value(kMaxStreams, &particle_params.unknown24(), 0);
    params->fetch_float(kEmitRate, &particle_params.rate(), 0);
    params->fetch_float(kLifeTime, &particle_params.lifetime(), 0);
    params->fetch_float(kMidPointPCT, &particle_params.mid_percent(), 0);

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
    vector(params->has_bare_flag(kUseStartPosition) ? kStartPosition : kPos, &core::at(particle_params.at_point(), 0));
    vector(kMidPosition, &core::at(particle_params.at_point(), 1));
    vector(kEndPosition, &core::at(particle_params.at_point(), 2));
    scale_and_turn(particle_params.at_point());
    vector(kBoxDimsStart, &core::at(particle_params.box(), 0));
    vector(kBoxDimsMid, &core::at(particle_params.box(), 1));
    vector(kBoxDimsEnd, &core::at(particle_params.box(), 2));
    scale_and_turn(particle_params.box());
    particle_params.set_colour_use_mid(params->has_bare_flag(kUseColorMidTime));
    params->fetch_float(kColorMidTime, &particle_params.mid_percent(), 0);

    // The colours (rgb from three arrays; the alphas at most 128).
    uint32_t value = 0;
    script::Array* array = nullptr;
    static const std::array<uint32_t, 3> kColours = {{0xDAB75B6CU, 0x0444769AU, 0x5A3728E7U}};
    for (int i = 0; i < 3; ++i) {
        if (params->fetch_list(core::at(kColours, i), &array, 0)) {
            get_rgb(array, core::at(particle_params.shade(), i), &value);
        }
    }
    static const std::array<uint32_t, 3> kAlphas = {{0x4D83DB4CU, 0x4ABA1FF1U, 0x5CF83ACAU}};
    for (int i = 0; i < 3; ++i) {
        if (params->fetch_int_value(core::at(kAlphas, i), &value, 0)) {
            auto alpha = static_cast<uint8_t>(value);
            core::at(core::at(particle_params.shade(), i), 3) = alpha > kMaxShadeAlpha ? kMaxShadeAlpha : alpha;
        }
    }
    params->read_hash_value(kBlendMode, &particle_params.blend_mode(), 0);
    params->fetch_int_value(kAlphaCutoff, &particle_params.unknown13c(), 0);
    params->fetch_int_value(kFixedAlpha, &particle_params.alpha_cut_off(), 0);
    params->read_hash_value(kTexture, &particle_params.texture(), 0);
    params->fetch_int_value(kLODDist1, &particle_params.unknown144(), 0);
    params->fetch_int_value(kLODDist2, &particle_params.unknown148(), 0);
    params->fetch_int_value(kSuspendDistance, &particle_params.unknown14c(), 0);
    int byte_value = 0;
    params->fetch_int_value(kPriority, &byte_value, 0);
    particle_params.set_unknown150(static_cast<uint8_t>(byte_value));

    // The rotation: X, Y, then Z.
    core::at(particle_params.orientation_turn().row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(particle_params.orientation_turn().row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(particle_params.orientation_turn().row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(particle_params.orientation_turn().row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    maths::Vec4 angles = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    params->fetch_vec(kAngles, &angles, 0);
    maths::Mat4 rotation{};
    maths::matrix_multiply(&particle_params.orientation_turn(), &particle_params.orientation_turn(),
                           &maths::make_pitch_matrix(rotation, angles.x));
    maths::matrix_multiply(&particle_params.orientation_turn(), &particle_params.orientation_turn(),
                           &maths::make_yaw_matrix(rotation, angles.y));
    maths::matrix_multiply(&particle_params.orientation_turn(), &particle_params.orientation_turn(),
                           &maths::make_roll_matrix(rotation, angles.z));

    particle_params.set_local_positions(params->has_bare_flag(kLocalSpace));
    if (particle_params.local_positions()) {
        for (int i = 0; i < 3; ++i) {
            core::at(particle_params.offset_pos(), i) = core::at(particle_params.at_point(), i);
        }
    }
    particle_params.set_orient_with_object(params->has_bare_flag(kEnableRotate));
    particle_params.set_flag158(params->has_bare_flag(kUseGlobalColorModulation));
    effect_handle_ = stored_particle_hub()->spawn_particle(&particle_params);

    // The model it follows (0x5E2A8D9F: an object's; no such object - a crash, as the original).
    uint32_t model = 0;
    if (params->read_hash_value(kUpdateBrightnessFrom, &model, 0) && (model != 0U)) {
        mesh_instance_ = core::down_cast<GameEntity>(find_object(model))->fetch_module(kModel);
    }
    if (particle_params.emit_script() != 0U) {
        script::Struct* script_params = nullptr;
        params->read_sub_struct(kParams, &script_params, 0);
        entity()->start_script_task(particle_params.emit_script(), 0, false, false, script_params);
    }
    uint32_t value54 = kOutdoor;
    params->read_hash_value(kLightGroup, &value54, 0);
    effect_handle_->args().set_unknown50(value54);
    if (particle_params.flag158()) {
        effect_handle_->change_colour(nullptr);
    }
}

// 0x0042AD90
void EffectsEmitterModule::tick_frame()
{
    // (suspended: nothing - unless a network game runs and it has a lifetime)
    if (suspend_ && core::down_cast<DormancyModule>(suspend_)->is_far()) {
        if (!net::in_net_game() || lifespan_ == 0) {
            return;
        }
    }
    if (lifespan_ != 0) {
        auto age = static_cast<uint32_t>(frame_clock::game_clock_elapsed() + (clock_offset() - start_time_));
        if (age > lifespan_) {
            entity()->flag_as_dead();
            return;
        }
    }
    if (mesh_instance_) {
        effect_handle_->set_brightness(core::down_cast<MeshInstanceModule>(mesh_instance_)->brightness());
    }
    const render::SparkleConfig& args = effect_handle_->args();
    if (!args.local_positions()) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        const maths::Vec4& local = core::at(args.offset_pos(), i);
        const maths::Vec4& at = entity()->at_point();
        const maths::Vec4 pos = {.x = local.x + at.x, .y = local.y + at.y, .z = local.z + at.z, .w = local.w + at.w};
        effect_handle_->change_place(i, &pos);
        if (args.orient_with_object()) {
            maths::Mat4 orientation{};
            orientation = entity()->basis_frame();
            core::at(orientation.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            effect_handle_->spin_by(i, &orientation);
        }
    }
}

// 0x0042AEE0
int EffectsEmitterModule::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    if (name == kEmitting) {
        return effect_handle_->is_emitting() ? 1 : 0;
    }
    if (name == kGroundTest) {
        // A line from just above the particle system's position to 4800 below it: the face hit's vertex value,
        // doubled (1 without a hit).
        float value = 1.0F;
        render::ProbeRay feeler;
        const maths::Vec4& at = core::at(effect_handle_->args().at_point(), 0);
        feeler.line().start = {.x = at.x, .y = static_cast<float>(at.y + 1.0F), .z = at.z, .w = at.w};
        feeler.line().end = {.x = at.x, .y = static_cast<float>(static_cast<double>(at.y) - kGroundProbeDepth), .z = at.z, .w = at.w};
        if (feeler.get_hit_shape(true, false) != 0) {
            value = static_cast<float>(feeler.get_hit_vertex_value() * kVertexValueFactor);
        }
        if (effect_handle_) {
            effect_handle_->set_brightness(value);
        }
        return 1;
    }
    return 2;
}

// 0x0042B040
bool EffectsEmitterModule::get_particle_params(script::Struct* /*params*/, script::Script* script)
{
    gsl::owner<script::Struct*> result = script::spawn_param_bag();
    if (result) {
        result = new (result) script::Struct;
    }
    // (the name, two floats and the colours read straight from the system's +4; the rest through the getter, once
    // for each value)
    const ParticleParams* own = &effect_handle_->args();
    Expects(result != nullptr);
    result->put_name_key(checksums::Name{kName}, own->label());
    result->push_float(checksums::Name{kEmitRate}, own->rate());
    result->push_float(checksums::Name{kLifeTime}, own->lifetime());
    std::array<script::Array*, 3> colours{};
    for (script::Array*& array : colours) {
        gsl::owner<script::Array*> made = script::new_array();
        array = made;
    }
    std::array<std::array<uint8_t, 4>, 3> colour{};
    std::memcpy(colour.data(), own->shade().data(), sizeof(colour));
    for (script::Array* array : colours) {
        array->configure_size_kind(4, script::QbValueKind::kInteger);
    }
    for (int i = 0; i < 3; ++i) {
        for (uint32_t j = 0; j < 4; ++j) {
            core::at(colours, i)->set_element(j, core::at(core::at(colour, i), j));
        }
    }
    result->append_array_value(kStartRGBA, core::at(colours, 0));
    result->append_array_value(kMidRGBA, core::at(colours, 1));
    result->append_array_value(kEndRGBA, core::at(colours, 2));
    auto get = [this] { return &effect_handle_->args(); };
    auto add_vector = [&](uint32_t name, const maths::Vec4& v) {
        result->put_vec3(checksums::Name{name}, script::VectorValue{.x = v.x, .y = v.y, .z = v.z, .w = v.w});
    };
    add_vector(kStartPos, core::at(get()->at_point(), 0));
    add_vector(kMidPos, core::at(get()->at_point(), 1));
    add_vector(kEndPos, core::at(get()->at_point(), 2));
    result->push_float(checksums::Name{kStartRadius}, core::at(get()->extent(), 0));
    result->push_float(checksums::Name{kMidRadius}, core::at(get()->extent(), 1));
    result->push_float(checksums::Name{kEndRadius}, core::at(get()->extent(), 2));
    add_vector(kBoxDimsStart, core::at(get()->box(), 0));
    add_vector(kBoxDimsMid, core::at(get()->box(), 1));
    add_vector(kBoxDimsEnd, core::at(get()->box(), 2));
    script->args()->put_sub_struct(kParticleParams, result);
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

