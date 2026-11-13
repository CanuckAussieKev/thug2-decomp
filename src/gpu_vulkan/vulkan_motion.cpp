// NOT ORIGINAL - motion vectors for the upscaler (vulkan_motion.h; docs/design/vulkan.md "Upscaling").
#include "gpu_vulkan/vulkan_motion.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"

namespace render::vulkan {

namespace {

constexpr std::size_t kWorldSlot = 0; // (GpuStateBlock::transforms: world, view, projection)
constexpr std::size_t kViewSlot = 1;
constexpr std::size_t kProjectionSlot = 2;
constexpr std::size_t kSide = 4;
// How close the sky's view's rotation is to the camera's to be taken as the same (it is made from the same frame by
// another sum: not bit for bit), and how near the origin its translation is.
constexpr float kRotationTolerance = 1.0e-4F;
constexpr float kOriginTolerance = 1.0e-3F;
constexpr double kSingular = 1.0e-30; // (a pivot this small: no inverse)

float& cell(Float4x4& matrix, std::size_t row, std::size_t column) { return core::at(matrix, (row * kSide) + column); }
float cell(const Float4x4& matrix, std::size_t row, std::size_t column)
{
    return core::at(matrix, (row * kSide) + column);
}

// The view's rotation alone (its translation row zeroed).
Float4x4 rotation_of(const Float4x4& view)
{
    Float4x4 rotation = view;
    for (std::size_t column = 0; column < 3; ++column) {
        cell(rotation, 3, column) = 0.0F;
    }
    return rotation;
}

// Whether `view` is `camera`'s rotation drawn from the origin (the sky's view).
bool is_rotation_of(const Float4x4& view, const Float4x4& camera)
{
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            if (std::fabs(cell(view, row, column) - cell(camera, row, column)) > kRotationTolerance) {
                return false;
            }
        }
    }
    for (std::size_t column = 0; column < 3; ++column) {
        if (std::fabs(cell(view, 3, column)) > kOriginTolerance) {
            return false;
        }
    }
    return true;
}

// The projection with this frame's jitter taken out (rt::jitter_projection's inverse).
Float4x4 unjittered(const Float4x4& projection, const Float4& jitter)
{
    Float4x4 result = projection;
    for (std::size_t row = 0; row < kSide; ++row) {
        cell(result, row, 0) -= core::at(jitter, 0) * cell(result, row, 3);
        cell(result, row, 1) -= core::at(jitter, 1) * cell(result, row, 3);
    }
    return result;
}

} // namespace

Float4x4 motion_multiply(const Float4x4& a, const Float4x4& b)
{
    Float4x4 result{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < kSide; ++k) {
                sum += cell(a, row, k) * cell(b, k, column);
            }
            cell(result, row, column) = sum;
        }
    }
    return result;
}

bool motion_inverse(const Float4x4& matrix, Float4x4* inverse)
{
    // Gauss-Jordan with partial pivoting, in doubles
    std::array<std::array<double, 2 * kSide>, kSide> rows{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(rows, row), column) = cell(matrix, row, column);
        }
        core::at(core::at(rows, row), kSide + row) = 1.0;
    }
    for (std::size_t column = 0; column < kSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kSide; ++row) {
            if (std::fabs(core::at(core::at(rows, row), column)) > std::fabs(core::at(core::at(rows, pivot), column))) {
                pivot = row;
            }
        }
        const double lead = core::at(core::at(rows, pivot), column);
        if (std::fabs(lead) < kSingular) {
            return false;
        }
        std::swap(core::at(rows, pivot), core::at(rows, column));
        for (double& value : core::at(rows, column)) {
            value /= lead;
        }
        for (std::size_t row = 0; row < kSide; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = core::at(core::at(rows, row), column);
            for (std::size_t k = 0; k < 2 * kSide; ++k) {
                core::at(core::at(rows, row), k) -= factor * core::at(core::at(rows, column), k);
            }
        }
    }
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            cell(*inverse, row, column) = static_cast<float>(core::at(core::at(rows, row), kSide + column));
        }
    }
    return true;
}

bool motion_wanted(const Backend& state, const Surface* colour, const Image* colour_image)
{
    const MotionVectors& motion = state.motion;
    return motion.active && colour != nullptr && colour == motion.target && colour_image != nullptr &&
           colour_image->samples == VK_SAMPLE_COUNT_1_BIT && motion.image.image != VK_NULL_HANDLE &&
           motion.image.extent.width == colour_image->extent.width &&
           motion.image.extent.height == colour_image->extent.height;
}

void fill_motion_uniforms(Backend& state, const GpuStateBlock& block, VertexUniforms* uniforms)
{
    MotionVectors& motion = state.motion;
    const Float4x4& world = core::at(block.transforms, kWorldSlot);
    const Float4x4& view = core::at(block.transforms, kViewSlot);
    const Float4x4& projection = core::at(block.transforms, kProjectionSlot);
    // the world: the key's the frame before
    Float4x4 world_before = world;
    if (motion.source.key != 0) {
        motion.worlds_now[motion.source.key] = world;
        if (const auto found = motion.worlds_before.find(motion.source.key); found != motion.worlds_before.end()) {
            world_before = found->second;
        }
    }
    // the view and the projection: the camera's the frame before, for the camera's own and its rotation (the sky)
    Float4x4 view_before = view;
    Float4x4 projection_before = unjittered(projection, motion.jitter);
    if (motion.now.known && motion.before.known) {
        if (view == motion.now.view) {
            view_before = motion.before.view;
            if (projection_before == motion.now.projection) {
                projection_before = motion.before.projection;
            }
        } else if (is_rotation_of(view, motion.now.view)) {
            view_before = rotation_of(motion.before.view);
        }
    }
    uniforms->previous_clip = motion_multiply(motion_multiply(world_before, view_before), projection_before);
    uniforms->motion_jitter = motion.jitter;
    ++motion.draws;
}

void scale_pre_transformed(const Backend& state, VertexUniforms* uniforms)
{
    core::at(uniforms->pixel, 0) /= core::at(state.motion.output_scale, 0);
    core::at(uniforms->pixel, 1) /= core::at(state.motion.output_scale, 1);
}

std::pair<VkBuffer, VkDeviceSize> previous_vertices(Backend& state, VkBuffer buffer, VkDeviceSize offset)
{
    Buffer* const previous = state.motion.source.previous;
    if (previous == nullptr || !state.buffers.contains(previous) || previous->versions.empty()) {
        return {buffer, offset};
    }
    BufferVersion& version = core::at(previous->versions, previous->current);
    version.last_use = state.frames.serial();
    return {version.memory.buffer, state.motion.source.previous_offset};
}

} // namespace render::vulkan
