// Self-tests for the time-of-day manager (src/game/gfx/tod_manager.cpp): the original and ours run the same calls on
// identical stand-in managers, parameter structures, scenes, sectors and models in an arena. Everything they call
// outside the manager (the structure and array functions, the memory manager, the scripts, the node prefixes, the
// scenes' and models' colours, the light manager, fog, bloom, the sun, the timer, the game mode) is stood in for by
// recording code written over the game's function and ours alike, so the two are compared call by call, then the
// whole arena word by word.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/tod_manager.h"
#include "game/lst/hashtable.h"
#include "game/obj/compositemgr.h"
#include "game/script/cscript.h"
#include "game/script/node_prefixes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Gfx::CTODManager;
using Gfx::TODParams;
using Gfx::TODSystem;
using Script::CArray;
using Script::CStruct;

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

constexpr uint32_t kArenaSize = 0x10000;
ArenaPair<kArenaSize> g_tod_storage;
uint32_t g_tod_base;
uint8_t* Arena() { return reinterpret_cast<uint8_t*>(g_tod_base); }
uint32_t At(uint32_t offset) { return g_tod_base + offset; }
template <typename T>
T* Ptr(uint32_t offset)
{
    return reinterpret_cast<T*>(g_tod_base + offset);
}
// (both arenas, the same for both runs: a value that only happens to fall inside one of them maps the same way in
// each - ArenaWord, selftest.h)
bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_tod_storage.Get()) <= 2 * kArenaSize; }
uint32_t Rel(uint32_t word) { return ArenaWord(g_tod_storage.Get(), kArenaSize, word); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}

// Arena layout.
constexpr uint32_t kManager = 0x0000;  // the manager (0x340)
constexpr uint32_t kStructs = 0x0400;  // stand-in structures, 0x10 apart (64)
constexpr uint32_t kArrays = 0x0800;   // stand-in arrays, 0x40 apart: the CArray, elements at +0x10 (16)
constexpr uint32_t kSymbol = 0x0C00;   // a stand-in symbol (todmanager_temp_tod_changed)
constexpr uint32_t kNodes = 0x0C40;    // prefixed nodes (8)
constexpr uint32_t kScript = 0x0C80;   // a stand-in script (its parameters at +0x14)
constexpr uint32_t kScenes = 0x0D00;   // scenes, 0x100 apart (4): vtable +0, sky +0x8D, sector table +0xD0
constexpr uint32_t kTables = 0x1100;   // the scenes' sector tables, 0x60 apart: the table, 4 buckets at +0x20
constexpr uint32_t kSectors = 0x1300;  // sectors, 0x20 apart (16): group +0x0C, geometry +0x14
constexpr uint32_t kGeoms = 0x1500;    // the sectors' geometry, 8 apart (16): vtable +0
constexpr uint32_t kSceneNodes = 0x1600; // the scene list, 0x10 apart
constexpr uint32_t kModels = 0x1700;   // model components, 0x20 apart (6): object +0x0C, next +0x14
constexpr uint32_t kObjects = 0x1800;  // their objects, 0x20 apart: id +0x14
constexpr uint32_t kNamed = 0x1900;    // sectors found by name, 0x20 apart: group +0x0C
constexpr uint32_t kManagerObj = 0x1A00; // the memory manager / composite manager stand-ins
constexpr uint32_t kHeap = 0x2000;     // the stand-in allocator's memory

uint32_t g_tod_heapNext;
// Structure answers: (kind, structure offset, name) -> words ('S' / 'A': arena offsets).
std::map<std::tuple<char, uint32_t, uint32_t>, std::vector<uint32_t>> g_tod_answers;
bool g_tod_multiplayer;
uint32_t g_tod_gameMode;
int g_tod_inactive;
double g_tod_frameSeconds;
uint32_t g_tod_firstNode;   // arena offsets (0: none)
uint32_t g_tod_skyScene;
uint32_t g_tod_levelScene;
uint32_t g_tod_firstModel;
uint32_t g_tod_prefixCount;
std::map<uint32_t, uint32_t> g_tod_namedSectors; // id -> arena offset
void* g_tod_sceneVtable[16];
void* g_tod_geomVtable[16];
std::vector<Saved> g_tod_saved;
std::vector<uint32_t> g_tod_calls[2]; // each run's calls
// SetSceneColor's colours have an unset fourth byte in the original (stack garbage): compared without it.
bool g_tod_maskAlpha;
uint32_t Colour(uint32_t c) { return g_tod_maskAlpha ? c & 0xFFFFFF : c; }

void Answer(char kind, uint32_t structure, uint32_t name, std::vector<uint32_t> value)
{
    g_tod_answers[{kind, structure, name}] = std::move(value);
}
void AnswerFloat(uint32_t structure, uint32_t name, float value) { Answer('F', structure, name, {Bits(value)}); }
uint32_t StructAt(int n) { return kStructs + 0x10 * n; }
uint32_t ArrayAt(int n) { return kArrays + 0x40 * n; }
// A stand-in array of `values` (pointers given as arena offsets with `pointers`).
void MakeArray(int n, std::vector<uint32_t> values, bool pointers)
{
    auto* array = Ptr<CArray>(ArrayAt(n));
    array->m_unknown0 = 0;
    array->m_type = pointers ? Script::ESYMBOLTYPE_STRUCTURE : Script::ESYMBOLTYPE_INTEGER;
    array->m_size = static_cast<uint32_t>(values.size());
    auto* elements = Ptr<uint32_t>(ArrayAt(n) + 0x10);
    for (size_t i = 0; i < values.size(); ++i)
        elements[i] = pointers ? At(values[i]) : values[i];
}

void __fastcall StandInSceneColor(void* scene, void*, uint32_t color) { Rec(0x5C01, {reinterpret_cast<uint32_t>(scene), Colour(color)}); }
void __fastcall StandInGeomColor(void* geom, void*, uint32_t color) { Rec(0x6E01, {reinterpret_cast<uint32_t>(geom), Colour(color)}); }
double __cdecl StandInFrameSeconds()
{
    Rec(0xEAA0, {});
    return g_tod_frameSeconds;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_tod_saved);
    g_tod_saved.clear();
}

void InstallStandIns(StandIns& s)
{
    using Script::VectorValue;
    // The stand-in scenes' and sectors' vtables (their colour slots).
    g_tod_sceneVtable[8] = reinterpret_cast<void*>(&StandInSceneColor);
    g_tod_geomVtable[6] = reinterpret_cast<void*>(&StandInGeomColor);
    // The structure getters.
    auto getter = [&](std::initializer_list<uint32_t> addresses, char kind, uint32_t tag) {
        s.At(addresses, 12, [kind, tag](Regs& r) {
            uint32_t name = r.args[0];
            Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
            auto it = g_tod_answers.find({kind, r.ecx - g_tod_base, name});
            if (it == g_tod_answers.end())
                return 0u;
            std::vector<uint32_t> value = it->second;
            if (kind == 'S' || kind == 'A')
                value[0] = At(value[0]);
            std::memcpy(reinterpret_cast<void*>(r.args[1]), value.data(), value.size() * 4);
            return 1u;
        });
    };
    getter({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 'I',
           0x6630);
    getter({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 'F',
           0x66D0);
    getter({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
           'K', 0x6950);
    getter({0x00476A30, AddressOf(&CStruct::GetChecksumOrStringChecksum)}, 'C', 0x6A30);
    getter({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
           'S', 0x67C0);
    getter({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray))}, 'A',
           0x6860);
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        return g_tod_answers.count({'L', r.ecx - g_tod_base, r.args[0]}) ? 1u : 0u;
    });
    // The adders (recorded; the stand-in structures stay empty).
    auto adder = [&](std::initializer_list<uint32_t> addresses, uint32_t tag) {
        s.At(addresses, 8, [tag](Regs& r) {
            // (fog_on of blended parameters is stack garbage in the original: its name only)
            Rec(tag, {r.ecx, r.args[0], r.args[0] == 0x028CCF87u ? 0u : r.args[1]});
            return 0u;
        });
    };
    adder({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 0x7B80);
    adder({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 0x7C60);
    adder({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 0x7D40);
    adder({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 0x8670);
    adder({0x004781B0, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructurePointer))},
          0x81B0);
    // (AddArray records the array's elements too)
    s.At({0x00478B00, AddressOf(static_cast<void (CStruct::*)(uint32_t, CArray*)>(&CStruct::AddArray))}, 8,
         [](Regs& r) {
             auto* array = reinterpret_cast<uint32_t*>(r.args[1]);
             Rec(0x8B00, {r.ecx, r.args[0], r.args[1], array[0], array[1], array[2]});
             return 0u;
         });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x7090, {r.ecx});
        return 0u;
    });
    // The arrays: the stand-in ones (elements at +0x10), and new ones (recorded, three elements in place).
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0]];
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        reinterpret_cast<uint32_t*>(r.ecx)[r.args[0] % 3] = r.args[1]; // (kept in the CArray itself)
        return 0u;
    });
    s.At({0x0046CAB0, AddressOf(&CArray::Clear)}, 0, [](Regs& r) {
        Rec(0xCAB0, {r.ecx});
        return 0u;
    });
    s.At({0x00477140, AddressOf(&Script::CleanUpArray)}, 0, [](Regs& r) {
        Rec(0x7140, {r.args[0]});
        return 0u;
    });
    // The script globals and scripts.
    s.At({0x00478CC0, AddressOf(&Script::FindSymbol)}, 0, [](Regs& r) {
        Rec(0x8CC0, {r.args[0]});
        return At(kSymbol);
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x8F60, {r.args[0]});
        return static_cast<uint32_t>(g_tod_inactive);
    });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                 bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                          r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             return 0u;
         });
    s.At({0x0045BF90, AddressOf(static_cast<uint32_t* (*)(uint32_t, uint16_t*)>(&SkateScript::GetPrefixedNodes))}, 0,
         [](Regs& r) {
             Rec(0xBF90, {r.args[0]});
             *reinterpret_cast<uint16_t*>(r.args[1]) = static_cast<uint16_t>(g_tod_prefixCount);
             return At(kNodes);
         });
    s.At({0x005A32A0}, 0, [](Regs& r) {
        Rec(0x32A0, {r.args[0]});
        return 0u;
    });
    // The memory manager: a bump allocator in the arena.
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1]});
        uint32_t block = At(g_tod_heapNext);
        g_tod_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.args[0]});
        return 0u;
    });
    // The engine.
    auto cdecl1 = [&](uint32_t address, uint32_t tag, int args) {
        s.At({address}, 0, [tag, args](Regs& r) {
            g_trace.push_back(tag);
            for (int i = 0; i < args; ++i)
                g_trace.push_back(Rel(r.args[i]));
            return 0u;
        });
    };
    cdecl1(0x00490DE0, 0x0DE0, 1); // fog on / off
    cdecl1(0x00490E20, 0x0E20, 1); // fog colour
    cdecl1(0x00490E00, 0x0E00, 1); // fog distance
    cdecl1(0x0048E8F0, 0xE8F0, 1); // ambient colour
    cdecl1(0x0048E930, 0xE930, 2); // light colour
    cdecl1(0x0048E970, 0xE970, 2); // ambient modulation
    cdecl1(0x0048E990, 0xE990, 3); // light modulation
    s.At({0x0048ED70}, 0, [](Regs& r) {
        auto* v = reinterpret_cast<uint32_t*>(r.args[1]);
        Rec(0xED70, {r.args[0], v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x004426D0}, 0, [](Regs& r) {
        auto* v = reinterpret_cast<uint32_t*>(r.args[0]);
        Rec(0x26D0, {r.args[1], v[0], v[1], v[6], v[7], v[8], v[9], v[16] & 0xFF});
        return 0u;
    });
    auto vector = [&](uint32_t address, uint32_t tag, uint16_t pop) {
        s.At({address}, pop, [tag, pop](Regs& r) {
            auto* v = reinterpret_cast<uint32_t*>(r.args[0]);
            Rec(tag, {pop ? r.ecx : 0u, v[0], v[1], v[2], v[3]}); // (ECX only for the methods)
            return 0u;
        });
    };
    vector(0x004966B0, 0x66B0, 4);
    vector(0x0049AE80, 0xAE80, 4);
    vector(0x00491290, 0x1290, 0);
    s.At({0x0049E360}, 8, [](Regs& r) {
        Rec(0xE360, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005651F0}, 0, [](Regs&) {
        Rec(0x51F0, {});
        return g_tod_multiplayer ? 1u : 0u;
    });
    s.At({0x005532A0}, 0, [](Regs& r) {
        Rec(0x32A1, {r.ecx});
        return g_tod_gameMode;
    });
    s.At({0x0048C7F0}, 0, [](Regs&) {
        Rec(0xC7F0, {});
        return g_tod_firstNode ? At(g_tod_firstNode) : 0u;
    });
    s.At({0x0048C7B0}, 0, [](Regs&) {
        Rec(0xC7B0, {});
        return g_tod_skyScene ? At(g_tod_skyScene) : 0u;
    });
    s.At({0x0058D710}, 0, [](Regs&) {
        Rec(0xD710, {});
        return 0x1E5E1u;
    });
    s.At({0x0048C700}, 0, [](Regs& r) {
        Rec(0xC700, {r.args[0]});
        return g_tod_levelScene ? At(g_tod_levelScene) : 0u;
    });
    s.At({0x0048C510}, 0, [](Regs& r) {
        Rec(0xC510, {r.args[0]});
        auto it = g_tod_namedSectors.find(r.args[0]);
        return it == g_tod_namedSectors.end() ? 0u : At(it->second);
    });
    s.At({0x00420700}, 4, [](Regs& r) {
        Rec(0x0700, {r.ecx, Colour(r.args[0])});
        return 0u;
    });
    s.At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Rec(0xF2D0, {r.ecx, r.args[0]});
        return g_tod_firstModel ? At(g_tod_firstModel) : 0u;
    });
    s.Bytes(0x005BEAA0, Branch(0xE9, 0x005BEAA0, reinterpret_cast<void*>(&StandInFrameSeconds)));
    uint32_t ours = AddressOf(&Tmr::GetSlomoFrameSeconds);
    s.Bytes(ours, Branch(0xE9, ours, reinterpret_cast<void*>(&StandInFrameSeconds)));
}

// The globals the code touches: saved, set for the test, put back after.
void SaveGlobals()
{
    g_tod_saved = {{0x007CE490, 8, {}},  {0x007CE464, 4, {}}, {0x007CE470, 4, {}}, {0x007D1800, 4, {}},
                   {0x007CC979, 1, {}},  {0x006F0FB8, 4, {}}, {0x006FA7C0, 8, {}}, {0x006FA7CC, 4, {}},
                   {0x006F3614, 16, {}}, {0x006F22B4, 16, {}}};
    SaveAll(g_tod_saved);
}

// A run's arena: everything zeroed, then the stand-in world (the same in both runs, relative to the arena).
void SetUpArena(uint8_t* arena)
{
    g_tod_base = reinterpret_cast<uint32_t>(arena);
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + kHeap, 0xCD, kArenaSize - kHeap);
    g_tod_heapNext = kHeap;
    g_trace.clear();
    GameVariable<uint8_t*>(0x007CE464) = Ptr<uint8_t>(kManagerObj + 0x40); // the front end (+0x30 paused)
    GameVariable<void*>(0x007CE470) = Ptr<void>(kManagerObj + 0x80);       // the game mode
    GameVariable<void*>(0x007D1800) = Ptr<void>(kManagerObj);               // the memory manager
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<void*>(0x006F0FB8) = Ptr<void>(kManagerObj + 0xC0);        // the composite object manager
    GameVariable<void*>(0x006FA7C0) = Ptr<void>(kManagerObj + 0x100);
    GameVariable<void*>(0x006FA7C4) = Ptr<void>(kManagerObj + 0x140);
    GameVariable<void*>(0x006FA7CC) = Ptr<void>(kManagerObj + 0x180);
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16);
    GameVariable<void*>(0x007CE490) = nullptr;
    GameVariable<uint32_t>(0x007CE494) = 0;
    g_tod_answers.clear();
    g_tod_namedSectors.clear();
    g_tod_multiplayer = false;
    g_tod_gameMode = 0x4DA4937B; // career
    g_tod_inactive = 0;
    g_tod_frameSeconds = 1.0 / 60.0;
    g_tod_firstNode = g_tod_skyScene = g_tod_levelScene = g_tod_firstModel = 0;
    g_tod_prefixCount = 0;
}

// The arena as compared: pointers made arena-relative.
void PushArena(std::vector<uint32_t>& out)
{
    const auto* words = reinterpret_cast<const uint32_t*>(Arena());
    for (uint32_t i = 0; i < kArenaSize / 4; ++i)
        out.push_back(Rel(words[i]));
}

// Where two runs' calls part: the last mark (0xFEEDxxxx) before the first difference.
void LogLastMark(const char* what)
{
    const std::vector<uint32_t>& a = g_tod_calls[0];
    const std::vector<uint32_t>& b = g_tod_calls[1];
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i])
        ++i;
    if (i == a.size() && i == b.size())
        return;
    uint32_t last = 0;
    for (size_t j = 0; j < i; ++j)
        if ((a[j] & 0xFFFF0000u) == 0xFEED0000u)
            last = a[j];
    Log("  %s: the calls part at %u, after mark %08X", what, static_cast<unsigned>(i), last);
}

// --- TODParams and the manager's fields ----------------------------------------------------------------------

// Varied parameters (n picks a set).
TODParams MakeParams(int n)
{
    TODParams p;
    auto* words = reinterpret_cast<uint32_t*>(&p);
    for (int i = 0; i < 41; ++i)
        words[i] = static_cast<uint32_t>((i * 37 + n * 11) % 200 + 3);
    auto real = [n](int i) { return static_cast<float>(std::sin(i * 1.7 + n * 0.9) * 50.0 + i * 0.25); };
    p.sunTheta = real(1);
    p.sunPhi = real(2);
    p.sunRed = 255.0f - n;
    p.sunBlue = 100.5f + n;
    p.sunGreen = 3.0f * n;
    p.ambientModLo = real(3);
    p.ambientModHi = real(4);
    p.heading0 = real(5) * 3.0f;
    p.pitch0 = real(6);
    p.modLo0 = real(7);
    p.modHi0 = real(8);
    p.heading1 = real(9) * 4.0f;
    p.pitch1 = real(10);
    p.modLo1 = real(11);
    p.modHi1 = real(12);
    p.bloomOn = n % 3;
    p.bloomStrength = 0x00102030u * static_cast<uint32_t>(n + 1) + 0x0A0B0Cu;
    p.bloomColor = 0x00F0E0D0u - 0x00030201u * static_cast<uint32_t>(n);
    p.levBlue = -40 + n * 3;  // (a negative difference)
    p.fogDist = 1000 * n + 7;
    return p;
}
void PushParams(std::vector<uint32_t>& out, const TODParams& p, bool lerped)
{
    const auto* words = reinterpret_cast<const uint32_t*>(&p);
    for (int i = 0; i < 41; ++i) {
        // (a blend leaves +0x14..+0x24 and fog_on unset in the original)
        if (lerped && ((i >= 5 && i <= 9) || i == 16))
            continue;
        out.push_back(words[i]);
    }
}
CTODManager* Manager() { return Ptr<CTODManager>(kManager); }
void SetSystem(int index, int a, int b, bool transitioning, int time, int length, int stateLength, uint32_t state,
               bool sky)
{
    TODSystem& s = Manager()->m_systems[index];
    s.current = MakeParams(a);
    s.next = MakeParams(b);
    s.transitioning = transitioning;
    s.time = time;
    s.transitionLength = length;
    s.stateLength = stateLength;
    s.state = state;
    s.nextState = Gfx::NextTODState(state);
    s.sky = sky;
}

template <typename R, typename... A>
R Call(bool original, uint32_t address, R (CTODManager::*ours)(A...), A... args)
{
    if (original)
        return Original<R(__thiscall*)(CTODManager*, A...)>(address)(Manager(), args...);
    return (Manager()->*ours)(args...);
}

// Lerp, GetTODParams, GetLevelColor, UpdateSun, ApplyTODParams, SetLightDirection, SetFogActive: blends at t = 0,
// a quarter, 0.6, 1, past 1, negative; the first non-sky system (0, 1, both sky: past the end); transitioning or
// not, no transition length, a time past 2^31 (unsigned), clamped to 1; the sun at night or not; fog on / off and
// every SetFogActive change; light directions at several angles.
void TestTODBlend()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    InstallStandIns(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(g_tod_storage.Get()[run]);
        std::vector<uint32_t>& t = traces[run];
        CTODManager* m = Manager();
        for (float f : {0.0f, 0.25f, 0.6f, 1.0f, 1.5f, -0.3f}) {
            TODParams r = Call(original, 0x00571340, &CTODManager::Lerp, MakeParams(1), MakeParams(4), f);
            PushParams(t, r, true);
        }
        struct Case {
            bool skyA, skyB, transitioning;
            int time, length;
        };
        const Case cases[] = {{false, false, false, 100, 300},   {false, false, true, 100, 300},
                              {true, false, true, 250, 1000},    {false, true, true, 0, 0},
                              {false, false, true, 900, 300},    {false, false, true, -5, 7},
                              {true, false, false, 10, 20},      {false, false, true, 299, 300}};
        for (const Case& c : cases) {
            SetSystem(0, 2, 5, c.transitioning, c.time, c.length, 4000, Gfx::kEvening, c.skyA);
            SetSystem(1, 6, 3, c.transitioning, c.time + 50, c.length + 100, 3000, Gfx::kNight, c.skyB);
            for (int index : {-1, 0, 1}) {
                TODParams r = Call(original, 0x00571E00, &CTODManager::GetTODParams, index);
                PushParams(t, r, c.transitioning);
            }
            uint32_t color = 0xDEADBEEF;
            Call(original, 0x00571140, &CTODManager::GetLevelColor, &color);
            t.push_back(color);
            Call(original, 0x005724D0, &CTODManager::UpdateSun);
            for (bool fog : {false, true}) {
                m->m_fogActive = fog;
                Call(original, 0x00572A10, &CTODManager::ApplyTODParams, MakeParams(fog ? 7 : 8));
            }
        }
        // Both systems sky: past the end (the bloom words that follow).
        SetSystem(0, 2, 5, false, 0, 10, 10, Gfx::kNight, true);
        SetSystem(1, 2, 5, false, 0, 10, 10, Gfx::kNight, true);
        m->m_tempBloom = {11, 22, 33, 44, 55};
        m->m_totalTime = 66;
        uint32_t color = 0;
        Call(original, 0x00571140, &CTODManager::GetLevelColor, &color);
        t.push_back(color);
        // The sun at night and not.
        for (uint32_t state : {Gfx::kNight, Gfx::kMorning}) {
            for (bool transitioning : {false, true}) {
                SetSystem(0, 9, 10, transitioning, 1234, 5000, 60000, state, false);
                Call(original, 0x005724D0, &CTODManager::UpdateSun);
            }
        }
        for (float heading : {0.0f, 45.0f, -130.5f, 270.25f}) {
            for (float pitch : {0.0f, 30.0f, -89.5f, 12.75f}) {
                if (original)
                    Original<void(__cdecl*)(int, float, float)>(0x00572980)(static_cast<int>(heading) & 1, heading,
                                                                            pitch);
                else
                    Gfx::SetLightDirection(static_cast<int>(heading) & 1, heading, pitch);
            }
        }
        SetSystem(0, 3, 4, true, 30, 100, 100, Gfx::kMorning, false);
        for (bool was : {false, true}) {
            for (bool now : {false, true}) {
                m->m_fogActive = was;
                Call(original, 0x00572320, &CTODManager::SetFogActive, now);
                t.push_back(m->m_fogActive);
            }
        }
        g_tod_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_tod_calls[0], g_tod_calls[1], "tod: Lerp, GetTODParams, GetLevelColor, UpdateSun, ApplyTODParams, light directions, fog" " (calls)");
    LogLastMark("tod: Lerp, GetTODParams, GetLevelColor, UpdateSun, ApplyTODParams, light directions, fog");
    Compare(traces[0], traces[1], "tod: Lerp, GetTODParams, GetLevelColor, UpdateSun, ApplyTODParams, light directions, fog");
}

void MakeScene(int n, int count, std::vector<uint32_t> groups, bool sky);

// --- the states: parameters, time passing, the script commands -----------------------------------------------

// Names.
constexpr uint32_t kSystemName = 0x36B2EE74, kCurrentState = 0xF8725A00, kOverwrite = 0x6136E863;
constexpr uint32_t kStateLength = 0xF746BEE2, kTransitionLength = 0x37BC278D, kSkyFlag = 0xF9D98B10;
constexpr uint32_t kGroups = 0x0F92C68F, kParts = 0x96BF5801, kWaitFrames = 0x3FAF7F7F;
constexpr uint32_t kLightGroup = 0x9655CFF7, kResetLightGroup = 0xEC50037D;
constexpr uint32_t kBloomOn = 0x400681B6, kBloomStrength = 0x743D0048, kBloomColor = 0x6AC75185;
constexpr uint32_t kName = 0xA1DC81F9, kPrefix = 0x6C4E7971, kSystemIndex = 0x2B430364, kStateName = 0x353F083F;
constexpr uint32_t kOn = 0xF649D637, kBloom1 = 0xF68014D9, kBloom2 = 0x6F894563, kStrength = 0x29461E27,
                   kColor = 0x99A9B716;
// The parameters' names (ParseParams reads them: integers, then floats).
constexpr uint32_t kIntNames[] = {0x966014FF, 0x5F727131, 0x1C5D554E, 0xBFAC0E79, 0xCFC99E4A, 0x18AFBF41, 0xCE0BD6E5,
                                  0x9A35701D, 0xE167C7F6, 0x9A4FA756, 0xFE2DDDB5, 0xED664897, 0x908BF647, 0x5F4FBA78,
                                  0xE213AEB7, 0x6EF19631, 0x5EC44213, 0x95149E21, 0x19F6A6A7, 0x29C37285};
constexpr uint32_t kFloatNames[] = {0xB3B5E2EF, 0x3EBA82DE, 0xC15B55B1, 0xD9FF5B75, 0x05999531, 0x8896F500,
                                    0xB65C6527, 0xAEF86BE3, 0xBD25F254, 0x302A9265, 0xDF037B3B, 0x9C2BF09E,
                                    0x54B02600, 0x04811F13, 0x319D6AF1};
constexpr uint32_t kStates[] = {Gfx::kMorning, Gfx::kAfternoon, Gfx::kEvening, Gfx::kNight};

// A state's structure (stand-in structure `n`): most parameters (some left out), bloom on for odd `variant`.
void FillState(int n, int variant, int stateLength, bool full = false)
{
    uint32_t s = StructAt(n);
    for (int i = 0; i < 20; ++i) {
        if (full || (i + variant) % 7 != 3)
            Answer('I', s, kIntNames[i], {static_cast<uint32_t>(variant * 31 + i * 9 + 4)});
    }
    for (int i = 0; i < 15; ++i) {
        if (full || (i + variant) % 5 != 1)
            AnswerFloat(s, kFloatNames[i], static_cast<float>(variant * 13.5 - i * 7.25 + 0.5));
    }
    Answer('I', s, kStateLength, {static_cast<uint32_t>(stateLength)});
    if (variant % 2) {
        Answer('I', s, kBloomOn, {static_cast<uint32_t>(variant % 4 == 1 ? 1 : variant)});
        Answer('I', s, kBloom1, {static_cast<uint32_t>(variant + 40)});
        if (variant != 3)
            Answer('I', s, kBloom2, {static_cast<uint32_t>(variant + 70)});
        MakeArray(8 + variant % 4, {static_cast<uint32_t>(variant * 3), 0x1FF, static_cast<uint32_t>(variant)}, false);
        Answer('A', s, kBloomStrength, {ArrayAt(8 + variant % 4)});
        if (variant != 5) {
            MakeArray(12 + variant % 4, {0x77, static_cast<uint32_t>(variant * 5), 0x33}, false);
            Answer('A', s, kBloomColor, {ArrayAt(12 + variant % 4)});
        }
    }
}

// A system's parameters (structure `paramsN`): the four states (structures `firstState`..+3), the light groups
// (structures 20.. in array `arrayN`).
void FillSystem(int paramsN, int firstState, int arrayN, int firstGroup)
{
    uint32_t p = StructAt(paramsN);
    for (int i = 0; i < 4; ++i) {
        Answer('S', p, kStates[i], {StructAt(firstState + i)});
        FillState(firstState + i, firstState + i, 700 + 300 * i + 50 * paramsN);
    }
    MakeArray(arrayN, {StructAt(firstGroup), StructAt(firstGroup + 1), StructAt(firstGroup + 2)}, true);
    Answer('A', p, kGroups, {ArrayAt(arrayN)});
    for (int g = 0; g < 3; ++g) {
        uint32_t group = StructAt(firstGroup + g);
        Answer('I', group, kParts, {static_cast<uint32_t>(g + 2)});
        Answer('I', group, kWaitFrames, {static_cast<uint32_t>(g)});
        Answer('K', group, kLightGroup, {g == 1 ? 0xE3714DC1u : 0x1000u + g});
        if (g != 0)
            Answer('K', group, kResetLightGroup, {0x2000u + g});
    }
}

using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);
bool CallCFunc(bool original, uint32_t address, CFunc ours, uint32_t params)
{
    auto* p = Ptr<CStruct>(params);
    auto* script = Ptr<Script::CScript>(kScript);
    return original ? Original<CFunc>(address)(p, script) : ours(p, script);
}

// Construct, SetParams (both systems, overwrite_current_state, a sky system), ReadGroup, NextState, ParseParams,
// many frames of Update (not career, tod_manager_inactive, paused, a temporary state, inactive, the multiplier;
// states ending, transitions part by part and group by group, the transition script, bloom with and without the
// temporary bloom, levels lights reset), SetTODInstant / AdvanceTODState through several states, RefreshAll,
// ResetLevelLights, the temporary states and bloom, EnableBloom (in single and multiplayer), the other script
// commands, the dead objects (MarkPermanentlyDead, Create by name and prefix, the table emptied), Reset, the
// destructor, and the singleton made / referenced / released.
void TestTODStates()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    InstallStandIns(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(g_tod_storage.Get()[run]);
        std::vector<uint32_t>& t = traces[run];
        CTODManager* m = Manager();
        auto mark = [&](uint32_t tag) { g_trace.push_back(0xFEED0000u | tag); };

        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x00573620, &CTODManager::Construct))));
        GameVariable<CTODManager*>(0x007CE490) = m;
        m->m_systems[0].params = Ptr<CStruct>(StructAt(1));
        m->m_systems[1].params = Ptr<CStruct>(StructAt(2));
        FillSystem(1, 30, 1, 20);
        FillSystem(2, 40, 2, 24);
        // SetParams: system 1 (current morning, its own state structures), system 2 (sky, overwrite_current_state).
        uint32_t set1 = StructAt(3);
        Answer('I', set1, kSystemName, {1});
        Answer('K', set1, kCurrentState, {Gfx::kMorning});
        Answer('S', set1, Gfx::kMorning, {StructAt(30)});
        Answer('S', set1, Gfx::kAfternoon, {StructAt(31)});
        Answer('I', set1, kTransitionLength, {500});
        uint32_t set2 = StructAt(4);
        Answer('I', set2, kSystemName, {2});
        Answer('K', set2, kCurrentState, {Gfx::kEvening});
        Answer('S', set2, kOverwrite, {StructAt(42)});
        Answer('S', set2, Gfx::kEvening, {StructAt(41)});
        Answer('L', set2, kSkyFlag, {});
        mark(1);
        CallCFunc(original, 0x00572BE0, Gfx::CFunc_TODManager_SetParams, set1);
        CallCFunc(original, 0x00572BE0, Gfx::CFunc_TODManager_SetParams, set2);
        for (uint32_t i = 0; i < sizeof(CTODManager) / 4; ++i)
            t.push_back(Rel(reinterpret_cast<uint32_t*>(m)[i]));
        // A level scene, so the parts' percentages count.
        // (128 sectors - four sector objects, many items - so a percentage off by one shows)
        MakeScene(0, 4, {0x1000u, 0xE3714DC1u, 0x1002u, 0x2001u}, false);
        {
            auto* table = Ptr<Lst::HashTable>(kTables);
            table->m_size = 128;
            for (int i = 4; i < 128; ++i) {
                auto* item = Ptr<Lst::HashTable::Item>(0xD000 + 0xC * i);
                Lst::HashTable::Item* bucket = &table->m_buckets[i % 4];
                *item = {0x100u + i, bucket->value, bucket->next};
                bucket->next = item;
            }
        }
        g_tod_levelScene = kScenes;
        m->m_systems[1].transitionLength = 650; // (no transition_length: the default)
        mark(2);
        Call(original, 0x00571290, &CTODManager::ReadGroup, 0);
        Call(original, 0x00571290, &CTODManager::ReadGroup, 1);
        Call(original, 0x005723C0, &CTODManager::NextState, 1);
        // Frames: blocked ones, then time passing through states and transitions.
        mark(3);
        for (int frame = 0; frame < 90; ++frame) {
            g_tod_gameMode = frame == 0 ? 0x12345678u : 0x4DA4937Bu;
            g_tod_inactive = frame == 1 ? 1 : (frame == 2 ? 2 : 0);
            GameVariable<uint8_t*>(0x007CE464)[0x30] = frame == 3;
            m->m_tempTOD = frame == 4;
            m->m_active = frame != 5;
            m->m_timeMultiplier = frame < 40 ? 2.5f : 1.75f;
            g_tod_frameSeconds = 1.0 / (30.0 + frame % 7);
            m->m_tempBloom.on = frame >= 40 && frame < 70;
            m->m_levelLightsReset = frame >= 50 && frame < 55;
            m->m_transitionScript = frame < 30 ? 0u : 0x5C1A1Au;
            if (frame == 20)
                m->m_systems[0].time = -100; // (unsigned in the blends)
            Call(original, 0x00574590, &CTODManager::Update);
            mark(0x100 + frame);
        }
        for (uint32_t i = 0; i < sizeof(CTODManager) / 4; ++i)
            t.push_back(Rel(reinterpret_cast<uint32_t*>(m)[i]));
        // Jumps through the day.
        uint32_t instant = StructAt(5);
        for (uint32_t state : {Gfx::kNight, Gfx::kNight, Gfx::kAfternoon, Gfx::kMorning}) {
            Answer('K', instant, 0, {state});
            CallCFunc(original, 0x00574840, Gfx::CFunc_TODManager_SetTODInstant, instant);
            mark(4);
        }
        for (int i = 0; i < 3; ++i)
            t.push_back(CallCFunc(original, 0x00574870, Gfx::CFunc_TODManager_AdvanceTODState, StructAt(6)));
        m->m_systems[0].transitioning = true;
        m->m_systems[0].time = 120;
        CallCFunc(original, 0x00574870, Gfx::CFunc_TODManager_AdvanceTODState, StructAt(6));
        mark(5);
        m->m_active = false;
        m->m_systems[0].parts = 3; // (100 / 3 rounded to a float: the last part ends at 99 instead of 100)
        m->m_systems[0].groupCount = 2;
        Call(original, 0x00573CE0, &CTODManager::RefreshAll, true);
        t.push_back(m->m_active);
        m->m_active = true;
        // ResetLevelLights: no state, a state; in multiplayer: nothing.
        CallCFunc(original, 0x00574480, Gfx::CFunc_TODManager_ResetLevelLights, StructAt(6));
        // (a state with every parameter: the original's parameters are uninitialised, what the state leaves out)
        Answer('K', StructAt(7), kStateName, {0x5157A7Eu});
        Answer('S', StructAt(1), 0x5157A7Eu, {StructAt(50)});
        FillState(50, 7, 900, true);
        CallCFunc(original, 0x00574480, Gfx::CFunc_TODManager_ResetLevelLights, StructAt(7));
        g_tod_multiplayer = true;
        CallCFunc(original, 0x00574480, Gfx::CFunc_TODManager_ResetLevelLights, StructAt(7));
        g_tod_multiplayer = false;
        mark(6);
        // The temporary state and bloom.
        FillState(8, 9, 1000);
        CallCFunc(original, 0x005744C0, Gfx::CFunc_TODManager_SetTempTOD, StructAt(8));
        t.push_back(*Ptr<uint32_t>(kSymbol + 0xC));
        CallCFunc(original, 0x005744E0, Gfx::CFunc_TODManager_RestoreCurrentTOD, StructAt(6));
        uint32_t bloom = StructAt(9);
        Answer('I', bloom, kOn, {1});
        Answer('I', bloom, kBloom1, {5});
        MakeArray(5, {9, 8, 0x107}, false);
        Answer('A', bloom, kStrength, {ArrayAt(5)});
        MakeArray(6, {1, 2, 3}, false);
        Answer('A', bloom, kColor, {ArrayAt(6)});
        CallCFunc(original, 0x005744F0, Gfx::CFunc_TODManager_SetTempBloomParams, bloom);
        uint32_t bloomOff = StructAt(10);
        Answer('I', bloomOff, kOn, {0});
        Answer('I', bloomOff, kBloom2, {77});
        CallCFunc(original, 0x005744F0, Gfx::CFunc_TODManager_SetTempBloomParams, bloomOff);
        g_tod_multiplayer = true;
        CallCFunc(original, 0x005744F0, Gfx::CFunc_TODManager_SetTempBloomParams, bloomOff);
        mark(7);
        m->m_systems[0].current.bloomOn = 0;
        for (int on : {0, 0, 1}) {
            Answer('I', StructAt(11), 0, {static_cast<uint32_t>(on)});
            CallCFunc(original, 0x00574510, Gfx::CFunc_TODManager_EnableBloom, StructAt(11));
            g_tod_multiplayer = false;
        }
        CallCFunc(original, 0x00574510, Gfx::CFunc_TODManager_EnableBloom, StructAt(11));
        mark(8);
        // The other script commands.
        Ptr<uint32_t>(kScript)[5] = At(StructAt(12)); // the script's parameters
        Answer('I', StructAt(13), 0, {0});
        CallCFunc(original, 0x00571FE0, Gfx::CFunc_TODManager_SetActive, StructAt(13));
        t.push_back(m->m_active);
        Answer('I', StructAt(13), 0, {7});
        CallCFunc(original, 0x00571FE0, Gfx::CFunc_TODManager_SetActive, StructAt(13));
        t.push_back(m->m_active);
        m->m_systems[1].daysPassed = 12;
        CallCFunc(original, 0x00572010, Gfx::CFunc_TODManager_GetNumberOfDaysPassed, StructAt(14));
        Answer('I', StructAt(14), kSystemIndex, {1});
        CallCFunc(original, 0x00572010, Gfx::CFunc_TODManager_GetNumberOfDaysPassed, StructAt(14));
        CallCFunc(original, 0x00572060, Gfx::CFunc_TODManager_SetTimeMultiplier, StructAt(15));
        t.push_back(Bits(m->m_timeMultiplier));
        AnswerFloat(StructAt(15), 0, 3.5f);
        CallCFunc(original, 0x00572060, Gfx::CFunc_TODManager_SetTimeMultiplier, StructAt(15));
        t.push_back(CallCFunc(original, 0x005720A0, Gfx::CFunc_TODManager_GetTimeMultiplier, StructAt(15)));
        Answer('K', StructAt(16), 0, {0xABCDEF01});
        CallCFunc(original, 0x005720D0, Gfx::CFunc_TODManager_SetTODScript, StructAt(16));
        CallCFunc(original, 0x005720D0, Gfx::CFunc_TODManager_SetTODScript, StructAt(17));
        CallCFunc(original, 0x005730A0, Gfx::CFunc_TODManager_SetFogActive, StructAt(17));
        Answer('I', StructAt(18), 0, {0});
        CallCFunc(original, 0x005730A0, Gfx::CFunc_TODManager_SetFogActive, StructAt(18));
        mark(9);
        CallCFunc(original, 0x00572C00, Gfx::CFunc_TODManager_GetCurrentTODParams, StructAt(17));
        m->m_systems[0].transitioning = false;
        Answer('I', StructAt(18), kSystemIndex, {1});
        CallCFunc(original, 0x00572C00, Gfx::CFunc_TODManager_GetCurrentTODParams, StructAt(18));
        mark(10);
        // The dead objects.
        auto* nodes = Ptr<uint32_t>(kNodes);
        for (int i = 0; i < 6; ++i)
            nodes[i] = 0x3000u + i * 0x41u;
        Answer('K', StructAt(19), kName, {0x3082u});
        CallCFunc(original, 0x005735F0, Gfx::CFunc_TODManager_MarkPermanentlyDead, StructAt(19));
        CallCFunc(original, 0x005735F0, Gfx::CFunc_TODManager_MarkPermanentlyDead, StructAt(19));
        g_tod_prefixCount = 2;
        Answer('C', StructAt(28), kPrefix, {0x7777u});
        CallCFunc(original, 0x005735F0, Gfx::CFunc_TODManager_MarkPermanentlyDead, StructAt(28));
        CallCFunc(original, 0x005735F0, Gfx::CFunc_TODManager_MarkPermanentlyDead, StructAt(29));
        CallCFunc(original, 0x00572110, Gfx::CFunc_TODManager_Create, StructAt(19));
        Answer('K', StructAt(27), kName, {0x3123u});
        CallCFunc(original, 0x00572110, Gfx::CFunc_TODManager_Create, StructAt(27));
        g_tod_prefixCount = 6;
        Answer('L', StructAt(28), 0x80804991, {});
        CallCFunc(original, 0x00572110, Gfx::CFunc_TODManager_Create, StructAt(28));
        CallCFunc(original, 0x00572110, Gfx::CFunc_TODManager_Create, StructAt(29));
        mark(11);
        PushArena(t);
        Call(original, 0x00572BD0, &CTODManager::ClearDeadObjects);
        CallCFunc(original, 0x00572110, Gfx::CFunc_TODManager_Create, StructAt(19));
        CallCFunc(original, 0x00573610, Gfx::CFunc_TODManager_Reset, StructAt(6));
        mark(12);
        PushArena(t);
        Call(original, 0x00573770, &CTODManager::Destruct);
        mark(13);
        // The singleton.
        GameVariable<CTODManager*>(0x007CE490) = nullptr;
        auto create = [&](bool make) {
            CTODManager* r = original ? Original<CTODManager*(__cdecl*)(bool)>(0x00574540)(make) : Gfx::CreateTODManager(make);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
            t.push_back(GameVariable<uint32_t>(0x007CE494));
        };
        auto release = [&]() {
            if (original)
                Original<void(__cdecl*)()>(0x00574910)();
            else
                Gfx::ReleaseTODManager();
            t.push_back(Rel(GameVariable<uint32_t>(0x007CE490)));
            t.push_back(GameVariable<uint32_t>(0x007CE494));
        };
        create(false);
        release();
        create(true);
        create(true);
        create(false);
        release();
        release();
        release();
        g_tod_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_tod_calls[0], g_tod_calls[1], "tod: states, time passing, the script commands, dead objects, the singleton" " (calls)");
    LogLastMark("tod: states, time passing, the script commands, dead objects, the singleton");
    Compare(traces[0], traces[1], "tod: states, time passing, the script commands, dead objects, the singleton");
}

// --- colouring the scenes and models ---------------------------------------------------------------------------

// Scene n (0..3): `count` sectors (groups cycling through `groups`), in 4 buckets (some chained).
void MakeScene(int n, int count, std::vector<uint32_t> groups, bool sky)
{
    uint32_t scene = kScenes + 0x100 * n;
    *Ptr<void**>(scene) = g_tod_sceneVtable;
    Ptr<uint8_t>(scene)[0x8D] = sky;
    uint32_t table = kTables + 0x60 * n;
    *Ptr<uint32_t>(scene + 0xD0) = At(table);
    auto* t = Ptr<Lst::HashTable>(table);
    t->m_bits = 2;
    t->m_buckets = Ptr<Lst::HashTable::Item>(table + 0x20);
    t->m_size = count;
    // The items: bucket i gets sectors i, i+4 (chained from extra items placed after the buckets).
    static int next = 0;
    if (n == 0)
        next = 0;
    auto* buckets = t->m_buckets;
    for (int i = 0; i < count; ++i) {
        uint32_t sector = kSectors + 0x20 * ((next + i) % 16);
        *Ptr<uint32_t>(sector + 0xC) = groups[i % groups.size()];
        uint32_t geom = kGeoms + 8 * ((next + i) % 16);
        *Ptr<void**>(geom) = g_tod_geomVtable;
        *Ptr<uint32_t>(sector + 0x14) = (i % 5 == 3) ? 0u : At(geom);
        Lst::HashTable::Item* bucket = &buckets[i % 4];
        if (i < 4) {
            *bucket = {0x100u + i, Ptr<void>(sector), nullptr};
        } else {
            // a chained item (after the geometry)
            auto* item = Ptr<Lst::HashTable::Item>(kGeoms + 0x80 + 0x10 * ((next + i) % 8));
            *item = {0x100u + i, Ptr<void>(sector), bucket->next};
            bucket->next = item;
        }
    }
    next += count;
}

// ApplyColorToScene (every sector, a group, a group and a reset group; parts by percentage, past the end), ApplyColor
// (the sky; the scenes - skies left out -, the level scene, the models for the outdoor group / no group / a reset
// group, part-only), SetSceneColor's parameters.
void TestTODColor()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    InstallStandIns(*s);
    for (void*& slot : g_tod_sceneVtable)
        slot = nullptr;
    g_tod_sceneVtable[8] = reinterpret_cast<void*>(&StandInSceneColor);
    g_tod_geomVtable[6] = reinterpret_cast<void*>(&StandInGeomColor);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(g_tod_storage.Get()[run]);
        std::vector<uint32_t>& t = traces[run];
        auto mark = [&](uint32_t tag) { g_trace.push_back(0xFEED0000u | tag); };
        MakeScene(0, 7, {0x1001, 0xE3714DC1, 0x2002}, false);
        MakeScene(1, 5, {0x2002, 0x1001}, true);  // a sky scene in the list
        MakeScene(2, 3, {0x1001}, false);
        MakeScene(3, 0, {0}, false);
        auto scene = [](int n) { return Ptr<uint8_t>(kScenes + 0x100 * n); };
        auto apply = [&](int n, uint32_t color, uint32_t group, uint32_t reset, int start, int end) {
            if (original)
                Original<void(__cdecl*)(uint8_t*, uint32_t, uint32_t, uint32_t, int, int)>(0x005725F0)(
                    scene(n), color, group, reset, start, end);
            else
                Gfx::ApplyColorToScene(scene(n), color, group, reset, start, end);
            mark(n);
        };
        for (int n = 0; n < 4; ++n) {
            apply(n, 0x11223344, 0, 0, -1, -1);
            apply(n, 0x55667788, 0x1001, 0, -1, 50);
            apply(n, 0x99AABBCC, 0x1001, 0x2002, 0, 100);
            apply(n, 0x0D0E0F10, 0x2002, 0, 25, 75);
            apply(n, 0x01020304, 0, 0, 40, 30);
            apply(n, 0x05060708, 0x1001, 0x2002, 50, 300);
            apply(n, 0x0A0B0C0D, 0xE3714DC1, 0x1001, 99, 100);
        }
        *Ptr<uint32_t>(kScenes + 0x200 + 0xD0) = 0; // a scene without a sector table
        apply(2, 0x12121212, 0, 0, -1, -1);
        MakeScene(2, 3, {0x1001}, false);
        // The scene list: scenes 0, 1 (sky), 2, then the end.
        for (int i = 0; i < 4; ++i) {
            uint32_t node = kSceneNodes + 0x10 * i;
            *Ptr<uint32_t>(node + 4) = i < 3 ? At(kScenes + 0x100 * i) : 0xFFFFFFFFu;
            *Ptr<uint32_t>(node + 0xC) = i < 3 ? At(node + 0x10) : 0u;
        }
        // Models: objects without a sector, with an outdoor one, with the reset group's, with another.
        for (int i = 0; i < 5; ++i) {
            uint32_t model = kModels + 0x20 * i;
            *Ptr<uint32_t>(model + 0xC) = At(kObjects + 0x20 * i);
            *Ptr<uint32_t>(model + 0x14) = i < 4 ? At(model + 0x20) : 0u;
            *Ptr<uint32_t>(kObjects + 0x20 * i + 0x14) = 0x4000u + i;
        }
        const uint32_t groupsByModel[] = {0, 0xE3714DC1, 0x2002, 0x1001, 0x2002};
        for (int i = 1; i < 5; ++i) {
            *Ptr<uint32_t>(kNamed + 0x20 * i + 0xC) = groupsByModel[i];
            g_tod_namedSectors[0x4000u + i] = kNamed + 0x20 * i;
        }
        g_tod_firstModel = kModels;
        CTODManager* m = Manager();
        auto applyColor = [&](uint32_t color, uint32_t sky, bool isSky, uint32_t group, uint32_t reset, bool part,
                              int start, int end) {
            Call(original, 0x005727F0, &CTODManager::ApplyColor, color, sky, isSky, group, reset, part, start, end);
            mark(0x100);
        };
        for (int variant = 0; variant < 4; ++variant) {
            g_tod_firstNode = variant == 3 ? 0u : kSceneNodes;
            g_tod_skyScene = variant == 1 ? 0u : kScenes + 0x100;
            g_tod_levelScene = variant == 2 ? 0u : kScenes + 0x200;
            g_tod_firstModel = variant == 3 ? 0u : kModels;
            applyColor(0x10203040, 0x50607080, true, 0x1001, 0x2002, false, 10, 20);
            applyColor(0x10203041, 0x50607081, false, 0, 0, false, -1, -1);
            applyColor(0x10203042, 0x50607082, false, 0xE3714DC1, 0, false, 0, 50);
            applyColor(0x10203043, 0x50607083, false, 0x1001, 0x2002, false, 50, 100);
            applyColor(0x10203044, 0x50607084, false, 0x1001, 0, false, 0, 100);
            applyColor(0x10203045, 0x50607085, false, 0, 0x2002, true, 20, 60);
            applyColor(0x10203046, 0x50607086, false, 0xE3714DC1, 0x1001, true, -1, -1);
        }
        // SetSceneColor's parameters: the colour only; sky, groups, the flag and percentages.
        g_tod_firstNode = kSceneNodes;
        g_tod_skyScene = kScenes + 0x100;
        g_tod_maskAlpha = true;
        uint32_t p = StructAt(1);
        Answer('I', p, kColor, {0xAB123456});
        Call(original, 0x005732D0, &CTODManager::SetSceneColor, Ptr<CStruct>(p));
        uint32_t q = StructAt(2);
        Answer('I', q, kColor, {0x00654321});
        Answer('I', q, kSkyFlag, {0xFF778899});
        Answer('K', q, kLightGroup, {0x1001});
        Answer('K', q, kResetLightGroup, {0x2002});
        Answer('L', q, 0xE5B6C568, {});
        Answer('I', q, 0xDF1A695F, {30});
        Answer('I', q, 0x8F639F94, {90});
        Call(original, 0x005732D0, &CTODManager::SetSceneColor, Ptr<CStruct>(q));
        uint32_t r = StructAt(3);
        Answer('I', r, kColor, {0x00ABCDEF});
        Answer('K', r, kLightGroup, {0xE3714DC1});
        Answer('I', r, 0xDF1A695F, {10});
        Call(original, 0x005732D0, &CTODManager::SetSceneColor, Ptr<CStruct>(r));
        (void)m;
        g_tod_maskAlpha = false;
        g_tod_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_tod_calls[0], g_tod_calls[1], "tod: colouring the scenes' sectors, the sky, the level scene and the models" " (calls)");
    LogLastMark("tod: colouring the scenes' sectors, the sky, the level scene and the models");
    Compare(traces[0], traces[1], "tod: colouring the scenes' sectors, the sky, the level scene and the models");
}

} // namespace

SELFTEST(Early, TestTODBlend, Cleanup);
SELFTEST(Early, TestTODStates, Cleanup);
SELFTEST(Early, TestTODColor, Cleanup);
