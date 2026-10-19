// Self-tests for the objsys agent's batch OS-3: script structures to and from byte buffers
// (src/game/script/struct_buffer.cpp). The original and ours on a stand-in memory manager over a 64 KB-aligned arena:
// structures of every component type (integers of every size, tiny / negative / normal floats, strings, pairs,
// vectors, names short / medium / long, scripts, nested structures, arrays of every type, unwritable components)
// are written in full and into buffers too small, measured, read back, and crafted buffers are read; the buffers,
// the results and the whole arena (relocated) are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "hook/appmain_testkit.h"
#include "hook/objsys_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

constexpr uint32_t kArenaSize = 0x40000;
constexpr uint32_t kNamesOffset = 0x800;    // the two name arrays (stood-in globals)
constexpr uint32_t kBufferOffset = 0x1000;  // the buffers written / read (0x3000)
constexpr uint32_t kHeapOffset = 0x4000;

uint8_t* g_bufArenas[2];
uint8_t* g_bufArena;
std::vector<uint32_t> g_bufTrace;

uint8_t* BufArena(bool original)
{
    uint8_t*& arena = g_bufArenas[original ? 0 : 1];
    if (!arena) {
        // (high addresses, unlike the buffers' data: a byte pattern mistaken for a pointer into an arena low in
        // memory would be relocated in one run only)
        void* at = reinterpret_cast<void*>(original ? 0x6B300000u : 0x6B380000u);
        arena = static_cast<uint8_t*>(VirtualAlloc(at, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!arena)
            arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    return arena;
}

uint32_t BufReloc(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(g_bufArena);
    if (word - base <= kArenaSize)
        return 0xA0000000u + (word - base);
    return word;
}

void SetUpManager(uint8_t* arena)
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    std::memset(arena, 0, kHeapOffset);
    std::memset(arena + kHeapOffset, 0xCD, kArenaSize - kHeapOffset);
    GameVariable<bool>(0x007CC979) = true;
    auto* manager = reinterpret_cast<Manager*>(arena);
    GameVariable<void*>(0x007D1800) = manager;
    auto* region = new (arena + 0x600) Region(arena + kHeapOffset, arena + kArenaSize);
    auto* heap = new (arena + 0x620) Heap(region, 1, "test");
    manager->m_heaps[0] = heap;
    manager->m_heapCount = 1;
    manager->m_topDownHeap = heap;
    manager->m_scriptHeap = heap;
    manager->PushContext(heap);
}

std::vector<uint32_t> ArenaImage(uint8_t* arena)
{
    auto base = reinterpret_cast<uint32_t>(arena);
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    for (uint32_t& word : image) {
        word = BufReloc(word);
        uint32_t high = word >> 16;
        if (high >= (base >> 16) && high <= ((base + kArenaSize) >> 16))
            word = (word & 0xFFFF) | ((0xA000u + high - (base >> 16)) << 16);
    }
    return image;
}

// The two name arrays: n names each, as script arrays of checksums.
Script::CArray* NameArray(uint32_t which)
{
    return reinterpret_cast<Script::CArray*>(g_bufArena + kNamesOffset + 0x100 * which);
}
void BuildNameArrays()
{
    for (uint32_t which = 0; which < 2; ++which) {
        uint8_t* p = g_bufArena + kNamesOffset + 0x100 * which;
        auto* array = reinterpret_cast<Script::CArray*>(p);
        uint32_t count = which == 0 ? 20 : 300; // (indexes in the second need two bytes; it ends at 0xDC0)
        p[1] = Script::ESYMBOLTYPE_NAME;
        array->m_size = count;
        auto* elements = reinterpret_cast<uint32_t*>(p + 0x10);
        array->m_elements = elements;
        for (uint32_t i = 0; i < count; ++i)
            elements[i] = (which == 0 ? 0x517A0000u : 0x4D7A0000u) + i;
    }
}

uint32_t g_bufCalls;
void __cdecl Callback() { ++g_bufCalls; }

uint32_t RandomName(Lcg& random)
{
    switch (random.Below(3)) {
    case 0: return 0x517A0000u + random.Below(22);  // (some past the short array's end)
    case 1: return 0x4D7A0000u + random.Below(302);
    default: return 0x60000000u + random.Below(1000);
    }
}

Script::CArray* RandomArray(Lcg& random, int depth);

Script::CStruct* RandomStruct(Lcg& random, int depth)
{
    using namespace Script;
    CStruct* s = NewStruct();
    s->Init();
    uint32_t count = 1 + random.Below(depth == 0 ? 14 : 5);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t name = RandomName(random);
        switch (random.Below(depth < 2 ? 13 : 10)) {
        case 0:
        case 1: {
            static const int kIntegers[] = {0, 1, 0xFF, 0x100, 0xFFFF, 0x10000, -1, -0x80, -0x81, -0x8000, -0x8001,
                                            0x7FFFFFFF, static_cast<int>(0x80000000), 0x7F, 0x80, 1234567};
            s->AddInteger(name, random.Pick(kIntegers));
            break;
        }
        case 2: {
            static const float kFloats[] = {0.0f, -0.0f, 1e-5f, -1e-5f, 0.0001f, 0.00010001f, -0.00010001f, 0.5f,
                                            -3.25f, 1e30f};
            s->AddFloat(name, random.Pick(kFloats));
            break;
        }
        case 3: {
            static const char* const kStrings[] = {"", "a", "hello world", "0123456789012345678901234"};
            s->AddString(name, random.Pick(kStrings));
            break;
        }
        case 4:
            s->AddLocalString(name, random.Below(2) ? "local" : "");
            break;
        case 5:
            s->AddPair(name, 1.5f, -2.25f);
            break;
        case 6:
            s->AddVector(name, 3.0f, -4.5f, 1e-7f);
            break;
        case 7:
            s->AddChecksum(name, 0xC4EC0000u + random.Below(100));
            break;
        case 8: {
            static const uint8_t kScript[] = {0x01, 0x24};
            s->AddScript(name, kScript, sizeof(kScript));
            break;
        }
        case 9:
            // an integer turned into a type nothing can write (C function, member function, structure pointer)
            if (random.Below(4) == 0) {
                s->AddInteger(name, 5);
                static const uint8_t kTypes[] = {8, 9, 11, 20};
                for (CComponent* c = s->GetNextComponent(nullptr); c; c = s->GetNextComponent(c)) {
                    if (c->name == name)
                        c->typeAndFlag = static_cast<uint8_t>(random.Pick(kTypes) << 1 | (c->typeAndFlag & 1));
                }
            } else {
                s->AddInteger(name, 77);
            }
            break;
        case 10:
        case 11:
            s->AddStructurePointer(name, RandomStruct(random, depth + 1));
            break;
        default:
            s->AddArrayPointer(name, RandomArray(random, depth + 1));
            break;
        }
    }
    return s;
}

Script::CArray* RandomArray(Lcg& random, int depth)
{
    using namespace Script;
    CArray* a = NewArray();
    a->Init();
    static const ESymbolType kTypes[] = {ESYMBOLTYPE_INTEGER, ESYMBOLTYPE_FLOAT, ESYMBOLTYPE_NAME, ESYMBOLTYPE_STRING,
                                         ESYMBOLTYPE_LOCALSTRING, ESYMBOLTYPE_PAIR, ESYMBOLTYPE_VECTOR,
                                         ESYMBOLTYPE_STRUCTURE, ESYMBOLTYPE_ARRAY, ESYMBOLTYPE_QSCRIPT};
    ESymbolType type = depth >= 3 ? ESYMBOLTYPE_INTEGER : random.Pick(kTypes);
    uint32_t count = random.Below(4);
    a->SetSizeAndType(count, type);
    for (uint32_t i = 0; i < count; ++i) {
        switch (type) {
        case ESYMBOLTYPE_FLOAT:
            a->SetFloat(i, static_cast<float>(random.Below(100)) / 8.0f);
            break;
        case ESYMBOLTYPE_STRING:
        case ESYMBOLTYPE_LOCALSTRING:
            a->SetElement(i, reinterpret_cast<uint32_t>(CreateString(random.Below(2) ? "str" : "")));
            break;
        case ESYMBOLTYPE_PAIR: {
            CPair* pair = NewPair();
            pair->Init();
            pair->x = 1.0f + i;
            pair->y = -2.0f;
            a->SetElement(i, reinterpret_cast<uint32_t>(pair));
            break;
        }
        case ESYMBOLTYPE_VECTOR: {
            CVector* vector = NewVector();
            vector->Init();
            vector->x = 1.0f;
            vector->y = 2.0f + i;
            vector->z = -3.0f;
            a->SetElement(i, reinterpret_cast<uint32_t>(vector));
            break;
        }
        case ESYMBOLTYPE_STRUCTURE:
            a->SetElement(i, reinterpret_cast<uint32_t>(RandomStruct(random, depth + 1)));
            break;
        case ESYMBOLTYPE_ARRAY:
            a->SetElement(i, reinterpret_cast<uint32_t>(RandomArray(random, depth + 1)));
            break;
        case ESYMBOLTYPE_QSCRIPT:
            a->SetElement(i, 0x5C000000 + i); // (written as type and count only)
            break;
        default:
            a->SetElement(i, random.Next());
            break;
        }
    }
    return a;
}

void PushBytes(const uint8_t* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        g_bufTrace.push_back(p[i]);
}

// Writing (full, measured, short buffers of every length near the end and random ones), reading the written
// buffer back and writing what was read, reading crafted buffers (the short forms, unknown types).
void TestStructBuffer()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestStructBuffer skipped (the memory manager is already set up)");
        return;
    }
    ObjsysTests::SaveGlobals({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F2338, 16}, {0x006F3614, 16}, {0x006F22B4, 16},
                       {0x006F3644, 16}, {0x006F3654, 16}, {0x006F362C, 16}, {0x006F3570, 24}});
    StandIns& s = *new StandIns();
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        g_bufTrace.push_back(0x6A770000);
        g_bufTrace.push_back(r.args[0]);
        if (r.args[0] == 0x35115A20)
            return reinterpret_cast<uint32_t>(NameArray(0));
        if (r.args[0] == 0x25231F42)
            return reinterpret_cast<uint32_t>(NameArray(1));
        return 0u;
    });
    for (uint32_t seed = 1; seed <= 30; ++seed) {
        auto run = [&](bool original) {
            g_bufTrace.clear();
            ObjsysTests::ZeroGlobals();
            g_bufArena = BufArena(original);
            SetUpManager(g_bufArena);
            BuildNameArrays();
            Lcg random(0x5B0F0000 + seed);
            Script::CStruct* structure = RandomStruct(random, 0);
            uint8_t* buffer = g_bufArena + kBufferOffset;
            // in full
            std::memset(buffer, 0xEE, 0x3000);
            g_bufCalls = 0;
            uint32_t full = Fn<uint32_t(__cdecl*)(Script::CStruct*, uint8_t*, uint32_t, int, void (*)())>(
                original, 0x00479E20)(structure, buffer, 0x1000, 1, seed % 2 ? &Callback : nullptr);
            g_bufTrace.push_back(full);
            g_bufTrace.push_back(g_bufCalls);
            PushBytes(buffer, 0x1000);
            // measured
            g_bufTrace.push_back(Fn<uint32_t(__cdecl*)(Script::CStruct*, uint32_t)>(original, 0x0047A070)(
                structure, 0x1000));
            g_bufTrace.push_back(Fn<uint32_t(__cdecl*)(Script::CStruct*, uint32_t)>(original, 0x0047A070)(
                structure, full > 2 ? full - 2 : 1));
            // short buffers
            for (int i = 0; i < 6; ++i) {
                uint32_t size = i < 3 && full >= 3 ? full - static_cast<uint32_t>(i) : random.Below(full + 1);
                std::memset(buffer + 0x1000, 0xEE, 0x1000);
                uint32_t written = Fn<uint32_t(__cdecl*)(Script::CStruct*, uint8_t*, uint32_t, int, void (*)())>(
                    original, 0x00479E20)(structure, buffer + 0x1000, size, 1, nullptr);
                g_bufTrace.push_back(size);
                g_bufTrace.push_back(written);
                PushBytes(buffer + 0x1000, size + 4);
            }
            // an array alone into the smallest buffers
            for (uint32_t size = 0; size < 5; ++size) {
                std::memset(buffer + 0x1000, 0xEE, 8);
                g_bufTrace.push_back(Fn<uint32_t(__cdecl*)(Script::CArray*, uint8_t*, uint32_t, int)>(
                    original, 0x00479B80)(NameArray(seed & 1), buffer + 0x1000, size, 1));
                PushBytes(buffer + 0x1000, 8);
            }
            // the long name array (300: a two-byte count) read back alone
            {
                uint32_t written = Fn<uint32_t(__cdecl*)(Script::CArray*, uint8_t*, uint32_t, int)>(
                    original, 0x00479B80)(NameArray(1), buffer + 0x1000, 0x800, 1);
                auto* copy = reinterpret_cast<Script::CArray*>(g_bufArena + 0xE00); // (in the arena: no pool)
                std::memset(copy, 0, 0x20);
                copy->Init();
                const uint8_t* end = Fn<const uint8_t*(__cdecl*)(Script::CArray*, const uint8_t*)>(
                    original, 0x0047A440)(copy, buffer + 0x1000);
                g_bufTrace.push_back(written);
                g_bufTrace.push_back(static_cast<uint32_t>(end - (buffer + 0x1000)));
                g_bufTrace.push_back(copy->m_size);
            }
            // a null structure
            g_bufTrace.push_back(Fn<uint32_t(__cdecl*)(Script::CStruct*, uint8_t*, uint32_t, int, void (*)())>(
                original, 0x00479E20)(nullptr, buffer + 0x2000, seed % 3, 1, nullptr));
            // read back (when it was writable), then written again
            if (full) {
                Script::CStruct* copy = Script::NewStruct();
                copy->Init();
                copy->AddInteger(0x0D0D0D0D, 9); // (cleared first)
                const uint8_t* end = Fn<const uint8_t*(__cdecl*)(Script::CStruct*, const uint8_t*)>(
                    original, 0x0047A0C0)(copy, buffer);
                g_bufTrace.push_back(static_cast<uint32_t>(end - buffer));
                std::memset(buffer + 0x2000, 0xEE, 0x1000);
                g_bufTrace.push_back(Fn<uint32_t(__cdecl*)(Script::CStruct*, uint8_t*, uint32_t, int, void (*)())>(
                    original, 0x00479E20)(copy, buffer + 0x2000, 0x1000, 1, nullptr));
                PushBytes(buffer + 0x2000, 0x400);
            }
            // a crafted buffer: every short form, names by index, unknown types (only the name read)
            {
                uint8_t* p = buffer + 0x2800;
                auto put = [&p](std::initializer_list<uint8_t> bytes) {
                    for (uint8_t b : bytes)
                        *p++ = b;
                };
                put({0x0E, 1, 0, 0, 0x60, 0x85});                 // signed byte -123
                put({0x0F, 2, 0, 0, 0x60, 0x34, 0x92});           // signed short
                put({0x90, 3, 0xF0});                             // unsigned byte, short name 3
                put({0x51, 0x2B, 0x01, 0xFF, 0xFF});              // unsigned short, medium name 299
                put({0x12, 5, 0, 0, 0x60});                       // zero integer
                put({0x93, 6});                                   // zero float
                put({0x07, 7, 0, 0, 0x60});                       // a script: nothing
                put({0x08, 8, 0, 0, 0x60, 0x0B, 9, 0, 0, 0x60});  // C function, 11: nothing
                put({0x20, 10, 0, 0, 0x60, 0x3F, 11, 0, 0, 0x60}); // 0x20, 0x3F: unknown
                put({0x81, 2, 0x10, 0, 0, 0});                    // integer, short name 2
                put({0x0C, 12, 0, 0, 0x60, 0x0D, 2, 0, 0x34, 0x12, 0, 0, 0x78, 0x56, 0, 0}); // names array
                put({0x0C, 13, 0, 0, 0x60, 0x02, 2, 0, 0, 0, 0x80, 0x3F, 0, 0, 0, 0xC0});    // floats array
                put({0x0C, 14, 0, 0, 0x60, 0x07, 3, 0});           // an array of scripts: just made
                put({0x0C, 15, 0, 0, 0x60, 0x04, 2, 0, 'x', 0, 0}); // local strings
                put({0x00});
                Script::CStruct* crafted = Script::NewStruct();
                crafted->Init();
                const uint8_t* end = Fn<const uint8_t*(__cdecl*)(Script::CStruct*, const uint8_t*)>(
                    original, 0x0047A0C0)(crafted, buffer + 0x2800);
                g_bufTrace.push_back(static_cast<uint32_t>(end - (buffer + 0x2800)));
            }
            std::vector<uint32_t> trace = g_bufTrace;
            for (uint32_t& word : trace)
                word = BufReloc(word);
            std::vector<uint32_t> image = ArenaImage(g_bufArena);
            trace.insert(trace.end(), image.begin(), image.end());
            GameVariable<void*>(0x007D1800) = nullptr;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "structure buffers: write, measure, short buffers, read back, crafted");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestStructBuffer, ObjsysTests::CleanUp);

// The register entry points (naked stubs) against the originals: integers of each size, floats, strings - into
// buffers big enough, exactly enough and too small.
uint32_t CallInteger(void* code, uint32_t key, int value, uint8_t* buffer, uint32_t bytes)
{
    uint32_t result;
    __asm {
        push ebx
        mov ecx, key
        mov ebx, value
        push bytes
        push buffer
        call code
        add esp, 8
        pop ebx
        mov result, eax
    }
    return result;
}
uint32_t CallFloat(void* code, uint8_t* buffer, uint32_t key, uint32_t bytes, float value)
{
    uint32_t result;
    __asm {
        push esi
        mov ecx, buffer
        mov edx, key
        mov esi, bytes
        push value
        call code
        add esp, 4
        pop esi
        mov result, eax
    }
    return result;
}
uint32_t CallString(void* code, uint8_t* buffer, const char* string, uint32_t key, uint32_t bytes)
{
    uint32_t result;
    __asm {
        mov eax, buffer
        mov ecx, string
        push bytes
        push key
        call code
        add esp, 8
        mov result, eax
    }
    return result;
}

void TestStructBufferStubs()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestStructBufferStubs skipped (the memory manager is already set up)");
        return;
    }
    StandIns& s = *new StandIns();
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        if (r.args[0] == 0x35115A20)
            return reinterpret_cast<uint32_t>(NameArray(0));
        return reinterpret_cast<uint32_t>(NameArray(1));
    });
    g_bufArena = BufArena(true);
    std::memset(g_bufArena, 0, kHeapOffset);
    BuildNameArrays();
    bool same = true;
    uint8_t a[64], b[64];
    auto compare = [&](uint32_t ra, uint32_t rb, const char* what, int which) {
        if (ra != rb || std::memcmp(a, b, sizeof(a)) != 0) {
            Log("  %s %d: original %u ours %u", what, which, ra, rb);
            same = false;
        }
    };
    const int integers[] = {0, 1, 0xFF, 0x100, 0xFFFF, 0x10000, -1, -0x80, -0x81, -0x8000, -0x8001, 0x7FFFFFFF};
    const uint32_t names[] = {0x517A0003, 0x4D7A0100, 0x60001234};
    int which = 0;
    for (uint32_t name : names) {
        for (int value : integers) {
            for (uint32_t size : {64u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u}) {
                std::memset(a, 0xEE, sizeof(a));
                std::memset(b, 0xEE, sizeof(b));
                uint32_t ra = CallInteger(OriginalFunction(0x004798F0), name, value, a, size);
                uint32_t rb = CallInteger(reinterpret_cast<void*>(&Script::WriteIntegerStub), name, value, b, size);
                compare(ra, rb, "integer", which++);
            }
        }
        for (float value : {0.0f, -0.0f, 1e-5f, -1e-5f, 0.0001f, 0.00010001f, -2.5f, 1e30f}) {
            for (uint32_t size : {64u, 4u, 5u, 8u, 9u}) {
                std::memset(a, 0xEE, sizeof(a));
                std::memset(b, 0xEE, sizeof(b));
                uint32_t ra = CallFloat(OriginalFunction(0x00479A50), a, name, size, value);
                uint32_t rb = CallFloat(reinterpret_cast<void*>(&Script::WriteFloatStub), b, name, size, value);
                compare(ra, rb, "float", which++);
            }
        }
        for (const char* string : {"", "abc", "0123456789"}) {
            uint32_t length = static_cast<uint32_t>(std::strlen(string));
            for (uint32_t size : {64u, length + 5, length + 6, length + 7}) {
                for (int local = 0; local < 2; ++local) {
                    std::memset(a, 0xEE, sizeof(a));
                    std::memset(b, 0xEE, sizeof(b));
                    void* original = OriginalFunction(local ? 0x00479B20 : 0x00479AC0);
                    void* ours = local ? reinterpret_cast<void*>(&Script::WriteLocalStringStub)
                                       : reinterpret_cast<void*>(&Script::WriteStringStub);
                    uint32_t ra = CallString(original, a, string, name, size);
                    uint32_t rb = CallString(ours, b, string, name, size);
                    compare(ra, rb, "string", which++);
                }
            }
        }
    }
    Check(same, "structure buffers: the register entry points (integers, floats, strings)");
    RemoveStandIns();
}
SELFTEST(Early, TestStructBufferStubs, RemoveStandIns);

} // namespace
