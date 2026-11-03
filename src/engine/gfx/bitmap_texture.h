#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include "platform/d3d_forward.h"
#include "engine/maths/rect.h"
#include <gsl/pointers>

// A texture (Nx engine, PC): its Direct3D texture and, for render targets, the surface. Every texture is on a list
// (0x007CC254) so the render targets can be released and made again around a device reset.
namespace render {

// A texture's image: its sizes and Direct3D objects (what a material's pass draws with). Copied as it is where the
// original copied the record (the grass's snapshot of the sky's texture).
struct TextureImage {
    uint32_t checksum{};
    uint16_t width{}, height{};               // the texture's
    uint16_t image_width_{}, image_height_{}; // the image's, at its top left
    uint8_t levels{};
    uint8_t depth{};
    uint8_t unknowne_{}, unknownf_{};
    IDirect3DTexture9* texture{nullptr};
    IDirect3DTexture9* palette{nullptr}; // (or an alpha texture: GetPaletteOwner)
    IDirect3DSurface9* surface{nullptr}; // a render target's level 0
    bool render_target_{false};
};

// The texture itself: an image on the textures' list, its Direct3D objects released with it.
struct TextureRef : TextureImage {

    // 0x004E04B0: construct (no texture) and go on the list.
    TextureRef();
    // 0x004E0500: construct a copy of `source` - a managed texture of its size, format and levels, every level's
    // texels copied. Returns this.
    TextureRef* construct_copy(const TextureRef* source);
    // 0x004E06A0: the destructor's body: off the list, the surface and texture released, and unset where set.
    ~TextureRef();
    TextureRef(const TextureRef&) = delete;
    TextureRef& operator=(const TextureRef&) = delete;
    // 0x004E03F0: off the list.
    void unlist();
    // 0x004E0380: set on a stage.
    void assign_value(int stage);
    // 0x004E03A0: level 0 locked: its texels, or null.
    void* get_lock();
    // 0x004E03D0: level 0 unlocked.
    void get_unlock();
    // 0x004E03E0: this if it has the second texture (+0x14), else null.
    TextureRef* palette_owner();
    // The palette's entries (ARGB) the texture editing reads and changes for an image of 8 bits or less: none - no
    // PC texture keeps its palette (the second texture is never set: palette_owner is always null, and the original
    // read the entries through it, from null). An empty span: a read of one stops the game with a report.
    [[nodiscard]] std::span<uint32_t> palette_entries() const { return {}; }
    // 0x004E0700: make it a width x height A8R8G8B8 render target (`depth` kept). Returns whether it was made.
    bool create_render_target(maths::PixelSize size, uint8_t depth, int unused);
};
constexpr std::size_t kTextureRefTextureOffset = 0x10;
constexpr std::size_t kTextureRefRenderTargetOffset = 0x1C;
constexpr std::size_t kTextureRefBytes = 0x20;

static_assert(offsetof(TextureRef, texture) == kTextureRefTextureOffset && offsetof(TextureRef, render_target_) == kTextureRefRenderTargetOffset &&
                  sizeof(TextureRef) == kTextureRefBytes,
              "layout must match the original");

// 0x004E0790: a texture from an image file (opened "rb"), or null if there's no such file (or, not original, its
// image doesn't fit its texture: image_format.h).
gsl::owner<TextureRef*> read_texture_file(const char* name);
// 0x004E0A90: a texture from an image in memory, or null if its Direct3D texture couldn't be made. (Not original:
// `data_size` - the image's bytes, 0 if not known - bounds its reader; an image that doesn't fit its texture is null.)
gsl::owner<TextureRef*> load_texture_memory(const uint8_t* data, size_t data_size);

// 0x004E0320: before a device reset - every stage's texture unset, the render target textures released.
void release_render_target_textures();
// 0x004E0440: after it - the render target textures made again.
void restore_render_target_textures();

} // namespace render
