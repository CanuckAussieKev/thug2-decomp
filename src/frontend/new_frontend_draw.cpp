// NOT ORIGINAL - the new front end's drawing (ours): see new_frontend_draw.h. The numbers are the design's
// (docs/design/frontend/HMain.dc.html, HLevels.dc.html - their CSS), in design pixels at 1280 x 720. This file holds
// the design's constants, the row and list layout and the entry points (get_screen, restyled, get_waiting); the pieces
// (draw_text / draw_shapes / draw_parts / draw_levels / draw_panels / draw_menus) are new_frontend_draw_<name>.cpp, sharing new_frontend_draw_internal.h.

#include "frontend/new_frontend_draw.h"
#include <algorithm>
#include <string>
#include <vector>
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "frontend/new_frontend_draw_internal.h"

namespace frontend::new_frontend::paint {
// What the source file's anonymous namespace held that its pieces share (new_frontend_draw_internal.h): visible as it
// was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend::paint

namespace frontend::new_frontend::paint {
namespace {

constexpr float kMainListTopGap = 36.0F; // the main menu's first row under the header's rule
constexpr float kMainRowStep = 2.0F;     // the gap between two main menu rows
constexpr float kTabListGap = 18.0F;     // the level list under the last row of tabs

constexpr float kLetterboxMargin = 0.5F; // the frame is offset by more than this: bars are drawn

} // namespace

// --- the public parts -------------------------------------------------------------------------------------------

float baseline_in_line(float ascent, float descent, float css, float line_height)
{
    const float content = (ascent - descent) * css;
    return (((css * line_height) - content) * kHalf) + (ascent * css);
}

float main_row_top(int index)
{
    // (the header's rule at 40 + 12 * 1.3 + 12, 1 px; the nav 36 below; rows 2 apart)
    const float rule = 40.0F + (12.0F * 1.3F) + 12.0F + 1.0F;
    return rule + kMainListTopGap + (static_cast<float>(index) * (kMainRowHeight + kMainRowStep));
}

float level_list_top(int tab_rows)
{
    const float rule = 36.0F + (12.0F * 1.3F) + 12.0F + 1.0F;
    const float tab_h = (13.0F * 1.3F) + 8.0F;
    const int rows = std::max(1, tab_rows);
    const float last_tab_top = rule + 14.0F + (static_cast<float>(rows - 1) * (tab_h + 4.0F));
    return last_tab_top + tab_h + kTabListGap;
}

int level_list_rows(int tab_rows)
{
    return std::max(1, static_cast<int>((kPanelHintsBottom - level_list_top(tab_rows)) / kLevelRowHeight));
}

void get_screen(const Menu& menu, const TickCycle& frame, float window_width, float window_height, const Extras& extras,
                std::vector<Hit>* hits, int* visible_rows)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    // (the pause menus: over the game, dimmed - the rest on the ground's colour)
    const bool over_game =
        is_pause_screen(menu.get_shown()) || (menu.get_shown() == Screen::kOptions && menu.over_pause());
    if (!over_game) {
        dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
    }
    Ctx ctx{.dl = dl, .f = &frame, .menu = &menu, .extras = &extras, .hits = hits};
    if (hits) {
        hits->clear();
    }
    if (menu.get_shown() == Screen::kLevels) {
        levels_screen(ctx, visible_rows);
    } else if (menu.get_shown() == Screen::kOptions) {
        options_screen(ctx, window_width, window_height);
    }
    // (the roots - Main, Pause - are never drawn: ours goes aside at them)
    // (a 16:9 frame in a wider or taller window: the bars the ground's colour, over anything that spilled)
    if (frame.left() > kLetterboxMargin && !over_game) {
        dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(frame.left(), window_height), get_col(kGround));
        dl->AddRectFilled(ImVec2(frame.get_x(kDesignWidth), 0.0F), ImVec2(window_width, window_height),
                          get_col(kGround));
    }
    if (frame.top() > kLetterboxMargin && !over_game) {
        dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, frame.top()), get_col(kGround));
        dl->AddRectFilled(ImVec2(0.0F, frame.get_y(kDesignHeight)), ImVec2(window_width, window_height),
                          get_col(kGround));
    }
    wipe(dl, menu, {.width = window_width, .height = window_height});
    dl->PopTextureID();
}

void restyled(const mirror::Tracker& tracker, const TickCycle& frame, float window_width, float window_height,
              std::vector<Hit>* hits, float* scroll, const Extras* extras)
{
    static const Menu kNoMenu;
    static const Extras kNoExtras;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    Ctx ctx{.dl = dl, .f = &frame, .menu = &kNoMenu, .extras = extras ? extras : &kNoExtras, .hits = hits};
    if (hits) {
        hits->clear();
    }
    switch (tracker.get_shown().kind) {
    case mirror::Kind::kDialog:
        dialog_screen(ctx, tracker, window_width, window_height);
        break;
    case mirror::Kind::kSkaters:
        skaters_screen(ctx, tracker, window_width, window_height);
        break;
    case mirror::Kind::kLinkChain:
        list_of_items(ctx, tracker, window_width, window_height, scroll);
        break;
    case mirror::Kind::kFreeze:
        pause_of_items(ctx, tracker, window_width, window_height, scroll);
        break;
    case mirror::Kind::kMenu:
        // (in a level - the pause menu's GAME OPTIONS... - the game stays behind it, dimmed, as the pause menus; the
        // user's change, 2026-11-02)
        if (tracker.get_shown().over_game_) {
            pause_ground(ctx, window_width, window_height);
        } else {
            dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
        }
        menu_of_items(ctx, tracker, scroll);
        break;
    case mirror::Kind::kKeyboard:
        if (!tracker.get_shown().over_game_) {
            dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
        }
        keyboard_screen(ctx, tracker, window_width, window_height);
        break;
    case mirror::Kind::kStats:
        stats_screen(ctx, tracker, window_width, window_height);
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
    dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
    backdrop(dl, frame, art_for(extras, art_by_id(extras.catalogue, art_id)), 0.0F);
    dl->PopTextureID();
}

} // namespace frontend::new_frontend::paint
