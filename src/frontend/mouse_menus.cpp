// Mouse control of the menus: the mouse_menus feature (thug2-decomp.ini [features] mouse_menus). Our own
// addition - not original game code, not PartyMod's. See mouse_menus.h.

#include "frontend/mouse_menus.h"

#include "engine/ui/menu_reading.h"
#include "game/front_end/menu_pad_event.h"

#include <algorithm>
#include <cstdint>

#include <SDL3/SDL.h>

#include "game/front_end/front_end.h"
#include "frontend/new_frontend.h"
#include "engine/ext/extension.h"
#include "engine/sys/editor_mode.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_element_manager.h"
#include "engine/gfx/screen.h"
#include "engine/objects/signal.h"
#include "engine/script/cscript.h"
#include "engine/pad_input/input_event.h"
#include "engine/pad_input/input_source.h"
#include "platform/run_log.h"
#include "engine/ui/menu_element.h"
#include "base/core/down_cast.h"
#include "engine/memory/mem.h"
#include "base/core/no_destroy.h"

namespace mouse_menus {

using namespace frontend::menu_reading;
using frontend::MenuBase;
using frontend::UiTree;
using frontend::UiWidget;

namespace {

constexpr std::uint32_t kContainerElement = 0x5B9DA842U; // ContainerElement
constexpr std::uint32_t kElement3D = 0x82C59D78U;        // Element3D
constexpr std::uint32_t kSpriteElement = 0xB12B510AU;    // SpriteElement
constexpr std::uint32_t kTextBlockElement = 0x40D92263U; // TextBlockElement
constexpr std::uint32_t kTextElement = 0x5200DFB6U;      // TextElement
constexpr int kMaxViewLines = 12;
constexpr int kMaxWheelNotches = 5;            // a wheel event counts for at most this many menu steps

// Element types (Entity::kind_tag_) and names.
constexpr uint32_t kVMenu = 0x130EF802;             // "vmenu"
constexpr uint32_t kHMenu = 0xCCDED1E1;             // "hmenu"
constexpr uint32_t kWindowElement = 0xA13ADBB7;     // "windowelement" (a clipping viewport, 0x004B2060)
constexpr uint32_t kVScrollingMenu = 0x84298A3C;    // "vscrollingmenu"
constexpr uint32_t kHScrollingMenu = 0x2900E3D0;    // "hscrollingmenu"
constexpr uint32_t kParkEditorElement = 0x5999551E; // (the park editor's element - as Sio's in_park_editor)

// The menus' pad events and how input_logic launches them (front_end.cpp).
constexpr uint32_t kPadUp = 0x30A4F836;      // "pad_up"
constexpr uint32_t kPadDown = 0x0FD1AC26;    // "pad_down"
constexpr uint32_t kPadLeft = 0x6949DB75;    // "pad_left"
constexpr uint32_t kPadRight = 0x05DDD87C;   // "pad_right"
constexpr uint32_t kPadChoose = 0x1E3E253B;  // "pad_choose"
constexpr uint32_t kPadBack = 0x7EE0FD2A;    // "pad_back"

// UiWidget's flags (+0x08).
constexpr uint32_t kFlagHidden = 0x400000;      // hide

const objects::Entity* as_object(const UiWidget* element) { return static_cast<const objects::Entity*>(element); }
uint32_t current_flags(const UiWidget* element) { return as_object(element)->object_flags(); }
uint32_t kind_tag(const UiWidget* element) { return as_object(element)->kind_tag(); }
uint32_t ident_no(const UiWidget* element) { return as_object(element)->ident(); }
UiWidget* current_parent(const UiWidget* element) { return element->container().get_pull(); }
UiWidget* first_child(const UiWidget* element) { return element->eldest().get_pull(); }
UiWidget* next_sibling(const UiWidget* element) { return element->following().get_pull(); }
float get_alpha(const UiWidget* element) { return element->summed().alpha; }
float bearing(const UiWidget* element) { return element->summed().angle; }
const MenuBase* as_menu(const UiWidget* menu) { return core::down_cast<const MenuBase>(menu); }

} // namespace

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
    input::window_size(window_id, &mouse_menus_state().mouse_.window_width_, &mouse_menus_state().mouse_.window_height_);
}

void handle_mouse_event(const input::Event& event)
{
    switch (event.type) {
    case input::EventType::kMouseMotion:
        if (event.dx == 0 && event.dy == 0) {
            break;
        }
        mouse_menus_state().mouse_.x = static_cast<float>(event.x);
        mouse_menus_state().mouse_.y = static_cast<float>(event.y);
        read_window_size(event.window);
        mouse_menus_state().mouse_.moved = true;
        mouse_menus_state().mouse_.used = true;
        break;
    case input::EventType::kMouseButtonDown:
        mouse_menus_state().mouse_.x = static_cast<float>(event.x);
        mouse_menus_state().mouse_.y = static_cast<float>(event.y);
        read_window_size(event.window);
        if (event.button == input::kMouseLeft) {
            ++mouse_menus_state().mouse_.left_clicks_;
        } else if (event.button == input::kMouseRight) {
            ++mouse_menus_state().mouse_.right_clicks_;
        }
        mouse_menus_state().mouse_.used = true;
        break;
    case input::EventType::kMouseWheel:
        mouse_menus_state().mouse_.wheel += event.wheel_flipped ? -event.wheel : event.wheel;
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

// As input_logic launches a pad event (game/front_end/menu_pad_event.h), the refusal logged with debug_mouse.
void send_pad_event(modules::FrontEnd* front_end, uint32_t event)
{
    const char* not_sent = modules::menu_pad_event_refusal(front_end);
    if (log_events() && debug_enabled()) {
        get_log("  %s %s%s%s", not_sent ? "NOT sent:" : "sent", event_name(event), not_sent ? " - " : "",
                not_sent ? not_sent : "");
    }
    if (not_sent) {
        return;
    }
    modules::launch_menu_pad_event(front_end, event);
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
    const uint32_t vertical_event = direction < 0 ? kPadUp : kPadDown;
    const uint32_t sideways_event = direction < 0 ? kPadLeft : kPadRight;
    const uint32_t event = stepper->vertical ? vertical_event : sideways_event;
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
    input::show_cursor(show ? input::kCursorShow : input::kCursorHide);
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
    input::add_event_handler(handle_mouse_event);
}

namespace {

// The front end's frame while an extension owns the game (the level editor has the mouse while it runs): no menu
// events (the clicks and wheel meanwhile are dropped), the cursor left as the editor sets it - before the other
// extensions' frames (the editor's input logic runs in its own).
// Original behaviour (of the mouse_menus feature): the mouse drives the menus.
void stand_aside()
{
    mouse_menus_state().mouse_.moved = false;
    mouse_menus_state().mouse_.left_clicks_ = 0;
    mouse_menus_state().mouse_.right_clicks_ = 0;
    mouse_menus_state().mouse_.wheel = 0;
    clear_pending("the level editor runs");
}

// The mouse's menu events, after every extension's frame (the new front end's last: while it's up it has the mouse;
// the original menus under it get no mouse events, the cursor stays shown).
void drive_menus(modules::FrontEnd* front_end)
{
    // (the editor mode: not while the editor's pages stand over the front end, nor while the game playing in its Scene
    // panel lacks the focus - engine/sys/editor_mode.h)
    if (sys::editor_mode::on() && ext::game_input_blocked()) {
        mouse_menus_state().mouse_.moved = false;
        mouse_menus_state().mouse_.left_clicks_ = 0;
        mouse_menus_state().mouse_.right_clicks_ = 0;
        mouse_menus_state().mouse_.wheel = 0;
        clear_pending("the level editor's window has the mouse");
        show_cursor(true);
        return;
    }
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

class MouseMenusExtension final : public ext::Extension {
public:
    void prepare_input() override { prepare_all(); }
    void menu_frame(modules::FrontEnd* /*front_end*/, bool game_owned) override
    {
        if (game_owned) {
            stand_aside();
        }
    }
    void after_menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (!game_owned) {
            drive_menus(front_end);
        }
    }
};

} // namespace

ext::Extension& extension()
{
    static MouseMenusExtension mouse;
    return mouse;
}



} // namespace mouse_menus
