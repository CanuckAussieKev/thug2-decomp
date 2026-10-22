// Self-tests for the park editor module, its gap manager and its script commands (src/game/ed/gap_manager.cpp,
// park_editor.cpp, park_editor_cfuncs.cpp; 0x0058CD40-0x0058FBD0). Each function runs twice on a stand-in world
// (the park manager, the cursor, the editor, the gap manager and its gaps, script structures and the objects
// around them, all in one static block at the same address in both runs) - the original, then ours - with the
// game functions it calls stood in for by recording code (and scripted results); the calls, results and the whole
// world are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
#include <type_traits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/task.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/piece_menu.h"
#include "game/front/front_end.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace Ed;
using CFuncTests::Params;

// Everything the code under test touches.
struct World {
    CParkManager manager;
    CCursor cursor;
    CParkEditor editor;
    CGapManager gapManager;
    uint8_t gaps[4][0xF0];       // gap records (Calloc's blocks)
    uint8_t generator[0x40];
    uint8_t pieces[4][0x30];
    uint8_t checklist[0x20];
    uint8_t goalEditor[0x10];
    uint8_t railEditor[0x10];
    uint8_t skate[0x40];
    uint8_t career[0x10];
    uint8_t object[0x100];
    void* objectVtable[8];
    uint8_t script[0x100];
    uint8_t scriptParams[8];
    uint8_t memManager[0x200];
    uint8_t structs[4][8];
    uint8_t frontEnd[0x180];
    uint8_t mainLoop[0x80];
    Inp::Manager input;
    uint8_t tasks[3][0x30];
    uint8_t upperMenu[0x10];
    uint8_t buffer[0x80];
    uint8_t cameraObject[0x100];
    CompressedMap* map;
};
alignas(16) World g_world;
int g_nextGap;

// Scripted results by function (address): the next one is taken at each call (0 when they run out).
std::map<uint32_t, std::deque<uint32_t>> g_script;
uint32_t Next(uint32_t function)
{
    auto& queue = g_script[function];
    if (queue.empty())
        return 0;
    uint32_t value = queue.front();
    queue.pop_front();
    return value;
}
template <typename T>
uint32_t U(T* pointer)
{
    return reinterpret_cast<uint32_t>(pointer);
}
uint32_t F(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// Pointers to the caller's locals differ between the original and ours: they're recorded as one marker.
uint32_t Norm(uint32_t value)
{
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    auto low = reinterpret_cast<uint32_t>(tib->StackLimit), high = reinterpret_cast<uint32_t>(tib->StackBase);
    return value >= low && value < high ? 0x57AC0000u : value;
}

// Our function registered at a game address (from the replacements' own table), or 0.
uint32_t OursAt(uint32_t address)
{
    static std::map<uint32_t, void*> ours;
    if (ours.empty()) {
        for (const ArgumentSizeCheck& c : CheckArgumentSizes()) {
            if (!c.replacement.fallback)
                ours[c.replacement.address] = c.replacement.function;
        }
    }
    auto it = ours.find(address);
    return it == ours.end() ? 0u : Resolve(it->second);
}

// Stand in for the functions at `at` (and ours registered there): record the address, ECX and `n` stack
// arguments (those whose bit is set in `words` are GridDims halves: only their low 16 bits count), then return
// `result` (or the scripted value).
void Rec(StandIns& s, std::initializer_list<uint32_t> at, uint16_t pop, int n, uint32_t words = 0,
         std::function<uint32_t(Regs&)> result = nullptr, bool noEcx = false)
{
    uint32_t id = *at.begin();
    void* thunk = s.Thunk(pop, [=](Regs& r) {
        Record({id, noEcx ? 0u : Norm(r.ecx)});
        for (int i = 0; i < n; ++i)
            Record({(words >> i) & 1 ? r.args[i] & 0xFFFF : Norm(r.args[i])});
        return result ? result(r) : Next(id);
    });
    for (uint32_t address : at) {
        s.Bytes(address, Branch(0xE9, address, thunk));
        if (uint32_t ours = OursAt(address))
            s.Bytes(ours, Branch(0xE9, ours, thunk));
    }
}
// The same for a cdecl function (no ECX).
void RecC(StandIns& s, std::initializer_list<uint32_t> at, int n, std::function<uint32_t(Regs&)> result = nullptr)
{
    Rec(s, at, 0, n, 0, std::move(result), true);
}

uint32_t Text(uint32_t pointer) { return Hash(reinterpret_cast<const char*>(pointer)); }

void StandInGame(StandIns& s)
{
    // memory
    RecC(s, {0x00401CC0}, 1, [](Regs& r) {
        uint8_t* block = g_world.gaps[g_nextGap++ & 3];
        std::memset(block, 0, r.args[0]);
        return U(block);
    });
    Rec(s, {0x005BABA0}, 4, 1);
    Rec(s, {0x005BAFC0}, 0xC, 3, 0, [](Regs&) { return U(g_world.structs[g_nextGap++ & 3]); });
    Rec(s, {0x005BA3D0}, 0, 0, 0, [](Regs&) { return U(g_world.structs[g_nextGap++ & 3]); });
    RecC(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1);
    s.At({0x006234FE}, 0, StandInSprintf);
    // structures
    Rec(s, {0x00476410}, 0, 0, 0, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 8);
        return r.ecx;
    });
    Rec(s, {0x00477130}, 0, 0);
    Rec(s, {0x00477D40}, 8, 2);
    Rec(s, {0x00477DF0}, 8, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), r.args[1]});
        return 0u;
    });
    Rec(s, {0x00477B80}, 8, 2);
    Rec(s, {0x00477C30}, 8, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), r.args[1]});
        return 0u;
    });
    Rec(s, {0x004779D0}, 8, 1, 0, [](Regs& r) {
        Record({Text(r.args[1])});
        return 0u;
    });
    Rec(s, {0x00477A90}, 8, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), Text(r.args[1])});
        return 0u;
    });
    Rec(s, {0x00477E20}, 0x10, 4);
    RecC(s, {0x00478F60}, 1);
    RecC(s, {0x00479090}, 0, [](Regs& r) {
        Record({Text(r.args[0])});
        return Next(0x00479090);
    });
    // scripts
    RecC(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                             &Script::RunScript))},
         5);
    // the gap checklist, the career, the goal and rail editors
    Rec(s, {0x00584110}, 4, 1);
    Rec(s, {0x00578840}, 0, 0);
    Rec(s, {0x00578880}, 0xC, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), r.args[1], r.args[2]});
        return 0u;
    });
    Rec(s, {0x005656C0}, 0, 0, 0, [](Regs&) { return U(g_world.checklist); });
    Rec(s, {0x00578B30}, 4, 0, 0, [](Regs& r) {
        Record({Text(r.args[0])});
        return Next(0x00578B30);
    });
    RecC(s, {0x004E8470}, 0, [](Regs&) { return U(g_world.goalEditor); });
    RecC(s, {0x004F4970}, 0, [](Regs&) { return U(g_world.railEditor); });
    Rec(s, {0x004E8420}, 4, 1);
    // the generator, pieces
    Rec(s, {0x005935F0}, 4, 1);
    Rec(s, {0x005906B0}, 4, 1);
}

using Setup = std::function<void()>;
using Call = std::function<uint32_t(bool original)>;

// A starting world: the singletons pointing into it, a park, a cursor, an editor, two gaps.
void BasicWorld()
{
    World& w = g_world;
    CParkManager& m = w.manager;
    m.mp_generator = reinterpret_cast<CParkGenerator*>(w.generator);
    m.mp_gapManager = &w.gapManager;
    m.m_parkBounds = GridDims::Make(10, 0, 12, 30, 0, 28);
    m.m_shellBounds = GridDims::Make(4, 0, 5, 40, 0, 44);
    CCursor& c = w.cursor;
    c.mp_manager = &m;
    c.m_mode = CCursor::kModeRegular;
    c.m_pos = {-5.5f, 2.0f, 7.25f, 1.0f};
    c.m_clipboardSet = 3;
    c.m_menuIndex = 2;
    CGapManager& g = w.gapManager;
    g.m_nextId = 7;
    g.mp_manager = &m;
    for (int i = 0; i < 2; ++i) {
        auto* gap = reinterpret_cast<CGapManager::Gap*>(w.gaps[i]);
        gap->m_ids[0] = 3 + 2 * i;
        gap->m_ids[1] = 4 + 2 * i;
        gap->mp_pieces[0] = reinterpret_cast<CPiece*>(w.pieces[2 * i]);
        gap->mp_pieces[1] = reinterpret_cast<CPiece*>(w.pieces[2 * i + 1]);
        std::strcpy(gap->m_name, i ? "second gap" : "first gap");
        std::strcpy(gap->m_info.m_text, i ? "text two" : "text one");
        gap->m_info.m_score = 100 + 50 * i;
        gap->m_info.m_cancelFlags = i ? 0 : 0x1234;
        gap->m_info.m_tabIndex = i ? 9 : 2;
    }
    g.mp_gaps[2] = reinterpret_cast<CGapManager::Gap*>(w.gaps[0]);
    g.mp_gaps[9] = reinterpret_cast<CGapManager::Gap*>(w.gaps[1]);
    g.m_startedFirst[2] = true;
    g_nextGap = 2;
    *reinterpret_cast<void**>(w.skate + 0x20) = w.career;
    *reinterpret_cast<void***>(w.object) = w.objectVtable;
    reinterpret_cast<Script::CScript*>(w.script)->m_params = reinterpret_cast<Script::CStruct*>(w.scriptParams);
    reinterpret_cast<Script::CScript*>(w.script)->m_object.mp_object = reinterpret_cast<Obj::CObject*>(w.object);
    GameVariable<CParkManager*>(0x007CE604) = &m;
    GameVariable<CCursor*>(0x007CE608) = &c;
    GameVariable<void*>(0x007CE60C) = w.upperMenu;
    GameVariable<CGapManager*>(0x007CE5D4) = &g;
    GameVariable<CParkEditor*>(0x007CE5D8) = &w.editor;
    GameVariable<void*>(0x007CE478) = w.skate;
    GameVariable<void*>(0x007D1800) = w.memManager;
    GameVariable<uint8_t>(0x007CC979) = 1;
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16); // the structure pools: from the manager
}

// Run `call` on the original and on ours from the same `setup`; compare calls, results and the world.
void RunCase(const char* what, Setup setup, Call call, std::function<void(StandIns&)> more = nullptr)
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        std::memset(&g_world, 0, sizeof(g_world));
        g_script.clear();
        CFuncTests::UseEmptySymbolTable();
        BasicWorld();
        setup();
        auto* s = new StandIns;
        StandInGame(*s);
        if (more)
            more(*s);
        // the object's member functions (vtable slot 3)
        g_world.objectVtable[3] = s->Thunk(0xC, [](Regs& r) {
            Record({0x0B1E, r.ecx, r.args[0], r.args[2]});
            // (the parameters as they are now: their components' names and values)
            auto* params = reinterpret_cast<Script::CStruct*>(r.args[1]);
            Record({Norm(r.args[1])});
            (void)params;
            return 1u;
        });
        g_trace.clear();
        uint32_t result = call(run == 0);
        delete s;
        traces[run] = g_trace;
        traces[run].push_back(0xEEEEEEEE);
        traces[run].push_back(Norm(result));
        // the singletons and the other globals the code may change
        for (uint32_t global : {0x007CE5D4u, 0x007CE5D8u, 0x007CE5DCu, 0x007CE5E0u, 0x007CE5E4u, 0x007CE5E8u,
                                0x0067F398u, 0x007CE644u})
            traces[run].push_back(GameVariable<uint32_t>(global));
        g_world.objectVtable[3] = nullptr;
        const auto* words = reinterpret_cast<const uint32_t*>(&g_world);
        traces[run].insert(traces[run].end(), words, words + sizeof(g_world) / 4);
    }
    Compare(traces[0], traces[1], what);
}

template <typename Fn, typename... A>
uint32_t Game(uint32_t address, A... args)
{
    using R = decltype(GameFunction<Fn>(address)(args...));
    if constexpr (std::is_void_v<R>) {
        GameFunction<Fn>(address)(args...);
        return 0;
    } else if constexpr (std::is_pointer_v<R>) {
        return reinterpret_cast<uint32_t>(GameFunction<Fn>(address)(args...));
    } else {
        return static_cast<uint32_t>(GameFunction<Fn>(address)(args...));
    }
}

CGapManager* Gaps() { return &g_world.gapManager; }
CGapManager::Gap* GapAt(int i) { return reinterpret_cast<CGapManager::Gap*>(g_world.gaps[i]); }

std::vector<Saved> ParkGlobals()
{
    return {{0x007CE604, 12, {}}, {0x007CE5D4, 12, {}}, {0x007CE478, 4, {}}, {0x007D1800, 4, {}},
            {0x007CC979, 1, {}},  {0x006F3614, 16, {}}, {0x006F3624, 4, {}}};
}

// --- the gap manager --------------------------------------------------------------------------------------------

void TestParkGapManager()
{
    std::vector<Saved> saved = ParkGlobals();
    SaveAll(saved);

    // UpdateChecklist: no checklist; the gaps added (slots 2 and 9, and a third).
    for (int c = 0; c < 3; ++c) {
        RunCase("park gaps: UpdateChecklist", [&] {
            if (c)
                g_script[0x00584110] = {U(g_world.checklist)};
            if (c == 2)
                Gaps()->mp_gaps[31] = GapAt(0);
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CGapManager*)>(0x0058CD40, Gaps()) : (Gaps()->UpdateChecklist(), 0u);
        });
    }
    // SetGapInfo: a gap's name and info replaced (the checklist made again).
    RunCase("park gaps: SetGapInfo", [&] { g_script[0x00584110] = {U(g_world.checklist)}; }, [&](bool original) {
        GapInfo info;
        std::memset(&info, 0x5A, sizeof(info));
        std::strcpy(info.m_text, "renamed gap");
        info.m_score = 777;
        return original ? Game<void(__thiscall*)(CGapManager*, int, GapInfo*)>(0x0058CDA0, Gaps(), 9, &info)
                        : (Gaps()->SetGapInfo(9, &info), 0u);
    });
    // RemoveGap: with / without forgetting (found or not in the checklist), a missing piece, a second slot.
    for (int c = 0; c < 5; ++c) {
        RunCase("park gaps: RemoveGap", [&] {
            if (c == 1)
                g_script[0x00578B30] = {5};
            if (c == 2)
                g_script[0x00578B30] = {static_cast<uint32_t>(-1)};
            if (c == 3)
                GapAt(0)->mp_pieces[0] = nullptr;
            if (c == 4)
                Gaps()->mp_gaps[30] = GapAt(0);
        }, [&](bool original) {
            bool forget = c == 1 || c == 2 || c == 4;
            return original ? Game<void(__thiscall*)(CGapManager*, int, bool)>(0x0058CDF0, Gaps(), 2, forget)
                            : (Gaps()->RemoveGap(2, forget), 0u);
        });
    }
    // RemoveAllGaps, Destroy
    RunCase("park gaps: RemoveAllGaps", [] {}, [&](bool original) {
        return original ? Game<void(__thiscall*)(CGapManager*)>(0x0058CE90, Gaps()) : (Gaps()->RemoveAllGaps(), 0u);
    });
    RunCase("park gaps: Destroy", [] { Gaps()->mp_gaps[0] = GapAt(1); Gaps()->mp_gaps[9] = nullptr; },
            [&](bool original) {
                return original ? Game<void(__thiscall*)(CGapManager*)>(0x0058CF20, Gaps()) : (Gaps()->Destroy(), 0u);
            });
    // SetTriggerLength: the first end, the second, a piece of no gap; a piece in two gaps (the first counts).
    for (int c = 0; c < 5; ++c) {
        RunCase("park gaps: SetTriggerLength", [&] {
            if (c == 4)
                GapAt(1)->mp_pieces[1] = GapAt(0)->mp_pieces[0];
            for (int i = 0; i < 2; ++i) {
                GapInfo& info = GapAt(i)->m_info;
                info.m_rot[0] = 1;
                info.m_rot[1] = 2;
                info.m_leftExtent[0] = 2 + i;
                info.m_leftExtent[1] = 1;
                info.m_rightExtent[0] = 3;
                info.m_rightExtent[1] = 4 + i;
                info.m_pos[0] = GridDims::Make(1, 0, 2, 3, 1, 5);
                info.m_pos[1] = GridDims::Make(4, 0, 6, 2, 1, 7);
            }
        }, [&](bool original) {
            CPiece* piece = reinterpret_cast<CPiece*>(c == 3 ? g_world.buffer : g_world.pieces[c & 3]);
            return original ? Game<void(__thiscall*)(CGapManager*, CPiece*)>(0x0058CEC0, Gaps(), piece)
                            : (Gaps()->SetTriggerLength(piece), 0u);
        });
    }
    // CopyFrom: into a manager with stale slots.
    RunCase("park gaps: CopyFrom", [] {
        auto* copy = reinterpret_cast<CGapManager*>(g_world.buffer);
        std::memset(copy, 0x77, sizeof(CGapManager));
    }, [&](bool original) {
        auto* copy = reinterpret_cast<CGapManager*>(g_world.buffer);
        return original ? Game<void(__thiscall*)(CGapManager*, CGapManager*)>(0x0058CF30, copy, Gaps())
                        : (copy->CopyFrom(Gaps()), 0u);
    });
    // StartGap
    RunCase("park gaps: StartGap", [] {}, [&](bool original) {
        void* piece = g_world.pieces[3];
        auto* meta = reinterpret_cast<CMetaPiece*>(g_world.buffer);
        return original ? Game<void(__thiscall*)(CGapManager*, void*, CMetaPiece*, int)>(0x0058D070, Gaps(), piece, meta, 5)
                        : (Gaps()->StartGap(piece, meta, 5), 0u);
    });
    // FireGap: started first (cancel flags) or not (the default flags: CANCEL_GROUND).
    for (int c = 0; c < 2; ++c) {
        RunCase("park gaps: FireGap", [&] { g_script[0x00478F60] = {0x9999}; }, [&](bool original) {
            int index = c ? 9 : 2;
            auto* script = reinterpret_cast<Script::CScript*>(g_world.script);
            return original ? Game<void(__thiscall*)(CGapManager*, int, Script::CScript*)>(0x0058D120, Gaps(), index, script)
                            : (Gaps()->FireGap(index, script), 0u);
        });
    }
    RestoreAll(saved);
}

} // namespace

SELFTEST(Early, TestParkGapManager, RemoveStandIns);
