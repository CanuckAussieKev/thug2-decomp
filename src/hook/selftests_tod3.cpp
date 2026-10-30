// Self-tests for the Skatetris, TrickBeat, TimedScoreRun and TrickObjects goals (src/game/skate/trick_goals.cpp):
// the original and ours run the same calls on identical stand-in goals, parameter structures, skaters, scores, screen
// elements and scripts in an arena. Everything they call outside these goals (CGoal's own functions and virtual
// functions, the goal manager, the structure and array functions, the scripts, the screen element manager and its
// elements, the skater's score and trick mapping, random numbers, the clocks, the sounds, the memory manager, the C
// runtime's sprintf) is stood in for by recording code written over the game's function and ours alike, so the two
// are compared call by call, then the whole arena word by word.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/menu_element.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/mth/random.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skate/profile_manager.h"
#include "game/skate/trick_goals.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/goals/goal_manager.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CArray;
using Script::CStruct;

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

constexpr uint32_t kArenaSize = 0x20000;
ArenaPair<kArenaSize> g_tg_storage;
uint32_t g_tg_base;
uint32_t At(uint32_t offset) { return g_tg_base + offset; }
template <typename T>
T* Ptr(uint32_t offset)
{
    return reinterpret_cast<T*>(g_tg_base + offset);
}
// (both arenas, the same for both runs: a value that only happens to fall inside one of them maps the same way in
// each - ArenaWord, selftest.h)
bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_tg_storage.Get()) <= 2 * kArenaSize; }
uint32_t Rel(uint32_t word) { return ArenaWord(g_tg_storage.Get(), kArenaSize, word); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
void Mark(uint32_t tag) { g_trace.push_back(0xFEED0000u | tag); }

// Arena layout.
constexpr uint32_t kTetris = 0x0000;     // a CTetrisGoal (0x458)
constexpr uint32_t kBeat = 0x0500;       // a CTrickBeatGoal (0x47C)
constexpr uint32_t kSmall = 0x0A00;      // a TimedScoreRun / TrickObjects goal (0x70)
constexpr uint32_t kSkater = 0x0B00;     // the local skater (+0x17C the special trick slots)
constexpr uint32_t kSpecials = 0x0C00;   // special trick slot objects, 0x10 apart (+4 a structure)
constexpr uint32_t kScore = 0x0C80;      // the score object (+0x14 pending points, +0x60 the multiplier)
constexpr uint32_t kScript = 0x0D00;     // the stand-in spawned script (m_object at +0xBC)
constexpr uint32_t kManager = 0x0E00;    // the screen element manager (vtable +0)
constexpr uint32_t kMisc = 0x0E80;       // the goal manager, the profile, components
constexpr uint32_t kElements = 0x1000;   // screen elements found by id, 0x40 apart (vtable, smart pointers, id +0x14)
constexpr uint32_t kStructs = 0x1400;    // stand-in structures, 0x10 apart (64)
constexpr uint32_t kArrays = 0x1800;     // stand-in arrays, 0x40 apart: the CArray, elements at +0x10 (16)
constexpr uint32_t kHeap = 0x2000;

uint32_t g_tg_heapNext;
std::vector<Saved> g_tg_saved;
std::vector<uint32_t> g_tg_calls[2];
// Structure answers: (kind, structure offset, name) -> words ('S' / 'A': arena offsets).
std::map<std::tuple<char, uint32_t, uint32_t>, std::vector<uint32_t>> g_tg_answers;
std::map<uint32_t, uint32_t> g_tg_elements;  // id -> arena offset
std::map<uint32_t, int> g_tg_trickCounts;    // trick / key combo checksum -> how often done
std::map<uint32_t, uint32_t> g_tg_globals;   // script globals: name -> structure / array offset
std::map<uint32_t, float> g_tg_floats;       // script float globals
uint32_t g_tg_random;
int g_tg_pick;                               // the goal's index pick (-1: none)
bool g_tg_active, g_tg_paused, g_tg_locked, g_tg_flag554110, g_tg_flag554220, g_tg_flag5541E0;
uint32_t g_tg_localSkater;
uint32_t g_tg_nextId;
int g_tg_clock;
float g_tg_slomo;
double g_tg_frameSeconds;
void* g_tg_goalVtable[0x60 / 4];
void* g_tg_elementVtable[4];
void* g_tg_managerVtable[4];

uint32_t StructAt(int n) { return kStructs + 0x10 * n; }
uint32_t ArrayAt(int n) { return kArrays + 0x40 * n; }
void Answer(char kind, uint32_t structure, uint32_t name, std::vector<uint32_t> value)
{
    g_tg_answers[{kind, structure, name}] = std::move(value);
}
void Int(uint32_t structure, uint32_t name, int value) { Answer('I', structure, name, {static_cast<uint32_t>(value)}); }
void Flag(uint32_t structure, uint32_t name) { Answer('L', structure, name, {}); }
void MakeArray(int n, std::vector<uint32_t> values, bool pointers, Script::ESymbolType type = Script::ESYMBOLTYPE_STRUCTURE)
{
    auto* array = Ptr<CArray>(ArrayAt(n));
    array->m_unknown0 = 0;
    array->m_type = type;
    array->m_size = static_cast<uint32_t>(values.size());
    auto* elements = Ptr<uint32_t>(ArrayAt(n) + 0x10);
    for (size_t i = 0; i < values.size(); ++i)
        elements[i] = pointers ? At(values[i]) : values[i];
}
// A screen element (found by id).
uint32_t MakeElement(int n, uint32_t id)
{
    uint32_t element = kElements + 0x40 * n;
    *Ptr<void**>(element) = g_tg_elementVtable;
    *Ptr<uint32_t>(element + 0x14) = id;
    g_tg_elements[id] = element;
    return element;
}

// The goals' virtual functions (CGoal's slots).
uint32_t g_tg_volume;
bool __fastcall StandInIsActive(void* goal) { Rec(0x0C, {reinterpret_cast<uint32_t>(goal)}); return g_tg_active; }
bool __fastcall StandInIsPaused(void* goal) { Rec(0x48, {reinterpret_cast<uint32_t>(goal)}); return g_tg_paused; }
bool __fastcall StandInIsLocked(void* goal) { Rec(0x54, {reinterpret_cast<uint32_t>(goal)}); return g_tg_locked; }
void __fastcall StandInDeactivate(void* goal, void*, int a, int b)
{
    Rec(0x34, {reinterpret_cast<uint32_t>(goal), static_cast<uint32_t>(a), static_cast<uint32_t>(b)});
}
void __fastcall StandInSetFlag(void* goal, void*, uint32_t flag, int a, int b)
{
    Rec(0x4C, {reinterpret_cast<uint32_t>(goal), flag, static_cast<uint32_t>(a), static_cast<uint32_t>(b)});
}
void __fastcall StandInSetProperties(void* element, void*, CStruct* params)
{
    Rec(0xE101, {reinterpret_cast<uint32_t>(element), reinterpret_cast<uint32_t>(params)});
}
void __fastcall StandInAddElement(void* manager, void*, void* element)
{
    Rec(0xE102, {reinterpret_cast<uint32_t>(manager), reinterpret_cast<uint32_t>(element)});
}
// The script floats, the slow motion, the frame (x87 returns).
float __cdecl StandInScriptFloat(uint32_t name)
{
    Rec(0x8F00, {name});
    return g_tg_floats[name];
}
float __cdecl StandInSlomo()
{
    Rec(0xE8F0, {});
    return g_tg_slomo;
}
double __cdecl StandInFrameSeconds()
{
    Rec(0xEAA0, {});
    return g_tg_frameSeconds;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_tg_saved);
    g_tg_saved.clear();
}

void Install(StandIns& s)
{
    for (void*& slot : g_tg_goalVtable)
        slot = nullptr;
    g_tg_goalVtable[0x0C / 4] = reinterpret_cast<void*>(&StandInIsActive);
    g_tg_goalVtable[0x34 / 4] = reinterpret_cast<void*>(&StandInDeactivate);
    g_tg_goalVtable[0x48 / 4] = reinterpret_cast<void*>(&StandInIsPaused);
    g_tg_goalVtable[0x4C / 4] = reinterpret_cast<void*>(&StandInSetFlag);
    g_tg_goalVtable[0x54 / 4] = reinterpret_cast<void*>(&StandInIsLocked);
    g_tg_elementVtable[1] = reinterpret_cast<void*>(&StandInSetProperties);
    g_tg_managerVtable[1] = reinterpret_cast<void*>(&StandInAddElement);

    // The structures: getters from the answers, adders recorded and kept (later reads see them).
    auto getter = [&](std::initializer_list<uint32_t> addresses, char kind, uint32_t tag) {
        s.At(addresses, 12, [kind, tag](Regs& r) {
            Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
            auto it = g_tg_answers.find({kind, r.ecx - g_tg_base, r.args[0]});
            if (it == g_tg_answers.end())
                return 0u;
            std::vector<uint32_t> value = it->second;
            if (kind == 'S' || kind == 'A' || kind == 'T')
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
    getter({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
           'S', 0x67C0);
    getter({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray))}, 'A',
           0x6860);
    // Strings: their answers are arena offsets of the text.
    getter({0x00476590, 0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
           'T', 0x6590);
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        return g_tg_answers.count({'L', r.ecx - g_tg_base, r.args[0]}) ? 1u : 0u;
    });
    s.At({0x00477860, AddressOf(&CStruct::RemoveFlag)}, 4, [](Regs& r) {
        Rec(0x7860, {r.ecx, r.args[0]});
        g_tg_answers.erase({'L', r.ecx - g_tg_base, r.args[0]});
        return 0u;
    });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Rec(0x7B80, {r.ecx, r.args[0], r.args[1]});
        if (InArena(r.ecx))
            g_tg_answers[{'I', r.ecx - g_tg_base, r.args[0]}] = {r.args[1]};
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
             if (InArena(r.ecx)) {
                 if (r.args[0] == 0)
                     g_tg_answers[{'L', r.ecx - g_tg_base, r.args[1]}] = {};
                 else
                     g_tg_answers[{'K', r.ecx - g_tg_base, r.args[0]}] = {r.args[1]};
             }
             return 0u;
         });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Rec(0x79D0, {r.ecx, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return 0u;
         });
    s.At({0x00477FC0, AddressOf(&CStruct::AddPair)}, 12, [](Regs& r) {
        Rec(0x7FC0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x7090, {r.ecx});
        return 0u;
    });
    // Arrays.
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0]];
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
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
    s.At({0x00478A60, AddressOf(&Script::CopyArray)}, 0, [](Regs& r) {
        Rec(0x8A60, {r.args[0], r.args[1]});
        return 0u;
    });
    // Script globals.
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Rec(0x9020, {r.args[0]});
        auto it = g_tg_globals.find(r.args[0]);
        return it == g_tg_globals.end() || it->second >= kArrays ? 0u : At(it->second);
    });
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        auto it = g_tg_globals.find(r.args[0]);
        return it == g_tg_globals.end() ? 0u : At(it->second);
    });
    s.Bytes(0x00478F00, Branch(0xE9, 0x00478F00, reinterpret_cast<void*>(&StandInScriptFloat)));
    uint32_t ours = AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat));
    s.Bytes(ours, Branch(0xE9, ours, reinterpret_cast<void*>(&StandInScriptFloat)));
    // Scripts.
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                 bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                          r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             return At(kScript);
         });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x5790, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) {
        Rec(0x50B0, {r.ecx, *reinterpret_cast<uint32_t*>(r.ecx + 0xBC)});
        return 0u;
    });
    // CGoal's own and the goal manager.
    s.At({0x00556FE0}, 4, [](Regs& r) {
        Rec(0x6FE0, {r.ecx, r.args[0]});
        *reinterpret_cast<uint32_t*>(r.ecx + 0x1C) = r.args[0];
        return r.ecx;
    });
    auto simple = [&](uint32_t address, uint16_t pop, uint32_t tag, std::function<uint32_t(Regs&)> result) {
        s.At({address}, pop, [tag, pop, result](Regs& r) {
            g_trace.push_back(tag);
            g_trace.push_back(Rel(r.ecx));
            for (int i = 0; i < pop / 4; ++i)
                g_trace.push_back(Rel(r.args[i]));
            return result(r);
        });
    };
    auto zero = [](Regs&) { return 0u; };
    simple(0x00556110, 0, 0x6110, zero);
    simple(0x00554280, 0, 0x4280, [](Regs& r) { return 0x60A10000u + ((r.ecx - g_tg_base) & 0xFFFF); });
    simple(0x00554DD0, 0, 0x4DD0, [](Regs&) { return 0x5501u; });
    simple(0x00557360, 0, 0x7360, [](Regs&) { return 0x7301u; });
    simple(0x005576D0, 0, 0x76D0, [](Regs&) { return 0x7601u; });
    simple(0x00557560, 8, 0x7560, zero);
    simple(0x00554A80, 4, 0x4A80, zero);
    simple(0x00554C70, 0, 0x4C70, zero);
    simple(0x005568B0, 4, 0x68B0, [](Regs&) { return static_cast<uint32_t>(g_tg_pick); });
    simple(0x00554110, 0, 0x4110, [](Regs&) { return g_tg_flag554110 ? 1u : 0u; });
    simple(0x00554220, 0, 0x4220, [](Regs&) { return g_tg_flag554220 ? 1u : 0u; });
    simple(0x005541E0, 8, 0x41E0, [](Regs&) { return g_tg_flag5541E0 ? 1u : 0u; });
    s.At({0x005591B0, AddressOf(&Game::GetGoalManager)}, 0, [](Regs&) {
        Rec(0x91B0, {});
        return At(kMisc);
    });
    simple(0x00558030, 4, 0x8030, zero);
    simple(0x00558180, 4, 0x8180, zero);
    simple(0x00557FC0, 8, 0x7FC0, zero);
    // The skater and its score.
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Rec(0x6520, {r.ecx});
        return g_tg_localSkater ? At(g_tg_localSkater) : 0u;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Rec(0x0480, {r.ecx});
        return At(kScore);
    });
    s.At({0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, [](Regs& r) {
        Rec(0x1B40, {r.ecx});
        return *reinterpret_cast<uint32_t*>(r.ecx);
    });
    auto count = [&](uint32_t address, uint32_t tag) {
        s.At({address}, 16, [tag](Regs& r) {
            Rec(tag, {r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3]});
            auto it = g_tg_trickCounts.find(r.args[0] ^ (r.args[1] << 8) ^ (r.args[3] << 16));
            return it == g_tg_trickCounts.end() ? 0u : static_cast<uint32_t>(it->second);
        });
    };
    count(0x005624C0, 0x24C0);
    count(0x00562510, 0x2510);
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Rec(0x4F80, {r.ecx, r.args[0]});
        return At(kMisc + 0x20);
    });
    s.At({0x00586110, AddressOf(&Obj::CSkaterProfile::GetInfoStructure)}, 4, [](Regs& r) {
        Rec(0x6110, {r.ecx, r.args[0]});
        return At(StructAt(40)); // the trick mapping
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return At(kMisc + (r.args[0] == 0x43686585u ? 0x40u : 0x60u)); // the state (+0xE0), the score (+0x18)
    });
    // Random numbers, the clocks.
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Rec(0x3DE0, {r.args[0]});
        return r.args[0] ? (g_tg_random++ * 7u) % r.args[0] : 0u;
    });
    s.At({0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, [](Regs&) {
        Rec(0xECE0, {});
        return static_cast<uint32_t>(g_tg_clock);
    });
    s.Bytes(0x005BE8F0, Branch(0xE9, 0x005BE8F0, reinterpret_cast<void*>(&StandInSlomo)));
    ours = AddressOf(&Tmr::GetSlomo);
    s.Bytes(ours, Branch(0xE9, ours, reinterpret_cast<void*>(&StandInSlomo)));
    s.Bytes(0x005BEAA0, Branch(0xE9, 0x005BEAA0, reinterpret_cast<void*>(&StandInFrameSeconds)));
    ours = AddressOf(&Tmr::GetSlomoFrameSeconds);
    s.Bytes(ours, Branch(0xE9, ours, reinterpret_cast<void*>(&StandInFrameSeconds)));
    // The screen elements.
    s.At({0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 12, [](Regs& r) {
        Rec(0xAE20, {r.ecx, r.args[1], r.args[2]});
        auto* out = reinterpret_cast<Front::ElementPtr*>(r.args[0]);
        auto it = g_tg_elements.find(r.args[1]);
        out->mp_object = nullptr;
        out->mp_prev = nullptr;
        out->mp_next = nullptr;
        if (it != g_tg_elements.end())
            out->Set(Ptr<Front::CScreenElement>(it->second));
        return r.args[0];
    });
    s.At({0x004A8630, AddressOf(&Front::CScreenElement::SetAlpha)}, 8, [](Regs& r) {
        Rec(0x8630, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004A8940, AddressOf(&Front::CScreenElement::SetFocusSent)}, 4, [](Regs& r) {
        Rec(0x8940, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004ABBE0, AddressOf(&Front::CScreenElementManager::SetParent)}, 12, [](Regs& r) {
        Rec(0xBBE0, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
        return 0u;
    });
    s.At({0x004AD100, AddressOf(&Front::CScreenElementManager::DestroyElement)}, 16, [](Regs& r) {
        Rec(0xD100, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    auto element = [&](uint32_t address, uint32_t ours, uint32_t tag) {
        s.At({address, ours}, 0, [tag](Regs& r) {
            Rec(tag, {r.ecx});
            *reinterpret_cast<void***>(r.ecx) = g_tg_elementVtable;
            *reinterpret_cast<uint32_t*>(r.ecx + 4) = 0; // (no smart pointers)
            uint32_t id = 0x7000u + g_tg_nextId++;
            *reinterpret_cast<uint32_t*>(r.ecx + 0x14) = id;
            g_tg_elements[id] = r.ecx - g_tg_base;
            return r.ecx;
        });
    };
    element(0x004AAC20, AddressOf(&Front::CContainerElement::Construct), 0xAC20);
    element(0x004B0920, AddressOf(&Front::CTextElement::Construct), 0x0920);
    s.At({0x00461930, AddressOf(&Obj::CObject::SetID)}, 4, [](Regs& r) {
        Rec(0x1930, {r.ecx, r.args[0]});
        return 0u;
    });
    // Sounds (the volume structure is the caller's local).
    s.At({0x0047B030, AddressOf(&Sfx::sVolume::Init)}, 0, [](Regs& r) {
        g_tg_volume = r.ecx; // (a local of the caller's: only whether the same one is used is compared)
        Rec(0xB030, {});
        auto* volume = reinterpret_cast<float*>(r.ecx);
        for (int i = 0; i < 5; ++i)
            volume[i] = 50.0f + i;
        return r.ecx;
    });
    s.At({0x0047AD60, AddressOf(&Sfx::sVolume::SetSilent)}, 0, [](Regs& r) {
        Rec(0xAD60, {r.ecx == g_tg_volume});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x14);
        return 0u;
    });
    s.At({0x0047BD20, AddressOf(&Sfx::CSfxManager::PlaySound)}, 0x18, [](Regs& r) {
        auto* volume = reinterpret_cast<uint32_t*>(r.args[1]);
        Rec(0xBD20, {r.ecx, r.args[0], r.args[1] == g_tg_volume, volume[0], volume[1], volume[2], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0u;
    });
    // Memory, the C runtime.
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1]});
        uint32_t block = At(g_tg_heapNext);
        g_tg_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.args[0]});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
}

void SaveGlobals()
{
    g_tg_saved = {{0x007CE478, 4, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x006F3614, 16, {}},
                  {0x006F22B4, 16, {}}, {0x00701440, 4, {}}, {0x00696D60, 4, {}}, {0x006F85A8, 4, {}}};
    SaveAll(g_tg_saved);
}

// Texts (C strings) in the arena, by offset.
uint32_t g_tg_textNext;
uint32_t Text(const char* text)
{
    uint32_t at = g_tg_textNext;
    std::memcpy(Ptr<char>(at), text, std::strlen(text) + 1);
    g_tg_textNext += static_cast<uint32_t>(std::strlen(text) + 4) & ~3u;
    return at;
}

void SetUpArena(int run)
{
    uint8_t* arena = g_tg_storage.Get()[run];
    g_tg_base = reinterpret_cast<uint32_t>(arena);
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + kHeap, 0xCD, kArenaSize - kHeap);
    g_tg_heapNext = kHeap;
    g_tg_textNext = 0x1C00;
    g_trace.clear();
    g_tg_answers.clear();
    g_tg_elements.clear();
    g_tg_trickCounts.clear();
    g_tg_globals.clear();
    g_tg_floats.clear();
    g_tg_random = 0;
    g_tg_pick = 0;
    g_tg_active = true;
    g_tg_paused = false;
    g_tg_locked = false;
    g_tg_flag554110 = g_tg_flag554220 = g_tg_flag5541E0 = false;
    g_tg_localSkater = kSkater;
    g_tg_nextId = 0;
    g_tg_clock = 1000;
    g_tg_slomo = 1.0f;
    g_tg_frameSeconds = 1.0 / 60.0;
    GameVariable<uint8_t*>(0x007CE478) = Ptr<uint8_t>(kMisc + 0x80);
    GameVariable<void*>(0x007D1800) = Ptr<void>(kMisc + 0x100);
    GameVariable<bool>(0x007CC979) = true;
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16);
    GameVariable<void*>(0x00701440) = Ptr<void>(kManager);
    *Ptr<void**>(kManager) = g_tg_managerVtable;
    GameVariable<int>(0x00696D60) = 25;
    GameVariable<void*>(0x006F85A8) = Ptr<void>(kMisc + 0x140);
    g_tg_floats[0x855E6DA4] = 0.25f;
    g_tg_floats[0xDBE5B19E] = 1.0f;
    g_tg_floats[0x465128A1] = 0.5f;
    g_tg_floats[0xA02433CB] = 0.75f;
}

void PushArena(std::vector<uint32_t>& out)
{
    const auto* words = reinterpret_cast<const uint32_t*>(g_tg_base);
    for (uint32_t i = 0; i < kArenaSize / 4; ++i)
        out.push_back(Rel(words[i]));
}

// Where two runs' calls part: the last mark before the first difference.
void LogLastMark(const char* what)
{
    const std::vector<uint32_t>& a = g_tg_calls[0];
    const std::vector<uint32_t>& b = g_tg_calls[1];
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

// Names used by the tests.
constexpr uint32_t kMaxTricks = 0x89473DB7, kCombo = 0x4EC3CFB5, kSingleCombo = 0xDF47B144, kTrickTris = 0xEA7BA666;
constexpr uint32_t kTrickTime = 0xDED8540A, kCurrentTrickTime = 0x648D8D2A, kTricksCompleted = 0x8D958F01;
constexpr uint32_t kTetrisKeyCombos = 0x7BE1E689, kTetrisTricks = 0xEB79FB49, kTrick = 0x270F56E1, kGrind = 0x255ED86F;
constexpr uint32_t kText = 0xC4745838, kKeyCombo = 0xACFDB27A, kRandomSpin = 0x8C8ABD19, kSpin = 0xEDF5DB70;
constexpr uint32_t kPerfect = 0x1C39F1B9, kNumTaps = 0xA4BEE6A1, kParams = 0x7031F10C, kExtraTricks = 0x6E855102;
constexpr uint32_t kName = 0xA1DC81F9, kComboSize = 0x67DD90CA, kWaitToAdd = 0x7824963C, kTimeToStop = 0xA6D96603;
constexpr uint32_t kTotalToWinTT = 0xBFD3A831, kBlockSizeTT = 0x45A375C9, kNoAlphaCheck = 0x60B827D5;
constexpr uint32_t kAccInterval = 0x01181B29, kAccPercent = 0x076F65C8, kRedTricks = 0xD02AC196;
constexpr uint32_t kTestingGoal = 0x54D3CAC1, kTetrisMenu = 0xE35EC715, kBeatMenu = 0xBF7DF8D6;
constexpr uint32_t kTrickText = 0x8856C817, kTrickTextDouble = 0xE7ABFB95;
constexpr uint32_t kBeatPattern = 0x407CCE70, kTempo = 0x8B4A1490, kBeatWindow = 0x5CCDD89D, kTotalToWin = 0xD11D2F91;
constexpr uint32_t kSounds = 0x0EDCF90E, kVol = 0xF6A36814, kPitch = 0xD8604126, kPulse = 0xE4E93525;
constexpr uint32_t kBlockSize = 0xCE5189EE, kBeatKeyCombos = 0x548B4A38, kBeatTricks = 0x09381808;
constexpr uint32_t kRequiredScore = 0x1B35D319, kGoalObjects = 0x3CCE31F0, kClusterId = 0x3C95CCD7, kFlag = 0x2E0B1465;

// A trick's count key (the stand-in TrickCount / KeyComboCount: trick ^ spin << 8 ^ taps << 16).
uint32_t CountKey(uint32_t trick, int spin, int taps) { return trick ^ (spin << 8) ^ (taps << 16); }

// The goal's tricks (random ones): structures 10..13 in array 0, some grinds, some with a key combo.
void TetrisTricks(uint32_t params)
{
    const char* names[] = {"Kickflip", "50-50", "Heelflip", "Boardslide"};
    for (int i = 0; i < 4; ++i) {
        uint32_t trick = StructAt(10 + i);
        Answer('T', trick, kTrick, {Text(names[i])});
        Answer('T', trick, kText, {Text(i == 2 ? "A Heelflip" : names[i])});
        if (i == 1 || i == 3)
            Flag(trick, kGrind);
        if (i == 2)
            Answer('K', trick, kKeyCombo, {0xC0B0u});
        if (i == 3)
            Answer('T', trick, kKeyCombo, {Text("Up + Grind")});
    }
    MakeArray(0, {StructAt(10), StructAt(11), StructAt(12), StructAt(13)}, true);
    Answer('A', params, kTetrisTricks, {ArrayAt(0)});
}
// Key combos: array 1 of structures 16..19 (random_spin, spin, perfect, num_taps), array 2 of names; the trick
// mapping (structure 40): two by trick name, one by a special slot; the tricks' global structures and ExtraTricks.
void KeyCombos()
{
    Answer('K', StructAt(16), kKeyCombo, {0xA001u});
    Int(StructAt(16), kRandomSpin, 200);
    Answer('K', StructAt(17), kKeyCombo, {0xA002u});
    Int(StructAt(17), kSpin, 540);
    Flag(StructAt(17), kPerfect);
    Int(StructAt(17), kNumTaps, 2);
    Answer('K', StructAt(18), kKeyCombo, {0xA003u});
    Int(StructAt(18), kRandomSpin, 900);
    Answer('K', StructAt(19), kKeyCombo, {0xA004u});
    Int(StructAt(19), kNumTaps, 3);
    MakeArray(1, {StructAt(16), StructAt(17), StructAt(18), StructAt(19)}, true);
    MakeArray(2, {0xA001u, 0xA003u, 0xA004u}, false, Script::ESYMBOLTYPE_NAME);
    uint32_t mapping = StructAt(40);
    Answer('K', mapping, 0xA001u, {0xB001u});
    Answer('K', mapping, 0xA002u, {0xB002u});
    Int(mapping, 0xA003u, 1); // special slot 1
    Answer('K', mapping, 0xA004u, {0xB004u});
    // The skater's special slot 1.
    *Ptr<uint32_t>(kSkater + 0x17C + 4) = At(kSpecials);
    *Ptr<uint32_t>(kSpecials + 4) = At(StructAt(41));
    Answer('T', StructAt(41), kName, {Text("Special One")});
    // Trick B001: a global structure; B002: a global array (and ExtraTricks to an array, then none); B004: ExtraTricks
    // missing.
    g_tg_globals[0xB001u] = StructAt(42);
    Answer('S', StructAt(42), kParams, {StructAt(43)});
    Answer('T', StructAt(43), kName, {Text("Trick One")});
    MakeArray(3, {StructAt(44)}, true);
    g_tg_globals[0xB002u] = ArrayAt(3);
    Answer('S', StructAt(44), kParams, {StructAt(45)});
    Answer('K', StructAt(45), kExtraTricks, {0xB003u});
    Answer('T', StructAt(45), kName, {Text("Trick Two")});
    MakeArray(4, {StructAt(46)}, true);
    g_tg_globals[0xB003u] = ArrayAt(4);
    Answer('S', StructAt(46), kParams, {StructAt(47)});
    Answer('T', StructAt(47), kName, {Text("Trick Two Extra")});
    g_tg_globals[0xB004u] = StructAt(48);
    Answer('S', StructAt(48), kParams, {StructAt(49)});
    Answer('T', StructAt(49), kName, {Text("Trick Four")});
    // The key combos' texts.
    g_tg_globals[kTrickText] = StructAt(50);
    g_tg_globals[kTrickTextDouble] = StructAt(51);
    Answer('T', StructAt(50), 0xA001u, {Text("L1")});
    Answer('T', StructAt(50), 0xA003u, {Text("R2")});
    Answer('T', StructAt(51), 0xA002u, {Text("L1 L1")});
    Answer('T', StructAt(50), 0xC0B0u, {Text("Sq")});
}

using Game::CTetrisGoal;
using Game::CTrickBeatGoal;

template <typename R, typename G, typename... A>
R Call(bool original, uint32_t address, G* goal, R (G::*ours)(A...), A... args)
{
    if (original)
        return Original<R(__thiscall*)(G*, A...)>(address)(goal, args...);
    return (goal->*ours)(args...);
}

// Skatetris: the constructor and destructors; Init / ClearTricks / ClearTricksAndMenu / AllFree; the variants'
// checks; AddTricks (random tricks and grinds, key combos by name and by structure - spins, perfect, taps, the trick
// mapping by name and by special slot, ExtraTricks found and missing -, no pick, the stack overflowing, combo groups,
// red tricks); UpdateAlphas; RemoveTrick (acceleration, red tricks back); CheckTricks / CheckCombos; Update in the
// plain, combo, single combo and TrickTris variants over many frames; Win, Expire, Activate, Deactivate.
void TestTetrisGoal()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        auto* goal = Ptr<CTetrisGoal>(kTetris);
        uint32_t params = StructAt(1);
        MakeElement(0, kTetrisMenu);
        Mark(1);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x0056F090, goal, &CTetrisGoal::Construct, Ptr<CStruct>(params)))));
        Call(original, 0x0056F0B0, goal, &CTetrisGoal::DestroyBody);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x0056F290, goal, &CTetrisGoal::Destroy, uint8_t(0)))));
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x0056F290, goal, &CTetrisGoal::Destroy, uint8_t(1)))));
        goal->m_vtable = g_tg_goalVtable;
        goal->m_params = Ptr<CStruct>(params);
        Int(params, kMaxTricks, 14);
        Int(params, kTrickTime, 900);
        Int(params, kComboSize, 2);
        TetrisTricks(params);
        KeyCombos();
        Mark(2);
        auto variants = [&]() {
            t.push_back(Call(original, 0x0056F1A0, goal, &CTetrisGoal::IsCombo));
            t.push_back(Call(original, 0x0056F1F0, goal, &CTetrisGoal::IsSingleCombo));
            t.push_back(Call(original, 0x0056F240, goal, &CTetrisGoal::IsTrickTris));
            t.push_back(Call(original, 0x0056F180, goal, &CTetrisGoal::AllFree));
        };
        variants();
        Int(params, kCombo, 1);
        Flag(params, kSingleCombo);
        variants();
        Int(params, kCombo, 0);
        g_tg_answers.erase({'L', params, kSingleCombo});
        Flag(params, kTrickTris);
        Int(params, kSingleCombo, 3);
        variants();
        g_tg_answers.erase({'L', params, kTrickTris});
        Int(params, kSingleCombo, 0);
        Mark(3);
        Call(original, 0x0056F2E0, goal, &CTetrisGoal::Init);
        variants();
        // Random tricks, then key combos (by structure, then by name), then none picked.
        for (int n : {1, 2, 3}) {
            Call(original, 0x0056FA30, goal, &CTetrisGoal::AddTricks, n);
            Mark(0x10 + n);
        }
        variants();
        Int(params, kMaxTricks, 12); // (red from 9 on: the third key combo is the ninth trick)
        Answer('A', params, kTetrisKeyCombos, {ArrayAt(1)});
        for (int pick : {0, 1, 2, 3}) {
            g_tg_pick = pick;
            g_tg_localSkater = pick == 3 ? 0u : kSkater;
            Call(original, 0x0056FA30, goal, &CTetrisGoal::AddTricks, 1);
            Mark(0x20 + pick);
        }
        g_tg_localSkater = kSkater;
        // The stack overflows: lost.
        Call(original, 0x0056FA30, goal, &CTetrisGoal::AddTricks, 5);
        Mark(0x30);
        Int(params, kMaxTricks, 13); // (exactly full: not lost)
        Call(original, 0x0056FA30, goal, &CTetrisGoal::AddTricks, 3);
        Int(params, kMaxTricks, 14);
        Answer('A', params, kTetrisKeyCombos, {ArrayAt(2)});
        g_tg_pick = 1;
        Call(original, 0x0056FA30, goal, &CTetrisGoal::AddTricks, 1);
        g_tg_pick = -1;
        Call(original, 0x0056FA30, goal, &CTetrisGoal::AddTricks, 1);
        Mark(0x31);
        // Alphas, with tricks done.
        for (int i = 0; i < CTetrisGoal::kMaxTricks; ++i) {
            const Game::TetrisTrick& trick = goal->m_tricks[i];
            if (!trick.free && (i % 2 == 0))
                g_tg_trickCounts[CountKey(trick.keyCombo ? trick.keyCombo : (i % 4 ? trick.trick2 : trick.trick),
                                          trick.spin, trick.keyCombo ? trick.taps : 1)] = 1;
        }
        Call(original, 0x0056F3B0, goal, &CTetrisGoal::UpdateAlphas);
        Int(params, kNoAlphaCheck, 0);
        Call(original, 0x0056F3B0, goal, &CTetrisGoal::UpdateAlphas);
        Int(params, kNoAlphaCheck, 5);
        Call(original, 0x0056F3B0, goal, &CTetrisGoal::UpdateAlphas);
        Mark(0x40);
        // Tricks done and removed (acceleration every 2; red tricks back).
        Int(params, kAccInterval, 2);
        Answer('F', params, kAccPercent, {Bits(0.25f)});
        Int(params, kRedTricks, 1);
        Int(params, kNoAlphaCheck, 0);
        Call(original, 0x00570F60, goal, &CTetrisGoal::CheckTricks);
        Int(params, kNoAlphaCheck, 1);
        Call(original, 0x00570F60, goal, &CTetrisGoal::CheckTricks);
        Mark(0x41);
        t.insert(t.end(), reinterpret_cast<uint32_t*>(goal), reinterpret_cast<uint32_t*>(goal) + sizeof(CTetrisGoal) / 4);
        for (uint32_t& w : t)
            w = Rel(w);
        // Combo groups: add in combo mode, then check.
        Int(params, kCombo, 1);
        Call(original, 0x0056F2E0, goal, &CTetrisGoal::Init);
        g_tg_answers.erase({'A', params, kTetrisKeyCombos});
        for (int n : {2, 2, 1})
            Call(original, 0x0056FA30, goal, &CTetrisGoal::AddTricks, n);
        for (int i = 0; i < CTetrisGoal::kMaxTricks; ++i) {
            const Game::TetrisTrick& trick = goal->m_tricks[i];
            if (!trick.free && trick.group != 1)
                g_tg_trickCounts[CountKey(trick.keyCombo ? trick.keyCombo : trick.trick, trick.spin, trick.taps)] = 2;
        }
        Call(original, 0x00570F60, goal, &CTetrisGoal::CheckTricks);
        Flag(params, kSingleCombo);
        Call(original, 0x00570A70, goal, &CTetrisGoal::CheckCombos);
        g_tg_answers.erase({'L', params, kSingleCombo});
        Mark(0x50);
        for (uint32_t i = 0; i < sizeof(CTetrisGoal) / 4; ++i)
            t.push_back(Rel(reinterpret_cast<uint32_t*>(goal)[i]));
        // Update over frames in each variant.
        struct Variant {
            int combo, single, trickTris;
        };
        for (Variant v : {Variant{0, 0, 0}, Variant{1, 0, 0}, Variant{0, 1, 0}, Variant{0, 0, 1}}) {
            Int(params, kCombo, v.combo);
            Int(params, kSingleCombo, v.single);
            Int(params, kTrickTris, v.trickTris);
            Int(params, kTimeToStop, 3);
            Int(params, kTotalToWinTT, 6);
            Int(params, kBlockSizeTT, 3);
            Call(original, 0x0056F2E0, goal, &CTetrisGoal::Init);
            g_tg_trickCounts.clear();
            for (int frame = 0; frame < 40; ++frame) {
                Int(params, kWaitToAdd, frame == 1 ? 1 : 0);
                g_tg_active = frame != 2;
                g_tg_paused = frame == 3;
                goal->m_timeLeft = frame == 30 ? 0 : (frame == 31 ? 2500 : 60000 - frame * 1000);
                goal->m_flags = frame == 32 ? 0x10 : 0;
                g_tg_frameSeconds = 1.0 / (20 + frame % 5);
                if (frame == 20)
                    Int(params, kTricksCompleted, 6); // (TrickTris: exactly the total)
                t.push_back(Call(original, 0x00570C40, goal, &CTetrisGoal::Update));
                Mark(0x100 + frame);
            }
        }
        // The interval reached exactly: no trick yet.
        Int(params, kCombo, 0);
        Int(params, kSingleCombo, 0);
        Int(params, kTrickTris, 0);
        Int(params, kWaitToAdd, 0);
        g_tg_active = true;
        g_tg_paused = false;
        goal->m_timeLeft = 60000;
        goal->m_flags = 0;
        Int(params, kCurrentTrickTime, 900);
        goal->m_timer = 850;
        g_tg_frameSeconds = 1.0 / 20;
        t.push_back(Call(original, 0x00570C40, goal, &CTetrisGoal::Update));
        t.push_back(static_cast<uint32_t>(goal->m_numTricks));
        t.push_back(static_cast<uint32_t>(goal->m_timer));
        // Win, Expire, Activate, Deactivate, ClearTricksAndMenu.
        g_tg_active = true;
        Flag(params, kTestingGoal);
        t.push_back(Call(original, 0x0056F0C0, goal, &CTetrisGoal::Win));
        t.push_back(Call(original, 0x0056F0C0, goal, &CTetrisGoal::Win));
        for (int variant = 0; variant < 4; ++variant) {
            Int(params, kSingleCombo, variant == 0);
            Int(params, kTrickTris, variant == 1);
            goal->m_state = variant == 3 ? 2 : 1;
            if (variant == 2)
                Call(original, 0x0056F2E0, goal, &CTetrisGoal::Init);
            else
                goal->m_tricks[5].free = false;
            Call(original, 0x0056F500, goal, &CTetrisGoal::Expire);
        }
        for (bool active : {true, false}) {
            g_tg_active = active;
            t.push_back(Call(original, 0x0056F610, goal, &CTetrisGoal::Activate));
        }
        Call(original, 0x0056F2C0, goal, &CTetrisGoal::Deactivate, 7, 9);
        Call(original, 0x0056F5B0, goal, &CTetrisGoal::ClearTricksAndMenu);
        Call(original, 0x0056F130, goal, &CTetrisGoal::ClearTricks);
        Mark(0x60);
        g_tg_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_tg_calls[0], g_tg_calls[1], "tetris goal (calls)");
    LogLastMark("tetris goal");
    Compare(traces[0], traces[1], "tetris goal");
}

// TrickBeat: the constructor (pattern and tempo) and destructors; SetBeatPattern; Init / ClearTricks; AddTricks
// (random, key combos); the beat over many frames (sounds with and without vol / pitch, slow motion, the window
// opening on pulse beats and closing), tricks landed in and out of the window, ClearLanded, PulseTricks,
// RemoveLandedTricks; Update (waiting, winning, paused, time out, block size); Activate, Deactivate.
void TestTrickBeatGoal()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        auto* goal = Ptr<CTrickBeatGoal>(kBeat);
        uint32_t params = StructAt(2);
        MakeElement(1, kBeatMenu);
        // The beat pattern: 16 steps (array 5) of structures 20..23 (sounds arrays 6, 7; pulse).
        std::vector<uint32_t> steps;
        for (int i = 0; i < 16; ++i)
            steps.push_back(StructAt(20 + i % 4));
        MakeArray(5, steps, true);
        MakeArray(6, {StructAt(24), StructAt(25)}, true);
        MakeArray(7, {StructAt(26)}, true);
        Answer('A', StructAt(20), kSounds, {ArrayAt(6)});
        Answer('A', StructAt(22), kSounds, {ArrayAt(7)});
        Flag(StructAt(21), kPulse);
        Flag(StructAt(23), kPulse);
        Answer('K', StructAt(24), 0, {0x50001u});
        Answer('F', StructAt(24), kVol, {Bits(80.0f)});
        Answer('K', StructAt(25), 0, {0x50002u});
        Answer('F', StructAt(25), kPitch, {Bits(120.0f)});
        Answer('K', StructAt(26), 0, {0x50003u});
        Answer('A', params, kBeatPattern, {ArrayAt(5)});
        Int(params, kTempo, 120);
        Mark(1);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x00574C40, goal, &CTrickBeatGoal::Construct, Ptr<CStruct>(params)))));
        Call(original, 0x00574960, goal, &CTrickBeatGoal::SetBeatPattern, Ptr<CArray>(ArrayAt(5)), 95);
        t.push_back(Bits(goal->m_beatLength));
        goal->m_pattern = Ptr<CArray>(ArrayAt(5));
        goal->m_vtable = g_tg_goalVtable;
        Int(params, kBeatWindow, 301);
        Int(params, kTotalToWin, 9);
        Int(params, kBlockSize, 4);
        // Random tricks (array 8 of structures 30..32), key combos.
        const char* names[] = {"Ollie", "Smith", "Kickflip"};
        for (int i = 0; i < 3; ++i) {
            Answer('T', StructAt(30 + i), kTrick, {Text(names[i])});
            Answer('T', StructAt(30 + i), kText, {Text(names[i])});
        }
        Flag(StructAt(31), kGrind);
        Answer('T', StructAt(32), kKeyCombo, {Text("Up Up")});
        MakeArray(8, {StructAt(30), StructAt(31), StructAt(32)}, true);
        Answer('A', params, kBeatTricks, {ArrayAt(8)});
        KeyCombos();
        Mark(2);
        t.push_back(Call(original, 0x00574D50, goal, &CTrickBeatGoal::Activate));
        Call(original, 0x00574A10, goal, &CTrickBeatGoal::Init);
        Call(original, 0x00575450, goal, &CTrickBeatGoal::AddTricks, 3);
        Answer('A', params, kBeatKeyCombos, {ArrayAt(1)});
        for (int pick : {0, 1, 2, -1}) {
            g_tg_pick = pick;
            Call(original, 0x00575450, goal, &CTrickBeatGoal::AddTricks, 1);
        }
        g_tg_answers.erase({'A', params, kBeatKeyCombos});
        Mark(3);
        for (uint32_t i = 0; i < sizeof(CTrickBeatGoal) / 4; ++i)
            t.push_back(Rel(reinterpret_cast<uint32_t*>(goal)[i]));
        // Frames of the beat; tricks landed now and then.
        for (int frame = 0; frame < 60; ++frame) {
            g_tg_clock += 37 + frame % 13;
            g_tg_slomo = frame >= 40 ? 0.5f : 1.0f;
            Call(original, 0x005751C0, goal, &CTrickBeatGoal::UpdateBeat);
            if (frame % 5 == 0) {
                uint32_t trick = StructAt(36);
                Answer('S', trick, kParams, {StructAt(37)});
                Answer('T', StructAt(37), kName, {Text(frame % 10 ? "Ollie" : "Kickflip")});
                Call(original, 0x00574AB0, goal, &CTrickBeatGoal::TrickLanded, Ptr<CStruct>(trick));
                Call(original, 0x00574AB0, goal, &CTrickBeatGoal::TrickLanded, Ptr<CStruct>(trick)); // (landed already)
            }
            if (frame % 7 == 0)
                Call(original, 0x00574B90, goal, &CTrickBeatGoal::ClearLanded);
            if (frame % 9 == 0)
                Call(original, 0x005761D0, goal, &CTrickBeatGoal::RemoveLandedTricks);
            t.insert(t.end(), {static_cast<uint32_t>(goal->m_beat), static_cast<uint32_t>(goal->m_beatTime),
                               goal->m_inWindow, goal->m_pulse});
            Mark(0x100 + frame);
        }
        for (uint32_t i = 0; i < sizeof(CTrickBeatGoal) / 4; ++i)
            t.push_back(Rel(reinterpret_cast<uint32_t*>(goal)[i]));
        goal->m_beatTime = -5; // (unsigned in the comparison)
        Call(original, 0x005751C0, goal, &CTrickBeatGoal::UpdateBeat);
        Call(original, 0x00574D80, goal, &CTrickBeatGoal::PulseTricks);
        Call(original, 0x00574C20, goal, &CTrickBeatGoal::ResetClock);
        Mark(4);
        // Update.
        for (int frame = 0; frame < 30; ++frame) {
            Int(params, kWaitToAdd, frame == 1 ? 1 : 0);
            g_tg_active = frame != 2;
            g_tg_paused = frame == 3;
            goal->m_timeLeft = frame == 10 || frame == 12 ? 0 : 50000;
            goal->m_flags = frame == 11 || frame == 12 ? 0x10 : 0;
            if (frame == 20)
                goal->m_completed = 9;
            g_tg_clock += 101;
            t.push_back(Call(original, 0x00576240, goal, &CTrickBeatGoal::Update));
            Mark(0x200 + frame);
        }
        Call(original, 0x005749F0, goal, &CTrickBeatGoal::Deactivate, 3, 4);
        Call(original, 0x00574990, goal, &CTrickBeatGoal::ClearTricks);
        Call(original, 0x00574F00, goal, &CTrickBeatGoal::RemoveTrick, 2);
        // The destructors (on the copy of the pattern made by the constructor).
        goal->m_pattern = Ptr<CArray>(ArrayAt(9));
        Call(original, 0x00575140, goal, &CTrickBeatGoal::DestroyBody);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x00576210, goal, &CTrickBeatGoal::Destroy, uint8_t(1)))));
        Mark(5);
        g_tg_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_tg_calls[0], g_tg_calls[1], "trick beat goal (calls)");
    LogLastMark("trick beat goal");
    Compare(traces[0], traces[1], "trick beat goal");
}

// TimedScoreRun and TrickObjects: constructors, destructors, IsRunning; CheckScore (not ..., no Required_Score, no
// skater, below / at the score with points pending); GetObjectByCluster (found, not, an entry without cluster_id);
// ObjectHit (no object, locked, 0x00554220, no skater, on the ground without points, in the air, inactive goal, the
// flag already set).
void TestSmallGoals()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        uint32_t params = StructAt(3);
        auto* timed = Ptr<Game::CTimedScoreRunGoal>(kSmall);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x005710B0, timed, &Game::CTimedScoreRunGoal::Construct, Ptr<CStruct>(params)))));
        Call(original, 0x005710D0, timed, &Game::CTimedScoreRunGoal::DestroyBody);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x00571110, timed, &Game::CTimedScoreRunGoal::Destroy, uint8_t(1)))));
        timed->m_vtable = g_tg_goalVtable;
        for (int i = 0; i < 4; ++i) {
            g_tg_active = i & 1;
            g_tg_paused = i & 2;
            t.push_back(static_cast<uint32_t>(Call(original, 0x005710E0, timed, &Game::CTimedScoreRunGoal::IsRunning)));
        }
        auto* objects = Ptr<Game::CTrickObjectsGoal>(kSmall + 0x80);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x005763A0, objects, &Game::CTrickObjectsGoal::Construct, Ptr<CStruct>(params)))));
        Call(original, 0x005763C0, objects, &Game::CTrickObjectsGoal::DestroyBody);
        t.push_back(Rel(reinterpret_cast<uint32_t>(Call(original, 0x005763D0, objects, &Game::CTrickObjectsGoal::Destroy, uint8_t(0)))));
        objects->m_vtable = g_tg_goalVtable;
        objects->m_params = Ptr<CStruct>(params);
        // CheckScore.
        *Ptr<int>(kScore) = 500;
        *Ptr<int>(kScore + 0x14) = 40;
        *Ptr<float>(kScore + 0x60) = 2.5f;
        for (int step = 0; step < 6; ++step) {
            g_tg_flag554110 = step != 0;
            if (step >= 2)
                Int(params, kRequiredScore, step == 4 ? 600 : (step == 5 ? 601 : 100));
            g_tg_localSkater = step == 3 ? 0u : kSkater;
            Call(original, 0x00576400, objects, &Game::CTrickObjectsGoal::CheckScore);
        }
        g_tg_localSkater = kSkater;
        // GetObjectByCluster.
        Answer('K', StructAt(4), kClusterId, {0x111u});
        Answer('K', StructAt(6), kClusterId, {0x333u});
        Answer('K', StructAt(4), kFlag, {0xF1u});
        Answer('K', StructAt(6), kFlag, {0xF3u});
        Answer('K', StructAt(5), kFlag, {0xF2u});
        MakeArray(10, {StructAt(4), StructAt(5), StructAt(6)}, true);
        Answer('A', params, kGoalObjects, {ArrayAt(10)});
        for (uint32_t cluster : {0x111u, 0x333u, 0x222u}) {
            t.push_back(Rel(reinterpret_cast<uint32_t>(
                Call(original, 0x005764B0, objects, &Game::CTrickObjectsGoal::GetObjectByCluster, cluster))));
        }
        MakeArray(11, {StructAt(5), StructAt(6)}, true); // (the first entry without a cluster_id: matches)
        Answer('A', params, kGoalObjects, {ArrayAt(11)});
        t.push_back(Rel(reinterpret_cast<uint32_t>(
            Call(original, 0x005764B0, objects, &Game::CTrickObjectsGoal::GetObjectByCluster, 0x444u))));
        Answer('A', params, kGoalObjects, {ArrayAt(10)});
        // ObjectHit.
        for (int step = 0; step < 9; ++step) {
            g_tg_locked = step == 1;
            g_tg_flag554220 = step == 2;
            g_tg_localSkater = step == 3 ? 0u : kSkater;
            *Ptr<uint8_t>(kMisc + 0x40 + 0xE0) = step >= 5;  // in the air
            *Ptr<uint32_t>(kMisc + 0x60 + 0x18) = At(kScore);
            *Ptr<int>(kScore + 0x14) = step == 4 ? 0 : 3;
            g_tg_active = step != 6;
            g_tg_flag5541E0 = step == 7;
            Call(original, 0x00576520, objects, &Game::CTrickObjectsGoal::ObjectHit, step == 0 ? 0x999u : 0x333u);
        }
        g_tg_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_tg_calls[0], g_tg_calls[1], "small goals (calls)");
    Compare(traces[0], traces[1], "small goals");
}

} // namespace

SELFTEST(Early, TestTetrisGoal, Cleanup);
SELFTEST(Early, TestTrickBeatGoal, Cleanup);
SELFTEST(Early, TestSmallGoals, Cleanup);
