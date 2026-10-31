#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/bitmap_texture.h"
#include "engine/gfx/texture_dict.h"

namespace lists {
class KeyMap;
}

// The PC texture and texture dictionary (Nx engine: the engine's BitmapTexture - constructor 0x0049F200, destructor body
// 0x0049F210 - and TextureSet - constructor 0x0049F830, destructor body 0x0049FC70 - over them), the image helpers
// their editing uses (a sub-image copy, RGB <-> HSV) and the platform's dictionary makers.
namespace render {

// A texture being edited (16 bytes): its texture, a copy of the pixels as they were (or null), the pixels (32-bit,
// rows top down) and a texture of its own kept with them (or null).
struct TextureEdit {
    TextureRef* texture;
    uint32_t* original;
    uint32_t* pixels;
    TextureRef* kept;
};

// The PC texture (0x14 bytes, vtable 0x0064A770).
struct PcTexture {
    void** vtable;
    uint32_t checksum; // +0x04 (the engine's)
    uint32_t flags;    // +0x08: 1 being edited (`engine` is an STextureEdit*)
    uint8_t unknownc_; // +0x0C
    uint8_t unknownd_; // +0x0D
    uint8_t pade_[2];
    void* engine; // +0x10: the TextureRef, or the edit holding it

    TextureRef* get_texture() const;

    // 0x004C7ED0: construct (no texture). Returns this.
    PcTexture* make_new();
    // 0x004C7EF0: its texture (and checksum, the texture's).
    void set_texture(TextureRef* texture);
    // 0x004C8450: construct a copy of `source`: its texture copied, and an edit's copy of the pixels and kept texture
    // (never its pixels: the copy's own, still null, are tested for them, as the original). Returns this.
    PcTexture* construct_copy(const PcTexture* source);
    // 0x004C9D00: the destructor's body: an edit ended, the texture destroyed.
    void destroy_body();
    // 0x004CAE60 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    PcTexture* teardown(unsigned int free_flags);
    // 0x004C7F00 (virtual [1]): loaded from "<name>.img.xbx". Whether it was.
    bool ingest_file(const char* name, int unused1, int unused2);
    // 0x004C7F80 (virtual [2]): loaded from an image in memory. Whether it was.
    bool load_memory(const uint8_t* data, int size, int unused1, int unused2, int unused3);
    // 0x004C8830 (virtual [3]): a new texture of the size (one level per the device: the levels' 0), P8 for up to
    // 8 bits a pixel, X1R5G5B5 for 16, X8R8G8B8 for 32 (and below 8); the old one's Direct3D texture released. The
    // texture's size from the level's. True.
    bool fabricate(uint32_t checksum, uint32_t width, uint32_t height, int bits_per_pixel, int unused1, int unused2);
    // 0x004C86A0 (virtual [4]): its Direct3D texture made again as a copy of `source`'s, every level (exit() if it
    // can't be made). True.
    bool copy_from(const PcTexture* source);
    // 0x004C8A60 (virtual [8]): an edit begun: level 0's pixels read (32 bits only, bottom row first), a copy of them
    // kept and a texture of them made when asked. True.
    bool begin_edit(bool keep_texture, bool keep_copy);
    // 0x004C9EC0 (virtual [9]): the edit's pixels written back into level 0, bottom row first.
    bool write_back(int unused1, int unused2);
    // 0x004C98B0 (virtual [10]): the edit ended: its buffers and kept texture freed, the texture back in place.
    bool end_edit();
    // 0x004C97D0: the palette entry nearest a colour (alpha's difference counted twice).
    uint8_t nearest_palette_index(uint32_t colour);
    // 0x004C9950 (virtual [12]): the edit's pixels tinted by an RGBA colour (x / 128, alpha too), from the copy
    // unless `cumulative`; the palette too for up to 8 bits a pixel, unless `cumulative`. True.
    bool modulate(uint32_t colour, bool cumulative);
    // 0x004C9D80 (virtual [15]): the edit's pixels resized from another edit's (both edits: else false).
    bool resize_from(const PcTexture* source, int unused);
    // 0x004C9DE0 (virtual [16]): the edit's pixels zoomed about the centre. True.
    bool get_zoom(float x_zoom, float y_zoom);
    // 0x004C8D30 (virtual [17]): the edit's pixels turned by `angle` about a point (below 0: the middle). True.
    bool spin_by(float angle, float centre_x, float centre_y);
    // 0x004CABE0 (virtual [18]): the edit's pixels moved; what's uncovered filled from the nearest edge unless
    // `keep_edges`. True.
    bool get_offset(int dx, int dy, bool keep_edges, int unused);
    // 0x004CADC0 (virtual [19]): a one-pixel border of the colour round the edit's pixels. True.
    bool get_border(uint32_t colour);
    // 0x004CA220 (virtual [21]): the part one side of column / row `position` stretched by `amount`. True.
    bool get_stretch(uint16_t position, int vertical, int amount);
    // 0x004CA410 (virtual [22]): the part one side squashed by `amount`, the space left filled with the colour or
    // its edge. True.
    bool squash(uint16_t position, int vertical, int amount, bool fill, uint32_t colour);
    // 0x004CA7B0 (virtual [13]): the edit's pixels mirrored left to right. True.
    bool get_mirror();
    // 0x004CAA20 (virtual [24]): the edit's pixels' hue turned, saturation and value scaled (up to 1), from the copy
    // unless `cumulative`. True.
    bool shift_hue_sat_value(float hue, float saturation, float value, bool cumulative);
    // 0x004CA8E0 (virtual [41]): another texture's level 0 drawn over the edit's pixels by its alpha. False.
    bool get_overlay(PcTexture* over, int unused);
    // 0x004C9F90: a rectangle of the edit's pixels read out (a palettized texture's whole image instead).
    void read_rect(uint32_t* out, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    // 0x004CA0D0: a rectangle written in (a palettized texture's by nearest palette entry).
    void write_rect(const uint32_t* in, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    // 0x004C7FB0 / 0x004C7FE0 (virtual [33] / [34]): the image's width and height (0 without a texture).
    uint16_t fetch_breadth();
    uint16_t fetch_tallness();
    // 0x004C8010 / 0x004C8030 (virtual [35] / [38]): its bits per pixel and its +0x0E (0 without one).
    uint8_t get_depth();
    uint8_t get_unknown_e();
    // 0x004C8050 (virtual [39], folded with other classes' getters of a byte at +0x0C) / 0x004C8060 (virtual [40]).
    uint8_t get_unknown_c();
    uint8_t get_unknown_d();
};
static_assert(offsetof(PcTexture, flags) == 8 && offsetof(PcTexture, engine) == 0x10 && sizeof(PcTexture) == 0x14,
              "layout must match the original");

// The PC texture dictionary (0x14 bytes, vtable 0x0064A824).
class PcTexDict final : public TextureSet {
public:
    // 0x004C8070: the engine's constructor with this checksum and a table.
    explicit PcTexDict(uint32_t checksum) : TextureSet(checksum, true) {}
    // 0x004CB760: the engine's constructor with the name and a table, then the file loaded.
    explicit PcTexDict(const char* name);
    // 0x004CB380 / 0x004CB7C0: the textures of a .tex file / image in memory added - the table remade to fit when it
    // has none yet. True.
    bool ingest_file(const char* name);
    bool load_memory(const uint8_t* data);
    // 0x004C9AF0 (0x004C9F60 the scalar deleting destructor): a jump to the engine's.
    ~PcTexDict() override = default;
    // 0x004C9B00 (vtable slot 1): a new texture loaded from a file (the engine's 0x0049F220 with 1). Returns it.
    BitmapTexture* load_texture_file(const char* name, uint32_t a1, uint32_t a2) override;
    // 0x004C89B0 (slot 2): a new texture read from a block of memory (the engine's 0x0049F300 with 1). Returns it.
    BitmapTexture* load_texture_data(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4,
                                     uint32_t a6) override;
    // 0x004C9BA0 (slot 3): the dictionary's texture of this checksum, or a new one, made (the engine's 0x0049F330).
    // Returns it.
    BitmapTexture* make_texture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel,
                                uint32_t a4, uint32_t a5) override;
    // 0x004C8090 (slot 5): a texture destroyed (through its vtable). True.
    bool destroy_texture(BitmapTexture* texture) override;
    // 0x004C9C80 (slot 8): a new texture copied from `source` (0x004C8450) under `checksum`. Returns it (the
    // original leaves it in EAX, and the engine's CopyTexture hands it on - the skater's graffiti texture is one).
    BitmapTexture* duplicate_texture(uint32_t checksum, BitmapTexture* source) override;

    uint32_t unknown10_; // +0x10
};
static_assert(sizeof(PcTexDict) == 0x14, "layout must match the original");

// 0x004CAE90 / 0x004CB420: a .tex file's / image's textures (each: checksum, size, levels, depth, palette depth,
// compression, palette size, then its levels) made and put in `table` - remade to fit first with `remake` (in the
// heap the old one was in). Returns the table. A palettized texture becomes an A8R8G8B8 one with its texels not
// filled (its palette and levels read and dropped, as the original); the memory loader reads no palette and
// leaves the cursor where it was.
lists::KeyMap* __cdecl load_textures_from_file(const char* name, lists::KeyMap* table, bool remake);
lists::KeyMap* __cdecl load_textures_from_memory(const uint8_t** cursor, lists::KeyMap* table, bool remake);

// 0x004C7270: a dictionary loaded from a file. 0x004C72E0: one made with a checksum and loaded from memory (called
// on null without memory, as the original). 0x004C7360: an empty one.
PcTexDict* __cdecl load_tex_dict_file(const char* name);
PcTexDict* __cdecl load_tex_dict_memory(uint32_t checksum, const uint8_t* data);
PcTexDict* __cdecl create_tex_dict(uint32_t checksum);

// 0x004C80B0: an image's rows copied into another's at (x, y), clipped to it (`bits_per_pixel` 4 copies half bytes).
void __cdecl copy_sub_image(uint8_t* into, uint16_t x, uint16_t y, uint16_t into_width, uint16_t into_height,
                            const uint8_t* from, uint16_t from_width, uint16_t from_height, uint16_t bits_per_pixel);
// 0x004C9200: an image resized - 8 bits a pixel by nearest (the bytes cleared first: as many as the source has),
// else 32 bits bilinear in 1024ths.
void __cdecl resize_image(const uint8_t* from, uint16_t from_width, uint16_t from_height, uint8_t* into,
                          uint16_t into_width, uint16_t into_height, uint16_t bits_per_pixel);
// 0x004C9520: a 32-bit image zoomed about its centre (bilinear, clamped to the edges).
void __cdecl zoom_image(const uint8_t* from, uint16_t width, uint16_t height, uint8_t* into, float x_zoom,
                        float y_zoom);
// 0x004C8180: RGB (0-1) to hue (degrees; -1 when black), saturation and value.
void __cdecl rgb_to_hsv(float r, float g, float b, float* h, float* s, float* v);
// 0x004C82D0: and back.
void __cdecl hsv_to_rgb(float* r, float* g, float* b, float h, float s, float v);

} // namespace render
