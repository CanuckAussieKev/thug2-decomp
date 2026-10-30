#include "game/script/qb.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/memory_sizes.h"
#include "game/core/pool.h"
#include "game/mth/random.h"
#include "game/script/cache.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/script/parse.h"
#include "game/script/cscript.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace Script {

static_assert(offsetof(CStoredRandom, m_count) == 8, "layout must match the original");
static_assert(offsetof(CStoredRandom, m_order) == 0x12, "layout must match the original");
static_assert(offsetof(CStoredRandom, m_last) == 0x54 && offsetof(CStoredRandom, m_next) == 0x58,
              "layout must match the original");
static_assert(sizeof(CStoredRandom) == 0x60, "layout must match the original");

GAME_GLOBAL(bool, g_scriptFlag6F2564, 0x006F2564);
GAME_GLOBAL(Mem::PoolPair, g_storedRandomPools, 0x006F2578);
GAME_GLOBAL(int, g_pendingSymbolCount, 0x006F3528);
GAME_GLOBAL(uint32_t, g_permanentStringsFree, 0x006F3588);

namespace {

// The table of the .qb files' own checksum names: {checksum, name, next}, the first entry in the bucket.
struct ScriptChecksumName {
    uint32_t checksum;
    char* name;
    ScriptChecksumName* next;
};
GAME_GLOBAL(ScriptChecksumName*, g_scriptNames, 0x006F2368);
ScriptChecksumName*& ScriptNames() { return g_scriptNames; }

char* CopyString(const char* string)
{
    size_t size = std::strlen(string) + 1;
    auto* copy = static_cast<char*>(Mem::Malloc(static_cast<unsigned int>(size)));
    std::memcpy(copy, string, size);
    return copy;
}

} // namespace

// Create a pool of `count` items of `item_size` as the current pool of `pools`.
void CreatePool(Mem::PoolPair& pools, int item_size, int count, const char* name)
{
    if (!GameVariable<bool>(0x007CC979))
        Mem::EnsureManager();
    void* memory = Mem::Manager::Get()->Allocate(sizeof(Mem::Pool), 1, nullptr);
    Mem::Pool* pool = memory ? new (memory) Mem::Pool(item_size, count, name) : nullptr;
    pools.pools[pools.current] = pool;
    reinterpret_cast<uint8_t*>(&pools.unknown4)[pools.current] = 1;
}


// 0x0046F690
const uint8_t* SkipToken(const uint8_t* token)
{
    switch (*token) {
    case 0x01: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A:
    case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2C: case 0x2D: case 0x30: case 0x32: case 0x33: case 0x34: case 0x35:
    case 0x36: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
    case 0x42: case 0x45:
        return token + 1;
    case 0x02: case 0x16: case 0x17: case 0x18: case 0x1A: case 0x2E: case 0x43: case 0x44: case 0x46:
        return token + 5;
    case 0x1B: case 0x1C: // strings: the length, then the text
        return token + 5 + Read4Bytes(token + 1);
    case 0x1E: // vector
        return token + 13;
    case 0x1F: // pair
        return token + 9;
    case 0x2B: { // a checksum's name: the checksum, then the text
        const uint8_t* p = token + 5;
        while (*p)
            ++p;
        return p + 1;
    }
    case 0x2F: case 0x37: case 0x40: case 0x41: // tables of 6-byte entries
        return token + 5 + Read4Bytes(token + 1) * 6;
    case 0x47: case 0x48: case 0x49:
        return token + 3;
    default: // (0x00, 0x10, 0x19, 0x1D, 0x2A, 0x31 and unknown tokens: no step)
        return token;
    }
}

// 0x0046F7E0
uint32_t Read4Bytes(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// 0x0046F800
uint16_t Read2Bytes(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// 0x0046F810
uint8_t* Write4Bytes(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
    return p + 4;
}

// 0x0046F840
uint8_t* Write2Bytes(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    return p + 2;
}

// 0x0046F850
const uint8_t* SkipEndOfLines(const uint8_t* token)
{
    for (;;) {
        if (*token == 0x01)
            token += 1;
        else if (*token == 0x02)
            token += 5;
        else
            return token;
    }
}

// 0x0046FB30
const uint8_t* SkipToEndOfStatement(const uint8_t* token)
{
    int braces = 0, brackets = 0, parentheses = 0;
    for (;;) {
        uint8_t t = *token;
        if (t == 0x03)
            ++braces;
        if (t == 0x04)
            --braces;
        if (t == 0x05)
            ++brackets;
        if (t == 0x06)
            --brackets;
        if (t == 0x0E)
            ++parentheses;
        if (t == 0x0F)
            --parentheses;
        if (braces == 0 && brackets == 0 && parentheses == 0 && (t == 0x01 || t == 0x02 || t == 0x00))
            return SkipToken(token);
        token = SkipToken(token);
    }
}

// 0x0046F920
const uint8_t* SkipToEndOfScript(const uint8_t* token)
{
    while (*token != 0x24)
        token = SkipToken(token);
    token = SkipToken(token);
    if (*token == 0x02)
        return SkipToken(token);
    return token;
}

// 0x0046F960
uint32_t CalculateScriptChecksum(const uint8_t* token)
{
    uint32_t checksum = 0xFFFFFFFF;
    while (*token != 0x24) {
        const uint8_t* next = SkipToken(token);
        if (*token != 0x01 && *token != 0x02)
            checksum = Crc::ExtendCRCWithBuffer(token, static_cast<int>(next - token), checksum);
        token = next;
    }
    return checksum;
}

// 0x0046FF00
void PreProcessScript(uint8_t* token)
{
    token = const_cast<uint8_t*>(SkipToEndOfStatement(token));
    while (*token != 0x24) {
        if (*token == 0x16) {
            // A statement starting with a name: a call?
            uint8_t* name = token;
            token += 5;
            if (CSymbolTableEntry* symbol = LookUpSymbol(Read4Bytes(name + 1))) {
                if (symbol->type == ESYMBOLTYPE_CFUNCTION) {
                    name[0] = 0x43;
                    Write4Bytes(name + 1, symbol->raw);
                } else if (symbol->type == ESYMBOLTYPE_MEMBERFUNCTION) {
                    if (symbol->raw) {
                        Write4Bytes(name + 1, symbol->raw);
                        name[0] = 0x46;
                    } else {
                        name[0] = 0x44;
                    }
                }
            }
            token = const_cast<uint8_t*>(SkipToEndOfStatement(token));
        } else if (*token == 0x47) {
            token = const_cast<uint8_t*>(SkipToken(token));
            if (*token == 0x39)
                ++token;
        } else {
            token = const_cast<uint8_t*>(SkipToEndOfStatement(token));
        }
    }
}

// 0x0046F9B0
const char* FindScriptChecksumName(uint32_t checksum)
{
    if (!ScriptNames())
        return nullptr;
    for (ScriptChecksumName* entry = &ScriptNames()[checksum & 0xFFF]; entry; entry = entry->next) {
        if (entry->checksum == checksum)
            return entry->name;
    }
    return nullptr;
}

// 0x0046F9F0
void FreeScriptChecksumNames()
{
    if (!ScriptNames())
        return;
    for (uint32_t i = 0; i < 0x1000; ++i) {
        ScriptChecksumName& bucket = ScriptNames()[i];
        for (ScriptChecksumName* entry = bucket.next; entry;) {
            ScriptChecksumName* next = entry->next;
            Mem::FreeMalloc(entry->name);
            Mem::FreeMalloc(entry);
            entry = next;
        }
        if (bucket.name)
            Mem::FreeMalloc(bucket.name);
    }
    Mem::FreeMalloc(ScriptNames());
    ScriptNames() = nullptr;
}

namespace {
bool g_keepQbNames;
std::unordered_map<uint32_t, std::string>& KeptNames()
{
    static std::unordered_map<uint32_t, std::string> names;
    return names;
}
} // namespace

void KeepQbNames(bool keep) { g_keepQbNames = keep; }

void NoteQbName(uint32_t checksum, const char* name)
{
    if (g_keepQbNames && name && *name)
        KeptNames()[checksum] = name;
}

const char* KeptQbName(uint32_t checksum)
{
    auto it = KeptNames().find(checksum);
    return it == KeptNames().end() ? nullptr : it->second.c_str();
}

// 0x0046FA60 (a checksum already there is added again)
void AddScriptChecksumName(uint32_t checksum, const char* name)
{
    ScriptChecksumName& bucket = ScriptNames()[checksum & 0xFFF];
    if (bucket.name) {
        auto* entry = static_cast<ScriptChecksumName*>(Mem::Malloc(sizeof(ScriptChecksumName)));
        entry->checksum = checksum;
        entry->name = CopyString(name);
        entry->next = bucket.next;
        bucket.next = entry;
    } else {
        bucket.checksum = checksum;
        bucket.name = CopyString(name);
        bucket.next = nullptr;
    }
    AddChecksumName(checksum, name);
}

// 0x0046F870
void ForgetStoredRandomsOf(uint32_t owner)
{
    // The stored random states (newest first, the next at +0x58), the owning script first.
    for (auto* entry = reinterpret_cast<uint32_t*>(g_storedRandomsNewest); entry;
         entry = reinterpret_cast<uint32_t*>(entry[0x58 / 4])) {
        if (entry[0] == owner)
            entry[0] = 0;
    }
}

// 0x0046F8A0
void CStoredRandom::Shuffle(bool avoidRepeat)
{
    uint8_t last = m_order[m_count - 1];
    for (int swaps = m_count * 10; swaps; --swaps) {
        int a = Mth::Rnd(m_count);
        int b = Mth::Rnd(m_count);
        uint8_t swapped = m_order[a];
        m_order[a] = m_order[b];
        m_order[b] = swapped;
    }
    if (avoidRepeat && m_order[0] == last) {
        int other = Mth::Rnd(m_count - 1) + 1;
        uint8_t swapped = m_order[0];
        m_order[0] = m_order[other];
        m_order[other] = swapped;
    }
}

// 0x00476210
void AllocatePermanentStringHeap(uint32_t size, uint32_t count)
{
    g_permanentStringsSize = size;
    g_permanentStringsFree = count;
    char* strings = static_cast<char*>(Mem::Malloc(size));
    g_permanentStringsStart = strings;
    g_permanentStringsNext = strings;
    GameVariable<void*>(0x006F3580) = Mem::Malloc(count * 8);
    g_permanentStringCount = 0;
}

// 0x0046F520
void CreateScriptPools()
{
    // Not original - the counts are ours (game/core/memory_sizes.h: pool sizes as THUGPRO's, thug2-decomp.ini
    // [memory]); the script region they come from is grown to match (Mem::Manager::InitStandardHeaps).
    // Original behaviour: CScriptCacheEntry 300, CComponent 86000, CStruct 19800, CVector 9500, CPair 800, CArray
    // 7000, CSymbolTableEntry 12500, CScript 300, CStoredRandom 100, the string heap 190000 bytes / 7000 strings.
    const Mem::Sizes::Pools& sizes = Mem::Sizes::Current();
    Mem::PushMemProfile("AllocateChecksumNameLookupTables");
    AllocateChecksumNameLookupTables();
    Mem::PopMemProfile();
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    // (0x0046F090, 0x0046F110, 0x0046F190, 0x0046F210, 0x0046F290, 0x0046F310, 0x0046F390, 0x0046F410,
    // 0x0046F4A0: one function per class)
    Mem::PushMemProfile("CScriptCacheEntry");
    CreatePool(g_cacheEntryPools, 0x14, sizes.scriptCacheEntries, "CScriptCacheEntry");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CComponent and Reserve CComponent");
    CreatePool(g_componentPools, 0x10, sizes.components, "CComponent");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CStruct");
    CreatePool(GameVariable<Mem::PoolPair>(0x006F3614), 0x08, sizes.structs, "CStruct");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CVector");
    CreatePool(g_vectorPools, 0x10, sizes.vectors, "CVector");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CPair");
    CreatePool(g_pairPools, 0x0C, sizes.pairs, "CPair");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CArray");
    CreatePool(g_arrayPools, 0x0C, sizes.arrays, "CArray");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CSymbolTableEntry");
    CreatePool(g_symbolPools, 0x14, sizes.symbols, "CSymbolTableEntry");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CScript");
    CreatePool(g_scriptPools, 0xF4, sizes.scripts, "CScript");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CStoredRandom");
    CreatePool(g_storedRandomPools, 0x60, sizes.storedRandoms, "CStoredRandom");
    Mem::PopMemProfile();
    Mem::PushMemProfile("AllocatePermanentStringHeap");
    AllocatePermanentStringHeap(sizes.stringBytes, sizes.strings);
    Mem::PopMemProfile();
    Mem::PopContext();
    // (not original either: the sizes in the log)
    Log("script pools: CComponent %d, CStruct %d, CVector %d, CPair %d, CArray %d, CSymbolTableEntry %d, CScript %d, "
        "CScriptCacheEntry %d, strings %u bytes / %u (thug2-decomp.ini [memory]; the original's CComponent 86000); "
        "script region %u bytes",
        sizes.components, sizes.structs, sizes.vectors, sizes.pairs, sizes.arrays, sizes.symbols, sizes.scripts,
        sizes.scriptCacheEntries, sizes.stringBytes, sizes.strings, Mem::Sizes::ScriptRegionSize());
}

void LogScriptPoolUse(const char* when)
{
    // Not original (log only).
    struct Named {
        const char* name;
        uint32_t pair;
    };
    static const Named kPools[] = {{"CComponent", 0x006F2338}, {"CStruct", 0x006F3614}, {"CVector", 0x006F3654},
                                   {"CPair", 0x006F3644},      {"CArray", 0x006F22B4},  {"CSymbolTableEntry", 0x006F362C},
                                   {"CScript", 0x006F3548}};
    std::string line;
    for (const Named& p : kPools) {
        int used = 0, peak = 0, count = 0;
        for (Mem::Pool* pool : GameVariable<Mem::PoolPair>(p.pair).pools) {
            if (!pool)
                continue;
            used += pool->m_used;
            peak += pool->m_peakUsed;
            count += pool->m_count;
        }
        char part[96];
        std::snprintf(part, sizeof(part), "%s%s %d (peak %d) of %d", line.empty() ? "" : ", ", p.name, used, peak,
                      count);
        line += part;
    }
    // (and the hash tables' items, Main's HashItem pool - memory_sizes.h hash_item_pool)
    if (Mem::Pool* items = Mem::PoolManager::GetPool(0)) {
        char part[96];
        std::snprintf(part, sizeof(part), ", HashItem %d (peak %d) of %d", items->m_used, items->m_peakUsed,
                      items->m_count);
        line += part;
    }
    Log("script pools in use %s: %s", when, line.c_str());
}

} // namespace Script

namespace {

// The per-class pool creators (cdecl: count, name).
template <uint32_t Pools, int ItemSize>
void CreatePoolOf(int count, const char* name)
{
    Script::CreatePool(GameVariable<Mem::PoolPair>(Pools), ItemSize, count, name);
}

} // namespace

REPLACE(0x0046F690, Script::SkipToken);
REPLACE(0x0046F7E0, Script::Read4Bytes);
REPLACE(0x0046F800, Script::Read2Bytes);
REPLACE(0x0046F810, Script::Write4Bytes);
REPLACE(0x0046F840, Script::Write2Bytes);
REPLACE(0x0046F850, Script::SkipEndOfLines);
REPLACE(0x0046FB30, Script::SkipToEndOfStatement);
REPLACE(0x0046F920, Script::SkipToEndOfScript);
REPLACE(0x0046F960, Script::CalculateScriptChecksum);
REPLACE(0x0046FF00, Script::PreProcessScript);
REPLACE(0x0046F9B0, Script::FindScriptChecksumName);
REPLACE(0x0046F9F0, Script::FreeScriptChecksumNames);
REPLACE(0x0046FA60, Script::AddScriptChecksumName);
REPLACE(0x0046F870, Script::ForgetStoredRandomsOf);
REPLACE(0x0046F8A0, &Script::CStoredRandom::Shuffle);
REPLACE(0x00476210, Script::AllocatePermanentStringHeap);
REPLACE(0x0046F520, Script::CreateScriptPools);
REPLACE(0x0046F090, &CreatePoolOf<0x006F3560, 0x14>);
REPLACE(0x0046F110, &CreatePoolOf<0x006F2338, 0x10>);
REPLACE(0x0046F190, &CreatePoolOf<0x006F3614, 0x08>);
REPLACE(0x0046F210, &CreatePoolOf<0x006F3654, 0x10>);
REPLACE(0x0046F290, &CreatePoolOf<0x006F3644, 0x0C>);
REPLACE(0x0046F310, &CreatePoolOf<0x006F22B4, 0x0C>);
REPLACE(0x0046F390, &CreatePoolOf<0x006F362C, 0x14>);
REPLACE(0x0046F410, &CreatePoolOf<0x006F3548, 0xF4>);
REPLACE(0x0046F4A0, &CreatePoolOf<0x006F2578, 0x60>);

// --- symbol bookkeeping for the loader --------------------------------------------------------------------

namespace Script {

// The script cache (0x006F3558, or null): the scripts' decompressed code, by name.
CScriptCache* ScriptCache() { return g_scriptCache; }

void RemoveFromScriptCache(uint32_t name) { ScriptCache()->Invalidate(name); }

namespace {

// Symbols waiting to be removed (or replaced) once a .qb file has been loaded: {symbol, owned}.
struct PendingSymbol {
    CSymbolTableEntry* symbol;
    bool owned;
};
GAME_GLOBAL(PendingSymbol[500], g_pendingSymbols, 0x006F2588);
PendingSymbol* PendingSymbols() { return g_pendingSymbols; }
int& PendingSymbolCount() { return g_pendingSymbolCount; }


void AddPendingSymbol(CSymbolTableEntry* symbol, bool owned)
{
    PendingSymbol& pending = PendingSymbols()[PendingSymbolCount()];
    pending.symbol = symbol;
    pending.owned = owned;
    ++PendingSymbolCount();
    if (symbol->type == ESYMBOLTYPE_QSCRIPT && ScriptCache())
        RemoveFromScriptCache(symbol->name);
}

} // namespace

// 0x00470000 / 0x0046FFF0
void ClearScriptFlag6F2564() { g_scriptFlag6F2564 = false; }
void SetScriptFlag6F2564() { g_scriptFlag6F2564 = true; }

// 0x00470010
void AllocateScriptChecksumNames(bool debugHeap)
{
    Mem::Manager* manager = Mem::Manager::Get();
    Mem::PushContext(debugHeap ? manager->m_debugHeap : manager->m_scriptHeap);
    auto* names = static_cast<uint32_t*>(Mem::Malloc(0x1000 * 12));
    reinterpret_cast<uint32_t*&>(g_scriptNames) = names;
    Mem::PopContext();
    for (uint32_t i = 0; i < 0x1000 * 3; ++i)
        names[i] = 0;
}

// 0x0044C1D0 / 0x0044C230 / 0x00470070 / 0x004700D0
void FreePair(void* pair) { g_pairPools.Free(pair); }
void FreeVector(void* vector) { g_vectorPools.Free(vector); }
void FreeStoredRandom(void* random) { g_storedRandomPools.Free(random); }
void FreeSymbol(CSymbolTableEntry* symbol) { g_symbolPools.Free(symbol); }

// 0x004711D0
void CleanUpAndRemoveSymbol(CSymbolTableEntry* symbol)
{
    UnlinkSymbol(symbol);
    if (symbol->unknown0 & 4) // (kept: not owned by the table?)
        return;
    switch (symbol->type) {
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        FreeString(reinterpret_cast<const char*>(symbol->raw));
        break;
    case ESYMBOLTYPE_PAIR:
        FreePair(reinterpret_cast<void*>(symbol->raw));
        break;
    case ESYMBOLTYPE_VECTOR:
        FreeVector(reinterpret_cast<void*>(symbol->raw));
        break;
    case ESYMBOLTYPE_QSCRIPT:
        Mem::FreeMalloc(reinterpret_cast<void*>(symbol->raw));
        break;
    case ESYMBOLTYPE_STRUCTURE:
        if (CStruct* structure = symbol->structure) {
            structure->Clear();
            DeleteStruct(structure);
        }
        break;
    case ESYMBOLTYPE_ARRAY:
        CleanUpArray(reinterpret_cast<CArray*>(symbol->raw));
        if (auto* array = reinterpret_cast<CArray*>(symbol->raw)) {
            array->Clear();
            DeleteArray(array);
        }
        break;
    default:
        break;
    }
    symbol->raw = 0;
    FreeSymbol(symbol);
}

// 0x004727B0: `symbol` goes once loading is done.
void RemoveSymbolLater(CSymbolTableEntry* symbol) { AddPendingSymbol(symbol, true); }
// 0x004727F0: `symbol` (not in the table) replaces the symbol of its name once loading is done.
void ReplaceSymbolLater(CSymbolTableEntry* symbol) { AddPendingSymbol(symbol, false); }

// 0x00472830
void ProcessPendingSymbols()
{
    for (int i = 0; i < PendingSymbolCount(); ++i) {
        PendingSymbol& pending = PendingSymbols()[i];
        if (pending.owned) {
            CleanUpAndRemoveSymbol(pending.symbol);
        } else {
            CleanUpAndRemoveSymbol(FindSymbol(pending.symbol->name));
            LinkSymbol(pending.symbol);
        }
        // (reads the symbol even when it has just been freed, as the original does)
        if (pending.symbol->type == ESYMBOLTYPE_QSCRIPT && ScriptCache())
            RemoveFromScriptCache(pending.symbol->name);
    }
    PendingSymbolCount() = 0;
}

} // namespace Script

REPLACE(0x00470000, Script::ClearScriptFlag6F2564);
REPLACE(0x0046FFF0, Script::SetScriptFlag6F2564);
REPLACE(0x00470010, Script::AllocateScriptChecksumNames);
REPLACE(0x0044C1D0, Script::FreePair);
REPLACE(0x0044C230, Script::FreeVector);
REPLACE(0x00470070, Script::FreeStoredRandom);
REPLACE(0x004700D0, Script::FreeSymbol);
REPLACE(0x004711D0, Script::CleanUpAndRemoveSymbol);
REPLACE(0x004727B0, Script::RemoveSymbolLater);
REPLACE(0x004727F0, Script::ReplaceSymbolLater);
REPLACE(0x00472830, Script::ProcessPendingSymbols);
