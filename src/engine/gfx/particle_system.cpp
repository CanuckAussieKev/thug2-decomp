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
#include <array>
#include <span>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/model.h"
#include "engine/gfx/viewport.h"
#include "engine/maths/mat4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

constexpr float kNearlyUp = 0.99f;        // 0x00645450
constexpr float kFramesPerSecond = 60.0f; // 0x00645EA0

void* particle_hub_handle() { return g_flexible_particle_manager.get(); }

gfx::ViewCam* viewport_camera(int viewport) { return fetch_live_camera(viewport); }
// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The allocations of the types' constructors: the manager's, its assert flag the global byte 0x00658555.
void* carve(unsigned int size) { return memory::Hub::get_pull()->carve(size, kAllocAssertFlag, nullptr); }
void* allocate_zeroed(unsigned int size)
{
    return memory::Hub::get_pull()->allocate_zeroed(size, kAllocAssertFlag, nullptr);
}

} // namespace

// (shared with the other types' constructors: particle_system_internal.h)
namespace particle_parts {

// The particles: `count` after their count (the compiler's array new; their constructor and destructor are empty).
std::span<FlexibleParticle> new_particles(int count)
{
    // (the original's array new kept the count before them; nothing reads it - their destructor is empty)
    auto* particles = static_cast<FlexibleParticle*>(carve(static_cast<unsigned int>(count) * sizeof(FlexibleParticle)));
    return particles ? std::span<FlexibleParticle>(particles, static_cast<std::size_t>(count)) : std::span<FlexibleParticle>();
}
void delete_particles(std::span<FlexibleParticle> particles)
{
    // (the empty destructor run over each first: 0x0062382B with 0x005480A0)
    memory::dealloc(particles.data());
}

// The three colour arrays, `count` RGBA colours each (the constructor iterator 0x00401020 runs an empty one over
// them), each allocated only if the one before was; all grey once the three are there. Whether they are.
bool new_colours(EmitterInstance* system, int count)
{
    const std::array<std::span<uint32_t>*, 3> colours = {{&system->start_colour_, &system->mid_colour_, &system->end_colour_}};
    for (std::span<uint32_t>* const colour : colours) {
        auto* words = static_cast<uint32_t*>(carve(static_cast<unsigned int>(count) * sizeof(uint32_t)));
        if (!words)
            return false;
        *colour = std::span<uint32_t>(words, static_cast<std::size_t>(count));
    }
    for (int i = 0; i < count; ++i)
        for (const std::span<uint32_t>* const colour : colours)
            core::at(*colour, i) = 0x80808080; // (each byte 0x80)
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
    if (system->particle_list_.empty())
        return false;
    auto* positions = static_cast<float*>(positions_zeroed ? allocate_zeroed(positions_size) : carve(positions_size));
    if (!positions)
        return false;
    system->position_array_ = std::span<float>(positions, positions_size / sizeof(float));
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
    at_point_ = maths::Vec4{.x = 2048.0f, .y = 144.0f, .z = -928.0f, .w = 0.0f};
    last_pos_ = maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    size_scale_ = 1.0f;
    core::at(emit_range_, 0) = 1.0f;
    core::at(emit_range_, 1) = 1.0f;
    core::at(emit_target_, 1) = 1.0f;
    core::at(speed_, 0) = 1.0f;
    brightness_ = 1.0f;
    core::at(emit_range_, 2) = 0.0f;
    angle_spread_ = 0.5f;
    core::at(force_, 0) = 0.0f;
    core::at(force_, 1) = -0.02f;
    core::at(force_, 2) = 0.0f;
    core::at(emit_target_, 0) = 0.0f;
    core::at(emit_target_, 2) = 0.0f;
    core::at(orientation_turn_, 0) = 0.0f;
    core::at(orientation_turn_, 1) = 0.0f;
    core::at(orientation_turn_, 2) = 0.0f;
    rank_ = 0;
    for (float& size : extent_)
        size = 2.0f;
    core::at(age_left_, 0) = 2.0f;
    core::at(age_left_, 1) = 2.0f;
    core::at(speed_, 1) = 2.0f;
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
    if (!start_colour_.empty())
        memory::dealloc(start_colour_.data());
    if (!mid_colour_.empty())
        memory::dealloc(mid_colour_.data());
    if (!end_colour_.empty())
        memory::dealloc(end_colour_.data());
    if (!position_array_.empty())
        memory::dealloc(position_array_.data());
    if (!particle_list_.empty())
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
    core::at(emit_range_, 0) = width;
    core::at(emit_range_, 1) = height;
    core::at(emit_range_, 2) = depth;
}

// 0x004968E0
void EmitterInstance::set_force(float x, float y, float z)
{
    core::at(force_, 0) = x;
    core::at(force_, 1) = y;
    core::at(force_, 2) = z;
}

// 0x00496910
void EmitterInstance::set_emit_target(float x, float y, float z)
{
    core::at(emit_target_, 0) = x;
    core::at(emit_target_, 1) = y;
    core::at(emit_target_, 2) = z;
}

// 0x00496940
void EmitterInstance::change_spin(float x, float y, float z)
{
    core::at(orientation_turn_, 0) = x;
    core::at(orientation_turn_, 1) = y;
    core::at(orientation_turn_, 2) = z;
}

// 0x00496970
void EmitterInstance::fetch_place(int particle, int step, float* x, float* y, float* z)
{
    const std::span<const float, 3> position = place_at(position_array_, max_particles_, step, particle);
    *x = core::at(position, 0);
    *y = core::at(position, 1);
    *z = core::at(position, 2);
}

// 0x004969B0
void EmitterInstance::change_place(int particle, int step, float x, float y, float z)
{
    const std::span<float, 3> position = place_at(position_array_, max_particles_, step, particle);
    core::at(position, 0) = x;
    core::at(position, 1) = y;
    core::at(position, 2) = z;
}

// 0x004969F0
void EmitterInstance::get_emit_matrix(maths::Mat4* out)
{
    if (xform_ref_) {
        *out = *xform_ref_;
        return;
    }
    core::at(out->row, 0) = maths::Vec4{.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(out->row, 1) = maths::Vec4{.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(out->row, 2) = maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(out->row, 3) = maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::Vec4& at = core::at(out->row, 2);
    at = maths::Vec4{.x = core::at(emit_target_, 0), .y = core::at(emit_target_, 1), .z = core::at(emit_target_, 2), .w = 1.0f};
    at.make_unit();
    // across: up x the target (along x when the target is nearly up); then the second row across both (x87, 53 bits)
    double up = 1.0, along = 0.0;
    if (std::fabs(at.y) > kNearlyUp) {
        up = 0.0;
        along = 1.0;
    }
    maths::Vec4& across = core::at(out->row, 0);
    across.w = 0.0f;
    auto z = static_cast<float>(along * at.y - up * at.x);
    across.x = static_cast<float>(up * at.z - static_cast<double>(at.y) * 0.0);
    across.y = static_cast<float>(static_cast<double>(at.x) * 0.0 - along * at.z);
    across.z = z;
    maths::Vec4& second = core::at(out->row, 1);
    second.w = 0.0f;
    auto second_z = static_cast<float>(static_cast<double>(at.y) * across.x - static_cast<double>(at.x) * across.y);
    second.x = static_cast<float>(static_cast<double>(across.y) * at.z - static_cast<double>(across.z) * at.y);
    second.y = static_cast<float>(static_cast<double>(across.z) * at.x - static_cast<double>(across.x) * at.z);
    second.z = second_z;
}

// 0x00496B40
int EmitterInstance::lod_particle_count(int count)
{
    if (lod_near_ >= lod_far_ || modules::viewport_count() > 1) // (NaN distances go on)
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
    float force = std::fabs(core::at(force_, 2));
    if (std::fabs(core::at(force_, 1)) > force)
        force = std::fabs(core::at(force_, 1));
    if (std::fabs(core::at(force_, 0)) > force)
        force = std::fabs(core::at(force_, 0));
    const double travel = static_cast<double>(force) * core::at(age_left_, 1) + core::at(speed_, 1);
    bounds_ = at_point_;
    float range = core::at(emit_range_, 2);
    if (core::at(emit_range_, 1) > range)
        range = core::at(emit_range_, 1);
    if (core::at(emit_range_, 0) > range)
        range = core::at(emit_range_, 0);
    bounds_.w = static_cast<float>(travel * core::at(age_left_, 1) * kFramesPerSecond + range);
}

// 0x00496CE0
void EmitterInstance::set_colour_scale(std::span<const float, 4> rgba)
{
    if (flag_bits_ & 0x10000) {
        for (int i = 0; i < 4; ++i)
            core::at(colour_scale_, i) = core::at(rgba, i);
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
        system->script_args_ = script::new_struct();
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
    wants_time_ = on != 0;
}

// 0x00497AB0
maths::Mat4* EmitterInstance::build_emitter_orientation(maths::Mat4* out)
{
    const maths::Vec4 angles{.x = core::at(orientation_turn_, 0), .y = core::at(orientation_turn_, 1), .z = core::at(orientation_turn_, 2), .w = 1.0f};
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
    if (!particle_list_.empty()) {
        delete_particles(particle_list_);
        particle_list_ = {};
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
        for (unsigned int i = 0; i < model_count_; ++i) {
            gsl::owner<RenderModel*> model = memory::owned_element(core::at(std::span<RenderModel*>(model_list_, model_count_), i)); // (the list's)
            delete model; // (through 0x0048C3C0)
        }
        memory::dealloc(static_cast<void*>(model_list_));
        model_list_ = nullptr;
    }
}

} // namespace render
