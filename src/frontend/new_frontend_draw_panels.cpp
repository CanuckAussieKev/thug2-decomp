// NOT ORIGINAL - the new front end's dialog, skater-select and stats screens (ours). See new_frontend_draw.h.

#include "frontend/new_frontend_draw.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "frontend/new_frontend_text.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/core/ints.h"
#include "base/core/checked_index.h"
#include "base/maths/trig.h"
#include "frontend/new_frontend_draw_internal.h"

namespace frontend::new_frontend::paint {
// What the source file's anonymous namespace held that its pieces share (new_frontend_draw_internal.h): visible as it
// was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend::paint

namespace frontend::new_frontend::paint {
namespace {

constexpr float kCapBlockAlpha = 0.3F; // a stat block up to the cap, faint

constexpr float kColumnSpan = 1184.0F;  // its width (kColumnRight - kColumnLeft)

constexpr float kStatTextSize = 16.0F;
constexpr float kStripCenterX = kDesignWidth / 2;          // the centre of the design canvas (the skater strip centres the selection on it)

// The dialog: the dimmed screen, its band and the yellow edge, the title and the text under it, the hints' lift.
constexpr float kParagraphGap = 6.0F;
constexpr float kDialogDim = 0.62F;
constexpr float kDialogBandAlpha = 0.94F;
constexpr float kDialogEdgeHeight = 4.0F;
constexpr float kDialogTitleDrop = 56.0F;
constexpr float kDialogTitleLineHeight = 0.9F;
constexpr float kDialogTextGap = 14.0F;
constexpr int kDialogTextLines = 6;
constexpr float kDialogHintsLift = 18.0F;
constexpr float kHeaderTop = 36.0F;

// The skater select: the shades (top, panel, foot), the strip's clip, its cuts and hit boxes, the stat blocks.
constexpr float kSkaterTopShadeHeight = 170.0F;
constexpr float kSkaterTopShadeAlpha = 0.92F;
constexpr float kSkaterPanelFeather = 90.0F;
constexpr float kSkaterPanelAlpha = 0.86F;
constexpr float kSkaterFootShadeTop = 600.0F;
constexpr float kSkaterFootShadeAlpha = 0.9F;
constexpr float kStripClipAbove = 20.0F;
constexpr float kStripClipBelow = 60.0F;
constexpr float kStripCutLeadIn = 10.0F;
constexpr float kStripCutTop = 0.12F;
constexpr float kStripCutExtra = 26.0F;
constexpr float kStripCutBottom = 0.98F;
constexpr float kStripCutTopInset = 7.0F;
constexpr float kStripCutBottomInset = 10.0F;
constexpr float kStripTiltLift = 4.0F;
constexpr float kStripHitLeft = 10.0F;
constexpr float kStripHitRight = 16.0F;
constexpr float kStripHitVertical = 6.0F;
constexpr float kSkaterHintsLift = 22.0F;
constexpr int kStatBlockCount = 10;
constexpr float kStatBlockTop = 7.0F;
constexpr float kStatBlockGap = 4.0F;
constexpr float kStatBlockBottom = 19.0F;
constexpr float kStatBlockInset = 3.0F;

// The stats screen: the panel and its feather, the selected row's band and cut, the blocks, the hit padding, the
// points left and the text under the name.
constexpr float kStatsPanelAlpha = 0.88F;
constexpr float kStatsPanelFeather = 80.0F;
constexpr float kStatRowBandInset = 2.0F;
constexpr float kStatRowRightPad = 12.0F;
constexpr float kStatRowBandAlpha = 0.06F;
constexpr float kStatCutLeadIn = 12.0F;
constexpr float kStatCutTop = 0.18F;
constexpr float kStatCutExtra = 28.0F;
constexpr float kStatCutBottom = 0.82F;
constexpr float kStatCutInset = 6.0F;
constexpr int kStatsBlockCount = 10;
constexpr float kStatsBlockGap = 5.0F;
constexpr float kStatsBlockHeight = 14.0F;
constexpr float kStatsBlockInset = 3.0F;
constexpr float kStatBarHitPad = 6.0F;
constexpr float kStatsHintsGap = 16.0F;
constexpr float kPointsLineHeight = 0.95F;
constexpr float kPointsKickerGap = 4.0F;
constexpr float kPointsGap = 20.0F;
constexpr float kStatsTextTop = 96.0F;
constexpr int kStatsTextLines = 12;

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

Style get_body(uint32_t colour = kMuted)
{
    Style style;
    style.face = Face::kBody;
    style.css = kBodyTextSize;
    style.colour = colour;
    return style;
}

} // namespace
namespace internal {

// Paragraphs of body text in `block` across `band`. Returns the bottom.
float paragraphs(Ctx& ctx, const std::vector<std::string>& texts, Band band, TextBlock block)
{
    const float left = band.left;
    const float right = band.right;
    float top = block.top;
    const int max_lines = block.lines;
    const Style body = get_body(block.colour);
    const float line_h = 14.0F * 1.5F;
    int drawn = 0;
    for (const std::string& text : texts) {
        // (each \n of a dialog's text a line of its own)
        size_t at = 0;
        while (at <= text.size() && drawn < max_lines) {
            const size_t end = text.find('\n', at);
            const std::string part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            for (const std::string& line : get_wrap(body, *ctx.f, part, ctx.f->current_s(right - left))) {
                if (drawn >= max_lines) {
                    break;
                }
                draw_text(ctx.dl, body, *ctx.f,
                          {.x = ctx.f->get_x(left), .y = datum(body, *ctx.f, ctx.f->get_y(top), kBodyLineHeight)},
                          line.c_str());
                top += line_h;
                ++drawn;
            }
            if (end == std::string::npos) {
                break;
            }
            at = end + 1;
        }
        top += kParagraphGap;
    }
    return top;
}

// A dialog box: the screen dimmed, a band across it - the kicker and the title (Anton) with its text on the left,
// the choices down the right (the level list's rows, the kinetic selection).
void dialog_screen(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height)
{
    const mirror::View& v = tracker.get_shown();
    ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround, kDialogDim));
    const float y0 = 196.0F;
    const float y1 = 548.0F;
    ctx.dl->AddRectFilled(ImVec2(0.0F, ctx.f->get_y(y0)), ImVec2(window_width, ctx.f->get_y(y1)),
                          get_col(kGround, kDialogBandAlpha));
    ctx.dl->AddRectFilled(ImVec2(0.0F, ctx.f->get_y(y0)), ImVec2(window_width, ctx.f->get_y(y0 + kDialogEdgeHeight)),
                          get_col(Menu::kHouseYellow));
    const float left = 96.0F;
    const float text_right = 700.0F;
    int choices = 0;
    for (const mirror::Row& row : v.rows) {
        choices += row.focusable ? 1 : 0;
    }
    const char* kicker = text_tools::kPleaseWait; // (no choices: a wait; one: a notice; more: a question)
    if (choices >= 2) {
        kicker = text_tools::kDialogConfirm;
    } else if (choices == 1) {
        kicker = text_tools::kDialogNotice;
    }
    const float kicker_top = get_kicker(ctx, kicker, {.x = left, .y = y0 + 44.0F}, {.colour = Menu::kHouseYellow});
    (void)kicker_top;
    // The title (Anton 72, up to two lines) under the kicker.
    Style title;
    title.face = Face::kDisplay;
    title.css = kTitleTextSize;
    title.upper = true;
    std::vector<std::string> title_lines = get_wrap(title, *ctx.f, v.title, ctx.f->current_s(text_right - left));
    if (title_lines.size() > 2) {
        title_lines.resize(2);
    }
    float y = y0 + kDialogTitleDrop;
    for (const std::string& line : title_lines) {
        draw_text(ctx.dl, title, *ctx.f,
                  {.x = ctx.f->get_x(left), .y = datum(title, *ctx.f, ctx.f->get_y(y), kDialogTitleLineHeight)},
                  line.c_str());
        y += kTitleTextSize * kDialogTitleLineHeight;
    }
    paragraphs(ctx, v.lines, {.left = left, .right = text_right},
               {.top = y + kDialogTextGap, .lines = kDialogTextLines, .colour = kPaper});
    // The choices.
    RowStyle rs = level_row_style();
    const float rows_left = 760.0F;
    rs.text_x_ = rows_left + kRowNumberWidth + kLevelRowNumberGap + kLevelRowCutPad;
    rs.cut_left_ = rows_left + kRowNumberWidth + kLevelRowNumberGap - kRowCutLeadIn;
    const int row_count = static_cast<int>(v.rows.size());
    const float top = ((y0 + y1) * 0.5F) - (static_cast<float>(row_count) * kLevelRowHeight * 0.5F);
    for (int i = 0; i < row_count; ++i) {
        const float row_top = top + (static_cast<float>(i) * kLevelRowHeight);
        // (Row puts the number at the design's x 48: drawn here on the right instead)
        ImDrawList* dl = ctx.dl;
        const int start = dl->VtxBuffer.Size;
        const float amount = tracker.row_amount(i);
        const float base = datum(rs.name, *ctx.f, ctx.f->get_y(row_top), rs.line_height_);
        const float width = get_measure(rs.name, *ctx.f, core::at(v.rows, i).label.c_str());
        if (amount > kVisibleThreshold) {
            const float x0 = ctx.f->get_x(rs.cut_left_);
            float x1 = ctx.f->get_x(rs.text_x_) + width + ctx.f->current_s(rs.cut_right_pad_);
            x1 = x0 + ((x1 - x0) * amount);
            const float line_h = rs.name.css * rs.line_height_;
            get_cut(dl,
                    {.x0 = x0,
                     .y0 = ctx.f->get_y(row_top + (line_h * rs.cut_top_)),
                     .x1 = x1,
                     .y1 = ctx.f->get_y(row_top + (line_h * rs.cut_bottom_))},
                    {.top = ctx.f->current_s(rs.top_inset_), .bottom = ctx.f->current_s(rs.bottom_inset_)},
                    get_col(Menu::kHouseYellow));
        }
        Style name = rs.name;
        name.colour = get_mix(core::at(v.rows, i).focusable ? kPaper : kMuted2, kGround, amount);
        draw_text(dl, name, *ctx.f, {.x = ctx.f->get_x(rs.text_x_), .y = base}, core::at(v.rows, i).label.c_str());
        draw_text(dl, rs.number, *ctx.f, {.x = ctx.f->get_x(rows_left), .y = base}, pad2(i + 1).c_str());
        if (amount > kVisibleThreshold) {
            xform(dl, start, {.x = ctx.f->get_x(rows_left), .y = ctx.f->get_y(row_top + (kLevelRowHeight * kHalf))},
                  -kRowTiltDegrees * maths::kPi / kDegreesPerHalfTurn * amount,
                  {.x = ctx.f->current_s(kRowSlideDistance) * amount, .y = 0.0F});
        }
        if (core::at(v.rows, i).focusable) {
            ctx.get_hit(Hit::Kind::kItem, i, rows_left, row_top, kRowRight, row_top + kLevelRowHeight);
        }
    }
    std::vector<std::string> hints = {text_tools::kHintSelect, text_tools::kHintBack};
    if (choices == 0) {
        hints = {text_tools::kPleaseWait};
    }
    get_hints(ctx, hints, {.left = left, .right = kColumnSpan}, y1 - kDialogHintsLift);
}

// The skater select: the game's 3D skater stays in view (left of centre, as the original's camera puts it); the
// skaters as a strip across the top, the selected one's name and stats on a panel on the right.
void skaters_screen(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height)
{
    const mirror::View& v = tracker.get_shown();
    // The top: a shade for the strip and the header.
    ctx.dl->AddRectFilledMultiColor(ImVec2(0.0F, 0.0F), ImVec2(window_width, ctx.f->get_y(kSkaterTopShadeHeight)),
                                    get_col(kGround, kSkaterTopShadeAlpha), get_col(kGround, kSkaterTopShadeAlpha),
                                    get_col(kGround, 0.0F), get_col(kGround, 0.0F));
    // The right: a panel feathered from x 720.
    const float panel = 800.0F;
    ctx.dl->AddRectFilledMultiColor(ImVec2(ctx.f->get_x(panel - kSkaterPanelFeather), 0.0F),
                                    ImVec2(ctx.f->get_x(panel), window_height), get_col(kGround, 0.0F),
                                    get_col(kGround, kSkaterPanelAlpha), get_col(kGround, kSkaterPanelAlpha),
                                    get_col(kGround, 0.0F));
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(panel), 0.0F), ImVec2(window_width, window_height),
                          get_col(kGround, kSkaterPanelAlpha));
    // The foot: a shade under the hints.
    ctx.dl->AddRectFilledMultiColor(ImVec2(0.0F, ctx.f->get_y(kSkaterFootShadeTop)),
                                    ImVec2(window_width, window_height), get_col(kGround, 0.0F), get_col(kGround, 0.0F),
                                    get_col(kGround, kSkaterFootShadeAlpha), get_col(kGround, kSkaterFootShadeAlpha));

    const std::string count =
        v.selected >= 0 ? pad2(v.selected + 1) + " / " + pad2(static_cast<int>(v.rows.size())) : std::string();
    get_header(ctx, {.x = kColumnRight, .y = kHeaderTop}, v.title.c_str(), true, count.c_str());

    // The strip: every skater's name (Anton 28), the selected one centred, cut and turned.
    Style name;
    name.face = Face::kDisplay;
    name.css = kNameTextSize;
    name.upper = true;
    const float gap = 34.0F;
    std::vector<float> widths;
    float total = 0.0F;
    float selected_centre = 0.0F;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        widths.push_back(get_measure(name, *ctx.f, core::at(v.rows, i).label.c_str()) / ctx.f->scale());
        if (ints::as_signed(i) == v.selected) {
            selected_centre = total + (widths.back() * kHalf);
        }
        total += widths.back() + gap;
    }
    float shift = kStripCenterX - selected_centre; // (the strip's x 0 in design px)
    shift = std::min(shift, kColumnLeft);
    shift = std::max(shift, kColumnRight - (total - gap));
    if (total - gap < kColumnSpan) {
        shift = kColumnLeft;
    }
    const float strip_top = 88.0F;
    ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(strip_top - kStripClipAbove)),
                         ImVec2(ctx.f->get_x(kDesignWidth), ctx.f->get_y(strip_top + kStripClipBelow)), true);
    float x = shift;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        const float amount = tracker.row_amount(static_cast<int>(i));
        ImDrawList* dl = ctx.dl;
        const int start = dl->VtxBuffer.Size;
        const float line_h = 28.0F * 1.05F;
        if (amount > kVisibleThreshold) {
            get_cut(dl,
                    {.x0 = ctx.f->get_x(x - kStripCutLeadIn),
                     .y0 = ctx.f->get_y(strip_top + (line_h * kStripCutTop)),
                     .x1 = ctx.f->get_x(x - kStripCutLeadIn) +
                           ((ctx.f->current_s(core::at(widths, i) + kStripCutExtra)) * amount),
                     .y1 = ctx.f->get_y(strip_top + (line_h * kStripCutBottom))},
                    {.top = ctx.f->current_s(kStripCutTopInset), .bottom = ctx.f->current_s(kStripCutBottomInset)},
                    get_col(Menu::kHouseYellow));
        }
        Style style = name;
        style.colour = get_mix(core::at(v.rows, i).focusable ? kPaper : kMuted2, kGround, amount);
        draw_text(dl, style, *ctx.f,
                  {.x = ctx.f->get_x(x), .y = datum(style, *ctx.f, ctx.f->get_y(strip_top), kTabLineHeight)},
                  core::at(v.rows, i).label.c_str());
        if (amount > kVisibleThreshold) {
            xform(dl, start, {.x = ctx.f->get_x(x), .y = ctx.f->get_y(strip_top + (line_h * kHalf))},
                  -kRowTiltDegrees * maths::kPi / kDegreesPerHalfTurn * amount,
                  {.x = 0.0F, .y = -ctx.f->current_s(kStripTiltLift) * amount});
        }
        if (core::at(v.rows, i).focusable && x + core::at(widths, i) > 0.0F && x < kDesignWidth) {
            ctx.get_hit(Hit::Kind::kItem, static_cast<int>(i), x - kStripHitLeft, strip_top - kStripHitVertical,
                        x + core::at(widths, i) + kStripHitRight, strip_top + line_h + kStripHitVertical);
        }
        x += core::at(widths, i) + gap;
    }
    ctx.dl->PopClipRect();
    // (a rule under the strip)
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(kColumnLeft), ctx.f->get_y(strip_top + kColumnLeft)),
                          ImVec2(ctx.f->get_x(kColumnRight),
                                 ctx.f->get_y(strip_top + kColumnLeft) + std::max(1.0F, ctx.f->current_s(1.0F))),
                          get_col(kRule));

    // The panel: the stats as a mono table with bars, the name over it, the kicker.
    const float left = panel + 40.0F;
    const float right = 1232.0F;
    float bottom =
        get_hints(
            ctx, {text_tools::kHintSkater, text_tools::kHintAccept, text_tools::kHintBack, text_tools::kHintLoadSkater},
            {.left = kColumnLeft, .right = kColumnRight}, kPanelHintsBottom) -
        kSkaterHintsLift;
    Style label = mono12(kMuted, false);
    label.css = kLabelTextSize;
    Style number = mono12(kPaper, false);
    number.css = kLabelTextSize;
    const float row_h = 26.0F;
    const int stats = static_cast<int>(v.stats.size());
    const float table_top = bottom - (row_h * static_cast<float>(stats));
    for (int i = 0; i < stats; ++i) {
        const float top = table_top + (row_h * static_cast<float>(i));
        const float base = datum(label, *ctx.f, ctx.f->get_y(top + 4.0F), 1.3F);
        draw_text(ctx.dl, label, *ctx.f, {.x = ctx.f->get_x(left), .y = base}, core::at(v.stats, i).first.c_str());
        const int value = std::clamp(core::at(v.stats, i).second, 0, 10);
        const float bar_left = left + 110.0F;
        const float bar_right = right - 40.0F;
        const float seg = (bar_right - bar_left) / 10.0F;
        for (int k = 0; k < kStatBlockCount; ++k) {
            const float sx = bar_left + (seg * static_cast<float>(k));
            get_cut(ctx.dl,
                    {.x0 = ctx.f->get_x(sx),
                     .y0 = ctx.f->get_y(top + kStatBlockTop),
                     .x1 = ctx.f->get_x(sx + seg - kStatBlockGap),
                     .y1 = ctx.f->get_y(top + kStatBlockBottom)},
                    {.top = ctx.f->current_s(kStatBlockInset), .bottom = ctx.f->current_s(kStatBlockInset)},
                    get_col(k < value ? Menu::kHouseYellow : kRule));
        }
        const std::string text = std::to_string(core::at(v.stats, i).second);
        draw_text(ctx.dl, number, *ctx.f,
                  {.x = ctx.f->get_x(right) - get_measure(number, *ctx.f, text.c_str()), .y = base}, text.c_str());
    }
    if (stats > 0) {
        const float rule = table_top - 12.0F;
        ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule)),
                              ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule) + std::max(1.0F, ctx.f->current_s(1.0F))),
                              get_col(kRule));
        bottom = rule - kPanelStackGap;
    }
    const float name_top = big_name(ctx, v.big_name_, 84.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    std::string kicker = text_tools::kSkater;
    if (v.selected >= 0 && !core::at(v.rows, v.selected).focusable) {
        kicker += std::string(" \xC2\xB7 ") + text_tools::kLocked;
    }
    get_kicker(ctx, kicker, {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow});
}

// The stats screen (EDIT STATS, the story's STATS): the stats as the skater select's table - each stat's name in mono
// caps, ten slanted blocks filled to its value (the story's earned but unspent ones faint), the number - on a panel on
// the left, the selected stat marked; the points left and the selected stat big on the right; the game behind, dimmed,
// in a level. The game's scripts change the stats (Left / Right reach them); this only shows what its stars show.
// Hits: Item for a row (its index), Choice for a row's blocks - row * 16 + 0 under its value (Left), + 1 past it (Right).
void stats_screen(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height)
{
    const mirror::View& v = tracker.get_shown();
    if (v.over_game_) {
        ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height),
                              get_col(kGround, kPauseGroundDim));
    } else {
        ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
    }
    // The panel: x 0 .. 720, feathered to 800.
    const float panel = 720.0F;
    ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(ctx.f->get_x(panel), window_height),
                          get_col(kGround, kStatsPanelAlpha));
    ctx.dl->AddRectFilledMultiColor(ImVec2(ctx.f->get_x(panel), 0.0F),
                                    ImVec2(ctx.f->get_x(panel + kStatsPanelFeather), window_height),
                                    get_col(kGround, kStatsPanelAlpha), get_col(kGround, 0.0F), get_col(kGround, 0.0F),
                                    get_col(kGround, kStatsPanelAlpha));
    const float rule = get_header(ctx, {.x = panel - 48.0F, .y = 40.0F}, v.title.c_str(), true, nullptr);

    // The table.
    const int row_count = static_cast<int>(v.rows.size());
    const float row_h = std::min(44.0F, (600.0F - rule - 40.0F) / static_cast<float>(std::max(row_count, 1)));
    const float table_top = rule + 36.0F;
    const float label_x = 72.0F;
    const float bar_left = 250.0F;
    const float bar_right = 590.0F;
    const float number_right = panel - 48.0F;
    const float seg = (bar_right - bar_left) / 10.0F;
    Style label = mono12(kMuted, true);
    label.css = kStatTextSize;
    Style number = mono12(kPaper, false);
    number.css = kStatTextSize;
    for (int i = 0; i < row_count; ++i) {
        const mirror::Row& row = core::at(v.rows, i);
        const float top = table_top + (row_h * static_cast<float>(i));
        const bool selected = i == v.selected;
        const float amount = tracker.row_amount(i);
        const float base = datum(label, *ctx.f, ctx.f->get_y(top + ((row_h - (16.0F * 1.3F)) * 0.5F)), 1.3F);
        if (amount > kVisibleThreshold) {
            // (the selected stat: a slanted yellow cut behind its name, a faint band across its row)
            ctx.dl->AddRectFilled(
                ImVec2(ctx.f->get_x(kColumnLeft), ctx.f->get_y(top + kStatRowBandInset)),
                ImVec2(ctx.f->get_x(number_right + kStatRowRightPad), ctx.f->get_y(top + row_h - kStatRowBandInset)),
                get_col(kPaper, kStatRowBandAlpha * amount));
            const float w = get_measure(label, *ctx.f, row.label.c_str()) / ctx.f->scale();
            get_cut(ctx.dl,
                    {.x0 = ctx.f->get_x(label_x - kStatCutLeadIn),
                     .y0 = ctx.f->get_y(top + (row_h * kStatCutTop)),
                     .x1 = ctx.f->get_x(label_x - kStatCutLeadIn) + (ctx.f->current_s(w + kStatCutExtra) * amount),
                     .y1 = ctx.f->get_y(top + (row_h * kStatCutBottom))},
                    {.top = ctx.f->current_s(kStatCutInset), .bottom = ctx.f->current_s(kStatCutInset)},
                    get_col(Menu::kHouseYellow));
        }
        Style row_label = label;
        row_label.colour = get_mix(row.focusable ? kMuted : kMuted2, kGround, amount);
        draw_text(ctx.dl, row_label, *ctx.f, {.x = ctx.f->get_x(label_x), .y = base}, row.label.c_str());
        const int value = std::clamp(row.stat_value_, 0, 10);
        const int cap = std::clamp(row.stat_cap_, value, 10);
        const float block_top = top + (row_h * 0.5F) - 7.0F;
        for (int k = 0; k < kStatsBlockCount; ++k) {
            const float sx = bar_left + (seg * static_cast<float>(k));
            ImU32 colour = 0; // (the value's blocks yellow, up to the cap faint yellow, the rest the rule's)
            if (k < value) {
                colour = get_col(Menu::kHouseYellow);
            } else if (k < cap) {
                colour = get_col(Menu::kHouseYellow, kCapBlockAlpha);
            } else {
                colour = get_col(kRule);
            }
            get_cut(ctx.dl,
                    {.x0 = ctx.f->get_x(sx),
                     .y0 = ctx.f->get_y(block_top),
                     .x1 = ctx.f->get_x(sx + seg - kStatsBlockGap),
                     .y1 = ctx.f->get_y(block_top + kStatsBlockHeight)},
                    {.top = ctx.f->current_s(kStatsBlockInset), .bottom = ctx.f->current_s(kStatsBlockInset)}, colour);
        }
        const std::string text = std::to_string(value);
        Style num = number;
        num.colour = selected ? Menu::kHouseYellow : kPaper;
        draw_text(ctx.dl, num, *ctx.f,
                  {.x = ctx.f->get_x(number_right) - get_measure(num, *ctx.f, text.c_str()), .y = base}, text.c_str());
        // (the blocks: under the value Left, past it Right; the row: select / choose)
        ctx.get_hit(Hit::Kind::kChoice, i * kHitChoiceStride, bar_left - kStatBarHitPad, top,
                    bar_left + (seg * static_cast<float>(value)), top + row_h);
        ctx.get_hit(Hit::Kind::kChoice, (i * kHitChoiceStride) + 1, bar_left + (seg * static_cast<float>(value)), top,
                    bar_right + kStatBarHitPad, top + row_h);
        ctx.get_hit(Hit::Kind::kItem, i, kColumnLeft, top, number_right + kStatRowRightPad, top + row_h);
    }

    // The right: the hints, the screen's other words (the description, the story's goals), the points left, the stat.
    const float left = 800.0F;
    const float right = 1232.0F;
    std::vector<std::string> hints = {text_tools::kHintStat};
    if (v.stat_columns_ > 1 && !v.stats_editing_) {
        hints.emplace_back(text_tools::kHintColumn);
        hints.emplace_back(text_tools::kHintEditStat);
        hints.emplace_back(text_tools::kHintBack);
    } else {
        hints.emplace_back(text_tools::kHintChange);
        hints.emplace_back(v.stats_editing_ ? text_tools::kHintDone : text_tools::kHintBack);
    }
    float bottom = get_hints(ctx, hints, {.left = kColumnLeft, .right = right}, kHintsBottom) - kStatsHintsGap;
    if (v.points >= 0) {
        Style big;
        big.face = Face::kDisplay;
        big.css = kTitleTextSize;
        big.colour = v.points > 0 ? Menu::kHouseYellow : kMuted2;
        const std::string points = std::to_string(v.points);
        const float top = bottom - (72.0F * 0.95F);
        draw_text(ctx.dl, big, *ctx.f,
                  {.x = ctx.f->get_x(left), .y = datum(big, *ctx.f, ctx.f->get_y(top), kPointsLineHeight)},
                  points.c_str());
        bottom = get_kicker(ctx, text_tools::kPointsLeft, {.x = left, .y = top - kPointsKickerGap},
                            {.colour = Menu::kHouseYellow}) -
                 kPointsGap;
    }
    const float name_top = big_name(ctx, v.big_name_, 84.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    get_kicker(ctx, v.title, {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow});
    if (!v.lines.empty()) {
        paragraphs(ctx, v.lines, {.left = left, .right = right}, {.top = kStatsTextTop, .lines = kStatsTextLines});
    }
}

} // namespace internal
} // namespace frontend::new_frontend::paint
