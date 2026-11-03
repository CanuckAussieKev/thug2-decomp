#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/bitmap_texture.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/texture_dict.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec2.h"
#include <gsl/pointers>
#include <array>

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
// The engine texture's image (a PC texture's TextureRef, or the edit's while it's edited).
TextureRef* image_of(const BitmapTexture* texture);

constexpr std::size_t kPcTextureImageOffset = 0x10;
constexpr std::size_t kPcTextureBytes = 0x14;

class PcTexture final : public BitmapTexture {
public:
    PcTexture(const PcTexture&) = delete;
    PcTexture& operator=(const PcTexture&) = delete;
    // 0x004C7ED0: no texture.
    PcTexture() = default;
    // 0x004C8450: a copy of `source`: its texture copied, and an edit's copy of the pixels and kept texture (never its
    // pixels: the copy's own, still null, are tested for them, as the original).
    explicit PcTexture(const PcTexture* source);
    // 0x004C9D00 (0x004CAE60 the scalar deleting destructor): an edit ended, the texture destroyed.
    ~PcTexture() override;

    [[nodiscard]] TextureRef* get_texture() const;

    // 0x004C7EF0: its texture (and checksum, the texture's).
    void set_texture(TextureRef* texture);
    // 0x004C7F00 (vtable slot 1): loaded from "<name>.img.xbx". Whether it was.
    bool ingest_file(const char* name, int unused1, int unused2) override;
    // 0x004C7F80 (slot 2): loaded from an image in memory. Whether it was.
    bool load_memory(const uint8_t* data, uint32_t size, uint32_t, uint32_t, uint32_t) override;
    // 0x004C8830 (slot 3): a new texture of the size (one level per the device: the levels' 0), P8 for up to 8 bits a
    // pixel, X1R5G5B5 for 16, X8R8G8B8 for 32 (and below 8); the old one's Direct3D texture released. The texture's
    // size from the level's. True.
    bool create_image(checksums::Name texture_name, ImageFormat image_format, int unused1,
                      int unused2) override;
    // 0x004C86A0 (slot 4): its Direct3D texture made again as a copy of `source`'s (a PC texture), every level (exit()
    // if it can't be made). True.
    bool copy_from(const BitmapTexture* source) override;
    // 0x004C8A60 (slot 8): an edit begun: level 0's pixels read (32 bits only, bottom row first), a copy of them kept
    // and a texture of them made when asked. True.
    bool begin_edit(bool keep_texture, bool keep_copy) override;
    // 0x004C9EC0 (slot 9): the edit's pixels written back into level 0, bottom row first.
    bool write_back(int unused1, const void* unused2) override;
    // 0x004C98B0 (slot 10): the edit ended: its buffers and kept texture freed, the texture back in place.
    bool end_edit() override;
    // 0x004C97D0: the palette entry nearest a colour (alpha's difference counted twice).
    uint8_t nearest_palette_index(uint32_t colour);
    // 0x004C9950 (slot 12): the edit's pixels tinted by an RGBA colour (x / 128, alpha too), from the copy unless
    // `cumulative`; the palette too for up to 8 bits a pixel, unless `cumulative`. True.
    bool modulate(uint32_t colour, bool cumulative) override;
    // 0x004C9D80 (slot 15): the edit's pixels resized from another edit's (both edits: else false).
    bool resize_from(const BitmapTexture* source, int unused) override;
    // 0x004C9DE0 (slot 16): the edit's pixels zoomed about the centre. True.
    bool get_zoom(float x_zoom, float y_zoom) override;
    // 0x004C8D30 (slot 17): the edit's pixels turned by `angle` about a point (below 0: the middle). True.
    bool spin_by(float angle, maths::Vec2 centre) override;
    // 0x004CABE0 (slot 18): the edit's pixels moved; what's uncovered filled from the nearest edge unless
    // `keep_edges`. True.
    bool get_offset(int dx, int dy, bool keep_edges, int unused) override;
    // 0x004CADC0 (slot 19): a one-pixel border of the colour round the edit's pixels. True.
    bool get_border(uint32_t colour) override;
    // 0x004CA220 (slot 21): the part one side of column / row `position` stretched by `amount`. True.
    bool get_stretch(Stretch stretch) override;
    // 0x004CA410 (slot 22): the part one side squashed by `amount`, the space left filled with the colour or its
    // edge. True.
    bool squash(BitmapTexture::Stretch stretch, bool fill, uint32_t colour) override;
    // 0x004CA7B0 (slot 13): the edit's pixels mirrored left to right. True.
    bool get_mirror() override;
    // 0x004CAA20 (slot 24): the edit's pixels' hue turned, saturation and value scaled (up to 1), from the copy
    // unless `cumulative`. True.
    bool shift_hue_sat_value(HsvShift shift, bool cumulative) override;
    // 0x004CA8E0 (slot 41): another (PC) texture's level 0 drawn over the edit's pixels by its alpha. False.
    bool get_overlay(BitmapTexture* over, int unused) override;
    // An area of a texture: its top left and its size in texels.
    struct TexelArea {
        uint16_t x{};
        uint16_t y{};
        uint16_t width{};
        uint16_t height{};
    };

    // 0x004C9F90: a rectangle of the edit's pixels read out (a palettized texture's whole image instead).
    void read_rect(std::span<uint32_t> out, TexelArea area);
    // 0x004CA0D0: a rectangle written in (a palettized texture's by nearest palette entry).
    void write_rect(std::span<const uint32_t> in, TexelArea area);
    // 0x004C7FB0 / 0x004C7FE0 (slots 33 / 34): the image's width and height (0 without a texture).
    [[nodiscard]] uint16_t fetch_breadth() const override;
    [[nodiscard]] uint16_t fetch_tallness() const override;
    // 0x004C8010 / 0x004C8030 (slots 35 / 38): its bits per pixel and its +0x0E (0 without one).
    uint8_t get_depth() override;
    uint8_t get_unknown_e() override;
    // 0x004C8050 (slot 39, folded with other classes' getters of a byte at +0x0C) / 0x004C8060 (slot 40).
    uint8_t get_unknown_c() override;
    uint8_t get_unknown_d() override;

    // The fields other code reads and sets.
    [[nodiscard]] void* image() const { return image_; }
    void set_image(void* value) { image_ = value; }

private:
    uint8_t unknownc_ = 0; // +0x0C
    uint8_t unknownd_ = 0; // +0x0D
    std::array<uint8_t, 2> pade_{};
    void* image_ = nullptr; // +0x10: the TextureRef, or the edit holding it (flag_bits_ & 1: a TextureEdit)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PcTexture, image_) == kPcTextureImageOffset && sizeof(PcTexture) == kPcTextureBytes,
                      "layout must match the original");
    }
};

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
    // (not original: `size` - the image's bytes, the bound for its reader)
    bool load_memory(const uint8_t* data, size_t size);
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
    gsl::owner<BitmapTexture*> duplicate_texture(uint32_t checksum, BitmapTexture* source) override;

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t unknown10() const { return unknown10_; }

private:
    uint32_t unknown10_{}; // +0x10
};
constexpr std::size_t kPcTexDictBytes = 0x14;

static_assert(sizeof(PcTexDict) == kPcTexDictBytes, "layout must match the original");

// 0x004CAE90 / 0x004CB420: a .tex file's / image's textures (each: checksum, size, levels, depth, palette depth,
// compression, palette size, then its levels) made and put in `table` - remade to fit first with `remake` (in the
// heap the old one was in). Returns the table. A palettized texture becomes an A8R8G8B8 one with its texels not
// filled (its palette and levels read and dropped, as the original); the memory loader reads no palette and
// leaves the cursor where it was.
lists::KeyMap* load_textures_from_file(const char* name, lists::KeyMap* table, bool remake);
// (Not original: read through texture_format.h's reader - the memory loader bounded by `size`; a level larger than
// its surface, or a count / level past the end, stops the dictionary there instead of writing / reading past.)
lists::KeyMap* load_textures_from_memory(const uint8_t** cursor, size_t size, lists::KeyMap* table, bool remake);

// 0x004C7270: a dictionary loaded from a file. 0x004C72E0: one made with a checksum and loaded from memory (called
// on null without memory, as the original). 0x004C7360: an empty one.
gsl::owner<PcTexDict*> load_tex_dict_file(const char* name);
gsl::owner<PcTexDict*> load_tex_dict_memory(uint32_t checksum, const uint8_t* data, size_t size);
gsl::owner<PcTexDict*> create_tex_dict(uint32_t checksum);

// An image's width and height in texels.
struct ImageSize16 {
    uint16_t width{};
    uint16_t height{};
};

// The texture editing's image helpers, on 32-bit pixels (every caller's; the original took the bits a pixel and also
// copied 8- and 4-bit images and resized 8-bit ones by nearest - nothing asked for those).
// 0x004C80B0: an image's rows copied into another's at (x, y), clipped to it.
void copy_sub_image(std::span<uint32_t> into, uint16_t x, uint16_t y, ImageSize16 into_size,
                    std::span<const uint32_t> from, ImageSize16 from_size);
// 0x004C9200: an image resized, bilinear in 1024ths.
void resize_image(std::span<const uint32_t> from, ImageSize16 from_size, std::span<uint32_t> into, ImageSize16 into_size);
// 0x004C9520: an image zoomed about its centre (bilinear, clamped to the edges).
void zoom_image(std::span<const uint32_t> from, uint16_t width, uint16_t height, std::span<uint32_t> into, float x_zoom,
                float y_zoom);
// 0x004C8180: RGB (0-1) to hue (degrees; -1 when black), saturation and value.
void rgb_to_hsv(const std::array<float, 3>& rgb, std::array<float, 3>* hsv);
// 0x004C82D0: and back.
void hsv_to_rgb(const std::array<float, 3>& hsv, std::array<float, 3>* rgb);

} // namespace render
