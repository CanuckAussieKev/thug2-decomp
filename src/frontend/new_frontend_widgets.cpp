// NOT ORIGINAL - the new front end reading the original menus' widgets (ours): the element tree, a menu's items,
// the stats and keyboard screens, into the mirror's records. See new_frontend.h.

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "game/front_end/front_end.h"
#include "engine/ui/menu_reading.h"
#include "frontend/new_frontend_game.h"
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_text.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_element_manager.h"
#include "engine/ui/sprite_element.h"
#include "engine/ui/text_element.h"
#include "engine/gfx/font.h"
#include "engine/gfx/glyphs.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/pad_input/device.h"
#include "base/core/str.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/rider/thaw_bmx.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "game/rider/skater_state.h"
#include "game/rider/skater_score.h"
#include "game/skate/point_tally.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "frontend/new_frontend_internal.h"

namespace frontend::new_frontend {
// What the source file's anonymous namespace held that its pieces share (new_frontend_internal.h): visible as it was.
// (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend

namespace frontend::new_frontend {
namespace {

constexpr float kVisibleAlpha = 0.02F;          // an element needs more alpha than this to count as shown
constexpr int kWalkBudget = 8192;               // the most elements a tree walk visits

constexpr float kScaleArrowLeft = -49.0F;       // the scaling menus' arrow markers
constexpr float kScaleArrowRight = 76.0F;
constexpr float kColourArrowLeft = -53.0F; // the colour menus' arrow markers
constexpr float kColourArrowRight = 80.0F;
constexpr int kLitRedThreshold = 40;            // a stat cap sprite counts as lit when its red reaches this
constexpr float kLitAlpha = 0.6F;
constexpr uint32_t kMaxStatNames = 16;
constexpr int kMaxMenuChildren = 80;
constexpr int kSpriteBudget = 256;
constexpr int kValueBudget = 256;
constexpr float kRowSpacing = 24.0F; // the default spacing between a menu's rows
constexpr float kRowSlack = 2.0F;    // rows closer than this belong to one line
constexpr float kHalf = 0.5F;
constexpr std::size_t kStatNameSize = 32;
constexpr int kMaxKeyboardChildren = 32;
constexpr std::size_t kLogLineSize = 512;
constexpr int kMaxDumpDepth = 8;
constexpr int kTextBudget = 400;
constexpr std::size_t kMaxTexts = 6;
constexpr int kScreenBudget = 600;
constexpr std::size_t kTextLineSize = 160;
constexpr int kKeyTextBudget = 256;             // a wide key: a container of a score of cap sprites before its words
constexpr int kMinSingles = 20;                 // single-letter children that make a container a keyboard
constexpr int kChildTextBudget = 32;
constexpr int kButtonDown = 4;                  // the pad buttons a key names (render::button_for_tag)
constexpr int kButtonRight = 5;
constexpr int kButtonLeft = 6;
constexpr int kButtonUp = 7;
constexpr int kButtonDownRight = 10;
constexpr int kButtonUpLeft = 11;
constexpr int kButtonDownLeft = 12;
constexpr int kButtonUpRight = 13;

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

uint32_t type_of(const UiWidget* element) { return static_cast<const objects::Entity*>(element)->kind_tag(); }

} // namespace
namespace internal {

uint32_t id_of_element(const UiWidget* element) { return static_cast<const objects::Entity*>(element)->ident(); }
UiWidget* first_child(const UiWidget* element) { return element->eldest().get_pull(); }
UiWidget* get_next(const UiWidget* element) { return element->following().get_pull(); }

} // namespace internal
namespace {

bool element_hidden(const UiWidget* element)
{
    return (static_cast<const objects::Entity*>(element)->object_flags() & UiWidget::kHidden) != 0;
}
bool get_visible(const UiWidget* element)
{
    return !element_hidden(element) && element->aim_point().alpha > kVisibleAlpha && core::at(element->aim_point().rgba, 3) > 2;
}

uint32_t text_type()
{
    static const uint32_t kType = name_hash("TextElement");
    return kType;
}
uint32_t sprite_type()
{
    static const uint32_t kType = name_hash("SpriteElement");
    return kType;
}

UiWidget* root_element()
{
    const UiTree* manager = UiTree::sole();
    return manager ? manager->tree_root().get_pull() : nullptr;
}

} // namespace
namespace internal {

// The menu that takes the pad events now: of the focused menus, one in a dialog box first, else the last in the tree
// (the newest screen). `inDialog`: it is in dialog_box_container. `coming`: a menu not taking them yet counts too, when
// it is shown - the screen being made (the pause menu animates in for 13 frames before it takes the pad).
UiWidget* active_menu(bool* in_dialog, bool coming)
{
    const UiWidget* root = root_element();
    if (!root) {
        return nullptr;
    }
    static const uint32_t kDialogContainer = name_hash("dialog_box_container");
    UiWidget* found = nullptr;
    UiWidget* dialog = nullptr;
    int budget = kWalkBudget;
    // (depth first, without recursion: the element, its first child, its next sibling, back up through the parents)
    struct Walk {
        UiWidget* element;
        bool in_dialog_;
    };
    std::vector<Walk> stack;
    stack.push_back({.element = first_child(root), .in_dialog_ = false});
    while (!stack.empty() && --budget > 0) {
        Walk& top = stack.back();
        UiWidget* widget = top.element;
        if (!widget) {
            stack.pop_back();
            continue;
        }
        const bool here = top.in_dialog_ || id_of_element(widget) == kDialogContainer;
        top.element = get_next(widget);
        if (menu_reading::is_menu(widget) &&
            (coming ? get_visible(widget) : menu_reading::menu_takes_pad_events(widget))) {
            found = widget;
            if (here) {
                dialog = widget;
            }
        }
        if (UiWidget* child = first_child(widget)) {
            stack.push_back({.element = child, .in_dialog_ = here});
        }
    }
    if (in_dialog) {
        *in_dialog = dialog != nullptr;
    }
    return dialog ? dialog : found;
}

} // namespace internal
namespace {

// The strings of the text elements in `element` (itself included), in tree order; `skip`: subtrees left out.
void collect_texts(const UiWidget* element, std::vector<std::string>* out, const std::vector<const UiWidget*>& skip,
                   int* budget, bool visible_only)
{
    if (!element || --*budget <= 0) {
        return;
    }
    if (std::ranges::find(skip, element) != skip.end()) {
        return;
    }
    if (visible_only && !get_visible(element)) {
        return;
    }
    if (type_of(element) == text_type()) {
        const char* text = core::down_cast<const LabelWidget>(element)->chars();
        if (text && (*text != '\0')) {
            out->emplace_back(text);
        }
    }
    for (const UiWidget* child = first_child(element); child && *budget > 0; child = get_next(child)) {
        collect_texts(child, out, skip, budget, visible_only);
    }
}

// An element's height on the 640 x 480 layout: its position and its parents' added up (justification and scale aside -
// enough to tell rows apart).
float absolute_y(const UiWidget* element)
{
    float y = 0.0F;
    for (int guard = 0; element && guard < kMaxParentDepth; ++guard, element = element->container().get_pull()) {
        y += element->aim_point().y;
    }
    return y;
}

// The visible texts in `element` (itself included) with their heights, in tree order; `skip`: subtrees left out.
void collect_texts_at(const UiWidget* element, std::vector<std::pair<std::string, float>>* out,
                      const std::vector<const UiWidget*>& skip, int* budget)
{
    if (!element || --*budget <= 0 || !get_visible(element)) {
        return;
    }
    if (std::ranges::find(skip, element) != skip.end()) {
        return;
    }
    if (type_of(element) == text_type()) {
        const char* text = core::down_cast<const LabelWidget>(element)->chars();
        if (text && (*text != '\0')) {
            out->emplace_back(text, absolute_y(element));
        }
    }
    for (const UiWidget* child = first_child(element); child && *budget > 0; child = get_next(child)) {
        collect_texts_at(child, out, skip, budget);
    }
}

// A visible sprite in `element` that isn't a highlight bar (those are drawn at alpha 0 until their item is focused).
bool has_visible_sprite(const UiWidget* element, int* budget)
{
    if (!element || --*budget <= 0 || element_hidden(element) || element->aim_point().alpha <= kVisibleAlpha) {
        return false;
    }
    if (type_of(element) == sprite_type() && core::at(element->aim_point().rgba, 3) > 2 &&
        std::fabs(element->aim_point().scale_x_) > kInvisibleThreshold) {
        return true;
    }
    for (const UiWidget* child = first_child(element); child; child = get_next(child)) {
        if (has_visible_sprite(child, budget)) {
            return true;
        }
    }
    return false;
}

// A slider item's marker (its up_arrow sprite): where it is along the bar, and which colour slider it is.
void read_slider_marker(const UiWidget* widget, mirror::ItemRead* item)
{
    static const uint32_t kHueArrow = name_hash("hue_up_arrow");
    static const auto kColourArrows =
        std::to_array<uint32_t>({name_hash("hue_up_arrow"), name_hash("saturation_up_arrow"),
                                 name_hash("value_up_arrow"), name_hash("alpha_up_arrow")});
    // (colormenu_nowrap_arrow_left / right -53 .. 80 for the colour menus' markers, scalingmenu_arrow_left /
    // right -49 .. 76 for the scaling ones)
    const uint32_t id = id_of_element(widget);
    float left = -kScaleArrowLeft;
    float right = kScaleArrowRight;
    for (const uint32_t colour : kColourArrows) {
        if (id == colour) {
            left = -kColourArrowLeft;
            right = kColourArrowRight;
        }
    }
    item->adjustable = true;
    item->slider_at_ = std::clamp((widget->aim_point().x - left) / (right - left), 0.0F, 1.0F);
    item->slider_hue_ = id == kHueArrow;
    item->slider_kind_ = 0; // (the colour arrows' sliders: 1-4)
    for (int arrow = 0; arrow < 4; ++arrow) {
        if (id == core::at(kColourArrows, arrow)) {
            item->slider_kind_ = arrow + 1;
            break;
        }
    }
}

// An item's checkbox ("checkbox" sprite shown: ticked when its "checkmark" is) and whether its value changes with Left /
// Right (left_arrow / right_arrow sprites in it, shown on the focused item).
//
// A slider item (sliderbar_add_item: the CAS colour menus' HUE / SAT. / VAL., the scaling menus' bars) has no left_arrow
// sprites: a bar picture and an up_arrow / down_arrow marker pair that its scripts slide along the bar (SetScreenElementProps
// pos=(x_val, 0), x_val between the menu script's arrow_left and arrow_right) after every change - Left / Right on the
// focused item are its pad_left / pad_right handlers. So the marker's x is the value.
void read_value_sprites(const UiWidget* element, mirror::ItemRead* item, int* budget)
{
    static const uint32_t kCheckbox = name_hash("checkbox");
    static const uint32_t kCheckmark = name_hash("checkmark");
    static const uint32_t kLeft = name_hash("left_arrow");
    static const uint32_t kRight = name_hash("right_arrow");
    static const uint32_t kUp = name_hash("up_arrow");
    static const uint32_t kStar = name_hash("star");
    static const uint32_t kCircle = name_hash("circle");
    for (const UiWidget* widget = element; widget && --*budget > 0; widget = get_next(widget)) {
        if (type_of(widget) == sprite_type()) {
            const uint32_t texture = core::down_cast<const ImageWidget>(widget)->texture();
            if (texture == kLeft || texture == kRight) {
                item->adjustable = true;
            }
            if (texture == kUp) {
                read_slider_marker(widget, item);
            }
            if (item->stat_index_ >= 0 && (texture == kStar || texture == kCircle)) {
                // A stat's star (build_stat_slider: ten of them; stats_menu_update_row lights the first `value` in
                // stats_menu_star_highlight [85 85 0], the rest stats_menu_star_lowlight [10 10 10] - the story's earned
                // but unspent ones lit at alpha 0.3)
                item->stat_value_ = std::max(item->stat_value_, 0);
                item->stat_cap_ = std::max(item->stat_cap_, 0);
                if (core::at(widget->aim_point().rgba, 0) >= kLitRedThreshold) {
                    ++item->stat_cap_;
                    if (widget->aim_point().alpha > kLitAlpha) {
                        ++item->stat_value_;
                    }
                }
            }
            if (texture == kCheckbox && get_visible(widget)) {
                item->checkbox = 0;
                for (const UiWidget* mark = first_child(widget); mark; mark = get_next(mark)) {
                    if (type_of(mark) == sprite_type() && get_visible(mark) &&
                        core::down_cast<const ImageWidget>(mark)->texture() == kCheckmark) {
                        item->checkbox = 1;
                    }
                }
            }
        }
        if (const UiWidget* child = first_child(widget)) {
            read_value_sprites(child, item, budget);
        }
    }
}

// The skater select's stats for the template `name`: stat_names' short_string (else string) and the template's value.
std::vector<std::pair<std::string, int>> skater_stats(uint32_t name)
{
    std::vector<std::pair<std::string, int>> stats;
    objects::ProfileRoster* manager = objects::profile_roster();
    objects::RiderIdentity* profile = manager ? manager->get_template(name) : nullptr;
    script::Array* names = script::fetch_list(name_hash("stat_names"));
    if (!profile || !names || names->kind_tag() != script::QbValueKind::kStruct) {
        return stats;
    }
    for (uint32_t i = 0; i < names->extent() && i < kMaxStatNames; ++i) {
        auto* entry = names->struct_at(i);
        uint32_t stat = 0;
        const char* label = nullptr;
        if (!entry || !entry->read_hash_value(name_hash("name"), &stat, 0)) {
            continue;
        }
        if (!entry->fetch_text(name_hash("short_string"), &label, 0)) {
            entry->fetch_text(name_hash("string"), &label, 0);
        }
        stats.emplace_back(label ? label : "?", profile->fetch_tally(stat));
    }
    return stats;
}

// A menu's item: its id, whether it takes the focus, its texts; its graphic and its value's sprites (not on the
// skater select, where it's the skater's name).
mirror::ItemRead read_menu_item(const UiWidget* child, bool skaters, bool selected, int& budget)
{
    mirror::ItemRead item;
    item.id = id_of_element(child);
    item.focusable = menu_reading::item_focusable(child);
    collect_texts(child, &item.texts, {}, &budget, false);
    if (!skaters && !selected) {
        int sprite_budget = kSpriteBudget;
        item.graphic_value_ = has_visible_sprite(child, &sprite_budget);
    }
    if (!skaters) {
        int value_budget = kValueBudget;
        if (const UiWidget* first = first_child(child)) {
            read_value_sprites(first, &item, &value_budget);
        }
    }
    if (skaters) {
        objects::ProfileRoster* manager = objects::profile_roster();
        objects::RiderIdentity* profile = manager ? manager->get_template(item.id) : nullptr;
        const char* name = profile ? profile->fetch_shown_name() : nullptr;
        item.display = name ? name : "";
    }
    return item;
}

// A dialog's title (the menu's first text) and text (the text block's lines, its children).
void read_dialog_texts(UiWidget* menu, mirror::MenuRead& read, int& budget)
{
    // The title: the menu's first text; the text: the text block's lines (its children).
    static const uint32_t kTextBlock = name_hash("TextBlockElement");
    for (UiWidget* child = first_child(menu); child; child = get_next(child)) {
        if (read.dialog_title_.empty() && type_of(child) == text_type()) {
            const char* text = core::down_cast<LabelWidget>(child)->chars();
            read.dialog_title_ = text ? text : "";
        } else if (type_of(child) == kTextBlock) {
            std::vector<std::string> lines;
            collect_texts(child, &lines, {}, &budget, false);
            std::string joined;
            for (const std::string& line : lines) {
                joined += (joined.empty() ? "" : " ") + line;
            }
            read.dialog_lines_.push_back(joined);
        }
    }
}

// The skater select's skater name and the selected skater's stats.
void read_skater_select(mirror::MenuRead& read)
{
    if (UiWidget* name = game_screens::get_find(name_hash("select_skater_name"))) {
        if (type_of(name) == text_type() && core::down_cast<LabelWidget>(name)->chars()) {
            read.skater_name_ = core::down_cast<LabelWidget>(name)->chars();
        }
    }
    if (read.selected_index_ >= 0 && std::cmp_less(read.selected_index_, read.items.size())) {
        read.stats = skater_stats(core::at(read.items, read.selected_index_).id);
    }
}

// A column beside the menu on its screen (the grab tricks' names beside their key combos): a word at a row's height
// is that row's value; the rest are the screen's other words.
void read_texts_beside(const UiWidget* screen, const std::vector<const UiWidget*>& skip, const UiWidget* menu,
                       mirror::MenuRead& read, int& budget)
{
    // A column beside the menu (the grab tricks' names beside their key combos): a word at a row's height is that
    // row's value; the rest are the screen's other words.
    std::vector<std::pair<std::string, float>> beside;
    collect_texts_at(screen, &beside, skip, &budget);
    std::vector<float> row_y;
    for (const UiWidget* child = first_child(menu); child; child = get_next(child)) {
        float y = absolute_y(child);
        for (const UiWidget* text_child = first_child(child); text_child; text_child = get_next(text_child)) {
            if (type_of(text_child) == text_type()) {
                y = absolute_y(text_child);
                break;
            }
        }
        row_y.push_back(y);
    }
    // (the nearest row, within half the rows' spacing)
    float spacing = kRowSpacing;
    for (size_t i = 1; i < row_y.size(); ++i) {
        if (core::at(row_y, i) - core::at(row_y, i - 1) > kRowSlack) {
            spacing = std::min(spacing, core::at(row_y, i) - core::at(row_y, i - 1));
        }
    }
    for (const auto& [text, y] : beside) {
        size_t row = row_y.size();
        float best = spacing * kHalf;
        for (size_t i = 0; i < row_y.size() && i < read.items.size(); ++i) {
            if (std::fabs(core::at(row_y, i) - y) < best && core::at(read.items, i).focusable) {
                best = std::fabs(core::at(row_y, i) - y);
                row = i;
            }
        }
        if (row < row_y.size()) {
            core::at(read.items, row).texts.push_back(text);
        } else {
            read.extra_texts_.push_back(text);
        }
    }
}

} // namespace
namespace internal {

mirror::MenuRead read_menu(UiWidget* menu, bool in_dialog)
{
    mirror::MenuRead read;
    if (!menu) {
        return read;
    }
    read.found = true;
    read.menu_id_ = id_of_element(menu);
    read.menu_key_ = menu;
    read.vertical = menu_reading::menu_runs_top_to_bottom(menu);
    read.selected_index_ = menu_reading::selected_index(menu);
    read.in_dialog_ = in_dialog;
    read.in_level_ = !in_front_end();
    read.graphic_editor_ = script::fetch_int_value("EG_in_create_a_graphic") == 1;
    static const uint32_t kSkaterMenu = name_hash("select_skater_hmenu");
    const bool skaters = read.menu_id_ == kSkaterMenu;
    int budget = kWalkBudget;
    int index = 0;
    for (const UiWidget* child = first_child(menu); child && index < kMaxMenuChildren; child = get_next(child), ++index) {
        read.items.push_back(read_menu_item(child, skaters, index == read.selected_index_, budget));
    }
    if (in_dialog) {
        read_dialog_texts(menu, read, budget);
        return read;
    }
    if (skaters) {
        read_skater_select(read);
        return read;
    }
    // A list: its title (title_text, the themed menus'), the other words on its screen (under the same child of the
    // root: not the menu, its title or the helper text).
    if (UiWidget* title = game_screens::get_find(name_hash("title_text"))) {
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars() && get_visible(title)) {
            read.title = core::down_cast<LabelWidget>(title)->chars();
        }
    }
    const UiWidget* root = root_element();
    const UiWidget* screen = menu;
    for (int guard = 0; screen && guard < kMaxParentDepth; ++guard) {
        const UiWidget* parent = screen->container().get_pull();
        if (!parent || parent == root) {
            break;
        }
        screen = parent;
    }
    std::vector<const UiWidget*> skip = {menu};
    for (const char* id : {"helper_text_anchor", "submenu_title_anchor", "title_text"}) {
        if (const UiWidget* widget = game_screens::get_find(name_hash(id))) {
            skip.push_back(widget);
        }
    }
    if (screen && screen != menu) {
        read_texts_beside(screen, skip, menu, read, budget);
    }
    return read;
}

} // namespace internal
namespace {

// --- the stats screen (EDIT STATS, the story's STATS) ---

// The stat a stats screen's row stands for (stats_menu_add_item: its id stats_menu_names_<n>); -1 for any other element.
int stat_row_index(uint32_t id)
{
    static const std::array<uint32_t, 16> kIds = [] {
        std::array<uint32_t, 16> ids{};
        for (size_t i = 0; i < ids.size(); ++i) {
            std::array<char, kStatNameSize> name{};
            text::format_to(name, "stats_menu_names_%u", static_cast<unsigned>(i));
            core::at(ids, i) = name_hash(name.data());
        }
        return ids;
    }();
    for (size_t i = 0; i < kIds.size(); ++i) {
        if (core::at(kIds, i) == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool has_stat_rows(const UiWidget* menu)
{
    for (const UiWidget* child = menu ? first_child(menu) : nullptr; child; child = get_next(child)) {
        if (stat_row_index(id_of_element(child)) >= 0) {
            return true;
        }
    }
    return false;
}

} // namespace
namespace internal {

// Whether the stats screen is up: its menu takes the pad (classic_stats_vmenu, the story's sub_vmenu / sub_vmenu2), or
// the story's stat is being changed (stats_menu_build_change_menu: its change_stat_sub_anchor takes the pad - no stats
// menu does then).
bool stats_screen_up(const UiWidget* menu)
{
    return has_stat_rows(menu) || game_screens::get_find(name_hash("change_stat_sub_anchor")) != nullptr;
}

// The stats screen read: every stat row of its menus (the story's two columns both), what the stars show, the row
// selected (the one with the pad, or the one being changed), the screen's other words. The game's scripts do the rest:
// Left / Right reach its own handlers (stats_change_menu_left / right, the story's column change), Back its pad_back.
mirror::MenuRead read_stats(UiWidget* active)
{
    mirror::MenuRead read;
    read.found = true;
    read.stats_screen_ = true;
    read.in_level_ = !in_front_end();
    const UiWidget* editing = game_screens::get_find(name_hash("change_stat_sub_anchor"));
    read.stats_editing_ = editing != nullptr;
    const UiWidget* editing_row = editing ? editing->container().get_pull() : nullptr;
    static const uint32_t kColumn1 = name_hash("sub_vmenu");
    static const uint32_t kColumn2 = name_hash("sub_vmenu2");
    std::vector<const UiWidget*> menus;
    if (has_stat_rows(active) && id_of_element(active) != kColumn1 && id_of_element(active) != kColumn2) {
        menus.push_back(active);
    } else {
        for (const uint32_t column : {kColumn1, kColumn2}) {
            if (const UiWidget* menu = game_screens::get_find(column); menu && has_stat_rows(menu)) {
                menus.push_back(menu);
            }
        }
    }
    if (menus.empty()) {
        return read;
    }
    read.menu_id_ = id_of_element(menus.front());
    read.menu_key_ = menus.front();
    int budget = kWalkBudget;
    int selected_stat = -1;
    for (size_t column = 0; column < menus.size(); ++column) {
        const UiWidget* menu = core::at(menus, column);
        const bool takes_pad = menu == active && (editing == nullptr);
        const int selected = takes_pad ? menu_reading::selected_index(menu) : -1;
        int index = 0;
        for (const UiWidget* child = first_child(menu); child && index < kMaxKeyboardChildren; child = get_next(child), ++index) {
            const int stat = stat_row_index(id_of_element(child));
            if (stat < 0) {
                continue;
            }
            mirror::ItemRead item;
            item.id = id_of_element(child);
            item.focusable = menu_reading::item_focusable(child);
            item.stat_index_ = stat;
            item.stat_column_ = static_cast<int>(column);
            item.stat_child_ = takes_pad ? index : -1;
            collect_texts(child, &item.texts, {}, &budget, false);
            int value_budget = kValueBudget;
            if (const UiWidget* first = first_child(child)) {
                read_value_sprites(first, &item, &value_budget);
            }
            if ((takes_pad && index == selected) || child == editing_row) {
                selected_stat = stat;
            }
            read.items.push_back(std::move(item));
        }
    }
    std::ranges::stable_sort(read.items, {}, &mirror::ItemRead::stat_index_);
    for (size_t i = 0; i < read.items.size(); ++i) {
        if (core::at(read.items, i).stat_index_ == selected_stat) {
            read.stats_selected_ = static_cast<int>(i);
        }
    }
    // The other words: the screen's texts (the points left, the description, the story's goals) - the stats menus and the
    // helper text left out.
    if (UiWidget* title = game_screens::get_find(name_hash("title_text"))) {
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars() && get_visible(title)) {
            read.title = core::down_cast<LabelWidget>(title)->chars();
        }
    }
    const UiWidget* root = root_element();
    const UiWidget* screen = menus.front();
    for (int guard = 0; screen && guard < kMaxParentDepth; ++guard) {
        const UiWidget* parent = screen->container().get_pull();
        if (!parent || parent == root) {
            break;
        }
        screen = parent;
    }
    std::vector<const UiWidget*> skip(menus.begin(), menus.end());
    for (const char* id : {"helper_text_anchor", "title_text"}) {
        if (const UiWidget* widget = game_screens::get_find(name_hash(id))) {
            skip.push_back(widget);
        }
    }
    if (screen) {
        collect_texts(screen, &read.extra_texts_, skip, &budget, true);
    }
    return read;
}

} // namespace internal
namespace {

// [frontend] debug_restyle=1: a menu left as it is (not restyled) logged once with its screen's element tree - what
// the reader would have to understand to restyle it (each element: type, id, text, texture, position, hidden).
const char* current_type_name(uint32_t type)
{
    static const auto kTypes = std::to_array<const char*>({
        "TextElement",    "SpriteElement",    "ContainerElement", "VMenu",         "HMenu", "VScrollingMenu",
        "HScrollingMenu", "TextBlockElement", "Element3d",        "WindowElement", "Menu"});
    for (const char* name : kTypes) {
        if (name_hash(name) == type) {
            return name;
        }
    }
    return nullptr;
}

void dump_tree(const UiWidget* element, int depth, int* budget)
{
    for (; element && --*budget > 0; element = get_next(element)) {
        std::array<char, kLogLineSize> line{};
        const char* type = current_type_name(type_of(element));
        const size_t at =
            text::format_to(line, "%*s%s %08X (%.0f, %.0f)%s", depth * 2, "", type ? type : "?", id_of_element(element),
                            element->aim_point().x, element->aim_point().y, get_visible(element) ? "" : " hidden");
        if (type_of(element) == text_type()) {
            const char* text = core::down_cast<const LabelWidget>(element)->chars();
            const std::span<char> rest = std::span<char>(line).subspan(at);
            text::format_to(rest, " \"%.80s\"", text ? text : "");
        } else if (type_of(element) == sprite_type()) {
            text::format_to(std::span<char>(line).subspan(at), " texture %08X",
                            core::down_cast<const ImageWidget>(element)->texture());
        }
        get_log("new front end: debug_restyle %s", line.data());
        if (depth < kMaxDumpDepth) {
            dump_tree(first_child(element), depth + 1, budget);
        }
    }
}

} // namespace
namespace internal {

void dump_left_menu(UiWidget* menu, const std::string& why)
{
    static const bool kOn = GetPrivateProfileIntA("frontend", "debug_restyle", 0, ini_path().c_str()) != 0;
    // (each menu once by what it holds: menus share ids and are rebuilt in the same element)
    static std::set<std::string> logged;
    std::vector<std::string> texts;
    int text_budget = kTextBudget;
    collect_texts(menu, &texts, {}, &text_budget, false);
    std::string key = std::to_string(id_of_element(menu));
    for (size_t i = 0; i < texts.size() && i < kMaxTexts; ++i) {
        key += "|" + core::at(texts, i);
    }
    if (!kOn || (menu == nullptr) || !logged.insert(key).second) {
        return;
    }
    const UiWidget* root = root_element();
    const UiWidget* screen = menu;
    for (int guard = 0; screen && guard < kMaxParentDepth; ++guard) {
        const UiWidget* parent = screen->container().get_pull();
        if (!parent || parent == root) {
            break;
        }
        screen = parent;
    }
    get_log("new front end: debug_restyle - the menu %08X (%s), its screen %08X:", id_of_element(menu), why.c_str(),
            id_of_element(screen));
    int budget = kScreenBudget;
    const UiWidget* one = screen;
    std::array<char, kTextLineSize> line{};
    text::format_to(line, "%s %08X", current_type_name(type_of(one)) ? current_type_name(type_of(one)) : "?",
                    id_of_element(one));
    get_log("new front end: debug_restyle %s", line.data());
    dump_tree(first_child(screen), 1, &budget);
}

} // namespace internal
namespace {

uint32_t v_menu_type()
{
    static const uint32_t kType = name_hash("VMenu");
    return kType;
}
uint32_t h_menu_type()
{
    static const uint32_t kType = name_hash("HMenu");
    return kType;
}

} // namespace
namespace internal {

// The on-screen keyboard around `menu` (a row of keys - an HMenu - in a VMenu of such rows): the VMenu, else null.
UiWidget* keyboard_of(UiWidget* menu)
{
    if (!menu || type_of(menu) != h_menu_type()) {
        return nullptr;
    }
    UiWidget* parent = menu->container().get_pull();
    if (!parent || type_of(parent) != v_menu_type()) {
        return nullptr;
    }
    int rows = 0;
    int singles = 0;
    for (const UiWidget* row = first_child(parent); row; row = get_next(row)) {
        if (type_of(row) != h_menu_type()) {
            continue;
        }
        ++rows;
        for (const UiWidget* key = first_child(row); key; key = get_next(key)) {
            std::vector<std::string> texts;
            int budget = kKeyTextBudget;
            collect_texts(key, &texts, {}, &budget, false);
            for (const std::string& text : texts) {
                singles += text.size() == 1 ? 1 : 0;
            }
        }
    }
    return rows >= 3 && singles >= kMinSingles ? parent : nullptr;
}

// The keyboard read: its rows of keys (a key's words: the last that isn't a button tag, e.g. "Backspace"), the key
// selected, the text typed (the first row that isn't keys - its text, the cursor's "_" left in). `active`: the menu
// whose keyboard it is (keyboard_of).
mirror::MenuRead read_keyboard(UiWidget* active)
{
    const UiWidget* keys = keyboard_of(active);
    mirror::MenuRead read;
    read.found = true;
    read.keyboard = true;
    read.menu_id_ = id_of_element(keys);
    read.menu_key_ = keys;
    read.in_level_ = !in_front_end();
    int row = 0;
    bool entry_read = false;
    for (const UiWidget* child = first_child(keys); child; child = get_next(child)) {
        if (type_of(child) != h_menu_type()) {
            if (!entry_read) {
                std::vector<std::string> texts;
                int budget = kChildTextBudget;
                collect_texts(child, &texts, {}, &budget, false);
                if (!texts.empty()) {
                    read.entry = texts.front();
                }
                entry_read = true;
            }
            continue;
        }
        std::vector<std::string> key_row;
        for (const UiWidget* key = first_child(child); key; key = get_next(key)) {
            std::vector<std::string> texts;
            int budget = kKeyTextBudget; // (a wide key: a container of a score of cap sprites before its words)
            collect_texts(key, &texts, {}, &budget, false);
            std::string label;
            for (const std::string& text : texts) {
                if (!(text.size() >= 2 && core::at(text, 0) == '\\')) {
                    label = text;
                }
            }
            key_row.push_back(label);
        }
        if (child == active) {
            read.key_row_ = row;
            read.key_col_ = menu_reading::selected_index(active);
        }
        read.keys.push_back(std::move(key_row));
        ++row;
    }
    if (UiWidget* title = game_screens::get_find(name_hash("title_text"))) {
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars() && get_visible(title)) {
            read.title = core::down_cast<LabelWidget>(title)->chars();
        }
    }
    return read;
}

// A button tag as the game spells it (gfx/text.cpp's): the explicit buttons through the controller map when one is in
// use, the meta buttons through the meta map; the arrow glyphs Up / Down / Left / Right, else the key's name.
std::string pad_button_label_for(mirror::ButtonTag button_tag)
{
    const char tag = button_tag.tag;
    const char digit = button_tag.digit;
    int button = 0;
    if (tag == 'm' || tag == 'M') {
        button = text::hex_digit_value(render::button_glyph(digit));
    } else {
        button = static_cast<int>(render::button_for_tag(digit));
    }
    switch (button) {
    case kButtonDown:
        return text_tools::kKeyDown;
    case kButtonRight:
        return text_tools::kKeyRight;
    case kButtonLeft:
        return text_tools::kKeyLeft;
    case kButtonUp:
        return text_tools::kKeyUp;
    case kButtonDownRight:
        return text_tools::
            kKeyDownRight; // (the grab tricks' order: Down, Down-Left \bc, Down-Right \ba, ... Up-Left \bb,
    case kButtonUpLeft:
        return text_tools::kKeyUpLeft; // Up-Right \bd)
    case kButtonDownLeft:
        return text_tools::kKeyDownLeft;
    case kButtonUpRight:
        return text_tools::kKeyUpRight;
    default:
        break;
    }
    const char* name = sio::get_button_key_name(button);
    return name ? name : "";
}

} // namespace internal
} // namespace frontend::new_frontend
