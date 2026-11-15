// NOT ORIGINAL - the surface trails' window on the device (vulkan_surface_trail.h).
#include "gpu_vulkan/vulkan_surface_trail.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <optional>
#include <span>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/surface_trail.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_ring.h"

namespace render::vulkan {

namespace {

constexpr VkDeviceSize kStagingAlignment = 16;
constexpr std::size_t kTexelBytes = 4;
constexpr std::size_t kTileBytes = surface_trail::kTileTexelCount * kTexelBytes;

int slot_of(int value, int count) { return ((value % count) + count) % count; }

// A row-vector 4 x 4 matrix (D3D's layout) inverted (Gauss-Jordan in double precision; a singular one: the identity).
using Matrix = std::array<std::array<float, 4>, 4>;

Matrix inverted(const Matrix& matrix)
{
    std::array<std::array<double, 8>, 4> work{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(core::at(work, row), column) = static_cast<double>(core::at(core::at(matrix, row), column));
        }
        core::at(core::at(work, row), 4 + row) = 1.0;
    }
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row) {
            if (std::abs(core::at(core::at(work, row), column)) > std::abs(core::at(core::at(work, pivot), column))) {
                pivot = row;
            }
        }
        constexpr double kSingular = 1.0e-12;
        if (std::abs(core::at(core::at(work, pivot), column)) < kSingular) {
            Matrix identity{};
            for (std::size_t k = 0; k < 4; ++k) {
                core::at(core::at(identity, k), k) = 1.0F;
            }
            return identity;
        }
        std::swap(core::at(work, pivot), core::at(work, column));
        const double scale = 1.0 / core::at(core::at(work, column), column);
        for (double& value : core::at(work, column)) {
            value *= scale;
        }
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = core::at(core::at(work, row), column);
            for (std::size_t k = 0; k < 8; ++k) {
                core::at(core::at(work, row), k) -= factor * core::at(core::at(work, column), k);
            }
        }
    }
    Matrix out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(core::at(out, row), column) = static_cast<float>(core::at(core::at(work, row), 4 + column));
        }
    }
    return out;
}


} // namespace

Float4x4 trail_eye_to_world(const std::array<std::array<float, 4>, 4>& view)
{
    const Matrix inverse = inverted(view);
    Float4x4 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(out, (row * 4) + column) = core::at(core::at(inverse, row), column);
        }
    }
    return out;
}

bool ensure_trail_image(Backend& state, Image* image, VkFormat format, Extent extent, VkImageUsageFlags usage)
{
    if (image->image != VK_NULL_HANDLE && image->format == format && image->extent.width == extent.width &&
        image->extent.height == extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = usage,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0) {
        image->resting = VK_IMAGE_LAYOUT_GENERAL;
    }
    prepare_image(state, image);
    return true;
}

bool copy_into_image(Backend& state, Image* image, const std::vector<uint8_t>& bytes, std::span<VkBufferImageCopy> regions)
{
    if (regions.empty()) {
        return true;
    }
    const std::optional<RingSpan> staged = state.staging.copy(bytes, kStagingAlignment);
    if (!staged) {
        return false;
    }
    for (VkBufferImageCopy& region : regions) {
        region.bufferOffset += staged->offset;
    }
    VkCommandBuffer commands = outside_pass(state);
    const VkImageLayout resting = image->resting;
    Memory::transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(commands, staged->buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
    Memory::transition(commands, image, resting);
    image->used = state.frames.serial();
    return true;
}

VkBufferImageCopy image_region(VkDeviceSize offset, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    return {.bufferOffset = offset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
            .imageOffset = {.x = x, .y = y, .z = 0},
            .imageExtent = {.width = width, .height = height, .depth = 1}};
}

bool update_trail_window(Backend& state, TrailWindow* window, const surface_trail::Field& field, const surface_trail::Point& centre,
                         int tiles, std::span<Image* const> images, std::vector<std::array<int, 2>>* moved)
{
    if (tiles <= 0 || images.size() < field.planes()) {
        return false;
    }
    if (window->tiles != tiles || window->texel != field.texel() || window->field != field.serial()) {
        window->tiles = tiles;
        window->texel = field.texel();
        window->field = field.serial();
        window->slots.assign(static_cast<std::size_t>(tiles) * static_cast<std::size_t>(tiles), TrailSlot{});
    }
    const float tile_units = field.texel() * static_cast<float>(surface_trail::kTileTexels);
    const int centre_x = static_cast<int>(std::floor(core::at(centre, 0) / tile_units));
    const int centre_z = static_cast<int>(std::floor(core::at(centre, 1) / tile_units));
    window->first = {centre_x - (tiles / 2), centre_z - (tiles / 2)};
    const std::size_t planes = field.planes();
    std::vector<std::vector<uint8_t>> bytes(planes);
    std::vector<std::vector<VkBufferImageCopy>> regions(planes);
    const auto column = static_cast<int32_t>(window->first_column);
    for (int dz = 0; dz < tiles; ++dz) {
        for (int dx = 0; dx < tiles; ++dx) {
            const int x = core::at(window->first, 0) + dx;
            const int z = core::at(window->first, 1) + dz;
            const int sx = slot_of(x, tiles);
            const int sz = slot_of(z, tiles);
            TrailSlot& slot =
                core::at(window->slots, (static_cast<std::size_t>(sz) * static_cast<std::size_t>(tiles)) + static_cast<std::size_t>(sx));
            const surface_trail::Tile* tile = field.tile(x, z);
            const bool same_place = slot.known && slot.x == x && slot.z == z;
            if (same_place && ((tile == nullptr && !slot.filled) || (tile != nullptr && slot.filled && slot.version == tile->version))) {
                continue;
            }
            if (!same_place && moved != nullptr) {
                moved->push_back({sx, sz});
            }
            if (tile == nullptr && slot.known && !slot.filled) {
                slot = {.filled = false, .known = true, .x = x, .z = z, .version = 0};
                continue; // (zeros there already)
            }
            for (std::size_t plane = 0; plane < planes; ++plane) {
                std::vector<uint8_t>& out = core::at(bytes, plane);
                const VkDeviceSize offset = out.size();
                out.resize(out.size() + kTileBytes);
                if (tile != nullptr) {
                    const std::span<const uint32_t> words = tile->plane(plane);
                    std::memcpy(std::span(out).subspan(static_cast<std::size_t>(offset), kTileBytes).data(), words.data(), kTileBytes);
                }
                core::at(regions, plane).push_back(image_region(offset, column + (sx * surface_trail::kTileTexels),
                                                                sz * surface_trail::kTileTexels, surface_trail::kTileTexels,
                                                                surface_trail::kTileTexels));
            }
            if (tile != nullptr) {
                ++window->tiles_copied;
            } else {
                ++window->slots_cleared;
            }
            slot = {.filled = tile != nullptr, .known = true, .x = x, .z = z, .version = tile != nullptr ? tile->version : 0};
        }
    }
    for (std::size_t plane = 0; plane < planes; ++plane) {
        if (!copy_into_image(state, core::at(images, plane), core::at(bytes, plane), core::at(regions, plane))) {
            return false;
        }
    }
    return true;
}

} // namespace render::vulkan
