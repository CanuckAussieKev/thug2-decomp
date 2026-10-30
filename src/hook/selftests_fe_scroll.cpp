// Self-tests for the scrolling menus (src/game/front/scrolling_menu.cpp), on the shared element world
// (fe_element_fixture.h): a scrolling menu holding a vmenu (and an hmenu) of stand-in items; the event listener's
// tracker list, the object's type and script event, the events' marks and the manager's pad-event priorities are
// recording stand-ins.

#include "game/front/menu_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/scrolling_menu.h"
#include "hook/fe_element_fixture.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;
using Front::CBaseMenu;
using Front::CScreenElementManager;
using Front::CScrollingMenu;

bool g_passResult;
bool g_priorityEvent;
uint32_t g_topPriority;

void InstallScrollStandIns(StandIns& s)
{
    InstallElementStandIns(s);
    s.At({0x0045FB30, AddressOf(&Obj::CEventListener::RegisterWithTracker)}, 4, [](Regs& r) {
        Rec(0xFB30, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045FAC0, AddressOf(&Obj::CEventListener::Destroy)}, 0, [](Regs& r) {
        Rec(0xFAC0, {r.ecx});
        return 0u;
    });
    s.At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Rec(0x1970, {r.ecx, r.args[0]});
        *reinterpret_cast<uint32_t*>(r.ecx + 0x18) = r.args[0];
        return 0u;
    });
    s.At({0x00461140, AddressOf(&Obj::CObject::PassEventToScript)}, 4, [](Regs& r) {
        Rec(0x1140, {r.ecx, r.args[0]});
        return g_passResult ? 1u : 0u;
    });
    s.At({0x0045F9B0, AddressOf(&Obj::CEvent::MarkHandled)}, 8, [](Regs& r) {
        Rec(0xF9B0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0045F980, AddressOf(&Obj::CEvent::MarkRead)}, 8, [](Regs& r) {
        Rec(0xF980, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004AAD50, AddressOf(&CScreenElementManager::IsPadEvent)}, 4, [](Regs& r) {
        Rec(0xAD50, {r.ecx, r.args[0]});
        return g_priorityEvent ? 1u : 0u;
    });
    s.At({0x004AB7C0, AddressOf(&CScreenElementManager::IsTopPriority)}, 4, [](Regs& r) {
        Rec(0xB7C0, {r.ecx, r.args[0]});
        return r.args[0] == g_topPriority ? 1u : 0u;
    });
    s.At({AddressOf(&CScreenElementManager::HasPriorities)}, 0, [](Regs& r) {
        Rec(0xAD80, {r.ecx});
        return g_usesPriorities ? 1u : 0u;
    });
}

CScrollingMenu* Scroll() { return Ptr<CScrollingMenu>(ElemAt(10)); }

// 10: the scrolling menu (vertical) <- 11: a vmenu <- 12, 13, 14, 15; 9 (in 8): an hmenu.
void ScrollWorld(bool original)
{
    BeginWorld(original);
    g_passResult = true;
    g_priorityEvent = false;
    g_topPriority = 0;
    for (int i = 10; i < 16; ++i)
        Fill(i, i - 8);
    E(11)->m_type = Crc::GenerateCRCFromString("vmenu");
    E(9)->m_type = Crc::GenerateCRCFromString("hmenu");
    Link(10, 11);
    for (int i = 12; i < 16; ++i)
        Link(11, i);
    auto* menu = reinterpret_cast<CBaseMenu*>(E(11));
    menu->m_regularSpace = 0.0f;
    auto* s = Scroll();
    std::memset(reinterpret_cast<uint8_t*>(s) + 0x178, 0, 0x48);
    s->m_listener.m_vtable = reinterpret_cast<void**>(0x0064A180);
    s->m_scrollSpeed = 12.0f;
    s->m_numItemsToShow = 2;
    s->m_regularSpace = -1.0f;
    s->m_vertical = true;
    s->m_windowLength = 60.0f;
    auto* event = Ptr<Obj::CEvent>(kEvent);
    std::memset(event, 0, sizeof(*event));
}

void TestScrollingMenus()
{
    SaveElementGlobals();
    {
        StandIns s;
        InstallScrollStandIns(s);

        RunBoth("scrolling menus: construction", [&](bool original) {
            ScrollWorld(original);
            for (int k = 0; k < 3; ++k) {
                auto* m = Ptr<CScrollingMenu>(ElemAt(12 + k));
                std::memset(m, 0xCD, sizeof(*m));
                uint32_t r = k == 0   ? reinterpret_cast<uint32_t>(Call(original, 0x004AE050, m, &CScrollingMenu::Construct))
                             : k == 1 ? reinterpret_cast<uint32_t>(Call(original, 0x004AE170, m, &CScrollingMenu::ConstructVertical))
                                      : reinterpret_cast<uint32_t>(Call(original, 0x004AE1C0, m, &CScrollingMenu::ConstructHorizontal));
                Rec(0x400, {r});
            }
            Mark(1);
            Call(original, 0x004AE110, Ptr<CScrollingMenu>(ElemAt(12)), &CScrollingMenu::DestroyBody);
            Call(original, 0x004AE1A0, Ptr<CScrollingMenu>(ElemAt(13)), &CScrollingMenu::DestroyBodyVertical);
            Call(original, 0x004AE1F0, Ptr<CScrollingMenu>(ElemAt(14)), &CScrollingMenu::DestroyBodyHorizontal);
            Mark(2);
            const uint32_t destroys[3] = {0x004AE240, 0x004AE270, 0x004AE2A0};
            CScrollingMenu* (CScrollingMenu::*ours[3])(uint8_t) = {&CScrollingMenu::Destroy, &CScrollingMenu::DestroyVertical,
                                                                   &CScrollingMenu::DestroyHorizontal};
            const uint32_t fromListener[3] = {0x004AE210, 0x004AE220, 0x004AE230};
            for (int k = 0; k < 3; ++k) {
                auto* m = Ptr<CScrollingMenu>(ElemAt(12 + k));
                Rec(0x401, {reinterpret_cast<uint32_t>(Call(original, destroys[k], m, ours[k], static_cast<uint8_t>(k & 1)))});
                void* listener = &m->m_listener;
                Rec(0x402, {reinterpret_cast<uint32_t>(
                               original ? Original<void*(__thiscall*)(void*, uint8_t)>(fromListener[k])(listener, 1)
                                        : reinterpret_cast<void*(__thiscall*)(void*, uint8_t)>(AddressOf(ours[k]))(m, 1))});
            }
            Mark(3);
        });

        RunBoth("scrolling menus: the menu, properties, places", [&](bool original) {
            ScrollWorld(original);
            CScrollingMenu* m = Scroll();
            // FindMenu: vertical with spacing (items shown), without; horizontal (an hmenu child)
            auto* menu = reinterpret_cast<CBaseMenu*>(E(11));
            // (4: horizontal with a vmenu after the hmenu - only the hmenu counts; 5: 0 items to show)
            for (int k = 0; k < 6; ++k) {
                menu->m_regularSpace = k == 0 ? 0.0f : 15.0f;
                m->m_numItemsToShow = k == 2 ? -1 : k == 5 ? 0 : 3;
                m->m_vertical = k != 3 && k != 4;
                if (k == 3)
                    Link(10, 9);
                if (k == 4) {
                    E(7)->m_type = E(11)->m_type;
                    Link(10, 7);
                }
                if (k == 5)
                    E(7)->m_type = 0x77000007;
                m->mp_menu = nullptr;
                Call(original, 0x004AE2D0, m, &CScrollingMenu::FindMenu);
                Mark(10u + k);
            }
            m->m_vertical = true;
            m->mp_menu = E(11);
            // ItemPlace
            float* out = Ptr<float>(kOut);
            ElementPtr* item = Ptr<ElementPtr>(kOut + 0x20);
            item->mp_object = nullptr;
            item->Set(E(13));
            Call(original, 0x004AE450, m, &CScrollingMenu::ItemPlace, item, out, out + 1, out + 2, out + 3);
            item->Release();
            Mark(14);
            // SetProperties: everything; reset_window_bottom; the rearrange flag; nothing
            uint32_t p = At(kParams);
            Answer('S', p, 0x67E093E4, {MakeArray(0, Script::ESYMBOLTYPE_FLOAT, {Bits(0.25f), Bits(-0.5f)})});
            Answer('I', p, 0x9020A1D1, {33});
            Answer('I', p, 0xC855511C, {4});
            Answer('L', p, 0xED889E1F, {1});
            Answer('L', At(kParams + 0x40), 0xC3A894F6, {1});
            Answer('L', At(kParams + 0x80), 0xA1F3B8BB, {1});
            for (int k = 0; k < 4; ++k) {
                m->m_rearrange = false;
                m->m_windowStart = 7.0f;
                Call(original, 0x004AE520, m, &CScrollingMenu::SetProperties, Params(k));
                Mark(20u + k);
            }
        });

        RunBoth("scrolling menus: updates", [&](bool original) {
            ScrollWorld(original);
            CScrollingMenu* m = Scroll();
            m->mp_menu = E(11);
            auto* menu = reinterpret_cast<CBaseMenu*>(E(11));
            // the window following the selection: by sizes (vertical, horizontal), by regular spacing (each
            // margin), back and forth, items hidden / shown; no selection; an id not in the menu; no menu
            struct Case {
                uint32_t selected;
                float start, space;
                bool vertical, rearrange;
                int shown;
            };
            const Case cases[] = {
                {0x100E, 0.0f, -1.0f, true, false, 2},   {0x100F, 0.0f, -1.0f, true, false, 2},
                {0x100C, 200.0f, -1.0f, true, false, 2}, {0x100C, 205.0f, -1.0f, true, false, 2},
                {0x100D, 0.0f, 20.0f, true, false, 3},   {0x100F, 0.0f, 20.0f, true, false, 3},
                {0x100C, 30.0f, 20.0f, true, true, 4},   {0x100E, 0.0f, -1.0f, false, false, 2},
                {0, 0.0f, -1.0f, true, true, 2},         {0x7777, 0.0f, -1.0f, true, true, 2},
                {0x100F, -50.0f, 20.0f, true, false, 1},
                // 4 shown: the selection at half (the margin before it counts, the one after not), forward just
                // past the end (clamped), the margin after added when clamping
                {0x100E, 25.0f, 20.0f, true, false, 4},  {0x100E, 0.0f, 20.0f, true, false, 4},
                {0x100E, -31.5f, 20.0f, true, false, 4}, {0x100D, -10.0f, 20.0f, true, false, 4},
            };
            uint32_t mark = 30;
            for (const Case& c : cases) {
                m->m_selectedId = c.selected;
                m->m_windowStart = c.start;
                m->m_regularSpace = c.space;
                menu->m_regularSpace = c.space < 0 ? 0.0f : c.space;
                m->m_vertical = c.vertical;
                m->m_rearrange = c.rearrange;
                m->m_numItemsToShow = c.shown;
                m->m_windowLength = c.space < 0 ? 60.0f : c.space * c.shown;
                Call(original, 0x004AE960, m, &CScrollingMenu::Update);
                Mark(mark++);
            }
            m->mp_menu = nullptr;
            Call(original, 0x004AE960, m, &CScrollingMenu::Update);
            Mark(mark);
        });

        RunBoth("scrolling menus: events", [&](bool original) {
            ScrollWorld(original);
            CScrollingMenu* m = Scroll();
            auto* event = Ptr<Obj::CEvent>(kEvent);
            uint32_t mark = 50;
            auto pass = [&](uint32_t type, uint32_t target) {
                event->m_type = type;
                event->m_target = target;
                Rec(0x410, {static_cast<uint32_t>(Call(original, 0x004AE6A0, m, &CScrollingMenu::PassEventToScript, event)) & 0xFF});
                Mark(mark++);
            };
            auto listen = [&](uint32_t type, uint32_t target) {
                event->m_type = type;
                event->m_target = target;
                void* listener = &m->m_listener;
                if (original)
                    Original<void(__thiscall*)(void*, Obj::CEvent*)>(0x004AE7E0)(listener, event);
                else
                    reinterpret_cast<CScrollingMenu*>(listener)->ListenerReceive(event);
                Mark(mark++);
            };
            // priorities refuse; the script's event refuses
            g_usesPriorities = true;
            g_priorityEvent = true;
            pass(0x30A4F836, 0);
            listen(0x30A4F836, 0);
            g_usesPriorities = false;
            g_passResult = false;
            pass(0x9D3FB516, 0);
            g_passResult = true;
            // focus (no menu yet: found), again (already focused), unfocus, unfocus again, the focus flag's event
            pass(0x9D3FB516, 0);
            pass(0x9D3FB516, 0);
            pass(0x4ADF0CD3, 0);
            pass(0x4ADF0CD3, 0);
            pass(0x3056434F, 0);
            // the listener: the menu's focus flag event (the menu's / another's id), focus on an item / not
            listen(0x3056434F, 0x100B);
            listen(0x3056434F, 0x1002);
            listen(0x9D3FB516, 0x100D);
            listen(0x9D3FB516, 0x1002);
            m->mp_menu = nullptr;
            listen(0x9D3FB516, 0x100E);
            m->mp_menu = nullptr;
            listen(0x3056434F, 0x100B);
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestScrollingMenus, FeTest::Cleanup);
