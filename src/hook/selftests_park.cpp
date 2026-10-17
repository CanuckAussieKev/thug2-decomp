// Self-tests for the park editor (src/game/ed): the cursor and the park manager. Each function runs on a stand-in
// park (manager, cursor, metapieces, lists, clipboards) twice - the original, then ours - with every game function
// it calls stood in for by recording code (and scripted results), and the two runs' calls, results and memory
// are compared.

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
#include "game/core/pool.h"
#include "game/ed/park.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace Ed;

// Everything the code under test touches.
struct World {
    CParkManager manager;
    CCursor cursor;
    CMetaPiece metas[10];
    SMetaDescriptor extra[4];
    CMapListNode nodes[16];
    CClipboard clipboards[8];
    GapInfo gaps[2];
    uint8_t generator[0x40];
    uint8_t gapManager[0x10];
    uint8_t memManager[0x200];
    void* vtable[2];
    uint8_t pieces[6][16];
    uint8_t editor[0x80];
    uint8_t rails[0x10];
    alignas(4) uint8_t pool[sizeof(Mem::Pool)];
};
alignas(16) World g_world;

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

// Stand in for the functions at `at`: record the address, ECX and `n` stack arguments (those whose bit is set in
// `words` are GridDims halves: only their low 16 bits count), then return `result` (or the scripted value).
void Rec(StandIns& s, std::initializer_list<uint32_t> at, uint16_t pop, int n, uint32_t words = 0,
         std::function<uint32_t(Regs&)> result = nullptr, bool noEcx = false)
{
    uint32_t id = *at.begin();
    s.At(at, pop, [=](Regs& r) {
        Record({id, noEcx ? 0u : Norm(r.ecx)});
        for (int i = 0; i < n; ++i)
            Record({(words >> i) & 1 ? r.args[i] & 0xFFFF : Norm(r.args[i])});
        return result ? result(r) : Next(id);
    });
}

void StandInGame(StandIns& s)
{
    Rec(s, {0x005971E0}, 0, 0, 0, [](Regs& r) {
        // ClampToPark: keep x below 50 (so the call shows in the memory too)
        auto* cursor = reinterpret_cast<CCursor*>(r.ecx);
        if (cursor->m_cell_area.x > 50)
            cursor->m_cell_area.x = 50;
        return 0u;
    });
    Rec(s, {0x005954D0}, 0, 0);
    Rec(s, {0x00597110}, 4, 1, 0, [](Regs& r) {
        auto* out = reinterpret_cast<uint8_t*>(r.args[0]);
        for (int i = 0; i < 6; ++i)
            out[i] = static_cast<uint8_t>(Next(0x00597110 + 1));
        return 0u;
    });
    Rec(s, {0x00597190}, 0, 0);
    Rec(s, {0x00597740}, 0x14, 5, 2);
    Rec(s, {0x00597A00}, 8, 2, 2);
    Rec(s, {0x005959D0}, 0xC, 3, 2);
    Rec(s, {0x00597AC0}, 8, 2, 0, [](Regs& r) {
        auto* pos = reinterpret_cast<GridDims*>(r.args[0]);
        Record({pos->x, static_cast<uint32_t>(pos->y), pos->z});
        pos->w = pos->h = pos->l = 1;
        *reinterpret_cast<int*>(r.args[1]) = 7;
        return Next(0x00597AC0);
    });
    Rec(s, {0x00595A90}, 0xC, 3, 0, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* area = reinterpret_cast<GridDims*>(r.args[1]);
        auto* size = reinterpret_cast<float*>(r.args[2]);
        Record({HashBytes(reinterpret_cast<uint8_t*>(area), 6)});
        out[0] = (area->x - 29) * 120.0f + 0.3f;
        out[1] = area->y * 48.0f + 0.01f;
        out[2] = (area->z - 29) * 120.0f - 0.7f;
        out[3] = 1.0f;
        if (size) {
            size[0] = area->w * 120.0f + 0.1f;
            size[1] = area->h * 48.0f;
            size[2] = area->l * 120.0f + 0.2f;
        }
        return r.args[0];
    });
    Rec(s, {0x00595CB0}, 8, 2);
    Rec(s, {0x00595210}, 8, 2, 0, [](Regs& r) {
        *reinterpret_cast<int*>(r.args[1]) = 5;
        return U(&g_world.manager.m_sets[r.args[0] + 1]);
    });
    Rec(s, {0x00595670}, 8, 2);
    Rec(s, {0x00591A50}, 8, 2);
    Rec(s, {0x00591800}, 0x14, 5);
    Rec(s, {0x00590750}, 4, 1, 0, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<uint8_t*>(r.args[0]), 6)});
        return Next(0x00590750);
    });
    Rec(s, {0x0058BDF0}, 0, 0, 0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x0058C980}, 0, 0);
    Rec(s, {0x0058B7E0}, 0, 0);
    Rec(s, {0x0058C590}, 8, 2, 2);
    Rec(s, {0x0058BE30}, 4, 1);
    Rec(s, {0x0058BEB0}, 8, 2);
    Rec(s, {0x0058C130}, 0x18, 6);
    Rec(s, {0x0058C200}, 0, 0);
    Rec(s, {0x00593610}, 0, 0);
    Rec(s, {0x00592290}, 4, 1);
    Rec(s, {0x00591BE0}, 0, 0);
    Rec(s, {0x005955C0}, 0, 1, 0, nullptr, true);
    Rec(s, {0x00590160}, 0, 1, 0, [](Regs&) { return U(g_world.editor); }, true);
    Rec(s, {0x0058D6D0}, 8, 2);
    Rec(s, {0x0058D290}, 0, 0, 0, nullptr, true);
    Rec(s, {0x004F4970}, 0, 0, 0, [](Regs&) { return U(g_world.rails); }, true);
    Rec(s, {0x004F4690}, 0x10, 4);
    Rec(s, {0x004F6BF0}, 0, 0);
    Rec(s, {0x0059E180}, 0, 0, 0, [](Regs&) { return 0x4EA90000u; }, true);
    Rec(s, {0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 1);
    Rec(s, {0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0);
    Rec(s, {0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 0xC, 3);
    Rec(s, {0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, 0);
    Rec(s, {0x0058CBC0}, 0, 0, 0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00598940}, 4, 1);
    Rec(s, {0x0058CA10}, 8, 2, 0, [](Regs& r) {
        *reinterpret_cast<int*>(r.args[1]) = 3;
        return Next(0x0058CA10);
    });
    Rec(s, {0x0058CB20}, 8, 2, 0, [](Regs&) { return U(&g_world.gaps[1]); });
    Rec(s, {0x00597FC0}, 4, 1);
    Rec(s, {0x00598150}, 8, 2);
    Rec(s, {0x00595FC0}, 4, 1);
    Rec(s, {0x00595E80}, 8, 2);
    Rec(s, {0x005933F0}, 8, 2);
    Rec(s, {0x005935F0}, 4, 1);
    Rec(s, {0x0059E1E0}, 0, 1, 0, nullptr, true);
}

// Our functions that call each other, stood in for (with the original's) when the caller is under test.
struct Ours {
    uint32_t game;
    uint32_t ours;
    uint16_t pop;
    int n;
    uint32_t words;
};
std::vector<Ours> OurFunctions()
{
    return {
        {0x00598B60, AddressOf(&CCursor::Move), 8, 2, 0},
        {0x00599960, AddressOf(&CParkManager::HighlightMetasWithinArea), 0xC, 3, 2},
        {0x0059AE90, AddressOf(&CParkManager::DestroyConcreteMeta), 8, 2, 0},
        {0x00599880, AddressOf(&CParkManager::DestroyMetasInArea), 0xC, 3, 2},
        {0x0059A610, AddressOf(&CCursor::IsAreaSelectionInvalid), 0, 0, 0},
        {0x0059A5A0, AddressOf(&CCursor::ClearAreaSelection), 0, 0, 0},
        {0x0059BC00, AddressOf(&CCursor::ChangePiece), 4, 1, 0},
        {0x0059C7A0, AddressOf(&CCursor::Construct), 0, 0, 0},
    };
}

// A starting park: a few metapieces in lists, piece sets, a clipboard list.
void BasicWorld()
{
    World& w = g_world;
    CParkManager& m = w.manager;
    m.mp_generator = reinterpret_cast<CParkGenerator*>(w.generator);
    m.mp_gapManager = reinterpret_cast<CGapManager*>(w.gapManager);
    m.m_parkBounds = GridDims::Make(10, 0, 12, 30, 0, 28);
    m.m_numSets = 6;
    for (int i = 0; i < 8; ++i) {
        m.m_sets[i].m_visible = (i % 3) != 1;
        m.m_sets[i].m_selected = i % 3;
        for (int j = 0; j < 4; ++j)
            m.m_sets[i].m_entries[j].checksum = 0xC0DE0000u + i * 16 + j;
    }
    m.m_sets[4].m_isClipboard = true;
    m.m_flags = 0xFF;
    for (int i = 0; i < 8; ++i)
        m.m_restartPieces[i] = 0x7E570000u + i;
    w.vtable[0] = nullptr;
    w.vtable[1] = nullptr; // set to a recording thunk by the case that deletes
    for (int i = 0; i < 10; ++i) {
        CMetaPiece& meta = w.metas[i];
        meta.m_vtable = w.vtable;
        meta.m_cell_area = GridDims::Make(static_cast<uint8_t>(12 + i), static_cast<int8_t>(i - 2),
                                          static_cast<uint8_t>(20 - i), static_cast<uint8_t>(1 + i % 3), 2,
                                          static_cast<uint8_t>(1 + i % 2));
        meta.m_flags = 0x1004u + (i & 1) * 0x20u + (i % 3 == 0) * 0x1u;
        meta.m_name_checksum = 0xC0DE0000u + i;
        meta.m_num_used_entries = static_cast<uint32_t>(i % 5);
        meta.m_desc[0].piece = w.pieces[0];
        meta.m_desc[1].piece = w.pieces[1];
        meta.mp_extra_desc = w.extra;
    }
    for (int i = 0; i < 4; ++i)
        w.extra[i].piece = w.pieces[2 + i];
    // lists: concrete 0 -> 1 -> 2 -> 3, abstract 4 -> 5 -> 6 -> 7 -> 8, a third 9 -> 0 -> 2
    auto chain = [&](int first, std::initializer_list<int> metas) {
        int n = first;
        for (int meta : metas) {
            w.nodes[n].mp_meta = &w.metas[meta];
            w.nodes[n].mp_next = n + 1 < first + static_cast<int>(metas.size()) ? &w.nodes[n + 1] : nullptr;
            ++n;
        }
        return &w.nodes[first];
    };
    m.mp_concrete_metas = chain(0, {0, 1, 2, 3});
    m.mp_abstract_metas = chain(4, {4, 5, 6, 7, 8});
    chain(9, {9, 0, 2});
    m.m_numConcreteMetas = 4;
    for (int i = 0; i < 8; ++i) {
        w.clipboards[i].mp_next = i < 2 ? &w.clipboards[i + 1] : nullptr;
        w.clipboards[i].m_area = GridDims::Make(static_cast<uint8_t>(i), 0, 1, 2, 1, 3);
    }
    CCursor& c = w.cursor;
    c.mp_manager = &m;
    c.m_cell_area = GridDims::Make(30, 1, 20, 2, 1, 3);
    c.m_mode = CCursor::kModeRegular;
    c.mp_meta = &w.metas[3];
    c.m_target = {1.0f, 2.0f, 3.0f, 1.0f};
    c.m_pos = {-5.5f, 2.0f, 7.25f, 1.0f};
    c.m_moveTime = 0.0f;
    c.m_targetAngle = 90.0f;
    c.m_angle = 60.0f;
    c.m_rot = 1;
    c.mp_clipboards = &w.clipboards[0];
    c.mp_clipboard = &w.clipboards[1];
    c.m_areaStart = GridDims::Make(3, 0, 4, 0, 0, 0);
    c.m_areaLast = GridDims::Make(30, 0, 20, 0, 0, 0);
    c.m_gap.m_numPlaced = 0;
    c.m_gap.m_gapIndex = 2;
    w.gaps[0].m_numPlaced = 2;
    w.gaps[0].unknown00[0] = 0x66;
    w.gaps[1].m_numPlaced = 1;
    w.gaps[1].unknown00[5] = 0x77;
    GameVariable<CParkManager*>(0x007CE604) = &m;
    GameVariable<CCursor*>(0x007CE608) = &c;
    GameVariable<void*>(0x007D1800) = w.memManager;
    GameVariable<uint8_t>(0x007CC979) = 1;
}

using Setup = std::function<void()>;
using Call = std::function<uint32_t(bool original)>;

// Run `call` on the original and on ours from the same `setup`; compare calls, results and memory.
void RunCase(const char* what, Setup setup, Call call, std::initializer_list<uint32_t> standInOurs = {})
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        std::memset(&g_world, 0, sizeof(g_world));
        g_script.clear();
        BasicWorld();
        setup();
        auto* s = new StandIns;
        StandInGame(*s);
        for (const Ours& f : OurFunctions()) {
            for (uint32_t game : standInOurs) {
                if (game == f.game)
                    Rec(*s, {f.game, f.ours}, f.pop, f.n, f.words);
            }
        }
        // the metapieces' destructor (vtable slot 1)
        g_world.vtable[1] = s->Thunk(4, [](Regs& r) {
            Record({0xDE1E7E, r.ecx, r.args[0]});
            return 0u;
        });
        g_trace.clear();
        uint32_t result = call(run == 0);
        delete s;
        traces[run] = g_trace;
        traces[run].push_back(0xEEEEEEEE);
        traces[run].push_back(Norm(result));
        g_world.vtable[1] = nullptr;
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

CCursor* Cur() { return &g_world.cursor; }
CParkManager* Man() { return &g_world.manager; }

void TestParkCursor()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x007CE5B4, 16, {}}};
    SaveAll(saved);
    World& w = g_world;

    // HighlightIntersectingMetas: paste (with / without a clipboard), regular (a list / none).
    for (int c = 0; c < 4; ++c) {
        RunCase("park cursor: HighlightIntersectingMetas", [&] {
            if (c < 2)
                w.cursor.m_mode = CCursor::kModePaste;
            if (c == 1)
                w.cursor.mp_clipboard = nullptr;
            if (c == 2)
                g_script[0x00597740] = {U(&w.nodes[9])};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, int)>(0x00598A60, Cur(), 0x101)
                            : (Cur()->HighlightIntersectingMetas(0x101), 0u);
        });
    }
    // IsOverGap
    for (int c = 0; c < 2; ++c) {
        RunCase("park cursor: IsOverGap", [&] {
            if (c)
                g_script[0x00597AC0] = {U(&w.gaps[0])};
        }, [&](bool original) {
            return original ? Game<bool(__thiscall*)(CCursor*)>(0x00598B40, Cur()) : Cur()->IsOverGap();
        });
    }
    // Move: paste (clipboard or none), regular (either cell higher), the gap modes.
    struct MoveCase {
        int mode;
        bool clipboard;
        std::vector<uint32_t> heights;
        bool list;
        bool gap;
        int placed;
        int dx, dz;
    };
    std::vector<MoveCase> moves = {
        {CCursor::kModePaste, true, {}, false, false, 0, 1, 0},
        {CCursor::kModePaste, false, {}, false, false, 0, 0, -1},
        {CCursor::kModeRegular, false, {3, 5}, false, false, 0, 30, 2},
        {CCursor::kModeRegular, false, {7, 2}, false, false, 0, -1, 0},
        {CCursor::kModeRegular, false, {static_cast<uint32_t>(-4), static_cast<uint32_t>(-4)}, false, false, 0, 0, 0},
        {CCursor::kModeGap, false, {1, 0}, false, false, 0, 1, 1},
        {CCursor::kModeGap, false, {1, 0}, true, false, 0, 1, 1},
        {CCursor::kModeGap, false, {1, 0}, true, true, 0, 1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, true, true, 1, 1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, true, true, 2, -1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, true, false, 3, -1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, false, false, 0, -1, 1},
    };
    for (const MoveCase& mc : moves) {
        RunCase("park cursor: Move", [&] {
            w.cursor.m_mode = mc.mode;
            if (!mc.clipboard)
                w.cursor.mp_clipboard = nullptr;
            g_script[0x0058BE30] = {static_cast<uint32_t>(-3)};
            g_script[0x005959D0] = std::deque<uint32_t>(mc.heights.begin(), mc.heights.end());
            if (mc.list)
                g_script[0x00597740] = {U(&w.nodes[9])};
            if (mc.gap)
                g_script[0x00597AC0] = {U(&w.gaps[0])};
            w.cursor.m_gap.m_numPlaced = mc.placed;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, int, int)>(0x00598B60, Cur(), mc.dx, mc.dz)
                            : (Cur()->Move(mc.dx, mc.dz), 0u);
        });
    }

    // Update: turning (wrapping either way), the stick, the slide, the angle, what's shown.
    struct UpdateCase {
        float x, z;
        int rot;
        float time, target, angle;
        int mode;
        bool clipboard;
        int startRot = 1;
    };
    std::vector<UpdateCase> updates = {
        {0.0f, 0.0f, 0, 0.0f, 90.0f, 60.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 1, 0.0f, 300.1f, 290.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 1, 0.0f, 200.3f, 290.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, -1, 0.0f, 10.7f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, -1, 0.0f, 100.0f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 3, 0.0f, 270.0f, 270.0f, CCursor::kModeRegular, true},
        {0.0001f, 0.0f, 0, 0.0f, 90.0f, 80.0f, CCursor::kModeRegular, true},
        {0.9f, 0.1f, 0, 0.0f, 90.0f, 80.0f, CCursor::kModeRegular, true},
        {-0.9f, 0.1f, 0, 0.0f, 90.0f, 80.0f, CCursor::kModeRegular, true},
        {0.1f, 0.9f, 0, 0.0f, 90.0f, 100.0f, CCursor::kModeRegular, true},
        {0.1f, -0.9f, 0, 0.0f, 90.0f, 100.0f, CCursor::kModeRegular, true},
        {0.5f, 0.5f, 0, 0.0f, 90.0f, 89.0f, CCursor::kModeRegular, true},
        {-0.5f, -0.6f, 0, 0.0f, 90.0f, 91.0f, CCursor::kModeRegular, true},
        {0.5f, -0.5f, 0, 0.3f, 90.0f, 91.0f, CCursor::kModeRegular, true},
        {0.9f, 0.2f, 0, 0.7f, 0.0f, 359.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 0.1f, 33.0f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 0.15f, 33.0f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 1.0f, 33.0f, 20.0f, CCursor::kModePaste, true},
        {0.0f, 0.0f, 0, 1.0f, 33.0f, 20.0f, CCursor::kModePaste, false},
        {0.0f, 0.0f, 0, 1.0f, 33.0f, 20.0f, CCursor::kModeRailPlacement, true},
        {0.3f, 0.0f, 1, 0.0f, 33.0f, 20.0f, CCursor::kModeGap, true},
        {0.0f, 0.0f, -1, 0.0f, 90.0f, 20.0f, CCursor::kModeRegular, true, 0},
        {0.0f, 0.0f, -1, 0.0f, 190.0f, 20.0f, CCursor::kModeRegular, true, 2},
        {0.0f, 0.0f, 0, 0.0f, 90.0f, 75.5f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 0.0f, 90.0f, 104.5f, CCursor::kModeRegular, true},
    };
    for (const UpdateCase& uc : updates) {
        RunCase("park cursor: Update", [&] {
            w.cursor.m_moveTime = uc.time;
            w.cursor.m_targetAngle = uc.target;
            w.cursor.m_angle = uc.angle;
            w.cursor.m_mode = uc.mode;
            w.cursor.m_rot = uc.startRot;
            w.cursor.m_clipboardHeight = 4;
            w.cursor.m_pos = {13.1f, -2.3f, 5.77f, 0.5f};
            if (!uc.clipboard)
                w.cursor.mp_clipboard = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, float, float, int)>(0x0059A1F0, Cur(), uc.x, uc.z, uc.rot)
                            : (Cur()->Update(uc.x, uc.z, uc.rot), 0u);
        }, {0x00598B60});
    }

    // ClearAreaSelection; IsAreaSelectionInvalid (too wide, too long, the editor's answer)
    RunCase("park cursor: ClearAreaSelection", [&] {
        g_script[0x00597110 + 1] = {1, 2, 3, 4, 5, 6};
        w.cursor.m_areaSelect = 1;
        w.cursor.m_areaHighlighted = true;
    }, [&](bool original) {
        return original ? Game<void(__thiscall*)(CCursor*)>(0x0059A5A0, Cur()) : (Cur()->ClearAreaSelection(), 0u);
    }, {0x00599960});
    for (int c = 0; c < 6; ++c) {
        RunCase("park cursor: IsAreaSelectionInvalid", [&] {
            uint32_t wide = c == 0 ? 11 : 10, longer = c == 1 ? 11 : (c == 2 ? 3 : 10);
            g_script[0x00597110 + 1] = {1, 2, 3, wide, 5, longer};
            g_script[0x0058D6D0] = {c >= 4 ? 1u : 0u};
        }, [&](bool original) {
            return original ? Game<bool(__thiscall*)(CCursor*)>(0x0059A610, Cur()) : Cur()->IsAreaSelectionInvalid();
        });
    }

    // CopyAreaToClipboard: invalid; too many rails; 6 clipboards; copies failing until the list is gone; empty;
    // from the heap (no pool).
    struct CopyCase {
        uint32_t invalid;
        uint32_t rails, free;
        int clipboards;
        std::vector<uint32_t> copies;
        uint32_t empty;
        bool pool;
    };
    std::vector<CopyCase> copies = {
        {1, 0, 0, 3, {1}, 0, true},
        {0, 9, 8, 3, {1}, 0, true},
        {0, 8, 8, 3, {1}, 0, true},
        {0, 2, 8, 6, {1}, 0, true},
        {0, 2, 8, 3, {0, 0, 1}, 0, true},
        {0, 2, 8, 2, {0, 0, 0}, 1, true},
        {0, 2, 8, 0, {0}, 0, true},
        {0, 2, 8, 1, {1}, 1, false},
    };
    for (const CopyCase& cc : copies) {
        RunCase("park cursor: CopyAreaToClipboard", [&] {
            g_script[0x0059A610] = {cc.invalid};
            g_script[0x00597110 + 1] = {2, 0, 3, 4, 1, 5};
            g_script[0x004F4690] = {cc.rails};
            g_script[0x004F6BF0] = {cc.free};
            for (int i = 0; i < 8; ++i)
                w.clipboards[i].mp_next = i + 1 < cc.clipboards ? &w.clipboards[i + 1] : nullptr;
            w.cursor.mp_clipboards = cc.clipboards ? &w.clipboards[0] : nullptr;
            g_script[0x0058C590] = std::deque<uint32_t>(cc.copies.begin(), cc.copies.end());
            g_script[0x0058B7E0] = {cc.empty};
            GameVariable<Mem::Pool*>(0x007CE5B4 + 8) = cc.pool ? reinterpret_cast<Mem::Pool*>(w.pool) : nullptr;
            GameVariable<int>(0x007CE5B4) = 0;
            g_script[0x005BA3D0] = {U(&w.clipboards[7])};
            g_script[0x005BAFC0] = {U(&w.clipboards[6])};
        }, [&](bool original) {
            return original ? Game<bool(__thiscall*)(CCursor*)>(0x0059A6D0, Cur()) : Cur()->CopyAreaToClipboard();
        }, {0x0059A610});
    }

    // UpdateAreaSelectionHighlight: no selection, not yet highlighted, moved in x / in z, not moved.
    for (int c = 0; c < 5; ++c) {
        RunCase("park cursor: UpdateAreaSelectionHighlight", [&] {
            g_script[0x00597110 + 1] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
            w.cursor.m_areaSelect = c == 0 ? 2 : 1;
            w.cursor.m_areaHighlighted = c > 1;
            if (c == 2)
                w.cursor.m_areaLast.x = 7;
            if (c == 3)
                w.cursor.m_areaLast.z = 7;
            w.cursor.m_areaLast.y = 9;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*)>(0x0059A8D0, Cur())
                            : (Cur()->UpdateAreaSelectionHighlight(), 0u);
        }, {0x00599960});
    }

    // DestroyCursorMeta: a piece / none
    for (int c = 0; c < 2; ++c) {
        RunCase("park cursor: DestroyCursorMeta", [&] {
            w.cursor.mp_meta->m_flags |= CMetaPiece::kCursor | 0x40000;
            if (c)
                w.cursor.mp_meta = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*)>(0x0059BAC0, Cur()) : (Cur()->DestroyCursorMeta(), 0u);
        }, {0x0059AE90});
    }

    // SwitchToClipboard: no clipboards; to paste mode; already pasting (with a piece / without)
    for (int c = 0; c < 4; ++c) {
        RunCase("park cursor: SwitchToClipboard", [&] {
            if (c == 0)
                w.cursor.mp_clipboards = nullptr;
            if (c >= 2)
                w.cursor.m_mode = CCursor::kModePaste;
            if (c == 3)
                w.cursor.mp_meta = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*)>(0x0059BAF0, Cur()) : (Cur()->SwitchToClipboard(), 0u);
        }, {0x0059AE90});
    }

    // ChangePiece: each kind of piece, the mode already set or not, turned or not
    struct PieceCase {
        uint32_t flags;
        int mode;
        int rot;
    };
    std::vector<PieceCase> pieces = {
        {0x10000, CCursor::kModeRegular, 0}, {0x10000, CCursor::kModeRailPlacement, 1},
        {0x800, CCursor::kModeRegular, 0},   {0x800, CCursor::kModeGap, 1},
        {0x8000, CCursor::kModeRegular, 3},  {0x8000, CCursor::kModeAreaSelect, 2},
        {0x0, CCursor::kModeRegular, 1},     {0x4, CCursor::kModeGap, 0},
        {0x8800, CCursor::kModeNone, 0},
    };
    for (const PieceCase& pc : pieces) {
        RunCase("park cursor: ChangePiece", [&] {
            w.metas[6].m_flags = pc.flags;
            w.cursor.m_mode = pc.mode;
            w.cursor.m_rot = pc.rot;
            g_script[0x00595670] = {U(&w.metas[9])};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, uint32_t)>(0x0059BC00, Cur(), w.metas[6].m_name_checksum)
                            : (Cur()->ChangePiece(w.metas[6].m_name_checksum), 0u);
        }, {0x0059AE90, 0x0059A5A0});
    }

    // Construct; CreateInstance (made / already there / no memory)
    RunCase("park cursor: Construct", [&] { std::memset(&w.cursor, 0xCD, sizeof(w.cursor)); }, [&](bool original) {
        return original ? Game<CCursor*(__thiscall*)(CCursor*)>(0x0059C7A0, Cur()) : U(Cur()->Construct());
    }, {0x0059BC00});
    for (int c = 0; c < 3; ++c) {
        RunCase("park cursor: CreateInstance", [&] {
            if (c != 1)
                GameVariable<CCursor*>(0x007CE608) = nullptr;
            if (c == 0)
                g_script[0x005BAFC0] = {U(&w.cursor)};
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x0059C860) : (CCursor::CreateInstance(), 0u);
        }, {0x0059C7A0});
    }
    RestoreAll(saved);
}

void TestParkManagerMetas()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}};
    SaveAll(saved);
    World& w = g_world;

    // DestroyMetasInArea: no list; flags 0 / 4 (kFlag20 ones kept) / 2
    for (uint32_t flags : {0u, 4u, 2u, 7u, 0x10003u}) {
        for (int list = 0; list < 2; ++list) {
            RunCase("park manager: DestroyMetasInArea", [&] {
                if (list)
                    g_script[0x00597740] = {U(&w.nodes[0])};
                w.metas[2].m_num_used_entries = 0xFFFF;
            }, [&](bool original) {
                GridDims area = GridDims::Make(4, -2, 9, 3, 7, 5);
                return original ? Game<uint32_t(__thiscall*)(CParkManager*, GridDims, uint32_t)>(0x00599880, Man(), area, flags)
                                : Man()->DestroyMetasInArea(area, flags);
            }, {0x0059AE90});
        }
    }
    // HighlightMetasWithinArea: a null metapiece, without flag 1, outside, inside
    RunCase("park manager: HighlightMetasWithinArea", [&] {
        g_script[0x00597A00] = {U(&w.nodes[0])};
        w.nodes[1].mp_meta = nullptr;
        w.metas[0].m_flags |= 1;
        w.metas[2].m_flags &= ~1u;
        w.metas[3].m_flags |= 1;
        g_script[0x00590750] = {0, 1};
    }, [&](bool original) {
        GridDims area = GridDims::Make(4, -2, 9, 3, 7, 5);
        return original ? Game<void(__thiscall*)(CParkManager*, GridDims, int)>(0x00599960, Man(), area, 0x201)
                        : (Man()->HighlightMetasWithinArea(area, 0x201), 0u);
    });

    // DestroyConcreteMeta: the cursor's piece; no cursor; a gap; each flag combination for the map; standing
    // pieces (flags 1, not inner shell); a restart piece; descriptors past the inline two.
    struct DestroyCase {
        uint32_t metaFlags;
        uint32_t flags;
        bool cursor;
        uint32_t gap;
        int restart;
        uint32_t entries;
    };
    std::vector<DestroyCase> destroys = {
        {0x200, 1, true, 1, 0, 3},     {0x4, 0, true, 0, -1, 0},      {0x4, 1, false, 1, 3, 1},
        {0x104, 1, true, 0, 7, 2},     {0xC, 1, true, 0, -1, 5},      {0x0, 0x11, true, 1, 0, 4},
        {0x8, 0, true, 0, -1, 6},      {0x20005, 0xFF, true, 0, -1, 2},
    };
    for (const DestroyCase& dc : destroys) {
        RunCase("park manager: DestroyConcreteMeta", [&] {
            CMetaPiece& meta = w.metas[5];
            meta.m_flags = dc.metaFlags;
            meta.m_num_used_entries = dc.entries;
            if (dc.restart >= 0)
                w.manager.m_restartPieces[dc.restart] = meta.m_name_checksum;
            if (!dc.cursor)
                GameVariable<CCursor*>(0x007CE608) = nullptr;
            g_script[0x0058CA10] = {dc.gap};
            w.manager.m_numConcreteMetas = 9;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*, CMetaPiece*, uint32_t)>(0x0059AE90, Man(), &w.metas[5], dc.flags)
                            : (Man()->DestroyConcreteMeta(&w.metas[5], dc.flags), 0u);
        }, {0x00599880});
    }
    RestoreAll(saved);
}

} // namespace

SELFTEST(Early, TestParkCursor, RemoveStandIns);
SELFTEST(Early, TestParkManagerMetas, RemoveStandIns);
