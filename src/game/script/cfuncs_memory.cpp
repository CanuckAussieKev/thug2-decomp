// Script commands of the engine, 0x00447FF0-0x00448840: the memory manager's (profiling, heap contexts, named
// heaps, reports) and the pool-usage dump (see cfuncs_engine.h).

#include "game/script/cfuncs_engine.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/dump_text.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/stubs.h"
#include "game/gfx/engine_glue.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/script/script_startup.h"
#include "game/script/cfuncs_scene.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace CFuncs {

using Script::CScript;
using Script::CStruct;

namespace {

constexpr uint32_t kOff = 0xD443A2BC;          // "off"
constexpr uint32_t kName = 0xA1DC81F9;         // "name"
constexpr uint32_t kSize = 0x083FDB95;         // "size"
constexpr uint32_t kBottomUpHeap = 0x0C7800B0; // "BottomUpHeap"
constexpr uint32_t kDumpHeapsBytes = 0xD65B9AC7; // (name unknown) DumpHeaps: sizes in bytes rather than K

// Compiled out of this build (calls of the empty function 0x005480A0).
inline void SetMemThreadSafe(bool /*safe*/) {}
inline void DumpFragments(int /*size*/, bool /*kilobytes*/) {}
inline void AnalyzeHeap(Mem::Heap* /*heap*/) {}
inline void PrintMemInfo(const char* /*heapName*/) {}
inline void DumpHeapsBegin() {}

Mem::Manager* Manager() { return Mem::Manager::Get(); }

// The heap a command names: name="<text>" or name=<checksum>.
uint32_t HeapName(CStruct* params)
{
    const char* text;
    uint32_t name = 0; // (uninitialised in the original: asserted)
    if (params->GetString(kName, &text, 0))
        name = Crc::GenerateCRCFromString(text);
    else
        params->GetChecksum(kName, &name, 1);
    return name;
}

// The sound manager's free memory (0x0047AE10 on 0x006F85A8: `xor eax, eax; ret`, folded with Stubs::ReturnZero -
// 0 in this build).
int FreeSoundMemory() { return Stubs::ReturnZero(); }

} // namespace

// 0x00447FF0: PushMemProfile "<name>" (compiled out). Always true.
bool __cdecl CFunc_PushMemProfile(CStruct* params, CScript*)
{
    const char* name = nullptr;
    params->GetString(0u, &name, 1);
    Mem::PushMemProfile(name);
    return true;
}

// 0x00448020: PopMemProfile (compiled out). Always true.
bool __cdecl CFunc_PopMemProfile(CStruct*, CScript*)
{
    Mem::PopMemProfile();
    return true;
}

// 0x00448030: SetMemThreadSafe [off] (compiled out). Always true.
bool __cdecl CFunc_SetMemThreadSafe(CStruct* params, CScript*)
{
    if (params->ContainsFlag(kOff))
        SetMemThreadSafe(false);
    else
        SetMemThreadSafe(true);
    return true;
}

// 0x00448060: DumpFragments [<size> (0x3C00)] [K] (compiled out). Always true.
bool __cdecl CFunc_DumpFragments(CStruct* params, CScript*)
{
    int size = 0x3C00;
    params->GetInteger(0u, &size, 0);
    DumpFragments(size, params->ContainsFlag("K"));
    return true;
}

// 0x00448090: MemViewToggle. Always true.
bool __cdecl CFunc_MemViewToggle(CStruct*, CScript*)
{
    Nx::ToggleMemView();
    return true;
}

// 0x004480A0: GotExtraMemory: whether the machine has the extra memory (0x007CEFAC).
bool __cdecl CFunc_GotExtraMemory(CStruct*, CScript*) { return g_gotExtraMemory != 0; }

// 0x004480B0: MemPushContext <heap name> | 0 (the bottom-up heap; another number does nothing): allocations go to
// that heap until MemPopContext. Always true.
bool __cdecl CFunc_MemPushContext(CStruct* params, CScript*)
{
    int number = 0;
    if (params->GetInteger(0u, &number, 0)) {
        if (number == 0)
            Manager()->PushContext(Manager()->m_bottomUpHeap);
        return true;
    }
    uint32_t name = 0; // (uninitialised in the original: asserted)
    params->GetChecksum(0u, &name, 1);
    Manager()->PushContext(Manager()->GetHeap(name));
    return true;
}

// 0x00448130: MemPopContext. Always true.
bool __cdecl CFunc_MemPopContext(CStruct*, CScript*)
{
    Manager()->PopContext();
    return true;
}

// 0x00448140: MemInitHeap name="<text>" size=<bytes>: a named heap of its own region. Always true.
bool __cdecl CFunc_MemInitHeap(CStruct* params, CScript*)
{
    const char* text = nullptr; // (both asserted; uninitialised in the original)
    int size = 0;
    params->GetString(kName, &text, 1);
    params->GetInteger(kSize, &size, 1);
    Manager()->InitNamedHeap(Crc::GenerateCRCFromString(text), static_cast<uint32_t>(size), text, 0);
    return true;
}

// 0x004481A0: MemDeleteHeap name=...: whether the named heap was there (and is deleted).
bool __cdecl CFunc_MemDeleteHeap(CStruct* params, CScript*)
{
    return Manager()->DeleteNamedHeap(HeapName(params), 0);
}

// 0x00448200: MemHeapExists name=...: whether there is such a named heap.
bool __cdecl CFunc_MemHeapExists(CStruct* params, CScript*)
{
    return Manager()->GetNamedHeap(HeapName(params), 0) != nullptr;
}

// 0x00448270: AnalyzeHeap [<heap name> (BottomUpHeap)] (compiled out; params may be null). Always true.
bool __cdecl CFunc_AnalyzeHeap(CStruct* params, CScript*)
{
    uint32_t name = kBottomUpHeap;
    if (params)
        params->GetChecksum(0u, &name, 0);
    if (Mem::Heap* heap = Manager()->GetHeap(name))
        AnalyzeHeap(heap);
    return true;
}

// 0x004482C0: PrintMemInfo [<heap name> (BottomUpHeap)]: the heap looked up and its name copied for a compiled-out
// report (params may be null). Always true.
bool __cdecl CFunc_PrintMemInfo(CStruct* params, CScript*)
{
    uint32_t name = kBottomUpHeap;
    if (params)
        params->GetChecksum(0u, &name, 0);
    Manager()->GetHeap(name);
    char heapName[64];
    std::snprintf(heapName, sizeof(heapName), "%s", Manager()->GetHeapName(name)); // (the original's copy is unbounded)
    PrintMemInfo(heapName);
    return true;
}

// 0x00448330 (the flag in BL; DumpPoolUsageStub): the pools' usage (used of size, the most used) - the script
// system's, the hash items', the animation system's, spawned polygons', event receiver lists' - written to the dump
// file (Mem::DumpText) and / or printed.
void __cdecl DumpPoolUsage(bool toDumpFile, bool print)
{
    char line[256];
    auto emit = [&]() {
        if (toDumpFile)
            Mem::DumpText(line);
        if (print)
            Dbg::Printf(line); // (the line as the format, as in the original)
    };
    // (the texts are the exe's)
    auto text = [&](const char* message) {
        std::snprintf(line, sizeof(line), message);
        emit();
    };
    auto pool = [&](const char* format, const Mem::Pool* p) {
        std::snprintf(line, sizeof(line), format, p->m_used, p->m_count, p->m_peakUsed);
        emit();
    };
    auto current = [](const Mem::PoolPair& pools) {
        return pools.pools[pools.current]; // (not checked: the pools are set up by then)
    };
    text(GAME_TEXT(0x00646E38, "\nScript pool usage:\n"));                               // "\nScript pool usage:\n"
    pool(GAME_TEXT(0x00646E10, "CStruct:           %5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x006F3614)));          // " | CStruct:           %5d of %5d, max %5d\n"
    pool(GAME_TEXT(0x00646DE8, "CComponent:        %5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x006F2338)));          // CComponent
    pool(GAME_TEXT(0x00646DC0, "CSymbolTableEntry: %5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x006F362C)));          // CSymbolTableEntry
    pool(GAME_TEXT(0x00646D98, "CVector:           %5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x006F3654)));          // CVector
    pool(GAME_TEXT(0x00646D70, "CPair:             %5d of %5d, max %5d\n"), current(Script::g_pairPools));          // CPair
    pool(GAME_TEXT(0x00646D48, "CArray:            %5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x006F22B4)));          // CArray
    pool(GAME_TEXT(0x00646D20, "CScript:           %5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x006F3548)));          // CScript
    pool(GAME_TEXT(0x00646D00, "CHashItem:\t%5d of %5d, max %5d\n"), Mem::PoolManager::GetPool(0)); // CHashItem
    pool(GAME_TEXT(0x00646CD4, "CAnimCachePredictor:\t%5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x006F92B8)));          // CAnimCachePredictor
    pool(GAME_TEXT(0x00646CB0, "CBlendChannel:\t%5d of %5d, max %5d\n"), current(Gfx::g_animTrackPools));          // CBlendChannel
    pool(GAME_TEXT(0x00646C84, "CBonedAnimController:\t%5d of %5d, max %5d\n"), current(GameVariable<Mem::PoolPair>(0x00701430)));          // CBonedAnimController
    pool(GAME_TEXT(0x00646C60, "CAnimLookup:\t%5d of %5d, max %5d\n"), current(Nx::g_frameSourcePools));          // CAnimLookup
    pool(GAME_TEXT(0x00646C34, "sSpawnedPolyDetails:\t%5d of %5d, max %5d\n"), current(Nx::g_spawnedPolyPools));          // sSpawnedPolyDetails
    pool(GAME_TEXT(0x00646C04, "Obj::CEventReceiverList:\t%5d of %5d, max %5d\n"), current(Obj::g_scriptListPools));          // Obj::CEventReceiverList
    text(GAME_TEXT(0x00646BC4, "\n(HashItems are init in main.cpp, others are in init.cpp)\n\n\n")); // "\n(HashItems are init in main.cpp, others are in init.cpp)\n\n\n"
}

// 0x00448840: DumpHeaps [<flag>]: the current heap, then each heap's used / fragmented / free / minimum free sizes and
// block count (in K, or in bytes with the flag), the sound memory free and the pools' usage - printed (params may be
// null). Always true.
bool __cdecl CFunc_DumpHeaps(CStruct* params, CScript*)
{
    auto format = [](const char* text) { return text; }; // (the exe's texts)
    DumpHeapsBegin();
    Mem::Manager* manager = Manager();
    Dbg::Printf(format(GAME_TEXT(0x00646FB0, "Current Heap: %s\n\n")), manager->CurrentHeapName()); // "Current Heap: %s\n"
    bool bytes = params && params->ContainsFlag(kDumpHeapsBytes);
    Dbg::Printf(format(bytes ? GAME_TEXT(0x00646F70, "Name                       Used    Frag    Free    Min Blocks\n") : GAME_TEXT(0x00646ED4, "Name                       Used  Frag  Free   Min  Blocks\n"))); // "Name                       Used ..." (two layouts)
    Dbg::Printf(format(bytes ? GAME_TEXT(0x00646F30, "------------------------- ------ ------- ------- ------ ------\n") : GAME_TEXT(0x00646E98, "------------------------- ----- ----- ---- ------ ------\n"))); // "------------------------- -----..."
    for (Mem::Allocator* heap = manager->FirstHeap(); heap; heap = manager->NextHeap(heap)) {
        Mem::Region* region = heap->m_region;
        int minimum = region->m_minAvailable; // (read before MemAvailable)
        int blocks = heap->m_usedBlocks;
        int available = region->MemAvailable();
        if (bytes)
            Dbg::Printf(format(GAME_TEXT(0x00646F10, "%23s: %8d %7d %7d %7d  %5d \n")), heap->m_name, heap->m_usedSize, heap->m_freeSize, available, minimum,
                        blocks); // "%23s: %8d %7d %7d %7d  %5d \n"
        else
            Dbg::Printf(format(GAME_TEXT(0x00646E74, "%23s: %5dK %4dK %4dK %4dK  %5d \n")), heap->m_name, heap->m_usedSize / 1024, heap->m_freeSize / 1024,
                        available / 1024, minimum / 1024, blocks); // "%23s: %5dK %4dK %4dK %4dK  %5d \n"
    }
    int soundFree = FreeSoundMemory();
    Dbg::Printf(format(GAME_TEXT(0x00646E50, "\nSound mem free %d K  (%d bytes)\n")), FreeSoundMemory() / 1024, soundFree); // "\nSound mem free %d K  (%d bytes)\n"
    DumpPoolUsage(false, true);
    return true;
}

// 0x00448330: the "to the dump file" flag in BL, the "print" flag on the stack (the caller pops it).
__declspec(naked) void DumpPoolUsageStub()
{
    __asm {
        push dword ptr [esp + 4]
        movzx eax, bl
        push eax
        call DumpPoolUsage
        add esp, 8
        ret
    }
}

} // namespace CFuncs

REPLACE(0x00447FF0, CFuncs::CFunc_PushMemProfile);
REPLACE(0x00448020, CFuncs::CFunc_PopMemProfile);
REPLACE(0x00448030, CFuncs::CFunc_SetMemThreadSafe);
REPLACE(0x00448060, CFuncs::CFunc_DumpFragments);
REPLACE(0x00448090, CFuncs::CFunc_MemViewToggle);
REPLACE(0x004480A0, CFuncs::CFunc_GotExtraMemory);
REPLACE(0x004480B0, CFuncs::CFunc_MemPushContext);
REPLACE(0x00448130, CFuncs::CFunc_MemPopContext);
REPLACE(0x00448140, CFuncs::CFunc_MemInitHeap);
REPLACE(0x004481A0, CFuncs::CFunc_MemDeleteHeap);
REPLACE(0x00448200, CFuncs::CFunc_MemHeapExists);
REPLACE(0x00448270, CFuncs::CFunc_AnalyzeHeap);
REPLACE(0x004482C0, CFuncs::CFunc_PrintMemInfo);
REPLACE_UNCHECKED(0x00448330, CFuncs::DumpPoolUsageStub); // a flag in BL
REPLACE(0x00448840, CFuncs::CFunc_DumpHeaps);
