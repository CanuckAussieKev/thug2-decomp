// Textures and the render target list: see bitmap_texture.h.

#include "render_d3d9/bitmap_texture.h"
#include "engine/gfx/gpu_handles.h"

#include <cstdint>
#include <cstring>
#include <span>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "render_d3d9/render_state.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "base/maths/rect.h"

namespace render {

constexpr int kDxt1BlockBytes = 8; // a row of 4x4 blocks: the width in pixels times this (the other formats use 16)
constexpr int kSamplerStageCount = 8;

namespace {


// The list (0x007CC254): doubly linked nodes.
struct Elem {
    TextureRef* texture;
    Elem* previous;
    Elem* next;
};
Elem*& foremost()
{
    static Elem* texture_list_head{};
    return texture_list_head;
}

// What the textures being drawn with are (the render state cache's).
GpuTexture* cached_texture(int stage) { return renderer().stage_texture(stage); }

} // namespace

// 0x004E04B0
TextureRef::TextureRef()
{
    auto* node = static_cast<Elem*>(memory::alloc(sizeof(Elem)));
    Elem* head = foremost();
    node->texture = this;
    node->previous = nullptr;
    node->next = head;
    foremost() = node;
    if (head) {
        head->previous = node;
    }
}

// 0x004E0500
TextureRef* TextureRef::construct_copy(const TextureRef* source)
{
    texture = nullptr;
    palette = nullptr;
    surface = nullptr;
    checksum = source->checksum;
    width = source->width;
    height = source->height;
    image_width_ = source->image_width_;
    image_height_ = source->image_height_;
    levels = source->levels;
    depth = source->depth;
    unknowne_ = source->unknowne_;
    unknownf_ = source->unknownf_;
    GpuTexture* from = source->texture;
    const uint32_t level_count = gpu().texture_level_count(from);
    GpuLevelDesc desc;
    gpu().texture_level_desc(from, 0, &desc);
    gpu().create_texture({.width = desc.width,
                          .height = desc.height,
                          .levels = level_count,
                          .usage = 0,
                          .format = desc.format,
                          .pool = gpu_value::kPoolManaged},
                         &texture);
    for (uint32_t level = 0; level < levels; ++level) {
        GpuLevelDesc level_desc;
        gpu().texture_level_desc(source->texture, level, &level_desc);
        // Rows of texels (or of 4x4 blocks for compressed formats: 8 bytes a block for DXT1, 16 for the others).
        uint32_t rows = level_desc.height;
        uint32_t row_bytes = 0;
        if (level_desc.format == gpu_value::kFormatA8R8G8B8) {
            row_bytes = level_desc.width * 4;
        } else {
            rows = (rows + 3) >> 2;
            row_bytes = level_desc.format == gpu_value::kFormatDxt1 ? level_desc.width * kDxt1BlockBytes : level_desc.width << 4;
        }
        GpuLockedRect source_rect;
        GpuLockedRect target_rect;
        gpu().lock_texture(source->texture, level, &source_rect, nullptr, 0);
        gpu().lock_texture(texture, level, &target_rect, nullptr, 0);
        const auto source_pitch = static_cast<size_t>(source_rect.pitch);
        const auto target_pitch = static_cast<size_t>(target_rect.pitch);
        const std::span<const uint8_t> from8(static_cast<const uint8_t*>(source_rect.bits), size_t{rows} * source_pitch);
        const std::span<uint8_t> to(static_cast<uint8_t*>(target_rect.bits), size_t{rows} * target_pitch);
        for (size_t row = 0; row < rows; ++row) {
            std::memcpy(to.subspan(row * target_pitch).data(), from8.subspan(row * source_pitch).data(), row_bytes);
        }
        gpu().unlock_texture(source->texture, level);
        gpu().unlock_texture(texture, level);
    }
    return this;
}

// 0x004E03F0
void TextureRef::unlist()
{
    Elem* node = foremost();
    for (; node; node = node->next) {
        if (node->texture == this) {
            break;
        }
    }
    if (!node) {
        return;
    }
    if (node->previous) {
        node->previous->next = node->next;
    } else {
        foremost() = node->next;
    }
    if (node->next) {
        node->next->previous = node->previous;
    }
    memory::Hub::dealloc(node);
}

// 0x004E06A0
TextureRef::~TextureRef()
{
    unlist();
    if (surface) {
        gpu().release_surface(surface);
    }
    if (texture) {
        gpu().release_texture(texture);
        for (int stage = 0; stage < 4; ++stage) {
            if (cached_texture(stage) == texture) {
                set_texture(stage, nullptr);
            }
        }
    }
}

// 0x004E0380
void TextureRef::assign_value(int stage)
{
    set_texture(stage, texture); // (the original also passes the second texture, unused)
}

// 0x004E03A0
void* TextureRef::get_lock() const
{
    GpuLockedRect rect;
    if (gpu().lock_texture(texture, 0, &rect, nullptr, 0) != gpu_value::kResultOk) {
        return nullptr;
    }
    return rect.bits;
}

// 0x004E03D0
void TextureRef::get_unlock() const { gpu().unlock_texture(texture, 0); }

// 0x004E03E0
TextureRef* TextureRef::palette_owner() { return palette ? this : nullptr; }

// 0x004E0700
bool TextureRef::create_render_target(maths::PixelSize size, uint8_t new_depth, int /*unused*/)
{
    const auto new_width = size.width;
    const auto new_height = size.height;
    if (texture) {
        gpu().release_texture(texture);
    }
    const long result = gpu().create_texture({.width = static_cast<uint32_t>(new_width),
                                              .height = static_cast<uint32_t>(new_height),
                                              .levels = 1,
                                              .usage = gpu_value::kUsageRenderTarget,
                                              .format = gpu_value::kFormatA8R8G8B8,
                                              .pool = gpu_value::kPoolDefault},
                                             &texture);
    gpu().texture_surface(texture, 0, &surface);
    render_target_ = true;
    if (result != gpu_value::kResultOk) {
        return false;
    }
    image_width_ = static_cast<uint16_t>(new_width);
    width = static_cast<uint16_t>(new_width);
    image_height_ = static_cast<uint16_t>(new_height);
    height = static_cast<uint16_t>(new_height);
    depth = new_depth;
    checksum = 0;
    levels = 1;
    unknowne_ = 0;
    unknownf_ = 0;
    return true;
}

// 0x004E0320
void release_render_target_textures()
{
    for (uint32_t stage = 0; stage < kSamplerStageCount; ++stage) {
        gpu().set_texture(stage, nullptr); // (the cache isn't told)
    }
    for (const Elem* node = foremost(); node; node = node->next) {
        TextureRef* texture_ref = node->texture;
        if (!texture_ref->texture || !texture_ref->render_target_) {
            continue;
        }
        gpu().release_texture(texture_ref->texture);
        texture_ref->texture = nullptr;
        if (texture_ref->surface) {
            gpu().release_surface(texture_ref->surface);
        }
        texture_ref->surface = nullptr;
    }
}

// 0x004E0440
void restore_render_target_textures()
{
    for (const Elem* node = foremost(); node; node = node->next) {
        TextureRef* texture_ref = node->texture;
        if (texture_ref->texture || !texture_ref->render_target_) {
            continue;
        }
        gpu().create_texture({.width = texture_ref->width,
                              .height = texture_ref->height,
                              .levels = 1,
                              .usage = gpu_value::kUsageRenderTarget,
                              .format = gpu_value::kFormatA8R8G8B8,
                              .pool = gpu_value::kPoolDefault},
                             &texture_ref->texture);
        if (texture_ref->texture) {
            gpu().texture_surface(texture_ref->texture, 0, &texture_ref->surface);
        }
    }
}

} // namespace render
