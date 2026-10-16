// Self-tests for the pre-compiled .qb format (src/game/script/precompiled.cpp) and UnloadQB (load.cpp): the
// originals against ours on identical copies of a generated pre-compiled image and stand-in symbol tables.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/file/pip.h"
#include "game/script/load.h"
#include "game/script/parse.h"
#include "game/script/precompiled.h"
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
using Script::CComponent;
using Script::CStruct;
using Script::CSymbolTableEntry;

// --- a pre-compiled image builder ---------------------------------------------------------------------------

struct Image {
    std::vector<uint8_t> b;
    uint32_t pos() const { return static_cast<uint32_t>(b.size()); }
    uint32_t u32(uint32_t v)
    {
        uint32_t at = pos();
        for (int i = 0; i < 4; ++i)
            b.push_back(static_cast<uint8_t>(v >> (8 * i)));
        return at;
    }
    uint32_t f32(float f)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return u32(bits);
    }
    void set(uint32_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
    void pad()
    {
        while (b.size() & 3)
            b.push_back(0xEE);
    }
    uint32_t text(const char* t)
    {
        uint32_t at = pos();
        for (; *t; ++t)
            b.push_back(static_cast<uint8_t>(*t));
        b.push_back(0);
        return at;
    }
};
// Emits a value's data and returns the word stored for it (an offset, or the value itself).
using Value = std::function<uint32_t(Image&)>;

std::vector<uint32_t> g_records, g_components, g_structs, g_arrays;

Value Literal(uint32_t v)
{
    return [v](Image&) { return v; };
}
Value FloatBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return Literal(bits);
}
Value String(const char* t)
{
    return [t](Image& im) {
        uint32_t at = im.text(t);
        im.pad();
        return at;
    };
}
// A string array's element: the texts are padded once, after the last.
Value RawText(const char* t)
{
    return [t](Image& im) { return im.text(t); };
}
Value Pair(float x, float y)
{
    return [=](Image& im) {
        uint32_t at = im.u32(0x11);
        im.f32(x);
        im.f32(y);
        return at;
    };
}
Value Vector(float x, float y, float z)
{
    return [=](Image& im) {
        uint32_t at = im.u32(0x22);
        im.f32(x);
        im.f32(y);
        im.f32(z);
        return at;
    };
}
uint32_t g_firstBlock;
// {checksum, size, stored size, code}
Value ScriptBlock(uint32_t checksum, std::vector<uint8_t> code)
{
    return [=](Image& im) {
        uint32_t at = im.u32(checksum);
        im.u32(static_cast<uint32_t>(code.size()) + 9);
        im.u32(static_cast<uint32_t>(code.size()));
        for (uint8_t c : code)
            im.b.push_back(c);
        im.pad();
        return at;
    };
}
// A script component's tokens.
Value Tokens(std::vector<uint8_t> code)
{
    return [=](Image& im) {
        uint32_t at = im.pos();
        for (uint8_t c : code)
            im.b.push_back(c);
        im.pad();
        return at;
    };
}
struct Comp {
    uint8_t typeByte; // the type << 1 (and a flag)
    uint32_t name;
    Value value;
};
Value Struct(std::vector<Comp> comps)
{
    return [=](Image& im) {
        uint32_t at = im.u32(0x00000100);
        uint32_t link = im.u32(0);
        g_structs.push_back(at);
        for (const Comp& c : comps) {
            uint32_t component = im.u32(c.typeByte | 0x00330000u);
            g_components.push_back(component);
            im.u32(c.name);
            uint32_t valueAt = im.u32(0);
            uint32_t nextAt = im.u32(0);
            im.set(link, component);
            link = nextAt;
            im.set(valueAt, c.value(im));
        }
        return at;
    };
}
Value Array(uint8_t type, std::vector<Value> elements, bool nullTable = false)
{
    return [=](Image& im) {
        uint32_t at = im.u32(static_cast<uint32_t>(type) << 8 | 0x44000000u);
        g_arrays.push_back(at);
        uint32_t size = static_cast<uint32_t>(elements.size());
        im.u32(size);
        uint32_t slot = im.u32(0x5555);
        if (size > 1) {
            uint32_t table = im.pos();
            for (uint32_t i = 0; i < size; ++i)
                im.u32(0);
            im.set(slot, nullTable ? 0 : table);
            for (uint32_t i = 0; i < size; ++i)
                im.set(table + 4 * i, elements[i](im));
        } else if (size == 1) {
            im.set(slot, elements[0](im));
        }
        if (type == 3 || type == 4)
            im.pad();
        return at;
    };
}
void AddRecord(Image& im, uint8_t type, uint8_t flags, uint32_t name, const Value& value)
{
    uint32_t at = im.u32(flags | static_cast<uint32_t>(type) << 8 | 0x66000000u);
    g_records.push_back(at);
    im.u32(name);
    im.u32(0x5A5A5A5A);
    uint32_t valueAt = im.u32(0);
    im.u32(0xDEADBEEF);
    im.set(valueAt, value(im));
}

// Every kind of symbol, component and array element, with the odd cases (a script whose value isn't the
// block after it, empty structures, arrays of 0/1/more elements, a null element table, unknown types), and
// names that replace earlier ones (in the file and in the table) or share their bucket.
std::vector<uint8_t> BuildImage()
{
    g_records.clear();
    g_components.clear();
    g_structs.clear();
    g_arrays.clear();
    Image im;
    im.u32(0xB075EF6F);
    uint32_t sizeAt = im.u32(0);
    for (int i = 0; i < 5; ++i)
        im.u32(0x0C0C0C0C);
    AddRecord(im, 1, 4, 0x1001, Literal(5)); // (kept: replaced below)
    AddRecord(im, 2, 1, 0x1002, FloatBits(1.5f));
    AddRecord(im, 3, 0, 0x1003, String("hello"));
    AddRecord(im, 4, 0, 0x1004, String("abc"));
    AddRecord(im, 3, 4, 0x1005, String(""));
    AddRecord(im, 5, 0, 0x1006, Pair(1.0f, 2.0f));
    AddRecord(im, 6, 0, 0x1007, Vector(1.0f, 2.0f, 3.0f));
    AddRecord(im, 7, 5, 0x1008, [](Image& i) {
        g_firstBlock = ScriptBlock(0xC0DE, {0x16, 1, 2, 3, 4})(i);
        return g_firstBlock;
    });
    // (the value is the earlier block; the size comes from the block after the entry)
    AddRecord(im, 7, 1, 0x1009, [](Image& i) {
        ScriptBlock(0xC0DF, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10})(i);
        return g_firstBlock;
    });
    AddRecord(im, 8, 0, 0x100A, Literal(0x00401000));
    AddRecord(im, 9, 0, 0x100B, Literal(0x00402000));
    AddRecord(im, 0x0B, 0, 0x100C, Literal(0x33));
    AddRecord(im, 0x0D, 0, 0x100D, Literal(0x2222));
    AddRecord(im, 0x00, 0, 0x100E, Literal(3));
    AddRecord(im, 0x14, 0, 0x100F, Literal(0x77));
    AddRecord(im, 0x0A, 0, 0x2001,
           Struct({
               {1 << 1, 0x11, Literal(5)},
               {2 << 1 | 1, 0x12, FloatBits(2.5f)},
               {3 << 1, 0x13, String("xy")},
               {4 << 1 | 1, 0x14, String("12345")},
               {5 << 1, 0x15, Pair(3.0f, 4.0f)},
               {6 << 1, 0x16, Vector(5.0f, 6.0f, 7.0f)},
               {7 << 1, 0x17, Tokens({0x16, 1, 2, 3, 4, 0x01, 0x24})},
               {7 << 1 | 1, 0x18, Tokens({0x16, 1, 2, 3, 4, 0x01, 0x24, 0x02, 9, 0, 0, 0})},
               {7 << 1, 0x19, Tokens({0x01, 0x24, 0x02, 7, 0, 0, 0})},
               {8 << 1, 0x1A, Literal(0x00401000)},
               {9 << 1, 0x1B, Literal(0x00401010)},
               {0x0B << 1, 0x1C, Literal(0x44)},
               {0x0D << 1, 0x1D, Literal(0x1234)},
               {0, 0x1E, Literal(0x99)},
               {0x14 << 1, 0x1F, Literal(0x88)},
               {0x0A << 1, 0x20, Struct({{1 << 1, 0x21, Literal(1)}, {3 << 1, 0x22, String("in")}})},
               {0x0A << 1, 0x23, Struct({})},
               {0x0C << 1, 0x24, Array(1, {Literal(1), Literal(2), Literal(3)})},
               {0x0C << 1 | 1, 0x25, Array(3, {RawText("s1"), RawText("s22")})},
           }));
    AddRecord(im, 0x0A, 0, 0x2002, Struct({}));
    AddRecord(im, 0x0A, 0, 0x2003,
           Struct({{0x0D << 1, 0x31, Literal(0x31)}, {0x0A << 1, 0x32, Struct({{3 << 1, 0x33, String("end")}})}}));
    AddRecord(im, 0x0A, 0, 0x2004, Struct({{0x0C << 1, 0x34, Array(6, {Vector(1, 1, 1), Vector(2, 2, 2)})}}));
    AddRecord(im, 0x0C, 0, 0x3001, Array(1, {}));
    AddRecord(im, 0x0C, 0, 0x3002, Array(1, {Literal(7)}));
    AddRecord(im, 0x0C, 0, 0x3003, Array(1, {Literal(1), Literal(2), Literal(3)}));
    AddRecord(im, 0x0C, 0, 0x3004, Array(1, {Literal(1), Literal(2)}, true));
    AddRecord(im, 0x0C, 0, 0x3005, Array(3, {RawText("hello")}));
    AddRecord(im, 0x0C, 0, 0x3006, Array(3, {RawText("a"), RawText("bcd"), RawText("")}));
    AddRecord(im, 0x0C, 0, 0x3007, Array(4, {RawText("xy"), RawText("z")}));
    AddRecord(im, 0x0C, 0, 0x3008, Array(5, {Pair(1, 2), Pair(3, 4)}));
    AddRecord(im, 0x0C, 0, 0x3009, Array(5, {Pair(5, 6)}));
    AddRecord(im, 0x0C, 0, 0x300A, Array(6, {Vector(1, 2, 3), Vector(4, 5, 6), Vector(7, 8, 9)}));
    AddRecord(im, 0x0C, 0, 0x300B,
           Array(10, {Struct({{1 << 1, 0x41, Literal(4)}}), Struct({{3 << 1, 0x42, String("st")}, {1 << 1, 0x43, Literal(2)}})}));
    AddRecord(im, 0x0C, 0, 0x300C, Array(10, {Struct({{5 << 1, 0x44, Pair(9, 9)}})}));
    AddRecord(im, 0x0C, 0, 0x300D, Array(10, {}));
    AddRecord(im, 0x0C, 0, 0x300E,
           Array(12, {Array(1, {Literal(8), Literal(9)}), Array(3, {RawText("q")}), Array(0x0A, {})}));
    AddRecord(im, 0x0C, 0, 0x300F, Array(12, {Array(5, {Pair(7, 7)})}));
    AddRecord(im, 0x0C, 0, 0x3010, Array(7, {Literal(0x10), Literal(0x20)}));
    AddRecord(im, 0x0C, 0, 0x3011, Array(13, {Literal(0xAB)}));
    AddRecord(im, 0x0C, 0, 0x3012, Array(2, {FloatBits(1), FloatBits(2), FloatBits(3)}));
    AddRecord(im, 0x0C, 0, 0x3013, Array(0, {Literal(0x61), Literal(0x62)}));
    AddRecord(im, 0x0C, 0, 0x3014, Array(9, {Literal(0x71), Literal(0x72)}));
    AddRecord(im, 0x0C, 0, 0x3015, Array(0x0B, {Literal(0x81), Literal(0x82)}));
    // Replacing: an earlier record of the file (kept: only unlinked), and symbols already in the table.
    AddRecord(im, 1, 4, 0x1001, Literal(99));
    AddRecord(im, 1, 0, 0x5001, Literal(1));
    AddRecord(im, 3, 0, 0x1003 + 0x10000, String("bucket"));
    im.set(sizeAt, im.pos());
    return im.b;
}

alignas(16) uint8_t g_qbImage[0x2000];
// Symbols already in the table (all but the first kept: removing them only unlinks them).
CSymbolTableEntry g_existing[4];
CSymbolTableEntry* g_qbTable[0x1000];

uint32_t g_imageSize;
uint32_t ImageWord(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(g_qbImage);
    auto existing = reinterpret_cast<uint32_t>(g_existing);
    if (word >= base && word <= base + sizeof(g_qbImage))
        return 0xB0000000u + (word - base);
    if (word >= existing && word < existing + sizeof(g_existing))
        return 0xE0000000u + (word - existing);
    return word;
}
void FreshImage(const std::vector<uint8_t>& image)
{
    std::memset(g_qbImage, 0xCC, sizeof(g_qbImage));
    std::memcpy(g_qbImage, image.data(), image.size());
    g_imageSize = static_cast<uint32_t>(image.size());
}
void PushImage(std::vector<uint32_t>& trace)
{
    for (uint32_t i = 0; i < (g_imageSize + 3) / 4; ++i)
        trace.push_back(ImageWord(reinterpret_cast<uint32_t*>(g_qbImage)[i]));
}
void FreshTable()
{
    std::memset(g_qbTable, 0, sizeof(g_qbTable));
    std::memset(g_existing, 0, sizeof(g_existing));
    const uint32_t names[4] = {0x5001, 0x7001, 0x1003, 0x8003};
    for (int i = 0; i < 4; ++i) {
        g_existing[i].unknown0 = i == 0 ? 0 : 4; // (the first is freed: FreeSymbol is stood in for)
        g_existing[i].type = Script::ESYMBOLTYPE_INTEGER;
        g_existing[i].name = names[i];
        g_existing[i].raw = 0x100 + i;
        Script::LinkSymbol(&g_existing[i]);
    }
}
void PushTable(std::vector<uint32_t>& trace)
{
    for (CSymbolTableEntry* entry : g_qbTable)
        trace.push_back(ImageWord(reinterpret_cast<uint32_t>(entry)));
    for (const CSymbolTableEntry& e : g_existing) {
        const auto* words = reinterpret_cast<const uint32_t*>(&e);
        for (int i = 0; i < 5; ++i)
            trace.push_back(ImageWord(words[i]));
    }
}

std::vector<uint32_t> g_callbacks;
std::vector<uint32_t> g_frees;
void __cdecl RecordingCallback(CSymbolTableEntry* symbol, uint32_t argument, uint32_t flag)
{
    g_callbacks.insert(g_callbacks.end(), {ImageWord(reinterpret_cast<uint32_t>(symbol)), ImageWord(symbol->raw),
                                           ImageWord(reinterpret_cast<uint32_t>(symbol->next)), argument, flag});
}

void CallElements(bool original, CArray* array, uint32_t base)
{
    if (original)
        Original<void(__thiscall*)(CArray*, uint32_t)>(0x0046C7B0)(array, base);
    else
        Script::RelocateArrayElements(array, base);
}

// (the symbol table and the other globals are put back even when a test crashes)
std::vector<Saved> g_qbSaved;
void RestoreQbGlobals()
{
    RemoveStandIns();
    if (!g_qbSaved.empty()) {
        RestoreAll(g_qbSaved);
        g_qbSaved.clear();
    }
}

// The pre-compiled format: each relocation function on every item of the image (fresh copy each time: return
// values and the whole image), the walk over the symbols with a recording callback (and none, and an empty
// file), ParsePrecompiledQB into a stand-in table holding symbols of the file's names, and LoadQBData's
// pre-compiled branch.
void TestPrecompiledQB()
{
    using namespace Script;
    g_qbSaved = {{0x006F3624, 4, {}}, {0x006F3538, 4, {}}, {0x006F363C, 8, {}}};
    SaveAll(g_qbSaved);
    GameVariable<CSymbolTableEntry**>(0x006F3624) = g_qbTable;
    GameVariable<void*>(0x006F3538) = nullptr; // no scripts running
    const std::vector<uint8_t> image = BuildImage();
    auto* standIns = new StandIns;
    standIns->At({0x004700D0, AddressOf(&Script::FreeSymbol)}, 0, [](Regs& r) {
        g_frees.push_back(ImageWord(r.args[0]));
        return 0u;
    });
    if (image.size() > sizeof(g_qbImage)) {
        Check(false, "pre-compiled .qb: test image too big");
        RestoreQbGlobals();
        return;
    }
    auto base = reinterpret_cast<uint32_t>(g_qbImage);
    auto at = [&](uint32_t offset) { return g_qbImage + offset; };
    using Relocator = uint8_t*(__cdecl*)(void*, uint32_t);

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        g_frees.clear();
        auto each = [&](const std::vector<uint32_t>& offsets, uint32_t address, auto ours) {
            for (uint32_t offset : offsets) {
                FreshImage(image);
                uint8_t* end = original ? Original<Relocator>(address)(at(offset), base) : ours(at(offset));
                trace.push_back(ImageWord(reinterpret_cast<uint32_t>(end)));
                PushImage(trace);
            }
        };
        each(g_records, 0x0046C970, [&](uint8_t* p) { return RelocateSymbol(reinterpret_cast<CSymbolTableEntry*>(p), base); });
        each(g_components, 0x0046C6B0, [&](uint8_t* p) { return RelocateComponent(reinterpret_cast<CComponent*>(p), base); });
        each(g_structs, 0x0046C7F0, [&](uint8_t* p) { return RelocateStruct(reinterpret_cast<CStruct*>(p), base); });
        each(g_arrays, 0x0046C830, [&](uint8_t* p) { return RelocateArray(reinterpret_cast<CArray*>(p), base); });
        for (uint32_t offset : g_arrays) {
            FreshImage(image);
            // (the element table itself is relocated first, by RelocateArray)
            auto* array = reinterpret_cast<CArray*>(at(offset));
            if (array->m_size > 1) {
                if (!array->m_elements)
                    continue;
                array->m_elements = reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t>(array->m_elements) + base);
            }
            CallElements(original, array, base);
            PushImage(trace);
        }
        // The walk: with a callback, without, and over a file with no symbols.
        using Walk = void(__cdecl*)(uint8_t*, uint32_t, PrecompiledSymbolCallback, uint32_t, uint32_t);
        auto walk = [&](PrecompiledSymbolCallback callback, uint32_t argument, uint32_t flag) {
            g_callbacks.clear();
            if (original)
                Original<Walk>(0x0046CA50)(g_qbImage, base, callback, argument, flag);
            else
                RelocatePrecompiledSymbols(g_qbImage, base, callback, argument, flag);
            trace.insert(trace.end(), g_callbacks.begin(), g_callbacks.end());
            trace.push_back(0xCA11);
            PushImage(trace);
        };
        FreshImage(image);
        walk(RecordingCallback, 0x12345678, 0xABCD0001);
        FreshImage(image);
        walk(nullptr, 1, 2);
        FreshImage(image);
        *reinterpret_cast<uint32_t*>(g_qbImage + 4) = 0x1C;
        walk(RecordingCallback, 3, 4);
        // Defining a file's symbols.
        for (int unused : {1, 0}) {
            FreshImage(image);
            FreshTable();
            if (original)
                Original<void(__cdecl*)(const char*, uint8_t*, int)>(0x0046FBA0)("levels\\test\\test.qb.xbx", g_qbImage, unused);
            else
                ParsePrecompiledQB("levels\\test\\test.qb.xbx", g_qbImage, unused);
            PushImage(trace);
            PushTable(trace);
            trace.insert(trace.end(), g_frees.begin(), g_frees.end());
            trace.push_back(0xF4EE);
            g_frees.clear();
        }
        // The callback alone, on an already relocated symbol (replacing an existing one, then a new name).
        FreshImage(image);
        FreshTable();
        RelocatePrecompiledSymbols(g_qbImage, base, nullptr, 0, 0);
        for (uint32_t offset : {g_records[g_records.size() - 2], g_records[1], g_records[0]}) {
            auto* symbol = reinterpret_cast<CSymbolTableEntry*>(at(offset));
            if (original)
                Original<PrecompiledSymbolCallback>(0x00478DE0)(symbol, 0x0F11E000 + offset, 7);
            else
                DefinePrecompiledSymbol(symbol, 0x0F11E000 + offset, 7);
            PushTable(trace);
            trace.insert(trace.end(), g_frees.begin(), g_frees.end());
            g_frees.clear();
        }
        PushImage(trace);
        // LoadQBData's pre-compiled branch (then restarting changed scripts: none running).
        FreshImage(image);
        FreshTable();
        if (original)
            Original<void(__cdecl*)(const char*, const uint8_t*, int, int, int)>(0x0046ED90)("pre.qb.xbx", g_qbImage, 0, 1, 1);
        else
            LoadQBData("pre.qb.xbx", g_qbImage, 0, 1, 1);
        PushImage(trace);
        PushTable(trace);
        trace.insert(trace.end(), g_frees.begin(), g_frees.end());
        g_frees.clear();
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreQbGlobals();
    Compare(expected, actual, "pre-compiled .qb: relocating every item, the symbol walk, defining the symbols");
}

// --- UnloadQB -------------------------------------------------------------------------------------------------

alignas(16) uint8_t g_unloadArenas[2][0x20000];
alignas(16) uint8_t g_fakeScripts[4][0x100];

uint32_t ScriptId(uint32_t p)
{
    auto base = reinterpret_cast<uint32_t>(g_fakeScripts);
    return p >= base && p < base + sizeof(g_fakeScripts) ? 0x5C000000u + (p - base) : p;
}

// UnloadQB: no file (0), a file with symbols of several kinds (kept or not, in different buckets, between other
// files' symbols), a file with none, then one more; scripts still defined or not (CScript's methods stood in for,
// with Pip::Unload); original vs ours over the same stand-in heap and symbol table.
void TestUnloadQB()
{
    using namespace Script;
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestUnloadQB skipped (the memory manager is already set up)");
        return;
    }
    g_qbSaved = {{0x007CC979, 1, {}},  {0x006F2338, 16, {}}, {0x006F3614, 16, {}}, {0x006F22B4, 16, {}},
                 {0x006F3644, 16, {}}, {0x006F3654, 16, {}}, {0x006F362C, 16, {}}, {0x006F3570, 24, {}},
                 {0x006F3624, 4, {}},  {0x006F3538, 4, {}},  {0x006F363C, 8, {}},  {0x007D1800, 4, {}}};
    SaveAll(g_qbSaved);
    GameVariable<CSymbolTableEntry**>(0x006F3624) = g_qbTable;

    auto run = [&](bool original) {
        g_trace.clear();
        for (Saved& s : g_qbSaved) {
            if (s.address != 0x006F3624)
                std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        }
        GameVariable<bool>(0x007CC979) = true;
        std::memset(g_qbTable, 0, sizeof(g_qbTable));
        uint8_t* arena = g_unloadArenas[original ? 0 : 1];
        std::memset(arena, 0, sizeof(g_unloadArenas[0]));
        std::memset(arena + 0x1000, 0xCD, sizeof(g_unloadArenas[0]) - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + 0x10000);
        auto* heap = new (arena + 0x620) Heap(region, 1, "script");
        auto* topRegion = new (arena + 0x6C0) Region(arena + 0x10000, arena + sizeof(g_unloadArenas[0]));
        auto* topHeap = new (arena + 0x6E0) Heap(topRegion, 1, "top down");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = topHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = heap;
        manager->m_topDownHeap = topHeap;
        manager->PushContext(heap);
        auto arenaBase = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= arenaBase && word <= arenaBase + sizeof(g_unloadArenas[0]) ? 0xA0000000u + (word - arenaBase)
                                                                                       : ScriptId(word);
        };

        // Scripts: 0 still defined, 1 and 2 not, 3 still defined.
        std::memset(g_fakeScripts, 0, sizeof(g_fakeScripts));
        for (int i = 0; i < 4; ++i) {
            *reinterpret_cast<uint8_t**>(g_fakeScripts[i]) = i < 3 ? g_fakeScripts[i + 1] : nullptr;
            g_fakeScripts[i][0x40] = i == 0 || i == 3;
        }
        GameVariable<void*>(0x006F3538) = g_fakeScripts[0];
        auto* s = new StandIns;
        s->At({0x00472C90}, 0, [](Regs& r) {
            Record({0xC90, ScriptId(r.ecx)});
            return static_cast<uint32_t>(reinterpret_cast<uint8_t*>(r.ecx)[0x40]) | 0xABCDEF00u;
        });
        s->At({0x00473A80}, 0, [](Regs& r) {
            Record({0xA80, ScriptId(r.ecx)});
            reinterpret_cast<uint8_t*>(r.ecx)[0x40] = 1; // (defined again: not stopped twice)
            return 0u;
        });
        s->At({0x00473010}, 0, [](Regs& r) {
            Record({0x010, ScriptId(r.ecx)});
            return 0u;
        });
        s->At({0x005B7200, AddressOf(static_cast<void (*)(uint32_t)>(&Pip::Unload))}, 0, [](Regs& r) {
            Record({0x7200, r.args[0]});
            return 0u;
        });

        // Symbols of files A, B and C (and kept ones), in several buckets.
        const uint32_t fileA = 0xAAAA0001, fileB = 0xBBBB0002, fileC = 0xCCCC0003;
        auto symbol = [&](uint32_t name, uint32_t file, bool kept, ESymbolType type, uint32_t value) {
            CSymbolTableEntry* entry = CreateSymbol(name);
            entry->unknown8 = file;
            entry->type = type;
            entry->raw = value;
            if (kept)
                entry->unknown0 |= 4;
            return entry;
        };
        symbol(0x0101, fileA, false, ESYMBOLTYPE_INTEGER, 1);
        symbol(0x0102, fileB, false, ESYMBOLTYPE_INTEGER, 2);
        symbol(0x1101, fileA, true, ESYMBOLTYPE_STRING, 0x12345678);
        symbol(0x0FFF, fileA, false, ESYMBOLTYPE_QSCRIPT, reinterpret_cast<uint32_t>(Mem::Malloc(24)));
        symbol(0x2101, fileB, false, ESYMBOLTYPE_FLOAT, 0x3F800000);
        symbol(0x0000, fileA, false, ESYMBOLTYPE_NAME, 0x55);
        symbol(0x0500, fileC, false, ESYMBOLTYPE_INTEGER, 5);
        symbol(0x3101, fileA, true, ESYMBOLTYPE_INTEGER, 6);
        symbol(0x0501, 0, false, ESYMBOLTYPE_INTEGER, 7);

        for (uint32_t file : {0u, fileA, 0xDDDD0004u, fileC, 0u}) {
            if (original)
                Original<void(__cdecl*)(uint32_t)>(0x0046EEC0)(file);
            else
                UnloadQB(file);
            Record({0xF11E, file});
            for (CSymbolTableEntry* entry = GetNextSymbol(nullptr); entry; entry = GetNextSymbol(entry))
                Record({entry->name, entry->unknown8, relocate(reinterpret_cast<uint32_t>(entry))});
            for (int i = 0; i < 4; ++i)
                g_fakeScripts[i][0x40] = i == 0;
        }
        RemoveStandIns();

        std::vector<uint32_t> trace = g_trace;
        for (uint32_t& word : trace)
            word = relocate(word);
        std::vector<uint32_t> image(sizeof(g_unloadArenas[0]) / 4);
        std::memcpy(image.data(), arena, sizeof(g_unloadArenas[0]));
        for (uint32_t& word : image)
            word = relocate(word);
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreQbGlobals();
    Compare(expected, actual, "UnloadQB: symbols of a file removed, scripts stopped, the file unloaded");
}

} // namespace

SELFTEST(Early, TestPrecompiledQB, RestoreQbGlobals);
SELFTEST(Early, TestUnloadQB, RestoreQbGlobals);
