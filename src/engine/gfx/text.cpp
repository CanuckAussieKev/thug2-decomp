// Bitmap font text: measuring and drawing, with PartyMod's button glyphs (engine/gfx/glyphs.h) built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/gfx/text.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>
#include <array>

#include "engine/core/cursor.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/file/file_load.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"
#include "formats/byte_stream.h"
#include "formats/font_format.h"
#include "engine/gfx/glyphs.h"
#include "engine/gfx/hd_ui.h"
#include "engine/gfx/render_state.h"
#include "engine/pad_input/device.h"
#include "platform/platform_d3d.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/screen_2d.h"
#include "engine/gfx/engine_parts.h"

namespace render {

namespace {
// NOT ORIGINAL grouping - text.cpp's state (the original's globals). Made on first use, never destroyed (text_state()).
struct TextState {
    // The vertices' store (0x7000 bytes, as words) and the quads' triangle indices (256 quads of six).
    std::array<TextVertex, 0x1C00 * 4 / sizeof(TextVertex)> text_vertex_store_{}; // (0x7000 bytes)
    std::array<uint16_t, 0x100 * 6> text_quad_indices_{};
    TypefaceData* typeface_head_{};
    TypefaceData* texture_font_{};
    uint32_t text_vertex_count_{};
    bool quad_indices_made_{};
};

TextState& text_state()
{
    static core::NoDestroy<TextState> state;
    return state.get();
}

} // namespace

namespace {

constexpr float get_bits(uint32_t bits) { return std::bit_cast<float>(bits); }
constexpr float kEffectScale = get_bits(0x38800000); // 0x006487C8, 1 / 16384
constexpr float kMinusHalf = get_bits(0xBF000000);   // 0x0064642C

// The 640x480 -> back buffer conversion (set up by init_direct_3d).

// How many vertices the text vertex store holds (the original's buffer pointer, open_sketch's 0x00779FD8, always
// pointed at the store's start).
uint32_t& get_vertex_count() { return text_state().text_vertex_count_; }
// The place of the next quad's four vertices in the store.
core::Cursor<TextVertex> next_quad() { return core::Cursor<TextVertex>(std::span<TextVertex>(text_state().text_vertex_store_), static_cast<std::ptrdiff_t>(get_vertex_count())); }
constexpr uint32_t kVertexLimit = 0x400;
// The font button glyphs come from, and the font whose texture the buffer is drawn with.

TypefaceData*& get_texture_font() { return text_state().texture_font_; }

// A button tag's digit through the meta button map (0x0048D6A0, also used by 0x004AF8C0; not rewritten yet).
char meta_button(char digit) { return render::button_glyph(digit); }
// The name of the key a button is mapped to.
const char* button_key_name(int button) { return sio::get_button_key_name(button); }

// "\b<d>" / "\m<d>" (d the digit): which button.
int glyph_for_button_tag(char tag, char digit)
{
    if (tag == 'M' || tag == 'm') {
        const char mapped = meta_button(digit);
        return text::hex_digit_value(mapped);
    }
    // Originally from PartyMod Fixes (its call at 0x004CED4B / 0x004CFF0C): with a controller (or PreferGlyphs) the PC's
    // explicit button tags are turned back into the controller's. The original: TextUtil::hex_digit_value(digit).
    return static_cast<int>(button_for_tag(digit));
}

// Whether a button is drawn as a glyph of the button font (otherwise its key's name is spelled out).
bool use_button_glyph(int button)
{
    // Originally from PartyMod Fixes (its test at 0x004CED6D / 0x004CFF36). The original, where 4-7 and 10-13 are the glyphs:
    //   uint8_t b = static_cast<uint8_t>(button);
    //   return (b >= 4 && b <= 7) || (b >= 10 && b <= 13);
    return should_use_glyph(static_cast<uint8_t>(button)) == 1;
}

// RGBA bytes (as stored) -> the vertex's ARGB: red and blue swap.
uint32_t get_vertex_color(uint32_t c) { return (c & 0xFF) << 16 | (c & 0xFF00FF00) | (c >> 16 & 0xFF); }

// The CRT's _ftol (0x006236FC): truncation; the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x004CECB0
void TypefaceData::read_string_param(const char* text, float* out_width, float* out_height)
{
    int total = 0;
    core::CStringCursor p(text);
    for (;;) {
        const Char* glyph = nullptr; // set by an escape that draws a glyph in place of the next character
        const TypefaceData* font = this;
        bool counted = true;
        bool done = false;
        while (*p == '\\' && !done) {
            ++p;
            const char tag = *p;
            if (tag == '\\')
                break; // "\\": the second backslash is a character
            switch (tag) {
            case 'S':
            case 's':
                ++p;
                glyph = &this->glyph(core::at(special_map_, text::hex_digit_value(*p)));
                done = true;
                break;
            case 'B':
            case 'b':
            case 'M':
            case 'm': {
                ++p;
                const int button = glyph_for_button_tag(tag, *p);
                if (use_button_glyph(button)) {
                    font = screen_2d().pad_glyph_typeface();
                    glyph = &font->glyph(core::at(font->special_map_, button));
                } else {
                    // the key's name, in this font (the map indexed by signed chars)
                    const auto& name_map = map;
                    for (const char name : std::string_view(button_key_name(button)))
                        total += this->glyph(core::at(name_map, static_cast<signed char>(name))).w + glyph_gap_;
                    counted = false; // not the digit
                }
                done = true;
                break;
            }
            case 'C':
            case 'c':
            case 'U':
            case 'u':
                p += 2;
                break;
            case 'L': // (only capital: "\l" is drawn as an l)
                ++p;
                break;
            default:
                break; // the letter is a character (the table's '\\' case can't be reached)
            }
        }
        if (*p == '\0')
            break;
        if (counted) {
            int w = 0;
            if (glyph)
                w = glyph->w;
            else if (*p == ' ')
                w = blank_gap_;
            else
                w = font->glyph(core::at(font->map, static_cast<uint8_t>(*p))).w;
            total += w + glyph_gap_;
        }
        ++p;
    }
    *out_width = static_cast<float>(total);
    *out_height = static_cast<float>(static_cast<double>(height));
}

// 0x004CFB20
void GlyphRun::draw_key_char(char c, float* pen_x, float pen_y)
{
    const double scale_x = renderer().display_factor_x();
    const double scale_y = renderer().display_factor_y();
    auto spacing = static_cast<float>(font->glyph_gap_ * static_cast<double>(xscale));
    if (c == ' ') {
        *pen_x = static_cast<float>((font->blank_gap_ * static_cast<double>(xscale) + spacing) * scale_x + *pen_x);
        return;
    }
    const Char* glyph = &font->glyph(core::at(font->map, static_cast<uint8_t>(c)));
    // (x87: top and bottom stay at double precision until stored)
    const double top = static_cast<double>(font->base - glyph->baseline) * yscale * scale_y + pen_y;
    const float left = *pen_x;
    auto right = static_cast<float>(glyph->w * scale_x * xscale + left);
    const double bottom = glyph->h * scale_y * yscale + top;
    const uint32_t vertex_color = get_vertex_color(color);
    const core::Cursor<TextVertex> v = next_quad();
    v[0] = {.x = left, .y = static_cast<float>(top), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u0, .v = glyph->v0};
    v[1] = {.x = left, .y = static_cast<float>(bottom), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u0, .v = glyph->v1};
    v[2] = {.x = right, .y = static_cast<float>(bottom), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u1, .v = glyph->v1};
    v[3] = {.x = right, .y = static_cast<float>(top), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u1, .v = glyph->v0};
    get_vertex_count() += 4;
    if (get_vertex_count() >= kVertexLimit) {
        close_sketch();
        open_sketch();
    }
    *pen_x = static_cast<float>(scale_x * spacing + right);
}

// 0x004CFD00
void GlyphRun::draw()
{
    const double scale_x = renderer().display_factor_x();
    const double scale_y = renderer().display_factor_y();
    const double offset_x = renderer().screen_offset_x();
    const double offset_y = renderer().screen_offset_y();
    // Screen positions (x87: each sum rounded to float once, when stored).
    auto pos_x = static_cast<float>(scale_x * x + offset_x);
    auto pos_y = static_cast<float>(scale_y * y + offset_y);
    auto pivot_screen_x = static_cast<float>((static_cast<double>(pivot_x_) + x) * scale_x + offset_x);
    auto pivot_screen_y = static_cast<float>((static_cast<double>(pivot_y_) + y) * scale_y + offset_y);
    auto glyph_gap = static_cast<float>(font->glyph_gap_ * static_cast<double>(xscale));
    auto blank_gap = static_cast<float>(font->blank_gap_ * static_cast<double>(xscale));
    if (xscale == 0.0f || yscale == 0.0f) // (0x0066D520)
        return;
    const float cos_a = std::cos(angle);
    const float sin_a = std::sin(angle);
    uint32_t vertex_color = get_vertex_color(color);
    int effect = 0; // byte offset of this character's effect record
    core::CStringCursor p(text);
    // (the effects array: one record per character of the string's buffer)
    const std::span<const TextEffect> effect_records = effects ? std::span<const TextEffect>(effects, kCharEffects) : std::span<const TextEffect>();
    for (;;) {
        core::Cursor<TextVertex> v = next_quad();
        const TypefaceData* char_font = font;
        const Char* glyph = nullptr;
        bool drawn = true;
        bool done = false;
        while (*p == '\\' && !done) {
            ++p;
            const char tag = *p;
            switch (tag) {
            case '\\':
                done = true; // the second backslash is drawn
                break;
            case 'C':
            case 'c': {
                ++p;
                const int index = text::hex_digit_value(*p);
                ++p;
                // colour 0, or any colour while the text's is fixed, is the text's own
                vertex_color = get_vertex_color(index != 0 && !fixed_color_ ? font->get_color(index) : color);
                break;
            }
            case 'S':
            case 's':
                ++p;
                glyph = &font->glyph(core::at(font->special_map_, text::hex_digit_value(*p)));
                done = true;
                break;
            case 'B':
            case 'b':
            case 'M':
            case 'm': {
                ++p;
                const int button = glyph_for_button_tag(tag, *p);
                if (use_button_glyph(button)) {
                    // drawn with the button font's texture: draw what's buffered first
                    TypefaceData* buttons = screen_2d().pad_glyph_typeface();
                    glyph = &buttons->glyph(core::at(buttons->special_map_, button));
                    char_font = buttons;
                    close_sketch();
                    open_sketch();
                    v = next_quad();
                    get_texture_font() = buttons;
                } else {
                    close_sketch();
                    open_sketch();
                    v = next_quad();
                    drawn = false; // not the digit
                    for (const char name : std::string_view(button_key_name(button)))
                        draw_key_char(name, &pos_x, pos_y);
                }
                done = true;
                break;
            }
            case 'L': // (only capital: "\l" is drawn as an l)
                ++p;
                break;
            case 'U':
            case 'u':
                p += 2;
                break;
            default:
                break; // the letter is drawn
            }
        }
        if (*p == '\0')
            return;
        if (drawn) {
            if (*p == ' ' && !glyph) {
                pos_x = static_cast<float>((static_cast<double>(blank_gap) + glyph_gap) * scale_x + pos_x);
                ++p;
                effect += sizeof(TextEffect);
                continue;
            }
            if (!glyph)
                glyph = &char_font->glyph(core::at(char_font->map, static_cast<uint8_t>(*p)));
            auto top =
                static_cast<float>(static_cast<double>(char_font->base - glyph->baseline) * yscale * scale_y + pos_y);
            const TextEffect* e =
                effects ? &core::at(effect_records, effect / static_cast<int>(sizeof(TextEffect))) : nullptr;
            float right = 0.0f, bottom = 0.0f;
            if (e) {
                const int w = ftol(e->xscale * static_cast<double>(kEffectScale) * glyph->w * xscale * scale_x);
                right = static_cast<float>(static_cast<double>(w) + e->x + pos_x);
                const int h = ftol(e->yscale * static_cast<double>(kEffectScale) * glyph->h * yscale * scale_y);
                bottom = static_cast<float>(static_cast<double>(h) + e->y + top);
            } else {
                const int w = ftol(static_cast<double>(glyph->w) * xscale * scale_x);
                right = static_cast<float>(static_cast<double>(w) + pos_x);
                const int h = ftol(static_cast<double>(glyph->h) * yscale * scale_y);
                bottom = static_cast<float>(static_cast<double>(h) + top);
            }

            // The corners, relative to the pivot (whole pixels), rotated by the text's angle. The left and top
            // offsets go through floats; the right and bottom ones don't (the same unless they're huge).
            const int pivot_yi = ftol(pivot_screen_y);
            const int top_rel = ftol(static_cast<double>(top) - pivot_yi);
            const int pivot_xi = ftol(pivot_screen_x);
            const int left_rel = ftol(static_cast<double>(pos_x) - pivot_xi);
            double l = static_cast<float>(left_rel);
            const double t = static_cast<float>(top_rel);
            int x0 = ftol(l * cos_a - t * sin_a) + pivot_xi; // left, top
            int y0 = ftol(t * cos_a + l * sin_a) + pivot_yi;
            const double r = ftol(static_cast<double>(right) - pivot_xi);
            int x1 = ftol(r * cos_a - t * sin_a) + pivot_xi; // right, top
            int y1 = ftol(t * cos_a + r * sin_a) + pivot_yi;
            const double b = ftol(static_cast<double>(bottom) - pivot_yi);
            l = left_rel;
            int x2 = ftol(l * cos_a - b * sin_a) + pivot_xi; // left, bottom
            int y2 = ftol(b * cos_a + l * sin_a) + pivot_yi;
            int x3 = ftol(r * cos_a - b * sin_a) + pivot_xi; // right, bottom
            int y3 = ftol(b * cos_a + r * sin_a) + pivot_yi;

            if (e && e->rotation != 0) {
                // The character's own rotation about its centre - as the original computes it: the centre is
                // negated and offset by the pivot's x on both axes, then turned by the text's angle, and the
                // corners are turned by (angle - 90 degrees) mirrored. Kept as it is.
                double a = e->rotation * static_cast<double>(kEffectScale);
                auto cos_r = static_cast<float>(std::cos(a));
                const int cy = ftol((static_cast<double>(bottom) + top) * kMinusHalf);
                const double oy = static_cast<float>(-pivot_xi - cy);
                const int cx = ftol((static_cast<double>(right) + pos_x) * kMinusHalf);
                const double ox = static_cast<float>(-pivot_xi - cx);
                int centre_x = ftol(ox * cos_a - oy * sin_a) + pivot_xi;
                int centre_y = ftol(oy * cos_a + ox * sin_a) + pivot_xi;
                auto turn = [&](int& px, int& py) {
                    auto dx = static_cast<float>(px - centre_x);
                    auto dy = static_cast<float>(py - centre_y);
                    px = ftol(std::sin(a) * dx - static_cast<double>(dy) * cos_r) + centre_x;
                    py = ftol(std::sin(a) * dy + static_cast<double>(dx) * cos_r) + centre_y;
                };
                turn(x0, y0);
                turn(x1, y1);
                turn(x2, y2);
                turn(x3, y3);
            }

            auto at = [](int i) { return static_cast<float>(i); };
            v[0] = {.x = at(x0), .y = at(y0), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u0, .v = glyph->v0};
            v[1] = {.x = at(x2), .y = at(y2), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u0, .v = glyph->v1};
            v[2] = {.x = at(x3), .y = at(y3), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u1, .v = glyph->v1};
            v[3] = {.x = at(x1), .y = at(y1), .z = z, .rhw = 1.0f, .color = vertex_color, .u = glyph->u1, .v = glyph->v0};
            get_vertex_count() += 4;
            if (get_vertex_count() >= kVertexLimit) {
                close_sketch();
                open_sketch();
            }
            pos_x = static_cast<float>(scale_x * glyph_gap + right);
            if (char_font != font) { // back to this font's texture
                close_sketch();
                open_sketch();
            }
        }
        ++p;
        effect += sizeof(TextEffect);
    }
}

namespace {

void set_texture_stage_state(int stage, int type, int value)
{
    renderer().d3d_device()->SetTextureStageState(static_cast<DWORD>(stage), static_cast<D3DTEXTURESTAGESTATETYPE>(type),
                                       static_cast<DWORD>(value));
}

// The vertices' store (open_sketch points the buffer at it) and the quads' triangles as indices into it (made the
// first time they're drawn).
std::array<TextVertex, 0x1C00 * 4 / sizeof(TextVertex)>& vertex_store() { return text_state().text_vertex_store_; }
std::array<uint16_t, 0x100 * 6>& quad_indices() { return text_state().text_quad_indices_; }
bool& quad_indices_made() { return text_state().quad_indices_made_; }
constexpr int kQuads = 0x100;

// The loaded fonts (through TypefaceData::next).
TypefaceData*& typeface_chain() { return text_state().typeface_head_; }

uint16_t u16(const uint8_t* p)
{
    uint16_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}
uint32_t get_u32(const uint8_t* p)
{
    uint32_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

} // namespace

// 0x004CEF00
void GlyphRun::open_sketch()
{
    // (the original pointed the vertex buffer at the store's start; the quads now go to the store at the count)
}

// 0x004CEF10
void GlyphRun::close_sketch()
{
    if (!quad_indices_made()) {
        for (int quad = 0; quad < kQuads; ++quad) {
            auto first = static_cast<uint16_t>(quad * 4);
            const core::Cursor<uint16_t> triangles(std::span<uint16_t>(quad_indices()), quad * 6);
            triangles[0] = first;
            triangles[1] = static_cast<uint16_t>(first + 1);
            triangles[2] = static_cast<uint16_t>(first + 2);
            triangles[3] = first;
            triangles[4] = static_cast<uint16_t>(first + 2);
            triangles[5] = static_cast<uint16_t>(first + 3);
        }
        quad_indices_made() = true;
    }
    const int count = static_cast<int>(get_vertex_count());
    if (count <= 0)
        return;
    const TypefaceData* texture_font = get_texture_font() ? get_texture_font() : font;
    set_pixel_shader(nullptr, nullptr); // (no pixel shader)
    set_texture_stage_state(0, 1, 5);   // colour: texture x diffuse, doubled
    set_texture_stage_state(0, 2, 2);
    set_texture_stage_state(0, 3, 1);
    set_texture_stage_state(0, 4, 5); // alpha: the same
    set_texture_stage_state(0, 5, 2);
    set_texture_stage_state(0, 6, 1);
    set_texture_stage_state(1, 1, 1);      // stage 1 off
    set_vertex_format(0x144);              // position (pre-transformed), colour, one texture coordinate
    set_texture(0, texture_font->texture); // (the original also passes unknown17c_, which SetTexture doesn't take)
    renderer().d3d_device()->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    if (!engine_parts().suppress_draw()) {                                                      // (rendering not skipped)
        renderer().count_draw(count / 2); // the frame's draw calls and triangles
        renderer().d3d_device()->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, count, count / 2, quad_indices().data(), D3DFMT_INDEX16,
                                             vertex_store().data(), sizeof(TextVertex));
    }
    set_texture_stage_state(0, 1, 4); // colour and alpha back to texture x diffuse
    set_texture_stage_state(0, 4, 4);
    get_vertex_count() = 0;
    get_texture_font() = nullptr;
}

// 0x004CF0F0. The file's format: formats/font_format.h (a header, the glyphs' character map entries, the page's
// header, its texels and palette, the glyphs' rectangles).
TypefaceData* TypefaceData::read_in(const char* name)
{
    // Not original - the whole file read first and parsed through font_format's readers (the original read it piece
    // by piece into a 0x808-byte stack buffer: a font of more than 256 glyphs overflowed it); a font cut short, with
    // more than 256 glyphs or no file is not loaded (null); a special glyph past the special map's 0x20 is left
    // unmapped (the original wrote it past the map, as far as the font's next pointer and colours).
    // (the original could also parse a font already in memory: nothing asks for that)
    std::vector<uint8_t> contents;
    std::array<char, 128> path{}; // (the original strcpy/strcats into it: a longer name overflows; here it's cut short)
    text::format_to(path, "fonts/%s.fnt.xbx", name);
    file::Hdl file = file::unseal(path.data(), "rb");
    if (!file) {
        debug::printf("font %s: no file\n", path.data());
        return nullptr;
    }
    contents.resize(file::get_file_size(file));
    const int read =
        contents.empty() ? 0 : file::get_take_in(contents.data(), static_cast<int>(contents.size()), 1, file);
    file::shut(file);
    contents.resize(read > 0 ? static_cast<size_t>(read) : 0);
    util::ByteReader reader(contents.data(), contents.size());
    font_format::Header header;
    std::array<font_format::GlyphEntry, font_format::kMaxGlyphs> entries;
    font_format::Page page;
    if (!font_format::read_header(reader, &header) ||
        !font_format::read_glyph_entries(reader, header.glyph_count, entries) ||
        !font_format::read_page(reader, &page)) {
        debug::printf("font %s damaged: cut short or %d glyphs\n", name, header.glyph_count);
        return nullptr;
    }
    // Not original - HD menu UI (engine/gfx/hd_ui.h): an upscaled page has 32-bit BGRA texels and no palette; its
    // glyph rectangles are in the original page's pixels.
    const uint32_t hd_scale = hd_ui::font_scale(page.hd_mark, page.hd_scale);
    const uint32_t size = font_format::texel_count(page);
    const std::span<const uint8_t> page_bytes = reader.rest();
    reader.skip(hd_scale != 1 ? static_cast<size_t>(size) * 4 : size + font_format::kPaletteSize);
    std::array<font_format::GlyphRect, font_format::kMaxGlyphs> rects;
    if (!font_format::read_glyph_rects(reader, header.glyph_count, rects)) {
        debug::printf("font %s damaged: its page or rectangles cut short\n", name);
        return nullptr;
    }
    const int count = header.glyph_count;
    // (the skip above passed: the page's texels - and, in the original page, its palette - are in the file)
    const std::span<const uint8_t> texels = page_bytes.first(hd_scale != 1 ? static_cast<size_t>(size) * 4 : size);
    const std::span<const uint8_t> palette_bytes = hd_scale != 1 ? std::span<const uint8_t>() : page_bytes.subspan(size, font_format::kPaletteSize);

    auto* font = static_cast<TypefaceData*>(memory::alloc(sizeof(TypefaceData)));
    if (font)
        std::memset(font, 0, sizeof(TypefaceData));
    font->format = header.format;
    font->height = header.height;
    font->base = header.base;
    std::memset(font->map.data(), 0, sizeof(font->map));
    std::memset(font->special_map_.data(), 0, sizeof(font->special_map_));
    font->chars = static_cast<Char*>(memory::alloc(static_cast<uint32_t>(count) * sizeof(Char)));
    font->glyph_count_ = static_cast<uint16_t>(count);

    // The character map: which glyph each character (and special glyph) is.
    if (font->format == 1) {
        // Each glyph: its baseline, then a character (>= 0) or a special glyph (-1 - index).
        for (int i = 0; i < count; ++i) {
            font->glyph(static_cast<std::size_t>(i)).baseline = core::at(entries, i).baseline;
            const int16_t code = core::at(entries, i).code();
            if (code >= 0) {
                core::at(font->map, code & 0xFF) = static_cast<uint8_t>(i);
            } else {
                const int special = 0xFF - (code & 0xFF);
                if (special < font_format::kSpecialGlyphs)
                    core::at(font->special_map_, special) = static_cast<uint8_t>(i);
            }
        }
    } else if (font->format == 2) {
        // Each glyph: its baseline, then up to four characters (0xFF ends them early).
        for (int i = 0; i < count; ++i) {
            font->glyph(static_cast<std::size_t>(i)).baseline = core::at(entries, i).baseline;
            const auto& codes = core::at(entries, i).codes;
            core::at(font->map, core::at(codes, 0)) = static_cast<uint8_t>(i);
            for (int c = 1; c < 4 && core::at(codes, c) != 0xFF; ++c)
                core::at(font->map, core::at(codes, c)) = static_cast<uint8_t>(i);
        }
    }
    // Characters and special glyphs without one get the default glyph (glyph 0 counts as none, but for 'A'/'a').
    if (core::at(font->special_map_, 0x1F)) {
        for (int c = 0; c < 256; ++c) {
            if (!core::at(font->map, c) && c != 'A' && c != 'a')
                core::at(font->map, c) = core::at(font->special_map_, 0x1F);
            if (c < 0x1F && !core::at(font->special_map_, c))
                core::at(font->special_map_, c) = core::at(font->special_map_, 0x1F);
        }
    }

    // The texture: created on the device, its palette's colours (RGBA, PS2 alpha) made ARGB with alpha doubled
    // (0x80 and over: opaque), and the pixels written through it (the rows packed: the pitch isn't used).
    const uint32_t width = page.width;
    const uint32_t height = page.height;
    if (hd_scale != 1)
        hd_ui::note_font(name, hd_scale, width, height);
    IDirect3DTexture9* texture = nullptr;
    const HRESULT created =
        renderer().d3d_device()->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
    font->texture = texture;
    if (created != D3D_OK)
        return nullptr;
    // (not original - both buffers owned: the original left them allocated when the texture couldn't be locked)
    memory::HeapPtr<uint8_t> indices;
    memory::HeapPtr<uint32_t> game_pixels;
    std::vector<uint32_t> hd_pixels;
    uint32_t* pixels = nullptr;
    if (hd_scale != 1) {
        // (not original - HD menu UI: the texels as they are, in a buffer from the C runtime's heap - 4-16 MB, more
        // than the game's heap may have free while the front end loads)
        hd_pixels.resize(size);
        pixels = hd_pixels.data();
        std::memcpy(pixels, texels.data(), size * 4);
    } else {
        indices = memory::heap_ptr<uint8_t>(memory::alloc(size));
        std::array<uint32_t, 256> palette{};
        std::memcpy(indices.get(), texels.data(), size);
        std::memcpy(palette.data(), palette_bytes.data(), font_format::kPaletteSize);
        for (uint32_t& color : palette) {
            const uint32_t argb = (color >> 16 & 0xFF) | (color & 0xFF) << 16 | (color & 0xFF00FF00);
            uint32_t alpha = argb >> 24;
            alpha = alpha >= 0x80 ? 0xFF : alpha << 1;
            color = alpha << 24 | (argb & 0xFFFFFF);
        }
        game_pixels = memory::heap_ptr<uint32_t>(memory::alloc(size * 4));
        pixels = game_pixels.get();
        // palette indices -> colours
        os_layer::expand_palette(pixels, indices.get(), static_cast<int>(size), palette.data());
    }
    D3DLOCKED_RECT locked;
    if (texture->LockRect(0, &locked, nullptr, 0) != D3D_OK)
        return nullptr;
    std::memcpy(locked.pBits, pixels, size * 4);
    texture->UnlockRect(0);
    indices.reset();
    game_pixels.reset();

    // The glyphs' rectangles: texture coordinates (the right/bottom ones from the unrounded left/top) and sizes.
    // (not original - HD menu UI: an upscaled page's size divided by its scale, the rectangles' own pixels)
    const double texels_wide = static_cast<int>(width / hd_scale);
    const double texels_high = static_cast<int>(height / hd_scale);
    for (int i = 0; i < count; ++i) {
        const font_format::GlyphRect& rect = core::at(rects, i);
        Char& glyph = font->glyph(static_cast<std::size_t>(i));
        glyph.w = rect.w;
        glyph.h = rect.h;
        const double u0 = rect.x / texels_wide;
        const double v0 = rect.y / texels_high;
        glyph.u0 = static_cast<float>(u0);
        glyph.v0 = static_cast<float>(v0);
        glyph.u1 = static_cast<float>(glyph.w / texels_wide + u0);
        glyph.v1 = static_cast<float>(glyph.h / texels_high + v0);
    }

    font->next = typeface_chain();
    typeface_chain() = font;
    font->blank_gap_ = static_cast<int16_t>(font->glyph(core::at(font->map, 'I')).w);
    return font;
}

// 0x004CFA80
void TypefaceData::offload(TypefaceData* font)
{
    if (typeface_chain() == font) {
        typeface_chain() = font->next;
    } else {
        for (TypefaceData* p = typeface_chain(); p->next; p = p->next) {
            if (p->next == font) {
                p->next = font->next;
                break;
            }
        }
    }
    memory::dealloc(font->chars);
    memory::dealloc(font);
}

} // namespace render
