// The menus (vmenu / hmenu) and the container element: see menu_element.h.

#include "frontend/menu_element.h"
#include "frontend/screen_element_manager.h"

#include <algorithm>
#include <cstdint>
#include "engine/objects/signal.h"
#include "engine/objects/tracker.h"
#include "engine/script/param_struct.h"
#include "game/skate/contest.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec2.h"
namespace frontend {

namespace {

constexpr double kHalf = 0.5;

using script::Struct;

// Names (checksums).
constexpr uint32_t kVMenu = 0x130EF802, kHMenu = 0xCCDED1E1, kContainerElement = 0x5B9DA842;
constexpr uint32_t kInnerAlign = 0x67E093E4, kRegularSpaceAmount = 0xF24ADEDB, kInsetFactor = 0x6D853B88;
constexpr uint32_t kItemSpacing = 0x4B4E14AB, kEnablePadHandling = 0x4BD08BFE, kDisablePadHandling = 0xC22541CC;
constexpr uint32_t kWrapAround = 0x43A7692D, kDontAllowWrap = 0x5DEA5C64, kFocusableChild = 0xF1822B9C;
constexpr uint32_t kTagSelectedId = 0xAC988EBE, kTagSelectedIndex = 0xF2239871, kTagGridIndex = 0x8321DD71;
constexpr uint32_t kTagGridX = 0x5B92F8DD, kTagNotFocusable = 0xF33A3321;
constexpr uint32_t kController = 0xB30D9965, kGridIndex = 0xFACF9A8B, kChildId = 0x229D3DE4;
// Events.
constexpr uint32_t kSystem = 0x36B2EE74, kFocus = 0x9D3FB516, kUnfocus = 0x4ADF0CD3, kFocusSentEvent = 0x3056434F;
constexpr uint32_t kPadUp = 0x30A4F836, kPadDown = 0x0FD1AC26, kPadLeft = 0x6949DB75, kPadRight = 0x05DDD87C;

float regular_space_minimum() { return ::modules::kCompetitionTiny; } // 0.0001

void get_launch(uint32_t type, uint32_t target, Struct* data)
{
    objects::get_tracker()->launch_event(type, target, kSystem, data, false, -1.0F);
}

// The screen element manager (0x00701440) and its pieces.

UiTree* get_elements(void* manager) { return static_cast<UiTree*>(manager); }
bool uses_priorities(void* manager) { return get_elements(manager)->has_priorities(); }
bool is_priority_event(void* manager, uint32_t type) { return get_elements(manager)->is_pad_event(type); }
bool has_top_precedence(void* manager, uint32_t id) { return get_elements(manager)->has_top_precedence(id); }
ElementPtr* get_look_up(void* manager, ElementPtr* out, uint32_t id, int assert)
{
    return get_elements(manager)->fetch_part(out, id, assert);
}

// focus to `id` with the controller (and a grid index, unless -1).
void send_focus(checksums::Name item, int controller, int grid_index)
{
    const uint32_t id = item.value();
    Struct data;
    data.put_int(checksums::Name{kController}, controller);
    if (grid_index != -1)
        data.put_int(checksums::Name{kGridIndex}, grid_index);
    get_launch(kFocus, id, &data);
    data.get_clear();
}

} // namespace

// --- the menu -------------------------------------------------------------------------------------------------

// 0x004A74E0 (0x004A75C0 / 0x004A7630 the vmenu's / hmenu's: then vertical_ and the type)
MenuBase::MenuBase(bool vertical) : vertical_(vertical) { change_species(vertical ? kVMenu : kHMenu); }

// 0x004A7550
int MenuBase::get_regular_space(float* out) const
{
    *out = regular_space_;
    return regular_space_ > regular_space_minimum() ? 1 : 0;
}

// 0x004A7580
void MenuBase::set_selection_tags()
{
    write_hash_tag(kTagSelectedId, chosen_item_id_);
    store_int_tag(kTagSelectedIndex, selected_index_);
    store_int_tag(kTagGridIndex, grid_index_);
}

// 0x004A76F0
void MenuBase::change_traits(script::Struct* params)
{
    maths::Vec2 align{.x = 0.0F, .y = 0.0F};
    if (read_alignment_param(params, kInnerAlign, &align)) {
        inner_align_h_ = align.x;
        inner_align_v_ = align.y;
    }
    int space = 0;
    if (params->fetch_int_value(kRegularSpaceAmount, &space, 0))
        regular_space_ = static_cast<float>(space);
    params->fetch_float(kInsetFactor, &margin_factor_, 0);
    int between = 0;
    if (params->fetch_int_value(kItemSpacing, &between, 0))
        item_gap_ = static_cast<float>(between);
    if (params->has_bare_flag(kEnablePadHandling))
        pad_handling_ = true;
    else if (params->has_bare_flag(kDisablePadHandling))
        pad_handling_ = false;
    if (params->has_bare_flag(kWrapAround))
        cycles_around_ = true;
    else if (params->has_bare_flag(kDontAllowWrap))
        cycles_around_ = false;
    UiWidget::change_traits(params);
}

// 0x004A7850 (each item gets two chances: twice the item count of tries)
void MenuBase::move_selection(int direction, bool stay, bool grid_x)
{
    int tries = child_total() * 2;
    selected_index_ = std::max(selected_index_, 0);
    while (tries != 0) {
        if (!stay) {
            const int index = selected_index_ + direction;
            selected_index_ = index;
            if (index >= child_total()) {
                if (cycles_around_) {
                    selected_index_ = 0;
                } else {
                    direction = -direction;
                    selected_index_ = child_total() - 1;
                }
            } else if (index < 0) {
                if (cycles_around_) {
                    selected_index_ = child_total() - 1;
                } else {
                    direction = -direction;
                    selected_index_ = 0;
                }
            }
        }
        stay = false;
        ElementPtr item;
        item.assign_value(find_child_at_slot(selected_index_));
        chosen_item_id_ = item->ident();
        if (grid_x) {
            grid_index_ = -1;
            item->read_int_tag(kTagGridX, &grid_index_);
        }
        if (!item->contains_tag_flag(kTagNotFocusable) && !item->input_suppressed()) {
            item.get_release();
            if (tries == 0)
                break;
            set_selection_tags();
            return;
        }
        --tries;
    }
    chosen_item_id_ = 0;
    selected_index_ = -1;
}

// 0x004A7A40
void MenuBase::get_step(int direction)
{
    int grid_index = -1;
    if (chosen_item_id_) {
        UiWidget* item = nullptr;
        {
            ElementPtr found;
            item = get_look_up(UiTree::sole(), &found, chosen_item_id_, 0)->get_pull();
        }
        if (item)
            item->read_int_tag(kTagGridIndex, &grid_index);
        get_launch(kUnfocus, chosen_item_id_, nullptr);
    }
    move_selection(direction, false, true);
    if (chosen_item_id_)
        send_focus(checksums::Name{chosen_item_id_}, controller_, grid_index);
}

// 0x004A7B80
void MenuBase::arrange()
{
    float total = 0.0F;
    float largest = 0.0F;
    const bool regular = regular_space_ > regular_space_minimum();
    {
        ElementPtr item;
        item.assign_value(eldest().get_pull());
        if (regular) {
            for (; item; item.assign_value(item->following().get_pull())) {
                const double across = vertical_ ? static_cast<double>(item->span()) * item->shown().scale_x_
                                                : static_cast<double>(item->level_y()) * item->shown().scale_y_;
                total = static_cast<float>(static_cast<double>(total) + regular_space_);
                if (across > largest)
                    largest = static_cast<float>(across);
            }
        } else {
            for (; item; item.assign_value(item->following().get_pull())) {
                double across = 0.0;
                double along = 0.0;
                if (vertical_) {
                    across = static_cast<double>(item->span()) * margin_factor_;
                    along = item->level_y();
                } else {
                    across = static_cast<double>(item->level_y()) * margin_factor_;
                    along = item->span();
                }
                total = static_cast<float>((along * margin_factor_) + item_gap_ + total);
                if (across > largest)
                    largest = static_cast<float>(across);
            }
            total = static_cast<float>(static_cast<double>(total) - item_gap_);
        }
    }
    if (object_flags() & kDimsLocked) {
        // the size only grows
        if (vertical_) {
            if (total > level_y())
                set_level_y(total);
        } else if (total > span()) {
            set_span(total);
        }
    } else if (vertical_) {
        set_span(largest);
        set_level_y(total);
    } else {
        set_level_y(largest);
        set_span(total);
    }
    place_top_left(&aim_point());
    object_flags() |= kDirty;
    // where the first item goes: the free space split by internal_just
    float offset = vertical_ ? static_cast<float>((static_cast<double>(level_y()) - total) *
                                                  (static_cast<double>(inner_align_v_) + 1.0) * kHalf)
                             : static_cast<float>((static_cast<double>(span()) - total) *
                                                  (static_cast<double>(inner_align_h_) + 1.0) * kHalf);
    {
        ElementPtr item;
        item.assign_value(eldest().get_pull());
        if (regular) {
            offset = static_cast<float>((static_cast<double>(regular_space_) * kHalf) + offset);
            for (; item; item.assign_value(item->following().get_pull())) {
                if (vertical_) {
                    item->change_alignment(inner_align_h_, 0.0F);
                    item->change_location(static_cast<float>((static_cast<double>(inner_align_h_) + 1.0) * span() * kHalf),
                                          offset, true);
                } else {
                    item->change_alignment(0.0F, inner_align_v_);
                    item->change_location(
                        offset, static_cast<float>((static_cast<double>(inner_align_v_) + 1.0) * level_y() * kHalf), true);
                }
                offset = static_cast<float>(static_cast<double>(offset) + regular_space_);
            }
        } else {
            for (; item; item.assign_value(item->following().get_pull())) {
                // half the item's padded length; the offset moves by the unrounded half, then by the rounded one
                const double half =
                    (vertical_ ? static_cast<double>(item->level_y()) : static_cast<double>(item->span())) *
                    margin_factor_ * 0.5;
                auto half_stored = static_cast<float>(half);
                offset = static_cast<float>(half + offset);
                if (vertical_) {
                    item->change_alignment(inner_align_h_, 0.0F);
                    item->change_location(static_cast<float>((static_cast<double>(inner_align_h_) + 1.0) * span() * kHalf),
                                          offset, true);
                } else {
                    item->change_alignment(0.0F, inner_align_v_);
                    item->change_location(
                        offset, static_cast<float>((static_cast<double>(inner_align_v_) + 1.0) * level_y() * kHalf), true);
                }
                offset = static_cast<float>(static_cast<double>(half_stored) + offset + item_gap_);
                if (item->object_flags() & kDirty)
                    object_flags() |= kDirty;
            }
        }
    }
    if (chosen_item_id_ == 0 && eldest() && focused_) {
        selected_index_ = 0;
        move_selection(1, true, true);
        if (chosen_item_id_)
            send_focus(checksums::Name{chosen_item_id_}, controller_, -1);
    }
}

// 0x004A8100
bool MenuBase::pass_event_to_script(objects::Signal* event)
{
    void* manager = park_owner();
    if (manager_uses_priorities() && is_priority_event(manager, event->kind_tag())) {
        if (!has_priority() || !has_top_precedence(manager, ident()))
            return false;
    }
    if (!objects::Entity::pass_event_to_script(event))
        return false;
    if (event->kind_tag() == kFocusSentEvent) {
        // find the selected item again (by id, else by index), lay the items out
        bool refocus = false;
        if (chosen_item_id_) {
            int index = 0;
            ElementPtr item;
            item.assign_value(find_child_with_id(chosen_item_id_, &index));
            if (!item) {
                ElementPtr by_index;
                by_index.assign_value(find_child_at_slot(selected_index_));
                if (by_index) {
                    chosen_item_id_ = by_index->ident();
                    set_selection_tags();
                } else {
                    selected_index_ = -1;
                    move_selection(1, true, true);
                }
                refocus = true;
            } else {
                selected_index_ = index;
                set_selection_tags();
            }
        } else {
            move_selection(1, true, true);
            refocus = true;
        }
        if (refocus && focused_) {
            move_selection(1, true, true);
            if (chosen_item_id_)
                send_focus(checksums::Name{chosen_item_id_}, controller_, -1);
        }
        arrange();
        event->set_event_handled(ident(), 0);
    }
    if (event->kind_tag() == kFocus && !focused_) {
        controller_ = objects::event_controller(event);
        // the item to focus: grid_index (the last item whose tag_grid_x is at most it), child_id
        int grid = -1;
        if (event->payload() && event->payload()->fetch_int_value(kGridIndex, &grid, 0)) {
            grid_index_ = grid;
            ElementPtr item;
            item.assign_value(eldest().get_pull());
            int found = -1;
            for (int i = 0; item; ++i) {
                int x = -1;
                if (item->read_int_tag(kTagGridX, &x) && x <= grid)
                    found = i;
                item.assign_value(item->following().get_pull());
            }
            if (found != -1) {
                selected_index_ = found;
                chosen_item_id_ = 1;
            }
        }
        uint32_t child_id = 0;
        if (event->payload() && event->payload()->read_hash_value(kChildId, &child_id, 0)) {
            ElementPtr item;
            item.assign_value(eldest().get_pull());
            for (int i = 0; item; ++i) {
                if (child_id == item->ident()) {
                    chosen_item_id_ = child_id;
                    selected_index_ = i;
                    break;
                }
                item.assign_value(item->following().get_pull());
            }
        }
        focused_ = true;
        if (chosen_item_id_) {
            move_selection(1, true, false);
            if (chosen_item_id_)
                get_launch(kFocus, chosen_item_id_, event->payload());
        }
        event->set_event_handled(ident(), 0);
    }
    if (event->kind_tag() == kUnfocus && focused_) {
        focused_ = false;
        if (chosen_item_id_)
            get_launch(kUnfocus, chosen_item_id_, nullptr);
        event->set_event_handled(ident(), 0);
    }
    uint32_t type = event->kind_tag();
    if (((type == kPadUp && vertical_) || (type == kPadLeft && !vertical_)) && focused_ && pad_handling_) {
        get_step(-1);
        event->set_event_handled(ident(), 0);
    }
    type = event->kind_tag();
    if (((type == kPadDown && vertical_) || (type == kPadRight && !vertical_)) && focused_ && pad_handling_) {
        get_step(1);
        event->set_event_handled(ident(), 0);
    }
    return true;
}

// --- the container element ------------------------------------------------------------------------------------

// 0x004AAC20
GroupWidget::GroupWidget() { change_species(kContainerElement); }

// 0x004AAC80
GroupWidget::~GroupWidget() = default;

// 0x004AA530
void GroupWidget::change_traits(script::Struct* params)
{
    params->read_hash_value(kFocusableChild, &focusable_child_, 0);
    UiWidget::change_traits(params);
}

// 0x004AA560
bool GroupWidget::pass_event_to_script(objects::Signal* event)
{
    void* manager = park_owner();
    if (uses_priorities(manager) && is_priority_event(manager, event->kind_tag())) {
        if (!has_priority() || !has_top_precedence(manager, ident()))
            return false;
    }
    if (!objects::Entity::pass_event_to_script(event))
        return false;
    if (event->kind_tag() == kFocus || event->kind_tag() == kUnfocus) {
        if (focusable_child_) {
            Struct data;
            data.put_int(checksums::Name{kController}, objects::event_controller(event));
            get_launch(event->kind_tag(), focusable_child_, &data);
            data.get_clear();
        }
        event->set_event_handled(ident(), 0);
    }
    return true;
}

} // namespace frontend

