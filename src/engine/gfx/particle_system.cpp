// The engine's flexible particle systems (render::EmitterInstance and its types' engine classes): see particle_system.h.
// From the disassembly of 0x00496890-0x0049A0BF (0x004968A0 isn't a Ghidra function). Emit, the member functions and
// Update are in particle_system_update.cpp.

#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_system_internal.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/model.h"
#include "engine/gfx/viewport.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "game/skate/skate_module.h"

namespace render {

uint8_t g_alloc_assert_flag;

namespace {

constexpr float kNearlyUp = 0.99f;        // 0x00645450
constexpr float kFramesPerSecond = 60.0f; // 0x00645EA0

void* particle_hub_handle() { return g_flexible_particle_manager; }
int viewport_count() { return modules::g_viewport_count; }
gfx::ViewCam* viewport_camera(int viewport) { return fetch_live_camera(viewport); }
// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The allocations of the types' constructors: the manager's, its assert flag the global byte 0x00658555.
void* carve(unsigned int size) { return memory::Hub::get_pull()->carve(size, g_alloc_assert_flag, nullptr); }
void* allocate_zeroed(unsigned int size)
{
    return memory::Hub::get_pull()->allocate_zeroed(size, g_alloc_assert_flag, nullptr);
}

script::Struct* spawn_param_table()
{
    script::Struct* structure = script::spawn_param_bag();
    return structure ? memory::construct_in<script::Struct>(structure) : nullptr;
}

} // namespace

// (shared with the other types' constructors: particle_system_internal.h)
namespace particle_parts {

// The particles: `count` after their count (the compiler's array new; their constructor and destructor are empty).
FlexibleParticle* new_particles(int count)
{
    auto* memory = static_cast<int*>(carve(static_cast<unsigned int>(count) * sizeof(FlexibleParticle) + 4));
    if (!memory)
        return nullptr;
    *memory = count; // (then the empty constructor run over each: 0x0062388B with 0x005902E0)
    return reinterpret_cast<FlexibleParticle*>(memory + 1);
}
void delete_particles(FlexibleParticle* particles)
{
    // (the empty destructor run over each first: 0x0062382B with 0x005480A0)
    memory::dealloc(reinterpret_cast<int*>(particles) - 1);
}

// The three colour arrays, `count` RGBA colours each (the constructor iterator 0x00401020 runs an empty one over
// them), each allocated only if the one before was; all grey once the three are there. Whether they are.
bool new_colours(EmitterInstance* system, int count)
{
    uint8_t** colours[3] = {&system->start_colour_, &system->mid_colour_, &system->end_colour_};
    for (uint8_t** colour : colours) {
        *colour = static_cast<uint8_t*>(carve(static_cast<unsigned int>(count) * 4));
        if (!*colour)
            return false;
    }
    for (int i = 0; i < count; ++i)
        for (uint8_t* const* colour : colours)
            for (int k = 0; k < 4; ++k)
                (*colour)[i * 4 + k] = 0x80;
    return true;
}

// A type's constructor after the base's: the name and the maximum, no particles yet; the particles, then the
// positions (`positions_zeroed`: zeroed memory), then the colours. Whether everything was allocated.
bool construct_parts(EmitterInstance* system, uint32_t name, int particle_cap, unsigned int positions_size,
                     bool positions_zeroed, int colours)
{
    system->label_ = name;
    system->max_particles_ = particle_cap;
    system->particle_count_ = 0;
    system->particle_list_ = new_particles(particle_cap);
    if (!system->particle_list_)
        return false;
    system->position_array_ =
        static_cast<float*>(positions_zeroed ? allocate_zeroed(positions_size) : carve(positions_size));
    if (!system->position_array_)
        return false;
    return new_colours(system, colours);
}

} // namespace particle_parts

using namespace particle_parts;

// --- life ---

void* EmitterInstance::operator new(std::size_t size) noexcept
{
    return allocate_zeroed(static_cast<unsigned int>(size));
}

void EmitterInstance::operator delete(void* memory) { memory::dealloc(memory); }

// 0x00496F10
EmitterInstance::EmitterInstance()
{
    list_link_.make_node(this);
    get_reset();
}

// 0x00496D70
void EmitterInstance::get_reset()
{
    emit_script_ptr_ = nullptr;
    tick_script_ = nullptr;
    script_args_ = nullptr;
    xform_ref_ = nullptr;
    emit_rate_ = 0.0f;
    emit_owed_ = 0.0f;
    lod_min_particles_ = 0;
    lod_near_ = 0.0f;
    lod_far_ = 0.0f;
    at_point_ = maths::Vec4{2048.0f, 144.0f, -928.0f, 0.0f};
    last_pos_ = maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    size_scale_ = 1.0f;
    emit_range_[0] = 1.0f;
    emit_range_[1] = 1.0f;
    emit_target_[1] = 1.0f;
    speed_[0] = 1.0f;
    brightness_ = 1.0f;
    emit_range_[2] = 0.0f;
    angle_spread_ = 0.5f;
    force_[0] = 0.0f;
    force_[1] = -0.02f;
    force_[2] = 0.0f;
    emit_target_[0] = 0.0f;
    emit_target_[2] = 0.0f;
    orientation_turn_[0] = 0.0f;
    orientation_turn_[1] = 0.0f;
    orientation_turn_[2] = 0.0f;
    rank_ = 0;
    for (float& size : extent_)
        size = 2.0f;
    age_left_[0] = 2.0f;
    age_left_[1] = 2.0f;
    speed_[1] = 2.0f;
    flag_bits_ &= ~1u;
    for (float& scale : colour_scale_)
        scale = 1.0f;
    moving_ = false;
    end_colour_set_ = false;
    flag_bits_ |= 1u;
    unknown12f_ = true;
    flag_bits_ &= ~0x10u;
    flag_bits_ &= ~1u;
    random_direction_ = false;
    flag_bits_ |= 0x100u;
    circular_emit_ = true;
    aligned_ = false;
    fading_out_ = false;
    fill_path_ = false;
    emitter_independent_ = false;
    update_brightness_ = false;
    unknown130_ = true;
    unknown133_ = true;
    unknown78_ = 0;
}

// 0x00498690
EmitterInstance::~EmitterInstance()
{
    if (emit_script_ptr_)
        script::Script::destroy(emit_script_ptr_);
    if (tick_script_)
        script::Script::destroy(tick_script_);
    if (script::Struct* params = script_args_) {
        params->get_clear();
        script::delete_struct(params);
    }
    if (start_colour_)
        memory::dealloc(start_colour_);
    if (mid_colour_)
        memory::dealloc(mid_colour_);
    if (end_colour_)
        memory::dealloc(end_colour_);
    if (position_array_)
        memory::dealloc(position_array_);
    if (particle_list_)
        delete_particles(particle_list_);
    // the particle manager lets go of it
    static_cast<FlexibleParticleManager*>(particle_hub_handle())->remove_system(this);
    list_link_.uncouple(); // (0x00496D10)
    // (then SharedBase's: its smart pointers cleared)
}

// 0x004A1040 (the weather's "plat_render" stub, shared)
void EmitterInstance::render() { debug::printf("STUB: plat_render\n"); }

// --- settings ---

// 0x004968B0
void EmitterInstance::set_emit_range(float width, float height, float depth)
{
    emit_range_[0] = width;
    emit_range_[1] = height;
    emit_range_[2] = depth;
}

// 0x004968E0
void EmitterInstance::set_force(float x, float y, float z)
{
    force_[0] = x;
    force_[1] = y;
    force_[2] = z;
}

// 0x00496910
void EmitterInstance::set_emit_target(float x, float y, float z)
{
    emit_target_[0] = x;
    emit_target_[1] = y;
    emit_target_[2] = z;
}

// 0x00496940
void EmitterInstance::change_spin(float x, float y, float z)
{
    orientation_turn_[0] = x;
    orientation_turn_[1] = y;
    orientation_turn_[2] = z;
}

// 0x00496970
void EmitterInstance::fetch_place(int particle, int step, float* x, float* y, float* z)
{
    const float* position = position_array_ + 3 * (max_particles_ * step + particle);
    *x = position[0];
    *y = position[1];
    *z = position[2];
}

// 0x004969B0
void EmitterInstance::change_place(int particle, int step, float x, float y, float z)
{
    float* position = position_array_ + 3 * (max_particles_ * step + particle);
    position[0] = x;
    position[1] = y;
    position[2] = z;
}

// 0x004969F0
void EmitterInstance::get_emit_matrix(maths::Mat4* out)
{
    if (xform_ref_) {
        *out = *xform_ref_;
        return;
    }
    out->row[0] = maths::Vec4{1.0f, 0.0f, 0.0f, 0.0f};
    out->row[1] = maths::Vec4{0.0f, 1.0f, 0.0f, 0.0f};
    out->row[2] = maths::Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    out->row[3] = maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    maths::Vec4& at = out->row[2];
    at = maths::Vec4{emit_target_[0], emit_target_[1], emit_target_[2], 1.0f};
    at.make_unit();
    // across: up x the target (along x when the target is nearly up); then the second row across both (x87, 53 bits)
    double up = 1.0, along = 0.0;
    if (std::fabs(at.y) > kNearlyUp) {
        up = 0.0;
        along = 1.0;
    }
    maths::Vec4& across = out->row[0];
    across.w = 0.0f;
    auto z = static_cast<float>(along * at.y - up * at.x);
    across.x = static_cast<float>(up * at.z - static_cast<double>(at.y) * 0.0);
    across.y = static_cast<float>(static_cast<double>(at.x) * 0.0 - along * at.z);
    across.z = z;
    maths::Vec4& second = out->row[1];
    second.w = 0.0f;
    auto second_z = static_cast<float>(static_cast<double>(at.y) * across.x - static_cast<double>(at.x) * across.y);
    second.x = static_cast<float>(static_cast<double>(across.y) * at.z - static_cast<double>(across.z) * at.y);
    second.y = static_cast<float>(static_cast<double>(across.z) * at.x - static_cast<double>(across.x) * at.z);
    second.z = second_z;
}

// 0x00496B40
int EmitterInstance::lod_particle_count(int count)
{
    if (lod_near_ >= lod_far_ || viewport_count() > 1) // (NaN distances go on)
        return count;
    gfx::ViewCam* camera = viewport_camera(0);
    if (!camera)
        return count;
    const maths::Vec4* eye = camera->fetch_location();
    const double dx = static_cast<double>(at_point_.x) - eye->x;
    const double dy = static_cast<double>(at_point_.y) - eye->y;
    const double dz = static_cast<double>(at_point_.z) - eye->z;
    auto distance = static_cast<float>(dz * dz + dy * dy + dx * dx);
    if (distance < lod_near_)
        return count;
    if (distance > lod_far_)
        return lod_min_particles_;
    const double t = (static_cast<double>(lod_far_) - distance) / (static_cast<double>(lod_far_) - lod_near_);
    const int lod = truncate(t * (max_particles_ - lod_min_particles_) + lod_min_particles_);
    return count < lod ? count : lod;
}

// 0x00496C30
void EmitterInstance::update_bounds()
{
    float force = std::fabs(force_[2]);
    if (std::fabs(force_[1]) > force)
        force = std::fabs(force_[1]);
    if (std::fabs(force_[0]) > force)
        force = std::fabs(force_[0]);
    const double travel = static_cast<double>(force) * age_left_[1] + speed_[1];
    bounds_ = at_point_;
    float range = emit_range_[2];
    if (emit_range_[1] > range)
        range = emit_range_[1];
    if (emit_range_[0] > range)
        range = emit_range_[0];
    bounds_.w = static_cast<float>(travel * age_left_[1] * kFramesPerSecond + range);
}

// 0x00496CE0
void EmitterInstance::set_colour_scale(const float* rgba)
{
    if (flag_bits_ & 0x10000) {
        for (int i = 0; i < 4; ++i)
            colour_scale_[i] = rgba[i];
    }
}

// 0x00496F60
void EmitterInstance::run_emit_script()
{
    if (emit_script_ptr_ && emit_script_ptr_->tick_frame() == script::kScriptOutcomeFinished) {
        if (script::Script* script = emit_script_ptr_)
            script::Script::destroy(script);
        emit_script_ptr_ = nullptr;
    }
}

// 0x00496FA0
void EmitterInstance::render_if_visible(bool force)
{
    if ((unknown12f_ && unknown130_) || force) {
        render();
        flag_bits_ &= ~1u;
        unknown12f_ = false;
    }
}

namespace {

// 0x00497790 / 0x00497900: a script slot set (see SetEmitScript).
void bind_script(EmitterInstance* system, script::Script*& slot, uint32_t script, script::Struct* params, void* object)
{
    if (!script) {
        if (script::Script* old = slot)
            script::Script::destroy(old);
        slot = nullptr;
        return;
    }
    if (!slot)
        slot = script::Script::create();
    if (!system->script_args_)
        system->script_args_ = spawn_param_table();
    system->script_args_->append_structure_if_any(params);
    slot->bind_script(script, system->script_args_, static_cast<objects::Entity*>(object));
}

} // namespace

// 0x00497790
void EmitterInstance::set_emit_script(uint32_t script, script::Struct* params, void* object)
{
    bind_script(this, emit_script_ptr_, script, params, object);
}

// 0x00497900
void EmitterInstance::set_update_script(uint32_t script, script::Struct* params, void* object)
{
    bind_script(this, tick_script_, script, params, object);
}

// 0x00497A70
void EmitterInstance::set_wants_time(uint8_t on)
{
    if (on)
        flag_bits_ |= 0x10000u;
    else
        flag_bits_ &= ~0x10000u;
    reinterpret_cast<uint8_t&>(wants_time_) = on;
}

// 0x00497AB0
maths::Mat4* EmitterInstance::build_emitter_orientation(maths::Mat4* out)
{
    const maths::Vec4 angles{orientation_turn_[0], orientation_turn_[1], orientation_turn_[2], 1.0f};
    maths::Mat4 rotation{};
    rotation.init_from_angles(angles);
    *out = rotation;
    return out;
}

// 0x00496890
int EmitterInstance::get_num_colours()
{
    debug::printf("STUB: plat_get_num_particle_colors\n");
    return 0;
}

// 0x004968A0
int EmitterInstance::get_num_vertex_lists()
{
    debug::printf("STUB: plat_get_num_vertex_lists\n");
    return 0;
}

// --- the types ---

// 0x00499010
NewFlatParticles::NewFlatParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float, int)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * 12, false, 1)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// 0x004992C0
FlatRibbonParticles::FlatRibbonParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float,
                                         int history)
    : segments_(history + 1)
{
    // (the colours' count read again as segments_ + 3 for each array and each step of the loop)
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * segments_ * 12), true,
                        segments_ + 3)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// 0x00499200
FlatRibbonParticles::~FlatRibbonParticles()
{
    if (particle_list_) {
        delete_particles(particle_list_);
        particle_list_ = nullptr;
    }
}

// 0x004991E0
int FlatRibbonParticles::get_num_colours() { return segments_ + 3; }

// 0x004995B0
GlowParticles::GlowParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int segments, float glow_width,
                             int)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * 12, false, 3)) {
        segments_ = segments;
        colour_mid_ = -1.0f;
        glow_width_ = glow_width;
        flag_bits_ |= 4u;
    }
}

// 0x00499880
GlowRibbonTrailParticles::GlowRibbonTrailParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int,
                                                   int segments, float width, int history)
{
    history_length_ = history + 1;
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>((history + 1) * particle_cap * 12), true,
                        history_length_ + 3)) {
        segments_ = segments;
        colour_mid_ = -1.0f;
        span_ = width;
        flag_bits_ |= 4u;
    }
}

// 0x00499820 / 0x00499830
int GlowRibbonTrailParticles::get_num_colours() { return history_length_ + 3; }
int GlowRibbonTrailParticles::get_num_vertex_lists() { return history_length_; }

// 0x00499B80
LineParticles::LineParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float, int history)
    : steps_(history + 1)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * (history + 1) * 12), true,
                        steps_)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// 0x00499E10
ModelParticles::~ModelParticles()
{
    if (unknown_13c_ptr_) {
        memory::dealloc(unknown_13c_ptr_);
        unknown_13c_ptr_ = nullptr;
    }
    if (model_list_) {
        for (unsigned int i = 0; i < model_count_; ++i)
            delete model_list_[i]; // (through 0x0048C3C0)
        memory::dealloc(static_cast<void*>(model_list_));
        model_list_ = nullptr;
    }
}

} // namespace render
