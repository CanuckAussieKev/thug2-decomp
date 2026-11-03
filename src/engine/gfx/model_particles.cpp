// The flexible particle type that draws models (render::ModelParticles, the type 0x286A8D26): its constructor and Render
// (see particle_system.h). From the disassembly of 0x0049A0C0-0x0049A4B3 and 0x0049A4C0-0x0049A8DD.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/packed_colour.h"
#include "engine/core/str.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_system_internal.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/maths/vec4.h"
#include <gsl/pointers>

namespace render {

constexpr std::size_t kParticleFileNameSize = 512;
constexpr int kPositionBytes = 12;          // three floats a model
constexpr double kScaleSpan = 2.0;          // the scale runs from 1 to 3 over the random percent
constexpr int kTurnRange = 100;
constexpr int kBitsPerByte = 8;
constexpr int kBitsPerTwoBytes = 16;
constexpr int kBitsPerThreeBytes = 24;

namespace {

constexpr unsigned int kMemoryPerModel = 0x1194; // (what's needed free for each model)
constexpr float kScaleStep = 0.01f;              // 0x0064544C
constexpr float kSizeStep = 0.25f;               // 0x006467AC
const char* const kModelExtension = ".mdl";      // ".mdl"

uint8_t assert_flag() { return kAllocAssertFlag; }
void* carve(unsigned int size) { return memory::Hub::get_pull()->carve(size, assert_flag(), nullptr); }
void* allocate_zeroed(unsigned int size)
{
    return memory::Hub::get_pull()->allocate_zeroed(size, assert_flag(), nullptr);
}
// `count` words of type T at what the allocator gave (none if it gave nothing).
template <typename T>
std::span<T> counted_words(void* memory, std::size_t count)
{
    return memory ? std::span<T>(static_cast<T*>(memory), count) : std::span<T>();
}
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)
gsl::owner<RenderModel*> spawn_render_model(int /*unused*/) { return render::spawn_model(); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void model_path_for(const char* name, const char* ext, char* out) { gfx::make_model_file_name(name, ext, out); }

} // namespace

// 0x0049A0C0
ModelParticles::ModelParticles(const ConstructArgs& args)
    : model_count_(static_cast<unsigned int>(args.particle_cap_)), drawn_models_(static_cast<unsigned int>(args.particle_cap_))
{
    const uint32_t name = args.name;
    const int particle_cap = args.particle_cap_;
    const char* model = args.model;
    set_label(name);
    std::array<char, kParticleFileNameSize> file_name = {};
    position_array() = {};
    particle_list() = {};
    set_particle_count(0);
    set_max_particles(particle_cap);
    particle_list() = particle_parts::new_particles(particle_cap);
    if (particle_list().empty())
        return;
    position_array() = counted_words<float>(carve(model_count_ * kPositionBytes), model_count_ * 3);
    if (position_array().empty())
        return;
    model_path_for(model, kModelExtension, file_name.data());
    text::to_lower_text(file_name.data());
    model_list_ = static_cast<RenderModel**>(allocate_zeroed(model_count_ * 4));
    if (!model_list_)
        return;
    if (memory::malloc_available() < model_count_ * kMemoryPerModel)
        return;
    const std::span<RenderModel*> models(model_list_, model_count_);
    for (unsigned int i = 0; i < model_count_; ++i) {
        gsl::owner<RenderModel*> made = spawn_render_model(assert_flag()); // (the system's list owns them)
        core::at(models, i) = made;
        RenderModel* each = core::at(models, i);
        if (!each)
            return;
        each->drop_geoms();
        const int percent = get_rand() % 100;
        auto scale = static_cast<float>((static_cast<double>(percent) * kScaleStep * kScaleSpan) + 1.0);
        const maths::Vec4 scales = {.x = scale, .y = scale, .z = scale, .w = 1.0f};
        each->change_scale(&scales);
        if (!each->add_file_geom(file_name.data(), 0, true, 0, false, true, assert_flag() != 0, true))
            return;
    }
    auto* rotations = static_cast<maths::QuatRotation*>(carve(model_count_ * sizeof(maths::QuatRotation)));
    const std::span<maths::QuatRotation> turns(rotations, rotations ? model_count_ : 0);
    for (maths::QuatRotation& turn : turns)
        turn = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}; // (the constructor 0x00464340)
    unknown_13c_ptr_ = rotations;
    if (!rotations)
        return;
    start_colour() = counted_words<uint32_t>(carve(4), 1);
    if (start_colour().empty())
        return;
    mid_colour() = counted_words<uint32_t>(carve(4), 1);
    if (mid_colour().empty())
        return;
    end_colour() = counted_words<uint32_t>(carve(4), 1);
    if (end_colour().empty())
        return;
    for (unsigned int i = 0; i < model_count_; ++i) {
        maths::QuatRotation& turn = core::at(turns, i);
        turn.x = static_cast<float>(get_rand() % kTurnRange);
        turn.y = static_cast<float>(get_rand() % kTurnRange);
        turn.z = static_cast<float>(get_rand() % kTurnRange);
        turn.w = static_cast<float>(get_rand() % kTurnRange);
        static_cast<maths::QuatRotation*>(unknown_13c_ptr_)->make_unit(1.0f); // (the first, each time: the original's)
    }
    set_colour_mid(-1.0f);
    flag_bits() |= 4u;
}

// 0x0049A4C0
void ModelParticles::render()
{
    const std::span<RenderModel*> models(model_list_, model_count_);
    auto model = [models](unsigned int i) { return core::at(models, i); };
    const int count = particle_count();
    if (count == 0) {
        for (unsigned int i = 0; i < drawn_models_; ++i)
            model(i)->change_live(0);
        return;
    }
    // (from the last particle's model: it's shown again below)
    for (auto i = static_cast<unsigned int>(count - 1); i < drawn_models_; ++i)
        model(i)->change_live(0);
    const std::span<maths::QuatRotation> rotations(static_cast<maths::QuatRotation*>(unknown_13c_ptr_), model_count_);
    for (int p = 0; p < count; ++p) {
        const FlexibleParticle& particle = core::at(particle_list(), p);
        model(static_cast<unsigned int>(p))->change_live(1);
        float life = particle.age / particle.lifetime;
        std::span<const uint32_t> from;
        std::span<const uint32_t> to;
        if (!(colour_mid() >= 0.0f)) { // (no mid colour; NaN too)
            from = start_colour();
            to = end_colour();
        } else if (life < colour_mid()) {
            life = static_cast<float>(static_cast<double>(life) / colour_mid());
            from = start_colour();
            to = mid_colour();
        } else {
            life = static_cast<float>((static_cast<double>(life) - colour_mid()) /
                                      (1.0 - static_cast<double>(colour_mid())));
            from = mid_colour();
            to = end_colour();
        }
        uint32_t colour = 0;
        colour = *lerp_colour(&colour, colour::bytes_of(core::at(from, 0)), colour::bytes_of(core::at(to, 0)), life);
        model(static_cast<unsigned int>(p))
            ->change_tint(static_cast<uint8_t>(colour), static_cast<uint8_t>(colour >> kBitsPerByte),
                          static_cast<uint8_t>(colour >> kBitsPerTwoBytes), static_cast<uint8_t>(colour >> kBitsPerThreeBytes));
        // the rotation moved by the velocity (w by its x too), then normalised
        maths::QuatRotation& q = core::at(rotations, p);
        q.x = static_cast<float>(static_cast<double>(core::at(particle.velocity, 0)) + q.x);
        q.y = static_cast<float>(static_cast<double>(core::at(particle.velocity, 1)) + q.y);
        q.z = static_cast<float>(static_cast<double>(core::at(particle.velocity, 2)) + q.z);
        q.w = static_cast<float>(static_cast<double>(core::at(particle.velocity, 0)) + q.w);
        const double x = q.x;
        const double y = q.y;
        const double z = q.z;
        const double w = q.w;
        const double length = std::sqrt((((x * x) + (y * y)) + (z * z)) + (w * w));
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            q.x = static_cast<float>(inverse * q.x);
            q.y = static_cast<float>(inverse * q.y);
            q.z = static_cast<float>(inverse * q.z);
            q.w = static_cast<float>(inverse * q.w);
        }
        maths::Mat4 matrix;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                core::at(matrix.row, r)[c] = r == c ? 1.0f : 0.0f;
        q.fetch_mat(&matrix);
        const std::span<const float, 3> position = place_at(position_array(), max_particles(), 0, p);
        core::at(matrix.row, 3) = {.x = core::at(position, 0), .y = core::at(position, 1), .z = core::at(position, 2), .w = 1.0f};
        const double shrink = static_cast<double>(p % 4) * kSizeStep;
        const double size = static_cast<double>(core::at(particle.start_size_, 0)) - shrink;
        for (int k = 0; k < 3; ++k)
            core::at(matrix.row, k)[k] = static_cast<float>(size * core::at(matrix.row, k)[k]);
        model(static_cast<unsigned int>(p))->render(&matrix, true, nullptr);
    }
}

} // namespace render
