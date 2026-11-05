// NOT ORIGINAL - the new front end's screen parts (ours): the header, the rows, the big name, the kicker, the hints,
// the records row, the tabs and the buttons the screens are put together from. See new_frontend_draw.h.

#include "frontend/new_frontend_draw.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <array>
#include <vector>
#include "frontend/new_frontend_model.h"
#include "frontend/new_frontend_text.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/maths/vec2.h"
#include "base/maths/trig.h"
#include "frontend/new_frontend_draw_internal.h"

namespace frontend::new_frontend::paint {
// What the source file's anonymous namespace held that its pieces share (new_frontend_draw_internal.h): visible as it
// was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend::paint

namespace frontend::new_frontend::paint {
namespace {

// The back arrow and its hit box in the header, and the gap before the title after it.
constexpr float kBackHitPad = 6.0F;
constexpr float kBackLabelGap = 16.0F;

constexpr float kMainRowLineHeight = 0.92F;
constexpr float kMainRowPadTop = 2.0F;
constexpr float kMainRowNumberGap = 18.0F;
constexpr float kMainRowCutPad = 12.0F;
constexpr float kMainRowCutTail = 26.0F;
constexpr float kMainRowCutTop = 0.18F; // as a fraction of the line
constexpr float kMainRowCutBottom = 0.92F;
constexpr float kMainRowTopInset = 10.0F;
constexpr float kMainRowBottomInset = 18.0F;

constexpr float kLevelRowCutTail = 22.0F;
constexpr float kLevelRowCutTop = 0.16F;
constexpr float kLevelRowCutBottom = 0.94F;
constexpr float kLevelRowTopInset = 8.0F;
constexpr float kLevelRowBottomInset = 14.0F;

// The big name: the smallest it shrinks to for a word that is too wide, the clip box that is never the limit, and the
// half thickness of the cut line while it splits.
constexpr float kBigNameMinScale = 0.66F;
constexpr float kClipUnbounded = 1e6F;
constexpr float kSliceRuleHalfThickness = 2.0F;

constexpr std::size_t kClockBufferSize = 32;
constexpr int kSecondsPerHour = 3600;
constexpr int kSecondsPerMinute = 60;
constexpr float kSideTitleLift = 10.0F;

// Tabs: the gap between rows of tabs and between tabs, the underline under the selected one, the hit box's side padding.
constexpr float kTabRowGap = 4.0F;
constexpr float kTabUnderlineTop = 5.0F;
constexpr float kTabUnderlineBottom = 6.0F;
constexpr float kTabHitPad = 4.0F;
constexpr float kTabGap = 16.0F;

} // namespace
namespace internal {

Style mono12(uint32_t colour, bool tracked)
{
    Style style;
    style.face = Face::kMono;
    style.css = kNoteTextSize;
    style.tracking = tracked ? 1.0F : 0.0F;
    style.upper = true;
    style.colour = colour;
    return style;
}

// A header row whose top right is `top_right` (design; its left at 48): the texts, the rule under it. Returns the
// rule's y.
float get_header(Ctx& ctx, maths::Vec2 top_right, const char* left_text, bool back, const char* right_text)
{
    const float top = top_right.y;
    const float right = top_right.x;
    Style style = mono12(kMuted);
    const float line_h = 1.3F;
    const float base = datum(style, *ctx.f, ctx.f->get_y(top), line_h);
    float x = ctx.f->get_x(kColumnLeft);
    if (back) {
        const Style back_style = mono12(kMuted, false);
        float adv = 0.0F;
        get_arrow(ctx.dl, {.x = x, .y = base}, ctx.f->current_s(kNoteTextSize), true, get_col(kMuted), &adv);
        draw_text(ctx.dl, back_style, *ctx.f, {.x = x + adv, .y = base}, text_tools::kBack);
        const float w = adv + get_measure(back_style, *ctx.f, text_tools::kBack);
        ctx.get_hit(Hit::Kind::kBack, 0, kColumnLeft - kBackHitPad, top - kBackHitPad,
                    kColumnLeft + (w / ctx.f->scale()) + kBackHitPad, top + (kNoteTextSize * line_h) + kBackHitPad);
        x += w + ctx.f->current_s(kBackLabelGap);
        style.colour = kPaper;
    }
    draw_text(ctx.dl, style, *ctx.f, {.x = x, .y = base}, left_text);
    if (right_text && (*right_text != '\0')) {
        const Style right_style = mono12(kMuted);
        draw_text(ctx.dl, right_style, *ctx.f,
                  {.x = ctx.f->get_x(right) - get_measure(right_style, *ctx.f, right_text), .y = base}, right_text);
    }
    const float rule = top + (12.0F * line_h) + 12.0F;
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(kColumnLeft), ctx.f->get_y(rule)),
                          ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule) + std::max(1.0F, ctx.f->current_s(1.0F))),
                          get_col(kRule));
    return rule + 1.0F;
}

void get_row(Ctx& ctx, const RowStyle& rs, float top, const char* number, const char* label, RowLook look)
{
    const float amount = look.amount;
    const uint32_t cut_colour = look.cut;
    const bool dim = look.dim;
    ImDrawList* dl = ctx.dl;
    const int start = dl->VtxBuffer.Size;
    const float line_top = top + rs.row_pad_top_;
    const float line_h = rs.name.css * rs.line_height_;
    const float base = datum(rs.name, *ctx.f, ctx.f->get_y(line_top), rs.line_height_);
    const float width = get_measure(rs.name, *ctx.f, label);
    if (amount > kVisibleThreshold) {
        const float x0 = ctx.f->get_x(rs.cut_left_);
        float x1 = ctx.f->get_x(rs.text_x_) + width + ctx.f->current_s(rs.cut_right_pad_);
        x1 = x0 + ((x1 - x0) * amount); // (the cut snaps out from the left)
        get_cut(dl,
                {.x0 = x0,
                 .y0 = ctx.f->get_y(line_top + (line_h * rs.cut_top_)),
                 .x1 = x1,
                 .y1 = ctx.f->get_y(line_top + (line_h * rs.cut_bottom_))},
                {.top = ctx.f->current_s(rs.top_inset_), .bottom = ctx.f->current_s(rs.bottom_inset_)},
                get_col(cut_colour));
    }
    Style name = rs.name;
    name.colour = get_mix(dim ? kMuted2 : kPaper, kGround, amount);
    draw_text(dl, name, *ctx.f, {.x = ctx.f->get_x(rs.text_x_), .y = base}, label);
    const Style num = rs.number;
    draw_text(dl, num, *ctx.f, {.x = ctx.f->get_x(kColumnLeft), .y = base}, number);
    if (amount > kVisibleThreshold) {
        const float pivot_y = ctx.f->get_y(top + (rs.row_height_ * 0.5F));
        xform(dl, start, {.x = ctx.f->get_x(kColumnLeft), .y = pivot_y},
              -kRowTiltDegrees * maths::kPi / kDegreesPerHalfTurn * amount,
              {.x = ctx.f->current_s(kRowSlideDistance) * amount, .y = 0.0F});
    }
}

RowStyle main_row_style()
{
    RowStyle rs;
    rs.name.face = Face::kDisplay;
    rs.name.css = kMainType;
    rs.name.upper = true;
    rs.number = mono12(kMuted, false);
    rs.number.css = kLabelTextSize;
    rs.line_height_ = kMainRowLineHeight;
    rs.row_pad_top_ = kMainRowPadTop;
    rs.row_height_ = kMainRowHeight;
    rs.text_x_ = kColumnLeft + kRowNumberWidth + kMainRowNumberGap + kMainRowCutPad;
    rs.cut_left_ = kColumnLeft + kRowNumberWidth + kMainRowNumberGap - kRowCutLeadIn;
    rs.cut_right_pad_ = kMainRowCutPad + kMainRowCutTail;
    rs.cut_top_ = kMainRowCutTop;
    rs.cut_bottom_ = kMainRowCutBottom;
    rs.top_inset_ = kMainRowTopInset;
    rs.bottom_inset_ = kMainRowBottomInset;
    return rs;
}

RowStyle level_row_style()
{
    RowStyle rs;
    rs.name.face = Face::kDisplay;
    rs.name.css = kLevelType;
    rs.name.upper = true;
    rs.number = mono12(kMuted2, false);
    rs.line_height_ = kTabLineHeight;
    rs.row_pad_top_ = 0.0F;
    rs.row_height_ = kLevelRowHeight;
    rs.text_x_ = kColumnLeft + kRowNumberWidth + kLevelRowNumberGap + kLevelRowCutPad;
    rs.cut_left_ = kColumnLeft + kRowNumberWidth + kLevelRowNumberGap - kRowCutLeadIn;
    rs.cut_right_pad_ = kLevelRowCutPad + kLevelRowCutTail;
    rs.cut_top_ = kLevelRowCutTop;
    rs.cut_bottom_ = kLevelRowCutBottom;
    rs.top_inset_ = kLevelRowTopInset;
    rs.bottom_inset_ = kLevelRowBottomInset;
    return rs;
}

// The big name at the right's foot: lines of `name` (Anton `css`, line-height 0.85) ending at design y `bottom`, from
// x `left` to `right`. Returns the top of the block. With `slice` > 0 (the launch), the top half moves up and the
// bottom half down along a cut, fading.
float big_name(Ctx& ctx, const std::string& name, float css, Band band, float bottom, Slice cut)
{
    const float left = band.left;
    const float right = band.right;
    const float slice = cut.amount;
    const uint32_t slice_colour = cut.colour;
    Style style;
    style.face = Face::kDisplay;
    style.css = css;
    style.upper = true;
    std::vector<std::string> lines = get_wrap(style, *ctx.f, name, ctx.f->current_s(right - left));
    if (lines.size() > 3) {
        lines.resize(3);
    }
    // (a word wider than the space - CREATE-A-SKATER - makes the type smaller, to two thirds at most)
    float widest = 0.0F;
    for (const std::string& line : lines) {
        widest = std::max(widest, get_measure(style, *ctx.f, line.c_str()));
    }
    if (widest > ctx.f->current_s(right - left)) {
        css = std::max(css * kBigNameMinScale, css * ctx.f->current_s(right - left) / widest);
        style.css = css;
    }
    const float line_h = css * 0.85F;
    const float top = bottom - (line_h * static_cast<float>(lines.size()));
    const float mid = ctx.f->get_y(top + ((bottom - top) * 0.55F));
    for (int half = 0; half < (slice > 0.0F ? 2 : 1); ++half) {
        const float dy = slice > 0.0F ? (half == 0 ? -1.0F : 1.0F) * ctx.f->current_s(70.0F) * slice : 0.0F;
        style.alpha = 1.0F - slice;
        if (slice > 0.0F) {
            // (each half keeps its own part of the name: the cut moves with it, the gap between opens)
            if (half == 0) {
                ctx.dl->PushClipRect(ImVec2(0, 0), ImVec2(kClipUnbounded, mid + dy), true);
            } else {
                ctx.dl->PushClipRect(ImVec2(0, mid + dy), ImVec2(kClipUnbounded, kClipUnbounded), true);
            }
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            const float line_top = top + (line_h * static_cast<float>(i));
            const float base = datum(style, *ctx.f, ctx.f->get_y(line_top), 0.85F);
            draw_text(ctx.dl, style, *ctx.f, {.x = ctx.f->get_x(left), .y = base + dy}, core::at(lines, i).c_str());
        }
        if (slice > 0.0F) {
            ctx.dl->PopClipRect();
        }
    }
    if (slice > 0.0F) {
        const float w = ctx.f->current_s(right - left) * std::min(1.0F, slice * 2.0F);
        ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), mid - ctx.f->current_s(kSliceRuleHalfThickness)),
                              ImVec2(ctx.f->get_x(left) + w, mid + ctx.f->current_s(kSliceRuleHalfThickness)),
                              get_col(slice_colour, 1.0F - slice));
    }
    return top;
}

// A kicker line (mono 12, tracked, upper case) from design x `at.x`, its box ending at design y `at.y`. Returns its top.
float get_kicker(Ctx& ctx, const std::string& text, maths::Vec2 at, Ink ink)
{
    const float left = at.x;
    const float bottom = at.y;
    Style style = mono12(ink.colour);
    style.alpha = ink.alpha;
    const float top = bottom - (12.0F * 1.3F);
    draw_text(ctx.dl, style, *ctx.f,
              {.x = ctx.f->get_x(left), .y = datum(style, *ctx.f, ctx.f->get_y(top), kTextLineHeight)}, text.c_str());
    return top;
}

// The hints row (a rule over it), from `left` to `right`, its text's box ending at `bottom`: spread as CSS's
// justify-content: space-between. Returns the rule's y.
float get_hints(Ctx& ctx, const std::vector<std::string>& hints, Band band, float bottom,
                std::vector<std::pair<float, float>>* spans)
{
    const float left = band.left;
    const float right = band.right;
    const Style style = mono12(kMuted, false);
    const float top = bottom - (12.0F * 1.3F);
    const float base = datum(style, *ctx.f, ctx.f->get_y(top), 1.3F);
    float total = 0.0F;
    std::vector<float> widths;
    for (const std::string& hint : hints) {
        widths.push_back(get_measure(style, *ctx.f, hint.c_str()));
        total += widths.back();
    }
    const float gap =
        hints.size() > 1 ? (ctx.f->current_s(right - left) - total) / static_cast<float>(hints.size() - 1) : 0.0F;
    float x = ctx.f->get_x(left);
    for (size_t i = 0; i < hints.size(); ++i) {
        draw_text(ctx.dl, style, *ctx.f, {.x = x, .y = base}, core::at(hints, i).c_str());
        if (spans) { // (design x: each hint's left and right)
            spans->emplace_back(ctx.f->design_x(x), ctx.f->design_x(x + core::at(widths, i)));
        }
        x += core::at(widths, i) + gap;
    }
    const float rule = top - 10.0F - 1.0F;
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule)),
                          ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule) + std::max(1.0F, ctx.f->current_s(1.0F))),
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
    const std::string image = data ? data->menu_art(id) : std::string();
    return image.empty() ? default_art(id) : image;
}

// The pause screens' ground: the game dimmed, the left column nearly opaque. `dim`: how much (an editor's view less).
void pause_ground(Ctx& ctx, float window_width, float window_height, float dim)
{
    ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround, dim));
    scrim(ctx.dl, *ctx.f, Art{}, {.pan = 0.0F, .alpha = 1.0F});
}

} // namespace internal
namespace {

// A mono line of records spread in three columns across `band`, its box ending at `bottom` (a rule above).
// Returns the rule's y (or `bottom` when there is nothing).
float records_row(Ctx& ctx, const std::vector<std::string>& cols, Band band, float bottom)
{
    if (cols.empty()) {
        return bottom;
    }
    const float left = band.left;
    const float right = band.right;
    const float alpha = 1.0F;
    Style style = mono12(kMuted, false);
    style.alpha = alpha;
    const float top = bottom - (12.0F * 1.3F);
    const float col_w = (right - left) / 3.0F;
    for (size_t i = 0; i < cols.size() && i < 3; ++i) {
        draw_text(ctx.dl, style, *ctx.f,
                  {.x = ctx.f->get_x(left + (col_w * static_cast<float>(i))),
                   .y = datum(style, *ctx.f, ctx.f->get_y(top), kTextLineHeight)},
                  core::at(cols, i).c_str());
    }
    const float rule_y = top - 10.0F - 1.0F;
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule_y)),
                          ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule_y) + std::max(1.0F, ctx.f->current_s(1.0F))),
                          get_col(kRule, alpha));
    return rule_y;
}

std::string get_clock(float seconds)
{
    const int total = static_cast<int>(seconds);
    std::array<char, kClockBufferSize> text{};
    if (total >= kSecondsPerHour) {
        text::format_to(text, "%d:%02d:%02d", total / kSecondsPerHour, total / kSecondsPerMinute % kSecondsPerMinute, total % kSecondsPerMinute);
    } else {
        text::format_to(text, "%02d:%02d", total / kSecondsPerMinute, total % kSecondsPerMinute);
    }
    return text.data();
}

std::string thousands(int value)
{
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += ',';
        }
        out += core::at(digits, i);
    }
    return value < 0 ? "-" + out : out;
}

} // namespace
namespace internal {

// The pause menus' right side: the session so far (what is known) over its rule from `bottom`, and its titles (the
// house yellow unless the level has a colour).
SideTitle pause_side(Ctx& ctx, Band band, float bottom)
{
    std::vector<std::string> cols;
    const Session& session = ctx.extras->session;
    if (session.best_combo_ >= 0) {
        cols.push_back(std::string(text_tools::kLabelBestChain) + " " + thousands(session.best_combo_));
    }
    if (session.gaps >= 0) {
        cols.push_back(std::string(text_tools::kGaps) + " " + std::to_string(session.gaps) +
                       (session.gaps_total_ > 0 ? "/" + std::to_string(session.gaps_total_) : ""));
    }
    if (session.seconds >= 0.0F) {
        cols.push_back(std::string(text_tools::kTime) + " " + get_clock(session.seconds));
    }
    SideTitle title{
        .top = records_row(ctx, cols, band, bottom) - kSideTitleLift,
        .kicker = std::string(text_tools::kTitlePaused) +
                  (ctx.extras->level_kicker_.empty() ? std::string() : " \xE2\x80\x94 " + ctx.extras->level_kicker_),
        .name = ctx.extras->level_name_.empty() ? std::string(text_tools::kTitlePaused) : ctx.extras->level_name_,
        .colour = (ctx.extras->level_colour_ != 0U) ? ctx.extras->level_colour_ : Menu::kHouseYellow};
    return title;
}

// Mono tabs (the level select's game tabs, the options' groups) from x 48 to 520 at design y `y`, wrapping: the
// selected one in its colour, underlined. Returns the bottom of the last row.
float get_tabs(Ctx& ctx, TabRow row, float y)
{
    const std::span<const std::pair<std::string, uint32_t>> tabs = row.tabs;
    const int selected = row.selected;
    Style tab = mono12(kMuted, false);
    tab.css = kLabelTextSize;
    float x = kColumnLeft;
    const float tab_h = (13.0F * 1.3F) + 8.0F;
    for (int i = 0; std::cmp_less(i, tabs.size()); ++i) {
        const float w = get_measure(tab, *ctx.f, core::at(tabs, i).first.c_str()) / ctx.f->scale();
        if (x > kColumnLeft && x + w > kColumnInner) {
            x = kColumnLeft;
            y += tab_h + kTabRowGap;
        }
        const bool on = i == selected;
        tab.colour = on ? core::at(tabs, i).second : kMuted;
        const float base = datum(tab, *ctx.f, ctx.f->get_y(y + 4.0F), 1.3F);
        draw_text(ctx.dl, tab, *ctx.f, {.x = ctx.f->get_x(x), .y = base}, core::at(tabs, i).first.c_str());
        if (on) {
            ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(x), base + ctx.f->current_s(kTabUnderlineTop)),
                                  ImVec2(ctx.f->get_x(x + w), base + ctx.f->current_s(kTabUnderlineBottom)),
                                  get_col(core::at(tabs, i).second));
        }
        ctx.get_hit(Hit::Kind::kTab, i, x - kTabHitPad, y, x + w + kTabHitPad, y + tab_h);
        x += w + kTabGap;
    }
    return y + tab_h;
}

// A slanted button (the level select's Skate) from design x `at.x`, its text in Anton 30 ending at design y `at.y`.
void get_button(Ctx& ctx, maths::Vec2 at, const char* text, uint32_t colour, bool arrow, Hit::Kind kind, float alpha)
{
    const float left = at.x;
    const float bottom = at.y;
    Style style;
    style.face = Face::kDisplay;
    style.css = kHeadingTextSize;
    style.upper = true;
    style.colour = kGround;
    style.alpha = alpha;
    const float tw = get_measure(style, *ctx.f, text) / ctx.f->scale();
    const float arrow_w = arrow ? (30.0F * 0.9F) + 10.0F : 0.0F;
    const float bh = (30.0F * 1.15F) + 12.0F;
    const float bx0 = left;
    const float bx1 = left + 22.0F + tw + arrow_w + 22.0F;
    const float by0 = bottom - bh;
    const float by1 = bottom;
    get_cut(ctx.dl,
            {.x0 = ctx.f->get_x(bx0), .y0 = ctx.f->get_y(by0), .x1 = ctx.f->get_x(bx1), .y1 = ctx.f->get_y(by1)},
            {.top = ctx.f->current_s(kButtonCutInset), .bottom = ctx.f->current_s(kButtonCutInset)},
            get_col(colour, alpha));
    const float sb = datum(style, *ctx.f, ctx.f->get_y(by0 + 6.0F), 1.15F);
    draw_text(ctx.dl, style, *ctx.f, {.x = ctx.f->get_x(bx0 + kButtonPadding), .y = sb}, text);
    if (arrow) {
        float adv = 0.0F;
        get_arrow(ctx.dl, {.x = ctx.f->get_x(bx0 + kButtonPadding + tw + kButtonArrowGap), .y = sb},
                  ctx.f->current_s(kHeadingTextSize), false, get_col(kGround, alpha), &adv);
    }
    ctx.get_hit(kind, 0, bx0, by0, bx1, by1);
}

} // namespace internal
} // namespace frontend::new_frontend::paint
