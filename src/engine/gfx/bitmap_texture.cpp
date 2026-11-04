// Textures and the render target list: see bitmap_texture.h.

#include "engine/gfx/bitmap_texture.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <span>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/renderer.h"
#include "engine/maths/rect.h"

namespace render {

constexpr int kDxt1BlockBytes = 8; // a row of 4x4 blocks: the width in pixels times this (the other formats use 16)
constexpr int kSamplerStageCount = 8;

namespace {

IDirect3DTexture9* get_d3d(const TextureRef* texture_ref) { return texture_ref->texture; }

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
void* cached_texture(int stage) { return renderer().stage_texture(stage); }

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
    IDirect3DTexture9* from = get_d3d(source);
    const DWORD level_count = from->GetLevelCount();
    D3DSURFACE_DESC desc;
    from->GetLevelDesc(0, &desc);
    renderer().d3d_device()->CreateTexture(desc.Width, desc.Height, level_count, 0, desc.Format, D3DPOOL_MANAGED,
                                &texture, nullptr);
    for (uint32_t level = 0; level < levels; ++level) {
        D3DSURFACE_DESC level_desc;
        get_d3d(source)->GetLevelDesc(level, &level_desc);
        // Rows of texels (or of 4x4 blocks for compressed formats: 8 bytes a block for DXT1, 16 for the others).
        uint32_t rows = level_desc.Height;
        uint32_t row_bytes = 0;
        if (level_desc.Format == D3DFMT_A8R8G8B8) {
            row_bytes = level_desc.Width * 4;
        } else {
            rows = (rows + 3) >> 2;
            row_bytes = level_desc.Format == D3DFMT_DXT1 ? level_desc.Width * kDxt1BlockBytes : level_desc.Width << 4;
        }
        D3DLOCKED_RECT source_rect;
        D3DLOCKED_RECT target_rect;
        get_d3d(source)->LockRect(level, &source_rect, nullptr, 0);
        get_d3d(this)->LockRect(level, &target_rect, nullptr, 0);
        const auto source_pitch = static_cast<size_t>(source_rect.Pitch);
        const auto target_pitch = static_cast<size_t>(target_rect.Pitch);
        const std::span<const uint8_t> from8(static_cast<const uint8_t*>(source_rect.pBits), size_t{rows} * source_pitch);
        const std::span<uint8_t> to(static_cast<uint8_t*>(target_rect.pBits), size_t{rows} * target_pitch);
        for (size_t row = 0; row < rows; ++row) {
            std::memcpy(to.subspan(row * target_pitch).data(), from8.subspan(row * source_pitch).data(), row_bytes);
        }
        get_d3d(source)->UnlockRect(level);
        get_d3d(this)->UnlockRect(level);
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
        surface->Release();
    }
    if (texture) {
        get_d3d(this)->Release();
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
    D3DLOCKED_RECT rect;
    if (get_d3d(this)->LockRect(0, &rect, nullptr, 0) != D3D_OK) {
        return nullptr;
    }
    return rect.pBits;
}

// 0x004E03D0
void TextureRef::get_unlock() const { get_d3d(this)->UnlockRect(0); }

// 0x004E03E0
TextureRef* TextureRef::palette_owner() { return palette ? this : nullptr; }

// 0x004E0700
bool TextureRef::create_render_target(maths::PixelSize size, uint8_t new_depth, int /*unused*/)
{
    const auto new_width = size.width;
    const auto new_height = size.height;
    if (texture) {
        get_d3d(this)->Release();
    }
    const HRESULT result =
        renderer().d3d_device()->CreateTexture(new_width, new_height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                    &texture, nullptr);
    get_d3d(this)->GetSurfaceLevel(0, &surface);
    render_target_ = true;
    if (result != D3D_OK) {
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
    for (DWORD stage = 0; stage < kSamplerStageCount; ++stage) {
        renderer().d3d_device()->SetTexture(stage, nullptr); // (the cache isn't told)
    }
    for (const Elem* node = foremost(); node; node = node->next) {
        TextureRef* texture_ref = node->texture;
        if (!texture_ref->texture || !texture_ref->render_target_) {
            continue;
        }
        get_d3d(texture_ref)->Release();
        texture_ref->texture = nullptr;
        if (texture_ref->surface) {
            texture_ref->surface->Release();
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
        renderer().d3d_device()->CreateTexture(texture_ref->width, texture_ref->height, 1, D3DUSAGE_RENDERTARGET,
                                               D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture_ref->texture, nullptr);
        if (texture_ref->texture) {
            get_d3d(texture_ref)->GetSurfaceLevel(0, &texture_ref->surface);
        }
    }
}

} // namespace render
