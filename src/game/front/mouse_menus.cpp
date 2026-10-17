// Mouse control of the menus: the mouse_menus feature (thug2-decomp.ini [features] mouse_menus). Our own
// addition - not original game code, not PartyMod's. See mouse_menus.h.

#include "game/front/mouse_menus.h"

#include <algorithm>
#include <cstddef>

#include <SDL2/SDL.h>

#include "game/front/front_end.h"
#include "game/front/screen_element.h"
#include "game/gfx/screen.h"
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

// The item's area: its own, and across its menu (a vertical menu's width, a horizontal one's height).
Rect ItemArea(const CScreenElement* item, const CScreenElement* menu)
{
    Rect area = ElementRect(item);
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
    if (parentAccepts && ItemFocusable(element)) {
        Rect area = ItemArea(element, parent);
        if (area.Contains(search.x, search.y) && (!clipped || clip.Contains(search.x, search.y))) {
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

Rect ElementRect(const CScreenElement* element)
{
    float left = Field<float>(element, 0x134);
    float top = Field<float>(element, 0x138);
    float width = Field<float>(element, 0x160) * Field<float>(element, 0x13C);
    float height = Field<float>(element, 0x164) * Field<float>(element, 0x140);
    Rect rect = {left, top, left + width, top + height};
    if (rect.right < rect.left)
        std::swap(rect.left, rect.right);
    if (rect.bottom < rect.top)
        std::swap(rect.top, rect.bottom);
    return rect;
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
            if (!GameFunction<bool(__thiscall*)(void*, uint32_t)>(0x004AB7C0)(manager, Id(menu)))
                return false;
        }
    }
    return true;
}

// As InputLogic launches a pad event: for the first controller's device, unless its mapping is inactive or menu
// input is blocked.
void SendPadEvent(Mdl::FrontEnd* frontEnd, uint32_t event)
{
    if (frontEnd->m_padMappings[0].m_mapping == 0 || frontEnd->m_blockMenuInput)
        return;
    Mdl::FrontEndInputHandler* handler = frontEnd->m_inputHandlers[0];
    if (!handler || !handler->m_device)
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

void SelectItem(Mdl::FrontEnd* frontEnd, const Hit& hit)
{
    Stepper stepper;
    stepper.frontEnd = frontEnd;
    stepper.menu.Set(hit.menu);
    stepper.vertical = IsVerticalMenu(hit.menu);
    StepSelection(hit.index, ChildCount(hit.menu), MenuWraps(hit.menu), ReadStepper, SendStep, &stepper);
}

// Select the item under the cursor - when it isn't the one that was (or `always`, for a click). Moving an outer
// menu can focus an inner one (an option row's hmenu), so a few passes.
void SelectUnderCursor(Mdl::FrontEnd* frontEnd, float x, float y, bool always)
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
        if (!changed && !(always && pass == 0))
            return;
        SelectItem(frontEnd, hit);
    }
}

void Click(Mdl::FrontEnd* frontEnd, float x, float y)
{
    SelectUnderCursor(frontEnd, x, y, true);
    Hit hit;
    if (!FindItemAt(RootElement(ElementManager()), x, y, MenuTakesPadEvents, &hit))
        return;
    if (SelectedIndex(hit.menu) != hit.index)
        return; // (it couldn't be selected)
    uint32_t event = kPadChoose;
    if (IsVerticalMenu(hit.menu) && (HandlesEvent(hit.item, kPadLeft) || HandlesEvent(hit.item, kPadRight))) {
        int side = OptionSide(ElementRect(hit.item), x);
        if (side < 0)
            event = kPadLeft;
        else if (side > 0)
            event = kPadRight;
    }
    SendPadEvent(frontEnd, event);
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

void Init()
{
    if (!Enabled())
        return;
    Platform::RegisterEventHandler(HandleMouseEvent);
}

void Update(Mdl::FrontEnd* frontEnd)
{
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
    ShowCursor(menus);
    if (!menus || !input.used)
        return;

    float x, y;
    if (!WindowToLayout(CurrentMapping(input.windowWidth, input.windowHeight), input.x, input.y, &x, &y))
        return;
    if (input.moved)
        SelectUnderCursor(frontEnd, x, y, false);
    for (int i = 0; i < input.leftClicks; ++i)
        Click(frontEnd, x, y);
    for (int i = 0; i < input.rightClicks; ++i)
        if (AnyMenu(RootElement(ElementManager()), MenuTakesPadEvents, false))
            SendPadEvent(frontEnd, kPadBack);
    int notches = std::max(-5, std::min(5, input.wheel));
    for (; notches != 0; notches += notches > 0 ? -1 : 1)
        if (AnyMenu(RootElement(ElementManager()), MenuTakesPadEvents, true))
            SendPadEvent(frontEnd, notches > 0 ? kPadUp : kPadDown);
}

} // namespace MouseMenus
