// NOT ORIGINAL - the new front end's text (ours): colours, the baked faces and their metrics, the fonts built at a
// scale, and the styled text's measuring, wrapping and drawing. See new_frontend_draw.h.

#include "frontend/new_frontend_draw.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <array>
#include <vector>
#include "frontend/new_frontend_model.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec2.h"
#include "base/maths/maths.h"
#include "frontend/new_frontend_draw_internal.h"

namespace frontend::new_frontend::paint {
// What the source file's anonymous namespace held that its pieces share (new_frontend_draw_internal.h): visible as it
// was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend::paint

namespace frontend::new_frontend::paint {
namespace {

// --- layout numbers, in design pixels unless the name says otherwise; grouped by the screen or element they place ---

// Bits and bytes: shifts and masks that take a colour channel or a font file's big-endian field apart.
constexpr int kBitsPerByte = 8;
constexpr int kBitsPerTwoBytes = 16;
constexpr int kBitsPerThreeBytes = 24;
constexpr int kBitsPerWord = 32;
constexpr int kByteMask = 0xFF;
constexpr long kMinFontFileBytes = 12; // a font file smaller than its own header is not one
constexpr long kMaxFontFileMegabytes = 64;
constexpr long kBytesPerKilobyte = 1024;

// Glyph, arrow and art quads: the indices of a quad, how many copies the scrim draws, the shade and arrow edges.
constexpr int kGlyphQuadIndices = 6; // two triangles

// The font file reader: an sfnt's header, its table records and the tags of the tables read (the tags are their four
// letters as a big-endian number), the head table's size and where it keeps the units per em.
constexpr std::size_t kSfntHeaderBytes = 12;
constexpr uint32_t kSfntVersionTrueType = 0x00010000;
constexpr uint32_t kTagTrue = 0x74727565; // true
constexpr uint32_t kTagOtto = 0x4F54544F; // OTTO
constexpr std::size_t kSfntTableRecordBytes = 16;
constexpr std::size_t kHeadTableBytes = 54;
constexpr uint32_t kTagHead = 0x68656164; // head
constexpr uint32_t kTagHhea = 0x68686561; // hhea
constexpr std::size_t kHeadUnitsPerEmOffset = 18;

// The font loader's limits (the ini's condense and bold), and the letterbox bars' threshold.
constexpr float kCondenseMin = 0.5F;
constexpr float kCondenseMax = 1.5F;
constexpr float kBoldMax = 0.1F;

} // namespace
namespace internal {

ImU32 get_col(uint32_t argb, float alpha)
{
    const uint32_t alpha_byte =
        static_cast<uint32_t>(std::clamp(static_cast<float>((argb >> 24) & 0xFF) * alpha, 0.0F, 255.0F));
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, alpha_byte);
}

uint32_t get_mix(uint32_t a, uint32_t b, float blend)
{
    blend = std::clamp(blend, 0.0F, 1.0F);
    uint32_t out = 0;
    for (int shift = 0; shift < kBitsPerWord; shift += kBitsPerByte) {
        auto x = static_cast<float>((a >> shift) & kByteMask);
        auto y = static_cast<float>((b >> shift) & kByteMask);
        out |= maths::round_non_negative<uint32_t>(x + ((y - x) * blend)) << shift;
    }
    return out;
}

} // namespace internal
namespace {

// --- the fonts ---

// The sizes baked (CSS px at 1280 x 720): the display face's 120 (the big names: the level select's 112 drawn from
// it), 78 (the main menu), 40 (the levels), 30 (Skate); the mono face's 12 and 13; the body's 14.
constexpr auto kBakes = std::to_array<Bake>({
    {.face = Face::kDisplay, .css = 120.0F},
    {.face = Face::kDisplay, .css = kMainType},
    {.face = Face::kDisplay, .css = kLevelType},
    {.face = Face::kDisplay, .css = 28.0F},
    {.face = Face::kMono, .css = 12.0F},
    {.face = Face::kMono, .css = 13.0F},
    {.face = Face::kBody, .css = 14.0F},
    {.face = Face::kMono, .css = 20.0F},
    {.face = Face::kBody, .css = 48.0F}, // (the restyled keyboard's keys and the text typed)
});
constexpr int kBakeCount = static_cast<int>(sizeof(kBakes) / sizeof(core::at(kBakes, 0)));

struct FaceLayout {
    std::vector<uint8_t> file; // (kept: the atlas doesn't own it - several sizes share it)
    float ascent = 0.8F, descent = -0.2F;
    bool ok = false;
};
std::array<FaceLayout, 3>& stored_faces()
{
    static core::NoDestroy<std::array<FaceLayout, 3>> faces;
    return faces.get();
}

struct Baked {
    ImFont* font = nullptr;
    Face face = Face::kBody;
    float em_px_ = 0.0F; // the em in window px it was baked for
};
// NOT ORIGINAL grouping - new_frontend_draw.cpp's state (were globals). Made on first use, never destroyed
// (new_frontend_draw_state()).
struct NewFrontendDrawState {
    std::array<Baked, kBakeCount> baked_{};
    float scale_ = 0.0F;
    ImFontAtlas* atlas_ = nullptr; // (the atlas the fonts are in: another context's means none built here)
    float condense_ = 1.0F;
    float bold_ = 0.0F;
};

NewFrontendDrawState& new_frontend_draw_state()
{
    static core::NoDestroy<NewFrontendDrawState> state;
    return state.get();
}

// The display face's glyphs: upper case (the design sets it in capitals), figures, punctuation, the em dash. The
// mono and body faces: ASCII, the em dash, the middle dot, the apostrophe.
constexpr auto kDisplayRanges = std::to_array<ImWchar>({0x0020, 0x0060, 0x00C0, 0x00DE, 0x2014, 0x2014, 0x2019, 0x2019, 0});
constexpr auto kTextRanges = std::to_array<ImWchar>({0x0020, 0x007E, 0x00B7, 0x00B7, 0x00C0, 0x00FF, 0x2014, 0x2014, 0x2019, 0x2019, 0});

bool read_file(const std::string& path, std::vector<uint8_t>* out)
{
    const fs::OwnedStream file = fs::open_stream(path.c_str(), "rb", fs::StreamSharing::kSecure);
    if (!file) {
        return false;
    }
    fs::disk().stream_seek(file.get(), 0, SEEK_END);
    const long size = fs::disk().stream_tell(file.get());
    fs::disk().stream_seek(file.get(), 0, SEEK_SET);
    bool ok = size > kMinFontFileBytes && size < kMaxFontFileMegabytes * kBytesPerKilobyte * kBytesPerKilobyte;
    if (ok) {
        out->resize(static_cast<size_t>(size));
        ok = fs::disk().stream_read(file.get(), out->data(), 1, out->size()) == out->size();
    }
    return ok;
}

uint16_t be16(std::span<const uint8_t> bytes)
{
    return static_cast<uint16_t>(core::at(bytes, 0) << kBitsPerByte | core::at(bytes, 1));
}
uint32_t be32(std::span<const uint8_t> bytes)
{
    return static_cast<uint32_t>(core::at(bytes, 0)) << kBitsPerThreeBytes | core::at(bytes, 1) << kBitsPerTwoBytes |
           core::at(bytes, 2) << kBitsPerByte | core::at(bytes, 3);
}

const Baked* font_for(Face face, float css)
{
    const Baked* best = nullptr;
    for (const Baked& baked : new_frontend_draw_state().baked_) {
        if (!baked.font || baked.face != face) {
            continue;
        }
        // (the smallest bake at least as large, else the largest)
        const float baked_css = baked.em_px_ / std::max(new_frontend_draw_state().scale_, 0.01F);
        if (!best) {
            best = &baked;
            continue;
        }
        const float best_css = best->em_px_ / std::max(new_frontend_draw_state().scale_, 0.01F);
        const bool fits = baked_css >= css - 0.5F;
        const bool best_fits = best_css >= css - 0.5F;
        if ((fits && (!best_fits || baked_css < best_css)) || (!fits && !best_fits && baked_css > best_css)) {
            best = &baked;
        }
    }
    if (!best && face != Face::kBody) {
        return font_for(Face::kBody, css); // (a face that couldn't load: the body's)
    }
    if (!best && face != Face::kMono) {
        return font_for(Face::kMono, css);
    }
    return best;
}

const FaceLayout& data_of(Face face) { return core::at(stored_faces(), static_cast<int>(face)); }

std::string get_upper(const char* text)
{
    std::string upper = text;
    for (char& character : upper) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return upper;
}

// The scale from the baked font to the size asked, and the display face's width.
struct Gauges {
    const Baked* baked;
    float k;        // window px per baked px
    float x_scale_; // (the display face condensed)
    float bold_px_;
};
Gauges metrics_of(const Style& style, const TickCycle& frame)
{
    Gauges gauges = {.baked = font_for(style.face, style.css), .k = 1.0F, .x_scale_ = 1.0F, .bold_px_ = 0.0F};
    if (gauges.baked) {
        gauges.k = frame.current_s(style.css) / gauges.baked->em_px_;
    }
    if (style.face == Face::kDisplay && gauges.baked && gauges.baked->face == Face::kDisplay) {
        gauges.x_scale_ = new_frontend_draw_state().condense_;
        gauges.bold_px_ = new_frontend_draw_state().bold_ * frame.current_s(style.css);
    }
    return gauges;
}

} // namespace
namespace internal {

// The text's width in window px.
float get_measure(const Style& style, const TickCycle& frame, const char* text)
{
    const Gauges gauges = metrics_of(style, frame);
    if (!gauges.baked) {
        return 0.0F;
    }
    const std::string shown = style.upper ? get_upper(text) : text;
    const char* cursor = shown.c_str();
    const char* end = std::next(cursor, static_cast<std::ptrdiff_t>(shown.size()));
    float width = 0.0F;
    int count = 0;
    while (cursor < end) {
        unsigned int codepoint = 0;
        cursor = std::next(cursor, ImTextCharFromUtf8(&codepoint, cursor, end));
        if (codepoint == 0U) {
            break;
        }
        const ImFontGlyph* glyph = gauges.baked->font->FindGlyph(static_cast<ImWchar>(codepoint));
        if (glyph) {
            width += glyph->AdvanceX * gauges.k * gauges.x_scale_;
        }
        ++count;
    }
    width += (frame.current_s(style.tracking) * static_cast<float>(count)) + gauges.bold_px_;
    return width;
}

// The text starting at window x `at.x` with its baseline at window y `at.y`.
void draw_text(ImDrawList* dl, const Style& style, const TickCycle& frame, maths::Vec2 at, const char* text)
{
    const float x = at.x;
    const float baseline = at.y;
    const Gauges gauges = metrics_of(style, frame);
    if (!gauges.baked) {
        return;
    }
    ImFont* font = gauges.baked->font;
    const std::string shown = style.upper ? get_upper(text) : text;
    const char* end = std::next(shown.c_str(), static_cast<std::ptrdiff_t>(shown.size()));
    const ImU32 colour = get_col(style.colour, style.alpha);
    const float top = baseline - (font->Ascent * gauges.k);
    for (int pass = 0; pass < (gauges.bold_px_ > 0.0F ? 2 : 1); ++pass) {
        float pen = x + ((pass != 0) ? gauges.bold_px_ : 0.0F);
        for (const char* cursor = shown.c_str(); cursor < end;) {
            unsigned int codepoint = 0;
            cursor = std::next(cursor, ImTextCharFromUtf8(&codepoint, cursor, end));
            if (codepoint == 0U) {
                break;
            }
            const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(codepoint));
            if (!glyph) {
                continue;
            }
            if (glyph->Visible) {
                const ImVec2 corner_min(pen + (glyph->X0 * gauges.k * gauges.x_scale_), top + (glyph->Y0 * gauges.k));
                const ImVec2 corner_max(pen + (glyph->X1 * gauges.k * gauges.x_scale_), top + (glyph->Y1 * gauges.k));
                dl->PrimReserve(kGlyphQuadIndices, 4);
                dl->PrimRectUV(corner_min, corner_max, ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1),
                               colour);
            }
            pen += (glyph->AdvanceX * gauges.k * gauges.x_scale_) + frame.current_s(style.tracking);
        }
    }
}

// The baseline of a line box at window y `top` for the style at `lineHeight` (a multiple of its size).
float datum(const Style& style, const TickCycle& frame, float top, float line_height)
{
    const Baked* baked = font_for(style.face, style.css);
    const FaceLayout& data = data_of(baked ? baked->face : style.face);
    return top + frame.current_s(baseline_in_line(data.ascent, data.descent, style.css, line_height));
}

// Words of `text` in lines no wider than `width` (window px).
std::vector<std::string> get_wrap(const Style& style, const TickCycle& frame, const std::string& text, float width)
{
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    while (at < text.size()) {
        const size_t space = text.find(' ', at);
        const std::string word = text.substr(at, space == std::string::npos ? std::string::npos : space - at);
        at = space == std::string::npos ? text.size() : space + 1;
        if (word.empty()) {
            continue;
        }
        std::string tried = line.empty() ? word : line;
        if (!line.empty()) {
            tried.append(" ").append(word);
        }
        if (!line.empty() && get_measure(style, frame, tried.c_str()) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace internal

std::optional<FontMetrics> read_font_metrics(std::span<const uint8_t> data)
{
    if (data.size() < kSfntHeaderBytes) {
        return std::nullopt;
    }
    const uint32_t tag = be32(data);
    if (tag != kSfntVersionTrueType && tag != kTagTrue /* true */ && tag != kTagOtto /* OTTO */) {
        return std::nullopt;
    }
    const int tables = be16(data.subspan(4));
    std::span<const uint8_t> head;
    std::span<const uint8_t> hhea;
    for (int i = 0; i < tables; ++i) {
        const size_t entry = 12 + (static_cast<size_t>(i) * 16);
        if (entry + kSfntTableRecordBytes > data.size()) {
            return std::nullopt;
        }
        const uint32_t name = be32(data.subspan(entry));
        const uint32_t offset = be32(data.subspan(entry + 8));
        if (offset + kHeadTableBytes > data.size()) {
            continue;
        }
        if (name == kTagHead) { // head
            head = data.subspan(offset);
        } else if (name == kTagHhea) { // hhea
            hhea = data.subspan(offset);
        }
    }
    if (head.empty() || hhea.empty()) {
        return std::nullopt;
    }
    auto units_per_em = static_cast<float>(be16(head.subspan(kHeadUnitsPerEmOffset)));
    if (units_per_em <= 0.0F) {
        return std::nullopt;
    }
    const FontMetrics metrics{.ascent = static_cast<float>(static_cast<int16_t>(be16(hhea.subspan(4)))) / units_per_em,
                              .descent = static_cast<float>(static_cast<int16_t>(be16(hhea.subspan(6)))) / units_per_em};
    if (metrics.ascent <= 0.0F) {
        return std::nullopt;
    }
    return metrics;
}

namespace {

// The face's metrics from its file (false, the face's defaults kept, if they can't be read).
bool read_face_metrics(FaceLayout& face)
{
    const std::optional<FontMetrics> metrics = read_font_metrics(face.file);
    if (!metrics) {
        return false;
    }
    face.ascent = metrics->ascent;
    face.descent = metrics->descent;
    return true;
}

} // namespace

void forget_fonts()
{
    new_frontend_draw_state().atlas_ = nullptr;
    new_frontend_draw_state().scale_ = 0.0F;
    for (Baked& baked : new_frontend_draw_state().baked_) {
        baked = Baked{};
    }
}

float built_scale()
{
    return ImGui::GetCurrentContext() && ImGui::GetIO().Fonts == new_frontend_draw_state().atlas_
               ? new_frontend_draw_state().scale_
               : 0.0F;
}

bool build_fonts(const FontFiles& files, float scale)
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    for (Baked& baked : new_frontend_draw_state().baked_) {
        baked = Baked{};
    }
    new_frontend_draw_state().condense_ = std::clamp(files.condense, kCondenseMin, kCondenseMax);
    new_frontend_draw_state().bold_ = std::clamp(files.bold, 0.0F, kBoldMax);
    const std::array<const std::string*, 3> paths = {{&files.display, &files.mono, &files.body}};
    bool all = true;
    for (int i = 0; i < 3; ++i) {
        FaceLayout& face = core::at(stored_faces(), i);
        face = FaceLayout{};
        if (core::at(paths, i)->empty() || !read_file(*core::at(paths, i), &face.file) ||
            !read_face_metrics(face)) {
            get_log("new front end: can't read the font %s", core::at(paths, i)->empty() ? "(none)" : core::at(paths, i)->c_str());
            face.file.clear();
            all = false;
            continue;
        }
        face.ok = true;
    }
    for (int i = 0; i < kBakeCount; ++i) {
        FaceLayout& face = core::at(stored_faces(), static_cast<int>(core::at(kBakes, i).face));
        if (!face.ok) {
            continue;
        }
        const float em = std::max(4.0F, std::round(core::at(kBakes, i).css * scale));
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        const bool big = em > 32.0F;
        config.OversampleH = big ? 1 : 2;
        config.OversampleV = 1;
        config.PixelSnapH = big;
        text::format_to(config.Name, "fe %d %.0f", static_cast<int>(core::at(kBakes, i).face), core::at(kBakes, i).css);
        // (stb_truetype's size is ascent - descent: the em asked for times that)
        const float pixel_height = em * (face.ascent - face.descent);
        ImFont* font =
            atlas->AddFontFromMemoryTTF(face.file.data(), static_cast<int>(face.file.size()), pixel_height, &config,
                                        core::at(kBakes, i).face == Face::kDisplay ? kDisplayRanges.data() : kTextRanges.data());
        core::at(new_frontend_draw_state().baked_, i) = {.font = font, .face = core::at(kBakes, i).face, .em_px_ = em};
    }
    new_frontend_draw_state().scale_ = scale;
    new_frontend_draw_state().atlas_ = atlas;
    atlas->Build();
    get_log("new front end: fonts for scale %.2f - atlas %dx%d", scale, atlas->TexWidth, atlas->TexHeight);
    return all;
}

} // namespace frontend::new_frontend::paint
