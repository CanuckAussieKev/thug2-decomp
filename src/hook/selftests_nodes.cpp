// Self-tests for SkateScript's nodes (src/game/script/nodes.cpp, node_prefixes.cpp and the node manager's hash in
// node_manager.cpp): the original and ours run the same calls on identical stand-in nodes, node arrays and hash
// tables in an arena. What they call outside the batch (the structure and array functions, Lst::HashTable, the
// memory manager, the checksum names, _stricmp, quaternion to matrix) is stood in for by recording code written
// over the game's function and ours alike; the traces, return values and the whole arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/hashtable.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cache.h"
#include "game/script/parse.h"
#include "game/script/names.h"
#include "game/script/node_manager.h"
#include "game/script/node_prefixes.h"
#include "game/script/nodes.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CArray;
using Script::CStruct;
using SkateScript::CNodeManager;

constexpr uint32_t kArenaSize = 0x40000;
constexpr uint32_t kManagerAt = 0x20000; // (the node manager is 0x100FC bytes)
constexpr uint32_t kBumpStart = 0x8000;
alignas(16) uint8_t g_nodes_arenas[2][kArenaSize];
uint32_t g_nodes_base;
uint32_t g_nodes_bump;
bool g_nodes_original;
std::vector<Saved> g_nodes_saved;
std::map<std::tuple<char, uint32_t, uint32_t>, std::vector<uint32_t>> g_nodes_answers;
std::map<uint32_t, uint32_t> g_nodes_arrays;      // the stand-in global arrays (name -> array)
std::map<uint32_t, std::string> g_nodes_names;    // the stand-in checksum names (checksum -> text)
std::map<uint32_t, uint32_t> g_nodes_scripts;     // the stand-in script cache (name -> code)
std::map<uint32_t, uint32_t> g_nodes_symbols;     // the stand-in symbols (name -> type)
std::map<uint32_t, std::pair<uint32_t, uint32_t>> g_nodes_scanAnswers; // a first argument -> (name, value) it gives
void* g_nodes_callback;                           // the stand-in ForEachScriptCall callback
uint32_t g_nodes_feelerHit;                       // what the stand-in feeler's smart pointer is made to hold
uint32_t g_nodes_collisions;                      // the stand-in feeler's answers (one bit each)

constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kLinks = 0x2E7D5EE7;
constexpr uint32_t kClass = 0x12B4E660;
constexpr uint32_t kLink = 0xC953660E;
constexpr uint32_t kPos = 0x7F261953;
constexpr uint32_t kPosition = 0xB9D31B0A;
constexpr uint32_t kAngles = 0x9D2D0915;
constexpr uint32_t kOrientation = 0xC97F3AA9;

bool InArena(uint32_t word) { return word >= g_nodes_base && word <= g_nodes_base + kArenaSize; }
uint32_t Rel(uint32_t word) { return InArena(word) ? 0xA0000000u + (word - g_nodes_base) : word; }
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
void Result(uint32_t value) { Rec(0x2E5, {value}); }
uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
uint32_t Bump(uint32_t size, uint8_t fill)
{
    uint32_t block = g_nodes_bump;
    g_nodes_bump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), fill, size);
    return block;
}
// A stand-in structure's answer: kind 'K' checksum, 'V' vector (4 words), 'A' array.
void Answer(char kind, const void* structure, uint32_t name, std::vector<uint32_t> value)
{
    g_nodes_answers[{kind, Rel(structure), name}] = std::move(value);
}
const std::vector<uint32_t>* Lookup(char kind, uint32_t structure, uint32_t name)
{
    auto it = g_nodes_answers.find({kind, Rel(structure), name});
    return it == g_nodes_answers.end() ? nullptr : &it->second;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_nodes_saved);
    g_nodes_saved.clear();
}

uint32_t StandInIterateNext(Lst::HashTable* t)
{
    Lst::HashTable::Item* item = t->m_iterItem;
    if (item && item->next) {
        t->m_iterItem = item->next;
        return 1;
    }
    int count = 1 << t->m_bits;
    for (int b = t->m_iterBucket + 1; b < count; ++b) {
        if (t->m_buckets[b].value) {
            t->m_iterBucket = b;
            t->m_iterItem = &t->m_buckets[b];
            return 1;
        }
    }
    t->m_iterBucket = count;
    t->m_iterItem = nullptr;
    return 0;
}

void InstallStandIns(StandIns& s)
{
    auto getter = [&](std::initializer_list<uint32_t> addresses, char kind, uint32_t tag) {
        s.At(addresses, 12, [kind, tag](Regs& r) {
            Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
            const std::vector<uint32_t>* value = Lookup(kind, r.ecx, r.args[0]);
            if (!value)
                return 0u;
            std::memcpy(reinterpret_cast<void*>(r.args[1]), value->data(), value->size() * 4);
            return 1u;
        });
    };
    getter({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
           'K', 0x6950);
    getter({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(
                            &CStruct::GetVector))},
           'V', 0x6FB0);
    getter({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray))}, 'A',
           0x6860);
    // Arrays: +0x04 the size, the elements from +0x10.
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0] & 0xFFFF];
    });
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        auto it = g_nodes_arrays.find(r.args[0]);
        return it == g_nodes_arrays.end() ? 0u : it->second;
    });
    s.At({0x0042BC40}, 4, [](Regs& r) {
        const auto* q = reinterpret_cast<const uint32_t*>(r.ecx);
        Rec(0xBC40, {q[0], q[1], q[2], q[3], r.args[0]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = 0.5f * static_cast<float>(i) - 2.0f;
        return 0u;
    });
    // The memory manager.
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Rec(0xBAE0, {r.args[0]});
        return Bump(r.args[0], 0xCB);
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Rec(0xBB60, {r.args[0]});
        return 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1CC0, {r.args[0]});
        return Bump(r.args[0], 0);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0], 0xDA);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xB0E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xB110, {r.ecx});
        return 0u;
    });
    // Lst::HashTable (items chained from the bump allocator).
    s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [](Regs& r) {
        auto* t = reinterpret_cast<Lst::HashTable*>(r.ecx);
        uint32_t key = *reinterpret_cast<uint32_t*>(r.args[0]);
        Rec(0x2740, {r.ecx, key, r.args[1]});
        Lst::HashTable::Item* bucket = &t->m_buckets[key & ((1u << t->m_bits) - 1)];
        if (!bucket->value) {
            bucket->key = key;
            bucket->value = reinterpret_cast<void*>(r.args[1]);
        } else {
            auto* item = reinterpret_cast<Lst::HashTable::Item*>(Bump(12, 0));
            *item = {key, reinterpret_cast<void*>(r.args[1]), bucket->next};
            bucket->next = item;
        }
        ++t->m_size;
        return 1u;
    });
    s.At({0x005B80D0, AddressOf(&Lst::HashTable::FlushItem)}, 4, [](Regs& r) {
        auto* t = reinterpret_cast<Lst::HashTable*>(r.ecx);
        uint32_t key = *reinterpret_cast<uint32_t*>(r.args[0]);
        Rec(0x80D0, {r.ecx, key});
        Lst::HashTable::Item* bucket = &t->m_buckets[key & ((1u << t->m_bits) - 1)];
        if (bucket->key == key && bucket->value) {
            bucket->value = nullptr;
            bucket->key = 0;
        } else {
            for (Lst::HashTable::Item* p = bucket; p->next; p = p->next) {
                if (p->next->key == key) {
                    p->next = p->next->next;
                    break;
                }
            }
        }
        return 0u;
    });
    s.At({0x0049FD30, AddressOf(&Lst::HashTable::FlushAllItems)}, 0, [](Regs& r) {
        auto* t = reinterpret_cast<Lst::HashTable*>(r.ecx);
        Rec(0xFD30, {r.ecx});
        std::memset(t->m_buckets, 0, 12u << t->m_bits);
        return 0u;
    });
    s.At({0x00588190, AddressOf(&Lst::HashTable::IterateNext)}, 0, [](Regs& r) {
        Rec(0x8190, {r.ecx});
        return StandInIterateNext(reinterpret_cast<Lst::HashTable*>(r.ecx));
    });
    // Checksum names, _stricmp.
    s.At({0x0046F9B0, AddressOf(&Script::FindScriptChecksumName)}, 0, [](Regs& r) {
        Rec(0xF9B0, {r.args[0]});
        auto it = g_nodes_names.find(r.args[0]);
        return it == g_nodes_names.end() ? 0u : reinterpret_cast<uint32_t>(it->second.c_str());
    });
    s.At({0x0046CF60, AddressOf(&Script::AddChecksumName)}, 0, [](Regs& r) {
        Rec(0xCF60, {r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]),
                                              reinterpret_cast<const char*>(r.args[1])));
    });
    // --- the scan of scripts: the cache, symbols, argument parsing, structures, the callback ---
    s.At({0x00475E80, AddressOf(&Script::CScriptCache::GetScript)}, 4, [](Regs& r) {
        Rec(0x5E80, {r.ecx, r.args[0]});
        auto it = g_nodes_scripts.find(r.args[0]);
        return it == g_nodes_scripts.end() ? 0u : it->second;
    });
    s.At({0x00475D60, AddressOf(&Script::CScriptCache::ReleaseScript)}, 4, [](Regs& r) {
        Rec(0x5D60, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00478CF0, AddressOf(&Script::LookUpSymbol)}, 0, [](Regs& r) {
        Rec(0x8CF0, {r.args[0]});
        uint32_t index = 0;
        for (auto& [name, type] : g_nodes_symbols) {
            if (name == r.args[0]) {
                auto* entry = reinterpret_cast<uint8_t*>(g_nodes_base + 0x7000 + 0x20 * index);
                entry[1] = static_cast<uint8_t>(type);
                *reinterpret_cast<uint32_t*>(entry + 4) = name;
                return reinterpret_cast<uint32_t>(entry);
            }
            ++index;
        }
        return 0u;
    });
    s.At({0x00470F30, AddressOf(&Script::ParseArguments)}, 0, [](Regs& r) {
        // To the line's end token (not past it); a first argument named in g_nodes_scanAnswers gives an answer.
        const auto* token = reinterpret_cast<const uint8_t*>(r.args[1]);
        Rec(0x0F30, {r.args[0], r.args[1], r.args[2]});
        if (*token == 0x16) {
            uint32_t value;
            std::memcpy(&value, token + 1, 4);
            auto it = g_nodes_scanAnswers.find(value);
            if (it != g_nodes_scanAnswers.end())
                Answer('K', reinterpret_cast<void*>(r.args[0]), it->second.first, {it->second.second});
        }
        int depth = 0;
        while (!(depth == 0 && *token == 0x01)) {
            if (*token == 0x0E)
                ++depth;
            if (*token == 0x0F)
                --depth;
            token = Script::SkipToken(token);
        }
        return reinterpret_cast<uint32_t>(token);
    });
    s.At({0x005BA3D0, AddressOf(&Script::NewStruct)}, 0, [](Regs& r) {
        Rec(0xA3D0, {});
        (void)r;
        return Bump(8, 0xEE);
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Rec(0x6410, {r.ecx});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0x1717;
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0;
        return r.ecx;
    });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x7090, {r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        return 0u;
    });
    g_nodes_callback = s.Thunk(0, [](Regs& r) {
        Rec(0xCA11, {r.args[0], r.args[1]});
        return 0x77u;
    });
    // --- Obj_GetCollision: the floats and flags, the feeler ---
    getter({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 'F',
           0x66D0);
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        return Lookup('L', r.ecx, r.args[0]) ? 1u : 0u;
    });
    s.At({0x00417760}, 0, [](Regs& r) {
        Rec(0x7760, {});
        auto* feeler = reinterpret_cast<uint32_t*>(r.ecx);
        for (uint32_t i = 0; i < 0xB0 / 4; ++i)
            feeler[i] = 0xFEE10000 + i;
        feeler[0x98 / 4] = g_nodes_feelerHit;
        return r.ecx;
    });
    s.At({0x004172F0}, 8, [](Regs& r) {
        const auto* line = reinterpret_cast<const uint32_t*>(r.ecx);
        Rec(0x72F0, {line[0], line[1], line[2], line[3], line[4], line[5], line[6], line[7], r.args[0] & 0xFF,
                     r.args[1] & 0xFF, line[8]});
        uint32_t answer = g_nodes_collisions & 1;
        g_nodes_collisions >>= 1;
        return answer;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x37E0, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0])});
        return 0u;
    });
}

void SaveGlobals()
{
    g_nodes_saved = {
        {0x007D1800, 4, {}},     // the memory manager
        {0x007CC979, 1, {}},     // the memory manager is ready
        {0x006F0D80, 4, {}},     // the prefix tables
        {0x006F0D84, 8, {}},     // the node manager and its references
        {0x006EFDE0, 0xFA0, {}}, // GetNodes' buffer
        {0x006F3558, 4, {}},     // the script cache
        {0x006F3614, 16, {}},    // the CStruct pools
        {0x006F2564, 1, {}},     // the script flag ForEachNodeScriptCall clears
    };
    SaveAll(g_nodes_saved);
}

uint8_t* BeginRun(bool original)
{
    g_nodes_original = original;
    g_trace.clear();
    g_nodes_answers.clear();
    g_nodes_arrays.clear();
    uint8_t* arena = g_nodes_arenas[original ? 0 : 1];
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + kBumpStart, 0xDD, kArenaSize - kBumpStart);
    g_nodes_base = reinterpret_cast<uint32_t>(arena);
    g_nodes_bump = g_nodes_base + kBumpStart;
    for (Saved& saved : g_nodes_saved)
        std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<uint8_t*>(0x007D1800) = arena + 0x6000;
    *reinterpret_cast<uint32_t*>(arena + 0x6000 + 0x124) = g_nodes_base + 0x6800; // its script heap
    GameVariable<uint8_t*>(0x006F3558) = arena + 0x6400; // (the stand-in script cache)
    GameVariable<uint8_t*>(0x006F361C) = arena + 0x6200; // (the struct pool: the original allocates from it)
    return arena;
}

std::vector<uint32_t> EndRun(uint8_t* arena)
{
    std::vector<uint32_t> result = g_trace;
    for (int i = 0; i < 0xFA0 / 4; ++i)
        result.push_back(Rel((&GameVariable<uint32_t>(0x006EFDE0))[i]));
    result.push_back(Rel(GameVariable<uint32_t>(0x006F0D80)));
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    for (uint32_t& word : image)
        word = Rel(word);
    result.insert(result.end(), image.begin(), image.end());
    return result;
}

template <typename R, typename F, typename... A>
R CallC(uint32_t address, F ours, A... args)
{
    void* code = g_nodes_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
    return reinterpret_cast<R(__cdecl*)(A...)>(code)(args...);
}
template <typename R, typename F, typename... A>
R Call(uint32_t address, F ours, void* self, A... args)
{
    void* code = g_nodes_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(code)(self, args...);
}

CStruct* Struct(uint8_t* arena, int i) { return reinterpret_cast<CStruct*>(arena + 0x3000 + 0x10 * i); }
CArray* Array(uint8_t* arena, uint32_t offset, std::initializer_list<uint32_t> elements)
{
    auto* array = reinterpret_cast<CArray*>(arena + offset);
    array->m_size = static_cast<uint32_t>(elements.size());
    uint32_t* p = reinterpret_cast<uint32_t*>(arena + offset + 0x10);
    for (uint32_t e : elements)
        *p++ = e;
    return array;
}
uint32_t P(const void* p) { return reinterpret_cast<uint32_t>(p); }
void RecordVector(const Mth::Vector* v) { Rec(0x7EC, {Bits(v->x), Bits(v->y), Bits(v->z), Bits(v->w)}); }

// --- the hash and the node functions ----------------------------------------------------------------------------
std::vector<uint32_t> RunNodeTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    auto* m = reinterpret_cast<CNodeManager*>(arena + kManagerAt);
    for (auto& slot : m->m_slots)
        slot = {0, -1};
    m->m_walk.m_node = -1;
    GameVariable<CNodeManager*>(0x006F0D84) = m;

    // Two node arrays (A: nodes 0-5, B: nodes 6-9). Names: 0x100 and 0x4100 collide (slot 0x100), 0x3FFF and
    // 0x7FFF wrap round to slot 0; node 4 is class=link, node 5 has no name.
    CStruct* n[10];
    for (int i = 0; i < 10; ++i)
        n[i] = Struct(arena, i);
    const uint32_t names[10] = {0x100, 0x4100, 0x3FFF, 0x7FFF, 0x200, 0, 0x8100, 0x300, 0x12345, 0xABC0300};
    for (int i = 0; i < 10; ++i) {
        if (i != 5)
            Answer('K', n[i], kName, {names[i]});
    }
    Answer('K', n[4], kClass, {kLink});
    Answer('K', n[1], kClass, {0x777});
    CArray* a = Array(arena, 0x2000, {P(n[0]), P(n[1]), P(n[2]), P(n[3]), P(n[4]), P(n[5])});
    CArray* b = Array(arena, 0x2080, {P(n[6]), P(n[7]), P(n[8]), P(n[9])});
    m->m_count = 2;
    m->m_arrays[0] = {0xAAAA, a, false, {}};
    m->m_arrays[1] = {0xBBBB, b, false, {}};
    using M = CNodeManager;
    for (int i = 0; i < 6; ++i)
        Call<void>(0x0045CE60, &M::HashNode, m, 0u, static_cast<uint32_t>(i), n[i]);
    for (int i = 0; i < 4; ++i)
        Call<void>(0x0045CE60, &M::HashNode, m, 1u, static_cast<uint32_t>(i), n[6 + i]);
    for (uint32_t slot : {0x100u, 0x101u, 0x102u, 0x3FFFu, 0u, 1u, 0x200u, 0x300u, 0x301u, 0x2345u})
        Rec(0x5107, {slot, m->m_slots[slot].m_array, static_cast<uint32_t>(m->m_slots[slot].m_node)});

    // Lookups: present (first try, after collisions, after wrapping), absent (an empty slot, a full run, a slot
    // set by hand to the node without a name).
    m->m_slots[0x500] = {0, 5};
    using GetM = CStruct* (M::*)(uint32_t, bool);
    for (uint32_t name : {0x100u, 0x4100u, 0x8100u, 0x3FFFu, 0x7FFFu, 0x200u, 0x300u, 0xABC0300u, 0x5100u, 0x500u,
                          0x12345u}) {
        Result(Rel(Call<CStruct*>(0x0045CFC0, static_cast<GetM>(&M::GetNode), m, name, true)));
        Result(Rel(CallC<CStruct*>(0x0045C1B0, &SkateScript::GetNode, name, false)));
        Result(CallC<uint32_t>(0x0045BE70, &SkateScript::NodeExists, name) & 0xFF);
    }

    // Names, links. Node 0 links to [0x200 (node 4: not hashed), 0x300, 0x4100]; node 7 to [0x100]; node 8 has
    // an empty links array; node 9 none.
    CArray* links0 = Array(arena, 0x2100, {0x200, 0x300, 0x4100});
    CArray* links7 = Array(arena, 0x2180, {0x100});
    CArray* links8 = Array(arena, 0x2200, {});
    Answer('A', n[0], kLinks, {P(links0)});
    Answer('A', n[7], kLinks, {P(links7)});
    Answer('A', n[8], kLinks, {P(links8)});
    using ByStruct = uint32_t (*)(CStruct*);
    using ByStructIndex = uint32_t (*)(CStruct*, int);
    using LinkedStruct = CStruct* (*)(CStruct*, int);
    using LinkedName = CStruct* (*)(uint32_t, int);
    using ByName = uint32_t (*)(uint32_t);
    using ByNameIndex = uint32_t (*)(uint32_t, int);
    for (int i : {0, 5, 7, 8, 9}) {
        Result(CallC<uint32_t>(0x0045BE40, &SkateScript::GetNodeName, n[i]));
        Result(CallC<uint32_t>(0x0045C020, static_cast<ByStruct>(&SkateScript::GetLinkCount), n[i]));
    }
    for (int i = 0; i < 3; ++i) {
        Result(CallC<uint32_t>(0x0045BE10, static_cast<ByStructIndex>(&SkateScript::GetLink), n[0], i));
        Result(Rel(CallC<CStruct*>(0x0045C420, static_cast<LinkedStruct>(&SkateScript::GetLinkedNode), n[0], i)));
        Result(CallC<uint32_t>(0x0045C3E0, static_cast<ByNameIndex>(&SkateScript::GetLink), 0x100u, i));
        Result(Rel(CallC<CStruct*>(0x0045C460, static_cast<LinkedName>(&SkateScript::GetLinkedNode), 0x100u, i)));
    }
    for (uint32_t name : {0x100u, 0x300u, 0x12345u})
        Result(CallC<uint32_t>(0x0045C3A0, static_cast<ByName>(&SkateScript::GetLinkCount), name));
    using LinkedS = bool (*)(CStruct*, CStruct*);
    using LinkedN = bool (*)(uint32_t, uint32_t);
    for (auto [from, to] : {std::pair{0, 7}, std::pair{0, 1}, std::pair{0, 2}, std::pair{7, 0}, std::pair{8, 0},
                            std::pair{9, 0}}) {
        Result(CallC<uint32_t>(0x0045C4B0, static_cast<LinkedS>(&SkateScript::IsLinkedTo), n[from], n[to]) & 0xFF);
        Result(CallC<uint32_t>(0x0045C520, static_cast<LinkedN>(&SkateScript::IsLinkedTo), names[from], names[to]) &
               0xFF);
    }

    // Positions, angles, orientations.
    Answer('V', n[0], kPos, {Bits(1.5f), Bits(-2.25f), Bits(3.0f), Bits(1.0f)});
    Answer('V', n[0], kPosition, {Bits(9.0f), Bits(9.0f), Bits(9.0f), Bits(9.0f)});
    Answer('V', n[7], kPosition, {Bits(4.0f), Bits(5.5f), Bits(-6.75f), Bits(0.5f)});
    Answer('V', n[7], kAngles, {Bits(0.25f), Bits(-1.0f), Bits(2.0f), Bits(0.0f)});
    auto* v = reinterpret_cast<Mth::Vector*>(arena + 0x5000);
    using PosS = bool (*)(CStruct*, Mth::Vector*);
    using PosN = bool (*)(uint32_t, Mth::Vector*);
    for (int i : {0, 7, 9}) {
        std::memset(v, 0x77, 16);
        Result(CallC<uint32_t>(0x0045C050, static_cast<PosS>(&SkateScript::GetPosition), n[i], v) & 0xFF);
        RecordVector(v);
        std::memset(v, 0x77, 16);
        Result(CallC<uint32_t>(0x0045C560, static_cast<PosN>(&SkateScript::GetPosition), names[i], v) & 0xFF);
        RecordVector(v);
        std::memset(v, 0x77, 16);
        Result(CallC<uint32_t>(0x0045C180, static_cast<PosS>(&SkateScript::GetAngles), n[i], v) & 0xFF);
        RecordVector(v);
        std::memset(v, 0x77, 16);
        Result(CallC<uint32_t>(0x0045C590, static_cast<PosN>(&SkateScript::GetAngles), names[i], v) & 0xFF);
        RecordVector(v);
    }
    Answer('V', n[0], kOrientation, {Bits(0.5f), Bits(-0.25f), Bits(0.125f), Bits(7.0f)});
    Answer('V', n[7], kOrientation, {Bits(0.9f), Bits(0.5f), Bits(-0.1f), Bits(7.0f)});
    Answer('V', n[8], kOrientation, {Bits(0.6f), Bits(0.0f), Bits(0.8f), Bits(7.0f)});
    Answer('V', n[9], kOrientation, {Bits(0.3f), Bits(0.7f), Bits(-0.1f), Bits(7.0f)});
    // (a sum that comes out differently in float)
    Answer('V', n[1], kOrientation, {Bits(0.1050967276096344f), Bits(0.45897480845451355f),
                                     Bits(0.41543689370155334f), Bits(7.0f)});
    auto* matrix = reinterpret_cast<Mth::Matrix*>(arena + 0x5100);
    for (int i : {0, 7, 8, 9, 1, 2}) {
        std::memset(matrix, 0x66, sizeof(Mth::Matrix));
        CallC<void>(0x0045C0B0, &SkateScript::GetOrientation, n[i], matrix);
        for (int j = 0; j < 4; ++j)
            RecordVector(&matrix->row[j]);
    }

    // The instance's walk and last array.
    CallC<void>(0x0045C1D0, &SkateScript::StartNodeWalk, 0u);
    for (int i = 0; i < 3; ++i)
        Result(Rel(CallC<CStruct*>(0x0045C1F0, &SkateScript::NextNode)));
    CallC<void>(0x0045C200, &SkateScript::EndNodeWalk);
    Result(Rel(CallC<CStruct*>(0x0045C1F0, &SkateScript::NextNode)));
    Result(Rel(CallC<CArray*>(0x0045C210, &SkateScript::GetLastNodeArray)));
    Result(CallC<uint32_t>(0x0045C230, &SkateScript::GetLastNodeArrayName));
    m->mp_owner = arena + 0x5200;
    *reinterpret_cast<uint32_t*>(arena + 0x5258) = 0x5858;
    Result(CallC<uint32_t>(0x0045C220, &SkateScript::GetNodeOwnerValue58));

    // Unhashing node array 0 (node array 1's nodes move back to their first slots).
    Call<void>(0x0045CEF0, &M::UnhashNodeArray, m, 0u);
    // ... and then 1, with slots set by hand so a node is met again after moving on.
    m->m_slots[0x2345] = {1, 2};
    m->m_slots[0x2340] = {1, 0}; // (node 6, named 0x8100: moves to slot 0x100)
    Call<void>(0x0045CEF0, &M::UnhashNodeArray, m, 7u);
    Call<void>(0x0045CEF0, &M::UnhashNodeArray, m, 0u);
    return EndRun(arena);
}

// --- the prefix tables -----------------------------------------------------------------------------------------
std::vector<uint32_t> RunPrefixTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    using SkateScript::PrefixNodes;
    using SkateScript::PrefixTable;
    using SkateScript::PrefixTables;

    // PrefixNodes::Add past two growths.
    auto* pn = reinterpret_cast<PrefixNodes*>(arena + 0x5000);
    pn->m_count = 0;
    pn->mp_indices = reinterpret_cast<uint16_t*>(Bump(200, 0xCB));
    pn->m_prefix = 0x1234;
    for (uint32_t i = 0; i < 205; ++i)
        Call<void>(0x0045BDA0, &PrefixNodes::Add, pn, (i * 7919u) & 0xFFFF);
    Rec(0xDA0, {static_cast<uint32_t>(pn->m_count), P(pn->mp_indices)});

    // A node array of names: prefixes at each '_' (not "TRG"), names ending in digits.
    const char* texts[] = {"Ped_01", "TRG_Ped_Rail_3", "Car_12_a", "NoPrefix", "Ped_02", "ABC9", "_x", "", nullptr,
                           "trg_Car77", "Ped_Ped_5", "X0Y_1"};
    constexpr int kCount = sizeof(texts) / sizeof(texts[0]);
    std::vector<uint32_t> elements;
    g_nodes_names.clear();
    for (int i = 0; i < kCount; ++i) {
        CStruct* node = Struct(arena, i);
        uint32_t name = 0x51000 + i;
        Answer('K', node, kName, {name});
        if (texts[i])
            g_nodes_names[name] = texts[i];
        elements.push_back(P(node));
    }
    auto* nodes = reinterpret_cast<CArray*>(arena + 0x2000);
    nodes->m_size = kCount;
    std::memcpy(arena + 0x2010, elements.data(), elements.size() * 4);
    g_nodes_arrays[0xAAAA] = P(nodes);
    // A second node array: more "Ped" nodes.
    for (int i = 0; i < 3; ++i) {
        CStruct* node = Struct(arena, 40 + i);
        uint32_t name = 0x52000 + i;
        Answer('K', node, kName, {name});
        g_nodes_names[name] = i == 1 ? "Ped_Car_9" : "Ped_7";
        reinterpret_cast<uint32_t*>(arena + 0x2110)[i] = P(node);
    }
    reinterpret_cast<CArray*>(arena + 0x2100)->m_size = 3;
    g_nodes_arrays[0xBBBB] = P(arena + 0x2100);

    CallC<void>(0x0045C900, &SkateScript::AddNodeArrayPrefixes, 0xAAAAu);
    CallC<void>(0x0045C900, &SkateScript::AddNodeArrayPrefixes, 0xBBBBu);
    g_nodes_arrays[0xCCCC] = P(arena + 0x2100);
    CallC<void>(0x0045C900, &SkateScript::AddNodeArrayPrefixes, 0xCCCCu);
    Result(Rel(GameVariable<uint32_t>(0x006F0D80)));

    // Lookups by checksum and by text.
    uint16_t* count = reinterpret_cast<uint16_t*>(arena + 0x5800);
    using ByChecksum = uint32_t* (*)(uint32_t, uint16_t*);
    using ByText = uint32_t* (*)(const char*, uint16_t*);
    for (const char* prefix : {"Ped", "Car", "ABC", "TRG", "Ped_Rail", "X0Y", "Nothing", "trg_Car", "Ped_Ped"}) {
        *count = 0xEEEE;
        uint32_t* found = CallC<uint32_t*>(0x0045BF90, static_cast<ByChecksum>(&SkateScript::GetPrefixedNodes),
                                           Crc::GenerateCRCFromString(prefix), count);
        Rec(0xBF90, {P(found), *count});
        for (int i = 0; i < *count; ++i)
            Result(found[i]);
    }
    char* text = reinterpret_cast<char*>(arena + 0x5900);
    for (const char* prefix : {"Ped_", "Ped", "Car__", "", "_"}) {
        std::memcpy(text, prefix, std::strlen(prefix) + 1);
        *count = 0xEEEE;
        uint32_t* found = CallC<uint32_t*>(0x0045BFB0, static_cast<ByText>(&SkateScript::GetPrefixedNodes),
                                           static_cast<const char*>(text), count);
        Rec(0xBFB0, {P(found), *count});
    }

    // The nearest "Ped" node (node manager hash set up by hand for the names involved).
    auto* m = reinterpret_cast<CNodeManager*>(arena + kManagerAt);
    for (auto& slot : m->m_slots)
        slot = {0, -1};
    GameVariable<CNodeManager*>(0x006F0D84) = m;
    m->m_count = 2;
    m->m_arrays[0] = {0xAAAA, nodes, false, {}};
    m->m_arrays[1] = {0xBBBB, reinterpret_cast<CArray*>(arena + 0x2100), false, {}};
    for (int i = 0; i < kCount; ++i)
        m->m_slots[(0x51000 + i) & 0x3FFF] = {0, static_cast<int16_t>(i)};
    for (int i = 0; i < 3; ++i)
        m->m_slots[(0x52000 + i) & 0x3FFF] = {1, static_cast<int16_t>(i)};
    Answer('V', Struct(arena, 0), kPos, {Bits(10.0f), Bits(0.0f), Bits(0.0f), Bits(1.0f)});
    Answer('V', Struct(arena, 4), kPos, {Bits(-3.0f), Bits(2.0f), Bits(1.0f), Bits(1.0f)});
    Answer('V', Struct(arena, 40), kPos, {Bits(0.5f), Bits(-0.25f), Bits(4.0f), Bits(1.0f)});
    Answer('V', Struct(arena, 41), kPos, {Bits(3.0f), Bits(-2.0f), Bits(-1.0f), Bits(1.0f)});
    Answer('V', Struct(arena, 42), kPos, {Bits(-3.0f), Bits(2.0f), Bits(1.0f), Bits(1.0f)});
    Answer('V', Struct(arena, 2), kPos, {Bits(7.0f), Bits(7.0f), Bits(-7.0f), Bits(1.0f)}); // (the only "Car")
    Answer('V', Struct(arena, 10), kPos, {Bits(1.0f), Bits(1.0f), Bits(1.0f), Bits(1.0f)});
    // (every other node placed too: the original measures a node without pos= where the last one was - for the
    // first such node, a stack value it never set; the result then depended on what the stack held, matching ours
    // offline by chance and not in the game)
    for (int i : {1, 3, 5, 6, 7, 8, 9, 11})
        Answer('V', Struct(arena, i), kPos, {Bits(-20.0f + i), Bits(5.0f), Bits(2.0f * i), Bits(1.0f)});
    auto* pos = reinterpret_cast<Mth::Vector*>(arena + 0x5A00);
    const float targets[][3] = {{0, 0, 0}, {-3, 2, 1}, {3, -2, -1}, {100, 0, 0}, {0.5f, -0.25f, 3.9f}};
    for (const auto& t : targets) {
        *pos = {t[0], t[1], t[2], 1.0f};
        for (const char* prefix : {"Ped", "Nothing", "Car"})
            Result(Rel(CallC<CStruct*>(0x0045C2D0, &SkateScript::GetNearestNodeByPrefix,
                                       Crc::GenerateCRCFromString(prefix), static_cast<const Mth::Vector*>(pos))));
    }

    // Removing: a missing node array, the middle one, the last, the only one.
    for (uint32_t name : {0xDDDDu, 0xBBBBu, 0xAAAAu, 0xCCCCu}) {
        CallC<void>(0x0045CE40, &SkateScript::RemoveNodeArrayPrefixes, name);
        Result(Rel(*reinterpret_cast<uint32_t*>(GameVariable<uint32_t>(0x006F0D80))));
    }
    *count = 0xEEEE;
    CallC<uint32_t*>(0x0045BF90, static_cast<ByChecksum>(&SkateScript::GetPrefixedNodes),
                     Crc::GenerateCRCFromString("Ped"), count);
    Result(*count);

    // A table with two prefixes in one bucket (a chained item), destroyed (the chain walked by IterateNext).
    auto* table = reinterpret_cast<PrefixTable*>(arena + 0x5B00);
    Result(Rel(Call<PrefixTable*>(0x0045C5D0, &PrefixTable::Construct, table)));
    for (uint32_t i = 0; i < 4; ++i) {
        Call<void>(0x0045C650, &PrefixTable::Add, table, 0x100u + 0x1000u * (i & 1), i);
        Call<void>(0x0045C650, &PrefixTable::Add, table, 0x2FFu, 10 + i);
    }
    Call<void>(0x0045C240, &PrefixTable::Compact, table);
    Call<void>(0x0045CD30, &PrefixTable::Destruct, table);
    return EndRun(arena);
}

// --- the scan of scripts for calls -------------------------------------------------------------------------------
constexpr uint32_t kTarget = 0x7A7A0001;   // a member function
constexpr uint32_t kEndGap = 0xE5399FB2;
constexpr uint32_t kCFunc = 0x7A7A0002;    // a C function
constexpr uint32_t kScriptA = 0x5C000001, kScriptB = 0x5C000002, kScriptH = 0x5C000003, kScriptG = 0x5C000004,
                   kScriptR = 0x5C000005;

struct Bytes {
    std::vector<uint8_t> b;
    Bytes& T(uint8_t t)
    {
        b.push_back(t);
        return *this;
    }
    Bytes& N(uint8_t t, uint32_t v)
    {
        b.push_back(t);
        for (int i = 0; i < 4; ++i)
            b.push_back(static_cast<uint8_t>(v >> (8 * i)));
        return *this;
    }
    Bytes& J()
    {
        b.insert(b.end(), {0x47, 0x00, 0x00});
        return *this;
    }
};

uint32_t PlaceScript(uint8_t* arena, uint32_t& at, const Bytes& code)
{
    uint32_t start = g_nodes_base + at;
    std::memcpy(arena + at, code.b.data(), code.b.size());
    at += (static_cast<uint32_t>(code.b.size()) + 15) & ~15u;
    return start;
}

std::vector<uint32_t> RunScanTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    g_nodes_scripts.clear();
    g_nodes_symbols.clear();
    uint32_t at = 0x4000;
    // The symbols: scripts, the target member function, a C function, EndGap, an assigned name.
    for (uint32_t name : {kScriptA, kScriptB, kScriptH, kScriptG, kScriptR})
        g_nodes_symbols[name] = Script::ESYMBOLTYPE_QSCRIPT;
    g_nodes_symbols[kTarget] = Script::ESYMBOLTYPE_MEMBERFUNCTION;
    g_nodes_symbols[0x7A7A0003] = Script::ESYMBOLTYPE_MEMBERFUNCTION;
    g_nodes_symbols[kEndGap] = Script::ESYMBOLTYPE_MEMBERFUNCTION;
    g_nodes_symbols[kCFunc] = Script::ESYMBOLTYPE_CFUNCTION;
    g_nodes_symbols[0x7A7A0004] = Script::ESYMBOLTYPE_CFUNCTION;
    g_nodes_symbols[0x7A7A0005] = Script::ESYMBOLTYPE_INTEGER;
    // Member function table entries (their names first), outside the arena: the code holds their addresses
    // unaligned, where the arena comparison can't relocate them.
    static uint32_t entries[2];
    entries[0] = kEndGap;
    entries[1] = kTarget;
    uint32_t endGapEntry = reinterpret_cast<uint32_t>(&entries[0]);
    uint32_t targetEntry = reinterpret_cast<uint32_t>(&entries[1]);

    Bytes a;
    a.N(0x16, kScriptA).T(0x01);                             // (the first line is skipped)
    a.N(0x16, kScriptB).N(0x16, 0x1111).T(0x01);             // a script: followed
    a.N(0x16, kTarget).N(0x17, 5).T(0x01);                    // the target
    a.N(0x16, 0x7A7A0003).N(0x16, kTarget).T(0x01);           // another member function: skipped
    a.N(0x16, 0x7A7A0003).N(0x16, 0x1111).N(0x16, kTarget).T(0x01); // (the whole statement)
    a.N(0x16, 0x7A7A0005).T(0x07).N(0x17, 3).T(0x01);         // an assignment
    a.N(0x16, 0x7A7A0006).N(0x16, kTarget).T(0x01);           // an unknown name (its arguments are scanned)
    a.N(0x16, 0x7A7A0005).N(0x16, kTarget).T(0x01);           // an integer
    a.N(0x16, kCFunc).N(0x17, 1).T(0x01);                     // a C function (the target in the second scan)
    a.N(0x16, 0x7A7A0004).N(0x16, kTarget).T(0x01);           // another C function: skipped
    a.N(0x43, 0x0044AB20).N(0x16, 0x2222).T(0x01);            // SetEventHandler: scr= followed
    a.N(0x43, 0x00401234).N(0x16, kTarget).T(0x01);           // another C function (looked up): skipped
    a.N(0x46, endGapEntry).N(0x16, 0x3333).T(0x01);           // EndGap: gapscript= followed when it's the target
    a.N(0x46, targetEntry).N(0x17, 9).T(0x01);                // the target, looked up
    a.N(0x44, kTarget).T(0x01);                               // the target, without a table entry
    a.T(0x21).N(0x17, 4).N(0x16, kTarget).T(0x01);            // repeat: skipped
    a.J().T(0x39).T(0x0E).N(0x16, kTarget).T(0x0F).T(0x01);   // if not (...): skipped
    a.J().T(0x0E).N(0x16, kTarget).T(0x0F).T(0x01);           // if (...): skipped
    a.J().N(0x16, kTarget).T(0x01);                           // if target: the target
    a.N(0x16, kScriptR).T(0x01);                              // a script calling itself (depth limit)
    a.N(0x17, 1).N(0x16, kTarget).T(0x01);                    // a statement not starting with a name
    a.T(0x24);
    g_nodes_scripts[kScriptA] = PlaceScript(arena, at, a);
    Bytes b;
    b.N(0x16, kScriptB).T(0x01).N(0x16, kTarget).T(0x01).T(0x24);
    g_nodes_scripts[kScriptB] = PlaceScript(arena, at, b);
    Bytes h;
    h.N(0x16, kScriptH).T(0x01).N(0x16, kCFunc).T(0x01).N(0x16, kTarget).N(0x17, 2).T(0x01).T(0x24);
    g_nodes_scripts[kScriptH] = PlaceScript(arena, at, h);
    Bytes g;
    g.N(0x16, kScriptG).T(0x01).N(0x16, kEndGap).T(0x01).N(0x16, kTarget).T(0x01).T(0x24);
    g_nodes_scripts[kScriptG] = PlaceScript(arena, at, g);
    Bytes r;
    r.N(0x16, kScriptR).T(0x01).N(0x16, kTarget).T(0x01).N(0x16, kScriptR).T(0x01).T(0x24);
    g_nodes_scripts[kScriptR] = PlaceScript(arena, at, r);
    g_nodes_scanAnswers.clear();
    g_nodes_scanAnswers[0x2222] = {0xA6D2D890, kScriptH}; // scr=
    g_nodes_scanAnswers[0x3333] = {0x7C9E51AB, kScriptG}; // gapscript=

    using Scan = void (*)(uint32_t, uint32_t, SkateScript::ScriptCallCallback, CStruct*, int);
    auto callback = reinterpret_cast<SkateScript::ScriptCallCallback>(g_nodes_callback);
    CStruct* parent = Struct(arena, 30);
    for (uint32_t target : {kTarget, kEndGap, kCFunc, kScriptB}) {
        Rec(0x5CA2, {target});
        CallC<void>(0x0045C950, static_cast<Scan>(&SkateScript::ForEachScriptCall), kScriptA, target, callback,
                    parent, 0);
    }
    CallC<void>(0x0045C950, static_cast<Scan>(&SkateScript::ForEachScriptCall), kScriptA, kTarget, callback, parent, 6);
    CallC<void>(0x0045C950, static_cast<Scan>(&SkateScript::ForEachScriptCall), kScriptB, kTarget, callback, parent, 5);

    // Every node's script= (the node manager's walk).
    auto* m = reinterpret_cast<CNodeManager*>(arena + kManagerAt);
    for (auto& slot : m->m_slots)
        slot = {0, -1};
    m->m_walk.m_node = -1;
    GameVariable<CNodeManager*>(0x006F0D84) = m;
    CStruct* n[4];
    for (int i = 0; i < 4; ++i)
        n[i] = Struct(arena, 40 + i);
    Answer('K', n[0], 0x2CA8A299, {kScriptB});
    Answer('K', n[2], 0x2CA8A299, {kScriptH});
    Answer('K', n[3], 0x2CA8A299, {kScriptA});
    m->m_count = 1;
    m->m_arrays[0] = {0xAAAA, Array(arena, 0x2000, {P(n[0]), P(n[1]), P(n[2]), P(n[3])}), false, {}};
    GameVariable<uint8_t>(0x006F2564) = 0x5A;
    CallC<void>(0x0045CCA0, &SkateScript::ForEachNodeScriptCall, 0x2CA8A299u, kTarget, callback);
    Rec(0x2564, {GameVariable<uint8_t>(0x006F2564), static_cast<uint32_t>(m->m_walk.m_node)});
    return EndRun(arena);
}

// --- Obj_GetCollision --------------------------------------------------------------------------------------------
std::vector<uint32_t> RunCollisionTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    using C = Obj::CCompositeObject;
    auto* object = reinterpret_cast<C*>(arena + 0x100);
    std::memset(object, 0xC3, sizeof(C));
    object->m_pos = {101.37f, -23.913f, 7.0331f, 0.98731f};
    const float rows[4][4] = {{0.81231f, 0.1277f, -0.5713f, 0.0031f},
                              {-0.0911f, 0.98761f, 0.13377f, -0.0127f},
                              {0.57719f, -0.0933f, 0.81139f, 0.0071f},
                              {3.3f, 4.4f, 5.5f, 1.0f}};
    for (int i = 0; i < 4; ++i)
        object->m_matrix.row[i] = {rows[i][0], rows[i][1], rows[i][2], rows[i][3]};
    auto* script = reinterpret_cast<Script::CScript*>(arena + 0x2F00);
    CStruct* params[7];
    for (int i = 0; i < 7; ++i)
        params[i] = Struct(arena, 50 + i);
    // 0: the defaults; 1: everything, side; 2: down; 3: side and down; 4: the default direction; 5: down, length 0.
    for (int i = 1; i < 5; ++i) {
        Answer('F', params[i], 0xFE82614D, {Bits(2.71f + i)});
        Answer('F', params[i], 0x0AB21AF0, {Bits(-1.37f * i)});
        Answer('F', params[i], 0xE36D657E, {Bits(0.917f + i)});
        Answer('F', params[i], 0x5AF4C4BA, {Bits(1.113f - i)});
    }
    Answer('L', params[1], 0xDC7EE44A, {1});
    Answer('L', params[2], 0xE3006FC4, {1});
    Answer('L', params[3], 0xDC7EE44A, {1});
    Answer('L', params[3], 0xE3006FC4, {1});
    Answer('L', params[5], 0xE3006FC4, {1});
    Answer('F', params[5], 0xFE82614D, {Bits(0.0f)});
    // 6: a start whose z comes out differently if a sum kept at 53 bits is rounded to float.
    Answer('F', params[6], 0x0AB21AF0, {Bits(6.056305885314941f)});
    Answer('F', params[6], 0xE36D657E, {Bits(-0.4256422519683838f)});
    g_nodes_collisions = 0x2D;
    for (int i = 0; i < 7; ++i) {
        g_nodes_feelerHit = i & 1 ? g_nodes_base + 0x2E00 : 0;
        Result(Call<uint32_t>(0x0045E9E0, &C::Obj_GetCollision, object, params[i], script) & 0xFF);
    }
    return EndRun(arena);
}

void RunPair(std::vector<uint32_t> (*run)(bool), const char* what)
{
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

void TestNodes()
{
    SaveGlobals();
    auto* s = new StandIns;
    InstallStandIns(*s);
    RunPair(RunNodeTest, "SkateScript nodes: the name hash, names, links, positions, orientations, the walk");
    RunPair(RunPrefixTest, "SkateScript nodes: prefix tables, prefixed nodes, the nearest by prefix");
    RunPair(RunScanTest, "SkateScript nodes: the calls in scripts and in the nodes' scripts");
    RunPair(RunCollisionTest, "CCompositeObject::Obj_GetCollision");
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNodes, Cleanup);
