// Self-tests for the mouse_menus feature (src/game/front/mouse_menus.cpp - our own addition, no original to compare
// with): its pure pieces against worked-out values. The window -> layout mapping through the game's own conversion
// (Nx::SetLetterbox) for several window / back buffer sizes, front end and level, widescreen front end on and off;
// hit-testing on a stand-in element tree laid out with the original's justification (0x004A8C30); the hover
// stepping against a stand-in menu that moves as the game's (0x004A7850: skipping items it can't focus, wrapping
// or bouncing at the ends), with its events recorded.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/mouse_menus.h"
#include "game/front/screen_element.h"
#include "game/gfx/screen.h"
#include "game/script/struct.h"
#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using Front::CScreenElement;

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// --- window -> layout ---------------------------------------------------------------------------------------------

struct Saved {
    uint32_t address;
    uint32_t size;
    uint8_t bytes[0x24];
};

void TestMouseMapping()
{
    // The conversion and back buffer (0x00786D6C-0x00786D8F), the letterbox flag, the skate module (the level).
    Saved saved[] = {{0x00786D6C, 0x24, {}}, {0x00786CBE, 1, {}}, {0x007CE478, 4, {}}};
    for (Saved& s : saved)
        std::memcpy(s.bytes, reinterpret_cast<void*>(s.address), s.size);
    int savedWidth = Sys::WindowWidth(), savedHeight = Sys::WindowHeight();
    bool savedWidescreen = Nx::WidescreenMenus();

    alignas(16) static uint8_t career[0x640];
    alignas(16) static uint8_t skater[0x40];
    *reinterpret_cast<void**>(skater + 0x20) = career;
    GameVariable<void*>(0x007CE478) = skater;

    struct Point {
        float windowX, windowY, layoutX, layoutY;
    };
    struct Case {
        int level;
        int resX, resY;         // the settings' window size (a level's aspect)
        uint32_t backW, backH;  // the back buffer
        int windowW, windowH;   // the window it's shown in
        Point points[4];
        int count;
    };
    const Case cases[] = {
        // Front end, 16:9: 4:3 layout pillarboxed: scale 2.25, offset (240, 36).
        {0, 1920, 1080, 1920, 1080, 1920, 1080,
         {{960, 576, 320, 240}, {240, 36, 0, 0}, {1680, 1044, 640, 448}, {100, 500, -62.2222f, 206.2222f}}, 4},
        // The same back buffer in a smaller window (1280x720 shows 1920x1080).
        {0, 1920, 1080, 1920, 1080, 1280, 720, {{640, 360, 320, 224}, {160, 24, 0, 0}}, 2},
        // A 1920x1080 back buffer squeezed into a 1280x1024 window (not proportional).
        {0, 1920, 1080, 1920, 1080, 1280, 1024, {{640, 512, 320, 224}, {160, 34.1333f, 0, 0}}, 2},
        // A 1600x900 back buffer stretched to a 1920x1080 window: scale 1.875, offset (200, 30).
        {0, 1920, 1080, 1600, 900, 1920, 1080, {{960, 540, 320, 224}, {240, 36, 0, 0}}, 2},
        // Front end, 4:3: scale 1.6, offset (0, 25) (16 * 1.6 truncated).
        {0, 1024, 768, 1024, 768, 1024, 768, {{512, 409, 320, 240}, {0, 25, 0, 0}, {1024, 25, 640, 0}}, 3},
        // Front end, 5:4 (narrower than 4:3): letterboxed, scale 2, offset (0, 32 + 32).
        {0, 1280, 1024, 1280, 1024, 1280, 1024, {{640, 544, 320, 240}, {1280, 64, 640, 0}}, 2},
        // A level, 16:10: the layout stretched to the window's shape: scale (3, 2.5), offset (0, 40).
        {7, 1920, 1200, 1920, 1200, 1920, 1200, {{960, 640, 320, 240}, {0, 40, 0, 0}, {1920, 1160, 640, 448}}, 3},
        // A level, 16:9: scale 3 x 2.25, offset (0, 36).
        {7, 1920, 1080, 1920, 1080, 1920, 1080, {{960, 576, 320, 240}, {300, 36, 100, 0}}, 2},
    };
    char what[160];
    for (bool widescreen : {true, false}) {
        Nx::WidescreenMenus() = widescreen; // (the layout stays 4:3 in the front end either way)
        for (const Case& c : cases) {
            career[0x630] = static_cast<uint8_t>(c.level);
            Sys::WindowWidth() = c.resX;
            Sys::WindowHeight() = c.resY;
            GameVariable<uint32_t>(0x00786D6C) = c.backW;
            GameVariable<uint32_t>(0x00786D70) = c.backH;
            Nx::SetLetterbox(false);
            MouseMenus::ScreenMapping mapping = MouseMenus::CurrentMapping(c.windowW, c.windowH);
            for (int i = 0; i < c.count; ++i) {
                const Point& p = c.points[i];
                float x = -1.0f, y = -1.0f;
                bool ok = MouseMenus::WindowToLayout(mapping, p.windowX, p.windowY, &x, &y);
                std::snprintf(what, sizeof(what), "mouse mapping level %d back %ux%u window %dx%d (%g, %g) widescreen %d",
                              c.level, c.backW, c.backH, c.windowW, c.windowH, p.windowX, p.windowY, widescreen);
                Check(ok && Near(x, p.layoutX) && Near(y, p.layoutY), what);
                if (!(ok && Near(x, p.layoutX) && Near(y, p.layoutY)))
                    Log("  got (%f, %f), expected (%f, %f)", x, y, p.layoutX, p.layoutY);
            }
        }
    }
    // Nothing to map onto: no window size, no back buffer, no scale.
    MouseMenus::ScreenMapping good = {640, 480, 640, 480, 1.0f, 1.0f, 0, 16};
    float x = 7.0f, y = 7.0f;
    Check(MouseMenus::WindowToLayout(good, 10, 20, &x, &y) && Near(x, 10) && Near(y, 4), "mouse mapping 1:1");
    MouseMenus::ScreenMapping bad[] = {good, good, good, good, good};
    bad[0].windowWidth = 0;
    bad[1].windowHeight = 0;
    bad[2].backBufferWidth = 0;
    bad[3].backBufferHeight = 0;
    bad[4].scaleY = 0.0f;
    for (const MouseMenus::ScreenMapping& m : bad) {
        x = 7.0f;
        Check(!MouseMenus::WindowToLayout(m, 10, 20, &x, &y) && x == 7.0f, "mouse mapping refuses empty sizes");
    }

    for (Saved& s : saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes, s.size);
    Sys::WindowWidth() = savedWidth;
    Sys::WindowHeight() = savedHeight;
    Nx::WidescreenMenus() = savedWidescreen;
}

// --- hit-testing ----------------------------------------------------------------------------------------------------

constexpr uint32_t kVMenu = 0x130EF802;
constexpr uint32_t kHMenu = 0xCCDED1E1;
constexpr uint32_t kWindow = 0xA13ADBB7;
constexpr uint32_t kContainer = 0x5B9DA842;
constexpr uint32_t kText = 0x5200DFB6;

struct Tree {
    alignas(16) uint8_t elements[40][0x1C0];
    int used = 0;
    // A tag structure holding the flag tag_not_focusable.
    Script::CComponent notFocusableFlag;
    Script::CStruct notFocusableTags;
};

template <typename T>
T& At(CScreenElement* e, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(e) + offset);
}

// A stand-in element under `parent` (appended to its children): local position, justification (-1 left/top, 0
// centre, 1 right/bottom), base dims, scale and alpha; then laid out as the game does (0x004A9D30 without rotation):
// the original's 0x004A8C30 gives the local top left from the justification, and the absolute values are the
// parent's position plus its scale times the local ones.
CScreenElement* Make(Tree& tree, CScreenElement* parent, uint32_t type, float x, float y, float justX, float justY,
                     float width, float height, float scale = 1.0f, float alpha = 1.0f, float scaleY = 0.0f)
{
    if (scaleY == 0.0f)
        scaleY = scale;
    if (tree.used == static_cast<int>(sizeof(tree.elements) / sizeof(tree.elements[0])))
        throw "mouse hit test: too many stand-in elements"; // (a crash for the test's guard)
    auto* e = reinterpret_cast<CScreenElement*>(tree.elements[tree.used++]);
    std::memset(e, 0, 0x1C0);
    At<uint32_t>(e, 0x14) = 0x1000 + tree.used; // id
    At<uint32_t>(e, 0x18) = type;
    At<CScreenElement*>(e, 0x4C) = parent;
    At<float>(e, 0x7C) = alpha;
    At<float>(e, 0x90) = x;
    At<float>(e, 0x94) = y;
    At<float>(e, 0xA4) = scale;
    At<float>(e, 0xA8) = scaleY;
    At<float>(e, 0x158) = justX;
    At<float>(e, 0x15C) = justY;
    At<float>(e, 0x160) = width;
    At<float>(e, 0x164) = height;
    GameFunction<void(__thiscall*)(void*, void*)>(0x004A8C30)(e, reinterpret_cast<uint8_t*>(e) + 0x7C);
    if (parent) {
        // The parent's angle turns the scaled local top left (0x004A9D30); the element starts with that angle (Turn
        // adds its own).
        double angle = At<float>(parent, 0x14C);
        double c = std::cos(angle), s = std::sin(angle);
        double lx = At<float>(parent, 0x13C) * At<float>(e, 0x9C), ly = At<float>(parent, 0x140) * At<float>(e, 0xA0);
        At<float>(e, 0x134) = static_cast<float>(At<float>(parent, 0x134) + (c * lx - s * ly));
        At<float>(e, 0x138) = static_cast<float>(At<float>(parent, 0x138) + (s * lx + c * ly));
        At<float>(e, 0x14C) = At<float>(parent, 0x14C);
        At<float>(e, 0x13C) = At<float>(parent, 0x13C) * scale;
        At<float>(e, 0x140) = At<float>(parent, 0x140) * scaleY;
        At<float>(e, 0x114) = At<float>(parent, 0x114) * alpha;
        CScreenElement** link = &At<CScreenElement*>(parent, 0x58);
        while (*link)
            link = &At<CScreenElement*>(*link, 0x64);
        *link = e;
    } else {
        At<float>(e, 0x134) = At<float>(e, 0x9C);
        At<float>(e, 0x138) = At<float>(e, 0xA0);
        At<float>(e, 0x13C) = scale;
        At<float>(e, 0x140) = scaleY;
        At<float>(e, 0x114) = alpha;
    }
    return e;
}

CScreenElement* Menu(Tree& tree, CScreenElement* parent, bool vertical, bool focused, float x, float y, float width,
                     float height, float scale = 1.0f, float alpha = 1.0f, float scaleY = 0.0f)
{
    CScreenElement* menu =
        Make(tree, parent, vertical ? kVMenu : kHMenu, x, y, -1, -1, width, height, scale, alpha, scaleY);
    At<int>(menu, 0x178) = 0;
    At<uint8_t>(menu, 0x180) = focused;
    At<uint8_t>(menu, 0x190) = vertical;
    return menu;
}

bool FocusedOnly(CScreenElement* menu) { return MouseMenus::MenuHasFocus(menu); }

// An element's own rotation (rot_angle, radians), before its children are made: added to its absolute angle.
void Turn(CScreenElement* e, float angle)
{
    At<float>(e, 0xB4) = angle;
    At<float>(e, 0x14C) += angle;
}

void TestMouseHitTest()
{
    static Tree tree;
    tree.used = 0;
    tree.notFocusableFlag = {};
    tree.notFocusableFlag.typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_NAME << 1);
    tree.notFocusableFlag.checksum = 0xF33A3321; // tag_not_focusable
    tree.notFocusableTags = {};
    tree.notFocusableTags.m_head = &tree.notFocusableFlag;

    CScreenElement* root = Make(tree, nullptr, kWindow, 0, 0, -1, -1, 640, 448);
    // A: a focused vertical menu at (100, 100), 200 x 125.
    CScreenElement* a = Menu(tree, root, true, true, 100, 100, 200, 125);
    CScreenElement* a0 = Make(tree, a, kText, 10, 0, -1, -1, 80, 20);    // (110,100)-(190,120)
    CScreenElement* a1 = Make(tree, a, kText, 10, 25, -1, -1, 80, 20);   // (110,125)-(190,145) hidden
    At<uint32_t>(a1, 0x08) = 0x400000;
    CScreenElement* a2 = Make(tree, a, kText, 10, 50, -1, -1, 80, 20);   // (110,150)-(190,170) not focusable
    At<Script::CStruct*>(a2, 0x20) = &tree.notFocusableTags;
    CScreenElement* a3 = Make(tree, a, kText, 10, 75, -1, -1, 80, 20);   // (110,175)-(190,195) block_events
    At<uint32_t>(a3, 0x08) = 0x200000;
    CScreenElement* a4 = Make(tree, a, kContainer, 10, 100, -1, -1, 180, 25); // (110,200)-(290,225)
    //   B: a focused horizontal menu inside A's last item, its items centred on their positions.
    CScreenElement* b = Menu(tree, a4, false, true, 0, 0, 120, 25);
    CScreenElement* b0 = Make(tree, b, kText, 60, 12.5f, 0, 0, 40, 20); // (150,202.5)-(190,222.5)
    CScreenElement* b1 = Make(tree, b, kText, 100, 12.5f, 0, 0, 30, 20); // (195,202.5)-(225,222.5)
    //   an item with block_events on itself, outside A's area
    CScreenElement* a5 = Make(tree, a, kContainer, 10, 230, -1, -1, 80, 20); // (110,330)-(190,350)
    At<uint32_t>(a5, 0x08) = 0x200000;
    // N: a focused menu with block_events: its items can't be focused.
    CScreenElement* n = Menu(tree, root, true, true, 500, 300, 100, 30);
    At<uint32_t>(n, 0x08) = 0x200000;
    CScreenElement* n0 = Make(tree, n, kText, 10, 5, -1, -1, 80, 20); // (510,305)-(590,325)
    // C: an unfocused menu over A (later in the tree): its items don't count.
    CScreenElement* c = Menu(tree, root, true, false, 100, 100, 200, 125);
    Make(tree, c, kText, 10, 0, -1, -1, 80, 20);
    // D: a focused menu later in the tree, over part of A's first item.
    CScreenElement* d = Menu(tree, root, true, true, 150, 105, 100, 10);
    CScreenElement* d0 = Make(tree, d, kText, 0, 0, -1, -1, 100, 10); // (150,105)-(250,115)
    // W: a window (clips) at (400,300), 100 x 50, with a menu whose second item lies below it.
    CScreenElement* w = Make(tree, root, kWindow, 400, 300, -1, -1, 100, 50);
    CScreenElement* e = Menu(tree, w, true, true, 0, 0, 100, 100);
    CScreenElement* e0 = Make(tree, e, kText, 10, 10, -1, -1, 80, 20); // (410,310)-(490,330)
    Make(tree, e, kText, 10, 60, -1, -1, 80, 20);                      // (410,360)-(490,380), clipped
    // F: scaled 2x at (400,50): its item at local (10,5), 20 x 10 -> (420,60)-(460,80); F spans (400..500).
    CScreenElement* f = Menu(tree, root, true, true, 400, 50, 50, 20, 2.0f);
    CScreenElement* f0 = Make(tree, f, kText, 10, 5, -1, -1, 20, 10);
    // L: scaled 1 x 3 at (20,200): its item at local (5,2), 30 x 10 -> (25,206)-(55,236).
    CScreenElement* l = Menu(tree, root, true, true, 20, 200, 40, 15, 1.0f, 1.0f, 3.0f);
    CScreenElement* l0 = Make(tree, l, kText, 5, 2, -1, -1, 30, 10);
    // G: invisible (alpha 0) at (20,300); H: half visible at (20,400).
    CScreenElement* g = Menu(tree, root, true, true, 20, 300, 100, 30, 1.0f, 0.0f);
    Make(tree, g, kText, 10, 10, -1, -1, 80, 10); // (30,310)-(110,320)
    CScreenElement* h = Menu(tree, root, true, true, 20, 400, 100, 30, 1.0f, 0.5f);
    CScreenElement* h0 = Make(tree, h, kText, 10, 10, -1, -1, 80, 10); // (30,410)-(110,420)
    // K: an item justified right / bottom at local (100,40), 40 x 20 -> top left (60,20): (560,170)-(600,190).
    CScreenElement* k = Menu(tree, root, true, true, 500, 150, 100, 60);
    CScreenElement* k0 = Make(tree, k, kText, 100, 40, 1, 1, 40, 20);
    // J: a hidden menu: nothing below it counts.
    CScreenElement* j = Menu(tree, root, true, true, 300, 400, 100, 30);
    At<uint32_t>(j, 0x08) = 0x400000;
    Make(tree, j, kText, 0, 0, -1, -1, 100, 30);

    struct Expect {
        float x, y;
        CScreenElement* menu;
        CScreenElement* item;
        int index;
    };
    const Expect expects[] = {
        {120, 110, a, a0, 0},      // A's first item (not C's, which isn't focused)
        {295, 110, a, a0, 0},      // across A's width
        {120, 135, nullptr, nullptr, 0}, // hidden
        {120, 160, nullptr, nullptr, 0}, // tag_not_focusable
        {120, 185, nullptr, nullptr, 0}, // block_events
        {120, 340, nullptr, nullptr, 0}, // block_events (another item's)
        {520, 310, nullptr, nullptr, 0}, // block_events on the parent menu
        {105, 110, a, a0, 0},      // across A's width, left of the item
        {160, 201, b, b0, 0},      // across B's height, above the item
        {160, 224, b, b0, 0},      // and below it
        {160, 212, b, b0, 0},      // the deepest: B's item inside A's
        {200, 212, b, b1, 1},
        {240, 212, a, a4, 4},      // A's item outside B's items
        {160, 110, d, d0, 0},      // D is later in the tree
        {420, 320, e, e0, 0},
        {420, 370, nullptr, nullptr, 0}, // below the window
        {490, 70, f, f0, 0},       // scaled, across F's width
        {420, 85, nullptr, nullptr, 0},  // below F's item
        {30, 230, l, l0, 0},       // scaled vertically only
        {30, 240, nullptr, nullptr, 0},
        {30, 315, nullptr, nullptr, 0},  // alpha 0
        {30, 415, h, h0, 0},
        {590, 180, k, k0, 0},      // justified
        {590, 195, nullptr, nullptr, 0},
        {310, 410, nullptr, nullptr, 0}, // hidden menu
        {600, 10, nullptr, nullptr, 0},
    };
    char what[96];
    for (const Expect& x : expects) {
        MouseMenus::Hit hit = {nullptr, nullptr, -1};
        bool found = MouseMenus::FindItemAt(root, x.x, x.y, FocusedOnly, &hit);
        std::snprintf(what, sizeof(what), "mouse hit test at (%g, %g)", x.x, x.y);
        bool ok = x.item ? found && hit.menu == x.menu && hit.item == x.item && hit.index == x.index : !found;
        Check(ok, what);
        if (!ok)
            Log("  found %d: menu %p item %p index %d", found, hit.menu, hit.item, hit.index);
    }
    // Every menu counts: C's item is found over A's.
    MouseMenus::Hit hit = {};
    Check(MouseMenus::FindItemAt(root, 120, 110, [](CScreenElement*) { return true; }, &hit) && hit.menu == c,
          "mouse hit test, all menus");
    Check(!MouseMenus::FindItemAt(nullptr, 120, 110, FocusedOnly, &hit), "mouse hit test, no root");

    // The pieces.
    MouseMenus::Rect r = MouseMenus::ElementRect(k0);
    Check(Near(r.left, 560) && Near(r.top, 170) && Near(r.right, 600) && Near(r.bottom, 190), "mouse ElementRect");
    r = MouseMenus::ElementRect(f0);
    Check(Near(r.left, 420) && Near(r.top, 60) && Near(r.right, 460) && Near(r.bottom, 80), "mouse ElementRect scaled");
    Check(MouseMenus::ChildCount(a) == 6 && MouseMenus::ChildCount(b) == 2 && MouseMenus::ChildCount(a0) == 0,
          "mouse ChildCount");
    Check(MouseMenus::IsMenu(a) && MouseMenus::IsMenu(b) && !MouseMenus::IsMenu(a4) && !MouseMenus::IsMenu(root),
          "mouse IsMenu");
    Check(MouseMenus::IsVerticalMenu(a) && !MouseMenus::IsVerticalMenu(b), "mouse IsVerticalMenu");
    Check(MouseMenus::ItemFocusable(a0) && !MouseMenus::ItemFocusable(a2) && !MouseMenus::ItemFocusable(a3) &&
              MouseMenus::ItemFocusable(a1) && !MouseMenus::ItemFocusable(n0),
          "mouse ItemFocusable");
    // AnyMenu: with every menu unfocused but B (horizontal), only a vertical one is wanted.
    for (CScreenElement* m : {a, d, e, f, g, h, k, j, l, n})
        At<uint8_t>(m, 0x180) = 0;
    Check(MouseMenus::AnyMenu(root, FocusedOnly, false), "mouse AnyMenu");
    Check(!MouseMenus::AnyMenu(root, FocusedOnly, true), "mouse AnyMenu vertical");
    At<uint8_t>(j, 0x180) = 1; // (hidden: doesn't count)
    Check(!MouseMenus::AnyMenu(root, FocusedOnly, true), "mouse AnyMenu hidden");
    At<uint8_t>(k, 0x180) = 1;
    Check(MouseMenus::AnyMenu(root, FocusedOnly, true), "mouse AnyMenu vertical found");
    Check(!MouseMenus::AnyMenu(nullptr, FocusedOnly, false), "mouse AnyMenu no root");

    // Option arrows: the left and right 30%.
    MouseMenus::Rect item = {100, 0, 200, 10};
    Check(MouseMenus::OptionSide(item, 100) == -1 && MouseMenus::OptionSide(item, 129) == -1 &&
              MouseMenus::OptionSide(item, 131) == 0 && MouseMenus::OptionSide(item, 169) == 0 &&
              MouseMenus::OptionSide(item, 171) == 1 && MouseMenus::OptionSide(item, 199) == 1,
          "mouse OptionSide");
}

// --- hover stepping -------------------------------------------------------------------------------------------------

// A menu that moves as the game's (0x004A7850): the index moves by the direction; past an end it wraps, or without
// wrapping bounces (the other way, from the end); items it can't focus are passed over (at most 2 x count tries,
// else no selection). `locked`: the menu ignores the events (movement off).
struct MenuModel {
    std::vector<bool> focusable;
    bool wraps = false;
    bool locked = false;
    int selected = 0;
    int destroyAfter = -1; // the menu is destroyed after this many events (-1: never)
    std::vector<int> events;

    void Move(int direction)
    {
        int count = static_cast<int>(focusable.size());
        int tries = count * 2;
        for (; tries > 0; --tries) {
            selected += direction;
            if (selected >= count) {
                if (wraps) {
                    selected = 0;
                } else {
                    direction = -direction;
                    selected = count - 1;
                }
            } else if (selected < 0) {
                if (wraps) {
                    selected = count - 1;
                } else {
                    direction = -direction;
                    selected = 0;
                }
            }
            if (focusable[selected])
                return;
        }
        selected = -1;
    }
};

int ReadModel(void* context)
{
    auto* m = static_cast<MenuModel*>(context);
    if (m->destroyAfter >= 0 && static_cast<int>(m->events.size()) >= m->destroyAfter)
        return -1;
    return m->selected;
}

void SendModel(void* context, int direction)
{
    auto* m = static_cast<MenuModel*>(context);
    m->events.push_back(direction);
    if (!m->locked)
        m->Move(direction);
}

void TestMouseStepping()
{
    struct Case {
        const char* what;
        const char* items; // '.' focusable, 'x' not
        bool wraps, locked;
        int from, to;
        int destroyAfter;
        const char* events; // '+' / '-'
        int end;            // the selected index after
    };
    const Case cases[] = {
        {"down", ".....", false, false, 0, 3, -1, "+++", 3},
        {"up", ".....", false, false, 3, 0, -1, "---", 0},
        {"already there", ".....", false, false, 2, 2, -1, "", 2},
        {"no wrap, the long way", ".....", false, false, 0, 4, -1, "++++", 4},
        {"wrap, the short way up", ".....", true, false, 0, 4, -1, "-", 4},
        {"wrap, the short way down", "......", true, false, 5, 1, -1, "++", 1},
        {"wrap, a tie goes direct", "....", true, false, 0, 2, -1, "++", 2},
        {"wrap, direct when shorter", "......", true, false, 1, 3, -1, "++", 3},
        {"skips an item it can't focus", ".x...", false, false, 0, 3, -1, "++", 3},
        {"wraps past an item it can't focus", "x.....", true, false, 1, 5, -1, "-", 5},
        {"target it can't focus: stops past it", "..x..", false, false, 0, 2, -1, "++", 3},
        {"locked: no movement, one event", ".....", false, true, 0, 3, -1, "+", 0},
        {"bounces at the end: stops", "...x", false, false, 0, 3, -1, "+++", 2},
        {"no selection: nothing", ".....", false, false, -1, 3, -1, "", -1},
        {"target out of range: nothing", ".....", false, false, 0, 5, -1, "", 0},
        {"menu destroyed: stops", ".....", false, false, 0, 4, 2, "++", 2},
        {"menu destroyed going round: stops", "......", true, false, 1, 5, 1, "-", 0},
        {"a menu that wraps unexpectedly: stops", "x....", false, false, 2, 0, -1, "--", 4},
    };
    char what[128];
    for (const Case& c : cases) {
        MenuModel model;
        for (const char* p = c.items; *p; ++p)
            model.focusable.push_back(*p == '.');
        model.wraps = c.wraps || std::strstr(c.what, "unexpectedly") != nullptr;
        model.locked = c.locked;
        model.selected = c.from;
        model.destroyAfter = c.destroyAfter;
        int count = static_cast<int>(model.focusable.size());
        int sent = MouseMenus::StepSelection(c.to, count, c.wraps, ReadModel, SendModel, &model);
        std::string got;
        for (int d : model.events)
            got += d > 0 ? '+' : '-';
        std::snprintf(what, sizeof(what), "mouse stepping: %s", c.what);
        bool ok = got == c.events && sent == static_cast<int>(model.events.size()) && model.selected == c.end;
        Check(ok, what);
        if (!ok)
            Log("  events \"%s\" (sent %d), selected %d; expected \"%s\", %d", got.c_str(), sent, model.selected,
                c.events, c.end);
    }
}

// --- item shapes: theme menus, the spin menu ------------------------------------------------------------------------

constexpr uint32_t kSprite = 0xB12B510A;
constexpr float kDegrees = 3.14159265358979f / 180.0f;

// A point `along` and `across` an element turned by `degrees` from (x, y).
void Along(float x, float y, float degrees, float along, float across, float* outX, float* outY)
{
    double a = degrees * kDegrees;
    *outX = static_cast<float>(x + std::cos(a) * along - std::sin(a) * across);
    *outY = static_cast<float>(y + std::sin(a) * along + std::cos(a) * across);
}

void TestMouseItemShapes()
{
    static Tree tree;
    tree.used = 0;
    // Tags: `blank`; `active = 0`; `active = 1`; tag_not_focusable.
    static Script::CComponent blankFlag, activeOff, activeOn, notFocusable;
    static Script::CStruct blankTags, offTags, onTags, notFocusableTags;
    blankFlag = {};
    blankFlag.typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_NAME << 1);
    blankFlag.checksum = 0xC3D43B9A;
    activeOff = {};
    activeOff.typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_INTEGER << 1);
    activeOff.name = 0xB4E103FD;
    activeOff.integer = 0;
    activeOn = activeOff;
    activeOn.integer = 1;
    notFocusable = blankFlag;
    notFocusable.checksum = 0xF33A3321;
    blankTags = {};
    blankTags.m_head = &blankFlag;
    offTags = {};
    offTags.m_head = &activeOff;
    onTags = {};
    onTags.m_head = &activeOn;
    notFocusableTags = {};
    notFocusableTags.m_head = &notFocusable;

    CScreenElement* root = Make(tree, nullptr, kWindow, 0, 0, -1, -1, 640, 448);

    // T: a theme menu (theme_menu_add_item, as the network lobby's ACTIONS box) at (219,125), 222 wide: items are
    // 200 x 20 containers centred on (111, 10 + 20 i) - (230, 125 + 20 i)-(430, 145 + 20 i) - each with its text
    // centred at (85, -5): 100 x 20, (265, 110 + 20 i)-(365, 130 + 20 i), mostly above the container.
    CScreenElement* t = Menu(tree, root, true, true, 219, 125, 222, 60);
    CScreenElement* items[3];
    for (int i = 0; i < 3; ++i) {
        items[i] = Make(tree, t, kContainer, 111, 10.0f + 20 * i, 0, 0, 200, 20);
        Make(tree, items[i], kText, 85, -5, 0, 0, 100, 20);
    }
    // Item 1: an invisible sprite (alpha 0) right of the menu: (450,145)-(480,155) - still the item's.
    Make(tree, items[1], kSprite, 220, 0, -1, -1, 30, 10, 1.0f, 0.0f);
    // Item 2: an empty container far below (no area: not part of it); a hidden sprite (190..210) with a child.
    Make(tree, items[2], kContainer, 0, 100, -1, -1, 0, 0);
    CScreenElement* hiddenSprite = Make(tree, items[2], kSprite, 0, 25, -1, -1, 100, 20);
    At<uint32_t>(hiddenSprite, 0x08) = 0x400000;
    Make(tree, hiddenSprite, kSprite, 0, 0, -1, -1, 100, 20);

    // S: the main menu's spin menu: a 1 x 1 vmenu with its top left at (45.5, 213.5), turned -22.5 degrees (item 1
    // in front); 1 x 1 items at its top left turned 0, 22.5, ... 112.5 degrees; each with a 150 x 20 text from the
    // item's top left (item 1's faded out under the roundbar). Item 4 is disabled (active = 0), item 5 a blank.
    CScreenElement* spin = Menu(tree, root, true, true, 45.5f, 213.5f, 1, 1);
    Turn(spin, -22.5f * kDegrees);
    CScreenElement* spinItems[6];
    for (int i = 0; i < 6; ++i) {
        spinItems[i] = Make(tree, spin, kContainer, 0.5f, 0.5f, 0, 0, 1, 1);
        Turn(spinItems[i], 22.5f * i * kDegrees);
        Make(tree, spinItems[i], kText, 0, 0, -1, -1, 150, 20, 1.0f, i == 1 ? 0.0f : 1.0f);
    }
    At<Script::CStruct*>(spinItems[4], 0x20) = &offTags;
    At<Script::CStruct*>(spinItems[5], 0x20) = &blankTags;
    At<Script::CStruct*>(spinItems[3], 0x20) = &onTags;

    // R: a 300 x 100 vertical menu turned 0.5 rad at (300,300) with an item turned back (-0.5: upright),
    // (310,305)-(350,325) unturned: not widened to R's width (R is turned).
    CScreenElement* r = Menu(tree, root, true, true, 300, 300, 300, 100);
    Turn(r, 0.5f);
    CScreenElement* r0 = Make(tree, r, kText, 10, 5, -1, -1, 40, 20);
    Turn(r0, -0.5f);
    float r0x = At<float>(r0, 0x134), r0y = At<float>(r0, 0x138);

    struct Expect {
        float x, y;
        CScreenElement* menu;
        CScreenElement* item;
        int index;
    };
    float sx[8], sy[8];
    Along(45.5f, 213.5f, 0.0f, 120, 10, &sx[0], &sy[0]);    // along item 1 (in front)
    Along(45.5f, 213.5f, 22.5f, 120, 10, &sx[1], &sy[1]);   // item 2
    Along(45.5f, 213.5f, -22.5f, 120, 10, &sx[2], &sy[2]);  // item 0
    Along(45.5f, 213.5f, 45.0f, 140, 18, &sx[3], &sy[3]);   // item 3
    Along(45.5f, 213.5f, 67.5f, 120, 10, &sx[4], &sy[4]);   // item 4 (disabled)
    Along(45.5f, 213.5f, 90.0f, 120, 10, &sx[5], &sy[5]);   // item 5 (blank)
    Along(45.5f, 213.5f, 22.5f, 120, -3, &sx[6], &sy[6]);   // just outside item 2's text (and item 1's)
    Along(45.5f, 213.5f, 45.0f, 151, 10, &sx[7], &sy[7]);   // past the end of item 3's text
    const Expect expects[] = {
        {300, 112, t, items[0], 0}, // item 0's text, above its container
        {225, 112, t, items[0], 0}, // across the menu's width
        {300, 128, t, items[0], 0},
        {300, 132, t, items[1], 1}, // item 1's text over item 0's container (later wins)
        {300, 147, t, items[1], 1}, // the rest of item 1's text (below item 0's container)
        {300, 152, t, items[2], 2}, // item 2's text over item 1's container
        {300, 108, nullptr, nullptr, 0},
        {460, 150, t, items[1], 1}, // the invisible sprite
        {300, 183, t, items[2], 2}, // item 2's container's bottom
        {300, 200, nullptr, nullptr, 0}, // the hidden sprite and its child
        {300, 230, nullptr, nullptr, 0}, // the empty container
        {sx[0], sy[0], spin, spinItems[1], 1},
        {sx[1], sy[1], spin, spinItems[2], 2},
        {sx[2], sy[2], spin, spinItems[0], 0},
        {sx[3], sy[3], spin, spinItems[3], 3},
        {sx[4], sy[4], nullptr, nullptr, 0},
        {sx[5], sy[5], nullptr, nullptr, 0},
        {sx[6], sy[6], nullptr, nullptr, 0},
        {sx[7], sy[7], nullptr, nullptr, 0},
        {150, 250, nullptr, nullptr, 0}, // inside the turned texts' surrounding rectangles, outside the texts
        {r0x + 20, r0y + 10, r, r0, 0},
        {r0x + 100, r0y + 10, nullptr, nullptr, 0}, // across R's (unturned) width: not widened
    };
    char what[96];
    for (const Expect& x : expects) {
        MouseMenus::Hit hit = {nullptr, nullptr, -1};
        bool found = MouseMenus::FindItemAt(root, x.x, x.y, FocusedOnly, &hit);
        std::snprintf(what, sizeof(what), "mouse item shapes at (%g, %g)", x.x, x.y);
        bool ok = x.item ? found && hit.menu == x.menu && hit.item == x.item && hit.index == x.index : !found;
        Check(ok, what);
        if (!ok)
            Log("  found %d: menu %p item %p index %d", found, hit.menu, hit.item, hit.index);
    }

    // ItemBounds: theme item 1 (its text, container and invisible sprite), across the menu; spin item 2 (the
    // turned text's corners).
    MouseMenus::Rect b = MouseMenus::ItemBounds(items[1], t);
    Check(Near(b.left, 219) && Near(b.top, 130) && Near(b.right, 480) && Near(b.bottom, 165), "mouse ItemBounds theme");
    if (!(Near(b.left, 219) && Near(b.top, 130) && Near(b.right, 480) && Near(b.bottom, 165)))
        Log("  (%f, %f)-(%f, %f)", b.left, b.top, b.right, b.bottom);
    float cx[3], cy[3];
    Along(45.5f, 213.5f, 22.5f, 150, 0, &cx[0], &cy[0]);
    Along(45.5f, 213.5f, 22.5f, 0, 20, &cx[1], &cy[1]);
    Along(45.5f, 213.5f, 22.5f, 150, 20, &cx[2], &cy[2]);
    b = MouseMenus::ItemBounds(spinItems[2], spin);
    bool boundsOk = Near(b.left, cx[1]) && Near(b.top, 213.5f) && Near(b.right, cx[0]) && Near(b.bottom, cy[2]);
    Check(boundsOk, "mouse ItemBounds turned");
    if (!boundsOk)
        Log("  (%f, %f)-(%f, %f)", b.left, b.top, b.right, b.bottom);

    // The pieces.
    Check(MouseMenus::IsRotatingMenu(spin) && !MouseMenus::IsRotatingMenu(t) && MouseMenus::IsRotatingMenu(r),
          "mouse IsRotatingMenu");
    // A menu at angle 0 with one turned item (as the spin menu with item 0 in front); a whole turn isn't turned.
    CScreenElement* u = Menu(tree, root, true, true, 0, 0, 10, 10);
    CScreenElement* u0 = Make(tree, u, kText, 0, 0, -1, -1, 5, 5);
    Make(tree, u, kText, 0, 5, -1, -1, 5, 5);
    Check(!MouseMenus::IsRotatingMenu(u), "mouse IsRotatingMenu flat");
    Turn(u0, 6.2831855f);
    Check(!MouseMenus::IsRotatingMenu(u), "mouse IsRotatingMenu a whole turn");
    At<float>(u0, 0x14C) = 0.4f;
    Check(MouseMenus::IsRotatingMenu(u), "mouse IsRotatingMenu one item");
    Check(!MouseMenus::Rotated(0.0f) && !MouseMenus::Rotated(6.2831855f) && !MouseMenus::Rotated(-6.2831855f) &&
              !MouseMenus::Rotated(0.0005f) && MouseMenus::Rotated(0.01f) && MouseMenus::Rotated(-0.01f) &&
              MouseMenus::Rotated(3.14159f) && MouseMenus::Rotated(6.3f),
          "mouse Rotated");
    MouseMenus::Box flipped = {10, 10, -20, 5, 0};
    Check(MouseMenus::BoxContains(flipped, 0, 12) && MouseMenus::BoxContains(flipped, -10, 10) &&
              !MouseMenus::BoxContains(flipped, 11, 12) && !MouseMenus::BoxContains(flipped, 0, 16) &&
              !MouseMenus::BoxContains(flipped, 0, 9),
          "mouse BoxContains negative width");
    MouseMenus::Box upright = {0, 0, 10, 5, 1.5707964f}; // a quarter turn: along +y, across -x
    Check(MouseMenus::BoxContains(upright, -2, 5) && !MouseMenus::BoxContains(upright, 2, 5) &&
              !MouseMenus::BoxContains(upright, -2, 11) && !MouseMenus::BoxContains(upright, -6, 5) &&
              !MouseMenus::BoxContains(upright, -2, -1),
          "mouse BoxContains turned");
    MouseMenus::Box tall = {0, 0, 10, -5, 0};
    Check(MouseMenus::BoxContains(tall, 5, -2) && !MouseMenus::BoxContains(tall, 5, 2), "mouse BoxContains negative height");
    Check(MouseMenus::ItemPassedOver(spinItems[4]) && MouseMenus::ItemPassedOver(spinItems[5]) &&
              !MouseMenus::ItemPassedOver(spinItems[3]) && !MouseMenus::ItemPassedOver(spinItems[0]),
          "mouse ItemPassedOver");
    At<Script::CStruct*>(spinItems[0], 0x20) = &notFocusableTags;
    Check(!MouseMenus::ItemPassedOver(spinItems[0]), "mouse ItemPassedOver other tags");
    // A named tag holding the name `blank` (icon = blank) isn't the flag.
    static Script::CComponent namedBlank;
    static Script::CStruct namedTags;
    namedBlank = blankFlag;
    namedBlank.name = 0x9A6BD624; // icon
    namedTags = {};
    namedTags.m_head = &namedBlank;
    At<Script::CStruct*>(spinItems[0], 0x20) = &namedTags;
    Check(!MouseMenus::ItemPassedOver(spinItems[0]), "mouse ItemPassedOver a named tag");
    At<Script::CStruct*>(spinItems[0], 0x20) = nullptr;
}

// --- a selection on its way over several frames ---------------------------------------------------------------------

// A menu taking a frame at a time: after a move it ignores the events for `lockAfter` frames (the spin menu's
// spin_menu_input_timer); with `drift` it moves one way by itself each frame (a blank item's focus script).
struct FrameModel {
    MenuModel menu;
    int lockAfter = 0;
    int lock = 0;
    int drift = 0;

    void Tick()
    {
        if (lock > 0)
            --lock;
        if (drift)
            menu.Move(drift);
    }
};

int ReadFrameModel(void* context) { return ReadModel(&static_cast<FrameModel*>(context)->menu); }

void SendFrameModel(void* context, int direction)
{
    auto* m = static_cast<FrameModel*>(context);
    m->menu.events.push_back(direction);
    if (m->menu.locked || m->lock > 0)
        return;
    m->menu.Move(direction);
    m->lock = m->lockAfter;
}

void TestMouseTargets()
{
    struct Case {
        const char* what;
        int count;
        bool wraps;
        int lockAfter;   // frames ignored after each move
        bool locked;     // never moves
        int drift;
        bool wait;       // AdvanceTarget told to send nothing
        int from, to;
        int destroyAfter;
        MouseMenus::TargetState end;
        int frames;      // the frame it ended in (1 = the first call)
        int events;
        int selected;
    };
    using S = MouseMenus::TargetState;
    const Case cases[] = {
        {"already there", 5, false, 0, false, 0, false, 2, 2, -1, S::Reached, 1, 0, 2},
        {"a menu that moves at once", 5, false, 0, false, 0, false, 0, 3, -1, S::Reached, 1, 3, 3},
        // (a move frame sends two events: the move, then one ignored - StepSelection stops there)
        {"the spin menu: a move every 9 frames", 16, true, 9, false, 0, false, 0, 3, -1, S::Reached, 19, 21, 3},
        {"the spin menu the short way round", 16, true, 9, false, 0, false, 1, 14, -1, S::Reached, 19, 21, 14},
        {"stuck: gives up", 5, false, 0, true, 0, false, 0, 3, -1, S::GaveUp, 1 + MouseMenus::kMaxStuckFrames + 1,
         1 + MouseMenus::kMaxStuckFrames + 1, 0},
        {"out of frames", 40, false, 9, false, 0, false, 0, 39, -1, S::GaveUp, MouseMenus::kTargetFrames + 1,
         MouseMenus::kTargetFrames + 34, 34},
        {"waiting: the menu moves by itself", 8, false, 0, false, 1, true, 0, 3, -1, S::Reached, 4, 0, 3},
        {"waiting and stuck", 8, false, 0, false, 0, true, 0, 3, -1, S::GaveUp, 1 + MouseMenus::kMaxStuckFrames + 1, 0,
         0},
        {"menu destroyed", 8, false, 9, false, 0, false, 0, 5, 1, S::GaveUp, 1, 1, 1},
        {"no selection", 8, false, 0, false, 0, false, -1, 5, -1, S::GaveUp, 1, 0, -1},
    };
    char what[128];
    for (const Case& c : cases) {
        FrameModel model;
        model.menu.focusable.assign(c.count, true);
        model.menu.wraps = c.wraps;
        model.menu.locked = c.locked;
        model.menu.selected = c.from;
        model.menu.destroyAfter = c.destroyAfter;
        model.lockAfter = c.lockAfter;
        model.drift = c.drift;
        MouseMenus::Target target = MouseMenus::StartTarget(c.to, c.count, c.wraps);
        S state = S::Moving;
        int frame = 0;
        while (state == S::Moving && frame < 1000) {
            ++frame;
            if (frame > 1)
                model.Tick();
            state = MouseMenus::AdvanceTarget(target, c.wait, ReadFrameModel, SendFrameModel, &model);
        }
        int events = static_cast<int>(model.menu.events.size());
        bool ok = state == c.end && frame == c.frames && events == c.events && model.menu.selected == c.selected;
        std::snprintf(what, sizeof(what), "mouse targets: %s", c.what);
        Check(ok, what);
        if (!ok)
            Log("  state %d after %d frames, %d events, selected %d; expected %d, %d, %d, %d", static_cast<int>(state),
                frame, events, model.menu.selected, static_cast<int>(c.end), c.frames, c.events, c.selected);
    }
    // Already there / no selection: decided before a frame is counted.
    for (int from : {2, -1}) {
        MenuModel m;
        m.focusable.assign(5, true);
        m.selected = from;
        MouseMenus::Target there = MouseMenus::StartTarget(2, 5, false);
        MouseMenus::AdvanceTarget(there, false, ReadModel, SendModel, &m);
        Check(there.framesLeft == MouseMenus::kTargetFrames && there.lastSeen == -2 && m.events.empty(),
              from < 0 ? "mouse targets: no selection, no frame counted" : "mouse targets: there, no frame counted");
    }
    MouseMenus::Target t = MouseMenus::StartTarget(4, 9, true);
    Check(t.index == 4 && t.count == 9 && t.wraps && t.framesLeft == MouseMenus::kTargetFrames && t.stuckFrames == 0 &&
              t.lastSeen == -2,
          "mouse StartTarget");
}

} // namespace

SELFTEST(Early, TestMouseMapping);
SELFTEST(Early, TestMouseHitTest);
SELFTEST(Early, TestMouseStepping);
SELFTEST(Early, TestMouseItemShapes);
SELFTEST(Early, TestMouseTargets);
