// The engine's flexible particle systems: emission, the member functions scripts reach through the flexible particle
// component, and the update (see particle_system.h). From the disassembly of 0x00496FE0-0x0049778E,
// 0x00497B00-0x0049868F, 0x004987F0-0x00498F7D and 0x00499EE0-0x0049A080. The x87 code is followed at 53 bits: what
// it keeps on its stack is a double here, what it stores to memory a float.

#include "engine/gfx/particle_system.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/packed_colour.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/viewport.h"
#include "engine/maths/mat4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/view_setup.h"
#include "engine/maths/vec4.h"
#include "engine/maths/trig.h"

namespace render {

constexpr uint32_t kFloatOneBits = 0x3F800000; // 1.0f
constexpr int kSystemFlagCircular = 8;         // the emitter emits round a circle
constexpr double kGroundProbeDepth = 4800.0;   // how far below a particle the ground is looked for
constexpr double kByteMax = 255.0;

namespace {

constexpr float kRandomUnit = std::bit_cast<float>(0x38000100u); // 0x00646510: 1 / 32767
constexpr float kDegrees = std::bit_cast<float>(0x3C8EFA35u);    // 0x00645464: pi / 180
constexpr float kFarAway = 1024.0f;                              // where a removed particle's positions go

int get_rand() { return crt::get_rand(); } // (the game's C runtime's)
// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
// A random number in [-1, 1].
float random_signed()
{
    const double v = static_cast<double>(get_rand()) * kRandomUnit;
    return static_cast<float>(v + v - 1.0);
}
// min + (max - min) times a random fraction.
double random_between(float min, float max)
{
    const double t = static_cast<double>(get_rand()) * kRandomUnit;
    return t * (static_cast<double>(max) - min) + min;
}

void* viewport_camera(int viewport) { return fetch_live_camera(viewport); }
// 0x0048C490: whether a sphere is out of the camera's view.
int sphere_hidden(const maths::Vec4* centre, float radius)
{
    return engine::sphere_visible_in_one_viewport(centre, radius);
}

std::span<float, 3> position_of(EmitterInstance* system, int particle, int step)
{
    return place_at(system->position_array(), system->max_particles(), step, particle);
}

// The member functions' names and parameters.
constexpr uint32_t kSetEmitAngle = 0x1CFBF078;
constexpr uint32_t kSetPos = 0x099CD17B;
constexpr uint32_t kSetParticleSize = 0x2A479569;
constexpr uint32_t kSetParticleSizeScale = 0x2E66815E;
constexpr uint32_t kEmitRate = 0x35B65BD4;
constexpr uint32_t kSetColor = 0x514B2584;
constexpr uint32_t kEmpty = 0x482482C6; // (its name unknown)
constexpr uint32_t kSpeedSpanCmd = 0x63274395;
constexpr uint32_t kEmit = 0x711B1A6A;
constexpr uint32_t kSetAngleSpread = 0x7911855B;
constexpr uint32_t kApplyEnvBrightness = 0x96896315;
constexpr uint32_t kSetEmitRange = 0x8FFDEF38;
constexpr uint32_t kSetForce = 0xA075EE45;
constexpr uint32_t kSetEmitTarget = 0xB373FB6E;
constexpr uint32_t kSetLife = 0xD3F1D333;
constexpr uint32_t kSetEmitterIndependent = 0xE3309AB5;
constexpr uint32_t kRandomSpread = 0xDB8621F7; // (its name unknown)
constexpr uint32_t kSetCircularEmit = 0xE985A9A6;
constexpr uint32_t kSetLodParams = 0xFBB6720D;
constexpr uint32_t kX = 0x7323E97C;
constexpr uint32_t kY = 0x0424D9EA;
constexpr uint32_t kZ = 0x9D2D8850;
constexpr uint32_t kAngle = 0xFF7EBAF6;
constexpr uint32_t kPos = 0x7F261953;
constexpr uint32_t kSw = 0x745523AA;
constexpr uint32_t kSh = 0xF95D2E5F;
constexpr uint32_t kEw = 0x68CD967D;
constexpr uint32_t kEh = 0xE5C59B88;
constexpr uint32_t kScale = 0x5B1D3822; // (SetParticleSizeScale's)
constexpr uint32_t kRate = 0x2013C0C6;
constexpr uint32_t kCorner = 0xFEC1C2FB;
constexpr uint32_t kMidTime = 0x856BC5E2;
constexpr uint32_t kMin = 0x5E84E22F;
constexpr uint32_t kMax = 0x6289DD76;
constexpr uint32_t kNum = 0x23BC5091;
constexpr uint32_t kSpread = 0x834A5F87;
constexpr uint32_t kEveryUpdate = 0x652F5D69; // (ApplyEnvBrightness's flag)
constexpr uint32_t kWidth = 0x73E5BAD0;
constexpr uint32_t kHeight = 0x0AB21AF0;
constexpr uint32_t kDepth = 0x055CE396;
constexpr uint32_t kForce = 0x68977CD7;
constexpr uint32_t kTarget = 0xB990D003;
constexpr uint32_t kRandom = 0xE9C4252A;
constexpr uint32_t kCircular = 0x9D2E07B3;
constexpr uint32_t kMinParticles = 0x6C4E18EB;
constexpr uint32_t kLodDist1 = 0x4EEDFAE7;
constexpr uint32_t kLodDist2 = 0xD7E4AB5D;

// The colour channels setColor reads: start, mid and end; the alphas halved.
const std::array<uint32_t, 4> kEndChannels = {{0x18A762F2, 0x757A8619, 0x05107296, 0x9C19232C}};   // er, eg, eb, ea
const std::array<uint32_t, 4> kMidChannels = {{0xD07EE8FA, 0xBDA30C11, 0xCDC9F89E, 0x54C0A924}};   // mr, mg, mb, ma
const std::array<uint32_t, 4> kStartChannels = {{0x043FD725, 0x69E233CE, 0x1988C741, 0x808196FB}}; // sr, sg, sb, sa

// A vector parameter, or x, y and z ones.
maths::Vec4 get_vector_or_xyz(script::Struct* params, uint32_t name)
{
    script::VectorValue v{};
    if (params->fetch_vec(name, &v, 0))
        return {.x = v.x, .y = v.y, .z = v.z, .w = 0.0f};
    maths::Vec4 xyz{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    params->fetch_float(kX, &xyz.x, 0);
    params->fetch_float(kY, &xyz.y, 0);
    params->fetch_float(kZ, &xyz.z, 0);
    return xyz;
}

// Sizes from the system's, scaled (unless the scale is exactly 1).
void set_sizes(EmitterInstance* system, FlexibleParticle& p)
{
    core::at(p.start_size_, 0) = core::at(system->extent(), 0);
    core::at(p.start_size_, 1) = core::at(system->extent(), 1);
    core::at(p.end_size_, 0) = core::at(system->extent(), 2);
    core::at(p.end_size_, 1) = core::at(system->extent(), 3);
    if (std::bit_cast<uint32_t>(system->size_scale()) != kFloatOneBits) {
        core::at(p.start_size_, 0) *= system->size_scale();
        core::at(p.start_size_, 1) *= system->size_scale();
        core::at(p.end_size_, 0) *= system->size_scale();
        core::at(p.end_size_, 1) *= system->size_scale();
    }
}

} // namespace

// 0x00496FE0
void EmitterInstance::get_emit(int count)
{
    if (fading_out_)
        return;
    const int first = particle_count_;
    const int total = count + first;
    particle_count_ = total > max_particles_ ? max_particles_ : total;
    const int last = particle_count_;
    maths::Mat4 m{};
    get_emit_matrix(&m);
    for (int i = first; i < last; ++i) {
        FlexibleParticle& p = core::at(particle_list_, i);
        if (!moving_) {
            set_sizes(this, p);
            p.age = 0.0f;
            p.lifetime = static_cast<float>(random_between(core::at(age_left_, 0), core::at(age_left_, 1)));
            for (int step = 0; step < get_num_vertex_lists(); ++step) {
                const std::span<float, 3> pos = position_of(this, i, step);
                core::at(pos, 0) = 0.0f;
                core::at(pos, 1) = 0.0f;
                core::at(pos, 2) = 0.0f;
            }
            continue;
        }
        // the direction: a point on the disc of the spread about the third row
        float a = random_signed();
        float b = random_signed();
        float c = random_signed();
        const double spread = static_cast<double>(angle_spread_) * 0.25f * maths::kPi;
        const double x = a * spread;
        const double y = spread * b;
        const double z = 1.0 - (x * x + y * y);
        auto zx = static_cast<float>(core::at(m.row, 2).x * z);
        auto zy = static_cast<float>(core::at(m.row, 2).y * z);
        auto zz = static_cast<float>(core::at(m.row, 2).z * z);
        auto yx = static_cast<float>(core::at(m.row, 1).x * y);
        auto yy = static_cast<float>(core::at(m.row, 1).y * y);
        auto yz = static_cast<float>(core::at(m.row, 1).z * y);
        auto xx = static_cast<float>(core::at(m.row, 0).x * x);
        auto xy = static_cast<float>(core::at(m.row, 0).y * x);
        const double xz = x * core::at(m.row, 0).z;
        auto sum_y = static_cast<float>(static_cast<double>(xy) + yy);
        auto sum_z = static_cast<float>(xz + yz);
        auto dir_x = static_cast<float>((static_cast<double>(xx) + yx) + zx);
        auto dir_y = static_cast<float>(static_cast<double>(sum_y) + zy);
        auto dir_z = static_cast<float>(static_cast<double>(sum_z) + zz);
        const double speed = random_between(core::at(speed_, 0), core::at(speed_, 1));
        const std::array<float, 3> velocity = {{static_cast<float>(dir_x * speed), static_cast<float>(dir_y * speed),
                                   static_cast<float>(dir_z * speed)}};
        // the position in the emit box (a new random point with the random spread; on its circle when circular)
        if (random_direction_) {
            a = random_signed();
            b = random_signed();
            c = random_signed();
        }
        if (circular_emit_) {
            const double f = 1.0 / std::sqrt(static_cast<double>(b) * b + static_cast<double>(a) * a);
            a = static_cast<float>(std::fabs(a) * f * a);
            b = static_cast<float>(std::fabs(b) * f * b);
        }
        const double ax = static_cast<double>(a) * core::at(emit_range_, 0);
        const double by = static_cast<double>(b) * core::at(emit_range_, 1);
        const double cz = static_cast<double>(c) * core::at(emit_range_, 2);
        auto czx = static_cast<float>(core::at(m.row, 2).x * cz);
        auto czy = static_cast<float>(core::at(m.row, 2).y * cz);
        auto czz = static_cast<float>(core::at(m.row, 2).z * cz);
        auto byx = static_cast<float>(core::at(m.row, 1).x * by);
        auto byy = static_cast<float>(core::at(m.row, 1).y * by);
        const double byz = by * core::at(m.row, 1).z;
        auto axz = static_cast<float>(core::at(m.row, 0).z * ax);
        auto px = static_cast<float>(core::at(m.row, 0).x * ax + byx);
        auto py = static_cast<float>(core::at(m.row, 0).y * ax + byy);
        const double pz = static_cast<double>(axz) + byz;
        auto pos_x = static_cast<float>(static_cast<double>(px) + czx);
        auto pos_y = static_cast<float>(static_cast<double>(py) + czy);
        auto pos_z = static_cast<float>(pz + czz);
        if (fill_path_) { // somewhere along the way it came
            auto dx = static_cast<float>(static_cast<double>(last_pos_.x) - at_point_.x);
            auto dy = static_cast<float>(static_cast<double>(last_pos_.y) - at_point_.y);
            const double dz53 = static_cast<double>(last_pos_.z) - at_point_.z;
            auto dz = static_cast<float>(dz53);
            const double length = (dz53 * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
            if (!(length == 0.0)) {
                const double r = static_cast<double>(get_rand()) * kRandomUnit;
                const double u = (r + r - 1.0 + 1.0) * 0.5;
                auto fx = static_cast<float>(dx * u);
                auto fy = static_cast<float>(dy * u);
                const double fz = u * dz;
                auto new_z = static_cast<float>(fz + pos_z);
                pos_x = static_cast<float>(static_cast<double>(fx) + pos_x);
                pos_y = static_cast<float>(static_cast<double>(fy) + pos_y);
                pos_z = new_z;
            }
        }
        for (int k = 0; k < 3; ++k)
            core::at(p.force, k) = core::at(force_, k);
        set_sizes(this, p);
        p.age = 0.0f;
        p.lifetime = static_cast<float>(random_between(core::at(age_left_, 0), core::at(age_left_, 1)));
        for (int k = 0; k < 3; ++k)
            core::at(p.velocity, k) = core::at(velocity, k);
        for (int step = 0; step < get_num_vertex_lists(); ++step) {
            const std::span<float, 3> pos = position_of(this, i, step);
            if (emitter_independent_) {
                auto world_z = static_cast<float>(static_cast<double>(pos_z) + at_point_.z);
                auto world_y = static_cast<float>(static_cast<double>(pos_y) + at_point_.y);
                core::at(pos, 0) = static_cast<float>(static_cast<double>(pos_x) + at_point_.x);
                core::at(pos, 1) = world_y;
                core::at(pos, 2) = world_z;
            } else {
                core::at(pos, 0) = pos_x;
                core::at(pos, 1) = pos_y;
                core::at(pos, 2) = pos_z;
            }
        }
    }
}

// 0x00497B00
bool EmitterInstance::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    switch (name) {
    case kSetEmitAngle: { // (degrees)
        const maths::Vec4 angles = get_vector_or_xyz(params, kAngle);
        const float x = angles.x;
        const float y = angles.y;
        const float z = angles.z;
        change_spin({.x = static_cast<float>(static_cast<double>(kDegrees) * x), .y = static_cast<float>(static_cast<double>(y) * kDegrees), .z = static_cast<float>(static_cast<double>(z) * kDegrees)});
        break;
    }
    case kSetPos: {
        const maths::Vec4 pos = get_vector_or_xyz(params, kPos);
        at_point_.x = pos.x;
        at_point_.y = pos.y;
        at_point_.z = pos.z;
        break;
    }
    case kSetParticleSize: { // (sh defaults to sw; ew to sw; eh to ew when that's given, else to sh)
        float sw = 0.0f;
        params->fetch_float(kSw, &sw, 0);
        float sh = sw;
        params->fetch_float(kSh, &sh, 0);
        core::at(extent_, 0) = sw;
        core::at(extent_, 1) = sh;
        float ew = sw;
        float eh = params->fetch_float(kEw, &ew, 0) ? ew : sh;
        params->fetch_float(kEh, &eh, 0);
        core::at(extent_, 2) = ew;
        core::at(extent_, 3) = eh;
        break;
    }
    case kSetParticleSizeScale: {
        float scale = 1.0f;
        params->fetch_float(kScale, &scale, 0);
        size_scale_ = scale;
        break;
    }
    case kEmitRate: {
        float rate = 1.0f;
        params->fetch_float(kRate, &rate, 0);
        emit_rate_ = rate;
        break;
    }
    case kSetColor: {
        int first = 0;
        int last = 0;
        int value = 0;
        if (params->fetch_int_value(kCorner, &value, 0)) {
            first = last = value;
        } else {
            first = 0;
            last = get_num_colours() - 1;
        }
        if (first < get_num_colours() && last < get_num_colours()) {
            for (int i = first; i < last + 1; ++i) {
                if (i >= get_num_colours())
                    continue;
                colour::Bytes end = colour::bytes_of(core::at(end_colour_, i));
                colour::Bytes mid = colour::bytes_of(core::at(mid_colour_, i));
                colour::Bytes start = colour::bytes_of(core::at(start_colour_, i));
                for (int k = 0; k < 4; ++k) {
                    if (params->fetch_int_value(core::at(kEndChannels, k), &value, 0)) {
                        core::at(end, k) = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                                  : static_cast<uint8_t>(value);
                        end_colour_set_ = true;
                    }
                }
                for (int k = 0; k < 4; ++k) {
                    if (params->fetch_int_value(core::at(kMidChannels, k), &value, 0))
                        core::at(mid, k) = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                                  : static_cast<uint8_t>(value);
                }
                for (int k = 0; k < 4; ++k) { // (the end too, unless it was given)
                    if (params->fetch_int_value(core::at(kStartChannels, k), &value, 0)) {
                        const uint8_t byte = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                                    : static_cast<uint8_t>(value);
                        core::at(start, k) = byte;
                        if (!end_colour_set_)
                            core::at(end, k) = byte;
                    }
                }
                core::at(end_colour_, i) = colour::packed_of(end);
                core::at(mid_colour_, i) = colour::packed_of(mid);
                core::at(start_colour_, i) = colour::packed_of(start);
            }
        }
        params->fetch_float(kMidTime, &colour_mid_, 0);
        break;
    }
    case kEmpty:
        particle_count_ = 0;
        break;
    case kSpeedSpanCmd: {
        float min = 0.0f;
        params->fetch_float(kMin, &min, 0);
        float max = min;
        params->fetch_float(kMax, &max, 0);
        core::at(speed_, 0) = min;
        core::at(speed_, 1) = max;
        break;
    }
    case kEmit: {
        int count = 1;
        params->fetch_int_value(kNum, &count, 0);
        get_emit(count);
        break;
    }
    case kSetAngleSpread: {
        float spread = 0.0f;
        params->fetch_float(kSpread, &spread, 0);
        angle_spread_ = spread;
        break;
    }
    case kApplyEnvBrightness:
        if (params->has_bare_flag(kEveryUpdate)) {
            update_brightness_ = true;
        } else {
            update_brightness_ = false;
            update_ground_brightness(true);
        }
        break;
    case kSetEmitRange: {
        float width = 0.0f;
        params->fetch_float(kWidth, &width, 0);
        float height = width;
        params->fetch_float(kHeight, &height, 0);
        float depth = 0.0f;
        params->fetch_float(kDepth, &depth, 0);
        set_emit_range({.x = width, .y = height, .z = depth});
        break;
    }
    case kSetForce: {
        set_force(get_vector_or_xyz(params, kForce));
        break;
    }
    case kSetEmitTarget: {
        const maths::Vec4 target = get_vector_or_xyz(params, kTarget);
        if (!(target.x == 0.0f) || !(target.y == 1.0f) || !(target.z == 0.0f)) // (up is the default; NaN isn't)
            set_emit_target(target);
        break;
    }
    case kSetLife: {
        float min = 0.0f;
        params->fetch_float(kMin, &min, 0);
        float max = min;
        params->fetch_float(kMax, &max, 0);
        core::at(age_left_, 0) = min;
        core::at(age_left_, 1) = max;
        moving_ = true;
        break;
    }
    case kSetEmitterIndependent: {
        int value = 0;
        params->fetch_int_value(0u, &value, 0);
        emitter_independent_ = value == 1;
        break;
    }
    case kRandomSpread: {
        int value = 0;
        params->fetch_int_value(kRandom, &value, 0);
        set_flag(4, value > 0);
        random_direction_ = value > 0;
        break;
    }
    case kSetCircularEmit: {
        int value = 0;
        params->fetch_int_value(kCircular, &value, 0);
        set_flag(kSystemFlagCircular, value > 0);
        circular_emit_ = value > 0;
        break;
    }
    case kSetLodParams: {
        int min_particles = 0;
        params->fetch_int_value(kMinParticles, &min_particles, 0); // (the bool-assert getter, 0x00476CB0)
        float near = 0.0f;
        float far = 0.0f;
        params->fetch_float(kLodDist1, &near, 0);
        params->fetch_float(kLodDist2, &far, 0);
        lod_min_particles_ = min_particles;
        lod_near_ = near * near;
        lod_far_ = far * far;
        break;
    }
    default:
        break;
    }
    return true;
}

void EmitterInstance::update_ground_brightness(bool movables)
{
    ProbeRay feeler;
    const float y = at_point_.y;
    feeler.line().start = {.x = at_point_.x, .y = static_cast<float>(static_cast<double>(y) + 1.0), .z = at_point_.z, .w = at_point_.w};
    feeler.line().end = {.x = at_point_.x, .y = static_cast<float>(static_cast<double>(y) - kGroundProbeDepth), .z = at_point_.z, .w = at_point_.w};
    if (feeler.get_hit_shape(movables, false)) {
        const double value = feeler.get_hit_vertex_value();
        brightness_ = static_cast<float>(value + value);
    }
}

void EmitterInstance::remove_particle(int index, bool through_accessors)
{
    --particle_count_;
    core::at(particle_list_, index) = core::at(particle_list_, particle_count_);
    for (int step = 0; step < get_num_vertex_lists(); ++step) {
        if (through_accessors) {
            change_place(index, step, fetch_place(particle_count_, step));
        } else {
            const std::span<const float, 3> from = position_of(this, particle_count_, step);
            const std::span<float, 3> to = position_of(this, index, step);
            core::at(to, 0) = core::at(from, 0);
            core::at(to, 1) = core::at(from, 1);
            core::at(to, 2) = core::at(from, 2);
        }
        const std::span<float, 3> gone = position_of(this, particle_count_, step);
        core::at(gone, 0) = kFarAway;
        core::at(gone, 1) = kFarAway;
        core::at(gone, 2) = kFarAway;
    }
}

// 0x004987F0
void EmitterInstance::tick_frame(float time, bool skip)
{
    if (fading_out_ && particle_count_ == 0) {
        delete this;
        return;
    }
    if (unknown78_ && !objects::find_object(unknown78_)) { // (following an object that's gone)
        delete this;
        return;
    }
    if (skip || !unknown12f_)
        return;
    run_emit_script();
    update_bounds();
    unknown130_ = true;
    if (modules::viewport_count() == 1 && viewport_camera(0) && unknown133_ && sphere_hidden(&bounds_, bounds_.w)) {
        unknown130_ = false;
        if (emit_rate_ == 0.0f) { // out of view and not emitting: the particles only age
            for (int i = 0; i < particle_count_; ++i) {
                FlexibleParticle& p = core::at(particle_list_, i);
                const double age = static_cast<double>(time) + p.age;
                p.age = static_cast<float>(age);
                if (age > p.lifetime) {
                    remove_particle(i, true);
                    --i;
                }
            }
            return;
        }
    }
    if (!(emit_rate_ == 0.0f)) {
        const double owed = static_cast<double>(time) + emit_owed_;
        emit_owed_ = static_cast<float>(owed);
        const int count = truncate(owed * emit_rate_);
        if (count != 0) {
            emit_owed_ = static_cast<float>(owed - static_cast<double>(count) / emit_rate_);
            get_emit(count);
        }
    }
    if (tick_script_)
        tick_script_->tick_frame();
    if (!moving_ || time == 0.0f)
        return;
    if (update_brightness_)
        update_ground_brightness(false);
    auto dt = static_cast<float>(static_cast<double>(view_setup().frame_time_scale()) * time);
    for (int i = 0; i < particle_count_; ++i) {
        FlexibleParticle& p = core::at(particle_list_, i);
        p.age = static_cast<float>(static_cast<double>(time) + p.age);
        for (int k = 0; k < 3; ++k)
            core::at(p.velocity, k) = static_cast<float>(static_cast<double>(dt) * core::at(p.force, k) + core::at(p.velocity, k));
        for (int step = get_num_vertex_lists() - 1; step > 0; --step) { // the history moves on a step
            const std::span<const float, 3> from = position_of(this, i, step - 1);
            const std::span<float, 3> to = position_of(this, i, step);
            core::at(to, 0) = core::at(from, 0);
            core::at(to, 1) = core::at(from, 1);
            core::at(to, 2) = core::at(from, 2);
        }
        const std::span<float, 3> pos = position_of(this, i, 0);
        for (int k = 0; k < 3; ++k)
            core::at(pos, k) = static_cast<float>(static_cast<double>(dt) * core::at(p.velocity, k) + core::at(pos, k));
    }
    for (int i = 0; i < particle_count_; ++i) { // the expired
        if (core::at(particle_list_, i).age > core::at(particle_list_, i).lifetime) {
            remove_particle(i, false);
            --i;
        }
    }
    const int lod = lod_particle_count(particle_count_);
    for (int i = 0; i < particle_count_; ++i) { // beyond the LOD count
        if (lod < particle_count_) {
            remove_particle(i, false);
            --i;
        }
    }
    last_pos_ = at_point_;
}

// 0x00499EE0
uint32_t* ModelParticles::lerp_colour(uint32_t* out, const colour::Bytes& from, const colour::Bytes& to, float t)
{
    const double green = static_cast<double>(core::at(colour_scale(), 1)) * brightness();
    auto blue = static_cast<float>(static_cast<double>(core::at(colour_scale(), 2)) * brightness());
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(truncate(static_cast<double>(core::at(to, k) - core::at(from, k)) * t) +
                                    core::at(from, k));
    };
    const uint8_t r = lerp(0);
    const uint8_t g = lerp(1);
    const uint8_t b = lerp(2);
    const uint8_t a = lerp(3);
    const double red = static_cast<double>(core::at(colour_scale(), 0)) * brightness();
    auto clamp = [](double value) {
        if (value < 0.0)
            value = 0.0;
        else if (value > kByteMax)
            value = kByteMax;
        return static_cast<uint8_t>(truncate(value));
    };
    std::array<uint8_t, 4> bytes = {{clamp(red * r), clamp(green * g), clamp(static_cast<double>(b) * blue), a}};
    std::memcpy(out, bytes.data(), 4);
    return out;
}

} // namespace render
