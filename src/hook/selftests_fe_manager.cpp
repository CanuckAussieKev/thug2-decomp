// Self-tests for the screen element manager (src/game/front/screen_element_manager.cpp), on the shared element
// world (fe_element_fixture.h) with a real manager in the arena: the elements are in its object list, the tracker's
// lookups, the base manager's unregistering, the event listener's tracker list, the script array of pad event
// types, the input device, the events' read / handled marks, the scripts and the text element are recording
// stand-ins.

#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/sio/inpman.h"
#include "hook/fe_element_fixture.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;
using Front::CScreenElementManager;

constexpr uint32_t kMgr = 0x5000;        // the manager (0x6A4 bytes)
constexpr uint32_t kPadTypes = 0x5800;   // the stand-in pad event type array
constexpr uint32_t kMgrEnd = 0x5900;

bool g_launchHandled;      // what the tracker's LaunchEvent returns
uint32_t g_launchHandledBy; // ... for this target only (0: any)
int g_device;
void* g_managerVtable[8];

CScreenElementManager* Mgr() { return Ptr<CScreenElementManager>(kMgr); }
void MarkAll(uint32_t n) { Snapshot(0, kMgrEnd, n); }

void InstallManagerStandIns(StandIns& s)
{
    InstallElementStandIns(s, false);
    g_vtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0xE000, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00462120, AddressOf(&Obj::CTracker::LookUpObject)}, 4, [](Regs& r) {
        Rec(0x2120, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(ById(r.args[0]));
    });
    s.At({0x00462190, AddressOf(&Obj::CTracker::GetObjectByAlias)}, 4, [](Regs& r) {
        Rec(0x2190, {r.ecx, r.args[0]});
        return r.args[0] == 0xA1A5 ? At(ElemAt(4)) : 0u;
    });
    s.At({0x00461A50, AddressOf(&Obj::CBaseManager::UnregisterObject)}, 4, [](Regs& r) {
        Rec(0x1A50, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045FAC0, AddressOf(&Obj::CEventListener::Destroy)}, 0, [](Regs& r) {
        Rec(0xFAC0, {r.ecx});
        return 0u;
    });
    s.At({0x0045FB30, AddressOf(&Obj::CEventListener::RegisterWithTracker)}, 4, [](Regs& r) {
        Rec(0xFB30, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00479090, AddressOf(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9090, {Hash(reinterpret_cast<const char*>(r.args[0]))});
        return At(kPadTypes);
    });
    s.At({0x0044E630, AddressOf(&Inp::Manager::GetPlayerController)}, 4, [](Regs& r) {
        Rec(0xE630, {r.ecx, r.args[0]});
        return static_cast<uint32_t>(g_device);
    });
    s.At({0x0045F980, AddressOf(&Obj::CEvent::MarkRead)}, 8, [](Regs& r) {
        Rec(0xF980, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0045F9B0, AddressOf(&Obj::CEvent::MarkHandled)}, 8, [](Regs& r) {
        Rec(0xF9B0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0045F9E0, AddressOf(&Obj::EventController)}, 0, [](Regs& r) {
        Rec(0xF9E0, {r.args[0]});
        return *reinterpret_cast<uint32_t*>(r.args[0] + 0x0C) >> 16; // (the controller kept in the flags' top)
    });
    s.At({0x00473BE0, AddressOf(&Script::CScript::ObjectDestroyed)}, 4, [](Regs& r) {
        Rec(0x3BE0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004607E0, AddressOf(&Obj::CObject::GetChecksumTag)}, 8, [](Regs& r) {
        Rec(0x07E0, {r.ecx, r.args[0]});
        const std::vector<uint32_t>* value = Lookup('C', r.ecx, r.args[0]);
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = (*value)[0];
        return 1u;
    });
    s.At({0x004AF8C0, AddressOf(&Front::CTextElement::SetText)}, 4, [](Regs& r) {
        Rec(0xF8C0, {r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
         [](Regs& r) {
             Rec(0x1D40, {r.args[0], r.args[1], r.args[2]});
             return At(kMgr);
         });
    // the tracker's LaunchEvent with a result
    s.At({0x00462F00, AddressOf(&Obj::CTracker::LaunchEvent)}, 0x18, [](Regs& r) {
        Rec(0x2F01, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4] & 0xFF, r.args[5]});
        return g_launchHandled && (!g_launchHandledBy || g_launchHandledBy == r.args[1]) ? 1u : 0u;
    });
    for (void*& slot : g_managerVtable)
        slot = nullptr;
    g_managerVtable[2] = s.Thunk(4, [](Regs& r) {
        Rec(0xEE02, {r.ecx, r.args[0]});
        return 0u;
    });
}

// The world: the elements in the manager's object list, a constructed manager, pad event types.
void ManagerWorld(bool original)
{
    BeginWorld(original);
    g_launchHandled = false;
    g_launchHandledBy = 0;
    g_device = 1;
    GameVariable<uint32_t>(0x00701440) = At(kMgr);
    std::memset(&GameVariable<uint8_t>(0x006F3614), 0, 16);
    CScreenElementManager* m = Mgr();
    std::memset(m, 0, sizeof(*m));
    m->Obj::CBaseManager::Construct();
    m->m_vtable = reinterpret_cast<void**>(0x0064A0E8);
    m->m_listener.m_vtable = reinterpret_cast<void**>(0x0064A104);
    // the objects' nodes: element i's node (+0x28) in the list, the head's data -1
    Mem::ListNode* head = &m->m_objects;
    head->data = reinterpret_cast<void*>(-1);
    Mem::ListNode* last = head;
    for (int i : {3, 0, 7, 1, 8, 2}) {
        Mem::ListNode* node = &E(i)->m_node;
        node->data = E(i);
        node->priority = i;
        node->prev = last;
        node->next = head;
        last->next = node;
        head->prev = node;
        last = node;
    }
    MakeArray(30, Script::ESYMBOLTYPE_NAME, {0x30A4F836, 0x0FD1AC26, 0x2B6A5ED1});
    std::memcpy(Ptr<uint8_t>(kPadTypes), Ptr<uint8_t>(kArrays + 0x40 * 30), 0x40);
    *Ptr<uint32_t*>(kPadTypes + 8) = Ptr<uint32_t>(kPadTypes + 0x10);
}

Obj::CEvent* Event(uint32_t type, uint32_t target, int controller, CStruct* data)
{
    auto* event = Ptr<Obj::CEvent>(kEvent);
    event->m_type = type;
    event->m_source = 0x5050;
    event->m_target = target;
    event->m_flags = static_cast<uint32_t>(controller) << 16;
    event->mp_data = data;
    return event;
}

void TestScreenElementManager()
{
    SaveElementGlobals();
    g_saved.push_back({0x006F3614, 16, {}});
    g_saved.push_back({0x00701444, 4, {}});
    SaveAll(g_saved);
    {
        StandIns s;
        InstallManagerStandIns(s);

        RunBoth("element manager: construction", [&](bool original) {
            ManagerWorld(original);
            CScreenElementManager* m = Mgr();
            std::memset(m, 0xCD, sizeof(*m));
            Rec(0x300, {reinterpret_cast<uint32_t>(Call(original, 0x004AB990, m, &CScreenElementManager::Construct))});
            MarkAll(1);
            m->mp_root.Set(E(1));
            m->m_focusEntries[3].element.Set(E(4));
            m->m_priorities[2].element.Set(E(5));
            Call(original, 0x004ABB00, m, &CScreenElementManager::DestroyBody);
            MarkAll(2);
            m->Obj::CBaseManager::Construct();
            for (uint8_t flags : {uint8_t(0), uint8_t(1)})
                Rec(0x301, {reinterpret_cast<uint32_t>(Call(original, 0x004ACB40, m, &CScreenElementManager::Destroy, flags))});
            m->Obj::CBaseManager::Construct();
            Rec(0x302, {reinterpret_cast<uint32_t>(
                           original ? Original<void*(__thiscall*)(void*, uint8_t)>(0x004ABAF0)(&m->m_listener, 1)
                                    : reinterpret_cast<void*(__thiscall*)(void*, uint8_t)>(AddressOf(
                                          &CScreenElementManager::Destroy))(m, 1))});
            MarkAll(3);
            // the singleton: made, its use count, deleted at the last use
            GameVariable<uint32_t>(0x00701440) = 0;
            GameVariable<uint32_t>(0x00701444) = 0;
            for (bool create : {false, true, true}) {
                if (original)
                    Original<void(__cdecl*)(bool)>(0x004ACAF0)(create);
                else
                    Front::CreateScreenElementManager(create);
                Rec(0x303, {GameVariable<uint32_t>(0x00701440), GameVariable<uint32_t>(0x00701444)});
            }
            Mgr()->m_vtable = g_vtable; // (slot 0 records the deletion)
            for (int k = 0; k < 4; ++k) {
                if (original)
                    Original<void(__cdecl*)()>(0x004AACF0)();
                else
                    Front::DeleteScreenElementManager();
                Rec(0x304, {GameVariable<uint32_t>(0x00701440), GameVariable<uint32_t>(0x00701444)});
            }
        });

        RunBoth("element manager: elements and the tree", [&](bool original) {
            ManagerWorld(original);
            CScreenElementManager* m = Mgr();
            auto* out = Ptr<ElementPtr>(kOut);
            for (uint32_t id : {0x1003u, 0x4444u}) {
                std::memset(out, 0xCD, 12);
                Rec(0x310, {reinterpret_cast<uint32_t>(Call(original, 0x004AAE20, m, &CScreenElementManager::GetElement, out, id, 1))});
                out->Release();
            }
            Call(original, 0x004AB730, m, &CScreenElementManager::SetRoot, 0x1001u);
            MarkAll(10);
            // unregistering the root (a new root: the first parentless other element of the list), another
            Call(original, 0x004AAFF0, m, &CScreenElementManager::UnregisterObject, static_cast<Obj::CObject*>(E(1)));
            MarkAll(11);
            m->mp_root.Set(E(3));
            Call(original, 0x004AAFF0, m, &CScreenElementManager::UnregisterObject, static_cast<Obj::CObject*>(E(2)));
            Call(original, 0x004AAFF0, m, &CScreenElementManager::UnregisterObject, static_cast<Obj::CObject*>(nullptr));
            m->mp_root.Set(E(0));
            Call(original, 0x004AAFF0, m, &CScreenElementManager::UnregisterObject, static_cast<Obj::CObject*>(E(0)));
            MarkAll(12);
            // SetParent: the root is the child (it becomes the new parent's top), no root, another root
            ElementPtr* parent = Ptr<ElementPtr>(kOut + 0x10);
            ElementPtr* child = Ptr<ElementPtr>(kOut + 0x20);
            auto setParent = [&](int p, int c, int root, uint32_t mark) {
                parent->mp_object = nullptr;
                parent->Set(p < 0 ? nullptr : E(p));
                child->mp_object = nullptr;
                child->Set(E(c));
                m->mp_root.Set(root < 0 ? nullptr : E(root));
                Call(original, 0x004ABBE0, m, &CScreenElementManager::SetParent, parent, child, 5);
                parent->Release();
                child->Release();
                MarkAll(mark);
            };
            m->m_focusEntries[0].element.Set(E(7));
            m->m_focusEntries[0].id = 0x1007;
            m->mp_focusLists[0] = &m->m_focusEntries[0];
            setParent(5, 7, 7, 13);
            setParent(9, 4, -1, 14);
            setParent(-1, 6, 0, 15);
            // FindByLocalId
            Answer('C', At(ElemAt(3)), 0x9494A525, {0x51});
            Answer('C', At(ElemAt(4)), 0x9494A525, {0x52});
            ElementPtr* from = Ptr<ElementPtr>(kOut + 0x30);
            from->mp_object = nullptr;
            from->Set(E(1));
            for (uint32_t local : {0x52u, 0x99u}) {
                std::memset(out, 0xCD, 12);
                Call(original, 0x004AB130, m, &CScreenElementManager::FindByLocalId, out, static_cast<const ElementPtr*>(from), local);
                Rec(0x311, {reinterpret_cast<uint32_t>(out->mp_object)});
                out->Release();
            }
            from->Release();
            MarkAll(16);
        });

        RunBoth("element manager: ids from parameters", [&](bool original) {
            ManagerWorld(original);
            CScreenElementManager* m = Mgr();
            uint32_t* id = Ptr<uint32_t>(kOut + 0x40);
            constexpr uint32_t kName = 0x40C698AF;
            // 0: { 0x1001 child = { local 0x52 } }; 1: { index 1 child = 0x51 } (index within nothing: crash
            // avoided by 2 first); 2: { 0x1001 child = 2 }; 3: an alias; 4: a plain id; 5: an integer (nothing
            // resolved: false); 6: { 0x4444 }: not found; 7: nothing; 8: { 0x1001 child = { 0x99 } } (not found:
            // then the plain checksum is tried)
            Answer('C', At(ElemAt(3)), 0x9494A525, {0x51});
            Answer('C', At(ElemAt(4)), 0x9494A525, {0x52});
            auto sub = [](int n) { return At(kParams + 0x40 * n); };
            Answer('S', sub(0), kName, {sub(10)});
            Answer('K', sub(10), 0, {0x1001});
            Answer('S', sub(10), 0xDD4CABD6, {sub(11)});
            Answer('K', sub(11), 0, {0x52});
            Answer('K', sub(11), 0xDD4CABD6, {0x52});
            Answer('S', sub(2), kName, {sub(12)});
            Answer('K', sub(12), 0, {0x1001});
            Answer('I', sub(12), 0xDD4CABD6, {2});
            Answer('K', sub(3), kName, {0xA1A5});
            Answer('K', sub(4), kName, {0x1006});
            Answer('I', sub(5), kName, {1});
            Answer('S', sub(6), kName, {sub(13)});
            Answer('K', sub(13), 0, {0x4444});
            Answer('S', sub(8), kName, {sub(14)});
            Answer('K', sub(14), 0, {0x1001});
            Answer('S', sub(14), 0xDD4CABD6, {sub(9)});
            Answer('K', sub(9), 0, {0x99});
            Answer('K', sub(8), kName, {0x1005});
            for (int k : {0, 2, 3, 4, 5, 6, 7, 8}) {
                *id = 0xEEEE;
                Rec(0x320, {static_cast<uint32_t>(Call(original, 0x004AC120, m, &CScreenElementManager::ResolveId,
                                                       Params(k), kName, id)) & 0xFF,
                            *id, reinterpret_cast<uint32_t>(m->mp_resolving.mp_object)});
            }
            // an index under a resolved structure: { 0x1003 child = 1 }
            Answer('S', sub(1), kName, {sub(15)});
            Answer('K', sub(15), 0, {0x1003});
            Answer('I', sub(15), 0xDD4CABD6, {1});
            Rec(0x321, {static_cast<uint32_t>(Call(original, 0x004AC120, m, &CScreenElementManager::ResolveId, Params(1), kName, id)) & 0xFF,
                        *id});
            auto* out = Ptr<ElementPtr>(kOut);
            for (int k : {4, 7}) {
                std::memset(out, 0xCD, 12);
                Call(original, 0x004AD1E0, m, &CScreenElementManager::ResolveElement, out, Params(k), kName, 1);
                Rec(0x322, {reinterpret_cast<uint32_t>(out->mp_object)});
                out->Release();
            }
            MarkAll(20);
        });

        RunBoth("element manager: frames, destruction", [&](bool original) {
            ManagerWorld(original);
            CScreenElementManager* m = Mgr();
            Call(original, 0x004ABD00, m, &CScreenElementManager::Update);
            m->mp_root.Set(E(1));
            E(4)->m_aliasCount = 0x80000000u;
            uint32_t lockMark = 300;
            for (int lock : {1, 1, 0, 0, 0, 2}) {
                Call(original, 0x004AAF90, m, &CScreenElementManager::LockTree, lock);
                MarkAll(lockMark++);
            }
            MarkAll(30);
            m->m_numPriorities = 0;
            Call(original, 0x004ABD00, m, &CScreenElementManager::Update);
            MarkAll(31);
            for (bool pause : {true, false})
                Call(original, 0x004AAEE0, m, &CScreenElementManager::SetPausedAll, pause);
            MarkAll(32);
            // destroying: a subtree (recursing; its parent's focus flag dropped), one element alone; with a script
            m->m_vtable = g_managerVtable;
            auto* script = Ptr<Script::CScript>(kScript);
            E(1)->m_objectFlags |= CScreenElement::kFocusSent;
            Call(original, 0x004AD100, m, &CScreenElementManager::DestroyElement, 0x1003u, 1, 0, script);
            MarkAll(33);
            Call(original, 0x004AD100, m, &CScreenElementManager::DestroyElement, 0x1009u, 0, 0,
                 static_cast<Script::CScript*>(nullptr));
            Call(original, 0x004AD100, m, &CScreenElementManager::DestroyElement, 0x1004u, 1, 1, script);
            MarkAll(34);
            ElementPtr* e = Ptr<ElementPtr>(kOut);
            e->mp_object = nullptr;
            e->Set(E(2));
            Call(original, 0x004AC400, m, &CScreenElementManager::DestroyTree, 0, e, script);
            e->Release();
            MarkAll(35);
        });

        RunBoth("element manager: focus lists", [&](bool original) {
            ManagerWorld(original);
            CScreenElementManager* m = Mgr();
            ElementPtr* e = Ptr<ElementPtr>(kOut);
            auto add = [&](int element, int list) {
                e->mp_object = nullptr;
                e->Set(E(element));
                Call(original, 0x004AB250, m, &CScreenElementManager::AddFocusEntry, e, list);
                e->Release();
            };
            auto remove = [&](int element, bool fromParent, bool mark) {
                e->mp_object = nullptr;
                e->Set(E(element));
                Call(original, 0x004AB330, m, &CScreenElementManager::RemoveFocusEntries, e, fromParent, mark);
                e->Release();
            };
            add(2, 0);
            add(5, 1);
            add(4, 0); // (appended to list 1's tail, as the original)
            add(2, 0);
            add(6, 1);
            m->m_focusEntries[0].marked = true;
            add(2, 0); // (unmarked)
            add(5, 0);
            // an element heading list 1 (as when both lists were empty) added for list 0: its own entry
            m->m_focusEntries[20].element.Set(E(9));
            m->m_focusEntries[20].id = 0x1009;
            m->m_focusEntries[20].next = nullptr;
            m->mp_focusLists[1] = &m->m_focusEntries[20];
            add(9, 0);
            MarkAll(40);
            remove(2, true, false); // (from its parent on: entry 2 itself stays)
            MarkAll(46);
            remove(3, true, true);
            MarkAll(41);
            e->mp_object = nullptr;
            e->Set(E(1));
            Call(original, 0x004AB4D0, m, &CScreenElementManager::ClearMarks, e);
            e->Release();
            MarkAll(42);
            remove(3, false, true);
            remove(1, true, false);
            MarkAll(43);
            remove(4, false, false);
            remove(0, false, false);
            MarkAll(44);
            // the entries full
            for (int i = 0; i < CScreenElementManager::kFocusEntries; ++i)
                m->m_focusEntries[i].element.Set(E(9));
            add(8, 1);
            MarkAll(45);
        });

        RunBoth("element manager: events", [&](bool original) {
            ManagerWorld(original);
            CScreenElementManager* m = Mgr();
            void* listener = &m->m_listener;
            auto receive = [&](Obj::CEvent* event, uint32_t mark) {
                if (original)
                    Original<void(__thiscall*)(void*, Obj::CEvent*)>(0x004ABD40)(listener, event);
                else
                    reinterpret_cast<CScreenElementManager*>(listener)->ListenerReceive(event);
                MarkAll(mark);
            };
            // focus / unfocus (from the system: ignored), with blocked events, the focus flag's events
            receive(Event(0x9D3FB516, 0x1002, 0, nullptr), 50);
            receive(Event(0x9D3FB516, 0x1005, 1, nullptr), 51);
            receive(Event(0x9D3FB516, 0x1006, 1, nullptr), 52);
            receive(Event(0x9D3FB516, 0x36B2EE74, 0, nullptr), 53);
            E(4)->m_objectFlags |= CScreenElement::kBlockEvents;
            receive(Event(0x9D3FB516, 0x1004, 0, nullptr), 54);
            receive(Event(0x4ADF0CD3, 0x1004, 0, nullptr), 55);
            receive(Event(0x9D3FB516, 0x1003, 0, nullptr), 68);
            receive(Event(0x4A57316B, 0x1003, 0, nullptr), 56);
            receive(Event(0x3056434F, 0x1001, 0, nullptr), 57);
            receive(Event(0x4ADF0CD3, 0x1005, 1, nullptr), 58);
            // pad events from the system: not handled (each tried), handled by the second, not a pad type, not from
            // the system, another device
            receive(Event(0x30A4F836, 0x36B2EE74, 0, nullptr), 59);
            g_launchHandled = true;
            g_launchHandledBy = 0x1006;
            receive(Event(0x9D3FB516, 0x1006, 0, nullptr), 60);
            receive(Event(0x0FD1AC26, 0x36B2EE74, 0, nullptr), 61);
            receive(Event(0x12345678, 0x36B2EE74, 0, nullptr), 62);
            receive(Event(0x30A4F836, 0x1002, 0, nullptr), 63);
            m->m_checkDevice = true;
            Answer('I', At(kParams + 0x40 * 6), 0xC9428A08, {2});
            receive(Event(0x30A4F836, 0x36B2EE74, 0, Params(6)), 64);
            g_device = 2;
            receive(Event(0x30A4F836, 0x36B2EE74, 0, Params(6)), 65);
            // with priorities: only the top priority element gets pad events
            g_launchHandled = false;
            m->m_numPriorities = 1;
            m->m_priorities[0].active = true;
            m->m_priorities[0].priority = 5;
            m->m_priorities[0].id = 0x1006;
            E(6)->m_priority = 5;
            E(6)->m_objectFlags |= CScreenElement::kPriority;
            receive(Event(0x30A4F836, 0x36B2EE74, 0, nullptr), 66);
            receive(Event(0x2B6A5ED1, 0x36B2EE74, 0, nullptr), 67);
        });

        RunBoth("element manager: priorities", [&](bool original) {
            ManagerWorld(original);
            CScreenElementManager* m = Mgr();
            for (uint32_t type : {0x30A4F836u, 0x2B6A5ED1u, 0x99u}) {
                m->m_numPadEventTypes = 3;
                std::memcpy(m->m_padEventTypes, Ptr<uint32_t>(kPadTypes + 0x10), 12);
                Rec(0x340, {static_cast<uint32_t>(CallC(original, 0x004AAD50, m, &CScreenElementManager::IsPadEvent, type)) & 0xFF});
            }
            Rec(0x341, {static_cast<uint32_t>(CallC(original, 0x004AAD80, m, &CScreenElementManager::HasPriorities)) & 0xFF});
            // pushes: into free slots (lower, higher: who is hidden), until full, a lower one refused, a higher
            // one replacing the lowest
            const uint32_t pushes[][2] = {{0x1002, 3}, {0x1003, 7}, {0x1004, 5}, {0x1005, 7}, {0x1006, 1},
                                          {0x1007, 2}, {0x1008, 9}, {0x1009, 4}, {0x1001, 6}, {0x1000, 8},
                                          {0x1002, 0}, {0x1003, 10}, {0x1005, 2}}; // (2: the lowest, replaced)
            uint32_t mark = 70;
            for (const auto& p : pushes) {
                Rec(0x342, {static_cast<uint32_t>(Call(original, 0x004AC570, m, &CScreenElementManager::PushPriority,
                                                       p[0], p[1])) & 0xFF});
                Rec(0x343, {static_cast<uint32_t>(CallC(original, 0x004AAD80, m, &CScreenElementManager::HasPriorities)) & 0xFF});
                MarkAll(mark++);
            }
            for (int i = 0; i < 10; ++i)
                E(i)->m_priority = static_cast<int>(m->m_priorities[i].priority);
            for (uint32_t id : {0x1003u, 0x1008u, 0x1001u})
                Rec(0x344, {static_cast<uint32_t>(Call(original, 0x004AB7C0, m, &CScreenElementManager::IsTopPriority, id)) & 0xFF});
            // elements gone: their slots freed, the top shown
            E(3)->m_id = 0x7003;
            E(8)->m_id = 0x7008;
            Call(original, 0x004AB5D0, m, &CScreenElementManager::UpdatePriorities);
            MarkAll(90);
            // an inactive slot's priority doesn't count
            m->m_priorities[9].active = false;
            m->m_priorities[9].priority = 50;
            for (uint32_t id : {0x1000u, 0x1001u, 0x1002u, 0x1004u, 0x1005u, 0x1006u, 0x1007u, 0x1009u})
                Rec(0x345, {static_cast<uint32_t>(Call(original, 0x004AB7C0, m, &CScreenElementManager::IsTopPriority, id)) & 0xFF});
            // the score text
            for (int player : {0, 2}) {
                E(5)->m_id = 0x675ADBAA + 2;
                if (original)
                    Original<void(__cdecl*)(const char*, int)>(0x004AB910)("12345", player);
                else
                    Front::SetScoreText("12345", player);
            }
            MarkAll(91);
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestScreenElementManager, FeTest::Cleanup);
