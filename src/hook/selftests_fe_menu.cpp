// Self-tests for the menus and the container element (src/game/front/menu_element.cpp), on the shared element
// world (fe_element_fixture.h): menus whose items are stand-in elements with tags; the object's tags and type, its
// script's event, the event's controller and handled mark, the manager's pad-event priorities are recording
// stand-ins too.

#include "game/front/menu_element.h"
#include "game/front/screen_element_manager.h"
#include "hook/fe_element_fixture.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;
using Front::CBaseMenu;
using Front::CContainerElement;

bool g_passResult;
bool g_priorityEvent;
uint32_t g_topPriority;

void InstallMenuStandIns(StandIns& s)
{
    InstallElementStandIns(s);
    // tags: 'G' integer tags, 'N' tag flags, by (element, name)
    s.At({0x004616B0, AddressOf(&Obj::CObject::SetIntegerTag)}, 8, [](Regs& r) {
        Rec(0x16B0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004607C0, AddressOf(&Obj::CObject::GetIntegerTag)}, 8, [](Regs& r) {
        Rec(0x07C0, {r.ecx, r.args[0]});
        const std::vector<uint32_t>* value = Lookup('G', r.ecx, r.args[0]);
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = (*value)[0];
        return 1u;
    });
    s.At({0x00460800, AddressOf(&Obj::CObject::ContainsTagFlag)}, 4, [](Regs& r) {
        Rec(0x0800, {r.ecx, r.args[0]});
        return Lookup('N', r.ecx, r.args[0]) ? 1u : 0u;
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
    s.At({0x0045F9E0, AddressOf(&Obj::EventController)}, 0, [](Regs& r) {
        Rec(0xF9E0, {r.args[0]});
        return 3u;
    });
    s.At({0x0045F9B0, AddressOf(&Obj::CEvent::MarkHandled)}, 8, [](Regs& r) {
        Rec(0xF9B0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004AAD50, AddressOf(&Front::CScreenElementManager::IsPadEvent)}, 4, [](Regs& r) {
        Rec(0xAD50, {r.ecx, r.args[0]});
        return g_priorityEvent ? 1u : 0u;
    });
    s.At({0x004AB7C0, AddressOf(&Front::CScreenElementManager::IsTopPriority)}, 4, [](Regs& r) {
        Rec(0xB7C0, {r.ecx, r.args[0]});
        return r.args[0] == g_topPriority ? 1u : 0u;
    });
}

CBaseMenu* M(int i) { return reinterpret_cast<CBaseMenu*>(E(i)); }

// Menu 1 (vertical) with items 2, 3, 4 (3 has children), menu 8 (horizontal) with item 9; menu fields set.
void MenuWorld(bool original)
{
    BeginWorld(original);
    g_passResult = true;
    g_priorityEvent = false;
    g_topPriority = 0;
    for (int i : {1, 8}) {
        CBaseMenu* m = M(i);
        m->m_selectedIndex = -1;
        m->m_selectedId = 0;
        m->m_focused = false;
        m->m_controller = 2;
        m->m_internalJustX = -0.5f;
        m->m_internalJustY = 0.25f;
        m->m_vertical = i == 1;
        m->m_regularSpace = 0.0f;
        m->m_paddingScale = i == 1 ? 1.25f : 1.1f; // (1.1: half lengths that don't fit a float)
        m->m_spacingBetween = 6.0f;
        m->m_padHandling = true;
        m->m_allowWrap = true;
        m->m_gridIndex = -1;
    }
    auto* event = Ptr<Obj::CEvent>(kEvent);
    event->m_type = 0;
    event->m_source = 0x36B2EE74;
    event->m_target = 0x1001;
    event->m_flags = 0;
    event->mp_data = nullptr;
}

void TestMenus()
{
    SaveElementGlobals();
    {
        StandIns s;
        InstallMenuStandIns(s);

        RunBoth("menus: construction, properties", [&](bool original) {
            MenuWorld(original);
            for (int k = 0; k < 3; ++k) {
                std::memset(E(12 + k), 0xCD, 0x1A8);
                uint32_t r = k == 0 ? reinterpret_cast<uint32_t>(Call(original, 0x004A74E0, M(12), &CBaseMenu::Construct))
                             : k == 1 ? reinterpret_cast<uint32_t>(Call(original, 0x004A75C0, M(13), &CBaseMenu::ConstructVMenu))
                                      : reinterpret_cast<uint32_t>(Call(original, 0x004A7630, M(14), &CBaseMenu::ConstructHMenu));
                Rec(0x200, {r});
            }
            Mark(1);
            // SetProperties: everything on, the opposites, nothing
            uint32_t p = At(kParams);
            Answer('S', p, 0x67E093E4, {MakeArray(0, Script::ESYMBOLTYPE_FLOAT, {Bits(0.5f), Bits(-1.0f)})});
            Answer('I', p, 0xF24ADEDB, {7});
            Answer('F', p, 0x6D853B88, {Bits(0.8f)});
            Answer('I', p, 0x4B4E14AB, {12});
            Answer('L', p, 0x4BD08BFE, {1});
            Answer('L', p, 0x43A7692D, {1});
            p = At(kParams + 0x40);
            Answer('L', p, 0xC22541CC, {1});
            Answer('L', p, 0x5DEA5C64, {1});
            for (int k = 0; k < 3; ++k) {
                M(13)->m_padHandling = k != 1;
                M(13)->m_allowWrap = k != 1;
                Call(original, 0x004A76F0, M(13), &CBaseMenu::SetProperties, Params(k));
                Mark(2u + k);
            }
            float* out = Ptr<float>(kOut);
            for (float space : {0.0f, 0.0001f, 0.00011f, 5.0f}) {
                M(1)->m_regularSpace = space;
                Rec(0x201, {static_cast<uint32_t>(CallC(original, 0x004A7550, M(1), &CBaseMenu::GetRegularSpace, out)),
                            Bits(*out)});
            }
            M(1)->m_selectedId = 0x1003;
            M(1)->m_selectedIndex = 1;
            M(1)->m_gridIndex = 4;
            Call(original, 0x004A7580, M(1), &CBaseMenu::SetSelectionTags);
            // the destructors
            Call(original, 0x004A7620, M(12), &CBaseMenu::DestroyBody);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)})
                Rec(0x202, {reinterpret_cast<uint32_t>(Call(original, 0x004A76C0, M(flags ? 14 : 13), &CBaseMenu::Destroy, flags))});
            Mark(5);
        });

        RunBoth("menus: moving the selection", [&](bool original) {
            MenuWorld(original);
            Answer('G', At(ElemAt(2)), 0x5B92F8DD, {2});
            Answer('G', At(ElemAt(4)), 0x5B92F8DD, {5});
            Answer('N', At(ElemAt(3)), 0xF33A3321, {1}); // item 3 not focusable
            CBaseMenu* m = M(1);
            struct Move {
                int start, direction;
                bool stay, gridX, wrap;
            };
            const Move moves[] = {{-1, 1, true, true, true},  {0, 1, false, true, true},  {2, 1, false, false, true},
                                  {2, 1, false, true, false}, {0, -1, false, true, true}, {0, -1, false, true, false},
                                  {1, 1, true, false, true},  {7, 1, false, true, true},  {-5, -1, false, true, false}};
            uint32_t mark = 10;
            for (const Move& mv : moves) {
                m->m_selectedIndex = mv.start;
                m->m_allowWrap = mv.wrap;
                Call(original, 0x004A7850, m, &CBaseMenu::MoveSelection, mv.direction, mv.stay, mv.gridX);
                Rec(0x210, {static_cast<uint32_t>(m->m_selectedIndex), m->m_selectedId, static_cast<uint32_t>(m->m_gridIndex)});
                Mark(mark++);
            }
            // every item unfocusable (2 by block_events on the item, 4 by a blocking parent... the menu)
            E(2)->m_objectFlags |= CScreenElement::kBlockEvents;
            Answer('N', At(ElemAt(4)), 0xF33A3321, {1});
            m->m_selectedIndex = 0;
            Call(original, 0x004A7850, m, &CBaseMenu::MoveSelection, 1, false, true);
            Rec(0x211, {static_cast<uint32_t>(m->m_selectedIndex), m->m_selectedId});
            // an empty menu
            M(7)->m_selectedIndex = 3;
            Call(original, 0x004A7850, M(7), &CBaseMenu::MoveSelection, 1, false, true);
            Rec(0x212, {static_cast<uint32_t>(M(7)->m_selectedIndex), M(7)->m_selectedId});
            Mark(30);
            // Step: from a selected item (its grid index tag), from an id not found, from nothing
            g_answers.clear();
            Answer('G', At(ElemAt(2)), 0x8321DD71, {9});
            E(2)->m_objectFlags = 0;
            for (uint32_t id : {0x1002u, 0x7777u, 0u}) {
                m->m_selectedId = id;
                m->m_selectedIndex = 0;
                Call(original, 0x004A7A40, m, &CBaseMenu::Step, id == 0x7777 ? -1 : 1);
                Mark(31u + (id & 3));
            }
        });

        RunBoth("menus: arranging", [&](bool original) {
            MenuWorld(original);
            uint32_t mark = 40;
            for (int menu : {1, 8})
                for (float space : {0.0f, 9.0f})
                    for (bool locked : {false, true}) {
                        CBaseMenu* m = M(menu);
                        m->m_regularSpace = space;
                        m->m_objectFlags = locked ? CScreenElement::kDimsLocked : 0;
                        m->m_width = 30.0f;
                        m->m_height = 400.0f;
                        m->m_selectedId = 0;
                        m->m_focused = locked;
                        E(3)->m_objectFlags = space == 0.0f ? CScreenElement::kDirty : 0;
                        Call(original, 0x004A7B80, m, &CBaseMenu::Arrange);
                        Mark(mark++);
                    }
        });

        RunBoth("menus: events", [&](bool original) {
            MenuWorld(original);
            auto* event = Ptr<Obj::CEvent>(kEvent);
            CBaseMenu* m = M(1);
            uint32_t mark = 60;
            auto send = [&](uint32_t type, CBaseMenu* menu) {
                event->m_type = type;
                Rec(0x220, {static_cast<uint32_t>(Call(original, 0x004A8100, menu, &CBaseMenu::PassEventToScript, event)) & 0xFF});
                Mark(mark++);
            };
            // priorities: not a priority element / not the top one / the top one; the script's event says gone
            g_usesPriorities = true;
            g_priorityEvent = true;
            send(0x30A4F836, m);
            m->m_objectFlags |= CScreenElement::kPriority;
            send(0x30A4F836, m);
            g_topPriority = 0x1001;
            g_passResult = false;
            send(0x30A4F836, m);
            g_passResult = true;
            g_usesPriorities = false;
            // the focus-sent event: selection by id found, id gone (by index found / not), nothing selected; focused
            m->m_selectedId = 0x1004;
            send(0x3056434F, m);
            m->m_selectedId = 0x7777;
            m->m_selectedIndex = 1;
            m->m_focused = true;
            send(0x3056434F, m);
            m->m_selectedId = 0x7777;
            m->m_selectedIndex = 9;
            send(0x3056434F, m);
            m->m_selectedId = 0;
            send(0x3056434F, m);
            m->m_focused = false; // (refocusing needs the focus)
            m->m_selectedId = 0;
            send(0x3056434F, m);
            // focus: with grid_index, with child_id, plain; already focused
            m->m_focused = false;
            uint32_t data = At(kParams + 0x40 * 5);
            event->mp_data = Ptr<CStruct>(kParams + 0x40 * 5);
            Answer('I', data, 0xFACF9A8B, {4});
            Answer('G', At(ElemAt(2)), 0x5B92F8DD, {1});
            Answer('G', At(ElemAt(3)), 0x5B92F8DD, {4});
            Answer('G', At(ElemAt(4)), 0x5B92F8DD, {6});
            send(0x9D3FB516, m);
            m->m_focused = false;
            g_answers.erase({'I', Rel(data), 0xFACF9A8B});
            Answer('K', data, 0x229D3DE4, {0x1004});
            send(0x9D3FB516, m);
            send(0x9D3FB516, m);
            m->m_focused = false;
            m->m_selectedId = 0;
            event->mp_data = nullptr;
            send(0x9D3FB516, m);
            // unfocus: focused, not
            m->m_selectedId = 0x1002;
            send(0x4ADF0CD3, m);
            send(0x4ADF0CD3, m);
            // the pad: each direction, vertical and horizontal, focused or not, handling off
            for (int menu : {1, 8})
                for (int state = 0; state < 3; ++state) {
                    CBaseMenu* mm = M(menu);
                    mm->m_focused = state != 1;
                    mm->m_padHandling = state != 2;
                    mm->m_selectedId = menu == 1 ? 0x1002 : 0x1009;
                    for (uint32_t pad : {0x30A4F836u, 0x0FD1AC26u, 0x6949DB75u, 0x05DDD87Cu})
                        send(pad, mm);
                }
        });

        RunBoth("container element", [&](bool original) {
            MenuWorld(original);
            auto* c = reinterpret_cast<CContainerElement*>(E(12));
            std::memset(c, 0xCD, 0x17C);
            Rec(0x230, {reinterpret_cast<uint32_t>(Call(original, 0x004AAC20, c, &CContainerElement::Construct))});
            Mark(80);
            auto* c3 = reinterpret_cast<CContainerElement*>(E(3));
            Answer('K', At(kParams), 0xF1822B9C, {0x1005});
            Call(original, 0x004AA530, c3, &CContainerElement::SetProperties, Params(0));
            Call(original, 0x004AA530, c3, &CContainerElement::SetProperties, Params(1));
            Mark(81);
            auto* event = Ptr<Obj::CEvent>(kEvent);
            uint32_t mark = 82;
            for (int k = 0; k < 8; ++k) {
                event->m_type = k == 2 ? 0x30A4F836u : (k & 1) ? 0x4ADF0CD3u : 0x9D3FB516u;
                c3->m_focusableChild = k == 3 ? 0 : 0x1005;
                g_usesPriorities = k >= 4 && k < 7;
                g_priorityEvent = k >= 4 && k < 7;
                g_topPriority = k == 6 ? 0x1003 : 0;
                c3->m_objectFlags = k == 5 ? 0 : CScreenElement::kPriority;
                g_passResult = k != 1;
                Rec(0x231, {static_cast<uint32_t>(Call(original, 0x004AA560, c3, &CContainerElement::PassEventToScript, event)) & 0xFF});
                Mark(mark++);
            }
            Call(original, 0x004AAC80, c, &CContainerElement::DestroyBody);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)})
                Rec(0x232, {reinterpret_cast<uint32_t>(Call(original, 0x004AACC0, c, &CContainerElement::Destroy, flags))});
            Mark(95);
        });

        RunBoth("smart pointer constructors", [&](bool original) {
            MenuWorld(original);
            auto* p = Ptr<ElementPtr>(kOut);
            for (int i : {2, -1}) {
                std::memset(p, 0xCD, sizeof(*p));
                CScreenElement* e = i < 0 ? nullptr : E(i);
                Rec(0x240, {reinterpret_cast<uint32_t>(
                               original ? Original<ElementPtr*(__thiscall*)(ElementPtr*, CScreenElement*)>(0x004A7820)(p, e)
                                        : p->ConstructFrom(e))});
                Mark(100u + (i < 0));
                p->Release();
                std::memset(p + 1, 0xCD, sizeof(*p));
                Rec(0x241, {reinterpret_cast<uint32_t>(
                               original ? Original<ElementPtr*(__thiscall*)(ElementPtr*, const ElementPtr*)>(0x004A7690)(p + 1, &E(1)->mp_firstChild)
                                        : (p + 1)->ConstructCopy(&E(1)->mp_firstChild))});
                Mark(102u + (i < 0));
                (p + 1)->Release();
            }
            // assignment: from another pointer, from a null one, to itself (null afterwards)
            ElementPtr* q = p + 2;
            q->mp_object = nullptr;
            q->mp_prev = q->mp_next = nullptr;
            q->Set(E(4));
            const ElementPtr* sources[3] = {&E(3)->mp_firstChild, q, &E(7)->mp_firstChild};
            for (int k = 0; k < 3; ++k) {
                Rec(0x242, {reinterpret_cast<uint32_t>(
                               original ? Original<ElementPtr*(__thiscall*)(ElementPtr*, const ElementPtr*)>(0x004A8F60)(q, sources[k])
                                        : q->Assign(sources[k]))});
                Mark(104u + k);
            }
            q->Release();
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestMenus, FeTest::Cleanup);
