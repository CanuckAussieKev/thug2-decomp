// NOT ORIGINAL - the new front end's drawing (ours): see new_frontend_draw.h. The numbers are the design's
// (docs/design/frontend/HMain.dc.html, HLevels.dc.html - their CSS), in design pixels at 1280 x 720.

#include "game/front/new_frontend_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "game/front/new_frontend_mirror.h"
#include "game/front/new_frontend_options.h"
#include "game/front/new_frontend_text.h"
#include "hook/log.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace Front::NewFrontend::Paint {

namespace {

// --- the colours (docs/design/frontend/README.md) ---
constexpr uint32_t kGround = 0xFF0A0A0A;
constexpr uint32_t kPaper = 0xFFF2EFE8;
constexpr uint32_t kMuted = 0xFFB9B5AC;
constexpr uint32_t kMuted2 = 0xFF8E8A82;
constexpr uint32_t kRule = 0xFF3A3A38;
constexpr uint32_t kArtGround = 0xFF1C1C1B;

ImU32 Col(uint32_t argb, float alpha = 1.0f)
{
    uint32_t a = static_cast<uint32_t>(std::clamp(static_cast<float>((argb >> 24) & 0xFF) * alpha, 0.0f, 255.0f));
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, a);
}

uint32_t Mix(uint32_t a, uint32_t b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float x = static_cast<float>((a >> shift) & 0xFF), y = static_cast<float>((b >> shift) & 0xFF);
        out |= static_cast<uint32_t>(x + (y - x) * t + 0.5f) << shift;
    }
    return out;
}

// --- the fonts ---

// The sizes baked (CSS px at 1280 x 720): the display face's 120 (the big names: the level select's 112 drawn from
// it), 78 (the main menu), 40 (the levels), 30 (Skate); the mono face's 12 and 13; the body's 14.
constexpr Bake kBakes[] = {
    {Face::Display, 120.0f}, {Face::Display, kMainType}, {Face::Display, kLevelType}, {Face::Display, 28.0f},
    {Face::Mono, 12.0f},     {Face::Mono, 13.0f},    {Face::Body, 14.0f},
};
constexpr int kBakeCount = static_cast<int>(sizeof(kBakes) / sizeof(kBakes[0]));

struct FaceData {
    std::vector<uint8_t> file; // (kept: the atlas doesn't own it - several sizes share it)
    float ascent = 0.8f, descent = -0.2f;
    bool ok = false;
};
FaceData g_faces[3];

struct Baked {
    ImFont* font = nullptr;
    Face face = Face::Body;
    float emPx = 0.0f; // the em in window px it was baked for
};
Baked g_baked[kBakeCount];
float g_scale = 0.0f;
ImFontAtlas* g_atlas = nullptr; // (the atlas the fonts are in: another context's means none built here)
float g_condense = 1.0f, g_bold = 0.0f;

// The display face's glyphs: upper case (the design sets it in capitals), figures, punctuation, the em dash. The
// mono and body faces: ASCII, the em dash, the middle dot, the apostrophe.
constexpr ImWchar kDisplayRanges[] = {0x0020, 0x0060, 0x00C0, 0x00DE, 0x2014, 0x2014, 0x2019, 0x2019, 0};
constexpr ImWchar kTextRanges[] = {0x0020, 0x007E, 0x00B7, 0x00B7, 0x00C0, 0x00FF, 0x2014, 0x2014, 0x2019, 0x2019, 0};

bool ReadFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
        return false;
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    bool ok = size > 12 && size < 64 * 1024 * 1024;
    if (ok) {
        out->resize(static_cast<size_t>(size));
        ok = std::fread(out->data(), 1, out->size(), file) == out->size();
    }
    std::fclose(file);
    return ok;
}

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

const Baked* FontFor(Face face, float css)
{
    const Baked* best = nullptr;
    for (const Baked& b : g_baked) {
        if (!b.font || b.face != face)
            continue;
        // (the smallest bake at least as large, else the largest)
        float bakedCss = b.emPx / std::max(g_scale, 0.01f);
        if (!best) {
            best = &b;
            continue;
        }
        float bestCss = best->emPx / std::max(g_scale, 0.01f);
        bool fits = bakedCss >= css - 0.5f, bestFits = bestCss >= css - 0.5f;
        if ((fits && (!bestFits || bakedCss < bestCss)) || (!fits && !bestFits && bakedCss > bestCss))
            best = &b;
    }
    if (!best && face != Face::Body)
        return FontFor(Face::Body, css); // (a face that couldn't load: the body's)
    if (!best && face != Face::Mono)
        return FontFor(Face::Mono, css);
    return best;
}

const FaceData& DataOf(Face face) { return g_faces[static_cast<int>(face)]; }

// --- text ---

struct Style {
    Face face = Face::Body;
    float css = 14.0f;      // its CSS size (design px)
    float tracking = 0.0f;  // letter-spacing (design px)
    bool upper = false;
    uint32_t colour = kPaper;
    float alpha = 1.0f;
};

std::string Upper(const char* text)
{
    std::string s = text;
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return s;
}

// The scale from the baked font to the size asked, and the display face's width.
struct Metrics {
    const Baked* baked;
    float k;      // window px per baked px
    float xScale; // (the display face condensed)
    float boldPx;
};
Metrics MetricsOf(const Style& style, const Frame& frame)
{
    Metrics m = {FontFor(style.face, style.css), 1.0f, 1.0f, 0.0f};
    if (m.baked)
        m.k = frame.S(style.css) / m.baked->emPx;
    if (style.face == Face::Display && m.baked && m.baked->face == Face::Display) {
        m.xScale = g_condense;
        m.boldPx = g_bold * frame.S(style.css);
    }
    return m;
}

// The text's width in window px.
float Measure(const Style& style, const Frame& frame, const char* text)
{
    Metrics m = MetricsOf(style, frame);
    if (!m.baked)
        return 0.0f;
    std::string s = style.upper ? Upper(text) : text;
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
            width += g->AdvanceX * m.k * m.xScale;
        ++count;
    }
    width += frame.S(style.tracking) * static_cast<float>(count) + m.boldPx;
    return width;
}

// The text with its baseline at window y `baseline`, starting at window x `x`.
void Text(ImDrawList* dl, const Style& style, const Frame& frame, float x, float baseline, const char* text)
{
    Metrics m = MetricsOf(style, frame);
    if (!m.baked)
        return;
    ImFont* font = m.baked->font;
    std::string s = style.upper ? Upper(text) : text;
    const char* end = s.c_str() + s.size();
    ImU32 colour = Col(style.colour, style.alpha);
    float top = baseline - font->Ascent * m.k;
    for (int pass = 0; pass < (m.boldPx > 0.0f ? 2 : 1); ++pass) {
        float pen = x + (pass ? m.boldPx : 0.0f);
        for (const char* p = s.c_str(); p < end;) {
            unsigned int c = 0;
            p += ImTextCharFromUtf8(&c, p, end);
            if (!c)
                break;
            const ImFontGlyph* g = font->FindGlyph(static_cast<ImWchar>(c));
            if (!g)
                continue;
            if (g->Visible) {
                ImVec2 a(pen + g->X0 * m.k * m.xScale, top + g->Y0 * m.k);
                ImVec2 b(pen + g->X1 * m.k * m.xScale, top + g->Y1 * m.k);
                dl->PrimReserve(6, 4);
                dl->PrimRectUV(a, b, ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V1), colour);
            }
            pen += g->AdvanceX * m.k * m.xScale + frame.S(style.tracking);
        }
    }
}

// The baseline of a line box at window y `top` for the style at `lineHeight` (a multiple of its size).
float Baseline(const Style& style, const Frame& frame, float top, float lineHeight)
{
    const Baked* baked = FontFor(style.face, style.css);
    const FaceData& data = DataOf(baked ? baked->face : style.face);
    return top + frame.S(BaselineInLine(data.ascent, data.descent, style.css, lineHeight));
}

// Words of `text` in lines no wider than `width` (window px).
std::vector<std::string> Wrap(const Style& style, const Frame& frame, const std::string& text, float width)
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
        std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && Measure(style, frame, tried.c_str()) > width) {
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
void Cut(ImDrawList* dl, float x0, float y0, float x1, float y1, float topInset, float bottomInset, ImU32 colour)
{
    ImVec2 points[4] = {{x0 + topInset, y0}, {x1, y0}, {x1 - bottomInset, y1}, {x0, y1}};
    dl->AddConvexPolyFilled(points, 4, colour);
}

// The vertices added since `start` turned by `radians` about (px, py) and moved by (dx, dy) - the row's
// translateX(24px) rotate(-3deg) about its left centre.
void Transform(ImDrawList* dl, int start, float px, float py, float radians, float dx, float dy)
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
void Arrow(ImDrawList* dl, float x, float baseline, float size, bool left, ImU32 colour, float* advance)
{
    float w = size * 0.9f, y = baseline - size * 0.32f, t = std::max(1.0f, size * 0.09f);
    float x0 = x, x1 = x + w;
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), colour, t);
    float hx = left ? x0 : x1, dir = left ? 1.0f : -1.0f, h = size * 0.3f;
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + dir * h, y - h), colour, t);
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + dir * h, y + h), colour, t);
    *advance = w + size * 0.45f;
}

void ArtBox(ImDrawList* dl, const Art& art, float x0, float y0, float x1, float y1, float pan, float alpha)
{
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), Col(kArtGround, alpha));
    if (!art.texture)
        return;
    Uv uv = CoverUv(art.width, art.height, x1 - x0, y1 - y0, pan, 0.03f);
    if (art.bottomUp) {
        uv.v0 = 1.0f - uv.v0;
        uv.v1 = 1.0f - uv.v1;
    }
    dl->AddImage(static_cast<ImTextureID>(art.texture), ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(uv.u0, uv.v0),
                 ImVec2(uv.u1, uv.v1), Col(0xFFFFFFFF, alpha));
}

// The bottom of the art darkened so the words over it read (not in the mock-ups, which have flat grey there).
void Shade(ImDrawList* dl, float x0, float y0, float x1, float y1)
{
    float mid = y0 + (y1 - y0) * 0.35f;
    dl->AddRectFilledMultiColor(ImVec2(x0, mid), ImVec2(x1, y1), Col(kGround, 0.0f), Col(kGround, 0.0f),
                                Col(kGround, 0.88f), Col(kGround, 0.88f));
}

// The list column's scrim (the user's change to the mock-ups, 2026-10-25: the art behind the whole screen, the column
// on a dark, softened copy of it rather than a flat panel): the art's column part drawn again a few times a little
// apart (averaged: a soft blur), darkened ~75 %, its right edge feathered over kScrimFeather. `art` may be empty (the
// pause menus: the game behind - only darkened).
void Scrim(ImDrawList* dl, const Frame& f, const Art& art, float pan, float alpha)
{
    if (alpha <= 0.001f)
        return;
    float x1 = kColumn + kScrimFeather;
    if (art.texture) {
        Uv uv = CoverUv(art.width, art.height, 1280.0f, 720.0f, pan, 0.03f);
        if (art.bottomUp) {
            uv.v0 = 1.0f - uv.v0;
            uv.v1 = 1.0f - uv.v1;
        }
        // (the column's share of the picture)
        float share = x1 / 1280.0f;
        float u1 = uv.u0 + (uv.u1 - uv.u0) * share;
        float uEdge = uv.u0 + (uv.u1 - uv.u0) * (kColumn / 1280.0f);
        static const float kOffsets[][2] = {{0, 0}, {9, 0}, {-9, 0}, {0, 9}, {0, -9}, {6, 6}, {-6, -6}, {6, -6}, {-6, 6}};
        dl->PushTextureID(static_cast<ImTextureID>(art.texture));
        dl->PushClipRect(ImVec2(f.X(0.0f), f.Y(0.0f)), ImVec2(f.X(x1), f.Y(720.0f)), true);
        for (int i = 0; i < 9; ++i) {
            float dx = f.S(kOffsets[i][0]), dy = f.S(kOffsets[i][1]);
            // (copy i at 1 / (i + 1): the result is the copies' average; across the feather it fades to the art)
            ImU32 full = Col(0xFFFFFFFF, alpha / static_cast<float>(i + 1)), none = Col(0xFFFFFFFF, 0.0f);
            float ya = f.Y(0.0f) + dy, yb = f.Y(720.0f) + dy;
            float xa = f.X(0.0f) + dx, xm = f.X(kColumn) + dx, xb = f.X(x1) + dx;
            dl->PrimReserve(12, 8);
            ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            dl->PrimWriteVtx(ImVec2(xa, ya), ImVec2(uv.u0, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(uEdge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(uEdge, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xa, yb), ImVec2(uv.u0, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(uEdge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xb, ya), ImVec2(u1, uv.v0), none);
            dl->PrimWriteVtx(ImVec2(xb, yb), ImVec2(u1, uv.v1), none);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(uEdge, uv.v1), full);
            for (int q = 0; q < 2; ++q) {
                ImDrawIdx b = static_cast<ImDrawIdx>(base + q * 4);
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
    dl->AddRectFilled(ImVec2(f.X(0.0f), f.Y(0.0f)), ImVec2(f.X(kColumn), f.Y(720.0f)), Col(kGround, dark));
    dl->AddRectFilledMultiColor(ImVec2(f.X(kColumn), f.Y(0.0f)), ImVec2(f.X(x1), f.Y(720.0f)), Col(kGround, dark),
                                Col(kGround, 0.0f), Col(kGround, 0.0f), Col(kGround, dark));
}

// The whole screen's art (full-bleed), the words' shade at the foot, the column's scrim.
void Backdrop(ImDrawList* dl, const Frame& f, const Art& art, float pan)
{
    ArtBox(dl, art, f.X(0.0f), f.Y(0.0f), f.X(1280.0f), f.Y(720.0f), pan, 1.0f);
    Shade(dl, f.X(kRight - 32.0f), f.Y(0.0f), f.X(1280.0f), f.Y(720.0f));
    Scrim(dl, f, art, pan, 1.0f);
}

std::string Pad2(int n)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%02d", n);
    return text;
}

// --- one screen's parts ---

struct Ctx {
    ImDrawList* dl;
    const Frame& f;
    const Menu& menu;
    const Extras& extras;
    std::vector<Hit>* hits;
    void Hit(Hit::Kind kind, int index, float x0, float y0, float x1, float y1)
    {
        if (hits)
            hits->push_back({kind, index, x0, y0, x1, y1});
    }
};

Style Mono12(uint32_t colour, bool tracked = true)
{
    Style s;
    s.face = Face::Mono;
    s.css = 12.0f;
    s.tracking = tracked ? 1.0f : 0.0f;
    s.upper = true;
    s.colour = colour;
    return s;
}

// A header row at design y `top` (left 48, right `right`): the texts, the rule under it. Returns the rule's y.
float Header(Ctx& c, float top, float right, const char* leftText, bool back, const char* rightText)
{
    Style s = Mono12(kMuted);
    float lineH = 1.3f;
    float base = Baseline(s, c.f, c.f.Y(top), lineH);
    float x = c.f.X(48.0f);
    if (back) {
        Style b = Mono12(kMuted, false);
        float adv = 0.0f;
        Arrow(c.dl, x, base, c.f.S(12.0f), true, Col(kMuted), &adv);
        Text(c.dl, b, c.f, x + adv, base, Text::kBack);
        float w = adv + Measure(b, c.f, Text::kBack);
        c.Hit(Hit::Back, 0, 48.0f - 6.0f, top - 6.0f, 48.0f + w / c.f.scale + 6.0f, top + 12.0f * lineH + 6.0f);
        x += w + c.f.S(16.0f);
        s.colour = kPaper;
    }
    Text(c.dl, s, c.f, x, base, leftText);
    if (rightText && *rightText) {
        Style r = Mono12(kMuted);
        Text(c.dl, r, c.f, c.f.X(right) - Measure(r, c.f, rightText), base, rightText);
    }
    float rule = top + 12.0f * lineH + 12.0f;
    c.dl->AddRectFilled(ImVec2(c.f.X(48.0f), c.f.Y(rule)), ImVec2(c.f.X(right), c.f.Y(rule) + std::max(1.0f, c.f.S(1.0f))),
                        Col(kRule));
    return rule + 1.0f;
}

// A list row with the kinetic selection: number, name, the cut behind the name when selected - all turned and moved
// by the selection's amount `e`. Design numbers: the row's top `top`, its line box `lineH`, the name's left `textX`,
// the number's style, the cut's extents and insets.
struct RowStyle {
    Style name;
    Style number;
    float lineHeight;   // (a multiple of the name's size)
    float rowPadTop;    // (above the line box)
    float rowHeight;
    float textX;        // the name's left
    float cutLeft;      // the cut's left (the label span's left - 6)
    float cutRightPad;  // past the name's right (span padding + 22 / 26)
    float cutTop, cutBottom; // (fractions of the line box)
    float topInset, bottomInset;
};

void Row(Ctx& c, const RowStyle& rs, float top, const char* number, const char* label, float e, uint32_t cutColour,
         bool dim)
{
    ImDrawList* dl = c.dl;
    int start = dl->VtxBuffer.Size;
    float lineTop = top + rs.rowPadTop;
    float lineH = rs.name.css * rs.lineHeight;
    float base = Baseline(rs.name, c.f, c.f.Y(lineTop), rs.lineHeight);
    float width = Measure(rs.name, c.f, label);
    if (e > 0.001f) {
        float x0 = c.f.X(rs.cutLeft);
        float x1 = c.f.X(rs.textX) + width + c.f.S(rs.cutRightPad);
        x1 = x0 + (x1 - x0) * e; // (the cut snaps out from the left)
        Cut(dl, x0, c.f.Y(lineTop + lineH * rs.cutTop), x1, c.f.Y(lineTop + lineH * rs.cutBottom), c.f.S(rs.topInset),
            c.f.S(rs.bottomInset), Col(cutColour));
    }
    Style name = rs.name;
    name.colour = Mix(dim ? kMuted2 : kPaper, kGround, e);
    Text(dl, name, c.f, c.f.X(rs.textX), base, label);
    Style num = rs.number;
    Text(dl, num, c.f, c.f.X(48.0f), base, number);
    if (e > 0.001f) {
        float pivotY = c.f.Y(top + rs.rowHeight * 0.5f);
        Transform(dl, start, c.f.X(48.0f), pivotY, -3.0f * 3.14159265f / 180.0f * e, c.f.S(24.0f) * e, 0.0f);
    }
}

RowStyle MainRowStyle()
{
    RowStyle rs;
    rs.name.face = Face::Display;
    rs.name.css = kMainType;
    rs.name.upper = true;
    rs.number = Mono12(kMuted, false);
    rs.number.css = 13.0f;
    rs.lineHeight = 0.92f;
    rs.rowPadTop = 2.0f;
    rs.rowHeight = kMainRowHeight;
    rs.textX = 48.0f + 26.0f + 18.0f + 12.0f;
    rs.cutLeft = 48.0f + 26.0f + 18.0f - 6.0f;
    rs.cutRightPad = 12.0f + 26.0f;
    rs.cutTop = 0.18f;
    rs.cutBottom = 0.92f;
    rs.topInset = 10.0f;
    rs.bottomInset = 18.0f;
    return rs;
}

RowStyle LevelRowStyle()
{
    RowStyle rs;
    rs.name.face = Face::Display;
    rs.name.css = kLevelType;
    rs.name.upper = true;
    rs.number = Mono12(kMuted2, false);
    rs.lineHeight = 1.05f;
    rs.rowPadTop = 0.0f;
    rs.rowHeight = kLevelRowHeight;
    rs.textX = 48.0f + 26.0f + 14.0f + 10.0f;
    rs.cutLeft = 48.0f + 26.0f + 14.0f - 6.0f;
    rs.cutRightPad = 10.0f + 22.0f;
    rs.cutTop = 0.16f;
    rs.cutBottom = 0.94f;
    rs.topInset = 8.0f;
    rs.bottomInset = 14.0f;
    return rs;
}

// The big name at the right's foot: lines of `name` (Anton `css`, line-height 0.85) ending at design y `bottom`, from
// x `left` to `right`. Returns the top of the block. With `slice` > 0 (the launch), the top half moves up and the
// bottom half down along a cut, fading.
float BigName(Ctx& c, const std::string& name, float css, float left, float right, float bottom, float slice,
              uint32_t sliceColour)
{
    Style s;
    s.face = Face::Display;
    s.css = css;
    s.upper = true;
    std::vector<std::string> lines = Wrap(s, c.f, name, c.f.S(right - left));
    if (lines.size() > 3)
        lines.resize(3);
    // (a word wider than the space - CREATE-A-SKATER - makes the type smaller, to two thirds at most)
    float widest = 0.0f;
    for (const std::string& line : lines)
        widest = std::max(widest, Measure(s, c.f, line.c_str()));
    if (widest > c.f.S(right - left)) {
        css = std::max(css * 0.66f, css * c.f.S(right - left) / widest);
        s.css = css;
    }
    float lineH = css * 0.85f;
    float top = bottom - lineH * static_cast<float>(lines.size());
    float mid = c.f.Y(top + (bottom - top) * 0.55f);
    for (int half = 0; half < (slice > 0.0f ? 2 : 1); ++half) {
        float dy = slice > 0.0f ? (half == 0 ? -1.0f : 1.0f) * c.f.S(70.0f) * slice : 0.0f;
        s.alpha = 1.0f - slice;
        if (slice > 0.0f) {
            // (each half keeps its own part of the name: the cut moves with it, the gap between opens)
            if (half == 0)
                c.dl->PushClipRect(ImVec2(0, 0), ImVec2(1e6f, mid + dy), true);
            else
                c.dl->PushClipRect(ImVec2(0, mid + dy), ImVec2(1e6f, 1e6f), true);
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            float lineTop = top + lineH * static_cast<float>(i);
            float base = Baseline(s, c.f, c.f.Y(lineTop), 0.85f);
            Text(c.dl, s, c.f, c.f.X(left), base + dy, lines[i].c_str());
        }
        if (slice > 0.0f)
            c.dl->PopClipRect();
    }
    if (slice > 0.0f) {
        float w = c.f.S(right - left) * std::min(1.0f, slice * 2.0f);
        c.dl->AddRectFilled(ImVec2(c.f.X(left), mid - c.f.S(2.0f)), ImVec2(c.f.X(left) + w, mid + c.f.S(2.0f)),
                            Col(sliceColour, 1.0f - slice));
    }
    return top;
}

// A kicker line (mono 12, tracked, upper case) whose box ends at design y `bottom`. Returns its top.
float Kicker(Ctx& c, const std::string& text, float left, float bottom, uint32_t colour, float alpha = 1.0f)
{
    Style s = Mono12(colour);
    s.alpha = alpha;
    float top = bottom - 12.0f * 1.3f;
    Text(c.dl, s, c.f, c.f.X(left), Baseline(s, c.f, c.f.Y(top), 1.3f), text.c_str());
    return top;
}

// The hints row (a rule over it), from `left` to `right`, its text's box ending at `bottom`: spread as CSS's
// justify-content: space-between. Returns the rule's y.
float Hints(Ctx& c, const std::vector<std::string>& hints, float left, float right, float bottom,
            std::vector<std::pair<float, float>>* spans = nullptr)
{
    Style s = Mono12(kMuted, false);
    float top = bottom - 12.0f * 1.3f;
    float base = Baseline(s, c.f, c.f.Y(top), 1.3f);
    float total = 0.0f;
    std::vector<float> widths;
    for (const std::string& h : hints) {
        widths.push_back(Measure(s, c.f, h.c_str()));
        total += widths.back();
    }
    float gap = hints.size() > 1 ? (c.f.S(right - left) - total) / static_cast<float>(hints.size() - 1) : 0.0f;
    float x = c.f.X(left);
    for (size_t i = 0; i < hints.size(); ++i) {
        Text(c.dl, s, c.f, x, base, hints[i].c_str());
        if (spans) // (design x: each hint's left and right)
            spans->emplace_back(c.f.DesignX(x), c.f.DesignX(x + widths[i]));
        x += widths[i] + gap;
    }
    float rule = top - 10.0f - 1.0f;
    c.dl->AddRectFilled(ImVec2(c.f.X(left), c.f.Y(rule)), ImVec2(c.f.X(right), c.f.Y(rule) + std::max(1.0f, c.f.S(1.0f))),
                        Col(kRule));
    return rule;
}

Art ArtFor(const Extras& extras, const std::string& image)
{
    return extras.art && !image.empty() ? extras.art(image) : Art{};
}

// --- the screens ---

// The art for an item: its own, levels.json's menu_art for its id, else THUG2's loading screen for it.
std::string ArtById(const Catalogue* data, const std::string& id)
{
    std::string image = data ? data->MenuArt(id) : std::string();
    return image.empty() ? DefaultArt(id) : image;
}

// The pause screens' ground: the game dimmed, the left column nearly opaque.
void PauseGround(Ctx& c, float windowWidth, float windowHeight)
{
    c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(windowWidth, windowHeight), Col(kGround, 0.55f));
    Scrim(c.dl, c.f, Art{}, 0.0f, 1.0f);
}

// A mono line of records spread in three columns from `left` to `right`, its box ending at `bottom` (a rule above).
// Returns the rule's y (or `bottom` when there is nothing).
float RecordsRow(Ctx& c, const std::vector<std::string>& cols, float left, float right, float bottom, float alpha)
{
    if (cols.empty())
        return bottom;
    Style s = Mono12(kMuted, false);
    s.alpha = alpha;
    float top = bottom - 12.0f * 1.3f;
    float colW = (right - left) / 3.0f;
    for (size_t i = 0; i < cols.size() && i < 3; ++i)
        Text(c.dl, s, c.f, c.f.X(left + colW * static_cast<float>(i)), Baseline(s, c.f, c.f.Y(top), 1.3f), cols[i].c_str());
    float ruleY = top - 10.0f - 1.0f;
    c.dl->AddRectFilled(ImVec2(c.f.X(left), c.f.Y(ruleY)), ImVec2(c.f.X(right), c.f.Y(ruleY) + std::max(1.0f, c.f.S(1.0f))),
                        Col(kRule, alpha));
    return ruleY;
}

std::string Clock(float seconds)
{
    int total = static_cast<int>(seconds);
    char text[32];
    if (total >= 3600)
        std::snprintf(text, sizeof(text), "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    else
        std::snprintf(text, sizeof(text), "%02d:%02d", total / 60, total % 60);
    return text;
}

std::string Thousands(int n)
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
float PauseSide(Ctx& c, float left, float right, float bottom, std::string* kicker, std::string* name, uint32_t* colour)
{
    std::vector<std::string> cols;
    const Session& s = c.extras.session;
    if (s.bestCombo >= 0)
        cols.push_back(std::string(Text::kBestCombo) + " " + Thousands(s.bestCombo));
    if (s.gaps >= 0)
        cols.push_back(std::string(Text::kGaps) + " " + std::to_string(s.gaps) + (s.gapsTotal > 0 ? "/" + std::to_string(s.gapsTotal) : ""));
    if (s.seconds >= 0.0f)
        cols.push_back(std::string(Text::kTime) + " " + Clock(s.seconds));
    float top = RecordsRow(c, cols, left, right, bottom, 1.0f) - 10.0f;
    *kicker = std::string(Text::kTitlePaused) +
              (c.extras.levelKicker.empty() ? std::string() : " \xE2\x80\x94 " + c.extras.levelKicker);
    *name = c.extras.levelName.empty() ? std::string(Text::kTitlePaused) : c.extras.levelName;
    if (c.extras.levelColour)
        *colour = c.extras.levelColour;
    return top;
}

// Mono tabs (the level select's game tabs, the options' groups) from x 48 to 520 at design y `y`, wrapping: the
// selected one in its colour, underlined. Returns the bottom of the last row.
float Tabs(Ctx& c, const std::vector<std::pair<std::string, uint32_t>>& tabs, int selected, float y)
{
    Style tab = Mono12(kMuted, false);
    tab.css = 13.0f;
    float x = 48.0f;
    float tabH = 13.0f * 1.3f + 8.0f;
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        float w = Measure(tab, c.f, tabs[i].first.c_str()) / c.f.scale;
        if (x > 48.0f && x + w > kColumnInner) {
            x = 48.0f;
            y += tabH + 4.0f;
        }
        bool on = i == selected;
        tab.colour = on ? tabs[i].second : kMuted;
        float base = Baseline(tab, c.f, c.f.Y(y + 4.0f), 1.3f);
        Text(c.dl, tab, c.f, c.f.X(x), base, tabs[i].first.c_str());
        if (on)
            c.dl->AddRectFilled(ImVec2(c.f.X(x), base + c.f.S(5.0f)), ImVec2(c.f.X(x + w), base + c.f.S(6.0f)),
                                Col(tabs[i].second));
        c.Hit(Hit::Tab, i, x - 4.0f, y, x + w + 4.0f, y + tabH);
        x += w + 16.0f;
    }
    return y + tabH;
}

// A slanted button (the level select's Skate) with its text in Anton 30 ending at design y `bottom`.
void Button(Ctx& c, float left, float bottom, const char* text, uint32_t colour, bool arrow, Hit::Kind kind, float alpha)
{
    Style s;
    s.face = Face::Display;
    s.css = 30.0f;
    s.upper = true;
    s.colour = kGround;
    s.alpha = alpha;
    float tw = Measure(s, c.f, text) / c.f.scale;
    float arrowW = arrow ? 30.0f * 0.9f + 10.0f : 0.0f;
    float bh = 30.0f * 1.15f + 12.0f;
    float bx0 = left, bx1 = left + 22.0f + tw + arrowW + 22.0f, by0 = bottom - bh, by1 = bottom;
    Cut(c.dl, c.f.X(bx0), c.f.Y(by0), c.f.X(bx1), c.f.Y(by1), c.f.S(10.0f), c.f.S(10.0f), Col(colour, alpha));
    float sb = Baseline(s, c.f, c.f.Y(by0 + 6.0f), 1.15f);
    Text(c.dl, s, c.f, c.f.X(bx0 + 22.0f), sb, text);
    if (arrow) {
        float adv = 0.0f;
        Arrow(c.dl, c.f.X(bx0 + 22.0f + tw + 10.0f), sb, c.f.S(30.0f), false, Col(kGround, alpha), &adv);
    }
    c.Hit(kind, 0, bx0, by0, bx1, by1);
}

// The options: the groups as tabs, the group's settings down the left (Anton 40, each one's value in mono at the
// column's right), the selected one's name on the right with its choices as a segmented control, what it does, its
// ini key and when a change is seen.
void OptionsScreen(Ctx& c, float windowWidth, float windowHeight)
{
    const Menu& menu = c.menu;
    const std::vector<Options::Spec>& specs = Options::Specs();
    const std::vector<Options::Group>& groups = Options::Groups();
    if (menu.OverPause()) {
        PauseGround(c, windowWidth, windowHeight);
    } else {
        Backdrop(c.dl, c.f, ArtFor(c.extras, ArtById(menu.Data(), "options")), menu.Pan());
    }
    float rule = Header(c, 36.0f, kColumnInner, Text::kTitleOptions, true, nullptr);
    std::vector<std::pair<std::string, uint32_t>> tabs;
    for (const Options::Group& g : groups)
        tabs.emplace_back(g.name, Menu::kHouseYellow);
    float listTop = Tabs(c, tabs, menu.OptionGroup(), rule + 14.0f) + 18.0f;
    const std::vector<int>& rows = menu.OptionRows();
    // (the options' rows: a size smaller than the level list's - their labels are long)
    RowStyle rs = LevelRowStyle();
    rs.name.css = 28.0f;
    rs.rowHeight = 28.0f * 1.05f + 6.0f;
    float rowH = rs.rowHeight;
    Style value = Mono12(kMuted, false);
    int count = static_cast<int>(rows.size()) + 1;
    for (int i = 0; i < count; ++i) {
        float top = listTop + static_cast<float>(i) * rowH;
        bool action = i == static_cast<int>(rows.size());
        const char* label = action ? Text::kGameOptions : specs[rows[i]].label;
        // (a short label for the row: the design's 40 px type in a 470 px column)
        Row(c, rs, top, Pad2(i + 1).c_str(), label, menu.RowAmount(i), Menu::kHouseYellow, false);
        std::string shown = action ? std::string(Text::kGameOptionsValue) : specs[rows[i]].choices[menu.OptionChoice(rows[i])].label;
        float base = Baseline(rs.name, c.f, c.f.Y(top), rs.lineHeight);
        value.colour = i == menu.OptionRow() ? Menu::kHouseYellow : kMuted;
        // (the value at the column's right, where the label leaves room for it)
        float valueX = c.f.X(kColumnInner) - Measure(value, c.f, shown.c_str());
        if (c.f.X(rs.textX) + Measure(rs.name, c.f, label) + c.f.S(40.0f) < valueX)
            Text(c.dl, value, c.f, valueX, base, shown.c_str());
        c.Hit(Hit::Item, i, 48.0f, top, kColumnInner + 20.0f, top + rowH);
    }

    // The right's foot.
    float left = kRight, right = 1240.0f;
    bool action = menu.OptionRow() >= static_cast<int>(rows.size());
    std::vector<std::string> hints = action ? std::vector<std::string>{Text::kHintOpen, Text::kHintGroup, Text::kHintBack}
                                            : std::vector<std::string>{Text::kHintChange, Text::kHintGroup, Text::kHintBack};
    float bottom = Hints(c, hints, left, right, 692.0f) - 14.0f;
    Style body;
    body.face = Face::Body;
    body.css = 14.0f;
    body.colour = kMuted;
    std::string name, kicker = std::string(groups[menu.OptionGroup()].name) + " \xE2\x80\x94 " + Pad2(menu.OptionRow() + 1);
    if (action) {
        Button(c, left, bottom, Text::kOpen, Menu::kHouseYellow, true, Hit::Skate, 1.0f);
        bottom -= 30.0f * 1.15f + 12.0f + 14.0f;
        Text(c.dl, body, c.f, c.f.X(left), Baseline(body, c.f, c.f.Y(bottom - 21.0f), 1.5f), Text::kGameOptionsHelp);
        bottom -= 21.0f + 10.0f;
        name = Text::kGameOptions;
    } else {
        int option = rows[menu.OptionRow()];
        const Options::Spec& spec = specs[option];
        // The choices: a segmented control (mono 13; the chosen one filled).
        Style seg = Mono12(kPaper, false);
        seg.css = 13.0f;
        float h = 13.0f * 1.3f + 16.0f;
        float x = left, y0 = bottom - h;
        int chosen = menu.OptionChoice(option);
        for (size_t i = 0; i < spec.choices.size(); ++i) {
            float w = Measure(seg, c.f, spec.choices[i].label) / c.f.scale + 28.0f;
            bool on = static_cast<int>(i) == chosen;
            if (on)
                Cut(c.dl, c.f.X(x), c.f.Y(y0), c.f.X(x + w), c.f.Y(bottom), c.f.S(6.0f), c.f.S(6.0f), Col(Menu::kHouseYellow));
            else
                c.dl->AddRect(ImVec2(c.f.X(x), c.f.Y(y0)), ImVec2(c.f.X(x + w), c.f.Y(bottom)), Col(kRule), 0.0f, 0,
                              std::max(1.0f, c.f.S(1.0f)));
            seg.colour = on ? kGround : kPaper;
            Text(c.dl, seg, c.f, c.f.X(x + 14.0f), Baseline(seg, c.f, c.f.Y(y0 + 8.0f), 1.3f), spec.choices[i].label);
            c.Hit(Hit::Choice, menu.OptionRow() * 16 + static_cast<int>(i), x, y0, x + w, bottom);
            x += w + 6.0f;
        }
        bottom = y0 - 14.0f;
        // When it's seen ("Restart to apply" - yellow once changed; nothing for a live one), its ini key only behind
        // [frontend] show_ini_keys=1, what it does (when it has a line).
        bool pending = option < static_cast<int>(c.extras.restartPending.size()) && c.extras.restartPending[option];
        std::string when = spec.apply == Options::Apply::Live ? std::string() : std::string(Text::kRestartToApply);
        std::string where = c.extras.showIniKeys ? std::string("[") + spec.section + "] " + spec.key : std::string();
        if (!when.empty() || !where.empty()) {
            Style key = Mono12(kMuted2, false);
            if (!where.empty())
                Text(c.dl, key, c.f, c.f.X(left), Baseline(key, c.f, c.f.Y(bottom - 15.6f), 1.3f), where.c_str());
            Style note = Mono12(pending ? Menu::kHouseYellow : kMuted2, false);
            if (!when.empty())
                Text(c.dl, note, c.f, c.f.X(right) - Measure(note, c.f, when.c_str()),
                     Baseline(note, c.f, c.f.Y(bottom - 15.6f), 1.3f), when.c_str());
            bottom -= 15.6f + 8.0f;
        }
        if (spec.help && *spec.help) {
            Text(c.dl, body, c.f, c.f.X(left), Baseline(body, c.f, c.f.Y(bottom - 21.0f), 1.5f), spec.help);
            bottom -= 21.0f + 10.0f;
        }
        name = spec.label;
    }
    float nameTop = BigName(c, name, 96.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    Kicker(c, kicker, left, nameTop - 8.0f, Menu::kHouseYellow);
    if (!c.extras.optionsNote.empty()) {
        Style note = Mono12(kMuted2, false);
        Text(c.dl, note, c.f, c.f.X(left), Baseline(note, c.f, c.f.Y(36.0f), 1.3f), c.extras.optionsNote.c_str());
    }
}

// The level select.
void LevelsScreen(Ctx& c, int* visibleRows)
{
    const Menu& menu = c.menu;
    const Catalogue* data = menu.Data();
    const SectionView* view = menu.View();
    int sectionIndex = menu.SectionIndex();
    const Section* section =
        data && sectionIndex >= 0 && sectionIndex < static_cast<int>(data->sections.size()) ? &data->sections[sectionIndex]
                                                                                            : nullptr;
    uint32_t colour = section ? section->colour : Menu::kHouseYellow;
    const Level* level = menu.SelectedLevel();
    float launch = menu.Launch();
    float grow = launch >= 0.0f ? EaseOut(launch * 1.4f) : 0.0f;

    // The level's loading screen behind the whole screen, the column's scrim over it (a launch: the scrim and the
    // list go under the sharp picture), the thin edge in the game's colour.
    Art art = ArtFor(c.extras, level ? level->art : std::string());
    Backdrop(c.dl, c.f, art, menu.Pan());
    c.dl->AddRectFilled(ImVec2(c.f.X(kColumn), c.f.Y(0.0f)), ImVec2(c.f.X(kColumn + 4.0f), c.f.Y(720.0f)),
                        Col(colour, 1.0f - grow));

    if (grow < 0.999f) {
        c.dl->PushClipRect(ImVec2(c.f.X(0.0f), c.f.Y(0.0f)), ImVec2(c.f.X(kColumn + kScrimFeather), c.f.Y(720.0f)), true);
        const char* title = menu.LevelsPurpose() == LevelsFor::Edit    ? Text::kTitleEditLevel
                            : menu.LevelsPurpose() == LevelsFor::Pause ? Text::kTitleChangeLevel
                                                                       : Text::kTitleFreeSkate;
        float rule = Header(c, 36.0f, kColumnInner, title, true, nullptr);
        // The game tabs (mono 13, wrapping; the selected one in its colour, underlined).
        std::vector<std::pair<std::string, uint32_t>> tabs;
        for (int i = 0; data && i < static_cast<int>(data->sections.size()); ++i)
            tabs.emplace_back(data->sections[i].tab, data->sections[i].colour);
        float tabsBottom = Tabs(c, tabs, sectionIndex, rule + 14.0f);
        float tabH = 0.0f, y = tabsBottom;        // The list.
        float listTop = y + tabH + 18.0f; // (tabH 0: Tabs gave the bottom of its last row)
        float listBottom = 692.0f;
        int fit = std::max(1, static_cast<int>((listBottom - listTop) / kLevelRowHeight));
        if (visibleRows)
            *visibleRows = fit;
        RowStyle rs = LevelRowStyle();
        c.dl->PushClipRect(ImVec2(c.f.X(0.0f), c.f.Y(listTop - 2.0f)), ImVec2(c.f.X(kColumn + kScrimFeather), c.f.Y(listBottom + 8.0f)), true);
        float scroll = menu.ListScroll();
        int count = view ? static_cast<int>(view->levels.size()) : 0;
        int first = std::max(0, static_cast<int>(std::floor(scroll)) - 1);
        int last = std::min(count - 1, static_cast<int>(std::ceil(scroll)) + fit + 1);
        for (int row = first; row <= last; ++row) {
            const Level& l = section->levels[view->levels[row]];
            float top = listTop + (static_cast<float>(row) - scroll) * kLevelRowHeight;
            bool locked = row < static_cast<int>(view->locked.size()) && view->locked[row];
            Row(c, rs, top, Pad2(row + 1).c_str(), l.name.c_str(), menu.RowAmount(row), colour, locked);
            if (top >= listTop - 1.0f && top + kLevelRowHeight <= listBottom + 1.0f)
                c.Hit(Hit::Level, row, 48.0f, top, kColumnInner + 20.0f, top + kLevelRowHeight);
        }
        c.dl->PopClipRect();
        if (count == 0) {
            Style empty = Mono12(kMuted2);
            Text(c.dl, empty, c.f, c.f.X(48.0f), Baseline(empty, c.f, c.f.Y(listTop), 1.3f), Text::kNoLevels);
        }
        c.dl->PopClipRect();
    }
    if (grow > 0.0f) {
        // (the launch: the picture comes up sharp over the column)
        c.dl->PushClipRect(ImVec2(c.f.X(0.0f), c.f.Y(0.0f)), ImVec2(c.f.X(kColumn + kScrimFeather), c.f.Y(720.0f)), true);
        ArtBox(c.dl, art, c.f.X(0.0f), c.f.Y(0.0f), c.f.X(1280.0f), c.f.Y(720.0f), menu.Pan(), grow);
        c.dl->PopClipRect();
    }

    // The right's foot: Skate, the records, the name, the kicker.
    float left = kRight + (32.0f - kRight) * grow, right = 1240.0f;
    float bottom = 692.0f;
    if (level) {
        float fade = 1.0f - grow;
        // Skate (Anton 30 on the game's colour, a slanted button).
        Style skate;
        skate.face = Face::Display;
        skate.css = 30.0f;
        skate.upper = true;
        skate.colour = kGround;
        skate.alpha = fade;
        bool locked = menu.SelectedLocked();
        const char* skateText = locked ? Text::kLocked : menu.LevelsPurpose() == LevelsFor::Edit ? Text::kEdit : Text::kSkate;
        float tw = Measure(skate, c.f, skateText) / c.f.scale;
        float arrowW = locked ? 0.0f : 30.0f * 0.9f + 10.0f;
        float bh = 30.0f * 1.15f + 12.0f;
        float bx0 = left, bx1 = left + 22.0f + tw + arrowW + 22.0f, by0 = bottom - bh, by1 = bottom;
        Cut(c.dl, c.f.X(bx0), c.f.Y(by0), c.f.X(bx1), c.f.Y(by1), c.f.S(10.0f), c.f.S(10.0f),
            Col(locked ? kMuted2 : colour, fade));
        float sb = Baseline(skate, c.f, c.f.Y(by0 + 6.0f), 1.15f);
        Text(c.dl, skate, c.f, c.f.X(bx0 + 22.0f), sb, skateText);
        if (!locked) {
            float adv = 0.0f;
            Arrow(c.dl, c.f.X(bx0 + 22.0f + tw + 10.0f), sb, c.f.S(30.0f), false, Col(kGround, fade), &adv);
        }
        c.Hit(Hit::Skate, 0, bx0, by0, bx1, by1);
        float y = by0 - 6.0f;
        // The records (only what the career keeps for this level).
        Records rec = c.extras.records ? c.extras.records(*level) : Records{};
        std::vector<std::string> cols;
        char text[64];
        if (rec.gapsTotal > 0) {
            std::snprintf(text, sizeof(text), "%s %d/%d", Text::kGaps, rec.gapsGot, rec.gapsTotal);
            cols.push_back(text);
        }
        if (rec.goals >= 0) {
            std::snprintf(text, sizeof(text), "%s %d", Text::kGoals, rec.goals);
            cols.push_back(text);
        }
        if (!cols.empty()) {
            Style s = Mono12(kMuted, false);
            s.alpha = fade;
            float top = y - 10.0f - 12.0f * 1.3f;
            float colW = (right - left) / 3.0f;
            for (size_t i = 0; i < cols.size(); ++i)
                Text(c.dl, s, c.f, c.f.X(left + colW * static_cast<float>(i)), Baseline(s, c.f, c.f.Y(top), 1.3f),
                     cols[i].c_str());
            float ruleY = top - 10.0f - 1.0f;
            c.dl->AddRectFilled(ImVec2(c.f.X(left), c.f.Y(ruleY)), ImVec2(c.f.X(right), c.f.Y(ruleY) + std::max(1.0f, c.f.S(1.0f))),
                                Col(kRule, fade));
            y = ruleY;
        }
        float slice = launch >= 0.0f ? EaseOut(launch * 1.6f) : 0.0f;
        float nameTop = BigName(c, level->name, 112.0f, left, right, y - 10.0f, slice, colour);
        std::string kicker = (section ? section->tab : std::string()) + " \xE2\x80\x94 " + Pad2(menu.LevelRow() + 1);
        if (locked)
            kicker += std::string(" \xC2\xB7 ") + Text::kLocked;
        Kicker(c, kicker, left, nameTop - 10.0f, colour, 1.0f - slice);
    }
}

// The screen change: one diagonal band in `colour` across the window - in over the first half (the screen changes
// under it), out over the second.
void Wipe(ImDrawList* dl, float t, uint32_t colour, float width, float height)
{
    if (t < 0.0f)
        return;
    float skew = height * 0.35f;
    float lead = -skew + (width + skew * 2.0f) * EaseOut(std::min(1.0f, t * 2.0f));
    float trail = -skew + (width + skew * 2.0f) * EaseOut(std::max(0.0f, t * 2.0f - 1.0f));
    if (lead <= trail)
        return;
    ImVec2 points[4] = {{trail + skew, 0.0f}, {lead + skew, 0.0f}, {lead, height}, {trail, height}};
    dl->AddConvexPolyFilled(points, 4, Col(colour));
}

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

Style Body(uint32_t colour = kMuted)
{
    Style s;
    s.face = Face::Body;
    s.css = 14.0f;
    s.colour = colour;
    return s;
}

// Paragraphs of body text from `top` (design y) between `left` and `right`, at most `maxLines` lines. Returns the
// bottom.
float Paragraphs(Ctx& c, const std::vector<std::string>& texts, float left, float right, float top, int maxLines,
                 uint32_t colour = kMuted)
{
    Style body = Body(colour);
    float lineH = 14.0f * 1.5f;
    int drawn = 0;
    for (const std::string& text : texts) {
        // (each \n of a dialog's text a line of its own)
        size_t at = 0;
        while (at <= text.size() && drawn < maxLines) {
            size_t end = text.find('\n', at);
            std::string part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            for (const std::string& line : Wrap(body, c.f, part, c.f.S(right - left))) {
                if (drawn >= maxLines)
                    break;
                Text(c.dl, body, c.f, c.f.X(left), Baseline(body, c.f, c.f.Y(top), 1.5f), line.c_str());
                top += lineH;
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
void DialogScreen(Ctx& c, const Mirror::Tracker& t, float windowWidth, float windowHeight)
{
    const Mirror::View& v = t.Shown();
    c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(windowWidth, windowHeight), Col(kGround, 0.62f));
    float y0 = 196.0f, y1 = 548.0f;
    c.dl->AddRectFilled(ImVec2(0.0f, c.f.Y(y0)), ImVec2(windowWidth, c.f.Y(y1)), Col(kGround, 0.94f));
    c.dl->AddRectFilled(ImVec2(0.0f, c.f.Y(y0)), ImVec2(windowWidth, c.f.Y(y0 + 4.0f)), Col(Menu::kHouseYellow));
    float left = 96.0f, textRight = 700.0f;
    int choices = 0;
    for (const Mirror::Row& r : v.rows)
        choices += r.focusable ? 1 : 0;
    const char* kicker = choices >= 2 ? Text::kDialogConfirm : choices == 1 ? Text::kDialogNotice : Text::kPleaseWait;
    float kickerTop = Kicker(c, kicker, left, y0 + 44.0f, Menu::kHouseYellow);
    (void)kickerTop;
    // The title (Anton 72, up to two lines) under the kicker.
    Style title;
    title.face = Face::Display;
    title.css = 72.0f;
    title.upper = true;
    std::vector<std::string> titleLines = Wrap(title, c.f, v.title, c.f.S(textRight - left));
    if (titleLines.size() > 2)
        titleLines.resize(2);
    float y = y0 + 56.0f;
    for (const std::string& line : titleLines) {
        Text(c.dl, title, c.f, c.f.X(left), Baseline(title, c.f, c.f.Y(y), 0.9f), line.c_str());
        y += 72.0f * 0.9f;
    }
    Paragraphs(c, v.lines, left, textRight, y + 14.0f, 6, kPaper);
    // The choices.
    RowStyle rs = LevelRowStyle();
    float rowsLeft = 760.0f;
    rs.textX = rowsLeft + 26.0f + 14.0f + 10.0f;
    rs.cutLeft = rowsLeft + 26.0f + 14.0f - 6.0f;
    int n = static_cast<int>(v.rows.size());
    float top = (y0 + y1) * 0.5f - static_cast<float>(n) * kLevelRowHeight * 0.5f;
    for (int i = 0; i < n; ++i) {
        float rowTop = top + static_cast<float>(i) * kLevelRowHeight;
        // (Row puts the number at the design's x 48: drawn here on the right instead)
        ImDrawList* dl = c.dl;
        int start = dl->VtxBuffer.Size;
        float e = t.RowAmount(i);
        float base = Baseline(rs.name, c.f, c.f.Y(rowTop), rs.lineHeight);
        float width = Measure(rs.name, c.f, v.rows[i].label.c_str());
        if (e > 0.001f) {
            float x0 = c.f.X(rs.cutLeft), x1 = c.f.X(rs.textX) + width + c.f.S(rs.cutRightPad);
            x1 = x0 + (x1 - x0) * e;
            float lineH = rs.name.css * rs.lineHeight;
            Cut(dl, x0, c.f.Y(rowTop + lineH * rs.cutTop), x1, c.f.Y(rowTop + lineH * rs.cutBottom), c.f.S(rs.topInset),
                c.f.S(rs.bottomInset), Col(Menu::kHouseYellow));
        }
        Style name = rs.name;
        name.colour = Mix(v.rows[i].focusable ? kPaper : kMuted2, kGround, e);
        Text(dl, name, c.f, c.f.X(rs.textX), base, v.rows[i].label.c_str());
        Text(dl, rs.number, c.f, c.f.X(rowsLeft), base, Pad2(i + 1).c_str());
        if (e > 0.001f)
            Transform(dl, start, c.f.X(rowsLeft), c.f.Y(rowTop + kLevelRowHeight * 0.5f), -3.0f * 3.14159265f / 180.0f * e,
                      c.f.S(24.0f) * e, 0.0f);
        if (v.rows[i].focusable)
            c.Hit(Hit::Item, i, rowsLeft, rowTop, 1240.0f, rowTop + kLevelRowHeight);
    }
    std::vector<std::string> hints = {Text::kHintSelect, Text::kHintBack};
    if (choices == 0)
        hints = {Text::kPleaseWait};
    Hints(c, hints, left, 1184.0f, y1 - 18.0f);
}

// The skater select: the game's 3D skater stays in view (left of centre, as the original's camera puts it); the
// skaters as a strip across the top, the selected one's name and stats on a panel on the right.
void SkatersScreen(Ctx& c, const Mirror::Tracker& t, float windowWidth, float windowHeight)
{
    const Mirror::View& v = t.Shown();
    // The top: a shade for the strip and the header.
    c.dl->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(windowWidth, c.f.Y(170.0f)), Col(kGround, 0.92f),
                                  Col(kGround, 0.92f), Col(kGround, 0.0f), Col(kGround, 0.0f));
    // The right: a panel feathered from x 720.
    float panel = 800.0f;
    c.dl->AddRectFilledMultiColor(ImVec2(c.f.X(panel - 90.0f), 0.0f), ImVec2(c.f.X(panel), windowHeight), Col(kGround, 0.0f),
                                  Col(kGround, 0.86f), Col(kGround, 0.86f), Col(kGround, 0.0f));
    c.dl->AddRectFilled(ImVec2(c.f.X(panel), 0.0f), ImVec2(windowWidth, windowHeight), Col(kGround, 0.86f));
    // The foot: a shade under the hints.
    c.dl->AddRectFilledMultiColor(ImVec2(0.0f, c.f.Y(600.0f)), ImVec2(windowWidth, windowHeight), Col(kGround, 0.0f),
                                  Col(kGround, 0.0f), Col(kGround, 0.9f), Col(kGround, 0.9f));

    std::string count = v.selected >= 0 ? Pad2(v.selected + 1) + " / " + Pad2(static_cast<int>(v.rows.size())) : std::string();
    Header(c, 36.0f, 1232.0f, v.title.c_str(), true, count.c_str());

    // The strip: every skater's name (Anton 28), the selected one centred, cut and turned.
    Style name;
    name.face = Face::Display;
    name.css = 28.0f;
    name.upper = true;
    float gap = 34.0f;
    std::vector<float> widths;
    float total = 0.0f, selectedCentre = 0.0f;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        widths.push_back(Measure(name, c.f, v.rows[i].label.c_str()) / c.f.scale);
        if (static_cast<int>(i) == v.selected)
            selectedCentre = total + widths.back() * 0.5f;
        total += widths.back() + gap;
    }
    float shift = 640.0f - selectedCentre; // (the strip's x 0 in design px)
    shift = std::min(shift, 48.0f);
    shift = std::max(shift, 1232.0f - (total - gap));
    if (total - gap < 1184.0f)
        shift = 48.0f;
    float stripTop = 88.0f;
    c.dl->PushClipRect(ImVec2(c.f.X(0.0f), c.f.Y(stripTop - 20.0f)), ImVec2(c.f.X(1280.0f), c.f.Y(stripTop + 60.0f)), true);
    float x = shift;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        float e = t.RowAmount(static_cast<int>(i));
        ImDrawList* dl = c.dl;
        int start = dl->VtxBuffer.Size;
        float lineH = 28.0f * 1.05f;
        if (e > 0.001f)
            Cut(dl, c.f.X(x - 10.0f), c.f.Y(stripTop + lineH * 0.12f), c.f.X(x - 10.0f) + (c.f.S(widths[i] + 26.0f)) * e,
                c.f.Y(stripTop + lineH * 0.98f), c.f.S(7.0f), c.f.S(10.0f), Col(Menu::kHouseYellow));
        Style s = name;
        s.colour = Mix(v.rows[i].focusable ? kPaper : kMuted2, kGround, e);
        Text(dl, s, c.f, c.f.X(x), Baseline(s, c.f, c.f.Y(stripTop), 1.05f), v.rows[i].label.c_str());
        if (e > 0.001f)
            Transform(dl, start, c.f.X(x), c.f.Y(stripTop + lineH * 0.5f), -3.0f * 3.14159265f / 180.0f * e, 0.0f,
                      -c.f.S(4.0f) * e);
        if (v.rows[i].focusable && x + widths[i] > 0.0f && x < 1280.0f)
            c.Hit(Hit::Item, static_cast<int>(i), x - 10.0f, stripTop - 6.0f, x + widths[i] + 16.0f, stripTop + lineH + 6.0f);
        x += widths[i] + gap;
    }
    c.dl->PopClipRect();
    // (a rule under the strip)
    c.dl->AddRectFilled(ImVec2(c.f.X(48.0f), c.f.Y(stripTop + 48.0f)), ImVec2(c.f.X(1232.0f), c.f.Y(stripTop + 48.0f) + std::max(1.0f, c.f.S(1.0f))),
                        Col(kRule));

    // The panel: the stats as a mono table with bars, the name over it, the kicker.
    float left = panel + 40.0f, right = 1232.0f;
    float bottom = Hints(c, {Text::kHintSkater, Text::kHintAccept, Text::kHintBack, Text::kHintLoadSkater}, 48.0f, 1232.0f, 692.0f) - 22.0f;
    Style label = Mono12(kMuted, false);
    label.css = 13.0f;
    Style number = Mono12(kPaper, false);
    number.css = 13.0f;
    float rowH = 26.0f;
    int stats = static_cast<int>(v.stats.size());
    float tableTop = bottom - rowH * static_cast<float>(stats);
    for (int i = 0; i < stats; ++i) {
        float top = tableTop + rowH * static_cast<float>(i);
        float base = Baseline(label, c.f, c.f.Y(top + 4.0f), 1.3f);
        Text(c.dl, label, c.f, c.f.X(left), base, v.stats[i].first.c_str());
        int value = std::clamp(v.stats[i].second, 0, 10);
        float barLeft = left + 110.0f, barRight = right - 40.0f;
        float seg = (barRight - barLeft) / 10.0f;
        for (int k = 0; k < 10; ++k) {
            float sx = barLeft + seg * static_cast<float>(k);
            Cut(c.dl, c.f.X(sx), c.f.Y(top + 7.0f), c.f.X(sx + seg - 4.0f), c.f.Y(top + 19.0f), c.f.S(3.0f), c.f.S(3.0f),
                Col(k < value ? Menu::kHouseYellow : kRule));
        }
        std::string text = std::to_string(v.stats[i].second);
        Text(c.dl, number, c.f, c.f.X(right) - Measure(number, c.f, text.c_str()), base, text.c_str());
    }
    if (stats > 0) {
        float rule = tableTop - 12.0f;
        c.dl->AddRectFilled(ImVec2(c.f.X(left), c.f.Y(rule)), ImVec2(c.f.X(right), c.f.Y(rule) + std::max(1.0f, c.f.S(1.0f))),
                            Col(kRule));
        bottom = rule - 14.0f;
    }
    float nameTop = BigName(c, v.bigName, 84.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    std::string kicker = Text::kSkater;
    if (v.selected >= 0 && !v.rows[v.selected].focusable)
        kicker += std::string(" \xC2\xB7 ") + Text::kLocked;
    Kicker(c, kicker, left, nameTop - 8.0f, Menu::kHouseYellow);
}

// A plain list: the pause menus' look - the screen dimmed, the column's scrim, the items numbered down the left (each
// one's value in mono at the column's right), the selected item's name on the right with the screen's other words.
void ListOfItems(Ctx& c, const Mirror::Tracker& t, float windowWidth, float windowHeight, float* scroll)
{
    const Mirror::View& v = t.Shown();
    PauseGround(c, windowWidth, windowHeight);
    float rule = Header(c, 40.0f, kColumnInner, v.title.empty() ? Text::kTitleMenu : v.title.c_str(), true, nullptr);
    int n = static_cast<int>(v.rows.size());
    bool big = n <= 6;
    if (big) {
        RowStyle probe = MainRowStyle();
        for (const Mirror::Row& r : v.rows)
            if (c.f.S(probe.textX) + Measure(probe.name, c.f, r.label.c_str()) > c.f.S(kColumnInner + 10.0f))
                big = false;
    }
    RowStyle rs = big ? MainRowStyle() : LevelRowStyle();
    float rowH = big ? kMainRowHeight + 2.0f : kLevelRowHeight;
    float listTop = rule + (big ? 36.0f : 20.0f);
    float listBottom = 620.0f;
    int fit = std::max(1, static_cast<int>((listBottom - listTop) / rowH));
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
    Style value = Mono12(kMuted, false);
    c.dl->PushClipRect(ImVec2(c.f.X(0.0f), c.f.Y(listTop - 2.0f)), ImVec2(c.f.X(kColumn + kScrimFeather), c.f.Y(listBottom + 8.0f)), true);
    for (int i = 0; i < n; ++i) {
        float top = listTop + (static_cast<float>(i) - first) * rowH;
        if (top < listTop - rowH || top > listBottom)
            continue;
        const Mirror::Row& r = v.rows[i];
        Row(c, rs, top, Pad2(i + 1).c_str(), r.label.c_str(), t.RowAmount(i), Menu::kHouseYellow, !r.focusable);
        if (!r.value.empty()) {
            float base = Baseline(rs.name, c.f, c.f.Y(top + rs.rowPadTop), rs.lineHeight);
            value.colour = i == v.selected ? Menu::kHouseYellow : kMuted;
            float valueX = c.f.X(kColumnInner) - Measure(value, c.f, r.value.c_str());
            if (c.f.X(rs.textX) + Measure(rs.name, c.f, r.label.c_str()) + c.f.S(24.0f) < valueX)
                Text(c.dl, value, c.f, valueX, base, r.value.c_str());
        }
        if (r.focusable && top >= listTop - 1.0f && top + rowH <= listBottom + 1.0f)
            c.Hit(Hit::Item, i, 48.0f, top, kColumnInner + 20.0f, top + rowH);
    }
    c.dl->PopClipRect();

    // The right: the hints, the screen's other words, the selected item (its value under it), the title as the kicker.
    float left = kRight, right = 1240.0f;
    float bottom = Hints(c, {Text::kHintSelect, Text::kHintChange, Text::kHintBack}, left, right, 688.0f) - 12.0f;
    const Mirror::Row* sel = v.selected >= 0 ? &v.rows[v.selected] : nullptr;
    if (sel && !sel->value.empty()) {
        Style d = Mono12(Menu::kHouseYellow, false);
        d.css = 13.0f;
        float top = bottom - 13.0f * 1.3f;
        Text(c.dl, d, c.f, c.f.X(left), Baseline(d, c.f, c.f.Y(top), 1.3f), sel->value.c_str());
        bottom = top - 12.0f;
    }
    float nameTop = BigName(c, v.bigName, 96.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    Kicker(c, v.title.empty() ? std::string(Text::kTitleMenu) : v.title, left, nameTop - 8.0f, Menu::kHouseYellow);
    if (!v.lines.empty())
        Paragraphs(c, v.lines, left, right, 48.0f, 10);
}

// A level's pause menu restyled (Kind::Pause): the game's items as numbered rows on the left (the main menu's type
// while they fit, the level list's past that), the level and the session on the right - the pause design, the menu
// itself the game's.
void PauseOfItems(Ctx& c, const Mirror::Tracker& t, float windowWidth, float windowHeight, float* scroll)
{
    const Mirror::View& v = t.Shown();
    PauseGround(c, windowWidth, windowHeight);
    float rule = Header(c, 40.0f, kColumnInner, Text::kTitlePaused, false, nullptr);
    int n = static_cast<int>(v.rows.size());
    bool big = n <= 7;
    if (big) {
        RowStyle probe = MainRowStyle();
        for (const Mirror::Row& r : v.rows)
            if (c.f.S(probe.textX) + Measure(probe.name, c.f, r.label.c_str()) > c.f.S(kColumnInner + 10.0f))
                big = false;
    }
    RowStyle rs = big ? MainRowStyle() : LevelRowStyle();
    float rowH = big ? kMainRowHeight + 2.0f : kLevelRowHeight;
    float listTop = rule + (big ? 36.0f : 20.0f);
    float listBottom = 680.0f;
    int fit = std::max(1, static_cast<int>((listBottom - listTop) / rowH));
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
    c.dl->PushClipRect(ImVec2(c.f.X(0.0f), c.f.Y(listTop - 2.0f)), ImVec2(c.f.X(kColumn + kScrimFeather), c.f.Y(listBottom + 8.0f)), true);
    for (int i = 0; i < n; ++i) {
        float top = listTop + (static_cast<float>(i) - first) * rowH;
        if (top < listTop - rowH || top > listBottom)
            continue;
        const Mirror::Row& r = v.rows[i];
        Row(c, rs, top, Pad2(i + 1).c_str(), r.label.c_str(), t.RowAmount(i), Menu::kHouseYellow, !r.focusable);
        if (r.focusable && top >= listTop - 1.0f && top + rowH <= listBottom + 1.0f)
            c.Hit(Hit::Item, i, 48.0f, top, kColumnInner + 20.0f, top + rowH);
    }
    c.dl->PopClipRect();

    float left = kRight, right = 1240.0f;
    float bottom = Hints(c, {Text::kHintSelect, Text::kHintResumeLevel}, left, right, 688.0f) - 8.0f;
    std::string kicker, name;
    uint32_t colour = Menu::kHouseYellow;
    bottom = PauseSide(c, left, right, bottom, &kicker, &name, &colour);
    float nameTop = BigName(c, name, 120.0f, left, right, bottom, 0.0f, colour);
    Kicker(c, kicker, left, nameTop - 8.0f, colour, 1.0f);
}

// A front end menu restyled (Kind::Menu): the main menu's look - the highlighted item's art full-bleed behind the
// column, the game's items as numbered rows in big type (a note after a label), the item's line at the foot, its name
// big on the right under the kicker; the main menu's Continue hint (X: the last level played).
void MenuOfItems(Ctx& c, const Mirror::Tracker& t, float* scroll)
{
    const Mirror::View& v = t.Shown();
    const Mirror::Row* sel = v.selected >= 0 ? &v.rows[v.selected] : nullptr;
    std::string art = sel && !sel->art.empty() ? ArtById(c.extras.catalogue, sel->art) : std::string();
    if (art.empty())
        art = ArtById(c.extras.catalogue, v.screenArt);
    Backdrop(c.dl, c.f, ArtFor(c.extras, art), 0.0f);
    float rule = Header(c, 40.0f, kColumnInner, v.title.c_str(), !v.mainMenu, nullptr);
    int n = static_cast<int>(v.rows.size());
    bool big = n <= 7;
    if (big) {
        RowStyle probe = MainRowStyle();
        for (const Mirror::Row& r : v.rows)
            if (c.f.S(probe.textX) + Measure(probe.name, c.f, r.label.c_str()) > c.f.S(kColumnInner + 10.0f))
                big = false;
    }
    RowStyle rs = big ? MainRowStyle() : LevelRowStyle();
    float rowH = big ? kMainRowHeight + 2.0f : kLevelRowHeight;
    float listTop = rule + (big ? 36.0f : 20.0f);
    float listBottom = 620.0f;
    int fit = std::max(1, static_cast<int>((listBottom - listTop) / rowH));
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
    c.dl->PushClipRect(ImVec2(c.f.X(0.0f), c.f.Y(listTop - 2.0f)), ImVec2(c.f.X(kColumn + kScrimFeather), c.f.Y(listBottom + 8.0f)), true);
    for (int i = 0; i < n; ++i) {
        float top = listTop + (static_cast<float>(i) - first) * rowH;
        if (top < listTop - rowH || top > listBottom)
            continue;
        const Mirror::Row& r = v.rows[i];
        Row(c, rs, top, Pad2(i + 1).c_str(), r.label.c_str(), t.RowAmount(i), Menu::kHouseYellow, !r.focusable);
        if (!r.tag.empty()) {
            Style tag = Mono12(kMuted2);
            float base = Baseline(rs.name, c.f, c.f.Y(top + rs.rowPadTop), rs.lineHeight);
            float x = c.f.X(rs.textX) + Measure(rs.name, c.f, r.label.c_str()) + c.f.S(16.0f);
            Text(c.dl, tag, c.f, x, base - c.f.S(rs.name.css * 0.12f), r.tag.c_str());
        }
        if (r.focusable && top >= listTop - 1.0f && top + rowH <= listBottom + 1.0f)
            c.Hit(Hit::Item, i, 48.0f, top, kColumnInner + 20.0f, top + rowH);
    }
    c.dl->PopClipRect();
    Style body;
    body.face = Face::Body;
    body.css = 14.0f;
    body.colour = kMuted;
    std::vector<std::string> lines = Wrap(body, c.f, sel ? sel->blurb : std::string(), c.f.S(kColumnInner - 48.0f));
    float lineH = 14.0f * 1.5f;
    float y = 688.0f - lineH * static_cast<float>(lines.size());
    for (const std::string& line : lines) {
        Text(c.dl, body, c.f, c.f.X(48.0f), Baseline(body, c.f, c.f.Y(y), 1.5f), line.c_str());
        y += lineH;
    }

    float left = kRight, right = 1240.0f;
    std::vector<std::string> hints = {Text::kHintSelect};
    int resumeHint = -1;
    if (v.mainMenu && !c.extras.continueName.empty()) {
        resumeHint = static_cast<int>(hints.size());
        hints.push_back(std::string(Text::kHintResume) + " " + c.extras.continueName);
    }
    if (!v.mainMenu)
        hints.push_back(Text::kHintBack);
    std::vector<std::pair<float, float>> spans;
    float bottom = Hints(c, hints, left, right, 688.0f, &spans) - 8.0f;
    if (resumeHint >= 0 && resumeHint < static_cast<int>(spans.size()))
        c.Hit(Hit::Continue, 0, spans[resumeHint].first - 6.0f, 688.0f - 12.0f * 1.3f - 6.0f, spans[resumeHint].second + 6.0f,
              688.0f + 6.0f);
    float nameTop = BigName(c, v.bigName, 120.0f, left, right, bottom, 0.0f, Menu::kHouseYellow);
    Kicker(c, v.kicker, left, nameTop - 8.0f, Menu::kHouseYellow, 1.0f);
}

} // namespace

// --- the public parts -------------------------------------------------------------------------------------------

float BaselineInLine(float ascent, float descent, float css, float lineHeight)
{
    float content = (ascent - descent) * css;
    return (css * lineHeight - content) * 0.5f + ascent * css;
}

float MainRowTop(int index)
{
    // (the header's rule at 40 + 12 * 1.3 + 12, 1 px; the nav 36 below; rows 2 apart)
    float rule = 40.0f + 12.0f * 1.3f + 12.0f + 1.0f;
    return rule + 36.0f + static_cast<float>(index) * (kMainRowHeight + 2.0f);
}

float LevelListTop(int tabRows)
{
    float rule = 36.0f + 12.0f * 1.3f + 12.0f + 1.0f;
    float tabH = 13.0f * 1.3f + 8.0f;
    int rows = std::max(1, tabRows);
    float lastTabTop = rule + 14.0f + static_cast<float>(rows - 1) * (tabH + 4.0f);
    return lastTabTop + tabH + 18.0f;
}

int LevelListRows(int tabRows)
{
    return std::max(1, static_cast<int>((692.0f - LevelListTop(tabRows)) / kLevelRowHeight));
}

bool ReadFontMetrics(const uint8_t* data, size_t size, float* ascent, float* descent)
{
    if (!data || size < 12)
        return false;
    uint32_t tag = Be32(data);
    if (tag != 0x00010000 && tag != 0x74727565 /* true */ && tag != 0x4F54544F /* OTTO */)
        return false;
    int tables = Be16(data + 4);
    const uint8_t* head = nullptr;
    const uint8_t* hhea = nullptr;
    for (int i = 0; i < tables; ++i) {
        size_t entry = 12 + static_cast<size_t>(i) * 16;
        if (entry + 16 > size)
            return false;
        uint32_t name = Be32(data + entry);
        uint32_t offset = Be32(data + entry + 8);
        if (offset + 54 > size)
            continue;
        if (name == 0x68656164) // head
            head = data + offset;
        else if (name == 0x68686561) // hhea
            hhea = data + offset;
    }
    if (!head || !hhea)
        return false;
    float unitsPerEm = static_cast<float>(Be16(head + 18));
    if (unitsPerEm <= 0.0f)
        return false;
    *ascent = static_cast<float>(static_cast<int16_t>(Be16(hhea + 4))) / unitsPerEm;
    *descent = static_cast<float>(static_cast<int16_t>(Be16(hhea + 6))) / unitsPerEm;
    return *ascent > 0.0f;
}

void ForgetFonts()
{
    g_atlas = nullptr;
    g_scale = 0.0f;
    for (Baked& b : g_baked)
        b = Baked{};
}

float BuiltScale() { return ImGui::GetCurrentContext() && ImGui::GetIO().Fonts == g_atlas ? g_scale : 0.0f; }

bool BuildFonts(const FontFiles& files, float scale)
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
        FaceData& face = g_faces[i];
        face = FaceData{};
        if (paths[i]->empty() || !ReadFile(*paths[i], &face.file) ||
            !ReadFontMetrics(face.file.data(), face.file.size(), &face.ascent, &face.descent)) {
            Log("new front end: can't read the font %s", paths[i]->empty() ? "(none)" : paths[i]->c_str());
            face.file.clear();
            all = false;
            continue;
        }
        face.ok = true;
    }
    for (int i = 0; i < kBakeCount; ++i) {
        FaceData& face = g_faces[static_cast<int>(kBakes[i].face)];
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
        float pixelHeight = em * (face.ascent - face.descent);
        ImFont* font = atlas->AddFontFromMemoryTTF(face.file.data(), static_cast<int>(face.file.size()), pixelHeight,
                                                   &config,
                                                   kBakes[i].face == Face::Display ? kDisplayRanges : kTextRanges);
        g_baked[i] = {font, kBakes[i].face, em};
    }
    g_scale = scale;
    g_atlas = atlas;
    atlas->Build();
    Log("new front end: fonts for scale %.2f - atlas %dx%d", scale, atlas->TexWidth, atlas->TexHeight);
    return all;
}

void Screen(const Menu& menu, const Frame& frame, float windowWidth, float windowHeight, const Extras& extras,
            std::vector<Hit>* hits, int* visibleRows)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    // (the pause menus: over the game, dimmed - the rest on the ground's colour)
    bool overGame = IsPauseScreen(menu.Shown()) || (menu.Shown() == Screen::Options && menu.OverPause());
    if (!overGame)
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(windowWidth, windowHeight), Col(kGround));
    Ctx c{dl, frame, menu, extras, hits};
    if (hits)
        hits->clear();
    if (menu.Shown() == Screen::Levels)
        LevelsScreen(c, visibleRows);
    else if (menu.Shown() == Screen::Options)
        OptionsScreen(c, windowWidth, windowHeight);
    // (the roots - Main, Pause - are never drawn: ours goes aside at them)
    // (a 16:9 frame in a wider or taller window: the bars the ground's colour, over anything that spilled)
    if (frame.left > 0.5f && !overGame) {
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(frame.left, windowHeight), Col(kGround));
        dl->AddRectFilled(ImVec2(frame.X(kDesignWidth), 0.0f), ImVec2(windowWidth, windowHeight), Col(kGround));
    }
    if (frame.top > 0.5f && !overGame) {
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(windowWidth, frame.top), Col(kGround));
        dl->AddRectFilled(ImVec2(0.0f, frame.Y(kDesignHeight)), ImVec2(windowWidth, windowHeight), Col(kGround));
    }
    Wipe(dl, menu.Wipe(), menu.WipeColour(), windowWidth, windowHeight);
    dl->PopTextureID();
}


void Restyled(const Mirror::Tracker& tracker, const Frame& frame, float windowWidth, float windowHeight,
              std::vector<Hit>* hits, float* scroll, const Extras* extras)
{
    static const Menu kNoMenu;
    static const Extras kNoExtras;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    Ctx c{dl, frame, kNoMenu, extras ? *extras : kNoExtras, hits};
    if (hits)
        hits->clear();
    switch (tracker.Shown().kind) {
    case Mirror::Kind::Dialog: DialogScreen(c, tracker, windowWidth, windowHeight); break;
    case Mirror::Kind::Skaters: SkatersScreen(c, tracker, windowWidth, windowHeight); break;
    case Mirror::Kind::List: ListOfItems(c, tracker, windowWidth, windowHeight, scroll); break;
    case Mirror::Kind::Pause: PauseOfItems(c, tracker, windowWidth, windowHeight, scroll); break;
    case Mirror::Kind::Menu:
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(windowWidth, windowHeight), Col(kGround));
        MenuOfItems(c, tracker, scroll);
        break;
    case Mirror::Kind::None: break;
    }
    dl->PopTextureID();
}

} // namespace Front::NewFrontend::Paint
