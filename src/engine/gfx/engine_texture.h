#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/memory/mem.h"
#include "base/core/checksums.h"
#include "base/maths/vec2.h"

// The engine's texture (render::BitmapTexture; the PC texture PcTexture - pc_texture.h - extends it), and the folded
// dispatchers the engine's classes call their virtual slots through.
namespace render {

// BitmapTexture::flag_bits_
constexpr uint32_t kTexturePermanent =
    0x10000; // kept when its dictionary's textures are flushed (drop_textures without `all`)

constexpr std::size_t kBitmapTextureFlagBitsOffset = 8;
constexpr std::size_t kBitmapTextureBytes = 0xC;

// The texture (0xC bytes of the engine's; vtable 0x00649AF0 - its own slots are stubs; PC 0x0064A770). The slots
// that are the same stub in both tables are plain methods here (14, 20, 23, 25 - 30, 32: false / 0 / nothing);
// those nothing calls (5 - 7, 11, 31, 36, 37) are left out.
class BitmapTexture : public memory::HeapObject {
public:
    // 0x0049F200: the upper half of the flags cleared (the PC constructor clears the rest).
    BitmapTexture() = default;
    // 0x0049F210 (0x0049F4D0 the scalar deleting destructor, vtable slot 0): nothing.
    virtual ~BitmapTexture() = default;
    BitmapTexture(const BitmapTexture&) = delete;
    BitmapTexture& operator=(const BitmapTexture&) = delete;

    // 0x0049F220: loaded from an image file (ingest_file) - the name as given, or with `in_images` (its low byte)
    // under "images/" (a "../" taken off instead); named by the CRC of the name after its last slash. Whether it was.
    bool load_by_name(const char* name, int in_images, int argument);
    // 0x0049F300: loaded from an image in memory (load_memory with the other arguments), named `checksum`. Whether it
    // was.
    bool read_from_ram_block(const uint8_t* data, uint32_t size, checksums::Name texture_name, uint32_t a3, uint32_t a4,
                             uint32_t a5);
    // 0x0049F330 (not a function in Ghidra): made (create_image with all the arguments), named `checksum`. Whether it
    // was.
    bool fabricate(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel, uint32_t a4,
                   uint32_t a5);
    // 0x0049F3F0: kept or not when its dictionary is flushed.
    void pin_resident(bool permanent);

    // A stretch or squash of a texture: where, along which axis (1: vertical), by how much.
    struct Stretch {
        uint16_t position{};
        int vertical{};
        int amount{};
    };

    // A shift of hue (degrees), saturation and value.
    struct HsvShift {
        float hue{};
        float saturation{};
        float value{};
    };

    // The platform's virtual functions (vtable slots 1 - 41; the engine's bodies are shared stubs: false / 0). The
    // editing ones work on an edit begun with begin_edit (32-bit pixels) and written back with write_back.
    virtual bool ingest_file(const char* /*unused*/, int /*unused*/, int /*unused*/) { return false; } // [1]
    virtual bool load_memory(const uint8_t* /*unused*/, uint32_t /*unused*/, uint32_t /*unused*/, uint32_t /*unused*/,
                             uint32_t /*unused*/)
    {
        return false;
    } // [2]
    // An image's width, height and bits per pixel.
    struct ImageFormat {
        uint32_t width{};
        uint32_t height{};
        int bits_per_pixel_{};
    };
    virtual bool create_image(checksums::Name /*unused*/, ImageFormat /*unused*/, int /*unused*/, int /*unused*/)
    {
        return false;
    }                                                                                                  // [3]
    virtual bool copy_from(const BitmapTexture* /*unused*/) { return false; }                          // [4]
    virtual bool begin_edit(bool /*unused*/, bool /*unused*/) { return false; }                        // [8]
    virtual bool write_back(int /*unused*/, const void* /*unused*/) { return false; }                  // [9]
    virtual bool end_edit() { return false; }                                                          // [10]
    virtual bool modulate(uint32_t /*unused*/, bool /*unused*/) { return false; }                      // [12]
    virtual bool get_mirror() { return false; }                                                        // [13]
    virtual bool resize_from(const BitmapTexture* /*unused*/, int /*unused*/) { return false; }        // [15]
    virtual bool get_zoom(float /*unused*/, float /*unused*/) { return false; }                        // [16]
    virtual bool spin_by(float /*unused*/, maths::Vec2 /*centre*/) { return false; }                   // [17]
    virtual bool get_offset(int /*unused*/, int /*unused*/, bool /*unused*/, int /*unused*/) { return false; } // [18]
    virtual bool get_border(uint32_t /*unused*/) { return false; }                                             // [19]
    virtual bool get_stretch(Stretch /*stretch*/) { return false; }                                     // [21]
    virtual bool squash(BitmapTexture::Stretch /*stretch*/, bool /*unused*/, uint32_t /*unused*/)
    {
        return false;
    }                                                                                       // [22]
    virtual bool shift_hue_sat_value(HsvShift /*shift*/, bool /*unused*/) { return false; } // [24]
    [[nodiscard]] virtual uint16_t fetch_breadth() const { return 0; }  // [33] the image's width
    [[nodiscard]] virtual uint16_t fetch_tallness() const { return 0; } // [34] its height
    virtual uint8_t get_depth() { return 0; }                       // [35] bits per pixel
    virtual uint8_t get_unknown_e() { return 0; }                   // [38]
    virtual uint8_t get_unknown_c() { return 0; }                   // [39]
    virtual uint8_t get_unknown_d() { return 0; }                   // [40]
    virtual bool get_overlay(BitmapTexture* /*unused*/, int /*unused*/) { return false; } // [41]

    // The slots the same stub in both tables (the PC has none of these).
    static bool flip_vertically() { return false; }                                // [14] (FlipTexture's vert)
    static bool adjust_region(int /*unused*/, int /*unused*/, int /*unused*/, int /*unused*/, int /*unused*/,
                              int /*unused*/, int /*unused*/)
    {
        return false;
    }                                                                                  // [20] (AdjustTextureRegion)
    static bool adjust_brightness(float /*unused*/, bool /*unused*/) { return false; } // [23] (AdjustTextureBrightness)
    static void* get_pixels() { return nullptr; }                                  // [25]
    static uint32_t get_pixels_size() { return 0; }                                // [26]
    static void* get_palette() { return nullptr; }                                 // [27]
    static uint32_t get_palette_size() { return 0; }                               // [28]
    void set_palette(const void* /*unused*/, int /*unused*/) {}                    // [29]
    static bool set_pixels(const void* /*unused*/) { return false; }               // [30]
    static bool release_pixels() { return false; }                          // [32]

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t checksum() const { return checksum_; }
    void set_checksum(uint32_t value) { checksum_ = value; }
    uint32_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint32_t flag_bits() const { return flag_bits_; }

private:
    uint32_t checksum_ = 0;  // +0x04
    uint32_t flag_bits_ = 0; // +0x08 (the platform's low half: PcTexture's)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(BitmapTexture, flag_bits_) == kBitmapTextureFlagBitsOffset, "layout must match the original");
        static_assert(offsetof(BitmapTexture, checksum_) == 4 && sizeof(BitmapTexture) == kBitmapTextureBytes,
                      "layout must match the original");
    }
};

// A texture's image size.
inline uint16_t texels_across(void* texture) { return static_cast<BitmapTexture*>(texture)->fetch_breadth(); }
inline uint16_t texels_down(void* texture) { return static_cast<BitmapTexture*>(texture)->fetch_tallness(); }

} // namespace render
