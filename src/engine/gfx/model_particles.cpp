// The flexible particle type that draws models (render::ModelParticles, the type 0x286A8D26): its constructor and Render
// (see particle_system.h). From the disassembly of 0x0049A0C0-0x0049A4B3 and 0x0049A4C0-0x0049A8DD.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_system_internal.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "platform/crt.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

constexpr unsigned int kMemoryPerModel = 0x1194; // (what's needed free for each model)
constexpr float kScaleStep = 0.01f;              // 0x0064544C
constexpr float kSizeStep = 0.25f;               // 0x006467AC
const char* const kModelExtension = ".mdl";      // ".mdl"

uint8_t assert_flag() { return g_alloc_assert_flag; }
void* carve(unsigned int size) { return memory::Hub::get_pull()->carve(size, assert_flag(), nullptr); }
void* allocate_zeroed(unsigned int size)
{
    return memory::Hub::get_pull()->allocate_zeroed(size, assert_flag(), nullptr);
}
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)
RenderModel* spawn_render_model(int) { return render::spawn_model(); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void model_path_for(const char* name, const char* ext, char* out) { gfx::make_model_file_name(name, ext, out); }

} // namespace

// 0x0049A0C0
ModelParticles::ModelParticles(uint32_t name, int particle_cap, const char* model, uint32_t, int, int, float, int)
    : model_count_(static_cast<unsigned int>(particle_cap)), drawn_models_(static_cast<unsigned int>(particle_cap))
{
    label_ = name;
    std::array<char, 512> file_name = {};
    position_array_ = nullptr;
    particle_list_ = nullptr;
    particle_count_ = 0;
    max_particles_ = particle_cap;
    particle_list_ = particle_parts::new_particles(particle_cap);
    if (!particle_list_)
        return;
    position_array_ = static_cast<float*>(carve(model_count_ * 12));
    if (!position_array_)
        return;
    model_path_for(model, kModelExtension, file_name.data());
    text::to_lower_text(file_name.data());
    model_list_ = static_cast<RenderModel**>(allocate_zeroed(model_count_ * 4));
    if (!model_list_)
        return;
    if (memory::malloc_available() < model_count_ * kMemoryPerModel)
        return;
    for (unsigned int i = 0; i < model_count_; ++i) {
        model_list_[i] = spawn_render_model(assert_flag());
        RenderModel* each = model_list_[i];
        if (!each)
            return;
        each->drop_geoms();
        const int percent = get_rand() % 100;
        auto scale = static_cast<float>(static_cast<double>(percent) * kScaleStep * 2.0 + 1.0);
        const maths::Vec4 scales = {scale, scale, scale, 1.0f};
        each->change_scale(&scales);
        if (!each->add_file_geom(file_name.data(), 0, true, 0, false, true, assert_flag() != 0, true))
            return;
    }
    auto* rotations = static_cast<maths::QuatRotation*>(carve(model_count_ * sizeof(maths::QuatRotation)));
    if (rotations) {
        for (unsigned int i = 0; i < model_count_; ++i)
            rotations[i] = {0.0f, 0.0f, 0.0f, 1.0f}; // (the constructor 0x00464340)
    }
    unknown_13c_ptr_ = rotations;
    if (!rotations)
        return;
    start_colour_ = static_cast<uint8_t*>(carve(4));
    if (!start_colour_)
        return;
    mid_colour_ = static_cast<uint8_t*>(carve(4));
    if (!mid_colour_)
        return;
    end_colour_ = static_cast<uint8_t*>(carve(4));
    if (!end_colour_)
        return;
    for (unsigned int i = 0; i < model_count_; ++i) {
        rotations[i].x = static_cast<float>(get_rand() % 100);
        rotations[i].y = static_cast<float>(get_rand() % 100);
        rotations[i].z = static_cast<float>(get_rand() % 100);
        rotations[i].w = static_cast<float>(get_rand() % 100);
        static_cast<maths::QuatRotation*>(unknown_13c_ptr_)->make_unit(1.0f); // (the first, each time: the original's)
    }
    colour_mid_ = -1.0f;
    flag_bits_ |= 4u;
    return;
}

// 0x0049A4C0
void ModelParticles::render()
{
    auto model = [this](unsigned int i) { return model_list_[i]; };
    const int count = particle_count_;
    if (count == 0) {
        for (unsigned int i = 0; i < drawn_models_; ++i)
            model(i)->change_live(0);
        return;
    }
    // (from the last particle's model: it's shown again below)
    for (auto i = static_cast<unsigned int>(count - 1); i < drawn_models_; ++i)
        model(i)->change_live(0);
    auto* rotations = static_cast<maths::QuatRotation*>(unknown_13c_ptr_);
    for (int p = 0; p < count; ++p) {
        const FlexibleParticle& particle = particle_list_[p];
        model(static_cast<unsigned int>(p))->change_live(1);
        float life = particle.age / particle.lifetime;
        const uint8_t* from = nullptr;
        const uint8_t* to = nullptr;
        if (!(colour_mid_ >= 0.0f)) { // (no mid colour; NaN too)
            from = start_colour_;
            to = end_colour_;
        } else if (life < colour_mid_) {
            life = static_cast<float>(static_cast<double>(life) / colour_mid_);
            from = start_colour_;
            to = mid_colour_;
        } else {
            life = static_cast<float>((static_cast<double>(life) - colour_mid_) /
                                      (1.0 - static_cast<double>(colour_mid_)));
            from = mid_colour_;
            to = end_colour_;
        }
        uint32_t from_colour = 0, to_colour = 0, colour = 0;
        std::memcpy(&from_colour, from, 4);
        std::memcpy(&to_colour, to, 4);
        colour = *lerp_colour(&colour, raw::bytes(&from_colour),
                              raw::bytes(&to_colour), life);
        model(static_cast<unsigned int>(p))
            ->change_tint(static_cast<uint8_t>(colour), static_cast<uint8_t>(colour >> 8),
                          static_cast<uint8_t>(colour >> 16), static_cast<uint8_t>(colour >> 24));
        // the rotation moved by the velocity (w by its x too), then normalised
        maths::QuatRotation& q = rotations[p];
        q.x = static_cast<float>(static_cast<double>(core::at(particle.velocity, 0)) + q.x);
        q.y = static_cast<float>(static_cast<double>(core::at(particle.velocity, 1)) + q.y);
        q.z = static_cast<float>(static_cast<double>(core::at(particle.velocity, 2)) + q.z);
        q.w = static_cast<float>(static_cast<double>(core::at(particle.velocity, 0)) + q.w);
        const double x = q.x;
        const double y = q.y;
        const double z = q.z;
        const double w = q.w;
        const double length = std::sqrt(((x * x + y * y) + z * z) + w * w);
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
                matrix.row[r][c] = r == c ? 1.0f : 0.0f;
        q.fetch_mat(&matrix);
        const float* position = position_array_ + p * 3;
        core::at(matrix.row, 3) = {position[0], position[1], position[2], 1.0f};
        const double shrink = static_cast<double>(p % 4) * kSizeStep;
        const double size = static_cast<double>(core::at(particle.start_size_, 0)) - shrink;
        for (int k = 0; k < 3; ++k)
            matrix.row[k][k] = static_cast<float>(size * matrix.row[k][k]);
        model(static_cast<unsigned int>(p))->render(&matrix, true, nullptr);
    }
}

} // namespace render
