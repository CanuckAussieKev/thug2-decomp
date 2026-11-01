// NOT ORIGINAL - the new front end's drawing (ours): see new_frontend_draw.h. The numbers are the design's
// (docs/design/frontend/HMain.dc.html, HLevels.dc.html - their CSS), in design pixels at 1280 x 720.

#include "frontend/new_frontend_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "engine/util/unique_handle.h"

namespace frontend::new_frontend::paint {

namespace {

// --- the colours (docs/design/frontend/README.md) ---
constexpr uint32_t kGround = 0xFF0A0A0A;
constexpr uint32_t kPaper = 0xFFF2EFE8;
constexpr uint32_t kMuted = 0xFFB9B5AC;
constexpr uint32_t kMuted2 = 0xFF8E8A82;
constexpr uint32_t kRule = 0xFF3A3A38;
constexpr uint32_t kArtGround = 0xFF1C1C1B;

ImU32 get_col(uint32_t argb, float alpha = 1.0f)
{
    uint32_t a = static_cast<uint32_t>(std::clamp(static_cast<float>((argb >> 24) & 0xFF) * alpha, 0.0f, 255.0f));
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, a);
}

uint32_t get_mix(uint32_t a, uint32_t b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        auto x = static_cast<float>((a >> shift) & 0xFF), y = static_cast<float>((b >> shift) & 0xFF);
        // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
        out |= static_cast<uint32_t>(x + (y - x) * t + 0.5f) << shift;
    }
    return out;
}

// --- the fonts ---

// The sizes baked (CSS px at 1280 x 720): the display face's 120 (the big names: the level select's 112 drawn from
// it), 78 (the main menu), 40 (the levels), 30 (Skate); the mono face's 12 and 13; the body's 14.
constexpr Bake kBakes[] = {
    {Face::kDisplay, 120.0f}, {Face::kDisplay, kMainType}, {Face::kDisplay, kLevelType}, {Face::kDisplay, 28.0f},
    {Face::kMono, 12.0f},     {Face::kMono, 13.0f},        {Face::kBody, 14.0f},         {Face::kMono, 20.0f},
    {Face::kBody, 48.0f}, // (the restyled keyboard's keys and the text typed)
};
constexpr int kBakeCount = static_cast<int>(sizeof(kBakes) / sizeof(kBakes[0]));

struct FaceLayout {
    std::vector<uint8_t> file; // (kept: the atlas doesn't own it - several sizes share it)
    float ascent = 0.8f, descent = -0.2f;
    bool ok = false;
};
FaceLayout g_faces[3];

struct Baked {
    ImFont* font = nullptr;
    Face face = Face::kBody;
    float em_px_ = 0.0f; // the em in window px it was baked for
};
Baked g_baked[kBakeCount];
float g_scale = 0.0f;
ImFontAtlas* g_atlas = nullptr; // (the atlas the fonts are in: another context's means none built here)
float g_condense = 1.0f, g_bold = 0.0f;

// The display face's glyphs: upper case (the design sets it in capitals), figures, punctuation, the em dash. The
// mono and body faces: ASCII, the em dash, the middle dot, the apostrophe.
constexpr ImWchar kDisplayRanges[] = {0x0020, 0x0060, 0x00C0, 0x00DE, 0x2014, 0x2014, 0x2019, 0x2019, 0};
constexpr ImWchar kTextRanges[] = {0x0020, 0x007E, 0x00B7, 0x00B7, 0x00C0, 0x00FF, 0x2014, 0x2014, 0x2019, 0x2019, 0};

bool read_file(const std::string& path, std::vector<uint8_t>* out)
{
    util::UniqueFile file = util::open_file(path.c_str(), "rb");
    if (!file)
        return false;
    std::fseek(file.get(), 0, SEEK_END);
    long size = std::ftell(file.get());
    std::fseek(file.get(), 0, SEEK_SET);
    bool ok = size > 12 && size < 64 * 1024 * 1024;
    if (ok) {
        out->resize(static_cast<size_t>(size));
        ok = std::fread(out->data(), 1, out->size(), file.get()) == out->size();
    }
    return ok;
}

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

const Baked* font_for(Face face, float css)
{
    const Baked* best = nullptr;
    for (const Baked& b : g_baked) {
        if (!b.font || b.face != face)
            continue;
        // (the smallest bake at least as large, else the largest)
        float baked_css = b.em_px_ / std::max(g_scale, 0.01f);
        if (!best) {
            best = &b;
            continue;
        }
        float best_css = best->em_px_ / std::max(g_scale, 0.01f);
        bool fits = baked_css >= css - 0.5f, best_fits = best_css >= css - 0.5f;
        if ((fits && (!best_fits || baked_css < best_css)) || (!fits && !best_fits && baked_css > best_css))
            best = &b;
    }
    if (!best && face != Face::kBody)
        return font_for(Face::kBody, css); // (a face that couldn't load: the body's)
    if (!best && face != Face::kMono)
        return font_for(Face::kMono, css);
    return best;
}

const FaceLayout& data_of(Face face) { return g_faces[static_cast<int>(face)]; }

// --- text ---

struct Style {
    Face face = Face::kBody;
    float css = 14.0f;     // its CSS size (design px)
    float tracking = 0.0f; // letter-spacing (design px)
    bool upper = false;
    uint32_t colour = kPaper;
    float alpha = 1.0f;
};

std::string get_upper(const char* text)
{
    std::string s = text;
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return s;
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
    Gauges m = {font_for(style.face, style.css), 1.0f, 1.0f, 0.0f};
    if (m.baked)
        m.k = frame.current_s(style.css) / m.baked->em_px_;
    if (style.face == Face::kDisplay && m.baked && m.baked->face == Face::kDisplay) {
        m.x_scale_ = g_condense;
        m.bold_px_ = g_bold * frame.current_s(style.css);
    }
    return m;
}

// The text's width in window px.
float get_measure(const Style& style, const TickCycle& frame, const char* text)
{
    Gauges m = metrics_of(style, frame);
    if (!m.baked)
        return 0.0f;
    std::string s = style.upper ? get_upper(text) : text;
    const char* p = s.c_str();
    const char* end = p + s.size();
    float width = 0.0f;
    int count = 0;
    while (p < end) {
        unsigned int c = 0;
        p += ImTextCharFromUtf8(&c, p, end);
        if (!c)
            break;
        const ImFontGlyph* g = m.baked->font->FindGlyph(static_cast<ImWchar>(c));
        if (g)
            width += g->AdvanceX * m.k * m.x_scale_;
        ++count;
    }
    width += frame.current_s(style.tracking) * static_cast<float>(count) + m.bold_px_;
    return width;
}

// The text with its baseline at window y `baseline`, starting at window x `x`.
void draw_text(ImDrawList* dl, const Style& style, const TickCycle& frame, float x, float baseline, const char* text)
{
    Gauges m = metrics_of(style, frame);
    if (!m.baked)
        return;
    ImFont* font = m.baked->font;
    std::string s = style.upper ? get_upper(text) : text;
    const char* end = s.c_str() + s.size();
    ImU32 colour = get_col(style.colour, style.alpha);
    float top = baseline - font->Ascent * m.k;
    for (int pass = 0; pass < (m.bold_px_ > 0.0f ? 2 : 1); ++pass) {
        float pen = x + (pass ? m.bold_px_ : 0.0f);
        for (const char* p = s.c_str(); p < end;) {
            unsigned int c = 0;
            p += ImTextCharFromUtf8(&c, p, end);
            if (!c)
                break;
            const ImFontGlyph* g = font->FindGlyph(static_cast<ImWchar>(c));
            if (!g)
                continue;
            if (g->Visible) {
                ImVec2 a(pen + g->X0 * m.k * m.x_scale_, top + g->Y0 * m.k);
                ImVec2 b(pen + g->X1 * m.k * m.x_scale_, top + g->Y1 * m.k);
                dl->PrimReserve(6, 4);
                dl->PrimRectUV(a, b, ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V1), colour);
            }
            pen += g->AdvanceX * m.k * m.x_scale_ + frame.current_s(style.tracking);
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
        size_t space = text.find(' ', at);
        std::string word = text.substr(at, space == std::string::npos ? std::string::npos : space - at);
        at = space == std::string::npos ? text.size() : space + 1;
        if (word.empty())
            continue;
        std::string tried = line.empty() ? word : line;
        if (!line.empty())
            tried.append(" ").append(word);
        if (!line.empty() && get_measure(style, frame, tried.c_str()) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

// --- shapes ---

// The design's clip-path: polygon(a 0, 100% 0, calc(100% - b) 100%, 0 100%) - a parallelogram leaning right.
void get_cut(ImDrawList* dl, float x0, float y0, float x1, float y1, float top_inset, float bottom_inset, ImU32 colour)
{
    ImVec2 points[4] = {{x0 + top_inset, y0}, {x1, y0}, {x1 - bottom_inset, y1}, {x0, y1}};
    dl->AddConvexPolyFilled(points, 4, colour);
}

// The vertices added since `start` turned by `radians` about (px, py) and moved by (dx, dy) - the row's
// translateX(24px) rotate(-3deg) about its left centre.
void xform(ImDrawList* dl, int start, float px, float py, float radians, float dx, float dy)
{
    float c = std::cos(radians), s = std::sin(radians);
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& p = dl->VtxBuffer[i].pos;
        float x = p.x - px, y = p.y - py;
        p.x = px + dx + x * c - y * s;
        p.y = py + dy + x * s + y * c;
    }
}

// An arrow (the design's "<-" / "->" in the mono face): a line with a head, `size` px tall, at the text's x-height.
void get_arrow(ImDrawList* dl, float x, float baseline, float size, bool left, ImU32 colour, float* advance)
{
    float w = size * 0.9f, y = baseline - size * 0.32f, t = std::max(1.0f, size * 0.09f);
    float x0 = x, x1 = x + w;
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), colour, t);
    float hx = left ? x0 : x1, dir = left ? 1.0f : -1.0f, h = size * 0.3f;
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + dir * h, y - h), colour, t);
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + dir * h, y + h), colour, t);
    *advance = w + size * 0.45f;
}

void art_box(ImDrawList* dl, const Art& art, float x0, float y0, float x1, float y1, float pan, float alpha)
{
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), get_col(kArtGround, alpha));
    if (!art.texture)
        return;
    Uv uv = cover_uv(art.width, art.height, x1 - x0, y1 - y0, pan, 0.03f);
    if (art.bottom_up_) {
        uv.v0 = 1.0f - uv.v0;
        uv.v1 = 1.0f - uv.v1;
    }
    dl->AddImage(static_cast<ImTextureID>(art.texture), ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(uv.u0, uv.v0),
                 ImVec2(uv.u1, uv.v1), get_col(0xFFFFFFFF, alpha));
}

// The bottom of the art darkened so the words over it read (not in the mock-ups, which have flat grey there).
void get_shade(ImDrawList* dl, float x0, float y0, float x1, float y1)
{
    float mid = y0 + (y1 - y0) * 0.35f;
    dl->AddRectFilledMultiColor(ImVec2(x0, mid), ImVec2(x1, y1), get_col(kGround, 0.0f), get_col(kGround, 0.0f),
                                get_col(kGround, 0.88f), get_col(kGround, 0.88f));
}

// The list column's scrim (the user's change to the mock-ups, 2026-10-25: the art behind the whole screen, the column
// on a dark, softened copy of it rather than a flat panel): the art's column part drawn again a few times a little
// apart (averaged: a soft blur), darkened ~75 %, its right edge feathered over kScrimFeather. `art` may be empty (the
// pause menus: the game behind - only darkened).
void scrim(ImDrawList* dl, const TickCycle& f, const Art& art, float pan, float alpha)
{
    if (alpha <= 0.001f)
        return;
    float x1 = kColumn + kScrimFeather;
    if (art.texture) {
        Uv uv = cover_uv(art.width, art.height, 1280.0f, 720.0f, pan, 0.03f);
        if (art.bottom_up_) {
            uv.v0 = 1.0f - uv.v0;
            uv.v1 = 1.0f - uv.v1;
        }
        // (the column's share of the picture)
        float share = x1 / 1280.0f;
        float u1 = uv.u0 + (uv.u1 - uv.u0) * share;
        float u_edge = uv.u0 + (uv.u1 - uv.u0) * (kColumn / 1280.0f);
        static const float kOffsets[][2] = {{0, 0}, {9, 0},   {-9, 0}, {0, 9}, {0, -9},
                                            {6, 6}, {-6, -6}, {6, -6}, {-6, 6}};
        dl->PushTextureID(static_cast<ImTextureID>(art.texture));
        dl->PushClipRect(ImVec2(f.get_x(0.0f), f.get_y(0.0f)), ImVec2(f.get_x(x1), f.get_y(720.0f)), true);
        for (int i = 0; i < 9; ++i) {
            float dx = f.current_s(kOffsets[i][0]), dy = f.current_s(kOffsets[i][1]);
            // (copy i at 1 / (i + 1): the result is the copies' average; across the feather it fades to the art)
            ImU32 full = get_col(0xFFFFFFFF, alpha / static_cast<float>(i + 1)), none = get_col(0xFFFFFFFF, 0.0f);
            float ya = f.get_y(0.0f) + dy, yb = f.get_y(720.0f) + dy;
            float xa = f.get_x(0.0f) + dx, xm = f.get_x(kColumn) + dx, xb = f.get_x(x1) + dx;
            dl->PrimReserve(12, 8);
            auto base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            dl->PrimWriteVtx(ImVec2(xa, ya), ImVec2(uv.u0, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(u_edge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(u_edge, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xa, yb), ImVec2(uv.u0, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(u_edge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xb, ya), ImVec2(u1, uv.v0), none);
            dl->PrimWriteVtx(ImVec2(xb, yb), ImVec2(u1, uv.v1), none);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(u_edge, uv.v1), full);
            for (int q = 0; q < 2; ++q) {
                auto b = static_cast<ImDrawIdx>(base + q * 4);
                dl->PrimWriteIdx(b);
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(b + 1));
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(b + 2));
                dl->PrimWriteIdx(b);
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(b + 2));
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(b + 3));
            }
        }
        dl->PopClipRect();
        dl->PopTextureID();
    }
    float dark = 0.8f * alpha;
    dl->AddRectFilled(ImVec2(f.get_x(0.0f), f.get_y(0.0f)), ImVec2(f.get_x(kColumn), f.get_y(720.0f)),
                      get_col(kGround, dark));
    dl->AddRectFilledMultiColor(ImVec2(f.get_x(kColumn), f.get_y(0.0f)), ImVec2(f.get_x(x1), f.get_y(720.0f)),
                                get_col(kGround, dark), get_col(kGround, 0.0f), get_col(kGround, 0.0f),
                                get_col(kGround, dark));
}

// The whole screen's art (full-bleed), the words' shade at the foot, the column's scrim.
void backdrop(ImDrawList* dl, const TickCycle& f, const Art& art, float pan)
{
    art_box(dl, art, f.get_x(0.0f), f.get_y(0.0f), f.get_x(1280.0f), f.get_y(720.0f), pan, 1.0f);
    get_shade(dl, f.get_x(kRight - 32.0f), f.get_y(0.0f), f.get_x(1280.0f), f.get_y(720.0f));
    scrim(dl, f, art, pan, 1.0f);
}

std::string pad2(int n)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%02d", n);
    return text;
}

// --- one screen's parts ---

struct Ctx {
    ImDrawList* dl;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): a short-lived view of the caller's data
    const TickCycle& f;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): a short-lived view of the caller's data
    const Menu& menu;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): a short-lived view of the caller's data
    const Extras& extras;
    std::vector<Hit>* hits;
    void get_hit(Hit::Kind kind, int index, float x0, float y0, float x1, float y1)
    {
        if (hits)
            hits->push_back({kind, index, x0, y0, x1, y1});
    }
};

Style mono12(uint32_t colour, bool tracked = true)
{
    Style s;
    s.face = Face::kMono;
    s.css = 12.0f;
    s.tracking = tracked ? 1.0f : 0.0f;
    s.upper = true;
    s.colour = colour;
    return s;
}

// A header row at design y `top` (left 48, right `right`): the texts, the rule under it. Returns the rule's y.
float get_header(Ctx& c, float top, float right, const char* left_text, bool back, const char* right_text)
{
    Style s = mono12(kMuted);
    float line_h = 1.3f;
    float base = datum(s, c.f, c.f.get_y(top), line_h);
    float x = c.f.get_x(48.0f);
    if (back) {
        Style b = mono12(kMuted, false);
        float adv = 0.0f;
        get_arrow(c.dl, x, base, c.f.current_s(12.0f), true, get_col(kMuted), &adv);
        draw_text(c.dl, b, c.f, x + adv, base, text_tools::kBack);
        float w = adv + get_measure(b, c.f, text_tools::kBack);
        c.get_hit(Hit::Kind::kBack, 0, 48.0f - 6.0f, top - 6.0f, 48.0f + w / c.f.scale + 6.0f,
                  top + 12.0f * line_h + 6.0f);
        x += w + c.f.current_s(16.0f);
        s.colour = kPaper;
    }
    draw_text(c.dl, s, c.f, x, base, left_text);
    if (right_text && *right_text) {
        Style r = mono12(kMuted);
        draw_text(c.dl, r, c.f, c.f.get_x(right) - get_measure(r, c.f, right_text), base, right_text);
    }
    float rule = top + 12.0f * line_h + 12.0f;
    c.dl->AddRectFilled(ImVec2(c.f.get_x(48.0f), c.f.get_y(rule)),
                        ImVec2(c.f.get_x(right), c.f.get_y(rule) + std::max(1.0f, c.f.current_s(1.0f))),
                        get_col(kRule));
    return rule + 1.0f;
}

// A list row with the kinetic selection: number, name, the cut behind the name when selected - all turned and moved
// by the selection's amount `e`. Design numbers: the row's top `top`, its line box `line_h`, the name's left `text_x_`,
// the number's style, the cut's extents and insets.
struct RowStyle {
    Style name;
    Style number;
    float line_height_{}; // (a multiple of the name's size)
    float row_pad_top_{}; // (above the line box)
    float row_height_{};
    float text_x_{};                 // the name's left
    float cut_left_{};               // the cut's left (the label span's left - 6)
    float cut_right_pad_{};          // past the name's right (span padding + 22 / 26)
    float cut_top_{}, cut_bottom_{}; // (fractions of the line box)
    float top_inset_{}, bottom_inset_{};
};

void get_row(Ctx& c, const RowStyle& rs, float top, const char* number, const char* label, float e, uint32_t cut_colour,
             bool dim)
{
    ImDrawList* dl = c.dl;
    int start = dl->VtxBuffer.Size;
    float line_top = top + rs.row_pad_top_;
    float line_h = rs.name.css * rs.line_height_;
    float base = datum(rs.name, c.f, c.f.get_y(line_top), rs.line_height_);
    float width = get_measure(rs.name, c.f, label);
    if (e > 0.001f) {
        float x0 = c.f.get_x(rs.cut_left_);
        float x1 = c.f.get_x(rs.text_x_) + width + c.f.current_s(rs.cut_right_pad_);
        x1 = x0 + (x1 - x0) * e; // (the cut snaps out from the left)
        get_cut(dl, x0, c.f.get_y(line_top + line_h * rs.cut_top_), x1, c.f.get_y(line_top + line_h * rs.cut_bottom_),
                c.f.current_s(rs.top_inset_), c.f.current_s(rs.bottom_inset_), get_col(cut_colour));
    }
    Style name = rs.name;
    name.colour = get_mix(dim ? kMuted2 : kPaper, kGround, e);
    draw_text(dl, name, c.f, c.f.get_x(rs.text_x_), base, label);
    Style num = rs.number;
    draw_text(dl, num, c.f, c.f.get_x(48.0f), base, number);
    if (e > 0.001f) {
        float pivot_y = c.f.get_y(top + rs.row_height_ * 0.5f);
        xform(dl, start, c.f.get_x(48.0f), pivot_y, -3.0f * 3.14159265f / 180.0f * e, c.f.current_s(24.0f) * e, 0.0f);
    }
}

RowStyle main_row_style()
{
    RowStyle rs;
    rs.name.face = Face::kDisplay;
    rs.name.css = kMainType;
    rs.name.upper = true;
    rs.number = mono12(kMuted, false);
    rs.number.css = 13.0f;
    rs.line_height_ = 0.92f;
    rs.row_pad_top_ = 2.0f;
    rs.row_height_ = kMainRowHeight;
    rs.text_x_ = 48.0f + 26.0f + 18.0f + 12.0f;
    rs.cut_left_ = 48.0f + 26.0f + 18.0f - 6.0f;
    rs.cut_right_pad_ = 12.0f + 26.0f;
    rs.cut_top_ = 0.18f;
    rs.cut_bottom_ = 0.92f;
    rs.top_inset_ = 10.0f;
    rs.bottom_inset_ = 18.0f;
    return rs;
}

RowStyle level_row_style()
{
    RowStyle rs;
    rs.name.face = Face::kDisplay;
    rs.name.css = kLevelType;
    rs.name.upper = true;
    rs.number = mono12(kMuted2, false);
    rs.line_height_ = 1.05f;
    rs.row_pad_top_ = 0.0f;
    rs.row_height_ = kLevelRowHeight;
    rs.text_x_ = 48.0f + 26.0f + 14.0f + 10.0f;
    rs.cut_left_ = 48.0f + 26.0f + 14.0f - 6.0f;
    rs.cut_right_pad_ = 10.0f + 22.0f;
    rs.cut_top_ = 0.16f;
    rs.cut_bottom_ = 0.94f;
    rs.top_inset_ = 8.0f;
    rs.bottom_inset_ = 14.0f;
    return rs;
}

// The big name at the right's foot: lines of `name` (Anton `css`, line-height 0.85) ending at design y `bottom`, from
// x `left` to `right`. Returns the top of the block. With `slice` > 0 (the launch), the top half moves up and the
// bottom half down along a cut, fading.
float big_name(Ctx& c, const std::string& name, float css, float left, float right, float bottom, float slice,
               uint32_t slice_colour)
{
    Style s;
    s.face = Face::kDisplay;
    s.css = css;
    s.upper = true;
    std::vector<std::string> lines = get_wrap(s, c.f, name, c.f.current_s(right - left));
    if (lines.size() > 3)
        lines.resize(3);
    // (a word wider than the space - CREATE-A-SKATER - makes the type smaller, to two thirds at most)
    float widest = 0.0f;
    for (const std::string& line : lines)
        widest = std::max(widest, get_measure(s, c.f, line.c_str()));
    if (widest > c.f.current_s(right - left)) {
        css = std::max(css * 0.66f, css * c.f.current_s(right - left) / widest);
        s.css = css;
    }
    float line_h = css * 0.85f;
    float top = bottom - line_h * static_cast<float>(lines.size());
    float mid = c.f.get_y(top + (bottom - top) * 0.55f);
    for (int half = 0; half < (slice > 0.0f ? 2 : 1); ++half) {
        float dy = slice > 0.0f ? (half == 0 ? -1.0f : 1.0f) * c.f.current_s(70.0f) * slice : 0.0f;
        s.alpha = 1.0f - slice;
        if (slice > 0.0f) {
            // (each half keeps its own part of the name: the cut moves with it, the gap between opens)
            if (half == 0)
                c.dl->PushClipRect(ImVec2(0, 0), ImVec2(1e6f, mid + dy), true);
            else
                c.dl->PushClipRect(ImVec2(0, mid + dy), ImVec2(1e6f, 1e6f), true);
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            float line_top = top + line_h * static_cast<float>(i);
            float base = datum(s, c.f, c.f.get_y(line_top), 0.85f);
            draw_text(c.dl, s, c.f, c.f.get_x(left), base + dy, lines[i].c_str());
        }
        if (slice > 0.0f)
            c.dl->PopClipRect();
    }
    if (slice > 0.0f) {
        float w = c.f.current_s(right - left) * std::min(1.0f, slice * 2.0f);
        c.dl->AddRectFilled(ImVec2(c.f.get_x(left), mid - c.f.current_s(2.0f)),
                            ImVec2(c.f.get_x(left) + w, mid + c.f.current_s(2.0f)),
                            get_col(slice_colour, 1.0f - slice));
    }
    return top;
}

// A kicker line (mono 12, tracked, upper case) whose box ends at design y `bottom`. Returns its top.
float get_kicker(Ctx& c, const std::string& text, float left, float bottom, uint32_t colour, float alpha = 1.0f)
{
    Style s = mono12(colour);
    s.alpha = alpha;
    float top = bottom - 12.0f * 1.3f;
    draw_text(c.dl, s, c.f, c.f.get_x(left), datum(s, c.f, c.f.get_y(top), 1.3f), text.c_str());
    return top;
}

// The hints row (a rule over it), from `left` to `right`, its text's box ending at `bottom`: spread as CSS's
// justify-content: space-between. Returns the rule's y.
float get_hints(Ctx& c, const std::vector<std::string>& hints, float left, float right, float bottom,
                std::vector<std::pair<float, float>>* spans = nullptr)
{
    Style s = mono12(kMuted, false);
    float top = bottom - 12.0f * 1.3f;
    float base = datum(s, c.f, c.f.get_y(top), 1.3f);
    float total = 0.0f;
    std::vector<float> widths;
    for (const std::string& h : hints) {
        widths.push_back(get_measure(s, c.f, h.c_str()));
        total += widths.back();
    }
    float gap = hints.size() > 1 ? (c.f.current_s(right - left) - total) / static_cast<float>(hints.size() - 1) : 0.0f;
    float x = c.f.get_x(left);
    for (size_t i = 0; i < hints.size(); ++i) {
        draw_text(c.dl, s, c.f, x, base, hints[i].c_str());
        if (spans) // (design x: each hint's left and right)
            spans->emplace_back(c.f.design_x(x), c.f.design_x(x + widths[i]));
        x += widths[i] + gap;
    }
    float rule = top - 10.0f - 1.0f;
    c.dl->AddRectFilled(ImVec2(c.f.get_x(left), c.f.get_y(rule)),
                        ImVec2(c.f.get_x(right), c.f.get_y(rule) + std::max(1.0f, c.f.current_s(1.0f))),
                        get_col(kRule));
    return rule;
}

Art art_for(const Extras& extras, const std::string& image)
{
    return extras.art && !image.empty() ? extras.art(image) : Art{};
}

// --- the screens ---

// The art for an item: its own, levels.json's menu_art for its id, else THUG2's loading screen for it.
std::string art_by_id(const Catalogue* data, const std::string& id)
{
    std::string image = data ? data->menu_art(id) : std::string();
    return image.empty() ? default_art(id) : image;
}

// The pause screens' ground: the game dimmed, the left column nearly opaque. `dim`: how much (an editor's view less).
void pause_ground(Ctx& c, float window_width, float window_height, float dim = 0.55f)
{
    c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(window_width, window_height), get_col(kGround, dim));
    scrim(c.dl, c.f, Art{}, 0.0f, 1.0f);
}

// A mono line of records spread in three columns from `left` to `right`, its box ending at `bottom` (a rule above).
// Returns the rule's y (or `bottom` when there is nothing).
float records_row(Ctx& c, const std::vector<std::string>& cols, float left, float right, float bottom, float alpha)
{
    if (cols.empty())
        return bottom;
    Style s = mono12(kMuted, false);
    s.alpha = alpha;
    float top = bottom - 12.0f * 1.3f;
    float col_w = (right - left) / 3.0f;
    for (size_t i = 0; i < cols.size() && i < 3; ++i)
        draw_text(c.dl, s, c.f, c.f.get_x(left + col_w * static_cast<float>(i)), datum(s, c.f, c.f.get_y(top), 1.3f),
                  cols[i].c_str());
    float rule_y = top - 10.0f - 1.0f;
    c.dl->AddRectFilled(ImVec2(c.f.get_x(left), c.f.get_y(rule_y)),
                        ImVec2(c.f.get_x(right), c.f.get_y(rule_y) + std::max(1.0f, c.f.current_s(1.0f))),
                        get_col(kRule, alpha));
    return rule_y;
}

std::string get_clock(float seconds)
{
    int total = static_cast<int>(seconds);
    char text[32];
    if (total >= 3600)
        std::snprintf(text, sizeof(text), "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    else
        std::snprintf(text, sizeof(text), "%02d:%02d", total / 60, total % 60);
    return text;
}

std::string thousands(int n)
{
    std::string digits = std::to_string(n < 0 ? -n : n), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return n < 0 ? "-" + out : out;
}

// The pause menus' right side: the session so far (what is known) over its rule from `bottom`; the kicker ("PAUSED -
// the game's tab"), the big name (the level) and their colour. Returns the rule's y less a gap.
float pause_side(Ctx& c, float left, float right, float bottom, std::string* kicker, std::string* name,
                 uint32_t* colour)
{
    std::vector<std::string> cols;
    const Session& s = c.extras.session;
    if (s.best_combo_ >= 0)
        cols.push_back(std::string(text_tools::kLabelBestChain) + " " + thousands(s.best_combo_));
    if (s.gaps >= 0)
        cols.push_back(std::string(text_tools::kGaps) + " " + std::to_string(s.gaps) +
                       (s.gaps_total_ > 0 ? "/" + std::to_string(s.gaps_total_) : ""));
    if (s.seconds >= 0.0f)
        cols.push_back(std::string(text_tools::kTime) + " " + get_clock(s.seconds));
    float top = records_row(c, cols, left, right, bottom, 1.0f) - 10.0f;
    *kicker = std::string(text_tools::kTitlePaused) +
              (c.extras.level_kicker_.empty() ? std::string() : " \xE2\x80\x94 " + c.extras.level_kicker_);
    *name = c.extras.level_name_.empty() ? std::string(text_tools::kTitlePaused) : c.extras.level_name_;
    if (c.extras.level_colour_)
        *colour = c.extras.level_colour_;
    return top;
}

// Mono tabs (the level select's game tabs, the options' groups) from x 48 to 520 at design y `y`, wrapping: the
// selected one in its colour, underlined. Returns the bottom of the last row.
float get_tabs(Ctx& c, const std::vector<std::pair<std::string, uint32_t>>& tabs, int selected, float y)
{
    Style tab = mono12(kMuted, false);
    tab.css = 13.0f;
    float x = 48.0f;
    float tab_h = 13.0f * 1.3f + 8.0f;
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        float w = get_measure(tab, c.f, tabs[i].first.c_str()) / c.f.scale;
        if (x > 48.0f && x + w > kColumnInner) {
            x = 48.0f;
            y += tab_h + 4.0f;
        }
        bool on = i == selected;
        tab.colour = on ? tabs[i].second : kMuted;
        float base = datum(tab, c.f, c.f.get_y(y + 4.0f), 1.3f);
        draw_text(c.dl, tab, c.f, c.f.get_x(x), base, tabs[i].first.c_str());
        if (on)
            c.dl->AddRectFilled(ImVec2(c.f.get_x(x), base + c.f.current_s(5.0f)),
                                ImVec2(c.f.get_x(x + w), base + c.f.current_s(6.0f)), get_col(tabs[i].second));
        c.get_hit(Hit::Kind::kTab, i, x - 4.0f, y, x + w + 4.0f, y + tab_h);
        x += w + 16.0f;
    }
    return y + tab_h;
}

// A slanted button (the level select's Skate) with its text in Anton 30 ending at design y `bottom`.
void get_button(Ctx& c, float left, float bottom, const char* text, uint32_t colour, bool arrow, Hit::Kind kind,
                float alpha)
{
    Style s;
    s.face = Face::kDisplay;
    s.css = 30.0f;
    s.upper = true;
    s.colour = kGround;
    s.alpha = alpha;
    float tw = get_measure(s, c.f, text) / c.f.scale;
    float arrow_w = arrow ? 30.0f * 0.9f + 10.0f : 0.0f;
    float bh = 30.0f * 1.15f + 12.0f;
    float bx0 = left, bx1 = left + 22.0f + tw + arrow_w + 22.0f, by0 = bottom - bh, by1 = bottom;
    get_cut(c.dl, c.f.get_x(bx0), c.f.get_y(by0), c.f.get_x(bx1), c.f.get_y(by1), c.f.current_s(10.0f),
            c.f.current_s(10.0f), get_col(colour, alpha));
    float sb = datum(s, c.f, c.f.get_y(by0 + 6.0f), 1.15f);
    draw_text(c.dl, s, c.f, c.f.get_x(bx0 + 22.0f), sb, text);
    if (arrow) {
        float adv = 0.0f;
        get_arrow(c.dl, c.f.get_x(bx0 + 22.0f + tw + 10.0f), sb, c.f.current_s(30.0f), false, get_col(kGround, alpha),
                  &adv);
    }
    c.get_hit(kind, 0, bx0, by0, bx1, by1);
}

// The options: the groups as tabs, the group's settings down the left (Anton 40, each one's value in mono at the
// column's right), the selected one's name on the right with its choices as a segmented control, what it does, its
// ini key and when a change is seen.
void options_screen(Ctx& c, float window_width, float window_height)
{
    const Menu& menu = c.menu;
    const std::vector<options::Spec>& specs = options::get_specs();
    const std::vector<options::GroupSet>& groups = options::bunches();
    if (menu.over_pause()) {
        pause_ground(c, window_width, window_height);
    } else {
        backdrop(c.dl, c.f, art_for(c.extras, art_by_id(menu.payload(), "options")), menu.get_pan());
    }
    float rule = get_header(c, 36.0f, kColumnInner, text_tools::kTitleOptions, true, nullptr);
    std::vector<std::pair<std::string, uint32_t>> tabs;
    tabs.reserve(groups.size());
    for (const options::GroupSet& g : groups)
        tabs.emplace_back(g.name, Menu::kHouseYellow);
    float list_top = get_tabs(c, tabs, menu.option_group(), rule + 14.0f) + 18.0f;
    const std::vector<int>& rows = menu.option_rows();
    // (the options' rows: a size smaller than the level list's - their labels are long)
    RowStyle rs = level_row_style();
    rs.name.css = 28.0f;
    rs.row_height_ = 28.0f * 1.05f + 6.0f;
    float row_h = rs.row_height_;
    Style value = mono12(kMuted, false);
    int count = static_cast<int>(rows.size()) + 1;
    for (int i = 0; i < count; ++i) {
        float top = list_top + static_cast<float>(i) * row_h;
        bool action = i == static_cast<int>(rows.size());
        const char* label = action ? text_tools::kGameOptions : specs[rows[i]].label;
        // (a short label for the row: the design's 40 px type in a 470 px column)
        get_row(c, rs, top, pad2(i + 1).c_str(), label, menu.row_amount(i), Menu::kHouseYellow, false);
        std::string shown = action ? std::string(text_tools::kGameOptionsValue)
                                   : specs[rows[i]].choices[menu.option_choice(rows[i])].label;
        float base = datum(rs.name, c.f, c.f.get_y(top), rs.line_height_);
        value.colour = i == menu.option_row() ? Menu::kHouseYellow : kMuted;
        // (the value at the column's right, where the label leaves room for it)
        float value_x = c.f.get_x(kColumnInner) - get_measure(value, c.f, shown.c_str());
        if (c.f.get_x(rs.text_x_) + get_measure(rs.name, c.f, label) + c.f.current_s(40.0f) < value_x)
            draw_text(c.dl, value, c.f, value_x, base, shown.c_str());
        c.get_hit(Hit::Kind::kItem, i, 48.0f, top, kColumnInner + 20.0f, top + row_h);
    }

    // The right's foot.
    float left = kRight, right = 1240.0f;
    bool action = menu.option_row() >= static_cast<int>(rows.size());
    std::vector<std::string> hints =
        action ? std::vector<std::string>{text_tools::kHintOpen, text_tools::kHintGroup, text_tools::kHintBack}
               : std::vector<std::string>{text_tools::kHintChange, text_tools::kHintGroup, text_tools::kHintBack};
    float bottom = get_hints(c, hints, left, right, 692.0f) - 14.0f;
    Style body;
    body.face = Face::kBody;
    body.css = 14.0f;
    body.colour = kMuted;
    std::string name,
        kicker = std::string(groups[menu.option_group()].name) + " \xE2\x80\x94 " + pad2(menu.option_row() + 1);
    if (action) {
        get_button(c, left, bottom, text_tools::kOpen, Menu::kHouseYellow, true, Hit::Kind::kSkate, 1.0f);
        bottom -= 30.0f * 1.15f + 12.0f + 14.0f;
        draw_text(c.dl, body, c.f, c.f.get_x(left), datum(body, c.f, c.f.get_y(bottom - 21.0f), 1.5f),
                  text_tools::kGameOptionsHelp);
        bottom -= 21.0f + 10.0f;
        name = text_tools::kGameOptions;
    } else {
        int option = rows[menu.option_row()];
        const options::Spec& spec = specs[option];
        // The choices: a segmented control (mono 13; the chosen one filled).
        Style seg = mono12(kPaper, false);
        seg.css = 13.0f;
        float h = 13.0f * 1.3f + 16.0f;
        float x = left, y0 = bottom - h;
        int chosen = menu.option_choice(option);
        for (size_t i = 0; i < spec.choices.size(); ++i) {
            float w = get_measure(seg, c.f, spec.choices[i].label) / c.f.scale + 28.0f;
            bool on = static_cast<int>(i) == chosen;
            if (on)
                get_cut(c.dl, c.f.get_x(x), c.f.get_y(y0), c.f.get_x(x + w), c.f.get_y(bottom), c.f.current_s(6.0f),
                        c.f.current_s(6.0f), get_col(Menu::kHouseYellow));
            else
                c.dl->AddRect(ImVec2(c.f.get_x(x), c.f.get_y(y0)), ImVec2(c.f.get_x(x + w), c.f.get_y(bottom)),
                              get_col(kRule), 0.0f, 0, std::max(1.0f, c.f.current_s(1.0f)));
            seg.colour = on ? kGround : kPaper;
            draw_text(c.dl, seg, c.f, c.f.get_x(x + 14.0f), datum(seg, c.f, c.f.get_y(y0 + 8.0f), 1.3f),
                      spec.choices[i].label);
            c.get_hit(Hit::Kind::kChoice, menu.option_row() * 16 + static_cast<int>(i), x, y0, x + w, bottom);
            x += w + 6.0f;
        }
        bottom = y0 - 14.0f;
        // When it's seen ("Restart to apply" - yellow once changed; nothing for a live one), its ini key only behind
        // [frontend] show_ini_keys=1, what it does (when it has a line).
        bool pending = option < static_cast<int>(c.extras.restart_pending_.size()) && c.extras.restart_pending_[option];
        std::string when =
            spec.apply == options::Apply::kLive ? std::string() : std::string(text_tools::kRestartToApply);
        std::string where = c.extras.show_ini_keys_ ? std::string("[") + spec.section + "] " + spec.key : std::string();
        if (!when.empty() || !where.empty()) {
            Style key = mono12(kMuted2, false);
            if (!where.empty())
                draw_text(c.dl, key, c.f, c.f.get_x(left), datum(key, c.f, c.f.get_y(bottom - 15.6f), 1.3f),
                          where.c_str());
            Style note = mono12(pending ? Menu::kHouseYellow : kMuted2, false);
            if (!when.empty())
                draw_text(c.dl, note, c.f, c.f.get_x(right) - get_measure(note, c.f, when.c_str()),
                          datum(note, c.f, c.f.get_y(bottom - 15.6f), 1.3f), when.c_str());
            bottom -= 15.6f + 8.0f;
        }
        if (spec.help && *spec.help) {
            draw_text(c.dl, body, c.f, c.f.get_x(left), datum(body, c.f, c.f.get_y(bottom - 21.0f), 1.5f), spec.help);
            bottom -= 21.0f + 10.0f;
        }
        name = spec.label;
    }
    float name_top = big_name(c, name, 96.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    get_kicker(c, kicker, left, name_top - 8.0f, Menu::kHouseYellow);
    if (!c.extras.options_note_.empty()) {
        Style note = mono12(kMuted2, false);
        draw_text(c.dl, note, c.f, c.f.get_x(left), datum(note, c.f, c.f.get_y(36.0f), 1.3f),
                  c.extras.options_note_.c_str());
    }
}

// The level select.
void levels_screen(Ctx& c, int* visible_rows)
{
    const Menu& menu = c.menu;
    const Catalogue* data = menu.payload();
    const SectionView* view = menu.get_view();
    int section_index = menu.get_section_index();
    const Section* section = data && section_index >= 0 && section_index < static_cast<int>(data->sections.size())
                                 ? &data->sections[section_index]
                                 : nullptr;
    uint32_t colour = section ? section->colour : Menu::kHouseYellow;
    const StageMap* level = menu.selected_level();
    float launch = menu.get_launch();
    float grow = launch >= 0.0f ? ease_out(launch * 1.4f) : 0.0f;

    // The level's loading screen behind the whole screen, the column's scrim over it (a launch: the scrim and the
    // list go under the sharp picture), the thin edge in the game's colour.
    Art art = art_for(c.extras, level ? level->current_art() : std::string());
    backdrop(c.dl, c.f, art, menu.get_pan());
    c.dl->AddRectFilled(ImVec2(c.f.get_x(kColumn), c.f.get_y(0.0f)),
                        ImVec2(c.f.get_x(kColumn + 4.0f), c.f.get_y(720.0f)), get_col(colour, 1.0f - grow));

    if (grow < 0.999f) {
        c.dl->PushClipRect(ImVec2(c.f.get_x(0.0f), c.f.get_y(0.0f)),
                           ImVec2(c.f.get_x(kColumn + kScrimFeather), c.f.get_y(720.0f)), true);
        const char* title = menu.levels_purpose() == LevelsFor::kEdit     ? text_tools::kTitleEditLevel
                            : menu.levels_purpose() == LevelsFor::kFreeze ? text_tools::kTitleChangeLevel
                                                                          : text_tools::kTitleFreeSkate;
        float rule = get_header(c, 36.0f, kColumnInner, title, true, nullptr);
        // The game tabs (mono 13, wrapping; the selected one in its colour, underlined).
        std::vector<std::pair<std::string, uint32_t>> tabs;
        for (int i = 0; data && i < static_cast<int>(data->sections.size()); ++i)
            tabs.emplace_back(data->sections[i].tab, data->sections[i].colour);
        float tabs_bottom = get_tabs(c, tabs, section_index, rule + 14.0f);
        float tab_h = 0.0f, y = tabs_bottom; // The list.
        float list_top = y + tab_h + 18.0f;  // (tab_h 0: Tabs gave the bottom of its last row)
        float list_bottom = 692.0f;
        int fit = std::max(1, static_cast<int>((list_bottom - list_top) / kLevelRowHeight));
        if (visible_rows)
            *visible_rows = fit;
        RowStyle rs = level_row_style();
        c.dl->PushClipRect(ImVec2(c.f.get_x(0.0f), c.f.get_y(list_top - 2.0f)),
                           ImVec2(c.f.get_x(kColumn + kScrimFeather), c.f.get_y(list_bottom + 8.0f)), true);
        float scroll = menu.list_scroll();
        int count = view ? static_cast<int>(view->levels.size()) : 0;
        int first = std::max(0, static_cast<int>(std::floor(scroll)) - 1);
        int last = std::min(count - 1, static_cast<int>(std::ceil(scroll)) + fit + 1);
        for (int row = first; row <= last; ++row) {
            const StageMap& l = section->levels[view->levels[row]];
            float top = list_top + (static_cast<float>(row) - scroll) * kLevelRowHeight;
            bool locked = row < static_cast<int>(view->locked.size()) && view->locked[row];
            get_row(c, rs, top, pad2(row + 1).c_str(), l.name.c_str(), menu.row_amount(row), colour, locked);
            if (top >= list_top - 1.0f && top + kLevelRowHeight <= list_bottom + 1.0f)
                c.get_hit(Hit::Kind::kStageMap, row, 48.0f, top, kColumnInner + 20.0f, top + kLevelRowHeight);
        }
        c.dl->PopClipRect();
        if (count == 0) {
            Style empty = mono12(kMuted2);
            draw_text(c.dl, empty, c.f, c.f.get_x(48.0f), datum(empty, c.f, c.f.get_y(list_top), 1.3f),
                      text_tools::kNoLevels);
        }
        c.dl->PopClipRect();
    }
    if (grow > 0.0f) {
        // (the launch: the picture comes up sharp over the column)
        c.dl->PushClipRect(ImVec2(c.f.get_x(0.0f), c.f.get_y(0.0f)),
                           ImVec2(c.f.get_x(kColumn + kScrimFeather), c.f.get_y(720.0f)), true);
        art_box(c.dl, art, c.f.get_x(0.0f), c.f.get_y(0.0f), c.f.get_x(1280.0f), c.f.get_y(720.0f), menu.get_pan(),
                grow);
        c.dl->PopClipRect();
    }

    // The picture's number at the top right (Q / E change it) - only for a level with more than one.
    if (level && level->art_variants_.size() > 1 && grow < 0.999f) {
        char text[64];
        if (level->variant >= 0 && level->variant < static_cast<int>(level->art_labels_.size()))
            std::snprintf(text, sizeof(text), "Q  %s  E", level->art_labels_[level->variant].c_str());
        else
            std::snprintf(text, sizeof(text), "Q  %d / %d  E", level->variant + 1,
                          static_cast<int>(level->art_variants_.size()));
        Style note = mono12(kMuted);
        note.alpha = 1.0f - grow;
        float nw = get_measure(note, c.f, text) / c.f.scale;
        draw_text(c.dl, note, c.f, c.f.get_x(1240.0f - nw), datum(note, c.f, c.f.get_y(36.0f), 1.3f), text);
    }

    // The right's foot: Skate, the records, the name, the kicker.
    float left = kRight + (32.0f - kRight) * grow, right = 1240.0f;
    float bottom = 692.0f;
    if (level) {
        float fade = 1.0f - grow;
        // Skate (Anton 30 on the game's colour, a slanted button).
        Style skate;
        skate.face = Face::kDisplay;
        skate.css = 30.0f;
        skate.upper = true;
        skate.colour = kGround;
        skate.alpha = fade;
        bool locked = menu.selected_locked();
        const char* skate_text = locked                                      ? text_tools::kLocked
                                 : menu.levels_purpose() == LevelsFor::kEdit ? text_tools::kEdit
                                                                             : text_tools::kSkate;
        float tw = get_measure(skate, c.f, skate_text) / c.f.scale;
        float arrow_w = locked ? 0.0f : 30.0f * 0.9f + 10.0f;
        float bh = 30.0f * 1.15f + 12.0f;
        float bx0 = left, bx1 = left + 22.0f + tw + arrow_w + 22.0f, by0 = bottom - bh, by1 = bottom;
        get_cut(c.dl, c.f.get_x(bx0), c.f.get_y(by0), c.f.get_x(bx1), c.f.get_y(by1), c.f.current_s(10.0f),
                c.f.current_s(10.0f), get_col(locked ? kMuted2 : colour, fade));
        float sb = datum(skate, c.f, c.f.get_y(by0 + 6.0f), 1.15f);
        draw_text(c.dl, skate, c.f, c.f.get_x(bx0 + 22.0f), sb, skate_text);
        if (!locked) {
            float adv = 0.0f;
            get_arrow(c.dl, c.f.get_x(bx0 + 22.0f + tw + 10.0f), sb, c.f.current_s(30.0f), false,
                      get_col(kGround, fade), &adv);
        }
        c.get_hit(Hit::Kind::kSkate, 0, bx0, by0, bx1, by1);
        float y = by0 - 6.0f;
        // The records (only what the career keeps for this level).
        Highscores rec = c.extras.records ? c.extras.records(*level) : Highscores{};
        std::vector<std::string> cols;
        char text[64];
        if (rec.gaps_total_ > 0) {
            std::snprintf(text, sizeof(text), "%s %d/%d", text_tools::kGaps, rec.gaps_got_, rec.gaps_total_);
            cols.emplace_back(text);
        }
        if (rec.goals >= 0) {
            std::snprintf(text, sizeof(text), "%s %d", text_tools::kGoals, rec.goals);
            cols.emplace_back(text);
        }
        if (!cols.empty()) {
            Style s = mono12(kMuted, false);
            s.alpha = fade;
            float top = y - 10.0f - 12.0f * 1.3f;
            float col_w = (right - left) / 3.0f;
            for (size_t i = 0; i < cols.size(); ++i)
                draw_text(c.dl, s, c.f, c.f.get_x(left + col_w * static_cast<float>(i)),
                          datum(s, c.f, c.f.get_y(top), 1.3f), cols[i].c_str());
            float rule_y = top - 10.0f - 1.0f;
            c.dl->AddRectFilled(ImVec2(c.f.get_x(left), c.f.get_y(rule_y)),
                                ImVec2(c.f.get_x(right), c.f.get_y(rule_y) + std::max(1.0f, c.f.current_s(1.0f))),
                                get_col(kRule, fade));
            y = rule_y;
        }
        float slice = launch >= 0.0f ? ease_out(launch * 1.6f) : 0.0f;
        float name_top = big_name(c, level->name, 112.0f, left, right, y - 10.0f, slice, colour);
        std::string kicker = (section ? section->tab : std::string()) + " \xE2\x80\x94 " + pad2(menu.level_row() + 1);
        if (locked)
            kicker += std::string(" \xC2\xB7 ") + text_tools::kLocked;
        get_kicker(c, kicker, left, name_top - 10.0f, colour, 1.0f - slice);
    }
}

// The screen change: one diagonal band in `colour` across the window - in over the first half (the screen changes
// under it), out over the second.
void wipe(ImDrawList* dl, float t, uint32_t colour, float width, float height)
{
    if (t < 0.0f)
        return;
    float skew = height * 0.35f;
    float lead = -skew + (width + skew * 2.0f) * ease_out(std::min(1.0f, t * 2.0f));
    float trail = -skew + (width + skew * 2.0f) * ease_out(std::max(0.0f, t * 2.0f - 1.0f));
    if (lead <= trail)
        return;
    ImVec2 points[4] = {{trail + skew, 0.0f}, {lead + skew, 0.0f}, {lead, height}, {trail, height}};
    dl->AddConvexPolyFilled(points, 4, get_col(colour));
}

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

Style get_body(uint32_t colour = kMuted)
{
    Style s;
    s.face = Face::kBody;
    s.css = 14.0f;
    s.colour = colour;
    return s;
}

// Paragraphs of body text from `top` (design y) between `left` and `right`, at most `max_lines` lines. Returns the
// bottom.
float paragraphs(Ctx& c, const std::vector<std::string>& texts, float left, float right, float top, int max_lines,
                 uint32_t colour = kMuted)
{
    Style body = get_body(colour);
    float line_h = 14.0f * 1.5f;
    int drawn = 0;
    for (const std::string& text : texts) {
        // (each \n of a dialog's text a line of its own)
        size_t at = 0;
        while (at <= text.size() && drawn < max_lines) {
            size_t end = text.find('\n', at);
            std::string part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            for (const std::string& line : get_wrap(body, c.f, part, c.f.current_s(right - left))) {
                if (drawn >= max_lines)
                    break;
                draw_text(c.dl, body, c.f, c.f.get_x(left), datum(body, c.f, c.f.get_y(top), 1.5f), line.c_str());
                top += line_h;
                ++drawn;
            }
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
        top += 6.0f;
    }
    return top;
}

// A dialog box: the screen dimmed, a band across it - the kicker and the title (Anton) with its text on the left,
// the choices down the right (the level list's rows, the kinetic selection).
void dialog_screen(Ctx& c, const mirror::Tracker& t, float window_width, float window_height)
{
    const mirror::View& v = t.get_shown();
    c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(window_width, window_height), get_col(kGround, 0.62f));
    float y0 = 196.0f, y1 = 548.0f;
    c.dl->AddRectFilled(ImVec2(0.0f, c.f.get_y(y0)), ImVec2(window_width, c.f.get_y(y1)), get_col(kGround, 0.94f));
    c.dl->AddRectFilled(ImVec2(0.0f, c.f.get_y(y0)), ImVec2(window_width, c.f.get_y(y0 + 4.0f)),
                        get_col(Menu::kHouseYellow));
    float left = 96.0f, text_right = 700.0f;
    int choices = 0;
    for (const mirror::Row& r : v.rows)
        choices += r.focusable ? 1 : 0;
    const char* kicker = choices >= 2   ? text_tools::kDialogConfirm
                         : choices == 1 ? text_tools::kDialogNotice
                                        : text_tools::kPleaseWait;
    float kicker_top = get_kicker(c, kicker, left, y0 + 44.0f, Menu::kHouseYellow);
    (void)kicker_top;
    // The title (Anton 72, up to two lines) under the kicker.
    Style title;
    title.face = Face::kDisplay;
    title.css = 72.0f;
    title.upper = true;
    std::vector<std::string> title_lines = get_wrap(title, c.f, v.title, c.f.current_s(text_right - left));
    if (title_lines.size() > 2)
        title_lines.resize(2);
    float y = y0 + 56.0f;
    for (const std::string& line : title_lines) {
        draw_text(c.dl, title, c.f, c.f.get_x(left), datum(title, c.f, c.f.get_y(y), 0.9f), line.c_str());
        y += 72.0f * 0.9f;
    }
    paragraphs(c, v.lines, left, text_right, y + 14.0f, 6, kPaper);
    // The choices.
    RowStyle rs = level_row_style();
    float rows_left = 760.0f;
    rs.text_x_ = rows_left + 26.0f + 14.0f + 10.0f;
    rs.cut_left_ = rows_left + 26.0f + 14.0f - 6.0f;
    int n = static_cast<int>(v.rows.size());
    float top = (y0 + y1) * 0.5f - static_cast<float>(n) * kLevelRowHeight * 0.5f;
    for (int i = 0; i < n; ++i) {
        float row_top = top + static_cast<float>(i) * kLevelRowHeight;
        // (Row puts the number at the design's x 48: drawn here on the right instead)
        ImDrawList* dl = c.dl;
        int start = dl->VtxBuffer.Size;
        float e = t.row_amount(i);
        float base = datum(rs.name, c.f, c.f.get_y(row_top), rs.line_height_);
        float width = get_measure(rs.name, c.f, v.rows[i].label.c_str());
        if (e > 0.001f) {
            float x0 = c.f.get_x(rs.cut_left_), x1 = c.f.get_x(rs.text_x_) + width + c.f.current_s(rs.cut_right_pad_);
            x1 = x0 + (x1 - x0) * e;
            float line_h = rs.name.css * rs.line_height_;
            get_cut(dl, x0, c.f.get_y(row_top + line_h * rs.cut_top_), x1, c.f.get_y(row_top + line_h * rs.cut_bottom_),
                    c.f.current_s(rs.top_inset_), c.f.current_s(rs.bottom_inset_), get_col(Menu::kHouseYellow));
        }
        Style name = rs.name;
        name.colour = get_mix(v.rows[i].focusable ? kPaper : kMuted2, kGround, e);
        draw_text(dl, name, c.f, c.f.get_x(rs.text_x_), base, v.rows[i].label.c_str());
        draw_text(dl, rs.number, c.f, c.f.get_x(rows_left), base, pad2(i + 1).c_str());
        if (e > 0.001f)
            xform(dl, start, c.f.get_x(rows_left), c.f.get_y(row_top + kLevelRowHeight * 0.5f),
                  -3.0f * 3.14159265f / 180.0f * e, c.f.current_s(24.0f) * e, 0.0f);
        if (v.rows[i].focusable)
            c.get_hit(Hit::Kind::kItem, i, rows_left, row_top, 1240.0f, row_top + kLevelRowHeight);
    }
    std::vector<std::string> hints = {text_tools::kHintSelect, text_tools::kHintBack};
    if (choices == 0)
        hints = {text_tools::kPleaseWait};
    get_hints(c, hints, left, 1184.0f, y1 - 18.0f);
}

// The skater select: the game's 3D skater stays in view (left of centre, as the original's camera puts it); the
// skaters as a strip across the top, the selected one's name and stats on a panel on the right.
void skaters_screen(Ctx& c, const mirror::Tracker& t, float window_width, float window_height)
{
    const mirror::View& v = t.get_shown();
    // The top: a shade for the strip and the header.
    c.dl->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(window_width, c.f.get_y(170.0f)), get_col(kGround, 0.92f),
                                  get_col(kGround, 0.92f), get_col(kGround, 0.0f), get_col(kGround, 0.0f));
    // The right: a panel feathered from x 720.
    float panel = 800.0f;
    c.dl->AddRectFilledMultiColor(ImVec2(c.f.get_x(panel - 90.0f), 0.0f), ImVec2(c.f.get_x(panel), window_height),
                                  get_col(kGround, 0.0f), get_col(kGround, 0.86f), get_col(kGround, 0.86f),
                                  get_col(kGround, 0.0f));
    c.dl->AddRectFilled(ImVec2(c.f.get_x(panel), 0.0f), ImVec2(window_width, window_height), get_col(kGround, 0.86f));
    // The foot: a shade under the hints.
    c.dl->AddRectFilledMultiColor(ImVec2(0.0f, c.f.get_y(600.0f)), ImVec2(window_width, window_height),
                                  get_col(kGround, 0.0f), get_col(kGround, 0.0f), get_col(kGround, 0.9f),
                                  get_col(kGround, 0.9f));

    std::string count =
        v.selected >= 0 ? pad2(v.selected + 1) + " / " + pad2(static_cast<int>(v.rows.size())) : std::string();
    get_header(c, 36.0f, 1232.0f, v.title.c_str(), true, count.c_str());

    // The strip: every skater's name (Anton 28), the selected one centred, cut and turned.
    Style name;
    name.face = Face::kDisplay;
    name.css = 28.0f;
    name.upper = true;
    float gap = 34.0f;
    std::vector<float> widths;
    float total = 0.0f, selected_centre = 0.0f;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        widths.push_back(get_measure(name, c.f, v.rows[i].label.c_str()) / c.f.scale);
        if (static_cast<int>(i) == v.selected)
            selected_centre = total + widths.back() * 0.5f;
        total += widths.back() + gap;
    }
    float shift = 640.0f - selected_centre; // (the strip's x 0 in design px)
    shift = std::min(shift, 48.0f);
    shift = std::max(shift, 1232.0f - (total - gap));
    if (total - gap < 1184.0f)
        shift = 48.0f;
    float strip_top = 88.0f;
    c.dl->PushClipRect(ImVec2(c.f.get_x(0.0f), c.f.get_y(strip_top - 20.0f)),
                       ImVec2(c.f.get_x(1280.0f), c.f.get_y(strip_top + 60.0f)), true);
    float x = shift;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        float e = t.row_amount(static_cast<int>(i));
        ImDrawList* dl = c.dl;
        int start = dl->VtxBuffer.Size;
        float line_h = 28.0f * 1.05f;
        if (e > 0.001f)
            get_cut(dl, c.f.get_x(x - 10.0f), c.f.get_y(strip_top + line_h * 0.12f),
                    c.f.get_x(x - 10.0f) + (c.f.current_s(widths[i] + 26.0f)) * e,
                    c.f.get_y(strip_top + line_h * 0.98f), c.f.current_s(7.0f), c.f.current_s(10.0f),
                    get_col(Menu::kHouseYellow));
        Style s = name;
        s.colour = get_mix(v.rows[i].focusable ? kPaper : kMuted2, kGround, e);
        draw_text(dl, s, c.f, c.f.get_x(x), datum(s, c.f, c.f.get_y(strip_top), 1.05f), v.rows[i].label.c_str());
        if (e > 0.001f)
            xform(dl, start, c.f.get_x(x), c.f.get_y(strip_top + line_h * 0.5f), -3.0f * 3.14159265f / 180.0f * e, 0.0f,
                  -c.f.current_s(4.0f) * e);
        if (v.rows[i].focusable && x + widths[i] > 0.0f && x < 1280.0f)
            c.get_hit(Hit::Kind::kItem, static_cast<int>(i), x - 10.0f, strip_top - 6.0f, x + widths[i] + 16.0f,
                      strip_top + line_h + 6.0f);
        x += widths[i] + gap;
    }
    c.dl->PopClipRect();
    // (a rule under the strip)
    c.dl->AddRectFilled(ImVec2(c.f.get_x(48.0f), c.f.get_y(strip_top + 48.0f)),
                        ImVec2(c.f.get_x(1232.0f), c.f.get_y(strip_top + 48.0f) + std::max(1.0f, c.f.current_s(1.0f))),
                        get_col(kRule));

    // The panel: the stats as a mono table with bars, the name over it, the kicker.
    float left = panel + 40.0f, right = 1232.0f;
    float bottom =
        get_hints(
            c, {text_tools::kHintSkater, text_tools::kHintAccept, text_tools::kHintBack, text_tools::kHintLoadSkater},
            48.0f, 1232.0f, 692.0f) -
        22.0f;
    Style label = mono12(kMuted, false);
    label.css = 13.0f;
    Style number = mono12(kPaper, false);
    number.css = 13.0f;
    float row_h = 26.0f;
    int stats = static_cast<int>(v.stats.size());
    float table_top = bottom - row_h * static_cast<float>(stats);
    for (int i = 0; i < stats; ++i) {
        float top = table_top + row_h * static_cast<float>(i);
        float base = datum(label, c.f, c.f.get_y(top + 4.0f), 1.3f);
        draw_text(c.dl, label, c.f, c.f.get_x(left), base, v.stats[i].first.c_str());
        int value = std::clamp(v.stats[i].second, 0, 10);
        float bar_left = left + 110.0f, bar_right = right - 40.0f;
        float seg = (bar_right - bar_left) / 10.0f;
        for (int k = 0; k < 10; ++k) {
            float sx = bar_left + seg * static_cast<float>(k);
            get_cut(c.dl, c.f.get_x(sx), c.f.get_y(top + 7.0f), c.f.get_x(sx + seg - 4.0f), c.f.get_y(top + 19.0f),
                    c.f.current_s(3.0f), c.f.current_s(3.0f), get_col(k < value ? Menu::kHouseYellow : kRule));
        }
        std::string text = std::to_string(v.stats[i].second);
        draw_text(c.dl, number, c.f, c.f.get_x(right) - get_measure(number, c.f, text.c_str()), base, text.c_str());
    }
    if (stats > 0) {
        float rule = table_top - 12.0f;
        c.dl->AddRectFilled(ImVec2(c.f.get_x(left), c.f.get_y(rule)),
                            ImVec2(c.f.get_x(right), c.f.get_y(rule) + std::max(1.0f, c.f.current_s(1.0f))),
                            get_col(kRule));
        bottom = rule - 14.0f;
    }
    float name_top = big_name(c, v.big_name_, 84.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    std::string kicker = text_tools::kSkater;
    if (v.selected >= 0 && !v.rows[v.selected].focusable)
        kicker += std::string(" \xC2\xB7 ") + text_tools::kLocked;
    get_kicker(c, kicker, left, name_top - 8.0f, Menu::kHouseYellow);
}

// A colour menu's slider track (Mirror::Row::track 1 hue, 2 saturation, 3 value, 4 alpha, 5 any other bar): a thin rail
// in the colour the slider sets - the hue spectrum at the colour's saturation limit and value, grey to the full saturation
// at the colour's hue and value, dark to bright at its hue and saturation - the colours as the skater shows them
// (Mirror::skater_colour), and a marker where the value stands. The numbers are the menu's own (View::hue / sat / val).
ImU32 skater_col(float hue, float sat, float val)
{
    float rgb[3];
    mirror::skater_colour(hue, sat, val, rgb);
    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
    return IM_COL32(static_cast<int>(rgb[0] * 255.0f + 0.5f), static_cast<int>(rgb[1] * 255.0f + 0.5f),
                    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                    static_cast<int>(rgb[2] * 255.0f + 0.5f), 255);
}

void colour_track(Ctx& c, const mirror::View& v, const mirror::Row& r, bool selected, float x0, float x1, float y_mid)
{
    constexpr float kSatLimit = 90.0f, kValLow = 12.0f, kValHigh = 60.0f;
    float hue = v.has_colour_ ? v.hue : 0.0f, sat = v.has_colour_ ? v.sat : kSatLimit,
          val = v.has_colour_ ? v.val : 40.0f;
    float rail_half = 3.0f;
    float px0 = c.f.get_x(x0), px1 = c.f.get_x(x1), py0 = c.f.get_y(y_mid - rail_half),
          py1 = c.f.get_y(y_mid + rail_half);
    int steps = r.track == 1 ? 12 : 8;
    if (r.track >= 1 && r.track <= 3) {
        for (int k = 0; k < steps; ++k) {
            float a = static_cast<float>(k) / static_cast<float>(steps),
                  b = static_cast<float>(k + 1) / static_cast<float>(steps);
            auto at = [&](float t) {
                if (r.track == 1)
                    return skater_col(t * 360.0f, kSatLimit, val);
                if (r.track == 2)
                    return skater_col(hue, t * kSatLimit, val);
                return skater_col(hue, sat, kValLow + t * (kValHigh - kValLow));
            };
            c.dl->AddRectFilledMultiColor(ImVec2(px0 + (px1 - px0) * a, py0), ImVec2(px0 + (px1 - px0) * b, py1), at(a),
                                          at(b), at(b), at(a));
        }
    } else {
        c.dl->AddRectFilled(ImVec2(px0, py0), ImVec2(px1, py1), get_col(kRule));
        c.dl->AddRectFilled(ImVec2(px0, py0), ImVec2(px0 + (px1 - px0) * std::clamp(r.track_at_, 0.0f, 1.0f), py1),
                            get_col(selected ? Menu::kHouseYellow : 0xFF8C8C86));
    }
    c.dl->AddRect(ImVec2(px0, py0), ImVec2(px1, py1), get_col(0xFF000000, 0.6f), 0.0f, 0,
                  std::max(1.0f, c.f.current_s(1.0f)));
    // The marker: a post over the rail, ink-edged, the accent when its row is the selected one.
    float mx = px0 + (px1 - px0) * std::clamp(r.track_at_, 0.0f, 1.0f);
    float half = std::max(1.5f, c.f.current_s(2.0f)), top = c.f.get_y(y_mid - 9.0f), bottom = c.f.get_y(y_mid + 9.0f);
    if (r.track >= 1 && r.track <= 4) {
        c.dl->AddRectFilled(ImVec2(mx - half - 1.0f, top - 1.0f), ImVec2(mx + half + 1.0f, bottom + 1.0f),
                            get_col(0xFF000000));
        c.dl->AddRectFilled(ImVec2(mx - half, top), ImVec2(mx + half, bottom),
                            get_col(selected ? Menu::kHouseYellow : 0xFFE8E8E0));
    }
}

// A plain list: the pause menus' look - the screen dimmed, the column's scrim, the items numbered down the left (each
// one's value in mono at the column's right), the selected item's name on the right with the screen's other words.
void list_of_items(Ctx& c, const mirror::Tracker& t, float window_width, float window_height, float* scroll)
{
    const mirror::View& v = t.get_shown();
    pause_ground(c, window_width, window_height, v.editor ? 0.15f : 0.55f); // (an editor's view shows through)
    float rule =
        get_header(c, 40.0f, kColumnInner, v.title.empty() ? text_tools::kTitleMenu : v.title.c_str(), true, nullptr);
    int n = static_cast<int>(v.rows.size());
    bool big = n <= 6;
    if (big) {
        // (big type only while every label - and its value, when it has one - fits the column)
        RowStyle probe = main_row_style();
        Style probe_value = mono12(kMuted, false);
        for (const mirror::Row& r : v.rows) {
            float need = c.f.current_s(probe.text_x_) + get_measure(probe.name, c.f, r.label.c_str());
            if (!r.value.empty())
                need += c.f.current_s(24.0f) + get_measure(probe_value, c.f, r.value.c_str());
            if (need > c.f.current_s(kColumnInner + 10.0f))
                big = false;
        }
    }
    RowStyle rs = big ? main_row_style() : level_row_style();
    float row_h = big ? kMainRowHeight + 2.0f : kLevelRowHeight;
    float list_top = rule + (big ? 36.0f : 20.0f);
    float list_bottom = 620.0f;
    int fit = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    // (the selection kept in view, a row of margin)
    float first = scroll ? *scroll : 0.0f;
    if (v.selected >= 0) {
        if (static_cast<float>(v.selected) < first + 1.0f)
            first = static_cast<float>(v.selected) - 1.0f;
        if (static_cast<float>(v.selected) > first + static_cast<float>(fit) - 2.0f)
            first = static_cast<float>(v.selected) - static_cast<float>(fit) + 2.0f;
    }
    first = std::clamp(first, 0.0f, static_cast<float>(std::max(0, n - fit)));
    if (scroll)
        *scroll = first;
    Style value = mono12(kMuted, false);
    c.dl->PushClipRect(ImVec2(c.f.get_x(0.0f), c.f.get_y(list_top - 2.0f)),
                       ImVec2(c.f.get_x(kColumn + kScrimFeather), c.f.get_y(list_bottom + 8.0f)), true);
    for (int i = 0; i < n; ++i) {
        float top = list_top + (static_cast<float>(i) - first) * row_h;
        if (top < list_top - row_h || top > list_bottom)
            continue;
        const mirror::Row& r = v.rows[i];
        get_row(c, rs, top, pad2(i + 1).c_str(), r.label.c_str(), t.row_amount(i), Menu::kHouseYellow, !r.focusable);
        if (!r.value.empty()) {
            float base = datum(rs.name, c.f, c.f.get_y(top + rs.row_pad_top_), rs.line_height_);
            value.colour = i == v.selected ? Menu::kHouseYellow : kMuted;
            float value_x = c.f.get_x(kColumnInner) - get_measure(value, c.f, r.value.c_str());
            if (c.f.get_x(rs.text_x_) + get_measure(rs.name, c.f, r.label.c_str()) + c.f.current_s(24.0f) < value_x)
                draw_text(c.dl, value, c.f, value_x, base, r.value.c_str());
        }
        if (r.track > 0 && r.focusable) {
            // (between the label and the number: from past the widest label to a fixed width, at most; a selected row's
            // label sits in its cut, shifted right by Row's 24 px - the track starts past the cut's end)
            float value_w =
                r.value.empty() ? 0.0f : get_measure(value, c.f, r.value.c_str()) / c.f.current_s(1.0f) + 14.0f;
            float label_end = rs.text_x_ + get_measure(rs.name, c.f, r.label.c_str()) / c.f.current_s(1.0f) + 16.0f +
                              t.row_amount(i) * (rs.cut_right_pad_ + 24.0f + 4.0f);
            float x1 = kColumnInner - value_w, x0 = std::max(label_end, x1 - 190.0f);
            if (x1 - x0 > 40.0f)
                colour_track(c, v, r, i == v.selected, x0, x1, top + row_h * 0.5f);
        }
        if (r.focusable && top >= list_top - 1.0f && top + row_h <= list_bottom + 1.0f)
            c.get_hit(Hit::Kind::kItem, i, 48.0f, top, kColumnInner + 20.0f, top + row_h);
    }
    c.dl->PopClipRect();

    // The right: the hints, the screen's other words, the selected item (its value under it), the title as the kicker.
    float left = kRight, right = 1240.0f;
    float bottom =
        get_hints(c, {text_tools::kHintSelect, text_tools::kHintChange, text_tools::kHintBack}, left, right, 688.0f) -
        12.0f;
    if (v.has_colour_) {
        // The colour chosen, as the skater shows it, with its numbers.
        float swatch_top = bottom - 30.0f;
        ImVec2 a(c.f.get_x(left), c.f.get_y(swatch_top)), b(c.f.get_x(left + 64.0f), c.f.get_y(swatch_top + 30.0f));
        c.dl->AddRectFilled(a, b, skater_col(v.hue, v.sat, v.val));
        c.dl->AddRect(a, b, get_col(kRule), 0.0f, 0, std::max(1.0f, c.f.current_s(1.0f)));
        Style d = mono12(kMuted, false);
        d.css = 12.0f;
        char numbers[48];
        // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
        std::snprintf(numbers, sizeof(numbers), "H %d  S %d  V %d", static_cast<int>(v.hue + 0.5f),
                      // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                      static_cast<int>(v.sat + 0.5f), static_cast<int>(v.val + 0.5f));
        draw_text(c.dl, d, c.f, c.f.get_x(left + 76.0f), datum(d, c.f, c.f.get_y(swatch_top + 8.0f), 1.3f), numbers);
        bottom = swatch_top - 12.0f;
    }
    if (v.editor) {
        // (an editor's view - the skater, the park - keeps the right: only the selected item's value over the hints)
        const mirror::Row* sel = v.selected >= 0 ? &v.rows[v.selected] : nullptr;
        if (sel && !sel->value.empty()) {
            Style d = mono12(Menu::kHouseYellow, false);
            d.css = 13.0f;
            draw_text(c.dl, d, c.f, c.f.get_x(left), datum(d, c.f, c.f.get_y(bottom - 13.0f * 1.3f), 1.3f),
                      sel->value.c_str());
        }
        return;
    }
    const mirror::Row* sel = v.selected >= 0 ? &v.rows[v.selected] : nullptr;
    if (sel && !sel->value.empty()) {
        Style d = mono12(Menu::kHouseYellow, false);
        d.css = 13.0f;
        float top = bottom - 13.0f * 1.3f;
        draw_text(c.dl, d, c.f, c.f.get_x(left), datum(d, c.f, c.f.get_y(top), 1.3f), sel->value.c_str());
        bottom = top - 12.0f;
    }
    float name_top = big_name(c, v.big_name_, 96.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    get_kicker(c, v.title.empty() ? std::string(text_tools::kTitleMenu) : v.title, left, name_top - 8.0f,
               Menu::kHouseYellow);
    if (!v.lines.empty())
        paragraphs(c, v.lines, left, right, 48.0f, 10);
}

// A level's pause menu restyled (Kind::Freeze): the game's items as numbered rows on the left (the main menu's type
// while they fit, the level list's past that), the level and the session on the right - the pause design, the menu
// itself the game's.
void pause_of_items(Ctx& c, const mirror::Tracker& t, float window_width, float window_height, float* scroll)
{
    const mirror::View& v = t.get_shown();
    pause_ground(c, window_width, window_height);
    float rule = get_header(c, 40.0f, kColumnInner, text_tools::kTitlePaused, false, nullptr);
    int n = static_cast<int>(v.rows.size());
    bool big = n <= 7;
    if (big) {
        RowStyle probe = main_row_style();
        for (const mirror::Row& r : v.rows)
            if (c.f.current_s(probe.text_x_) + get_measure(probe.name, c.f, r.label.c_str()) >
                c.f.current_s(kColumnInner + 10.0f))
                big = false;
    }
    RowStyle rs = big ? main_row_style() : level_row_style();
    float row_h = big ? kMainRowHeight + 2.0f : kLevelRowHeight;
    float list_top = rule + (big ? 36.0f : 20.0f);
    float list_bottom = 680.0f;
    int fit = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    float first = scroll ? *scroll : 0.0f;
    if (v.selected >= 0) {
        if (static_cast<float>(v.selected) < first + 1.0f)
            first = static_cast<float>(v.selected) - 1.0f;
        if (static_cast<float>(v.selected) > first + static_cast<float>(fit) - 2.0f)
            first = static_cast<float>(v.selected) - static_cast<float>(fit) + 2.0f;
    }
    first = std::clamp(first, 0.0f, static_cast<float>(std::max(0, n - fit)));
    if (scroll)
        *scroll = first;
    c.dl->PushClipRect(ImVec2(c.f.get_x(0.0f), c.f.get_y(list_top - 2.0f)),
                       ImVec2(c.f.get_x(kColumn + kScrimFeather), c.f.get_y(list_bottom + 8.0f)), true);
    for (int i = 0; i < n; ++i) {
        float top = list_top + (static_cast<float>(i) - first) * row_h;
        if (top < list_top - row_h || top > list_bottom)
            continue;
        const mirror::Row& r = v.rows[i];
        get_row(c, rs, top, pad2(i + 1).c_str(), r.label.c_str(), t.row_amount(i), Menu::kHouseYellow, !r.focusable);
        if (r.focusable && top >= list_top - 1.0f && top + row_h <= list_bottom + 1.0f)
            c.get_hit(Hit::Kind::kItem, i, 48.0f, top, kColumnInner + 20.0f, top + row_h);
    }
    c.dl->PopClipRect();

    float left = kRight, right = 1240.0f;
    float bottom = get_hints(c, {text_tools::kHintSelect, text_tools::kHintResumeLevel}, left, right, 688.0f) - 8.0f;
    std::string kicker, name;
    uint32_t colour = Menu::kHouseYellow;
    bottom = pause_side(c, left, right, bottom, &kicker, &name, &colour);
    float name_top = big_name(c, name, 120.0f, left, right, bottom, 0.0f, colour);
    get_kicker(c, kicker, left, name_top - 8.0f, colour, 1.0f);
}

// A front end menu restyled (Kind::Menu): the main menu's look - the highlighted item's art full-bleed behind the
// column, the game's items as numbered rows in big type (a note after a label), the item's line at the foot, its name
// big on the right under the kicker; the main menu's Continue hint (X: the last level played).
void menu_of_items(Ctx& c, const mirror::Tracker& t, float* scroll)
{
    const mirror::View& v = t.get_shown();
    const mirror::Row* sel = v.selected >= 0 ? &v.rows[v.selected] : nullptr;
    std::string art = sel && !sel->art.empty() ? art_by_id(c.extras.catalogue, sel->art) : std::string();
    if (art.empty())
        art = art_by_id(c.extras.catalogue, v.screen_art_);
    backdrop(c.dl, c.f, art_for(c.extras, art), 0.0f);
    float rule = get_header(c, 40.0f, kColumnInner, v.title.c_str(), !v.main_menu_, nullptr);
    int n = static_cast<int>(v.rows.size());
    bool big = n <= 7;
    if (big) {
        RowStyle probe = main_row_style();
        for (const mirror::Row& r : v.rows)
            if (c.f.current_s(probe.text_x_) + get_measure(probe.name, c.f, r.label.c_str()) >
                c.f.current_s(kColumnInner + 10.0f))
                big = false;
    }
    RowStyle rs = big ? main_row_style() : level_row_style();
    float row_h = big ? kMainRowHeight + 2.0f : kLevelRowHeight;
    float list_top = rule + (big ? 36.0f : 20.0f);
    float list_bottom = 620.0f;
    int fit = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    float first = scroll ? *scroll : 0.0f;
    if (v.selected >= 0) {
        if (static_cast<float>(v.selected) < first + 1.0f)
            first = static_cast<float>(v.selected) - 1.0f;
        if (static_cast<float>(v.selected) > first + static_cast<float>(fit) - 2.0f)
            first = static_cast<float>(v.selected) - static_cast<float>(fit) + 2.0f;
    }
    first = std::clamp(first, 0.0f, static_cast<float>(std::max(0, n - fit)));
    if (scroll)
        *scroll = first;
    c.dl->PushClipRect(ImVec2(c.f.get_x(0.0f), c.f.get_y(list_top - 2.0f)),
                       ImVec2(c.f.get_x(kColumn + kScrimFeather), c.f.get_y(list_bottom + 8.0f)), true);
    for (int i = 0; i < n; ++i) {
        float top = list_top + (static_cast<float>(i) - first) * row_h;
        if (top < list_top - row_h || top > list_bottom)
            continue;
        const mirror::Row& r = v.rows[i];
        get_row(c, rs, top, pad2(i + 1).c_str(), r.label.c_str(), t.row_amount(i), Menu::kHouseYellow, !r.focusable);
        if (!r.tag.empty()) {
            Style tag = mono12(kMuted2);
            float base = datum(rs.name, c.f, c.f.get_y(top + rs.row_pad_top_), rs.line_height_);
            float x = c.f.get_x(rs.text_x_) + get_measure(rs.name, c.f, r.label.c_str()) + c.f.current_s(16.0f);
            draw_text(c.dl, tag, c.f, x, base - c.f.current_s(rs.name.css * 0.12f), r.tag.c_str());
        }
        if (r.focusable && top >= list_top - 1.0f && top + row_h <= list_bottom + 1.0f)
            c.get_hit(Hit::Kind::kItem, i, 48.0f, top, kColumnInner + 20.0f, top + row_h);
    }
    c.dl->PopClipRect();
    Style body;
    body.face = Face::kBody;
    body.css = 14.0f;
    body.colour = kMuted;
    std::vector<std::string> lines =
        get_wrap(body, c.f, sel ? sel->blurb : std::string(), c.f.current_s(kColumnInner - 48.0f));
    float line_h = 14.0f * 1.5f;
    float y = 688.0f - line_h * static_cast<float>(lines.size());
    for (const std::string& line : lines) {
        draw_text(c.dl, body, c.f, c.f.get_x(48.0f), datum(body, c.f, c.f.get_y(y), 1.5f), line.c_str());
        y += line_h;
    }

    float left = kRight, right = 1240.0f;
    std::vector<std::string> hints = {text_tools::kHintSelect};
    int resume_hint = -1;
    if (v.main_menu_ && !c.extras.continue_name_.empty()) {
        resume_hint = static_cast<int>(hints.size());
        hints.push_back(std::string(text_tools::kHintResume) + " " + c.extras.continue_name_);
    }
    if (!v.main_menu_)
        hints.emplace_back(text_tools::kHintBack);
    std::vector<std::pair<float, float>> spans;
    float bottom = get_hints(c, hints, left, right, 688.0f, &spans) - 8.0f;
    if (resume_hint >= 0 && resume_hint < static_cast<int>(spans.size()))
        c.get_hit(Hit::Kind::kContinue, 0, spans[resume_hint].first - 6.0f, 688.0f - 12.0f * 1.3f - 6.0f,
                  spans[resume_hint].second + 6.0f, 688.0f + 6.0f);
    float name_top = big_name(c, v.big_name_, 120.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    get_kicker(c, v.kicker, left, name_top - 8.0f, Menu::kHouseYellow, 1.0f);
}

// The on-screen keyboard restyled (Kind::Keyboard): the text typed big under the title, the keys as rows of mono boxes
// (the selected one in the house yellow); a click on a key selects it in the game's keyboard and chooses it (Hit::StageMap,
// row * 64 + key).
void keyboard_screen(Ctx& c, const mirror::Tracker& t, float window_width, float window_height)
{
    const mirror::View& v = t.get_shown();
    if (v.over_game_)
        pause_ground(c, window_width, window_height, 0.7f);
    float rule = get_header(c, 40.0f, 1232.0f, v.title.c_str(), true, nullptr);
    // The text typed (the game's cursor "_" kept as it shows it).
    Style entry;
    entry.face = Face::kDisplay;
    entry.css = 72.0f;
    entry.upper = false;
    entry.colour = kPaper;
    float entry_top = rule + 30.0f;
    // (the game's cursor "_": drawn as our own bar after the text - the display face has no underscore)
    std::string typed = v.entry;
    if (!typed.empty() && typed.back() == '_')
        typed.pop_back();
    Style typed_style = entry;
    typed_style.face = Face::kBody; // (the case as typed: the display face is capitals only)
    typed_style.css = 48.0f;
    float entry_x = c.f.get_x(48.0f);
    draw_text(c.dl, typed_style, c.f, entry_x, datum(typed_style, c.f, c.f.get_y(entry_top + 12.0f), 1.0f),
              typed.c_str());
    float cursor_x = entry_x + get_measure(typed_style, c.f, typed.c_str()) + c.f.current_s(4.0f);
    c.dl->AddRectFilled(ImVec2(cursor_x, c.f.get_y(entry_top + 58.0f)),
                        ImVec2(cursor_x + c.f.current_s(22.0f), c.f.get_y(entry_top + 62.0f)),
                        get_col(Menu::kHouseYellow));
    float line = entry_top + 72.0f + 10.0f;
    c.dl->AddRectFilled(ImVec2(c.f.get_x(48.0f), c.f.get_y(line)),
                        ImVec2(c.f.get_x(1232.0f), c.f.get_y(line) + std::max(1.0f, c.f.current_s(2.0f))),
                        get_col(Menu::kHouseYellow));
    // The keys.
    Style key = mono12(kPaper, false);
    key.css = 20.0f;
    key.upper = false; // (the keys as the game shows them: lower case is lower case)
    float top = line + 36.0f, h = 44.0f, gap = 8.0f;
    for (size_t r = 0; r < v.keys.size(); ++r) {
        float x = 48.0f;
        for (size_t k = 0; k < v.keys[r].size(); ++k) {
            const std::string& label = v.keys[r][k];
            float w = std::max(52.0f, get_measure(key, c.f, label.c_str()) / c.f.scale + 28.0f);
            bool sel = static_cast<int>(r) == v.key_row_ && static_cast<int>(k) == v.key_col_;
            c.dl->AddRectFilled(ImVec2(c.f.get_x(x), c.f.get_y(top)), ImVec2(c.f.get_x(x + w), c.f.get_y(top + h)),
                                sel ? get_col(Menu::kHouseYellow) : get_col(kGround, 0.85f));
            c.dl->AddRect(ImVec2(c.f.get_x(x), c.f.get_y(top)), ImVec2(c.f.get_x(x + w), c.f.get_y(top + h)),
                          get_col(kRule), 0.0f, 0, std::max(1.0f, c.f.current_s(1.0f)));
            Style s = key;
            s.colour = sel ? kGround : kPaper;
            float tx = c.f.get_x(x + w * 0.5f) - get_measure(s, c.f, label.c_str()) * 0.5f;
            draw_text(c.dl, s, c.f, tx, datum(s, c.f, c.f.get_y(top + (h - 20.0f * 1.3f) * 0.5f), 1.3f), label.c_str());
            c.get_hit(Hit::Kind::kStageMap, static_cast<int>(r) * 64 + static_cast<int>(k), x, top, x + w, top + h);
            x += w + gap;
        }
        top += h + gap;
    }
    get_hints(c, {text_tools::kHintSelect, text_tools::kHintBack}, kRight, 1240.0f, 688.0f);
}

} // namespace

// --- the public parts -------------------------------------------------------------------------------------------

float baseline_in_line(float ascent, float descent, float css, float line_height)
{
    float content = (ascent - descent) * css;
    return (css * line_height - content) * 0.5f + ascent * css;
}

float main_row_top(int index)
{
    // (the header's rule at 40 + 12 * 1.3 + 12, 1 px; the nav 36 below; rows 2 apart)
    float rule = 40.0f + 12.0f * 1.3f + 12.0f + 1.0f;
    return rule + 36.0f + static_cast<float>(index) * (kMainRowHeight + 2.0f);
}

float level_list_top(int tab_rows)
{
    float rule = 36.0f + 12.0f * 1.3f + 12.0f + 1.0f;
    float tab_h = 13.0f * 1.3f + 8.0f;
    int rows = std::max(1, tab_rows);
    float last_tab_top = rule + 14.0f + static_cast<float>(rows - 1) * (tab_h + 4.0f);
    return last_tab_top + tab_h + 18.0f;
}

int level_list_rows(int tab_rows)
{
    return std::max(1, static_cast<int>((692.0f - level_list_top(tab_rows)) / kLevelRowHeight));
}

bool read_font_metrics(const uint8_t* data, size_t size, float* ascent, float* descent)
{
    if (!data || size < 12)
        return false;
    uint32_t tag = be32(data);
    if (tag != 0x00010000 && tag != 0x74727565 /* true */ && tag != 0x4F54544F /* OTTO */)
        return false;
    int tables = be16(data + 4);
    const uint8_t* head = nullptr;
    const uint8_t* hhea = nullptr;
    for (int i = 0; i < tables; ++i) {
        size_t entry = 12 + static_cast<size_t>(i) * 16;
        if (entry + 16 > size)
            return false;
        uint32_t name = be32(data + entry);
        uint32_t offset = be32(data + entry + 8);
        if (offset + 54 > size)
            continue;
        if (name == 0x68656164) // head
            head = data + offset;
        else if (name == 0x68686561) // hhea
            hhea = data + offset;
    }
    if (!head || !hhea)
        return false;
    auto units_per_em = static_cast<float>(be16(head + 18));
    if (units_per_em <= 0.0f)
        return false;
    *ascent = static_cast<float>(static_cast<int16_t>(be16(hhea + 4))) / units_per_em;
    *descent = static_cast<float>(static_cast<int16_t>(be16(hhea + 6))) / units_per_em;
    return *ascent > 0.0f;
}

void forget_fonts()
{
    g_atlas = nullptr;
    g_scale = 0.0f;
    for (Baked& b : g_baked)
        b = Baked{};
}

float built_scale() { return ImGui::GetCurrentContext() && ImGui::GetIO().Fonts == g_atlas ? g_scale : 0.0f; }

bool build_fonts(const FontFiles& files, float scale)
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    for (Baked& b : g_baked)
        b = Baked{};
    g_condense = std::clamp(files.condense, 0.5f, 1.5f);
    g_bold = std::clamp(files.bold, 0.0f, 0.1f);
    const std::string* paths[3] = {&files.display, &files.mono, &files.body};
    bool all = true;
    for (int i = 0; i < 3; ++i) {
        FaceLayout& face = g_faces[i];
        face = FaceLayout{};
        if (paths[i]->empty() || !read_file(*paths[i], &face.file) ||
            !read_font_metrics(face.file.data(), face.file.size(), &face.ascent, &face.descent)) {
            get_log("new front end: can't read the font %s", paths[i]->empty() ? "(none)" : paths[i]->c_str());
            face.file.clear();
            all = false;
            continue;
        }
        face.ok = true;
    }
    for (int i = 0; i < kBakeCount; ++i) {
        FaceLayout& face = g_faces[static_cast<int>(kBakes[i].face)];
        if (!face.ok)
            continue;
        float em = std::max(4.0f, std::round(kBakes[i].css * scale));
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        bool big = em > 32.0f;
        config.OversampleH = big ? 1 : 2;
        config.OversampleV = 1;
        config.PixelSnapH = big;
        std::snprintf(config.Name, sizeof(config.Name), "fe %d %.0f", static_cast<int>(kBakes[i].face), kBakes[i].css);
        // (stb_truetype's size is ascent - descent: the em asked for times that)
        float pixel_height = em * (face.ascent - face.descent);
        ImFont* font =
            atlas->AddFontFromMemoryTTF(face.file.data(), static_cast<int>(face.file.size()), pixel_height, &config,
                                        kBakes[i].face == Face::kDisplay ? kDisplayRanges : kTextRanges);
        g_baked[i] = {font, kBakes[i].face, em};
    }
    g_scale = scale;
    g_atlas = atlas;
    atlas->Build();
    get_log("new front end: fonts for scale %.2f - atlas %dx%d", scale, atlas->TexWidth, atlas->TexHeight);
    return all;
}

void get_screen(const Menu& menu, const TickCycle& frame, float window_width, float window_height, const Extras& extras,
                std::vector<Hit>* hits, int* visible_rows)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    // (the pause menus: over the game, dimmed - the rest on the ground's colour)
    bool over_game = is_pause_screen(menu.get_shown()) || (menu.get_shown() == Screen::kOptions && menu.over_pause());
    if (!over_game)
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(window_width, window_height), get_col(kGround));
    Ctx c{dl, frame, menu, extras, hits};
    if (hits)
        hits->clear();
    if (menu.get_shown() == Screen::kLevels)
        levels_screen(c, visible_rows);
    else if (menu.get_shown() == Screen::kOptions)
        options_screen(c, window_width, window_height);
    // (the roots - Main, Pause - are never drawn: ours goes aside at them)
    // (a 16:9 frame in a wider or taller window: the bars the ground's colour, over anything that spilled)
    if (frame.left > 0.5f && !over_game) {
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(frame.left, window_height), get_col(kGround));
        dl->AddRectFilled(ImVec2(frame.get_x(kDesignWidth), 0.0f), ImVec2(window_width, window_height),
                          get_col(kGround));
    }
    if (frame.top > 0.5f && !over_game) {
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(window_width, frame.top), get_col(kGround));
        dl->AddRectFilled(ImVec2(0.0f, frame.get_y(kDesignHeight)), ImVec2(window_width, window_height),
                          get_col(kGround));
    }
    wipe(dl, menu.wipe(), menu.wipe_colour(), window_width, window_height);
    dl->PopTextureID();
}

void restyled(const mirror::Tracker& tracker, const TickCycle& frame, float window_width, float window_height,
              std::vector<Hit>* hits, float* scroll, const Extras* extras)
{
    static const Menu kNoMenu;
    static const Extras kNoExtras;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    Ctx c{dl, frame, kNoMenu, extras ? *extras : kNoExtras, hits};
    if (hits)
        hits->clear();
    switch (tracker.get_shown().kind) {
    case mirror::Kind::kDialog:
        dialog_screen(c, tracker, window_width, window_height);
        break;
    case mirror::Kind::kSkaters:
        skaters_screen(c, tracker, window_width, window_height);
        break;
    case mirror::Kind::kLinkChain:
        list_of_items(c, tracker, window_width, window_height, scroll);
        break;
    case mirror::Kind::kFreeze:
        pause_of_items(c, tracker, window_width, window_height, scroll);
        break;
    case mirror::Kind::kMenu:
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(window_width, window_height), get_col(kGround));
        menu_of_items(c, tracker, scroll);
        break;
    case mirror::Kind::kKeyboard:
        if (!tracker.get_shown().over_game_)
            dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(window_width, window_height), get_col(kGround));
        keyboard_screen(c, tracker, window_width, window_height);
        break;
    case mirror::Kind::kNone:
        break;
    }
    dl->PopTextureID();
}

void get_waiting(const TickCycle& frame, float window_width, float window_height, const Extras& extras,
                 const char* art_id)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(window_width, window_height), get_col(kGround));
    backdrop(dl, frame, art_for(extras, art_by_id(extras.catalogue, art_id)), 0.0f);
    dl->PopTextureID();
}

} // namespace frontend::new_frontend::paint
