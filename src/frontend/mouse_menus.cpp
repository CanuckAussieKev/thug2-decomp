// Mouse control of the menus: the mouse_menus feature (thug2-decomp.ini [features] mouse_menus). Our own
// addition - not original game code, not PartyMod's. See mouse_menus.h.

#include "frontend/mouse_menus.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <SDL2/SDL.h>
#include <array>

#include "frontend/automation.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_playtest.h"
#include "frontend/new_frontend.h"
#include "frontend/restart_tester.h"
#include "game/rider/thaw_bmx.h"
#include "frontend/start_level.h"
#include "engine/sys/debug_mode.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/gfx/screen.h"
#include "engine/net/thugpro_peer.h"
#include "engine/objects/signal.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/sdl_events.h"
#include "platform/run_log.h"
#include "engine/pad_input/device.h"
#include "frontend/menu_element.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/mem.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/maths/vec2.h"

namespace mouse_menus {

using frontend::MenuBase;
using frontend::UiTree;
using frontend::UiWidget;

namespace {

constexpr std::uint32_t kContainerElement = 0x5B9DA842U; // ContainerElement
constexpr std::uint32_t kElement3D = 0x82C59D78U;        // Element3D
constexpr std::uint32_t kSpriteElement = 0xB12B510AU;    // SpriteElement
constexpr std::uint32_t kTextBlockElement = 0x40D92263U; // TextBlockElement
constexpr std::uint32_t kTextElement = 0x5200DFB6U;      // TextElement
constexpr double kTurnTolerance = 0.001;
constexpr float kEdgeFraction = 0.3F; // the part of an adjustable item at each end that steps left or right
constexpr int kMaxViewLines = 12;
constexpr int kMaxWheelNotches = 5;            // a wheel event counts for at most this many menu steps

// Element types (Entity::kind_tag_) and names.
constexpr uint32_t kVMenu = 0x130EF802;             // "vmenu"
constexpr uint32_t kHMenu = 0xCCDED1E1;             // "hmenu"
constexpr uint32_t kWindowElement = 0xA13ADBB7;     // "windowelement" (a clipping viewport, 0x004B2060)
constexpr uint32_t kVScrollingMenu = 0x84298A3C;    // "vscrollingmenu"
constexpr uint32_t kHScrollingMenu = 0x2900E3D0;    // "hscrollingmenu"
constexpr uint32_t kTagNotFocusable = 0xF33A3321;   // "tag_not_focusable" (set by not_focusable)
constexpr uint32_t kParkEditorElement = 0x5999551E; // (the park editor's element - as Sio's in_park_editor)
constexpr uint32_t kTagBlank = 0xC3D43B9A;          // "blank" (make_spin_menu_item's filler items)
constexpr uint32_t kTagActive = 0xB4E103FD;         // "active" (make_spin_menu_item: 0 = disabled)

// The menus' pad events and how input_logic launches them (front_end.cpp).
constexpr uint32_t kPadUp = 0x30A4F836;      // "pad_up"
constexpr uint32_t kPadDown = 0x0FD1AC26;    // "pad_down"
constexpr uint32_t kPadLeft = 0x6949DB75;    // "pad_left"
constexpr uint32_t kPadRight = 0x05DDD87C;   // "pad_right"
constexpr uint32_t kPadChoose = 0x1E3E253B;  // "pad_choose"
constexpr uint32_t kPadBack = 0x7EE0FD2A;    // "pad_back"
constexpr uint32_t kDeviceNum = 0xC9428A08;  // "device_num"
constexpr uint32_t kController = 0xB30D9965; // "controller"
constexpr uint32_t kMenuEvent = 0x36B2EE74;  // the pad events' target and source

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
    for (UiWidget* child = first_child(element); child; child = next_sibling(child)) {
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

// --- the pieces ---------------------------------------------------------------------------------------------------

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
    for (UiWidget* child = first_child(element); child; child = next_sibling(child)) {
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
    for (UiWidget* child = first_child(menu); child; child = next_sibling(child)) {
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

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

struct MouseState {
    float x = 0.0F, y = 0.0F; // window pixels
    int window_width_ = 0, window_height_ = 0;
    bool used = false;  // it has moved, clicked or turned its wheel since the game started
    bool moved = false; // this frame
    int left_clicks_ = 0, right_clicks_ = 0;
    int wheel = 0; // notches, away from the user positive
};
// NOT ORIGINAL grouping - mouse_menus.cpp's state (the original's globals). Made on first use, never destroyed
// (mouse_menus_state()).
struct MouseMenusState {
    MouseState mouse_{};
    bool cursor_shown_ = false; // (the window starts with it hidden: sys::create_sdl_window)
    // The item last under the cursor (only compared, never followed).
    const UiWidget* hover_item_ = nullptr;
    uint32_t hover_id_ = 0;
};

MouseMenusState& mouse_menus_state()
{
    static core::NoDestroy<MouseMenusState> state;
    return state.get();
}

void read_window_size(uint32_t window_id)
{
    if (SDL_Window* window = SDL_GetWindowFromID(window_id)) {
        SDL_GetWindowSize(window, &mouse_menus_state().mouse_.window_width_, &mouse_menus_state().mouse_.window_height_);
    }
}

void handle_mouse_event(SDL_Event* event)
{
    switch (event->type) {
    case SDL_MOUSEMOTION:
        if (event->motion.xrel == 0 && event->motion.yrel == 0) {
            break;
        }
        mouse_menus_state().mouse_.x = static_cast<float>(event->motion.x);
        mouse_menus_state().mouse_.y = static_cast<float>(event->motion.y);
        read_window_size(event->motion.windowID);
        mouse_menus_state().mouse_.moved = true;
        mouse_menus_state().mouse_.used = true;
        break;
    case SDL_MOUSEBUTTONDOWN:
        mouse_menus_state().mouse_.x = static_cast<float>(event->button.x);
        mouse_menus_state().mouse_.y = static_cast<float>(event->button.y);
        read_window_size(event->button.windowID);
        if (event->button.button == SDL_BUTTON_LEFT) {
            ++mouse_menus_state().mouse_.left_clicks_;
        } else if (event->button.button == SDL_BUTTON_RIGHT) {
            ++mouse_menus_state().mouse_.right_clicks_;
        }
        mouse_menus_state().mouse_.used = true;
        break;
    case SDL_MOUSEWHEEL:
        mouse_menus_state().mouse_.wheel += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        mouse_menus_state().mouse_.used = true;
        break;
    default:
        break;
    }
}

UiWidget* root_element(void* manager) { return static_cast<UiTree*>(manager)->tree_root().get_pull(); }

bool in_park_editor()
{
    frontend::ElementPtr element;
    frontend::fetch_part(&element, kParkEditorElement);
    return element.get_pull() != nullptr;
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

// --- the debug log ([features] debug_mouse) ---

bool& log_events()
{
    static bool log_events{false}; // (while handling a click: the events sent are logged)
    return log_events;
}

const char* current_type_name(uint32_t type)
{
    switch (type) {
    case kVMenu:
        return "vmenu";
    case kHMenu:
        return "hmenu";
    case kWindowElement:
        return "windowelement";
    case kVScrollingMenu:
        return "vscrollingmenu";
    case kHScrollingMenu:
        return "hscrollingmenu";
    case kContainerElement:
        return "containerelement";
    case kTextElement:
        return "textelement";
    case kSpriteElement:
        return "spriteelement";
    case kTextBlockElement:
        return "textblockelement";
    case kElement3D:
        return "element3d";
    default:
        return "?";
    }
}

const char* event_name(uint32_t event)
{
    switch (event) {
    case kPadUp:
        return "pad_up";
    case kPadDown:
        return "pad_down";
    case kPadLeft:
        return "pad_left";
    case kPadRight:
        return "pad_right";
    case kPadChoose:
        return "pad_choose";
    case kPadBack:
        return "pad_back";
    default:
        return "?";
    }
}

void log_element(const char* what, const UiWidget* element)
{
    const Rect2 rect = element_rect(element);
    get_log("  %s %08X (%s %08X): rect (%.1f, %.1f)-(%.1f, %.1f), angle %.3f, alpha %.2f, flags %08X", what,
            ident_no(element), current_type_name(kind_tag(element)), kind_tag(element), rect.left, rect.top, rect.right,
            rect.bottom, bearing(element), get_alpha(element), current_flags(element));
}

// --- the menus ---

// As input_logic launches a pad event: for the first controller's device, unless its mapping is inactive or menu
// input is blocked.
void send_pad_event(modules::FrontEnd* front_end, uint32_t event)
{
    const char* not_sent = nullptr;
    const modules::FrontEndInputHandler* handler = core::at(front_end->pad_listeners(), 0).get();
    if (core::at(front_end->pad_mappings(), 0).mapping_ == 0) {
        not_sent = "the first controller's pad mapping is inactive";
    } else if (front_end->block_menu_input()) {
        not_sent = "menu input is blocked (SetButtonEventMappings block_menu_input)";
    } else if (!handler || !handler->dev()) {
        not_sent = "no input device";
    }
    if (log_events() && debug_enabled()) {
        get_log("  %s %s%s%s", not_sent ? "NOT sent:" : "sent", event_name(event), not_sent ? " - " : "",
                not_sent ? not_sent : "");
    }
    if (not_sent) {
        return;
    }
    script::Struct params;
    params.put_int(checksums::Name{kDeviceNum}, handler->dev()->port_no());
    params.put_int(checksums::Name{kController}, 0);
    objects::get_tracker()->launch_event(event, kMenuEvent, kMenuEvent, &params, false, -1.0F);
    params.get_clear();
}

// The item's own handler for `type` (its script's event handler table).
bool handles_event(const UiWidget* item, uint32_t type)
{
    const script::Script* script = as_object(item)->own_script();
    if (!script || !script->signal_routes()) {
        return false;
    }
    const objects::SignalRouteTable* table = script->signal_routes();
    return std::ranges::any_of(table->entries(), [&](const objects::SignalRouteTable::TableRow& entry) {
        return entry.type == type && entry.script != objects::kNullScript;
    });
}

// The child at `index` (null if none).
UiWidget* child_at(const UiWidget* menu, int index)
{
    if (index < 0) {
        return nullptr;
    }
    UiWidget* child = first_child(menu);
    for (; child && index > 0; --index) {
        child = next_sibling(child);
    }
    return child;
}

void log_menu(UiWidget* menu)
{
    get_log("  menu %08X (%s): selected %d of %d, focus %d, takes pad events %d, movement %d, wraps %d%s",
            ident_no(menu), current_type_name(kind_tag(menu)), selected_index(menu), child_count(menu),
            menu_has_focus(menu), menu_takes_pad_events(menu), as_menu(menu)->pad_handling() ? 1 : 0, menu_wraps(menu),
            is_rotating_menu(menu) ? ", turned (a spin menu)" : "");
}

// What is under the cursor when no item is: every element (not hidden) whose own box holds the point, with the
// nearest menu above it. At most a dozen.
void log_elements_at(UiWidget* element, float x, float y, int& lines)
{
    if ((current_flags(element) & kFlagHidden) != 0U) {
        return;
    }
    const Cuboid box = element_box(element);
    if (lines < kMaxViewLines && box.width != 0.0F && box.height != 0.0F && box_contains(box, {.x = x, .y = y})) {
        ++lines;
        log_element("under the cursor:", element);
        for (UiWidget* parent = current_parent(element); parent; parent = current_parent(parent)) {
            if (is_menu(parent)) {
                log_menu(parent);
                break;
            }
        }
    }
    for (UiWidget* child = first_child(element); child; child = next_sibling(child)) {
        log_elements_at(child, x, y, lines);
    }
}

struct Stepper {
    modules::FrontEnd* front_end_{};
    frontend::ElementPtr menu; // (cleared if a handler destroys it)
    bool vertical{};
};

int read_stepper(void* context)
{
    auto* stepper = static_cast<Stepper*>(context);
    return stepper->menu ? selected_index(stepper->menu.get_pull()) : -1;
}

void send_step(void* context, int direction)
{
    auto* stepper = static_cast<Stepper*>(context);
    const uint32_t event =
        stepper->vertical ? (direction < 0 ? kPadUp : kPadDown) : (direction < 0 ? kPadLeft : kPadRight);
    send_pad_event(stepper->front_end_, event);
}

// The item the mouse is moving a menu's selection to, over as many frames as the menu needs (advance_target), and
// what to send when it gets there (a click's pad_choose / pad_left / pad_right; 0 for a hover).
struct Awaiting {
    bool active = false;
    const UiWidget* item = nullptr; // (only compared, never followed)
    uint32_t item_id_ = 0;
    Target target = {};
    uint32_t choose_event_ = 0;
};
Awaiting& stored_pending()
{
    static core::NoDestroy<Awaiting> pending;
    return pending.get();
}

// Its menu: a smart pointer (cleared if the menu dies), allocated once and never freed - no destructor running
// on the game's memory at exit.
frontend::ElementPtr& pending_menu()
{
    static gsl::owner<frontend::ElementPtr*> menu{memory::make_owned<frontend::ElementPtr>()};
    return *menu;
}

void clear_pending(const char* why)
{
    if (stored_pending().active && (stored_pending().choose_event_ != 0U) && debug_enabled()) {
        get_log("mouse: not choosing item %08X: %s", stored_pending().item_id_, why);
    }
    stored_pending().active = false;
    pending_menu().get_release();
}

TargetState advance_pending(modules::FrontEnd* front_end)
{
    UiWidget* menu = pending_menu().get_pull();
    // The menu still there, with the item still at its place (compared by address, then id).
    UiWidget* item = menu ? child_at(menu, stored_pending().target.index) : nullptr;
    if (!item || item != stored_pending().item || ident_no(item) != stored_pending().item_id_) {
        return TargetState::kGaveUp;
    }
    stored_pending().target.count = child_count(menu);
    Stepper stepper;
    stepper.front_end_ = front_end;
    stepper.menu.assign_value(menu);
    stepper.vertical = menu_runs_top_to_bottom(menu);
    // Nothing to send while the menu doesn't take pad events, or while it is on an item it passes over (its own
    // focus script moves on).
    UiWidget* current = child_at(menu, selected_index(menu));
    const bool wait = !menu_takes_pad_events(menu) || ((current != nullptr) && item_passed_over(current));
    return advance_target(stored_pending().target, wait, read_stepper, send_step, &stepper);
}

// Settle the pending item: there - send its event (a click's); given up - forget it. True if it got there.
bool settle_pending(modules::FrontEnd* front_end, TargetState state)
{
    if (state == TargetState::kMoving) {
        return false;
    }
    const uint32_t event = stored_pending().choose_event_;
    if (state == TargetState::kGaveUp) {
        clear_pending("its menu's selection didn't get there (or the menu / item is gone)");
        return false;
    }
    if ((event != 0U) && debug_enabled()) {
        get_log("mouse: item %08X selected", stored_pending().item_id_);
    }
    stored_pending().active = false;
    pending_menu().get_release();
    if (event != 0U) {
        const bool logging = log_events();
        log_events() = true;
        send_pad_event(front_end, event);
        log_events() = logging;
    }
    return true;
}

// Aim at an item: start moving its menu's selection there - and, with `event`, send it once there. True if it is
// selected already.
bool aim_at(modules::FrontEnd* front_end, const Hit& hit, uint32_t event)
{
    stored_pending().active = true;
    pending_menu().assign_value(hit.menu);
    stored_pending().item = hit.item;
    stored_pending().item_id_ = ident_no(hit.item);
    stored_pending().target = start_target(hit.index, child_count(hit.menu), menu_wraps(hit.menu));
    stored_pending().choose_event_ = event;
    return settle_pending(front_end, advance_pending(front_end));
}

// Select the item under the cursor - when it isn't the one that was (or on a `click`). Moving an outer menu can
// focus an inner one (an option row's hmenu), so a few passes. A turned (spin) menu isn't moved by hovering.
void select_under_cursor(modules::FrontEnd* front_end, float x, float y, bool click)
{
    for (int pass = 0; pass < 4; ++pass) {
        Hit hit{};
        if (!find_item_at(root_element(frontend::UiTree::sole()), x, y, menu_takes_pad_events, &hit)) {
            mouse_menus_state().hover_item_ = nullptr;
            mouse_menus_state().hover_id_ = 0;
            return;
        }
        const bool changed = hit.item != mouse_menus_state().hover_item_ || ident_no(hit.item) != mouse_menus_state().hover_id_;
        mouse_menus_state().hover_item_ = hit.item;
        mouse_menus_state().hover_id_ = ident_no(hit.item);
        if (!changed && !(click && pass == 0)) {
            return;
        }
        if (!click && is_rotating_menu(hit.menu)) {
            return;
        }
        if (!aim_at(front_end, hit, 0)) {
            return; // (on its way: the rest in the next frames)
        }
    }
}

void get_click(modules::FrontEnd* front_end, float x, float y)
{
    const bool debug = debug_enabled();
    log_events() = debug;
    if (debug) {
        get_log("mouse: left click at layout (%.1f, %.1f)", x, y);
    }
    select_under_cursor(front_end, x, y, true);
    Hit hit{};
    if (!find_item_at(root_element(frontend::UiTree::sole()), x, y, menu_takes_pad_events, &hit)) {
        if (debug) {
            get_log("  no item of a menu that takes pad events under the cursor - nothing sent");
            int lines = 0;
            if (UiWidget* root = root_element(frontend::UiTree::sole())) {
                log_elements_at(root, x, y, lines);
            }
        }
        log_events() = false;
        return;
    }
    uint32_t event = kPadChoose;
    if (menu_runs_top_to_bottom(hit.menu) &&
        (handles_event(hit.item, kPadLeft) || handles_event(hit.item, kPadRight))) {
        const int side = option_side(element_rect(hit.item), x);
        if (side < 0) {
            event = kPadLeft;
        } else if (side > 0) {
            event = kPadRight;
        }
    }
    if (debug) {
        log_element("item", hit.item);
        const Rect2 area = item_bounds(hit);
        get_log("    index %d, area (%.1f, %.1f)-(%.1f, %.1f)", hit.index, area.left, area.top, area.right,
                area.bottom);
        log_menu(hit.menu);
    }
    if (stored_pending().active && stored_pending().item == hit.item && pending_menu().get_pull() == hit.menu) {
        stored_pending().choose_event_ = event;
        if (debug) {
            get_log("  the menu is on its way to the item (selected %d): %s once it gets there",
                    selected_index(hit.menu), event_name(event));
        }
    } else if (selected_index(hit.menu) == hit.index) {
        clear_pending("another item was clicked");
        send_pad_event(front_end, event);
    } else if (!aim_at(front_end, hit, event) && stored_pending().active && debug) {
        get_log("  the menu is on its way to the item (selected %d): %s once it gets there", selected_index(hit.menu),
                event_name(event));
    }
    log_events() = false;
}

void show_cursor(bool show)
{
    if (show == mouse_menus_state().cursor_shown_) {
        return;
    }
    SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
    mouse_menus_state().cursor_shown_ = show;
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "mouse_menus", 1) != 0;
    return on;
}

bool& debug_enabled()
{
    static bool on = config_int("features", "debug_mouse", 0) != 0;
    return on;
}

void prepare_all()
{
    if (!is_switched_on()) {
        return;
    }
    os_layer::register_event_handler(handle_mouse_event);
}

void tick_frame(modules::FrontEnd* front_end)
{
    // (not original either: the THUGPRO peer mode's host options applied once a level is up -
    // engine/net/thugpro_peer.h)
    online::thugpro::tick_frame();
    // Not original either - the level editor (editor/level_editor.h) has the mouse while it runs: no menu
    // events (the clicks and wheel meanwhile are dropped), the cursor left as the editor sets it; its own input logic
    // runs here instead.
    // Original behaviour (of the mouse_menus feature): the mouse drives the menus.
    if (frontend::level_editor::live_now()) {
        mouse_menus_state().mouse_.moved = false;
        mouse_menus_state().mouse_.left_clicks_ = 0;
        mouse_menus_state().mouse_.right_clicks_ = 0;
        mouse_menus_state().mouse_.wheel = 0;
        clear_pending("the level editor runs");
        frontend::level_editor::input_logic();
        // (not original either: the new front end steps aside while the editor is up - frontend/new_frontend.h)
        frontend::new_frontend::tick_frame(front_end);
        return;
    }
    // (not original either: the restart tester's key, F8 - frontend/restart_tester.h)
    frontend::restart_tester::tick_frame(front_end);
    // (and THAW's BMX's key, F7 - game/rider/thaw_bmx.h)
    skater::thaw_bmx::tick_frame(front_end);
    // (and play from here's way back to the level editor, F5 / Ctrl+P - editor/level_editor_play.h)
    frontend::level_editor::perform::tick_frame(front_end);
    // (and play testing: the skater's trail recorded, a restart's play from here - editor/level_editor_playtest.h)
    frontend::level_editor::play_test::tick_frame(front_end);
    // (and debug mode's viewer and F keys - engine/sys/debug_mode.h)
    sys::debug_mode::tick_frame(front_end);
    // (and the start level: -level <name> - frontend/start_level.h)
    frontend::begin_level::tick_frame(front_end);
    // (and the test-automation channel: commands from a file - frontend/automation.h)
    frontend::automation::tick_frame(front_end);
    // (and the new front end - frontend/new_frontend.h: while it's up it has the mouse; the original menus under it
    // get no mouse events, the cursor stays shown)
    frontend::new_frontend::tick_frame(front_end);
    if (frontend::new_frontend::blocks_game_input() || frontend::new_frontend::owns_mouse()) {
        mouse_menus_state().mouse_.moved = false;
        mouse_menus_state().mouse_.left_clicks_ = 0;
        mouse_menus_state().mouse_.right_clicks_ = 0;
        mouse_menus_state().mouse_.wheel = 0;
        clear_pending("the new front end has the mouse");
        show_cursor(true);
        return;
    }
    if (!is_switched_on()) {
        return;
    }
    const MouseState input = mouse_menus_state().mouse_;
    mouse_menus_state().mouse_.moved = false;
    mouse_menus_state().mouse_.left_clicks_ = 0;
    mouse_menus_state().mouse_.right_clicks_ = 0;
    mouse_menus_state().mouse_.wheel = 0;

    // The front end, or a level's pause menu; never the park editor's own cursor.
    void* manager = frontend::UiTree::sole();
    bool menus = (manager != nullptr) && (root_element(manager) != nullptr) &&
                 (render::current_level() == 0 || front_end->is_halted());
    if (menus && in_park_editor() && !front_end->is_halted()) {
        menus = false;
    }
    if (debug_enabled()) {
        // (debug_mouse: each change of whether the mouse drives the menus, with what decided it)
        static int s_last_menus = -1;
        static int s_last_paused = -1;
        if (static_cast<int>(menus) != s_last_menus || static_cast<int>(front_end->is_halted()) != s_last_paused) {
            s_last_menus = static_cast<int>(menus);
            s_last_paused = static_cast<int>(front_end->is_halted());
            get_log("mouse: menus %s (level %u, front end paused %d, element manager %d, root %d, park editor %d)",
                    menus ? "active" : "inactive", render::current_level(), front_end->is_halted(), manager != nullptr,
                    (manager != nullptr) && root_element(manager) != nullptr, in_park_editor());
        }
    }
    show_cursor(menus);
    if (!menus) {
        clear_pending("the menus are gone");
        return;
    }
    // A selection on its way to an item from earlier frames.
    if (stored_pending().active) {
        log_events() = stored_pending().choose_event_ != 0;
        settle_pending(front_end, advance_pending(front_end));
        log_events() = false;
    }
    if (!input.used) {
        return;
    }

    float x = 0.0F;
    float y = 0.0F;
    if (!window_to_layout(current_mapping({.width = input.window_width_, .height = input.window_height_}), input.x,
                          input.y, &x, &y)) {
        return;
    }
    if (input.moved) {
        select_under_cursor(front_end, x, y, false);
    }
    for (int i = 0; i < input.left_clicks_; ++i) {
        get_click(front_end, x, y);
    }
    if ((input.right_clicks_ != 0) || (input.wheel != 0)) {
        clear_pending("a right click or the wheel");
    }
    for (int i = 0; i < input.right_clicks_; ++i) {
        const bool any = any_menu(root_element(frontend::UiTree::sole()), menu_takes_pad_events, false);
        if (debug_enabled()) {
            get_log("mouse: right click at layout (%.1f, %.1f)%s", x, y,
                    any ? "" : ": no menu takes pad events - nothing sent");
        }
        log_events() = true;
        if (any) {
            send_pad_event(front_end, kPadBack);
        }
        log_events() = false;
    }
    int notches = std::max(-kMaxWheelNotches, std::min(kMaxWheelNotches, input.wheel));
    for (; notches != 0; notches += notches > 0 ? -1 : 1) {
        if (any_menu(root_element(frontend::UiTree::sole()), menu_takes_pad_events, true)) {
            send_pad_event(front_end, notches > 0 ? kPadUp : kPadDown);
        }
    }
}

bool menu_gets_pad_events(UiWidget* menu) { return menu_takes_pad_events(menu); }

void send_menu_pad_event(modules::FrontEnd* front_end, uint32_t event) { send_pad_event(front_end, event); }

} // namespace mouse_menus
