#pragma once

#include <cstddef>
#include <cstdint>

// A texture (Nx engine, PC): its Direct3D texture and, for render targets, the surface. Every texture is on a list
// (0x007CC254) so the render targets can be released and made again around a device reset.
namespace render {

struct TextureRef {
    uint32_t checksum;
    uint16_t width, height;               // the texture's
    uint16_t image_width_, image_height_; // the image's, at its top left
    uint8_t levels;
    uint8_t depth;
    uint8_t unknowne_, unknownf_;
    void* texture; // IDirect3DTexture9
    void* palette; // (or an alpha texture: GetPaletteOwner)
    void* surface; // a render target's level 0 (IDirect3DSurface9)
    bool render_target_;

    // 0x004E04B0: construct (no texture) and go on the list. Returns this.
    TextureRef* make_new();
    // 0x004E0500: construct a copy of `source` - a managed texture of its size, format and levels, every level's
    // texels copied. Returns this.
    TextureRef* construct_copy(const TextureRef* source);
    // 0x004E06A0: the destructor's body: off the list, the surface and texture released, and unset where set.
    void destroy_body();
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
    // 0x004E0700: make it a width x height A8R8G8B8 render target (`depth` kept). Returns whether it was made.
    bool create_render_target(int width, int height, uint8_t depth, int unused);
};
static_assert(offsetof(TextureRef, texture) == 0x10 && offsetof(TextureRef, render_target_) == 0x1C &&
                  sizeof(TextureRef) == 0x20,
              "layout must match the original");

// 0x004E0790: a texture from an image file (opened "rb"), or null if there's no such file.
TextureRef* __cdecl read_texture_file(const char* name);
// 0x004E0A90: a texture from an image in memory, or null if its Direct3D texture couldn't be made.
TextureRef* __cdecl load_texture_memory(const uint8_t* data);

// 0x004E0320: before a device reset - every stage's texture unset, the render target textures released.
void __cdecl release_render_target_textures();
// 0x004E0440: after it - the render target textures made again.
void __cdecl restore_render_target_textures();

} // namespace render
