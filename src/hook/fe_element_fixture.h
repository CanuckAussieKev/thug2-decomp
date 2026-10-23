#pragma once

// The screen-element tests' shared world (selftests_fe_*.cpp): an arena of element trees with real smart-pointer
// links and patterned states, a stand-in vtable recording the element's virtual slots, and recording stand-ins
// for what elements call outside themselves (the object base and tags, the tracker's events, the screen element
// manager, the camera's projection, the clock, the structure getters / adders, arrays, the memory manager).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/obj/event.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/sys/timer.h"
#include "hook/fe_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace FeTest {

using Front::CScreenElement;
using Front::ElementPtr;
using Front::SElementState;

// Arena layout.
constexpr uint32_t kElems = 0x1000;     // 16 elements, 0x200 apart
constexpr int kCount = 16;
constexpr uint32_t kManager = 0x3200;
constexpr uint32_t kTracker = 0x3300;
constexpr uint32_t kCamera = 0x3400;
constexpr uint32_t kMemManager = 0x3500;
constexpr uint32_t kArrayMem = 0x3600;  // an array allocated from the memory manager
constexpr uint32_t kArrayElems = 0x3640;
constexpr uint32_t kArrays = 0x3700;    // stand-in arrays, 0x40 apart (elements at +0x10)
constexpr uint32_t kParams = 0x3C00;    // stand-in structures, 0x40 apart
constexpr uint32_t kScript = 0x4000;    // a stand-in script (+0x14: its params)
constexpr uint32_t kVector = 0x4100;
constexpr uint32_t kOut = 0x4200;       // outputs
constexpr uint32_t kEvent = 0x4300;     // a stand-in event
constexpr uint32_t kEnd = 0x4400;

inline uint32_t ElemAt(int i) { return kElems + 0x200 * i; }
inline CScreenElement* E(int i) { return Ptr<CScreenElement>(ElemAt(i)); }
inline CStruct* Params(int n) { return Ptr<CStruct>(kParams + 0x40 * n); }

inline void* g_vtable[16];
inline uint32_t g_time;
inline bool g_usesPriorities;
inline float g_projScale;
inline float g_proj[3];
inline std::vector<Saved> g_saved;

inline void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_saved);
}

inline CScreenElement* ById(uint32_t id)
{
    for (int i = 0; i < kCount; ++i)
        if (E(i)->m_id == id)
            return E(i);
    return nullptr;
}

// `manager`: the screen element manager's functions stood in for too (not when testing them).
inline void InstallElementStandIns(StandIns& s, bool manager = true)
{
    StandInStructGetters(s);
    StandInStructAdders(s);
    // the element's own slots
    for (void*& slot : g_vtable)
        slot = nullptr;
    g_vtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0xE001, {r.ecx, r.args[0]});
        return 0u;
    });
    g_vtable[8] = s.Thunk(4, [](Regs& r) {
        Rec(0xE008, {r.ecx, r.args[0]});
        return 0u;
    });
    g_vtable[9] = s.Thunk(4, [](Regs& r) {
        Rec(0xE009, {r.ecx, r.args[0]});
        return 0u;
    });
    g_vtable[10] = s.Thunk(0, [](Regs& r) {
        Rec(0xE00A, {r.ecx, *reinterpret_cast<uint32_t*>(r.ecx + 8)});
        return 0u;
    });
    // the object base
    s.At({0x00460B80, AddressOf(&Obj::CObject::Construct)}, 0, [](Regs& r) {
        Rec(0x0B80, {r.ecx});
        *reinterpret_cast<uint32_t*>(r.ecx + 8) = 0; // (the object's flags start clear)
        return r.ecx;
    });
    s.At({0x004615D0, AddressOf(&Obj::CObject::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x15D0, {r.ecx});
        return 0u;
    });
    s.At({0x00461710, AddressOf(&Obj::CObject::SetProperties)}, 4, [](Regs& r) {
        Rec(0x1710, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004608F0, AddressOf(static_cast<bool (Obj::CObject::*)(uint32_t, CStruct*, Script::CScript*)>(
                          &Obj::CObject::CallMemberFunction))},
         12, [](Regs& r) {
             Rec(0x08F0, {r.ecx, r.args[0], r.args[1], r.args[2]});
             return 0x55AA01u;
         });
    s.At({0x004616D0, AddressOf(&Obj::CObject::SetChecksumTag)}, 8, [](Regs& r) {
        Rec(0x16D0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004607B0, AddressOf(&Obj::CObject::RemoveTagFlag)}, 4, [](Regs& r) {
        Rec(0x07B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00462F00, AddressOf(&Obj::CTracker::LaunchEvent)}, 0x18, [](Regs& r) {
        Rec(0x2F00, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4] & 0xFF, r.args[5]});
        return 0u;
    });
    s.At({0x00472C10, AddressOf(&Script::CScript::WaitTime)}, 4, [](Regs& r) {
        Rec(0x2C10, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) { return g_time; });
    // the camera's projection
    s.At({0x004A06D0}, 0, [](Regs& r) {
        Rec(0x06D0, {r.args[0]});
        return At(kCamera);
    });
    AtFloat(s, {0x004A1090}, 16, [](Regs& r) {
        const auto* p = reinterpret_cast<const uint32_t*>(r.args[0]);
        Rec(0x1090, {r.ecx, r.args[0], p[0], p[1], p[2], p[3]});
        for (int i = 0; i < 3; ++i)
            *reinterpret_cast<float*>(r.args[1 + i]) = g_proj[i];
        return g_projScale;
    });
    // memory and arrays
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return At(kArrayMem);
    });
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        auto* array = reinterpret_cast<Script::CArray*>(r.ecx);
        array->m_size = r.args[0];
        array->m_type = static_cast<Script::ESymbolType>(r.args[1]);
        array->m_elements = Ptr<uint32_t>(kArrayElems);
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&Script::CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        auto* array = reinterpret_cast<Script::CArray*>(r.ecx);
        array->m_elements[r.args[0]] = r.args[1];
        return 0u;
    });
    s.At({0x00477140, AddressOf(&Script::CleanUpArray)}, 0, [](Regs& r) {
        Rec(0x7140, {r.args[0]});
        return 0u;
    });
    s.At({0x0046CAB0, AddressOf(&Script::CArray::Clear)}, 0, [](Regs& r) {
        Rec(0xCAB0, {r.ecx});
        return 0u;
    });
    s.At({0x00408FD0, AddressOf(&Script::DeleteArray)}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        return 0u;
    });
    if (!manager)
        return;
    // the screen element manager
    s.At({0x004AAD80}, 0, [](Regs& r) {
        Rec(0xAD80, {r.ecx});
        return g_usesPriorities ? 1u : 0u;
    });
    s.At({0x004AC120, AddressOf(&Front::CScreenElementManager::ResolveId)}, 12, [](Regs& r) {
        Rec(0xC120, {r.ecx, r.args[0], r.args[1]});
        const std::vector<uint32_t>* value = Lookup('E', r.args[0], r.args[1]);
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[2]) = (*value)[0];
        return 1u;
    });
    s.At({0x004AAE20}, 12, [](Regs& r) {
        Rec(0xAE20, {r.ecx, r.args[1], r.args[2]});
        auto* out = reinterpret_cast<ElementPtr*>(r.args[0]);
        out->mp_object = nullptr;
        out->mp_prev = out->mp_next = nullptr;
        out->Set(ById(r.args[1]));
        return r.args[0];
    });
    s.At({0x004ABBE0}, 12, [](Regs& r) {
        Rec(0xBBE0, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), *reinterpret_cast<uint32_t*>(r.args[1]),
                     r.args[2]});
        return 0u;
    });
    s.At({0x004AD100}, 16, [](Regs& r) {
        Rec(0xD100, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
}

// An element with patterned states (seed n), links cleared; vtable g_vtable, id 0x1000 + i.
inline void Fill(int i, int n)
{
    auto* e = E(i);
    std::memset(e, 0, 0x200);
    e->m_vtable = g_vtable;
    e->m_id = 0x1000 + i;
    e->m_type = 0x77000000u + i;
    e->mp_manager = Ptr<Obj::CBaseManager>(kManager);
    e->m_objectFlags = 0;
    SElementState* states[3] = {&e->m_shown, &e->m_target, &e->m_summed};
    for (int k = 0; k < 3; ++k) {
        SElementState& s = *states[k];
        float base = 1.0f + 0.37f * (n + 3 * k);
        s.alpha = 0.25f + 0.05f * (n + k);
        s.pos3d = {base, -base * 2.0f, base * 3.5f, 1.0f};
        s.x = 100.5f + 13.25f * n - 7.0f * k;
        s.y = 50.25f - 9.5f * n + 3.0f * k;
        s.z = 0.5f * k;
        s.left = s.x - 20.0f;
        s.top = s.y - 10.0f;
        s.scaleX = 1.25f + 0.125f * n + 0.0625f * k;
        s.scaleY = 0.75f + 0.25f * k - 0.03125f * n;
        s.baseScaleX = 1.0f + 0.5f * k;
        s.baseScaleY = 2.0f - 0.25f * n;
        s.angle = 0.1f * n - 0.2f * k;
        for (int b = 0; b < 4; ++b)
            s.rgba[b] = static_cast<uint8_t>(0x20 * b + 0x31 * n + 0x0B * k);
        s.unknown40 = 0x4040 + n;
    }
    e->m_justX = -1.0f + 0.5f * (n % 5);
    e->m_justY = 1.0f - 0.25f * (n % 9);
    e->m_width = 80.0f + 10.0f * n;
    e->m_height = 20.0f + 3.0f * n;
    e->m_zPriority = 0.5f * n;
    e->m_rememberedAlpha = 0.375f;
    e->m_priority = n;
    e->m_morphStart = 1000 + n;
    e->m_morphLength = 0;
}
// `child` last among `parent`'s children (the smart-pointer links as SetParent leaves them).
inline void Link(int parent, int child)
{
    CScreenElement* p = E(parent);
    CScreenElement* c = E(child);
    c->mp_parent.Set(p);
    CScreenElement* last = p->GetLastChild();
    if (last)
        last->mp_next.Set(c);
    else
        p->mp_firstChild.Set(c);
    c->mp_prev.Set(last);
}

// A tree: 0 (window type) <- 1 <- {2, 3, 4}; 3 <- {5, 6}; 7 alone; 8 <- 9.
inline void Tree()
{
    for (int i = 0; i < 10; ++i)
        Fill(i, i);
    E(0)->m_type = 0xA13ADBB7;
    Link(0, 1);
    Link(1, 2);
    Link(1, 3);
    Link(1, 4);
    Link(3, 5);
    Link(3, 6);
    Link(8, 9);
}

inline void SaveElementGlobals()
{
    g_saved = {{0x00701440, 4, {}}, {0x006F0FC0, 4, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}},
               {0x006F22B4, 16, {}}};
    SaveAll(g_saved);
}

inline void BeginWorld(bool original)
{
    BeginRun(original);
    g_time = 50000;
    g_usesPriorities = false;
    g_projScale = 1.5f;
    g_proj[0] = 210.5f;
    g_proj[1] = 140.25f;
    g_proj[2] = 0.875f;
    g_answers.clear();
    GameVariable<uint32_t>(0x00701440) = At(kManager);
    GameVariable<uint32_t>(0x006F0FC0) = At(kTracker);
    GameVariable<uint32_t>(0x007D1800) = At(kMemManager);
    GameVariable<bool>(0x007CC979) = true;
    std::memset(&GameVariable<uint8_t>(0x006F22B4), 0, 16);
    *Ptr<uint32_t>(kScript + 0x14) = At(kParams + 0x40 * 15);
    Tree();
}
inline void Mark(uint32_t n) { Snapshot(0, kEnd, n); }

template <typename T>
struct Same {
    using type = T;
};
// A member function of `e` (any element class): the original at `address`, or ours.
template <typename C, typename R, typename... A>
R Call(bool original, uint32_t address, C* e, R (C::*ours)(A...), typename Same<A>::type... args)
{
    if (original)
        return Original<R(__thiscall*)(C*, A...)>(address)(e, args...);
    return (e->*ours)(args...);
}
template <typename C, typename R, typename... A>
R CallC(bool original, uint32_t address, const C* e, R (C::*ours)(A...) const, typename Same<A>::type... args)
{
    if (original)
        return Original<R(__thiscall*)(const C*, A...)>(address)(e, args...);
    return (e->*ours)(args...);
}

// A stand-in array at kArrays + 0x40 n: type, elements.
inline uint32_t MakeArray(int n, Script::ESymbolType type, std::initializer_list<uint32_t> elements)
{
    auto* array = Ptr<Script::CArray>(kArrays + 0x40 * n);
    array->Init();
    array->m_type = type;
    array->m_size = static_cast<uint32_t>(elements.size());
    uint32_t* store = Ptr<uint32_t>(kArrays + 0x40 * n + 0x10);
    uint32_t k = 0;
    for (uint32_t v : elements)
        store[k++] = v;
    if (array->m_size == 1)
        array->m_single = store[0];
    else
        array->m_elements = store;
    return At(kArrays + 0x40 * n);
}

} // namespace FeTest
