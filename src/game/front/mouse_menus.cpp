// Mouse control of the menus: the mouse_menus feature (thug2-decomp.ini [features] mouse_menus). Our own
// addition - not original game code, not PartyMod's. See mouse_menus.h.

#include "game/front/mouse_menus.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <SDL2/SDL.h>

#include "game/front/automation.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_play.h"
#include "game/front/level_editor_playtest.h"
#include "game/front/restart_tester.h"
#include "game/front/start_level.h"
#include "game/sys/debug_mode.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/gfx/screen.h"
#include "game/net/thugpro_peer.h"
#include "game/obj/event.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/sdl_events.h"
#include "hook/game.h"
#include "hook/log.h"

namespace MouseMenus {

using Front::CScreenElement;

namespace {

// Element types (CObject::m_type) and names.
constexpr uint32_t kVMenu = 0x130EF802;          // "vmenu"
constexpr uint32_t kHMenu = 0xCCDED1E1;          // "hmenu"
constexpr uint32_t kWindowElement = 0xA13ADBB7;  // "windowelement" (a clipping viewport, 0x004B2060)
constexpr uint32_t kVScrollingMenu = 0x84298A3C; // "vscrollingmenu"
constexpr uint32_t kHScrollingMenu = 0x2900E3D0; // "hscrollingmenu"
constexpr uint32_t kTagNotFocusable = 0xF33A3321; // "tag_not_focusable" (set by not_focusable)
constexpr uint32_t kParkEditorElement = 0x5999551E; // (the park editor's element - as Sio's InParkEditor)
constexpr uint32_t kTagBlank = 0xC3D43B9A;  // "blank" (make_spin_menu_item's filler items)
constexpr uint32_t kTagActive = 0xB4E103FD; // "active" (make_spin_menu_item: 0 = disabled)

// The menus' pad events and how InputLogic launches them (front_end.cpp).
constexpr uint32_t kPadUp = 0x30A4F836;     // "pad_up"
constexpr uint32_t kPadDown = 0x0FD1AC26;   // "pad_down"
constexpr uint32_t kPadLeft = 0x6949DB75;   // "pad_left"
constexpr uint32_t kPadRight = 0x05DDD87C;  // "pad_right"
constexpr uint32_t kPadChoose = 0x1E3E253B; // "pad_choose"
constexpr uint32_t kPadBack = 0x7EE0FD2A;   // "pad_back"
constexpr uint32_t kDeviceNum = 0xC9428A08; // "device_num"
constexpr uint32_t kController = 0xB30D9965; // "controller"
constexpr uint32_t kMenuEvent = 0x36B2EE74; // the pad events' target and source

// CScreenElement's flags (+0x08).
constexpr uint32_t kFlagBlockEvents = 0x200000;  // block_events
constexpr uint32_t kFlagHidden = 0x400000;       // hide
constexpr uint32_t kFlagPriority = 0x10000000;   // priority > 0 (pad events with priorities: 0x004A8F20)

// An element below this alpha (+0x114, its parents' included) isn't seen.
constexpr float kMinAlpha = 0.01f;

template <typename T>
T Field(const CScreenElement* element, uint32_t offset)
{
    return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(element) + offset);
}

const Obj::CObject* AsObject(const CScreenElement* element) { return reinterpret_cast<const Obj::CObject*>(element); }
uint32_t Flags(const CScreenElement* element) { return Field<uint32_t>(element, 0x08); }
uint32_t Type(const CScreenElement* element) { return AsObject(element)->m_type; }
uint32_t Id(const CScreenElement* element) { return AsObject(element)->m_id; }
CScreenElement* Parent(const CScreenElement* element) { return Field<CScreenElement*>(element, 0x4C); }
CScreenElement* FirstChild(const CScreenElement* element) { return Field<CScreenElement*>(element, 0x58); }
CScreenElement* NextSibling(const CScreenElement* element) { return Field<CScreenElement*>(element, 0x64); }
float Alpha(const CScreenElement* element) { return Field<float>(element, 0x114); }
float Angle(const CScreenElement* element) { return Field<float>(element, 0x14C); }

// The flag on the element or a parent.
bool FlagInChain(const CScreenElement* element, uint32_t flag)
{
    for (; element; element = Parent(element))
        if (Flags(element) & flag)
            return true;
    return false;
}

bool ClipsChildren(const CScreenElement* element)
{
    uint32_t type = Type(element);
    return type == kWindowElement || type == kVScrollingMenu || type == kHScrollingMenu;
}

Rect Intersect(const Rect& a, const Rect& b)
{
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

Rect Union(const Rect& a, const Rect& b)
{
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
}

// The rectangle of an unturned box (right / bottom past left / top).
Rect BoxRect(const Box& box)
{
    Rect rect = {box.x, box.y, box.x + box.width, box.y + box.height};
    if (rect.right < rect.left)
        std::swap(rect.left, rect.right);
    if (rect.bottom < rect.top)
        std::swap(rect.top, rect.bottom);
    return rect;
}

// The rectangle around a turned box's corners.
Rect TurnedBoxRect(const Box& box)
{
    double c = std::cos(box.angle), s = std::sin(box.angle);
    Rect rect = {box.x, box.y, box.x, box.y};
    const float corners[3][2] = {{box.width, 0.0f}, {0.0f, box.height}, {box.width, box.height}};
    for (const auto& corner : corners) {
        float x = static_cast<float>(box.x + c * corner[0] - s * corner[1]);
        float y = static_cast<float>(box.y + s * corner[0] + c * corner[1]);
        rect = Union(rect, Rect{x, y, x, y});
    }
    return rect;
}

// Each part of an item: itself, then everything below it that isn't hidden (nor below a hidden one).
template <typename F>
void ForEachPart(const CScreenElement* element, F& visit, bool item)
{
    if (!item && (Flags(element) & kFlagHidden))
        return;
    visit(element, item);
    for (CScreenElement* child = FirstChild(element); child; child = NextSibling(child))
        ForEachPart(child, visit, false);
}

// The rectangle around the item's unturned parts (false: none - with `turnedInBounds` the turned ones' too), and
// whether a turned part contains (x, y).
bool ItemShape(const CScreenElement* item, float x, float y, bool turnedInBounds, Rect* bounds, bool* turnedHit)
{
    bool any = false;
    *turnedHit = false;
    auto visit = [&](const CScreenElement* part, bool isItem) {
        Box box = ElementBox(part);
        if (!isItem && (box.width == 0.0f || box.height == 0.0f))
            return; // (an empty container somewhere: nothing to point at)
        Rect rect;
        if (Rotated(box.angle)) {
            if (BoxContains(box, x, y))
                *turnedHit = true;
            if (!turnedInBounds)
                return;
            rect = TurnedBoxRect(box);
        } else {
            rect = BoxRect(box);
        }
        *bounds = any ? Union(*bounds, rect) : rect;
        any = true;
    };
    ForEachPart(item, visit, true);
    return any;
}

// Across its menu (a vertical menu's width, a horizontal one's height) - unless the menu is turned.
Rect Widen(Rect area, const CScreenElement* menu)
{
    if (Rotated(Angle(menu)))
        return area;
    Rect span = ElementRect(menu);
    if (IsVerticalMenu(menu)) {
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

struct Search {
    float x, y;
    MenuFilter accepts;
    bool verticalOnly; // (AnyMenu)
    bool found;
    Hit hit;
};

// Pre-order: a later find (deeper, or later in the tree) replaces an earlier one.
void Visit(Search& search, CScreenElement* element, bool parentAccepts, int index, const Rect& clip, bool clipped)
{
    if (Flags(element) & kFlagHidden)
        return;
    if (Alpha(element) < kMinAlpha)
        return;
    CScreenElement* parent = Parent(element);
    if (parentAccepts && !ItemPassedOver(element) && ItemFocusable(element)) {
        if (ItemContains(element, parent, search.x, search.y) && (!clipped || clip.Contains(search.x, search.y))) {
            search.found = true;
            search.hit = {parent, element, index};
        }
    }
    Rect childClip = clip;
    bool childClipped = clipped;
    if (ClipsChildren(element)) {
        Rect own = ElementRect(element);
        if (own.right > own.left && own.bottom > own.top) {
            childClip = clipped ? Intersect(clip, own) : own;
            childClipped = true;
        }
    }
    bool accepts = IsMenu(element) && search.accepts(element);
    int childIndex = 0;
    for (CScreenElement* child = FirstChild(element); child; child = NextSibling(child), ++childIndex)
        Visit(search, child, accepts, childIndex, childClip, childClipped);
}

bool FindMenu(CScreenElement* element, MenuFilter accepts, bool verticalOnly)
{
    if (Flags(element) & kFlagHidden)
        return false;
    if (IsMenu(element) && (!verticalOnly || IsVerticalMenu(element)) && accepts(element))
        return true;
    for (CScreenElement* child = FirstChild(element); child; child = NextSibling(child))
        if (FindMenu(child, accepts, verticalOnly))
            return true;
    return false;
}

} // namespace

// --- the pieces ---------------------------------------------------------------------------------------------------

ScreenMapping CurrentMapping(int windowWidth, int windowHeight)
{
    ScreenMapping mapping;
    mapping.windowWidth = windowWidth;
    mapping.windowHeight = windowHeight;
    mapping.backBufferWidth = GameVariable<uint32_t>(0x00786D6C);
    mapping.backBufferHeight = GameVariable<uint32_t>(0x00786D70);
    mapping.scaleX = GameVariable<float>(0x00786D80);
    mapping.scaleY = GameVariable<float>(0x00786D84);
    mapping.offsetX = GameVariable<int32_t>(0x00786D88);
    mapping.offsetY = GameVariable<int32_t>(0x00786D8C);
    return mapping;
}

bool WindowToLayout(const ScreenMapping& mapping, float windowX, float windowY, float* layoutX, float* layoutY)
{
    if (mapping.windowWidth <= 0 || mapping.windowHeight <= 0 || mapping.backBufferWidth == 0 ||
        mapping.backBufferHeight == 0 || mapping.scaleX == 0.0f || mapping.scaleY == 0.0f)
        return false;
    // Window -> back buffer (the window may be scaled to show it), then back buffer -> layout (the inverse of
    // back buffer = layout * scale + offset).
    double backX = static_cast<double>(windowX) * mapping.backBufferWidth / mapping.windowWidth;
    double backY = static_cast<double>(windowY) * mapping.backBufferHeight / mapping.windowHeight;
    *layoutX = static_cast<float>((backX - mapping.offsetX) / mapping.scaleX);
    *layoutY = static_cast<float>((backY - mapping.offsetY) / mapping.scaleY);
    return true;
}

Box ElementBox(const CScreenElement* element)
{
    return {Field<float>(element, 0x134), Field<float>(element, 0x138),
            Field<float>(element, 0x160) * Field<float>(element, 0x13C),
            Field<float>(element, 0x164) * Field<float>(element, 0x140), Angle(element)};
}

Rect ElementRect(const CScreenElement* element) { return BoxRect(ElementBox(element)); }

bool BoxContains(const Box& box, float x, float y)
{
    // Back along the angle: the point's offset from the top left in the box's own axes.
    double c = std::cos(box.angle), s = std::sin(box.angle);
    double dx = static_cast<double>(x) - box.x, dy = static_cast<double>(y) - box.y;
    double u = c * dx + s * dy;
    double v = -s * dx + c * dy;
    return u >= std::min(0.0f, box.width) && u < std::max(0.0f, box.width) && v >= std::min(0.0f, box.height) &&
           v < std::max(0.0f, box.height);
}

bool Rotated(float angle)
{
    constexpr double kTurn = 6.283185307179586;
    return std::fabs(std::remainder(static_cast<double>(angle), kTurn)) > 0.001;
}

bool IsMenu(const CScreenElement* element)
{
    uint32_t type = Type(element);
    return type == kVMenu || type == kHMenu;
}

bool IsVerticalMenu(const CScreenElement* menu) { return Field<uint8_t>(menu, 0x190) != 0; }
int SelectedIndex(const CScreenElement* menu) { return Field<int>(menu, 0x178); }
bool MenuHasFocus(const CScreenElement* menu) { return Field<uint8_t>(menu, 0x180) != 0; }
bool MenuWraps(const CScreenElement* menu) { return Field<uint8_t>(menu, 0x1A1) != 0; }

int ChildCount(const CScreenElement* element)
{
    int count = 0;
    for (CScreenElement* child = FirstChild(element); child; child = NextSibling(child))
        ++count;
    return count;
}

bool ItemFocusable(const CScreenElement* item)
{
    if (const_cast<Obj::CObject*>(AsObject(item))->ContainsTagFlag(kTagNotFocusable))
        return false;
    return !FlagInChain(item, kFlagBlockEvents);
}

bool ItemPassedOver(const CScreenElement* item)
{
    // The tags' own components (SetTags stores them there; no global names or included structures to follow).
    Script::CStruct* tags = AsObject(item)->mp_tags;
    for (Script::CComponent* c = tags ? tags->m_head : nullptr; c; c = c->next) {
        if (c->name == 0 && c->Type() == Script::ESYMBOLTYPE_NAME && c->checksum == kTagBlank)
            return true;
        if (c->name == kTagActive && c->Type() == Script::ESYMBOLTYPE_INTEGER && c->integer == 0)
            return true;
    }
    return false;
}

bool ItemContains(const CScreenElement* item, const CScreenElement* menu, float x, float y)
{
    Rect bounds;
    bool turnedHit;
    bool any = ItemShape(item, x, y, false, &bounds, &turnedHit);
    if (turnedHit)
        return true;
    return any && Widen(bounds, menu).Contains(x, y);
}

Rect ItemBounds(const CScreenElement* item, const CScreenElement* menu)
{
    Rect bounds = {};
    bool turnedHit;
    if (!ItemShape(item, 0.0f, 0.0f, true, &bounds, &turnedHit))
        return bounds;
    return Widen(bounds, menu);
}

bool IsRotatingMenu(const CScreenElement* menu)
{
    if (Rotated(Angle(menu)))
        return true;
    for (CScreenElement* child = FirstChild(menu); child; child = NextSibling(child))
        if (Rotated(Angle(child)))
            return true;
    return false;
}

bool FindItemAt(CScreenElement* root, float x, float y, MenuFilter accepts, Hit* out)
{
    if (!root)
        return false;
    Search search = {x, y, accepts, false, false, {}};
    Visit(search, root, false, 0, Rect{}, false);
    if (search.found)
        *out = search.hit;
    return search.found;
}

bool AnyMenu(CScreenElement* root, MenuFilter accepts, bool verticalOnly)
{
    return root && FindMenu(root, accepts, verticalOnly);
}

int StepSelection(int target, int count, bool wraps, int (*read)(void* context), void (*send)(void* context, int direction),
                  void* context)
{
    int current = read(context);
    if (current < 0 || target < 0 || target >= count || current == target)
        return 0;
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
        int next = read(context);
        if (next == target || next < 0)
            break;
        int moved = (next - current) * direction;
        if (wraps)
            moved = (moved % count + count) % count;
        if (moved <= 0 || moved > distance)
            break; // no movement, the wrong way, or past the target
        distance -= moved;
        current = next;
    }
    return sent;
}

Target StartTarget(int index, int count, bool wraps) { return {index, count, wraps, kTargetFrames, 0, -2}; }

TargetState AdvanceTarget(Target& target, bool wait, int (*read)(void* context),
                          void (*send)(void* context, int direction), void* context)
{
    int current = read(context);
    if (current == target.index)
        return TargetState::Reached;
    if (current < 0 || target.framesLeft <= 0)
        return TargetState::GaveUp;
    --target.framesLeft;
    if (!wait)
        StepSelection(target.index, target.count, target.wraps, read, send, context);
    int now = read(context);
    if (now == target.index)
        return TargetState::Reached;
    if (now < 0)
        return TargetState::GaveUp;
    if (now == target.lastSeen) {
        if (++target.stuckFrames > kMaxStuckFrames)
            return TargetState::GaveUp;
    } else {
        target.stuckFrames = 0;
        target.lastSeen = now;
    }
    return TargetState::Moving;
}

int OptionSide(const Rect& item, float x)
{
    float width = item.right - item.left;
    if (x < item.left + width * 0.3f)
        return -1;
    if (x >= item.right - width * 0.3f)
        return 1;
    return 0;
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

struct MouseState {
    float x = 0.0f, y = 0.0f;   // window pixels
    int windowWidth = 0, windowHeight = 0;
    bool used = false;          // it has moved, clicked or turned its wheel since the game started
    bool moved = false;         // this frame
    int leftClicks = 0, rightClicks = 0;
    int wheel = 0;              // notches, away from the user positive
};
MouseState s_mouse;
bool s_cursorShown = false; // (the window starts with it hidden: Sys::CreateSdlWindow)
// The item last under the cursor (only compared, never followed).
const CScreenElement* s_hoverItem = nullptr;
uint32_t s_hoverId = 0;

void ReadWindowSize(uint32_t windowId)
{
    if (SDL_Window* window = SDL_GetWindowFromID(windowId))
        SDL_GetWindowSize(window, &s_mouse.windowWidth, &s_mouse.windowHeight);
}

void HandleMouseEvent(SDL_Event* event)
{
    switch (event->type) {
    case SDL_MOUSEMOTION:
        if (event->motion.xrel == 0 && event->motion.yrel == 0)
            break;
        s_mouse.x = static_cast<float>(event->motion.x);
        s_mouse.y = static_cast<float>(event->motion.y);
        ReadWindowSize(event->motion.windowID);
        s_mouse.moved = true;
        s_mouse.used = true;
        break;
    case SDL_MOUSEBUTTONDOWN:
        s_mouse.x = static_cast<float>(event->button.x);
        s_mouse.y = static_cast<float>(event->button.y);
        ReadWindowSize(event->button.windowID);
        if (event->button.button == SDL_BUTTON_LEFT)
            ++s_mouse.leftClicks;
        else if (event->button.button == SDL_BUTTON_RIGHT)
            ++s_mouse.rightClicks;
        s_mouse.used = true;
        break;
    case SDL_MOUSEWHEEL:
        s_mouse.wheel += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        s_mouse.used = true;
        break;
    default:
        break;
    }
}

void* ElementManager() { return GameVariable<void*>(0x00701440); }
CScreenElement* RootElement(void* manager)
{
    return *reinterpret_cast<CScreenElement**>(static_cast<uint8_t*>(manager) + 0x44);
}
void* Tracker() { return GameVariable<void*>(0x006F0FC0); }

bool InParkEditor()
{
    Front::ElementPtr element;
    Front::GetElement(&element, kParkEditorElement);
    return element.Get() != nullptr;
}

// Whether a menu gets the pad events now: it has the focus, and - while the element manager gives pad events
// only to the elements of the highest priority (+0x4E8 > 0, for the pad events listed at +0x5DC, +0x69C of them) -
// it is one of those (the start of the menu's event handler 0x004A8100: 0x004A86B0, 0x004AAD50, 0x004A8F20,
// 0x004AB7C0).
bool MenuTakesPadEvents(CScreenElement* menu)
{
    if (!MenuHasFocus(menu))
        return false;
    auto* manager = reinterpret_cast<uint8_t*>(AsObject(menu)->mp_manager);
    if (!manager)
        return false;
    if (*reinterpret_cast<int*>(manager + 0x4E8) > 0) {
        bool listed = false;
        int count = *reinterpret_cast<int*>(manager + 0x69C);
        for (int i = 0; i < count; ++i)
            if (reinterpret_cast<uint32_t*>(manager + 0x5DC)[i] == kPadChoose)
                listed = true;
        if (listed) {
            if (!FlagInChain(menu, kFlagPriority))
                return false;
            if (!reinterpret_cast<Front::CScreenElementManager*>(manager)->IsTopPriority(Id(menu)))
                return false;
        }
    }
    return true;
}

// --- the debug log ([features] debug_mouse) ---

bool s_logEvents = false; // (while handling a click: the events sent are logged)

const char* TypeName(uint32_t type)
{
    switch (type) {
    case kVMenu: return "vmenu";
    case kHMenu: return "hmenu";
    case kWindowElement: return "windowelement";
    case kVScrollingMenu: return "vscrollingmenu";
    case kHScrollingMenu: return "hscrollingmenu";
    case 0x5B9DA842: return "containerelement";
    case 0x5200DFB6: return "textelement";
    case 0xB12B510A: return "spriteelement";
    case 0x40D92263: return "textblockelement";
    case 0x82C59D78: return "element3d";
    default: return "?";
    }
}

const char* EventName(uint32_t event)
{
    switch (event) {
    case kPadUp: return "pad_up";
    case kPadDown: return "pad_down";
    case kPadLeft: return "pad_left";
    case kPadRight: return "pad_right";
    case kPadChoose: return "pad_choose";
    case kPadBack: return "pad_back";
    default: return "?";
    }
}

void LogElement(const char* what, const CScreenElement* element)
{
    Rect r = ElementRect(element);
    Log("  %s %08X (%s %08X): rect (%.1f, %.1f)-(%.1f, %.1f), angle %.3f, alpha %.2f, flags %08X", what, Id(element),
        TypeName(Type(element)), Type(element), r.left, r.top, r.right, r.bottom, Angle(element), Alpha(element),
        Flags(element));
}

// --- the menus ---

// As InputLogic launches a pad event: for the first controller's device, unless its mapping is inactive or menu
// input is blocked.
void SendPadEvent(Mdl::FrontEnd* frontEnd, uint32_t event)
{
    const char* notSent = nullptr;
    Mdl::FrontEndInputHandler* handler = frontEnd->m_inputHandlers[0];
    if (frontEnd->m_padMappings[0].m_mapping == 0)
        notSent = "the first controller's pad mapping is inactive";
    else if (frontEnd->m_blockMenuInput)
        notSent = "menu input is blocked (SetButtonEventMappings block_menu_input)";
    else if (!handler || !handler->m_device)
        notSent = "no input device";
    if (s_logEvents && DebugEnabled())
        Log("  %s %s%s%s", notSent ? "NOT sent:" : "sent", EventName(event), notSent ? " - " : "",
            notSent ? notSent : "");
    if (notSent)
        return;
    Script::CStruct params;
    params.Init();
    params.AddInteger(kDeviceNum, *reinterpret_cast<int*>(handler->m_device + 0xC));
    params.AddInteger(kController, 0);
    static_cast<Obj::CTracker*>(Tracker())->LaunchEvent(event, kMenuEvent, kMenuEvent, &params, false, -1.0f);
    params.Clear();
}

// The item's own handler for `type` (its script's event handler table).
bool HandlesEvent(const CScreenElement* item, uint32_t type)
{
    Script::CScript* script = AsObject(item)->m_script;
    if (!script || !script->m_eventHandlers)
        return false;
    const Obj::CEventHandlerTable* table = script->m_eventHandlers;
    for (int i = 0; i < table->m_count; ++i)
        if (table->m_entries[i].type == type && table->m_entries[i].script != Obj::kNullScript)
            return true;
    return false;
}

// The child at `index` (null if none).
CScreenElement* ChildAt(const CScreenElement* menu, int index)
{
    if (index < 0)
        return nullptr;
    CScreenElement* child = FirstChild(menu);
    for (; child && index > 0; --index)
        child = NextSibling(child);
    return child;
}

void LogMenu(CScreenElement* menu)
{
    Log("  menu %08X (%s): selected %d of %d, focus %d, takes pad events %d, movement %d, wraps %d%s", Id(menu),
        TypeName(Type(menu)), SelectedIndex(menu), ChildCount(menu), MenuHasFocus(menu), MenuTakesPadEvents(menu),
        Field<uint8_t>(menu, 0x1A0), MenuWraps(menu), IsRotatingMenu(menu) ? ", turned (a spin menu)" : "");
}

// What is under the cursor when no item is: every element (not hidden) whose own box holds the point, with the
// nearest menu above it. At most a dozen.
void LogElementsAt(CScreenElement* element, float x, float y, int& lines)
{
    if (Flags(element) & kFlagHidden)
        return;
    Box box = ElementBox(element);
    if (lines < 12 && box.width != 0.0f && box.height != 0.0f && BoxContains(box, x, y)) {
        ++lines;
        LogElement("under the cursor:", element);
        for (CScreenElement* parent = Parent(element); parent; parent = Parent(parent)) {
            if (IsMenu(parent)) {
                LogMenu(parent);
                break;
            }
        }
    }
    for (CScreenElement* child = FirstChild(element); child; child = NextSibling(child))
        LogElementsAt(child, x, y, lines);
}

struct Stepper {
    Mdl::FrontEnd* frontEnd;
    Front::ElementPtr menu; // (cleared if a handler destroys it)
    bool vertical;
};

int ReadStepper(void* context)
{
    auto* stepper = static_cast<Stepper*>(context);
    return stepper->menu ? SelectedIndex(stepper->menu.Get()) : -1;
}

void SendStep(void* context, int direction)
{
    auto* stepper = static_cast<Stepper*>(context);
    uint32_t event = stepper->vertical ? (direction < 0 ? kPadUp : kPadDown) : (direction < 0 ? kPadLeft : kPadRight);
    SendPadEvent(stepper->frontEnd, event);
}

// The item the mouse is moving a menu's selection to, over as many frames as the menu needs (AdvanceTarget), and
// what to send when it gets there (a click's pad_choose / pad_left / pad_right; 0 for a hover).
struct Pending {
    bool active = false;
    const CScreenElement* item = nullptr; // (only compared, never followed)
    uint32_t itemId = 0;
    Target target = {};
    uint32_t chooseEvent = 0;
};
Pending s_pending;

// Its menu: a smart pointer (cleared if the menu dies), allocated once and never freed - no destructor running
// on the game's memory at exit.
Front::ElementPtr& PendingMenu()
{
    static Front::ElementPtr* menu = new Front::ElementPtr;
    return *menu;
}

void ClearPending(const char* why)
{
    if (s_pending.active && s_pending.chooseEvent && DebugEnabled())
        Log("mouse: not choosing item %08X: %s", s_pending.itemId, why);
    s_pending.active = false;
    PendingMenu().Release();
}

TargetState AdvancePending(Mdl::FrontEnd* frontEnd)
{
    CScreenElement* menu = PendingMenu().Get();
    // The menu still there, with the item still at its place (compared by address, then id).
    CScreenElement* item = menu ? ChildAt(menu, s_pending.target.index) : nullptr;
    if (!item || item != s_pending.item || Id(item) != s_pending.itemId)
        return TargetState::GaveUp;
    s_pending.target.count = ChildCount(menu);
    Stepper stepper;
    stepper.frontEnd = frontEnd;
    stepper.menu.Set(menu);
    stepper.vertical = IsVerticalMenu(menu);
    // Nothing to send while the menu doesn't take pad events, or while it is on an item it passes over (its own
    // focus script moves on).
    CScreenElement* current = ChildAt(menu, SelectedIndex(menu));
    bool wait = !MenuTakesPadEvents(menu) || (current && ItemPassedOver(current));
    return AdvanceTarget(s_pending.target, wait, ReadStepper, SendStep, &stepper);
}

// Settle the pending item: there - send its event (a click's); given up - forget it. True if it got there.
bool SettlePending(Mdl::FrontEnd* frontEnd, TargetState state)
{
    if (state == TargetState::Moving)
        return false;
    uint32_t event = s_pending.chooseEvent;
    if (state == TargetState::GaveUp) {
        ClearPending("its menu's selection didn't get there (or the menu / item is gone)");
        return false;
    }
    if (event && DebugEnabled())
        Log("mouse: item %08X selected", s_pending.itemId);
    s_pending.active = false;
    PendingMenu().Release();
    if (event) {
        bool logging = s_logEvents;
        s_logEvents = true;
        SendPadEvent(frontEnd, event);
        s_logEvents = logging;
    }
    return true;
}

// Aim at an item: start moving its menu's selection there - and, with `event`, send it once there. True if it is
// selected already.
bool AimAt(Mdl::FrontEnd* frontEnd, const Hit& hit, uint32_t event)
{
    s_pending.active = true;
    PendingMenu().Set(hit.menu);
    s_pending.item = hit.item;
    s_pending.itemId = Id(hit.item);
    s_pending.target = StartTarget(hit.index, ChildCount(hit.menu), MenuWraps(hit.menu));
    s_pending.chooseEvent = event;
    return SettlePending(frontEnd, AdvancePending(frontEnd));
}

// Select the item under the cursor - when it isn't the one that was (or on a `click`). Moving an outer menu can
// focus an inner one (an option row's hmenu), so a few passes. A turned (spin) menu isn't moved by hovering.
void SelectUnderCursor(Mdl::FrontEnd* frontEnd, float x, float y, bool click)
{
    for (int pass = 0; pass < 4; ++pass) {
        Hit hit;
        if (!FindItemAt(RootElement(ElementManager()), x, y, MenuTakesPadEvents, &hit)) {
            s_hoverItem = nullptr;
            s_hoverId = 0;
            return;
        }
        bool changed = hit.item != s_hoverItem || Id(hit.item) != s_hoverId;
        s_hoverItem = hit.item;
        s_hoverId = Id(hit.item);
        if (!changed && !(click && pass == 0))
            return;
        if (!click && IsRotatingMenu(hit.menu))
            return;
        if (!AimAt(frontEnd, hit, 0))
            return; // (on its way: the rest in the next frames)
    }
}

void Click(Mdl::FrontEnd* frontEnd, float x, float y)
{
    bool debug = DebugEnabled();
    s_logEvents = debug;
    if (debug)
        Log("mouse: left click at layout (%.1f, %.1f)", x, y);
    SelectUnderCursor(frontEnd, x, y, true);
    Hit hit;
    if (!FindItemAt(RootElement(ElementManager()), x, y, MenuTakesPadEvents, &hit)) {
        if (debug) {
            Log("  no item of a menu that takes pad events under the cursor - nothing sent");
            int lines = 0;
            if (CScreenElement* root = RootElement(ElementManager()))
                LogElementsAt(root, x, y, lines);
        }
        s_logEvents = false;
        return;
    }
    uint32_t event = kPadChoose;
    if (IsVerticalMenu(hit.menu) && (HandlesEvent(hit.item, kPadLeft) || HandlesEvent(hit.item, kPadRight))) {
        int side = OptionSide(ElementRect(hit.item), x);
        if (side < 0)
            event = kPadLeft;
        else if (side > 0)
            event = kPadRight;
    }
    if (debug) {
        LogElement("item", hit.item);
        Rect area = ItemBounds(hit.item, hit.menu);
        Log("    index %d, area (%.1f, %.1f)-(%.1f, %.1f)", hit.index, area.left, area.top, area.right, area.bottom);
        LogMenu(hit.menu);
    }
    if (s_pending.active && s_pending.item == hit.item && PendingMenu().Get() == hit.menu) {
        s_pending.chooseEvent = event;
        if (debug)
            Log("  the menu is on its way to the item (selected %d): %s once it gets there", SelectedIndex(hit.menu),
                EventName(event));
    } else if (SelectedIndex(hit.menu) == hit.index) {
        ClearPending("another item was clicked");
        SendPadEvent(frontEnd, event);
    } else if (!AimAt(frontEnd, hit, event) && s_pending.active && debug) {
        Log("  the menu is on its way to the item (selected %d): %s once it gets there", SelectedIndex(hit.menu),
            EventName(event));
    }
    s_logEvents = false;
}

void ShowCursor(bool show)
{
    if (show == s_cursorShown)
        return;
    SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
    s_cursorShown = show;
}

} // namespace

bool& Enabled()
{
    static bool on = ConfigInt("features", "mouse_menus", 1) != 0;
    return on;
}

bool& DebugEnabled()
{
    static bool on = ConfigInt("features", "debug_mouse", 0) != 0;
    return on;
}

void Init()
{
    if (!Enabled())
        return;
    Platform::RegisterEventHandler(HandleMouseEvent);
}

void Update(Mdl::FrontEnd* frontEnd)
{
    // (not original either: the THUGPRO peer mode's host options applied once a level is up -
    // game/net/thugpro_peer.h)
    GameNet::Thugpro::Update();
    // Not original either - the level editor (game/front/level_editor.h) has the mouse while it runs: no menu
    // events (the clicks and wheel meanwhile are dropped), the cursor left as the editor sets it; its own input logic
    // runs here instead.
    // Original behaviour (of the mouse_menus feature): the mouse drives the menus.
    if (Front::LevelEditor::Active()) {
        s_mouse.moved = false;
        s_mouse.leftClicks = 0;
        s_mouse.rightClicks = 0;
        s_mouse.wheel = 0;
        ClearPending("the level editor runs");
        Front::LevelEditor::InputLogic();
        return;
    }
    // (not original either: the restart tester's key, F8 - game/front/restart_tester.h)
    Front::RestartTester::Update(frontEnd);
    // (and play from here's way back to the level editor, F5 / Ctrl+P - game/front/level_editor_play.h)
    Front::LevelEditor::Play::Update(frontEnd);
    // (and play testing: the skater's trail recorded, a restart's play from here - game/front/level_editor_playtest.h)
    Front::LevelEditor::PlayTest::Update(frontEnd);
    // (and debug mode's viewer and F keys - game/sys/debug_mode.h)
    Sys::DebugMode::Update(frontEnd);
    // (and the start level: -level <name> - game/front/start_level.h)
    Front::StartLevel::Update(frontEnd);
    // (and the test-automation channel: commands from a file - game/front/automation.h)
    Front::Automation::Update(frontEnd);
    if (!Enabled())
        return;
    MouseState input = s_mouse;
    s_mouse.moved = false;
    s_mouse.leftClicks = 0;
    s_mouse.rightClicks = 0;
    s_mouse.wheel = 0;

    // The front end, or a level's pause menu; never the park editor's own cursor.
    void* manager = ElementManager();
    bool menus = manager && RootElement(manager) && (Nx::CurrentLevel() == 0 || frontEnd->m_paused);
    if (menus && InParkEditor() && !frontEnd->m_paused)
        menus = false;
    if (DebugEnabled()) {
        // (debug_mouse: each change of whether the mouse drives the menus, with what decided it)
        static int s_lastMenus = -1, s_lastPaused = -1;
        if (static_cast<int>(menus) != s_lastMenus || static_cast<int>(frontEnd->m_paused) != s_lastPaused) {
            s_lastMenus = menus;
            s_lastPaused = frontEnd->m_paused;
            Log("mouse: menus %s (level %u, front end paused %d, element manager %d, root %d, park editor %d)",
                menus ? "active" : "inactive", Nx::CurrentLevel(), frontEnd->m_paused, manager != nullptr,
                manager && RootElement(manager) != nullptr, InParkEditor());
        }
    }
    ShowCursor(menus);
    if (!menus) {
        ClearPending("the menus are gone");
        return;
    }
    // A selection on its way to an item from earlier frames.
    if (s_pending.active) {
        s_logEvents = s_pending.chooseEvent != 0;
        SettlePending(frontEnd, AdvancePending(frontEnd));
        s_logEvents = false;
    }
    if (!input.used)
        return;

    float x, y;
    if (!WindowToLayout(CurrentMapping(input.windowWidth, input.windowHeight), input.x, input.y, &x, &y))
        return;
    if (input.moved)
        SelectUnderCursor(frontEnd, x, y, false);
    for (int i = 0; i < input.leftClicks; ++i)
        Click(frontEnd, x, y);
    if (input.rightClicks || input.wheel)
        ClearPending("a right click or the wheel");
    for (int i = 0; i < input.rightClicks; ++i) {
        bool any = AnyMenu(RootElement(ElementManager()), MenuTakesPadEvents, false);
        if (DebugEnabled())
            Log("mouse: right click at layout (%.1f, %.1f)%s", x, y, any ? "" : ": no menu takes pad events - nothing sent");
        s_logEvents = true;
        if (any)
            SendPadEvent(frontEnd, kPadBack);
        s_logEvents = false;
    }
    int notches = std::max(-5, std::min(5, input.wheel));
    for (; notches != 0; notches += notches > 0 ? -1 : 1)
        if (AnyMenu(RootElement(ElementManager()), MenuTakesPadEvents, true))
            SendPadEvent(frontEnd, notches > 0 ? kPadUp : kPadDown);
}

bool MenuGetsPadEvents(CScreenElement* menu) { return MenuTakesPadEvents(menu); }

void SendMenuPadEvent(Mdl::FrontEnd* frontEnd, uint32_t event) { SendPadEvent(frontEnd, event); }

} // namespace MouseMenus
