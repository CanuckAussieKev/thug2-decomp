// NOT ORIGINAL - reading the original menus' screen elements (menu_reading.h): ours, from the mouse_menus feature.

#include "engine/ui/menu_reading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "base/maths/vec2.h"
#include "engine/gfx/renderer.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/ui/menu_element.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_element_manager.h"

namespace frontend::menu_reading {

namespace {

constexpr double kTurnTolerance = 0.001;
constexpr float kEdgeFraction = 0.3F; // the part of an adjustable item at each end that steps left or right

// Element types (Entity::kind_tag_) and names.
constexpr uint32_t kVMenu = 0x130EF802;             // "vmenu"
constexpr uint32_t kHMenu = 0xCCDED1E1;             // "hmenu"
constexpr uint32_t kWindowElement = 0xA13ADBB7;     // "windowelement" (a clipping viewport, 0x004B2060)
constexpr uint32_t kVScrollingMenu = 0x84298A3C;    // "vscrollingmenu"
constexpr uint32_t kHScrollingMenu = 0x2900E3D0;    // "hscrollingmenu"
constexpr uint32_t kTagNotFocusable = 0xF33A3321;   // "tag_not_focusable" (set by not_focusable)
constexpr uint32_t kTagBlank = 0xC3D43B9A;          // "blank" (make_spin_menu_item's filler items)
constexpr uint32_t kTagActive = 0xB4E103FD;         // "active" (make_spin_menu_item: 0 = disabled)

// The menus' pad events and how input_logic launches them (front_end.cpp).
constexpr uint32_t kPadChoose = 0x1E3E253B;  // "pad_choose"

// UiWidget's flags (+0x08).
constexpr uint32_t kFlagBlockEvents = 0x200000; // block_events
constexpr uint32_t kFlagHidden = 0x400000;      // hide
constexpr uint32_t kFlagPriority = 0x10000000;  // priority > 0 (pad events with priorities: 0x004A8F20)

// An element below this alpha (its summed alpha, its parents' included) isn't seen.
constexpr float kMinAlpha = 0.01F;

const objects::Entity* as_object(const UiWidget* element) { return static_cast<const objects::Entity*>(element); }
uint32_t current_flags(const UiWidget* element) { return as_object(element)->object_flags(); }
uint32_t kind_tag(const UiWidget* element) { return as_object(element)->kind_tag(); }
uint32_t ident_no(const UiWidget* element) { return as_object(element)->ident(); }
UiWidget* current_parent(const UiWidget* element) { return element->container().get_pull(); }
UiWidget* first_child(const UiWidget* element) { return element->eldest().get_pull(); }
UiWidget* next_sibling(const UiWidget* element) { return element->following().get_pull(); }
float get_alpha(const UiWidget* element) { return element->summed().alpha; }
float bearing(const UiWidget* element) { return element->summed().angle; }

// The flag on the element or a parent.
bool flag_in_chain(const UiWidget* element, uint32_t flag)
{
    for (; element; element = current_parent(element)) {
        if ((current_flags(element) & flag) != 0U) {
            return true;
        }
    }
    return false;
}

bool clips_children(const UiWidget* element)
{
    const uint32_t type = kind_tag(element);
    return type == kWindowElement || type == kVScrollingMenu || type == kHScrollingMenu;
}

Rect2 intersect(const Rect2& a, const Rect2& b)
{
    return {.left = std::max(a.left, b.left), .top = std::max(a.top, b.top), .right = std::min(a.right, b.right), .bottom = std::min(a.bottom, b.bottom)};
}

Rect2 union_of(const Rect2& a, const Rect2& b)
{
    return {.left = std::min(a.left, b.left), .top = std::min(a.top, b.top), .right = std::max(a.right, b.right), .bottom = std::max(a.bottom, b.bottom)};
}

// The rectangle of an unturned box (right / bottom past left / top).
Rect2 box_rect(const Cuboid& box)
{
    Rect2 rect = {.left = box.x, .top = box.y, .right = box.x + box.width, .bottom = box.y + box.height};
    if (rect.right < rect.left) {
        std::swap(rect.left, rect.right);
    }
    if (rect.bottom < rect.top) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

// The rectangle around a turned box's corners.
Rect2 turned_box_rect(const Cuboid& box)
{
    const double cosine = std::cos(box.angle);
    const double sine = std::sin(box.angle);
    Rect2 rect = {.left = box.x, .top = box.y, .right = box.x, .bottom = box.y};
    const std::array<std::array<float, 2>, 3> corners = {
        {{box.width, 0.0F}, {0.0F, box.height}, {box.width, box.height}}};
    for (const auto& corner : corners) {
        auto x = static_cast<float>(box.x + (cosine * core::at(corner, 0)) - (sine * core::at(corner, 1)));
        auto y = static_cast<float>(box.y + (sine * core::at(corner, 0)) + (cosine * core::at(corner, 1)));
        rect = union_of(rect, Rect2{.left = x, .top = y, .right = x, .bottom = y});
    }
    return rect;
}

// Each part of an item: itself, then everything below it that isn't hidden (nor below a hidden one).
template <typename F>
void for_each_part(const UiWidget* element, F& visit, bool item)
{
    if (!item && (current_flags(element) & kFlagHidden)) {
        return;
    }
    visit(element, item);
    for (const UiWidget* child = first_child(element); child; child = next_sibling(child)) {
        for_each_part(child, visit, false);
    }
}

// The rectangle around the item's unturned parts (false: none - with `turned_in_bounds` the turned ones' too), and
// whether a turned part contains (x, y).
bool item_shape(const UiWidget* item, float x, float y, bool turned_in_bounds, Rect2* bounds, bool* turned_hit)
{
    bool any = false;
    *turned_hit = false;
    auto visit = [&](const UiWidget* part, bool is_item) {
        const Cuboid box = element_box(part);
        if (!is_item && (box.width == 0.0F || box.height == 0.0F)) {
            return; // (an empty container somewhere: nothing to point at)
        }
        Rect2 rect{};
        if (get_rotated(box.angle)) {
            if (box_contains(box, {.x = x, .y = y})) {
                *turned_hit = true;
            }
            if (!turned_in_bounds) {
                return;
            }
            rect = turned_box_rect(box);
        } else {
            rect = box_rect(box);
        }
        *bounds = any ? union_of(*bounds, rect) : rect;
        any = true;
    };
    for_each_part(item, visit, true);
    return any;
}

// Across its menu (a vertical menu's width, a horizontal one's height) - unless the menu is turned.
Rect2 widen(Rect2 area, const UiWidget* menu)
{
    if (get_rotated(bearing(menu))) {
        return area;
    }
    const Rect2 span = element_rect(menu);
    if (menu_runs_top_to_bottom(menu)) {
        if (span.right > span.left) {
            area.left = std::min(area.left, span.left);
            area.right = std::max(area.right, span.right);
        }
    } else if (span.bottom > span.top) {
        area.top = std::min(area.top, span.top);
        area.bottom = std::max(area.bottom, span.bottom);
    }
    return area;
}

struct Hunt {
    float x, y;
    MenuFilter accepts;
    bool vertical_only_; // (any_menu)
    bool found;
    Hit hit;
};

// Pre-order: a later find (deeper, or later in the tree) replaces an earlier one.
void get_visit(Hunt& search, UiWidget* element, bool parent_accepts, int index, const Rect2& clip, bool clipped)
{
    if ((current_flags(element) & kFlagHidden) != 0U) {
        return;
    }
    if (get_alpha(element) < kMinAlpha) {
        return;
    }
    UiWidget* parent = current_parent(element);
    if (parent_accepts && !item_passed_over(element) && item_focusable(element)) {
        const Hit candidate{.menu = parent, .item = element, .index = index};
        if (item_contains(candidate, {.x = search.x, .y = search.y}) && (!clipped || get_holds(clip, search.x, search.y))) {
            search.found = true;
            search.hit = candidate;
        }
    }
    Rect2 child_clip = clip;
    bool child_clipped = clipped;
    if (clips_children(element)) {
        const Rect2 own = element_rect(element);
        if (own.right > own.left && own.bottom > own.top) {
            child_clip = clipped ? intersect(clip, own) : own;
            child_clipped = true;
        }
    }
    const bool accepts = is_menu(element) && search.accepts(element);
    int child_index = 0;
    for (UiWidget* child = first_child(element); child; child = next_sibling(child), ++child_index) {
        get_visit(search, child, accepts, child_index, child_clip, child_clipped);
    }
}

bool find_menu(UiWidget* element, MenuFilter accepts, bool vertical_only)
{
    if ((current_flags(element) & kFlagHidden) != 0U) {
        return false;
    }
    if (is_menu(element) && (!vertical_only || menu_runs_top_to_bottom(element)) && accepts(element)) {
        return true;
    }
    for (UiWidget* child = first_child(element); child; child = next_sibling(child)) {
        if (find_menu(child, accepts, vertical_only)) {
            return true;
        }
    }
    return false;
}

} // namespace


ScreenMapping current_mapping(WindowSize window)
{
    const int window_width = window.width;
    const int window_height = window.height;
    ScreenMapping mapping{};
    mapping.window_width_ = window_width;
    mapping.window_height_ = window_height;
    mapping.back_buffer_width_ = static_cast<uint32_t>(render::renderer().back_buffer_width());
    mapping.back_buffer_height_ = static_cast<uint32_t>(render::renderer().back_buffer_height());
    mapping.scale_x_ = render::renderer().display_factor_x();
    mapping.scale_y_ = render::renderer().display_factor_y();
    mapping.offset_x_ = static_cast<int32_t>(render::renderer().screen_offset_x());
    mapping.offset_y_ = static_cast<int32_t>(render::renderer().screen_offset_y());
    return mapping;
}

bool window_to_layout(const ScreenMapping& mapping, float window_x, float window_y, float* layout_x, float* layout_y)
{
    if (mapping.window_width_ <= 0 || mapping.window_height_ <= 0 || mapping.back_buffer_width_ == 0 ||
        mapping.back_buffer_height_ == 0 || mapping.scale_x_ == 0.0F || mapping.scale_y_ == 0.0F) {
        return false;
    }
    // Window -> back buffer (the window may be scaled to show it), then back buffer -> layout (the inverse of
    // back buffer = layout * scale + offset).
    const double back_x = static_cast<double>(window_x) * mapping.back_buffer_width_ / mapping.window_width_;
    const double back_y = static_cast<double>(window_y) * mapping.back_buffer_height_ / mapping.window_height_;
    *layout_x = static_cast<float>((back_x - mapping.offset_x_) / mapping.scale_x_);
    *layout_y = static_cast<float>((back_y - mapping.offset_y_) / mapping.scale_y_);
    return true;
}

Cuboid element_box(const UiWidget* element)
{
    return {.x = element->summed().left, .y = element->summed().top, .width = element->span() * element->summed().scale_x_,
            .height = element->level_y() * element->summed().scale_y_, .angle = bearing(element)};
}

Rect2 element_rect(const UiWidget* element) { return box_rect(element_box(element)); }

bool box_contains(const Cuboid& box, maths::Vec2 at)
{
    const float x = at.x;
    const float y = at.y;
    // Back along the angle: the point's offset from the top left in the box's own axes.
    const double cosine = std::cos(box.angle);
    const double sine = std::sin(box.angle);
    const double dx = static_cast<double>(x) - box.x;
    const double dy = static_cast<double>(y) - box.y;
    const double u = (cosine * dx) + (sine * dy);
    const double v = (-sine * dx) + (cosine * dy);
    return u >= std::min(0.0F, box.width) && u < std::max(0.0F, box.width) && v >= std::min(0.0F, box.height) &&
           v < std::max(0.0F, box.height);
}

bool get_rotated(float angle)
{
    constexpr double kTurn = 6.283185307179586;
    return std::fabs(std::remainder(static_cast<double>(angle), kTurn)) > kTurnTolerance;
}

bool is_menu(const UiWidget* element)
{
    const uint32_t type = kind_tag(element);
    return type == kVMenu || type == kHMenu;
}

namespace {
const MenuBase* as_menu(const UiWidget* menu) { return core::down_cast<const MenuBase>(menu); }
} // namespace
bool menu_runs_top_to_bottom(const UiWidget* menu) { return as_menu(menu)->vertical(); }
int selected_index(const UiWidget* menu) { return as_menu(menu)->selected_index(); }
bool menu_has_focus(const UiWidget* menu) { return as_menu(menu)->focused(); }
bool menu_wraps(const UiWidget* menu) { return as_menu(menu)->cycles_around(); }

int child_count(const UiWidget* element)
{
    int count = 0;
    for (const UiWidget* child = first_child(element); child; child = next_sibling(child)) {
        ++count;
    }
    return count;
}

bool item_focusable(const UiWidget* item)
{
    if (as_object(item)->contains_tag_flag(kTagNotFocusable)) {
        return false;
    }
    return !flag_in_chain(item, kFlagBlockEvents);
}

bool item_passed_over(const UiWidget* item)
{
    // The tags' own components (SetTags stores them there; no global names or included structures to follow).
    const script::Struct* tags = as_object(item)->tag_table();
    for (const script::Component* component = tags ? tags->head() : nullptr; component; component = component->next) {
        if (component->name == 0 && value_kind(*component) == script::QbValueKind::kName &&
            component->value.word() == kTagBlank) {
            return true;
        }
        if (component->name == kTagActive && value_kind(*component) == script::QbValueKind::kInteger &&
            component->value.integer() == 0) {
            return true;
        }
    }
    return false;
}

bool item_contains(const Hit& hit, maths::Vec2 at)
{
    const UiWidget* item = hit.item;
    const UiWidget* menu = hit.menu;
    const float x = at.x;
    const float y = at.y;
    Rect2 bounds{};
    bool turned_hit = false;
    const bool any = item_shape(item, x, y, false, &bounds, &turned_hit);
    if (turned_hit) {
        return true;
    }
    return any && get_holds(widen(bounds, menu), x, y);
}

Rect2 item_bounds(const Hit& hit)
{
    const UiWidget* item = hit.item;
    const UiWidget* menu = hit.menu;
    Rect2 bounds = {};
    bool turned_hit = false;
    if (!item_shape(item, 0.0F, 0.0F, true, &bounds, &turned_hit)) {
        return bounds;
    }
    return widen(bounds, menu);
}

bool is_rotating_menu(const UiWidget* menu)
{
    if (get_rotated(bearing(menu))) {
        return true;
    }
    for (const UiWidget* child = first_child(menu); child; child = next_sibling(child)) {
        if (get_rotated(bearing(child))) {
            return true;
        }
    }
    return false;
}

bool find_item_at(UiWidget* root, float x, float y, MenuFilter accepts, Hit* out)
{
    if (!root) {
        return false;
    }
    Hunt search = {.x = x, .y = y, .accepts = accepts, .vertical_only_ = false, .found = false, .hit = {}};
    get_visit(search, root, false, 0, Rect2{}, false);
    if (search.found) {
        *out = search.hit;
    }
    return search.found;
}

bool any_menu(UiWidget* root, MenuFilter accepts, bool vertical_only)
{
    return (root != nullptr) && find_menu(root, accepts, vertical_only);
}

int step_selection(int target, int count, bool wraps, int (*read)(void* context),
                   void (*send)(void* context, int direction), void* context)
{
    int current = read(context);
    if (current < 0 || target < 0 || target >= count || current == target) {
        return 0;
    }
    int direction = target > current ? 1 : -1;
    int distance = (target - current) * direction;
    if (wraps && count - distance < distance) {
        direction = -direction;
        distance = count - distance;
    }
    int sent = 0;
    while (sent < count) {
        send(context, direction);
        ++sent;
        const int next = read(context);
        if (next == target || next < 0) {
            break;
        }
        int moved = (next - current) * direction;
        if (wraps) {
            moved = ((moved % count) + count) % count;
        }
        if (moved <= 0 || moved > distance) {
            break; // no movement, the wrong way, or past the target
        }
        distance -= moved;
        current = next;
    }
    return sent;
}

Target start_target(int index, int count, bool wraps) { return {.index = index, .count = count, .wraps = wraps, .frames_left_ = kTargetFrames, .stuck_frames_ = 0, .last_seen_ = -2}; }

TargetState advance_target(Target& target, bool wait, int (*read)(void* context),
                           void (*send)(void* context, int direction), void* context)
{
    const int current = read(context);
    if (current == target.index) {
        return TargetState::kReached;
    }
    if (current < 0 || target.frames_left_ <= 0) {
        return TargetState::kGaveUp;
    }
    --target.frames_left_;
    if (!wait) {
        step_selection(target.index, target.count, target.wraps, read, send, context);
    }
    const int now = read(context);
    if (now == target.index) {
        return TargetState::kReached;
    }
    if (now < 0) {
        return TargetState::kGaveUp;
    }
    if (now == target.last_seen_) {
        if (++target.stuck_frames_ > kMaxStuckFrames) {
            return TargetState::kGaveUp;
        }
    } else {
        target.stuck_frames_ = 0;
        target.last_seen_ = now;
    }
    return TargetState::kMoving;
}

int option_side(const Rect2& item, float x)
{
    const float width = item.right - item.left;
    if (x < item.left + (width * kEdgeFraction)) {
        return -1;
    }
    if (x >= item.right - (width * kEdgeFraction)) {
        return 1;
    }
    return 0;
}

// Whether a menu gets the pad events now: it has the focus, and - while the element manager gives pad events
// only to the elements of the highest priority (+0x4E8 > 0, for the pad events listed at +0x5DC, +0x69C of them) -
// it is one of those (the start of the menu's event handler 0x004A8100: 0x004A86B0, 0x004AAD50, 0x004A8F20,
// 0x004AB7C0).
bool menu_takes_pad_events(UiWidget* menu)
{
    if (!menu_has_focus(menu)) {
        return false;
    }
    auto* manager = core::down_cast<UiTree>(as_object(menu)->park_owner());
    if (!manager) {
        return false;
    }
    if (manager->num_priorities() > 0) {
        bool listed = false;
        const int count = manager->pad_event_kind_count();
        for (int i = 0; i < count; ++i) {
            if (core::at(manager->pad_event_types(), i) == kPadChoose) {
                listed = true;
            }
        }
        if (listed) {
            if (!flag_in_chain(menu, kFlagPriority)) {
                return false;
            }
            if (!manager->has_top_precedence(ident_no(menu))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace frontend::menu_reading
