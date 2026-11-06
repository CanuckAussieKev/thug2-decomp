// NOT ORIGINAL - the new front end's shapes (ours): cuts, arrows, pictures in a box, the shade, the scrim and the
// backdrop. See new_frontend_draw.h.

#include "frontend/new_frontend_draw.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <array>
#include "frontend/new_frontend_model.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/maths/rect.h"
#include "base/maths/vec2.h"
#include "frontend/new_frontend_draw_internal.h"
#include "base/maths/trig.h"

namespace frontend::new_frontend::paint {
// What the source file's anonymous namespace held that its pieces share (new_frontend_draw_internal.h): visible as it
// was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend::paint

namespace frontend::new_frontend::paint {
namespace {

constexpr float kArrowTrailingGap = 0.45F; // of the arrow's size, between it and the text after it
constexpr uint32_t kOpaqueWhite = 0xFFFFFFFF;

constexpr float kShadeAlpha = 0.88F;  // the art's foot shade at its darkest
constexpr float kShadeLeadIn = 32.0F; // the shade begins this far left of the right panel
constexpr int kScrimCopyCount = 9;    // the scrim averages this many offset copies of the art
constexpr int kScrimCopyIndices = 12; // two quads of six indices
constexpr int kScrimCopyVertices = 8; // two quads of four vertices
constexpr std::size_t kPad2BufferSize = 16;

constexpr uint32_t kArtGround = 0xFF1C1C1B;

} // namespace
namespace internal {

// The design's clip-path: polygon(a 0, 100% 0, calc(100% - b) 100%, 0 100%) - a parallelogram leaning right.
void get_cut(ImDrawList* dl, const maths::Rect& box, Slant slant, ImU32 colour)
{
    std::array<ImVec2, 4> points = {
        {{box.x0 + slant.top, box.y0}, {box.x1, box.y0}, {box.x1 - slant.bottom, box.y1}, {box.x0, box.y1}}};
    dl->AddConvexPolyFilled(points.data(), 4, colour);
}

// The vertices added since `start` turned by `radians` about `pivot` and moved by `offset` - the row's
// translateX(24px) rotate(-3deg) about its left centre.
void xform(ImDrawList* dl, int start, maths::Vec2 pivot, float radians, maths::Vec2 offset)
{
    const float px = pivot.x;
    const float py = pivot.y;
    const float dx = offset.x;
    const float dy = offset.y;
    const float cosine = maths::runtime32::cos(radians);
    const float sine = maths::runtime32::sin(radians);
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& vertex_position = core::at(dl->VtxBuffer, i).pos;
        const float x = vertex_position.x - px;
        const float y = vertex_position.y - py;
        vertex_position.x = px + dx + (x * cosine) - (y * sine);
        vertex_position.y = py + dy + (x * sine) + (y * cosine);
    }
}

// An arrow (the design's "<-" / "->" in the mono face): a line with a head, `size` px tall, at the text's x-height -
// from window x `at.x`, on the baseline `at.y`.
void get_arrow(ImDrawList* dl, maths::Vec2 at, float size, bool left, ImU32 colour, float* advance)
{
    const float x = at.x;
    const float baseline = at.y;
    const float w = size * 0.9F;
    const float y = baseline - (size * 0.32F);
    const float thickness = std::max(1.0F, size * 0.09F);
    const float x0 = x;
    const float x1 = x + w;
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), colour, thickness);
    const float hx = left ? x0 : x1;
    const float dir = left ? 1.0F : -1.0F;
    const float head = size * 0.3F;
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + (dir * head), y - head), colour, thickness);
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + (dir * head), y + head), colour, thickness);
    *advance = w + (size * kArrowTrailingGap);
}

void art_box(ImDrawList* dl, const Art& art, const maths::Rect& box, ArtLook look)
{
    const float x0 = box.x0;
    const float y0 = box.y0;
    const float x1 = box.x1;
    const float y1 = box.y1;
    const float alpha = look.alpha;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), get_col(kArtGround, alpha));
    if (art.texture == 0U) {
        return;
    }
    Uv uv = cover_uv({.width = art.width, .height = art.height}, {.width = x1 - x0, .height = y1 - y0}, look.pan);
    if (art.bottom_up_) {
        uv.v0 = 1.0F - uv.v0;
        uv.v1 = 1.0F - uv.v1;
    }
    dl->AddImage(static_cast<ImTextureID>(art.texture), ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(uv.u0, uv.v0),
                 ImVec2(uv.u1, uv.v1), get_col(kOpaqueWhite, alpha));
}

} // namespace internal
namespace {

// The bottom of the art darkened so the words over it read (not in the mock-ups, which have flat grey there).
void get_shade(ImDrawList* dl, const maths::Rect& box)
{
    const float mid = box.y0 + ((box.y1 - box.y0) * 0.35F);
    dl->AddRectFilledMultiColor(ImVec2(box.x0, mid), ImVec2(box.x1, box.y1), get_col(kGround, 0.0F),
                                get_col(kGround, 0.0F), get_col(kGround, kShadeAlpha), get_col(kGround, kShadeAlpha));
}

} // namespace
namespace internal {

// The list column's scrim (the user's change to the mock-ups, 2026-10-25: the art behind the whole screen, the column
// on a dark, softened copy of it rather than a flat panel): the art's column part drawn again a few times a little
// apart (averaged: a soft blur), darkened ~75 %, its right edge feathered over kScrimFeather. `art` may be empty (the
// pause menus: the game behind - only darkened).
void scrim(ImDrawList* dl, const TickCycle& frame, const Art& art, ArtLook look)
{
    const float alpha = look.alpha;
    if (alpha <= kVisibleThreshold) {
        return;
    }
    const float x1 = kColumn + kScrimFeather;
    if (art.texture != 0U) {
        Uv uv = cover_uv({.width = art.width, .height = art.height}, {.width = kDesignWidth, .height = kDesignHeight}, look.pan);
        if (art.bottom_up_) {
            uv.v0 = 1.0F - uv.v0;
            uv.v1 = 1.0F - uv.v1;
        }
        // (the column's share of the picture)
        const float share = x1 / 1280.0F;
        const float u1 = uv.u0 + ((uv.u1 - uv.u0) * share);
        const float u_edge = uv.u0 + ((uv.u1 - uv.u0) * (kColumn / 1280.0F));
        static constexpr auto kOffsets = std::to_array<std::array<float, 2>>(
            {{0, 0}, {9, 0}, {-9, 0}, {0, 9}, {0, -9}, {6, 6}, {-6, -6}, {6, -6}, {-6, 6}});
        dl->PushTextureID(static_cast<ImTextureID>(art.texture));
        dl->PushClipRect(ImVec2(frame.get_x(0.0F), frame.get_y(0.0F)),
                         ImVec2(frame.get_x(x1), frame.get_y(kDesignHeight)), true);
        for (int i = 0; i < kScrimCopyCount; ++i) {
            const float dx = frame.current_s(core::at(kOffsets, i).front());
            const float dy = frame.current_s(core::at(kOffsets, i).back());
            // (copy i at 1 / (i + 1): the result is the copies' average; across the feather it fades to the art)
            const ImU32 full = get_col(kOpaqueWhite, alpha / static_cast<float>(i + 1));
            const ImU32 none = get_col(kOpaqueWhite, 0.0F);
            const float ya = frame.get_y(0.0F) + dy;
            const float yb = frame.get_y(720.0F) + dy;
            const float xa = frame.get_x(0.0F) + dx;
            const float xm = frame.get_x(kColumn) + dx;
            const float xb = frame.get_x(x1) + dx;
            dl->PrimReserve(kScrimCopyIndices, kScrimCopyVertices);
            auto base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            dl->PrimWriteVtx(ImVec2(xa, ya), ImVec2(uv.u0, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(u_edge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(u_edge, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xa, yb), ImVec2(uv.u0, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(u_edge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xb, ya), ImVec2(u1, uv.v0), none);
            dl->PrimWriteVtx(ImVec2(xb, yb), ImVec2(u1, uv.v1), none);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(u_edge, uv.v1), full);
            for (int quad = 0; quad < 2; ++quad) {
                auto base_index = static_cast<ImDrawIdx>(base + (quad * 4));
                dl->PrimWriteIdx(base_index);
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 1));
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 2));
                dl->PrimWriteIdx(base_index);
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 2));
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 3));
            }
        }
        dl->PopClipRect();
        dl->PopTextureID();
    }
    const float dark = 0.8F * alpha;
    dl->AddRectFilled(ImVec2(frame.get_x(0.0F), frame.get_y(0.0F)),
                      ImVec2(frame.get_x(kColumn), frame.get_y(kDesignHeight)), get_col(kGround, dark));
    dl->AddRectFilledMultiColor(ImVec2(frame.get_x(kColumn), frame.get_y(0.0F)),
                                ImVec2(frame.get_x(x1), frame.get_y(kDesignHeight)), get_col(kGround, dark),
                                get_col(kGround, 0.0F), get_col(kGround, 0.0F), get_col(kGround, dark));
}

// The whole screen's art (full-bleed), the words' shade at the foot, the column's scrim.
void backdrop(ImDrawList* dl, const TickCycle& frame, const Art& art, float pan)
{
    art_box(dl, art,
            {.x0 = frame.get_x(0.0F),
             .y0 = frame.get_y(0.0F),
             .x1 = frame.get_x(kDesignWidth),
             .y1 = frame.get_y(kDesignHeight)},
            {.pan = pan, .alpha = 1.0F});
    get_shade(dl, {.x0 = frame.get_x(kRight - kShadeLeadIn),
                   .y0 = frame.get_y(0.0F),
                   .x1 = frame.get_x(kDesignWidth),
                   .y1 = frame.get_y(kDesignHeight)});
    scrim(dl, frame, art, {.pan = pan, .alpha = 1.0F});
}

std::string pad2(int value)
{
    std::array<char, kPad2BufferSize> text{};
    text::format_to(text, "%02d", value);
    return text.data();
}

} // namespace internal
} // namespace frontend::new_frontend::paint
