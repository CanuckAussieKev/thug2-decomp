// The engine's flexible particle systems: emission, the member functions scripts reach through the flexible particle
// component, and the update (see particle_system.h). From the disassembly of 0x00496FE0-0x0049778E,
// 0x00497B00-0x0049868F, 0x004987F0-0x00498F7D and 0x00499EE0-0x0049A080. The x87 code is followed at 53 bits: what
// it keeps on its stack is a double here, what it stores to memory a float.

#include "engine/gfx/particle_system.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/viewport.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "platform/crt.h"

namespace render {

namespace {

constexpr float kRandomUnit = std::bit_cast<float>(0x38000100u); // 0x00646510: 1 / 32767
constexpr float kPi = std::bit_cast<float>(0x40490FDBu);         // 0x006493C0
constexpr float kDegrees = std::bit_cast<float>(0x3C8EFA35u);    // 0x00645464: pi / 180
constexpr float kFarAway = 1024.0f;                              // where a removed particle's positions go

int get_rand() { return crt::get_rand(); } // (the game's C runtime's)
// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
// A random number in [-1, 1].
float random_signed()
{
    double v = static_cast<double>(get_rand()) * kRandomUnit;
    return static_cast<float>(v + v - 1.0);
}
// min + (max - min) times a random fraction.
double random_between(float min, float max)
{
    double t = static_cast<double>(get_rand()) * kRandomUnit;
    return t * (static_cast<double>(max) - min) + min;
}

int viewport_count() { return modules::g_viewport_count; }
void* viewport_camera(int viewport) { return fetch_live_camera(viewport); }
// 0x0048C490: whether a sphere is out of the camera's view.
int sphere_hidden(const maths::Vec4* centre, float radius)
{
    return engine::sphere_visible_in_one_viewport(centre, radius);
}

float* position_of(EmitterInstance* system, int particle, int step)
{
    return system->position_array_ + 3 * (system->max_particles_ * step + particle);
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
const uint32_t kEndChannels[4] = {0x18A762F2, 0x757A8619, 0x05107296, 0x9C19232C};   // er, eg, eb, ea
const uint32_t kMidChannels[4] = {0xD07EE8FA, 0xBDA30C11, 0xCDC9F89E, 0x54C0A924};   // mr, mg, mb, ma
const uint32_t kStartChannels[4] = {0x043FD725, 0x69E233CE, 0x1988C741, 0x808196FB}; // sr, sg, sb, sa

// A vector parameter, or x, y and z ones.
void get_vector_or_xyz(script::Struct* params, uint32_t name, float& x, float& y, float& z)
{
    script::VectorValue v{};
    if (params->fetch_vec(name, &v, 0)) {
        x = v.x;
        y = v.y;
        z = v.z;
        return;
    }
    params->fetch_float(kX, &x, 0);
    params->fetch_float(kY, &y, 0);
    params->fetch_float(kZ, &z, 0);
}

// Sizes from the system's, scaled (unless the scale is exactly 1).
void set_sizes(EmitterInstance* system, FlexibleParticle& p)
{
    p.start_size_[0] = system->extent_[0];
    p.start_size_[1] = system->extent_[1];
    p.end_size_[0] = system->extent_[2];
    p.end_size_[1] = system->extent_[3];
    if (std::bit_cast<uint32_t>(system->size_scale_) != 0x3F800000) {
        p.start_size_[0] *= system->size_scale_;
        p.start_size_[1] *= system->size_scale_;
        p.end_size_[0] *= system->size_scale_;
        p.end_size_[1] *= system->size_scale_;
    }
}

} // namespace

// 0x00496FE0
void EmitterInstance::get_emit(int count)
{
    if (fading_out_)
        return;
    int first = particle_count_;
    int total = count + first;
    particle_count_ = total > max_particles_ ? max_particles_ : total;
    int last = particle_count_;
    maths::Mat4 m{};
    get_emit_matrix(&m);
    for (int i = first; i < last; ++i) {
        FlexibleParticle& p = particle_list_[i];
        if (!moving_) {
            set_sizes(this, p);
            p.age = 0.0f;
            p.lifetime = static_cast<float>(random_between(age_left_[0], age_left_[1]));
            for (int step = 0; step < get_num_vertex_lists(); ++step) {
                float* pos = position_of(this, i, step);
                pos[0] = 0.0f;
                pos[1] = 0.0f;
                pos[2] = 0.0f;
            }
            continue;
        }
        // the direction: a point on the disc of the spread about the third row
        float a = random_signed(), b = random_signed(), c = random_signed();
        double spread = static_cast<double>(angle_spread_) * 0.25f * kPi;
        double x = a * spread;
        double y = spread * b;
        double z = 1.0 - (x * x + y * y);
        auto zx = static_cast<float>(m.row[2].x * z), zy = static_cast<float>(m.row[2].y * z),
             zz = static_cast<float>(m.row[2].z * z);
        auto yx = static_cast<float>(m.row[1].x * y), yy = static_cast<float>(m.row[1].y * y),
             yz = static_cast<float>(m.row[1].z * y);
        auto xx = static_cast<float>(m.row[0].x * x), xy = static_cast<float>(m.row[0].y * x);
        double xz = x * m.row[0].z;
        auto sum_y = static_cast<float>(static_cast<double>(xy) + yy);
        auto sum_z = static_cast<float>(xz + yz);
        auto dir_x = static_cast<float>((static_cast<double>(xx) + yx) + zx);
        auto dir_y = static_cast<float>(static_cast<double>(sum_y) + zy);
        auto dir_z = static_cast<float>(static_cast<double>(sum_z) + zz);
        double speed = random_between(speed_[0], speed_[1]);
        float velocity[3] = {static_cast<float>(dir_x * speed), static_cast<float>(dir_y * speed),
                             static_cast<float>(dir_z * speed)};
        // the position in the emit box (a new random point with the random spread; on its circle when circular)
        if (random_direction_) {
            a = random_signed();
            b = random_signed();
            c = random_signed();
        }
        if (circular_emit_) {
            double f = 1.0 / std::sqrt(static_cast<double>(b) * b + static_cast<double>(a) * a);
            a = static_cast<float>(std::fabs(a) * f * a);
            b = static_cast<float>(std::fabs(b) * f * b);
        }
        double ax = static_cast<double>(a) * emit_range_[0];
        double by = static_cast<double>(b) * emit_range_[1];
        double cz = static_cast<double>(c) * emit_range_[2];
        auto czx = static_cast<float>(m.row[2].x * cz), czy = static_cast<float>(m.row[2].y * cz),
             czz = static_cast<float>(m.row[2].z * cz);
        auto byx = static_cast<float>(m.row[1].x * by), byy = static_cast<float>(m.row[1].y * by);
        double byz = by * m.row[1].z;
        auto axz = static_cast<float>(m.row[0].z * ax);
        auto px = static_cast<float>(m.row[0].x * ax + byx);
        auto py = static_cast<float>(m.row[0].y * ax + byy);
        double pz = static_cast<double>(axz) + byz;
        auto pos_x = static_cast<float>(static_cast<double>(px) + czx);
        auto pos_y = static_cast<float>(static_cast<double>(py) + czy);
        auto pos_z = static_cast<float>(pz + czz);
        if (fill_path_) { // somewhere along the way it came
            auto dx = static_cast<float>(static_cast<double>(last_pos_.x) - at_point_.x);
            auto dy = static_cast<float>(static_cast<double>(last_pos_.y) - at_point_.y);
            double dz53 = static_cast<double>(last_pos_.z) - at_point_.z;
            auto dz = static_cast<float>(dz53);
            double length = (dz53 * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
            if (!(length == 0.0)) {
                double r = static_cast<double>(get_rand()) * kRandomUnit;
                double u = (r + r - 1.0 + 1.0) * 0.5;
                auto fx = static_cast<float>(dx * u), fy = static_cast<float>(dy * u);
                double fz = u * dz;
                auto new_z = static_cast<float>(fz + pos_z);
                pos_x = static_cast<float>(static_cast<double>(fx) + pos_x);
                pos_y = static_cast<float>(static_cast<double>(fy) + pos_y);
                pos_z = new_z;
            }
        }
        for (int k = 0; k < 3; ++k)
            p.force[k] = force_[k];
        set_sizes(this, p);
        p.age = 0.0f;
        p.lifetime = static_cast<float>(random_between(age_left_[0], age_left_[1]));
        for (int k = 0; k < 3; ++k)
            p.velocity[k] = velocity[k];
        for (int step = 0; step < get_num_vertex_lists(); ++step) {
            float* pos = position_of(this, i, step);
            if (emitter_independent_) {
                auto world_z = static_cast<float>(static_cast<double>(pos_z) + at_point_.z);
                auto world_y = static_cast<float>(static_cast<double>(pos_y) + at_point_.y);
                pos[0] = static_cast<float>(static_cast<double>(pos_x) + at_point_.x);
                pos[1] = world_y;
                pos[2] = world_z;
            } else {
                pos[0] = pos_x;
                pos[1] = pos_y;
                pos[2] = pos_z;
            }
        }
    }
}

// 0x00497B00
bool EmitterInstance::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    switch (name) {
    case kSetEmitAngle: { // (degrees)
        float x = 0.0f, y = 0.0f, z = 0.0f;
        get_vector_or_xyz(params, kAngle, x, y, z);
        change_spin(static_cast<float>(static_cast<double>(kDegrees) * x),
                    static_cast<float>(static_cast<double>(y) * kDegrees),
                    static_cast<float>(static_cast<double>(z) * kDegrees));
        break;
    }
    case kSetPos: {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        get_vector_or_xyz(params, kPos, x, y, z);
        at_point_.x = x;
        at_point_.y = y;
        at_point_.z = z;
        break;
    }
    case kSetParticleSize: { // (sh defaults to sw; ew to sw; eh to ew when that's given, else to sh)
        float sw = 0.0f;
        params->fetch_float(kSw, &sw, 0);
        float sh = sw;
        params->fetch_float(kSh, &sh, 0);
        extent_[0] = sw;
        extent_[1] = sh;
        float ew = sw;
        float eh = params->fetch_float(kEw, &ew, 0) ? ew : sh;
        params->fetch_float(kEh, &eh, 0);
        extent_[2] = ew;
        extent_[3] = eh;
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
        int first = 0, last = 0, value = 0;
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
                uint8_t* end = end_colour_ + i * 4;
                uint8_t* mid = mid_colour_ + i * 4;
                uint8_t* start = start_colour_ + i * 4;
                for (int k = 0; k < 4; ++k) {
                    if (params->fetch_int_value(kEndChannels[k], &value, 0)) {
                        end[k] = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                        : static_cast<uint8_t>(value);
                        end_colour_set_ = true;
                    }
                }
                for (int k = 0; k < 4; ++k) {
                    if (params->fetch_int_value(kMidChannels[k], &value, 0))
                        mid[k] = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                        : static_cast<uint8_t>(value);
                }
                for (int k = 0; k < 4; ++k) { // (the end too, unless it was given)
                    if (params->fetch_int_value(kStartChannels[k], &value, 0)) {
                        uint8_t byte = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                              : static_cast<uint8_t>(value);
                        start[k] = byte;
                        if (!end_colour_set_)
                            end[k] = byte;
                    }
                }
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
        speed_[0] = min;
        speed_[1] = max;
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
        set_emit_range(width, height, depth);
        break;
    }
    case kSetForce: {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        get_vector_or_xyz(params, kForce, x, y, z);
        set_force(x, y, z);
        break;
    }
    case kSetEmitTarget: {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        get_vector_or_xyz(params, kTarget, x, y, z);
        if (!(x == 0.0f) || !(y == 1.0f) || !(z == 0.0f)) // (up is the default; NaN isn't)
            set_emit_target(x, y, z);
        break;
    }
    case kSetLife: {
        float min = 0.0f;
        params->fetch_float(kMin, &min, 0);
        float max = min;
        params->fetch_float(kMax, &max, 0);
        age_left_[0] = min;
        age_left_[1] = max;
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
        set_flag(8, value > 0);
        circular_emit_ = value > 0;
        break;
    }
    case kSetLodParams: {
        int min_particles = 0;
        params->fetch_int_value(kMinParticles, &min_particles, 0); // (the bool-assert getter, 0x00476CB0)
        float near = 0.0f, far = 0.0f;
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
    float y = at_point_.y;
    feeler.line_.start = {at_point_.x, static_cast<float>(static_cast<double>(y) + 1.0), at_point_.z, at_point_.w};
    feeler.line_.end = {at_point_.x, static_cast<float>(static_cast<double>(y) - 4800.0), at_point_.z, at_point_.w};
    if (feeler.get_hit_shape(movables, false)) {
        double value = feeler.get_hit_vertex_value();
        brightness_ = static_cast<float>(value + value);
    }
}

void EmitterInstance::remove_particle(int index, bool through_accessors)
{
    --particle_count_;
    particle_list_[index] = particle_list_[particle_count_];
    for (int step = 0; step < get_num_vertex_lists(); ++step) {
        if (through_accessors) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            fetch_place(particle_count_, step, &x, &y, &z);
            change_place(index, step, x, y, z);
        } else {
            const float* from = position_of(this, particle_count_, step);
            float* to = position_of(this, index, step);
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
        float* gone = position_of(this, particle_count_, step);
        gone[0] = kFarAway;
        gone[1] = kFarAway;
        gone[2] = kFarAway;
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
    if (viewport_count() == 1 && viewport_camera(0) && unknown133_ && sphere_hidden(&bounds_, bounds_.w)) {
        unknown130_ = false;
        if (emit_rate_ == 0.0f) { // out of view and not emitting: the particles only age
            for (int i = 0; i < particle_count_; ++i) {
                FlexibleParticle& p = particle_list_[i];
                double age = static_cast<double>(time) + p.age;
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
        double owed = static_cast<double>(time) + emit_owed_;
        emit_owed_ = static_cast<float>(owed);
        int count = truncate(owed * emit_rate_);
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
    auto dt = static_cast<float>(static_cast<double>(g_frame_time_scale) * time);
    for (int i = 0; i < particle_count_; ++i) {
        FlexibleParticle& p = particle_list_[i];
        p.age = static_cast<float>(static_cast<double>(time) + p.age);
        for (int k = 0; k < 3; ++k)
            p.velocity[k] = static_cast<float>(static_cast<double>(dt) * p.force[k] + p.velocity[k]);
        for (int step = get_num_vertex_lists() - 1; step > 0; --step) { // the history moves on a step
            const float* from = position_of(this, i, step - 1);
            float* to = position_of(this, i, step);
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
        float* pos = position_of(this, i, 0);
        for (int k = 0; k < 3; ++k)
            pos[k] = static_cast<float>(static_cast<double>(dt) * p.velocity[k] + pos[k]);
    }
    for (int i = 0; i < particle_count_; ++i) { // the expired
        if (particle_list_[i].age > particle_list_[i].lifetime) {
            remove_particle(i, false);
            --i;
        }
    }
    int lod = lod_particle_count(particle_count_);
    for (int i = 0; i < particle_count_; ++i) { // beyond the LOD count
        if (lod < particle_count_) {
            remove_particle(i, false);
            --i;
        }
    }
    last_pos_ = at_point_;
}

// 0x00499EE0
uint32_t* ModelParticles::lerp_colour(uint32_t* out, const uint8_t* from, const uint8_t* to, float t)
{
    double green = static_cast<double>(colour_scale_[1]) * brightness_;
    auto blue = static_cast<float>(static_cast<double>(colour_scale_[2]) * brightness_);
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(truncate(static_cast<double>(to[k] - from[k]) * t) + from[k]);
    };
    uint8_t r = lerp(0), g = lerp(1), b = lerp(2), a = lerp(3);
    double red = static_cast<double>(colour_scale_[0]) * brightness_;
    auto clamp = [](double value) {
        if (value < 0.0)
            value = 0.0;
        else if (value > 255.0)
            value = 255.0;
        return static_cast<uint8_t>(truncate(value));
    };
    uint8_t bytes[4] = {clamp(red * r), clamp(green * g), clamp(static_cast<double>(b) * blue), a};
    std::memcpy(out, bytes, 4);
    return out;
}

} // namespace render
