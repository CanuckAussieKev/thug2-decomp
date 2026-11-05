// NOT ORIGINAL - the new front end's options and level screens, and the wipe between menus (ours). See
// new_frontend_draw.h.

#include "frontend/new_frontend_draw.h"
#include <algorithm>
#include <cmath>
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
#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "base/core/ints.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/maths/rect.h"
#include "frontend/new_frontend_draw_internal.h"

namespace frontend::new_frontend::paint {
// What the source file's anonymous namespace held that its pieces share (new_frontend_draw_internal.h): visible as it
// was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend::paint

namespace frontend::new_frontend::paint {
namespace {

constexpr float kButtonLineHeight = 1.15F;
constexpr float kButtonVerticalPad = 12.0F;
constexpr float kButtonRecordsGap = 6.0F;

// The options screen: the row's padding, the value's gap from the label, the right panel's stack of gaps (the choices
// control, the notes under it, the help text, the kicker above the name).
constexpr float kOptionRowPad = 6.0F;
constexpr float kOptionValueGap = 40.0F;

constexpr float kHelpLineBox = 21.0F; // the help text's line: its size times its line height
constexpr float kHelpGap = 10.0F;
constexpr float kChoiceCutInset = 6.0F;
constexpr float kChoicePadX = 14.0F;
constexpr float kChoicePadY = 8.0F;
constexpr float kChoiceGap = 6.0F;
constexpr float kNoteLineBox = 15.6F; // the note's line: its size times its line height
constexpr float kNoteGap = 8.0F;

constexpr float kScreenNoteTop = 36.0F; // the note along the top, level with the header

// The level select: the coloured edge, when the launch counts as done, the variant label's and records' text buffers,
// the kicker's gap, the wipe's skew.
constexpr float kColumnEdgeWidth = 4.0F;
constexpr float kLaunchGrowDone = 0.999F;
constexpr std::size_t kVariantLabelBuffer = 64;
constexpr std::size_t kRecordsBuffer = 64;
constexpr float kLevelKickerGap = 10.0F;
constexpr float kWipeSkewSides = 2.0F; // the skew is added on both sides of the window
constexpr float kWipePhases = 2.0F;    // the band comes in over the first half and goes out over the second

} // namespace
namespace internal {

// The options: the groups as tabs, the group's settings down the left (Anton 40, each one's value in mono at the
// column's right), the selected one's name on the right with its choices as a segmented control, what it does, its
// ini key and when a change is seen.
void options_screen(Ctx& ctx, float window_width, float window_height)
{
    const Menu& menu = *ctx.menu;
    const std::vector<options::Spec>& specs = options::get_specs();
    const std::vector<options::GroupSet>& groups = options::bunches();
    if (menu.over_pause()) {
        pause_ground(ctx, window_width, window_height);
    } else {
        backdrop(ctx.dl, *ctx.f, art_for(*ctx.extras, art_by_id(menu.payload(), "options")), menu.get_pan());
    }
    const float rule = get_header(ctx, {.x = kColumnInner, .y = 36.0F}, text_tools::kTitleOptions, true, nullptr);
    std::vector<std::pair<std::string, uint32_t>> tabs;
    tabs.reserve(groups.size());
    for (const options::GroupSet& group : groups) {
        tabs.emplace_back(group.name, Menu::kHouseYellow);
    }
    const float list_top = get_tabs(ctx, {.tabs = tabs, .selected = menu.option_group()}, rule + 14.0F) + 18.0F;
    const std::vector<int>& rows = menu.option_rows();
    // (the options' rows: a size smaller than the level list's - their labels are long)
    RowStyle rs = level_row_style();
    rs.name.css = kNameTextSize;
    rs.row_height_ = (kNameTextSize * kTabLineHeight) + kOptionRowPad;
    const float row_h = rs.row_height_;
    Style value = mono12(kMuted, false);
    const int count = static_cast<int>(rows.size()) + 1;
    for (int i = 0; i < count; ++i) {
        const float top = list_top + (static_cast<float>(i) * row_h);
        const bool action = std::cmp_equal(i, rows.size());
        const char* label = action ? text_tools::kGameOptions : core::at(specs, core::at(rows, i)).label;
        // (a short label for the row: the design's 40 px type in a 470 px column)
        get_row(ctx, rs, top, pad2(i + 1).c_str(), label,
                {.amount = menu.row_amount(i), .cut = Menu::kHouseYellow, .dim = false});
        const std::string shown = action ? std::string(text_tools::kGameOptionsValue)
                                         : core::at(core::at(specs, core::at(rows, i)).choices, menu.option_choice(core::at(rows, i))).label;
        const float base = datum(rs.name, *ctx.f, ctx.f->get_y(top), rs.line_height_);
        value.colour = i == menu.option_row() ? Menu::kHouseYellow : kMuted;
        // (the value at the column's right, where the label leaves room for it)
        const float value_x = ctx.f->get_x(kColumnInner) - get_measure(value, *ctx.f, shown.c_str());
        if (ctx.f->get_x(rs.text_x_) + get_measure(rs.name, *ctx.f, label) + ctx.f->current_s(kOptionValueGap) <
            value_x) {
            draw_text(ctx.dl, value, *ctx.f, {.x = value_x, .y = base}, shown.c_str());
        }
        ctx.get_hit(Hit::Kind::kItem, i, kColumnLeft, top, kColumnInner + kRowHitOverhang, top + row_h);
    }

    // The right's foot.
    const float left = kRight;
    const float right = 1240.0F;
    const bool action = menu.option_row() >= static_cast<int>(rows.size());
    const std::vector<std::string> hints =
        action ? std::vector<std::string>{text_tools::kHintOpen, text_tools::kHintGroup, text_tools::kHintBack}
               : std::vector<std::string>{text_tools::kHintChange, text_tools::kHintGroup, text_tools::kHintBack};
    float bottom = get_hints(ctx, hints, {.left = left, .right = right}, kPanelHintsBottom) - kPanelStackGap;
    Style body;
    body.face = Face::kBody;
    body.css = kBodyTextSize;
    body.colour = kMuted;
    std::string name;
    std::string const kicker =
        std::string(core::at(groups, menu.option_group()).name) + " \xE2\x80\x94 " + pad2(menu.option_row() + 1);
    if (action) {
        get_button(ctx, {.x = left, .y = bottom}, text_tools::kOpen, Menu::kHouseYellow, true, Hit::Kind::kSkate, 1.0F);
        bottom -= (kHeadingTextSize * kButtonLineHeight) + kButtonVerticalPad + kPanelStackGap;
        draw_text(
            ctx.dl, body, *ctx.f,
            {.x = ctx.f->get_x(left), .y = datum(body, *ctx.f, ctx.f->get_y(bottom - kHelpLineBox), kBodyLineHeight)},
            text_tools::kGameOptionsHelp);
        bottom -= kHelpLineBox + kHelpGap;
        name = text_tools::kGameOptions;
    } else {
        const int option = core::at(rows, menu.option_row());
        const options::Spec& spec = core::at(specs, option);
        // The choices: a segmented control (mono 13; the chosen one filled).
        Style seg = mono12(kPaper, false);
        seg.css = kLabelTextSize;
        const float hints_height = (13.0F * 1.3F) + 16.0F;
        float x = left;
        float const y0 = bottom - hints_height;
        const int chosen = menu.option_choice(option);
        for (size_t i = 0; i < spec.choices.size(); ++i) {
            const float w = (get_measure(seg, *ctx.f, core::at(spec.choices, i).label) / ctx.f->scale()) + 28.0F;
            const bool on = ints::as_signed(i) == chosen;
            if (on) {
                get_cut(ctx.dl,
                        {.x0 = ctx.f->get_x(x),
                         .y0 = ctx.f->get_y(y0),
                         .x1 = ctx.f->get_x(x + w),
                         .y1 = ctx.f->get_y(bottom)},
                        {.top = ctx.f->current_s(kChoiceCutInset), .bottom = ctx.f->current_s(kChoiceCutInset)},
                        get_col(Menu::kHouseYellow));
            } else {
                ctx.dl->AddRect(ImVec2(ctx.f->get_x(x), ctx.f->get_y(y0)),
                                ImVec2(ctx.f->get_x(x + w), ctx.f->get_y(bottom)), get_col(kRule), 0.0F, 0,
                                std::max(1.0F, ctx.f->current_s(1.0F)));
            }
            seg.colour = on ? kGround : kPaper;
            draw_text(ctx.dl, seg, *ctx.f,
                      {.x = ctx.f->get_x(x + kChoicePadX),
                       .y = datum(seg, *ctx.f, ctx.f->get_y(y0 + kChoicePadY), kTextLineHeight)},
                      core::at(spec.choices, i).label);
            ctx.get_hit(Hit::Kind::kChoice, (menu.option_row() * kHitChoiceStride) + static_cast<int>(i), x, y0, x + w,
                        bottom);
            x += w + kChoiceGap;
        }
        bottom = y0 - kPanelStackGap;
        // When it's seen ("Restart to apply" - yellow once changed; nothing for a live one), its ini key only behind
        // [frontend] show_ini_keys=1, what it does (when it has a line).
        const bool pending = std::cmp_less(option, ctx.extras->restart_pending_.size()) &&
                             core::at(ctx.extras->restart_pending_, option);
        const std::string when =
            spec.apply == options::Apply::kLive ? std::string() : std::string(text_tools::kRestartToApply);
        const std::string where =
            ctx.extras->show_ini_keys_ ? std::string("[") + spec.section + "] " + spec.key : std::string();
        if (!when.empty() || !where.empty()) {
            const Style key = mono12(kMuted2, false);
            if (!where.empty()) {
                draw_text(ctx.dl, key, *ctx.f,
                          {.x = ctx.f->get_x(left),
                           .y = datum(key, *ctx.f, ctx.f->get_y(bottom - kNoteLineBox), kTextLineHeight)},
                          where.c_str());
            }
            const Style note = mono12(pending ? Menu::kHouseYellow : kMuted2, false);
            if (!when.empty()) {
                draw_text(ctx.dl, note, *ctx.f,
                          {.x = ctx.f->get_x(right) - get_measure(note, *ctx.f, when.c_str()),
                           .y = datum(note, *ctx.f, ctx.f->get_y(bottom - kNoteLineBox), kTextLineHeight)},
                          when.c_str());
            }
            bottom -= kNoteLineBox + kNoteGap;
        }
        if (spec.help && (*spec.help != '\0')) {
            draw_text(ctx.dl, body, *ctx.f,
                      {.x = ctx.f->get_x(left),
                       .y = datum(body, *ctx.f, ctx.f->get_y(bottom - kHelpLineBox), kBodyLineHeight)},
                      spec.help);
            bottom -= kHelpLineBox + kHelpGap;
        }
        name = spec.label;
    }
    const float name_top = big_name(ctx, name, 96.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    get_kicker(ctx, kicker, {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow});
    if (!ctx.extras->options_note_.empty()) {
        const Style note = mono12(kMuted2, false);
        draw_text(ctx.dl, note, *ctx.f,
                  {.x = ctx.f->get_x(left), .y = datum(note, *ctx.f, ctx.f->get_y(kScreenNoteTop), kTextLineHeight)},
                  ctx.extras->options_note_.c_str());
    }
}

} // namespace internal
namespace {

// The level select.
// The level select's column: the title for what it's for, the game tabs and the section's levels (`visible_rows`:
// how many fit).
void draw_level_column(Ctx& ctx, int* visible_rows, const Section* section, uint32_t colour)
{
    const Menu& menu = *ctx.menu;
    const Catalogue* data = menu.payload();
    const SectionView* view = menu.get_view();
    const int section_index = menu.get_section_index();
    ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(0.0F)),
                         ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(kDesignHeight)), true);
    const char* title = text_tools::kTitleFreeSkate;
    if (menu.levels_purpose() == LevelsFor::kEdit) {
        title = text_tools::kTitleEditLevel;
    } else if (menu.levels_purpose() == LevelsFor::kFreeze) {
        title = text_tools::kTitleChangeLevel;
    } else if (menu.levels_purpose() == LevelsFor::kNet) {
        title = text_tools::kTitleNetLevel;
    }
    const float rule = get_header(ctx, {.x = kColumnInner, .y = 36.0F}, title, true, nullptr);
    // The game tabs (mono 13, wrapping; the selected one in its colour, underlined).
    std::vector<std::pair<std::string, uint32_t>> tabs;
    for (int i = 0; data && std::cmp_less(i, data->sections().size()); ++i) {
        tabs.emplace_back(core::at(data->sections(), i).tab, core::at(data->sections(), i).colour);
    }
    const float tabs_bottom = get_tabs(ctx, {.tabs = tabs, .selected = section_index}, rule + 14.0F);
    const float tab_h = 0.0F;
    const float y = tabs_bottom;              // The list.
    const float list_top = y + tab_h + 18.0F; // (tab_h 0: Tabs gave the bottom of its last row)
    const float list_bottom = 692.0F;
    const int fit = std::max(1, static_cast<int>((list_bottom - list_top) / kLevelRowHeight));
    if (visible_rows) {
        *visible_rows = fit;
    }
    const RowStyle rs = level_row_style();
    ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(list_top - kListClipAbove)),
                         ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(list_bottom + kListClipBelow)),
                         true);
    const float scroll = menu.list_scroll();
    const int count = view ? static_cast<int>(view->levels.size()) : 0;
    const int first = std::max(0, static_cast<int>(std::floor(scroll)) - 1);
    const int last = std::min(count - 1, static_cast<int>(std::ceil(scroll)) + fit + 1);
    for (int row = first; row <= last; ++row) {
        const StageMap& stage = core::at(section->levels, core::at(view->levels, row));
        const float top = list_top + ((static_cast<float>(row) - scroll) * kLevelRowHeight);
        const bool locked = std::cmp_less(row, view->locked.size()) && core::at(view->locked, row);
        get_row(ctx, rs, top, pad2(row + 1).c_str(), stage.name.c_str(),
                {.amount = menu.row_amount(row), .cut = colour, .dim = locked});
        if (top >= list_top - 1.0F && top + kLevelRowHeight <= list_bottom + 1.0F) {
            ctx.get_hit(Hit::Kind::kStageMap, row, kColumnLeft, top, kColumnInner + kRowHitOverhang,
                        top + kLevelRowHeight);
        }
    }
    ctx.dl->PopClipRect();
    if (count == 0) {
        const Style empty = mono12(kMuted2);
        draw_text(ctx.dl, empty, *ctx.f,
                  {.x = ctx.f->get_x(kColumnLeft), .y = datum(empty, *ctx.f, ctx.f->get_y(list_top), kTextLineHeight)},
                  text_tools::kNoLevels);
    }
    ctx.dl->PopClipRect();
}

} // namespace
namespace internal {

void levels_screen(Ctx& ctx, int* visible_rows)
{
    const Menu& menu = *ctx.menu;
    const Catalogue* data = menu.payload();
    const int section_index = menu.get_section_index();
    const Section* section = data && section_index >= 0 && std::cmp_less(section_index, data->sections().size())
                                 ? &core::at(data->sections(), section_index)
                                 : nullptr;
    const uint32_t colour = section ? section->colour : Menu::kHouseYellow;
    const StageMap* level = menu.selected_level();
    const float launch = menu.get_launch();
    const float grow = launch >= 0.0F ? ease_out(launch * 1.4F) : 0.0F;

    // The level's loading screen behind the whole screen, the column's scrim over it (a launch: the scrim and the
    // list go under the sharp picture), the thin edge in the game's colour.
    const Art art = art_for(*ctx.extras, level ? current_art(*level) : std::string());
    backdrop(ctx.dl, *ctx.f, art, menu.get_pan());
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(kColumn), ctx.f->get_y(0.0F)),
                          ImVec2(ctx.f->get_x(kColumn + kColumnEdgeWidth), ctx.f->get_y(kDesignHeight)),
                          get_col(colour, 1.0F - grow));

    if (grow < kLaunchGrowDone) {
        draw_level_column(ctx, visible_rows, section, colour);
    }
    if (grow > 0.0F) {
        // (the launch: the picture comes up sharp over the column)
        ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(0.0F)),
                             ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(kDesignHeight)), true);
        art_box(ctx.dl, art,
                {.x0 = ctx.f->get_x(0.0F),
                 .y0 = ctx.f->get_y(0.0F),
                 .x1 = ctx.f->get_x(kDesignWidth),
                 .y1 = ctx.f->get_y(kDesignHeight)},
                {.pan = menu.get_pan(), .alpha = grow});
        ctx.dl->PopClipRect();
    }

    // The picture's number at the top right (Q / E change it) - only for a level with more than one.
    if (level && level->art_variants_.size() > 1 && grow < kLaunchGrowDone) {
        std::array<char, kVariantLabelBuffer> text{};
        if (level->variant >= 0 && std::cmp_less(level->variant, level->art_labels_.size())) {
            text::format_to(text, "Q  %s  E", core::at(level->art_labels_, level->variant).c_str());
        } else {
            text::format_to(text, "Q  %d / %d  E", level->variant + 1, static_cast<int>(level->art_variants_.size()));
        }
        Style note = mono12(kMuted);
        note.alpha = 1.0F - grow;
        const float nw = get_measure(note, *ctx.f, text.data()) / ctx.f->scale();
        draw_text(ctx.dl, note, *ctx.f,
                  {.x = ctx.f->get_x(kRowRight - nw),
                   .y = datum(note, *ctx.f, ctx.f->get_y(kScreenNoteTop), kTextLineHeight)},
                  text.data());
    }

    // The right's foot: Skate, the records, the name, the kicker.
    const float left = kRight + ((32.0F - kRight) * grow);
    const float right = 1240.0F;
    const float bottom = 692.0F;
    if (level) {
        const float fade = 1.0F - grow;
        // Skate (Anton 30 on the game's colour, a slanted button).
        Style skate;
        skate.face = Face::kDisplay;
        skate.css = kHeadingTextSize;
        skate.upper = true;
        skate.colour = kGround;
        skate.alpha = fade;
        const bool locked = menu.selected_locked();
        const char* skate_text = text_tools::kSkate;
        if (locked) {
            skate_text = text_tools::kLocked;
        } else if (menu.levels_purpose() == LevelsFor::kEdit) {
            skate_text = text_tools::kEdit;
        } else if (menu.levels_purpose() == LevelsFor::kNet) {
            skate_text = text_tools::kSelect;
        }
        const float tw = get_measure(skate, *ctx.f, skate_text) / ctx.f->scale();
        const float arrow_w = locked ? 0.0F : (30.0F * 0.9F) + 10.0F;
        const float bh = (30.0F * 1.15F) + 12.0F;
        const float bx0 = left;
        const float bx1 = left + 22.0F + tw + arrow_w + 22.0F;
        const float by0 = bottom - bh;
        const float by1 = bottom;
        get_cut(ctx.dl,
                {.x0 = ctx.f->get_x(bx0), .y0 = ctx.f->get_y(by0), .x1 = ctx.f->get_x(bx1), .y1 = ctx.f->get_y(by1)},
                {.top = ctx.f->current_s(kButtonCutInset), .bottom = ctx.f->current_s(kButtonCutInset)},
                get_col(locked ? kMuted2 : colour, fade));
        const float sb = datum(skate, *ctx.f, ctx.f->get_y(by0 + 6.0F), 1.15F);
        draw_text(ctx.dl, skate, *ctx.f, {.x = ctx.f->get_x(bx0 + kButtonPadding), .y = sb}, skate_text);
        if (!locked) {
            float adv = 0.0F;
            get_arrow(ctx.dl, {.x = ctx.f->get_x(bx0 + kButtonPadding + tw + kButtonArrowGap), .y = sb},
                      ctx.f->current_s(kHeadingTextSize), false, get_col(kGround, fade), &adv);
        }
        ctx.get_hit(Hit::Kind::kSkate, 0, bx0, by0, bx1, by1);
        float y = by0 - kButtonRecordsGap;
        // The records (only what the career keeps for this level).
        const Highscores rec = ctx.extras->records ? ctx.extras->records(*level) : Highscores{};
        std::vector<std::string> cols;
        std::array<char, kRecordsBuffer> text{};
        if (rec.gaps_total_ > 0) {
            text::format_to(text, "%s %d/%d", text_tools::kGaps, rec.gaps_got_, rec.gaps_total_);
            cols.emplace_back(text.data());
        }
        if (rec.goals >= 0) {
            text::format_to(text, "%s %d", text_tools::kGoals, rec.goals);
            cols.emplace_back(text.data());
        }
        if (!cols.empty()) {
            Style style = mono12(kMuted, false);
            style.alpha = fade;
            const float top = y - 10.0F - (12.0F * 1.3F);
            const float col_w = (right - left) / 3.0F;
            for (size_t i = 0; i < cols.size(); ++i) {
                draw_text(ctx.dl, style, *ctx.f,
                          {.x = ctx.f->get_x(left + (col_w * static_cast<float>(i))),
                           .y = datum(style, *ctx.f, ctx.f->get_y(top), kTextLineHeight)},
                          core::at(cols, i).c_str());
            }
            const float rule_y = top - 10.0F - 1.0F;
            ctx.dl->AddRectFilled(
                ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule_y)),
                ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule_y) + std::max(1.0F, ctx.f->current_s(1.0F))),
                get_col(kRule, fade));
            y = rule_y;
        }
        const float slice = launch >= 0.0F ? ease_out(launch * 1.6F) : 0.0F;
        const float name_top = big_name(ctx, level->name, 112.0F, {.left = left, .right = right}, y - 10.0F,
                                        {.amount = slice, .colour = colour});
        std::string kicker = (section ? section->tab : std::string()) + " \xE2\x80\x94 " + pad2(menu.level_row() + 1);
        if (locked) {
            kicker += std::string(" \xC2\xB7 ") + text_tools::kLocked;
        }
        get_kicker(ctx, kicker, {.x = left, .y = name_top - kLevelKickerGap},
                   {.colour = colour, .alpha = 1.0F - slice});
    }
}

// The screen change: one diagonal band in `colour` across the window - in over the first half (the screen changes
// under it), out over the second.
void wipe(ImDrawList* dl, const Menu& menu, maths::Size window)
{
    const float wipe_amount = menu.wipe();
    const uint32_t colour = menu.wipe_colour();
    const float width = window.width;
    const float height = window.height;
    if (wipe_amount < 0.0F) {
        return;
    }
    const float skew = height * 0.35F;
    float lead = -skew + ((width + (skew * kWipeSkewSides)) * ease_out(std::min(1.0F, wipe_amount * kWipePhases)));
    float trail =
        -skew + ((width + (skew * kWipeSkewSides)) * ease_out(std::max(0.0F, (wipe_amount * kWipePhases) - 1.0F)));
    if (lead <= trail) {
        return;
    }
    std::array<ImVec2, 4> points = {{{trail + skew, 0.0F}, {lead + skew, 0.0F}, {lead, height}, {trail, height}}};
    dl->AddConvexPolyFilled(points.data(), 4, get_col(colour));
}

} // namespace internal
} // namespace frontend::new_frontend::paint
