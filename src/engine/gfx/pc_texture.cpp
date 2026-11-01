// The PC texture and texture dictionary: see pc_texture.h. The colour conversions follow the original's x87 code:
// double precision where it keeps values in registers, floats where it stores them.

#include "engine/gfx/pc_texture.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/texture_dict.h"

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/lists/key_map.h"
#include "engine/maths/maths.h"
#include "engine/gfx/device.h"
#include "engine/gfx/texture_format.h"
#include "engine/util/byte_stream.h"
#include "platform/crt.h"

namespace render {

namespace {

TextureEdit* get_edit(const PcTexture* texture) { return static_cast<TextureEdit*>(texture->image_); }
IDirect3DDevice9* input_port() { return g_d3d_device; }

PcTexture* alloc_pc_texture() { return new PcTexture; }

} // namespace

TextureRef* PcTexture::get_texture() const
{
    return flag_bits_ & 1 ? get_edit(this)->texture : static_cast<TextureRef*>(image_);
}

// 0x004C7EF0
void PcTexture::set_texture(TextureRef* texture)
{
    image_ = texture;
    checksum_ = texture->checksum;
}

// 0x004C9D00
PcTexture::~PcTexture()
{
    if (flag_bits_ & 1)
        end_edit();
    if (auto* texture = static_cast<TextureRef*>(image_)) {
        texture->destroy_body();
        memory::Hub::get_pull()->dealloc(texture);
    }
}

// 0x004C7F00
bool PcTexture::ingest_file(const char* name, int /*unused1*/, int /*unused2*/)
{
    char path[0x100];
    size_t length = std::strlen(name); // (unchecked, as the original)
    std::memcpy(path, name, length);
    std::memcpy(path + length, ".img.xbx", 9);
    image_ = read_texture_file(path);
    return image_ != nullptr;
}

// 0x004C7F80
bool PcTexture::load_memory(const uint8_t* data, uint32_t size, uint32_t, uint32_t, uint32_t)
{
    image_ = load_texture_memory(data, size); // (not original: the size passed on as the reader's bound)
    return image_ != nullptr;
}

// 0x004C9EC0
bool PcTexture::write_back(int /*unused1*/, const void* /*unused2*/)
{
    auto* texels = static_cast<uint32_t*>(get_texture()->get_lock());
    const uint32_t* pixels = flag_bits_ & 1 ? get_edit(this)->pixels : nullptr; // (null read from, as the original)
    uint32_t width = fetch_breadth();
    uint32_t height = fetch_tallness();
    uint32_t* row = texels + (height - 1) * width;
    for (uint32_t y = height; y; --y) {
        for (uint32_t x = 0; x < width; ++x)
            row[x] = *pixels++;
        row -= width;
    }
    get_texture()->get_unlock();
    return true;
}

// 0x004C98B0
bool PcTexture::end_edit()
{
    // (the edit's fields read whether or not it is one, as the original)
    auto* edit = get_edit(this);
    TextureRef* texture = flag_bits_ & 1 ? edit->texture : static_cast<TextureRef*>(image_);
    if (edit->pixels) {
        memory::Hub::get_pull()->dealloc(edit->pixels);
        get_edit(this)->pixels = nullptr;
    }
    if (get_edit(this)->original) {
        memory::Hub::get_pull()->dealloc(get_edit(this)->original);
        get_edit(this)->original = nullptr;
    }
    if (TextureRef* kept = get_edit(this)->kept) {
        kept->destroy_body();
        memory::Hub::get_pull()->dealloc(kept);
        get_edit(this)->kept = nullptr;
    }
    memory::Hub::get_pull()->dealloc(image_);
    image_ = texture;
    flag_bits_ &= ~1u;
    return true;
}

// 0x004C7FB0
uint16_t PcTexture::fetch_breadth()
{
    TextureRef* texture = get_texture();
    return texture ? texture->image_width_ : 0;
}

// 0x004C7FE0
uint16_t PcTexture::fetch_tallness()
{
    TextureRef* texture = get_texture();
    return texture ? texture->image_height_ : 0;
}

// 0x004C8010
uint8_t PcTexture::get_depth()
{
    TextureRef* texture = get_texture();
    return texture ? texture->depth : 0;
}

// 0x004C8030
uint8_t PcTexture::get_unknown_e()
{
    TextureRef* texture = get_texture();
    return texture ? texture->unknowne_ : 0;
}

// 0x004C8050
uint8_t PcTexture::get_unknown_c() { return unknownc_; }

// 0x004C8060
uint8_t PcTexture::get_unknown_d() { return unknownd_; }

// 0x004C8450
PcTexture::PcTexture(const PcTexture* source)
{
    unknownc_ = source->unknownc_;
    flag_bits_ = source->flag_bits_;
    TextureRef* copy = nullptr;
    if (source->get_texture()) {
        void* memory = memory::alloc(sizeof(TextureRef));
        copy = memory ? static_cast<TextureRef*>(memory)->construct_copy(source->get_texture()) : nullptr;
    }
    // (the source's size through its virtual functions)
    uint32_t width = const_cast<PcTexture*>(source)->fetch_breadth();
    uint32_t height = const_cast<PcTexture*>(source)->fetch_tallness();
    if (!(source->flag_bits_ & 1)) {
        image_ = copy;
        return;
    }
    auto* edit = static_cast<TextureEdit*>(memory::alloc(sizeof(TextureEdit)));
    if (edit) // (no check after: written through null, as the original)
        *edit = {};
    image_ = edit;
    edit->texture = copy;
    const TextureEdit* from = get_edit(source);
    if (from->original) {
        uint32_t bytes = height * width * 4;
        edit->original = static_cast<uint32_t*>(memory::alloc(bytes));
        std::memcpy(edit->original, from->original, bytes);
    } else {
        edit->original = nullptr;
    }
    // (its own pixels tested, not the source's: always null here, so never copied - as the original)
    if (edit->pixels) {
        edit->pixels = static_cast<uint32_t*>(memory::alloc(height * height * 4));
        std::memcpy(edit->pixels, from->pixels, width * height * 4);
    } else {
        edit->pixels = nullptr;
    }
    if (from->kept) {
        void* memory = memory::alloc(sizeof(TextureRef));
        edit->kept = memory ? static_cast<TextureRef*>(memory)->construct_copy(from->kept) : nullptr;
    } else {
        edit->kept = nullptr;
    }
}

// 0x004C8830
bool PcTexture::create_image(uint32_t new_checksum, uint32_t width, uint32_t height, int bits_per_pixel, int, int)
{
    TextureRef* old = get_texture();
    if (old && old->texture) // (released, the old texture itself kept)
        static_cast<IUnknown*>(old->texture)->Release();
    void* memory = memory::alloc(sizeof(TextureRef));
    TextureRef* texture = memory ? static_cast<TextureRef*>(memory)->make_new() : nullptr;
    checksum_ = new_checksum;
    image_ = texture;                 // (an edit's place too)
    texture->checksum = new_checksum; // (no check, as the original)
    texture->unknownf_ = 0;
    texture->image_width_ = static_cast<uint16_t>(width);
    texture->image_height_ = static_cast<uint16_t>(height);
    if (bits_per_pixel < 8)
        bits_per_pixel = 32;
    uint32_t format = new_checksum; // (any other depth: the checksum taken for the format, as the original)
    switch (bits_per_pixel) {
    case 1:
    case 2:
    case 4:
    case 8:
        format = D3DFMT_P8;
        texture->depth = 8;
        break;
    case 16:
        format = D3DFMT_X1R5G5B5;
        texture->depth = 16;
        break;
    case 32:
        format = D3DFMT_X8R8G8B8;
        texture->depth = 32;
        break;
    default:
        break;
    }
    auto** d3d = reinterpret_cast<IDirect3DTexture9**>(&texture->texture);
    input_port()->CreateTexture(width, height, 0, 0, static_cast<D3DFORMAT>(format), D3DPOOL_MANAGED, d3d, nullptr);
    D3DSURFACE_DESC desc;
    (*d3d)->GetLevelDesc(0, &desc);
    texture->height = static_cast<uint16_t>(desc.Height);
    texture->width = static_cast<uint16_t>(desc.Width);
    texture->unknowne_ = format == D3DFMT_P8 ? 0x20 : 0;
    return true;
}

// 0x004C86A0
bool PcTexture::copy_from(const BitmapTexture* source_texture)
{
    const auto* source = static_cast<const PcTexture*>(source_texture);
    TextureRef* from = source->get_texture();
    auto** into = reinterpret_cast<IDirect3DTexture9**>(&get_texture()->texture);
    if (*into)
        (*into)->Release();
    auto* from_texture = static_cast<IDirect3DTexture9*>(from->texture);
    DWORD levels = from_texture->GetLevelCount();
    D3DSURFACE_DESC desc;
    from_texture->GetLevelDesc(0, &desc);
    if (input_port()->CreateTexture(desc.Width, desc.Height, levels, 0, desc.Format, D3DPOOL_MANAGED, into, nullptr) !=
        D3D_OK)
        crt::get_exit(0); // (the game's exit())
    for (DWORD level = 0; level < levels; ++level) {
        static_cast<IDirect3DTexture9*>(from->texture)->GetLevelDesc(level, &desc);
        uint32_t row_bytes, rows = desc.Height;
        if (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) {
            row_bytes = desc.Width << 2;
        } else { // (blocks of 4 rows; the row's bytes as the original works them out)
            rows = (rows + 3) >> 2;
            row_bytes = desc.Format == D3DFMT_DXT1 ? desc.Width << 3 : desc.Width << 4;
        }
        D3DLOCKED_RECT from_rect, into_rect;
        static_cast<IDirect3DTexture9*>(from->texture)->LockRect(level, &from_rect, nullptr, 0);
        (*into)->LockRect(level, &into_rect, nullptr, 0);
        auto* write = static_cast<uint8_t*>(into_rect.pBits);
        auto* read = static_cast<const uint8_t*>(from_rect.pBits);
        for (uint32_t row = rows; row; --row) {
            std::memcpy(write, read, row_bytes);
            write += into_rect.Pitch;
            read += from_rect.Pitch;
        }
        static_cast<IDirect3DTexture9*>(from->texture)->UnlockRect(level);
        (*into)->UnlockRect(level);
    }
    return true;
}

// 0x004C8A60
bool PcTexture::begin_edit(bool keep_texture, bool keep_copy)
{
    TextureRef* texture = get_texture();
    uint32_t width = texture->width, height = texture->height;
    auto* edit = static_cast<TextureEdit*>(memory::alloc(sizeof(TextureEdit)));
    if (edit) // (no check after: written through null, as the original)
        *edit = {};
    image_ = edit;
    edit->texture = texture;
    uint32_t bytes = width * height * 4;
    edit->pixels = static_cast<uint32_t*>(memory::alloc(bytes));
    flag_bits_ |= 1;
    uint32_t* pixels = get_edit(this)->pixels;
    auto* d3d = static_cast<IDirect3DTexture9*>(texture->texture);
    D3DSURFACE_DESC desc;
    d3d->GetLevelDesc(0, &desc);
    D3DLOCKED_RECT rect;
    d3d->LockRect(0, &rect, nullptr, D3DLOCK_READONLY);
    if (texture->depth != 8 && texture->depth == 32) { // (rows bottom up, the pitch taken as the width)
        const uint32_t* row = static_cast<const uint32_t*>(rect.pBits) + (height - 1) * width;
        for (uint32_t y = height; y; --y) {
            std::memcpy(pixels, row, width * 4);
            pixels += width;
            row -= width;
        }
    }
    static_cast<IDirect3DTexture9*>(texture->texture)->UnlockRect(0);
    if (keep_copy) {
        get_edit(this)->original = static_cast<uint32_t*>(memory::alloc(bytes));
        std::memcpy(get_edit(this)->original, get_edit(this)->pixels, bytes);
    }
    if (keep_texture) {
        void* memory = memory::alloc(sizeof(TextureRef));
        get_edit(this)->kept = memory ? static_cast<TextureRef*>(memory)->make_new() : nullptr;
        TextureRef* kept = get_edit(this)->kept; // (no check, as the original)
        kept->image_width_ = static_cast<uint16_t>(width);
        kept->image_height_ = static_cast<uint16_t>(height);
        kept->depth = 32;
        auto** kept_d3d = reinterpret_cast<IDirect3DTexture9**>(&kept->texture);
        input_port()->CreateTexture(width, height, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, kept_d3d, nullptr);
        (*kept_d3d)->GetLevelDesc(0, &desc);
        kept->height = static_cast<uint16_t>(desc.Height);
        kept->width = static_cast<uint16_t>(desc.Width);
        D3DLOCKED_RECT kept_rect;
        (*kept_d3d)->LockRect(0, &kept_rect, nullptr, 0);
        std::memcpy(kept_rect.pBits, get_edit(this)->pixels, bytes);
        (*kept_d3d)->UnlockRect(0);
    }
    return true;
}

namespace {

// A pixel of four bytes sampled between four (10-bit fractions; each channel rounded).
uint32_t bilinear(uint32_t c00, uint32_t c01, uint32_t c10, uint32_t c11, int fx, int fy)
{
    int w00 = ((0x400 - fx) * (0x400 - fy)) >> 10;
    int w01 = ((0x400 - fy) * fx) >> 10;
    int w10 = ((0x400 - fx) * fy) >> 10;
    int w11 = (fy * fx) >> 10;
    uint32_t result = 0;
    for (int k = 0; k < 32; k += 8) {
        int sum = static_cast<int>(c11 >> k & 0xFF) * w11 + static_cast<int>(c10 >> k & 0xFF) * w10 +
                  static_cast<int>(c01 >> k & 0xFF) * w01 + static_cast<int>(c00 >> k & 0xFF) * w00 + 0x200;
        result |= static_cast<uint32_t>(static_cast<uint8_t>(sum >> 10)) << k;
    }
    return result;
}

// The four pixels about (sx, sy) of a width x height image (the right / lower ones the same at the edges).
uint32_t sample_at(const uint32_t* image, int width, int height, int sx, int sy, int fx, int fy)
{
    int step = sx < width - 1 ? 1 : 0;
    const uint32_t* p = image + (sy * width + sx);
    uint32_t c00 = p[0], c01 = p[step];
    if (sy < height - 1)
        p += width;
    return bilinear(c00, c01, p[0], p[step], fx, fy);
}

int ftol(double value) { return static_cast<int>(value); } // (the game's _ftol: towards zero)

} // namespace

// 0x004C9200
void __cdecl resize_image(const uint8_t* from, uint16_t from_width, uint16_t from_height, uint8_t* into,
                          uint16_t into_width, uint16_t into_height, uint16_t bits_per_pixel)
{
    if (bits_per_pixel == 8) { // (nearest, the bytes cleared first - as many as the source has)
        std::memset(into, 0, static_cast<uint32_t>(from_height) * from_width);
        float width = static_cast<float>(static_cast<int>(from_width));
        float x_scale = static_cast<float>(static_cast<double>(width) / static_cast<int>(into_width));
        float y_scale =
            static_cast<float>(static_cast<double>(static_cast<int>(from_height)) / static_cast<int>(into_height));
        for (int y = 0; y < into_height; ++y) {
            double row = static_cast<double>(y) * width * y_scale;
            uint8_t* out = into + into_width * y;
            for (int x = 0; x < into_width; ++x)
                out[x] = from[ftol(static_cast<double>(x) * x_scale + row)];
        }
        return;
    }
    int x_step = ftol(static_cast<double>(static_cast<int>(from_width)) / static_cast<int>(into_width) * 1024.0f);
    int y_step = ftol(static_cast<double>(static_cast<int>(from_height)) / static_cast<int>(into_height) * 1024.0f);
    auto* out = reinterpret_cast<uint32_t*>(into);
    const auto* image = reinterpret_cast<const uint32_t*>(from);
    int y = 0;
    for (int row = into_height; row > 0; --row, y += y_step) {
        int sy = static_cast<int16_t>(y >> 10);
        int fy = y & 0x3FF;
        int x = 0;
        for (int column = into_width; column > 0; --column, x += x_step)
            *out++ = sample_at(image, from_width, from_height, static_cast<int16_t>(x >> 10), sy, x & 0x3FF, fy);
    }
}

// 0x004C9520
void __cdecl zoom_image(const uint8_t* from, uint16_t width, uint16_t height, uint8_t* into, float x_zoom, float y_zoom)
{
    int x_step = ftol(1.0f / static_cast<double>(x_zoom) * 1024.0f);
    int y_step = ftol(1.0f / static_cast<double>(y_zoom) * 1024.0f);
    int x_start = ((0x400 - x_step) * static_cast<int>(width)) >> 1; // (about the centre)
    int y = ((0x400 - y_step) * static_cast<int>(height)) >> 1;
    int x_max = width * 1024 - 1024, y_max = height * 1024 - 1024;
    auto* out = reinterpret_cast<uint32_t*>(into);
    const auto* image = reinterpret_cast<const uint32_t*>(from);
    for (int row = height; row > 0; --row, y += y_step) {
        int cy = y < 0 ? 0 : y > y_max ? y_max : y;
        int sy = static_cast<int16_t>(cy >> 10);
        int x = x_start;
        for (int column = width; column > 0; --column, x += x_step) {
            int cx = x < 0 ? 0 : x > x_max ? x_max : x;
            *out++ = sample_at(image, width, height, static_cast<int16_t>(cx >> 10), sy, cx & 0x3FF, cy & 0x3FF);
        }
    }
}

// 0x004C97D0
uint8_t PcTexture::nearest_palette_index(uint32_t colour)
{
    uint8_t depth = get_depth();
    int count = 1 << depth;
    const auto* entry =
        reinterpret_cast<const uint8_t*>(get_texture()->palette_owner()); // (its entries, as the original)
    uint8_t nearest = 0;
    int best = 0x7FFFFFFF;
    int c0 = colour & 0xFF, c1 = colour >> 8 & 0xFF, c2 = colour >> 16 & 0xFF, c3 = colour >> 24;
    for (int i = 0; i < count; ++i, entry += 4) {
        int d0 = entry[0] - c0, d1 = entry[1] - c1, d2 = entry[2] - c2, d3 = entry[3] - c3;
        int distance = d2 * d2 + d3 * d3 * 2 + d1 * d1 + d0 * d0;
        if (distance < best) {
            best = distance;
            nearest = static_cast<uint8_t>(i);
        }
    }
    return nearest;
}

// 0x004C9950
bool PcTexture::modulate(uint32_t colour, bool cumulative)
{
    // (the edit's fields read without checking it is one, as the original)
    uint8_t* into = reinterpret_cast<uint8_t*>(get_edit(this)->pixels);
    const uint8_t* from = get_edit(this)->original && !cumulative
                              ? reinterpret_cast<const uint8_t*>(get_edit(this)->original)
                              : reinterpret_cast<const uint8_t*>(get_edit(this)->pixels);
    uint32_t red = (colour & 0xFF) * 0xFF >> 7, green = (colour >> 8 & 0xFF) * 0xFF >> 7,
             blue = (colour >> 16 & 0xFF) * 0xFF >> 7, alpha = colour >> 24;
    int width = fetch_breadth();
    int count = fetch_tallness() * width;
    for (int i = 0; i < count; ++i, from += 4, into += 4) {
        into[0] = static_cast<uint8_t>(from[0] * blue / 0xFF);
        into[1] = static_cast<uint8_t>(from[1] * green / 0xFF);
        into[2] = static_cast<uint8_t>(from[2] * red / 0xFF);
        into[3] = static_cast<uint8_t>(from[3] * alpha >> 7);
    }
    if (get_depth() <= 8 && !cumulative) { // (the palette too, in place)
        auto* entry = reinterpret_cast<uint8_t*>(get_texture()->palette_owner());
        int entries = 1 << get_depth();
        for (int i = 0; i < entries; ++i, entry += 4) {
            entry[0] = static_cast<uint8_t>(entry[0] * blue / 0xFF);
            entry[1] = static_cast<uint8_t>(entry[1] * green / 0xFF);
            entry[2] = static_cast<uint8_t>(entry[2] * red / 0xFF);
            entry[3] = static_cast<uint8_t>(entry[3] * alpha >> 7);
        }
    }
    return true;
}

// 0x004C9D80
bool PcTexture::resize_from(const BitmapTexture* source_texture, int /*unused*/)
{
    const auto* source = static_cast<const PcTexture*>(source_texture);
    if (!(flag_bits_ & 1) || !(source->flag_bits_ & 1))
        return false;
    const TextureRef* into = get_edit(this)->texture;
    const TextureRef* from = get_edit(source)->texture;
    resize_image(reinterpret_cast<const uint8_t*>(get_edit(source)->pixels), from->image_width_, from->image_height_,
                 reinterpret_cast<uint8_t*>(get_edit(this)->pixels), into->image_width_, into->image_height_, 32);
    return true;
}

// 0x004C9DE0
bool PcTexture::get_zoom(float x_zoom, float y_zoom)
{
    uint32_t* pixels = get_edit(this)->pixels; // (read before the flag's test, as the original)
    const TextureRef* texture = get_texture();
    uint32_t width = texture->image_width_, height = texture->image_height_;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    uint32_t bytes = height * width * 4;
    auto* zoomed = static_cast<uint8_t*>(memory::alloc(bytes));
    std::memset(zoomed, 0, bytes);
    memory::Hub::get_pull()->leave_heap_scope();
    zoom_image(reinterpret_cast<const uint8_t*>(pixels), static_cast<uint16_t>(width), static_cast<uint16_t>(height),
               zoomed, x_zoom, y_zoom);
    std::memcpy(pixels, zoomed, bytes);
    memory::Hub::get_pull()->dealloc(zoomed);
    return true;
}

// 0x004C9F90
void PcTexture::read_rect(uint32_t* out, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t image_width = get_texture()->image_width_;
    if (flag_bits_ & 1 && get_edit(this)->pixels) {
        const uint32_t* from = get_edit(this)->pixels + (y * image_width + x);
        for (uint32_t row = height; row; --row) {
            std::memcpy(out, from, width * 4);
            out += width;
            from += image_width;
        }
        return;
    }
    // Palettized: the whole image (not the rectangle's size, as the original), bottom row first.
    const auto* texels = static_cast<const uint8_t*>(get_texture()->get_lock());
    const auto* palette = reinterpret_cast<const uint32_t*>(get_texture()->palette_owner());
    uint32_t rows = get_texture()->image_height_, columns = get_texture()->image_width_;
    const uint8_t* row = texels + x + (static_cast<int>(rows) - y - 1) * static_cast<int>(columns);
    for (uint32_t r = rows; r; --r) {
        for (uint32_t c = 0; c < columns; ++c)
            *out++ = palette[row[c]];
        row -= columns;
    }
    get_texture()->get_unlock();
}

// 0x004CA0D0
void PcTexture::write_rect(const uint32_t* in, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t image_width = get_texture()->image_width_, image_height = get_texture()->image_height_;
    if (flag_bits_ & 1 && get_edit(this)->pixels) {
        uint32_t* into = get_edit(this)->pixels + (y * image_width + x);
        for (uint32_t row = height; row; --row) {
            std::memcpy(into, in, width * 4);
            in += width;
            into += image_width;
        }
        return;
    }
    auto* texels = static_cast<uint8_t*>(get_texture()->get_lock());
    uint8_t* row = texels + (static_cast<int>(image_height) - y - 1) * static_cast<int>(image_width) + x;
    for (uint32_t r = height; r; --r) {
        for (uint32_t c = 0; c < width; ++c)
            row[c] = nearest_palette_index(*in++);
        row -= image_width;
    }
    get_texture()->get_unlock();
}

// 0x004C8D30
bool PcTexture::spin_by(float angle, float centre_x, float centre_y)
{
    uint32_t* pixels = get_edit(this)->pixels; // (read before the flag's test, as the original)
    uint32_t width = get_texture()->width, height = get_texture()->height;
    if (centre_x < 0.0f) // (below 0: the middle)
        centre_x = static_cast<float>((static_cast<double>(width) - 1.0f) * 0.5f);
    if (centre_y < 0.0f)
        centre_y = static_cast<float>((static_cast<double>(height) - 1.0f) * 0.5f);
    float m[4];
    maths::create_rotate_2d(m, -angle);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    uint32_t bytes = height * width * 4;
    auto* rotated = static_cast<uint32_t*>(memory::alloc(bytes));
    memory::Hub::get_pull()->leave_heap_scope();
    int y_max = static_cast<int>(height * 1024 - 1024), x_max = static_cast<int>(width * 1024 - 1024);
    // the image's axes turned (each a float, as the original stores them), in 1024ths
    float xx = static_cast<float>(static_cast<double>(m[2]) * 0.0f + m[0]);
    float xy = static_cast<float>(static_cast<double>(m[3]) * 0.0f + m[1]);
    float yx = static_cast<float>(static_cast<double>(m[0]) * 0.0f + m[2]);
    float yy = static_cast<float>(static_cast<double>(m[1]) * 0.0f + m[3]);
    int xx_step = ftol(static_cast<double>(xx) * 1024.0f), xy_step = ftol(static_cast<double>(xy) * 1024.0f);
    int yx_step = ftol(static_cast<double>(yx) * 1024.0f), yy_step = ftol(static_cast<double>(yy) * 1024.0f);
    // where the top left comes from: the centre moved back through the turn
    double nx = -static_cast<double>(centre_x), ny = -static_cast<double>(centre_y);
    float ox = static_cast<float>(m[2] * ny + nx * m[0]);
    float oy = static_cast<float>(ny * m[3] + m[1] * nx);
    double origin_coord_x = static_cast<double>(ox) + centre_x; // (kept in the register, unrounded)
    float origin_coord_y = static_cast<float>(static_cast<double>(oy) + centre_y);
    int row_x = ftol(origin_coord_x * 1024.0f);
    int row_y = ftol(static_cast<double>(origin_coord_y) * 1024.0f);
    uint32_t* out = rotated;
    for (uint32_t row = height; row; --row, row_x += yx_step, row_y += yy_step) {
        int x = row_x, y = row_y;
        for (uint32_t column = width; column; --column, x += xx_step, y += xy_step) {
            int cx = x < 0 ? 0 : x > x_max ? x_max : x;
            int cy = y < 0 ? 0 : y > y_max ? y_max : y;
            *out++ = sample_at(pixels, static_cast<int>(width), static_cast<int>(height),
                               static_cast<int16_t>(cx >> 10), static_cast<int16_t>(cy >> 10), cx & 0x3FF, cy & 0x3FF);
        }
    }
    std::memcpy(pixels, rotated, bytes);
    memory::Hub::get_pull()->dealloc(rotated);
    return true;
}

// 0x004CA220
bool PcTexture::get_stretch(uint16_t position, int vertical, int amount)
{
    uint32_t width = get_texture()->image_width_, height = get_texture()->image_height_;
    if (!amount)
        return true;
    int at = position;
    if (at + amount <= 0)
        amount = 1 - at;
    uint32_t end = static_cast<uint32_t>(at + amount);
    uint16_t read_x = 0, read_y = 0, read_width, read_height, new_width, new_height;
    int offset;
    if (!vertical) { // (the part left of the column stretched by -amount, or the part from it by amount)
        if (end >= width)
            amount = static_cast<int>(width) - at - 1;
        read_height = new_height = static_cast<uint16_t>(height);
        if (amount < 0) {
            read_width = static_cast<uint16_t>(at + 1);
            new_width = static_cast<uint16_t>(at - amount + 1);
            offset = -amount;
        } else {
            read_width = static_cast<uint16_t>(width - at);
            new_width = static_cast<uint16_t>(amount - at + static_cast<int>(width));
            read_x = position;
            offset = 0;
        }
    } else {
        if (end >= height)
            amount = static_cast<int>(height) - at - 1;
        read_width = new_width = static_cast<uint16_t>(width);
        if (amount < 0) {
            read_height = static_cast<uint16_t>(at + 1);
            new_height = static_cast<uint16_t>(at - amount + 1);
            offset = -amount * static_cast<int>(width);
        } else {
            read_height = static_cast<uint16_t>(height - at);
            new_height = static_cast<uint16_t>(amount - at + static_cast<int>(height));
            read_y = position;
            offset = 0;
        }
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->cinematic_high_heap_ ? manager->cinematic_high_heap_ : manager->top_down_heap_);
    auto* part = static_cast<uint32_t*>(memory::alloc(static_cast<uint32_t>(read_height) * read_width * 4));
    auto* stretched = static_cast<uint32_t*>(memory::alloc(static_cast<uint32_t>(new_height) * new_width * 4));
    memory::Hub::get_pull()->leave_heap_scope();
    read_rect(part, read_x, read_y, read_width, read_height);
    resize_image(reinterpret_cast<const uint8_t*>(part), read_width, read_height, reinterpret_cast<uint8_t*>(stretched),
                 new_width, new_height, 32);
    copy_sub_image(reinterpret_cast<uint8_t*>(part), 0, 0, read_width, read_height,
                   reinterpret_cast<const uint8_t*>(stretched + static_cast<uint16_t>(offset)), new_width, new_height,
                   32);
    write_rect(part, read_x, read_y, read_width, read_height);
    if (part)
        memory::Hub::get_pull()->dealloc(part);
    if (stretched)
        memory::Hub::get_pull()->dealloc(stretched);
    return true;
}

// 0x004CA7B0
bool PcTexture::get_mirror()
{
    uint32_t width = get_texture()->image_width_;
    int height = get_texture()->image_height_;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    auto* column = static_cast<uint32_t*>(memory::alloc(height * 4));
    memory::Hub::get_pull()->leave_heap_scope();
    uint32_t* left = flag_bits_ & 1 ? get_edit(this)->pixels : nullptr;
    uint32_t* right = (flag_bits_ & 1 ? get_edit(this)->pixels : nullptr) + width - 1;
    for (; left < right; ++left, --right) { // (the columns swapped)
        for (int i = 0; i < height; ++i)
            column[i] = left[i * width];
        for (int i = 0; i < height; ++i)
            left[i * width] = right[i * width];
        for (int i = 0; i < height; ++i)
            right[i * width] = column[i];
    }
    memory::Hub::get_pull()->dealloc(column);
    return true;
}

// 0x004CA8E0
bool PcTexture::get_overlay(BitmapTexture* over_texture, int /*unused*/)
{
    auto* over = static_cast<PcTexture*>(over_texture);
    const auto* texels = static_cast<const uint8_t*>(over->get_texture()->get_lock());
    auto* into = reinterpret_cast<uint8_t*>(flag_bits_ & 1 ? get_edit(this)->pixels : nullptr);
    uint32_t width = over->fetch_breadth();
    uint32_t height = over->fetch_tallness();
    const uint8_t* row = texels + (height - 1) * width * 4; // (bottom row first, by its alpha)
    for (uint32_t y = height; y; --y, row -= width * 4) {
        const uint8_t* from = row;
        for (uint32_t x = 0; x < width; ++x, from += 4, into += 4) {
            uint8_t alpha = from[3];
            int rest = static_cast<uint8_t>(0xFF - alpha);
            into[0] = static_cast<uint8_t>((from[0] * alpha + into[0] * rest) >> 8);
            into[1] = static_cast<uint8_t>((from[1] * alpha + into[1] * rest) >> 8);
            into[2] = static_cast<uint8_t>((from[2] * alpha + into[2] * rest) >> 8);
            if (alpha > into[3])
                into[3] = alpha;
        }
    }
    over->get_texture()->get_unlock();
    return false;
}

// 0x004CAA20
bool PcTexture::shift_hue_sat_value(float hue, float saturation, float value, bool cumulative)
{
    auto* into = reinterpret_cast<uint8_t*>(flag_bits_ & 1 ? get_edit(this)->pixels : nullptr);
    const uint8_t* from = nullptr;
    if (flag_bits_ & 1)
        from = reinterpret_cast<const uint8_t*>(get_edit(this)->original && !cumulative ? get_edit(this)->original
                                                                                        : get_edit(this)->pixels);
    int width = fetch_breadth();
    int count = fetch_tallness() * width;
    for (int i = 0; i < count; ++i, from += 4, into += 4) {
        float h, s, v;
        rgb_to_hsv(static_cast<float>(from[0] * (1.0f / 255.0f)), static_cast<float>(from[1] * (1.0f / 255.0f)),
                   static_cast<float>(from[2] * (1.0f / 255.0f)), &h, &s, &v);
        double turned = static_cast<double>(h) + hue;
        h = static_cast<float>(turned);
        if (!(turned <= 360.0f))
            h = static_cast<float>(static_cast<double>(h) - 360.0f);
        double scaled = static_cast<double>(s) * saturation;
        s = scaled < 1.0f ? static_cast<float>(scaled) : 1.0f;
        scaled = static_cast<double>(v) * value;
        v = scaled < 1.0f ? static_cast<float>(scaled) : 1.0f;
        float rgb[3];
        hsv_to_rgb(&rgb[0], &rgb[1], &rgb[2], h, s, v);
        for (int k = 0; k < 3; ++k)
            into[k] = static_cast<uint8_t>(ftol(static_cast<double>(rgb[k]) * 255.0f + 0.5f));
    }
    return true;
}

// 0x004CABE0
bool PcTexture::get_offset(int dx, int dy, bool keep_edges, int /*unused*/)
{
    int width = fetch_breadth();
    int height = fetch_tallness();
    int from_x, into_x, part_width, from_y = 0, into_y, part_height;
    if (dx >= 0) {
        from_x = 0;
        into_x = dx;
        part_width = width - dx;
    } else {
        from_x = -dx;
        into_x = 0;
        part_width = width + dx;
    }
    if (dy >= 0) {
        into_y = dy;
        part_height = height - dy;
    } else {
        from_y = -dy;
        into_y = 0;
        part_height = height + dy;
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    auto* part = static_cast<uint32_t*>(crt::alloc_block(part_height * part_width * 4)); // (the C runtime's malloc)
    memory::Hub::get_pull()->leave_heap_scope();
    read_rect(part, static_cast<uint16_t>(from_x), static_cast<uint16_t>(from_y), static_cast<uint16_t>(part_width),
              static_cast<uint16_t>(part_height));
    write_rect(part, static_cast<uint16_t>(into_x), static_cast<uint16_t>(into_y), static_cast<uint16_t>(part_width),
               static_cast<uint16_t>(part_height));
    if (!keep_edges) { // (what was uncovered filled from the moved part's nearest edge)
        for (int y = 0, sy = -into_y; y < height; ++y, ++sy) {
            int row = sy <= 0 ? 0 : sy;
            if (row >= part_height - 1)
                row = part_height - 1;
            int row_start = row * part_width;
            for (int x = 0, sx = -into_x; x < width; ++x, ++sx) {
                if (sx >= 0 && sx < part_width && sy >= 0 && sy < part_height)
                    continue;
                int column = sx <= 0 ? 0 : sx;
                if (column >= part_width - 1)
                    column = part_width - 1;
                uint32_t pixel = part[row_start + column];
                uint32_t texels_wide = get_texture()->width;
                if (flag_bits_ & 1 && get_edit(this)->pixels)
                    get_edit(this)->pixels[static_cast<uint16_t>(y) * texels_wide + static_cast<uint16_t>(x)] = pixel;
            }
        }
    }
    crt::dealloc(part); // (the C runtime's free)
    return true;
}

// 0x004CADC0
bool PcTexture::get_border(uint32_t colour)
{
    uint32_t* pixels = flag_bits_ & 1 ? get_edit(this)->pixels : nullptr;
    uint32_t width = get_texture()->width;
    int height = get_texture()->height;
    uint32_t* bottom = pixels + (height - 1) * width;
    uint32_t* right = pixels + width - 1;
    uint32_t* top = pixels;
    for (int i = 0; i < height; ++i)
        pixels[i * width] = colour;
    for (int i = 0; i < height; ++i)
        right[i * width] = colour;
    for (uint32_t i = 0; i < width; ++i) {
        top[i] = colour;
        bottom[i] = colour;
    }
    return true;
}

// 0x004CA410
bool PcTexture::squash(uint16_t position, int vertical, int amount, bool fill, uint32_t colour)
{
    uint32_t height = get_texture()->image_height_, width = get_texture()->image_width_;
    if (!amount)
        return true;
    int at = position;
    if (at + amount <= 0)
        amount = 1 - at;
    int end = at + amount;
    uint16_t read_x = 0, read_y = 0, into_x = 0, into_y = 0, read_width, read_height, new_width, new_height;
    if (!vertical) {
        if (end >= static_cast<int>(width))
            amount = static_cast<int>(width) - at - 1;
        new_height = read_height = static_cast<uint16_t>(height);
        if (amount < 0) {
            new_width = static_cast<uint16_t>(amount - at + static_cast<int>(width));
            read_width = static_cast<uint16_t>(width - at);
            read_x = position;
        } else {
            read_width = static_cast<uint16_t>(at + 1);
            new_width = static_cast<uint16_t>(at - amount + 1);
            into_x = static_cast<uint16_t>(amount);
        }
    } else {
        if (end >= static_cast<int>(height))
            amount = static_cast<int>(height) - at - 1;
        read_width = new_width = static_cast<uint16_t>(width);
        if (amount < 0) {
            read_height = static_cast<uint16_t>(height - at);
            new_height = static_cast<uint16_t>(amount - at + static_cast<int>(height));
            read_y = position;
        } else {
            read_height = static_cast<uint16_t>(at + 1);
            new_height = static_cast<uint16_t>(at - amount + 1);
            into_y = static_cast<uint16_t>(amount);
        }
    }
    auto crt_malloc = crt::alloc_block; // (the C runtime's)
    auto crt_free = crt::dealloc;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    int count = read_height * read_width;
    auto* part = static_cast<uint32_t*>(crt_malloc(count * 4));
    auto* squashed = static_cast<uint32_t*>(crt_malloc(new_height * new_width * 4));
    memory::Hub::get_pull()->leave_heap_scope();
    read_rect(part, read_x, read_y, read_width, read_height);
    resize_image(reinterpret_cast<const uint8_t*>(part), read_width, read_height, reinterpret_cast<uint8_t*>(squashed),
                 new_width, new_height, 32);
    if (fill) {
        for (int i = 0; i < count; ++i)
            part[i] = colour;
    }
    copy_sub_image(reinterpret_cast<uint8_t*>(part), into_x, into_y, read_width, read_height,
                   reinterpret_cast<const uint8_t*>(squashed), new_width, new_height, 32);
    write_rect(part, read_x, read_y, read_width, read_height);
    if (!fill) { // (what was uncovered filled with the squashed part's edge column / row)
        if (!vertical) {
            bool right = amount > 0;
            int source = right ? 0 : new_width - 1;
            manager = memory::Hub::get_pull();
            manager->enter_heap_scope(manager->top_down_heap_);
            int rows = static_cast<uint16_t>(height);
            auto* column = static_cast<uint32_t*>(crt_malloc(rows * 4));
            memory::Hub::get_pull()->leave_heap_scope();
            for (int i = 0; i < rows; ++i)
                column[i] = squashed[source + i * new_width];
            int first = right ? 0 : static_cast<int>(width) + amount;
            int last = right ? amount - 1 : static_cast<int>(width) - 1;
            for (int x = first; x <= last; ++x)
                write_rect(column, static_cast<uint16_t>(x), 0, 1, static_cast<uint16_t>(height));
            crt_free(column);
        } else {
            bool down = amount > 0;
            int source = down ? 0 : new_height - 1;
            manager = memory::Hub::get_pull();
            manager->enter_heap_scope(manager->top_down_heap_);
            uint32_t bytes = static_cast<uint16_t>(width) * 4u;
            auto* row = static_cast<uint32_t*>(crt_malloc(bytes));
            memory::Hub::get_pull()->leave_heap_scope();
            std::memcpy(row, squashed + new_width * source, bytes);
            int first = down ? 0 : static_cast<int>(height) + amount;
            int last = down ? amount - 1 : static_cast<int>(height) - 1;
            for (int y = first; y <= last; ++y)
                write_rect(row, 0, static_cast<uint16_t>(y), static_cast<uint16_t>(width), 1);
            crt_free(row);
        }
    }
    if (part)
        crt_free(part);
    if (squashed)
        crt_free(squashed);
    return true;
}

// 0x004C9B00
BitmapTexture* PcTexDict::load_texture_file(const char* name, uint32_t a1, uint32_t /*a2*/)
{
    BitmapTexture* texture = alloc_pc_texture();
    // (no check: the engine's load called on null without memory, as the original)
    texture->BitmapTexture::load_by_name(name, static_cast<int>(a1), 1);
    return texture;
}

// 0x004C89B0
BitmapTexture* PcTexDict::load_texture_data(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3,
                                            uint32_t a4, uint32_t /*a6*/)
{
    BitmapTexture* texture = alloc_pc_texture();
    texture->BitmapTexture::read_from_ram_block(data, size, checksum, a3, a4, 1);
    return texture;
}

// 0x004C9BA0
BitmapTexture* PcTexDict::make_texture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel,
                                       uint32_t a4, uint32_t a5)
{
    auto* texture = static_cast<BitmapTexture*>(textures_->fetch_entry(checksum));
    if (!texture)
        texture = alloc_pc_texture();
    texture->BitmapTexture::fabricate(checksum, width, height, bits_per_pixel, a4, a5);
    return texture;
}

// 0x004C8090
bool PcTexDict::destroy_texture(BitmapTexture* texture)
{
    delete texture; // (through its vtable)
    return true;
}

// 0x004C9C80
BitmapTexture* PcTexDict::duplicate_texture(uint32_t checksum, BitmapTexture* source)
{
    auto* texture = new PcTexture(static_cast<const PcTexture*>(source));
    texture->get_texture()->checksum = checksum; // (no check, as the original)
    return texture;
}

namespace {

// The dictionary's table remade for `count` textures: 2^bits buckets, bits the first with 2^bits at least twice the
// count (1-12), in the heap the old table was in (the old one freed).
lists::KeyMap* remake_table(lists::KeyMap* table, uint32_t count)
{
    uint32_t size = 2, bits = 1;
    uint32_t wanted = count * 2;
    if (wanted > size) {
        do {
            if (bits >= 12)
                break;
            size <<= 1;
            ++bits;
        } while (wanted > size);
    }
    memory::MemSource* heap =
        memory::fetch_chunk_prefix(table)->allocator; // (read before the null test, as the original)
    if (table) {
        if (table->buckets_) {
            table->empty_table();
            memory::Hub::get_pull()->dealloc(table->buckets_);
            table->buckets_ = nullptr;
        }
        memory::Hub::get_pull()->dealloc(table);
    }
    memory::Hub::get_pull()->enter_heap_scope(heap);
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    auto* remade = memory ? new (memory) lists::KeyMap(static_cast<int>(bits)) : nullptr;
    memory::Hub::get_pull()->leave_heap_scope();
    return remade;
}

// A texture's format from its header: by depth (8 P8, 16 A1R5G5B5, 32 A8R8G8B8) or its compression (1, 2: DXT1, 5:
// DXT5); anything else keeps the last one's (the original's stack).
uint32_t pixel_layout(uint8_t compression, uint8_t depth, uint32_t last)
{
    if (compression) {
        if (compression == 1 || compression == 2)
            return D3DFMT_DXT1;
        if (compression == 5)
            return D3DFMT_DXT5;
        return last;
    }
    if (depth == 8)
        return D3DFMT_P8;
    if (depth == 16)
        return D3DFMT_A1R5G5B5;
    if (depth == 32)
        return D3DFMT_A8R8G8B8;
    return last;
}

// The texture under its checksum in the table.
void add_to_table(lists::KeyMap* table, TextureRef* texture)
{
    auto* holder = new PcTexture;
    holder->image_ = texture; // (no check, as the original)
    holder->checksum_ = texture->checksum;
    table->store_entry(texture->checksum, holder);
}

TextureRef* new_engine_texture()
{
    void* memory = memory::alloc(sizeof(TextureRef));
    return memory ? static_cast<TextureRef*>(memory)->make_new() : nullptr;
}

} // namespace

// 0x004CAE90
lists::KeyMap* __cdecl load_textures_from_file(const char* name, lists::KeyMap* table, bool remake)
{
    file::Hdl file = file::unseal(name, "rb");
    if (!file)
        return table;
    // The file read in order; its headers through texture_format's reader (read into a buffer first). Not original
    // - a level larger than its texture's (texture_format::level_size), or a file cut short, stops the dictionary
    // there: the original read any size into the surface.
    auto read = [file](void* into, int size, int count) {
        return file::get_take_in(into, size, count, file) == size * count; // (bytes read)
    };
    uint8_t bytes[texture_format::kTextureHeaderSize];
    texture_format::DictionaryHeader dictionary;
    util::ByteReader dictionary_reader(bytes, texture_format::kDictionaryHeaderSize);
    if (!read(bytes, texture_format::kDictionaryHeaderSize, 1) ||
        !texture_format::read_dictionary_header(dictionary_reader, &dictionary)) {
        file::shut(file);
        return table;
    }
    int count = dictionary.count;
    if (remake)
        table = remake_table(table, static_cast<uint32_t>(count));
    uint32_t format = 0; // (the original's is whatever its stack held until a texture sets it)
    for (int i = 0; i < count; ++i) {
        texture_format::TextureHeader header;
        util::ByteReader header_reader(bytes, sizeof(bytes));
        if (!read(bytes, sizeof(bytes), 1) || !texture_format::read_texture_header(header_reader, &header)) {
            debug::printf("%s: cut short at texture %d of %d\n", name, i, count);
            break;
        }
        TextureRef* texture = new_engine_texture();
        texture->checksum = header.checksum; // (no check, as the original)
        texture->width = static_cast<uint16_t>(header.width);
        texture->height = static_cast<uint16_t>(header.height);
        texture->levels = static_cast<uint8_t>(header.levels);
        texture->depth = static_cast<uint8_t>(header.depth);
        texture->unknowne_ = static_cast<uint8_t>(header.palette_depth);
        texture->unknownf_ = static_cast<uint8_t>(header.compression);
        format = pixel_layout(texture->unknownf_, texture->depth, format);
        if (texture->height < 4)
            texture->height = 4;
        auto** d3d = reinterpret_cast<IDirect3DTexture9**>(&texture->texture);
        bool damaged = false;
        if (header.palette_size) {
            // Palettized: an A8R8G8B8 texture made, the palette and the levels read - and dropped (the texture's
            // texels left as they are, as the original).
            texture->depth = 32;
            format = D3DFMT_A8R8G8B8;
            input_port()->CreateTexture(texture->width, texture->height, texture->levels, 0, D3DFMT_A8R8G8B8,
                                        D3DPOOL_MANAGED, d3d, nullptr);
            void* palette = memory::alloc(header.palette_size);
            read(palette, static_cast<int>(header.palette_size), 1);
            for (uint32_t level = 0; level < texture->levels && !damaged; ++level) {
                uint32_t size = 0;
                read(&size, 4, 1);
                if (size > texture_format::level_size(header, level)) {
                    damaged = true;
                    break;
                }
                D3DLOCKED_RECT rect;
                (*d3d)->LockRect(level, &rect, nullptr, 0);
                D3DSURFACE_DESC desc;
                (*d3d)->GetLevelDesc(level, &desc);
                void* texels = memory::alloc(size);
                read(texels, static_cast<int>(size), 1);
                memory::Hub::get_pull()->dealloc(texels);
                (*d3d)->UnlockRect(level);
            }
            memory::Hub::get_pull()->dealloc(palette);
        } else {
            input_port()->CreateTexture(texture->width, texture->height, texture->levels, 0,
                                        static_cast<D3DFORMAT>(format), D3DPOOL_MANAGED, d3d, nullptr);
            texture->palette = nullptr;
            for (uint32_t level = 0; level < texture->levels; ++level) {
                uint32_t size = 0;
                read(&size, 4, 1);
                if (size > texture_format::level_size(header, level)) {
                    damaged = true;
                    break;
                }
                D3DLOCKED_RECT rect;
                (*d3d)->LockRect(level, &rect, nullptr, 0);
                read(rect.pBits, static_cast<int>(size), 1);
                (*d3d)->UnlockRect(level);
            }
        }
        add_to_table(table, texture);
        if (damaged) {
            debug::printf("%s: texture %08x has a level larger than its size\n", name, header.checksum);
            break;
        }
    }
    file::shut(file);
    return table;
}

// 0x004CB420
lists::KeyMap* __cdecl load_textures_from_memory(const uint8_t** cursor, size_t size, lists::KeyMap* table,
                                                 bool remake)
{
    // Read through texture_format's reader, bounded by `size` (the cursor not moved on, as the original). Not
    // original - a texture or level past the end, or a level larger than its texture's, stops the dictionary there.
    util::ByteReader reader(*cursor, size);
    texture_format::DictionaryHeader dictionary;
    if (!texture_format::read_dictionary_header(reader, &dictionary))
        return table;
    int count = dictionary.count;
    if (remake)
        table = remake_table(table, static_cast<uint32_t>(count));
    uint32_t format = 0; // (as the file loader's)
    for (int i = count; i > 0; --i) {
        texture_format::TextureHeader header;
        if (!texture_format::read_texture_header(reader, &header)) {
            debug::printf("texture dictionary: cut short\n");
            break;
        }
        TextureRef* texture = new_engine_texture();
        texture->checksum = header.checksum;
        texture->levels = static_cast<uint8_t>(header.levels);
        texture->width = static_cast<uint16_t>(header.width);
        texture->height = static_cast<uint16_t>(header.height);
        texture->depth = static_cast<uint8_t>(header.depth);
        texture->unknowne_ = static_cast<uint8_t>(header.palette_depth);
        texture->unknownf_ = static_cast<uint8_t>(header.compression);
        format = pixel_layout(static_cast<uint8_t>(header.compression), static_cast<uint8_t>(header.depth), format);
        auto** d3d = reinterpret_cast<IDirect3DTexture9**>(&texture->texture);
        input_port()->CreateTexture(static_cast<uint16_t>(header.width), static_cast<uint16_t>(header.height),
                                    static_cast<uint8_t>(header.levels), 0, static_cast<D3DFORMAT>(format),
                                    D3DPOOL_MANAGED, d3d, nullptr);
        // (no palette read: the memory loader expects none, as the original)
        if (!header.palette_size)
            texture->palette = nullptr;
        bool damaged = false;
        for (uint32_t level = 0; level < texture->levels; ++level) {
            const uint8_t* texels = nullptr;
            uint32_t level_bytes = 0;
            if (!texture_format::read_level(reader, &texels, &level_bytes) ||
                level_bytes > texture_format::level_size(header, level)) {
                damaged = true;
                break;
            }
            D3DLOCKED_RECT rect;
            (*d3d)->LockRect(level, &rect, nullptr, 0);
            std::memcpy(rect.pBits, texels, level_bytes);
            (*d3d)->UnlockRect(level);
        }
        add_to_table(table, texture);
        if (damaged) {
            debug::printf("texture dictionary: texture %08x damaged\n", header.checksum);
            break;
        }
    }
    return table;
}

namespace {

// Whether the table has any textures (iterated from the start).
bool has_textures(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    int count = 0;
    while (table->step_iterator() && table->iter_item_ && table->iter_item_->value)
        ++count;
    return count != 0;
}

} // namespace

// 0x004CB380
bool PcTexDict::ingest_file(const char* name)
{
    auto* table = textures_;
    if (!table) {
        load_textures_from_file(name, nullptr, false);
    } else if (!has_textures(table)) {
        textures_ = load_textures_from_file(name, table, true);
    } else {
        load_textures_from_file(name, table, false);
    }
    return true;
}

// 0x004CB7C0
bool PcTexDict::load_memory(const uint8_t* data, size_t size)
{
    auto* table = textures_;
    if (!table) {
        load_textures_from_memory(&data, size, nullptr, false);
    } else if (!has_textures(table)) {
        textures_ = load_textures_from_memory(&data, size, table, true);
    } else {
        load_textures_from_memory(&data, size, table, false);
    }
    return true;
}

// 0x004CB760
PcTexDict::PcTexDict(const char* name) : TextureSet(name, true) { ingest_file(name); }

// 0x004C7270
PcTexDict* __cdecl load_tex_dict_file(const char* name) { return new PcTexDict(name); }

// 0x004C72E0
PcTexDict* __cdecl load_tex_dict_memory(uint32_t checksum, const uint8_t* data, size_t size)
{
    auto* dict = new PcTexDict(checksum);
    dict->load_memory(data, size); // (called on null without memory, as the original)
    return dict;
}

// 0x004C7360
PcTexDict* __cdecl create_tex_dict(uint32_t checksum) { return new PcTexDict(checksum); }

// 0x004C80B0
void __cdecl copy_sub_image(uint8_t* into, uint16_t x, uint16_t y, uint16_t into_width, uint16_t into_height,
                            const uint8_t* from, uint16_t from_width, uint16_t from_height, uint16_t bits_per_pixel)
{
    int columns = from_width;
    if (columns >= into_width - x)
        columns = into_width - x;
    int rows = from_height < into_height - y ? from_height : into_height - y;
    int offset = y * into_width;
    uint32_t bytes_per_pixel = bits_per_pixel >> 3;
    int row_bytes;
    uint32_t from_stride, into_stride;
    if (bytes_per_pixel) {
        row_bytes = columns * static_cast<int>(bytes_per_pixel);
        from_stride = from_width * bytes_per_pixel;
        into += (offset + x) * static_cast<int>(bytes_per_pixel);
        into_stride = into_width * bytes_per_pixel;
    } else { // (4 bits: two pixels a byte)
        into += (offset + x) / 2;
        row_bytes = columns / 2;
        from_stride = from_width >> 1u;
        into_stride = into_width >> 1u;
    }
    for (; rows > 0; --rows) {
        std::memcpy(into, from, static_cast<uint32_t>(row_bytes));
        from += from_stride;
        into += into_stride;
    }
}

// 0x004C8180
void __cdecl rgb_to_hsv(float r, float g, float b, float* h, float* s, float* v)
{
    float low = g < b ? g : b;
    if (r < low)
        low = r;
    float high = g > b ? g : b;
    if (r > high)
        high = r;
    *v = high;
    float delta = static_cast<float>(static_cast<double>(high) - low);
    if (high == 0.0f) {
        *s = 0.0f;
        *h = -1.0f;
        return;
    }
    *s = static_cast<float>(static_cast<double>(delta) / high);
    if (delta == 0.0f)
        return;
    double hue;
    if (r == high)
        hue = (static_cast<double>(g) - b) / delta;
    else if (g == high)
        hue = (static_cast<double>(b) - r) / delta + 2.0f;
    else
        hue = (static_cast<double>(r) - g) / delta + 4.0f;
    *h = static_cast<float>(hue);
    double degrees = static_cast<double>(*h) * 60.0f;
    *h = static_cast<float>(degrees);
    if (degrees < 0.0)
        *h = static_cast<float>(degrees + 360.0f);
}

// 0x004C82D0
void __cdecl hsv_to_rgb(float* r, float* g, float* b, float h, float s, float v)
{
    if (s == 0.0f) {
        *b = v;
        *g = v;
        *r = v;
        return;
    }
    double sector = static_cast<double>(h) * (1.0f / 60.0f);
    int i = static_cast<int>(sector);
    double f = sector - i;
    float p = static_cast<float>((1.0f - static_cast<double>(s)) * v);
    float q = static_cast<float>((1.0f - s * f) * v);
    float t = static_cast<float>((1.0f - (1.0f - f) * s) * v);
    switch (static_cast<uint32_t>(i)) {
    case 0:
        *r = v, *g = t, *b = p;
        break;
    case 1:
        *r = q, *g = v, *b = p;
        break;
    case 2:
        *r = p, *g = v, *b = t;
        break;
    case 3:
        *r = p, *g = q, *b = v;
        break;
    case 4:
        *r = t, *g = p, *b = v;
        break;
    default:
        *r = v, *g = p, *b = q;
        break;
    }
}

} // namespace render
