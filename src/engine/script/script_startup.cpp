// The script system's start-up, 0x005A5660-0x005A5F6F (see script_startup.h).

#include "engine/script/script_startup.h"

#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/file/packs.h"
#include "engine/script/load.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "engine/script/script_tables.h"
#include "engine/script/param_struct.h"

namespace Scripts {

namespace {

constexpr uint32_t kNodeArray = 0xC472ECC5;      // "nodearray": looked up after each unload, the result unused
constexpr uint32_t kDebuggerNames = 0xC2F97BA7;  // "debugger_names": removed once the start-up files are loaded

// Compiled-out debug output (the empty function 0x005480A0): a .qb file's name before it loads, and a call with
// nothing after.
inline void PrintLoading(const char* /*filename*/) {}
inline void PrintDone() {}

// The pool pairs of the classes AllocatePools makes pools for.
void CreatePoolOf(MemSys::PoolPair& pools, int itemSize, int count, const char* name) { CreatePool(pools, itemSize, count, name); }

} // namespace

// 0x005A5660
uint32_t GetNumCFunctions() { return 1254; }
// 0x005A5670
uint32_t CountMethodEntries() { return 250; }

CNativeCommand* FindCFunction(const char* name)
{
    for (CNativeCommand& entry : g_cfunctionTable) {
        if (std::strcmp(entry.name, name) == 0)
            return &entry;
    }
    return nullptr;
}

void BindNativeCommand(CNativeCommand& entry, void* function)
{
    entry.function = function;
}

// 0x005A5960
void LoadQBFile(const char* filename, int unused)
{
    PrintLoading(filename);
    LoadQB(filename, unused, 0);
    PrintDone();
}

// 0x005A5990
void LoadQBFileData(const char* filename, const uint8_t* data, int unused, int useQbNames)
{
    LoadQBData(filename, data, unused, useQbNames, 0);
}

// 0x005A59B0
void UnloadQBFile(uint32_t fileChecksum)
{
    UnloadQB(fileChecksum);
    FetchList(kNodeArray);
}

// 0x005A59D0
void LoadStartupQBFiles()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    // (the reference build's name, 0x00657698; THUG2.exe has "qb_scripts.prx" there - the file layer opens a .pre
    // as .prx either way)
    Packs::LoadPre("qb_scripts.pre");
    MemSys::Hub::Pull()->LeaveHeapScope();
    auto* text = static_cast<const char*>(Packs::ReadIn("scripts\\qdir.txt", false, nullptr, nullptr, nullptr));
    int size = Packs::GetFileSize("scripts\\qdir.txt");
    BeginPermanentStrings();
    const char* end = text + size;
    const char* p = text;
    while (p < end) {
        // One line (no length check, as in the original), and the character after it skipped.
        char line[200];
        int length = 0;
        while (p < end && *p != '\r' && *p != '\n')
            line[length++] = *p++;
        line[length] = '\0';
        ++p;
        if (line[0] == '\0')
            continue;
        if (const char* data = std::strstr(line, "data\\"))
            std::memmove(line, data + 5, std::strlen(data + 5) + 1);
        UnloadQB(Checksums::HashFromText(line));
        FetchList(kNodeArray);
        PrintLoading(line);
        LoadQB(line, 1, 0);
        PrintDone();
    }
    EndPermanentStrings();
    Packs::Offload("scripts\\qdir.txt");
    Packs::UnloadPre("qb_scripts.pre");
    if (CSymbolTableEntry* symbol = FindSymbol(kDebuggerNames))
        CleanUpAndRemoveSymbol(symbol);
}

// 0x005A5B20
void LoadBootScriptFiles()
{
    MemSys::PushMemProfile("All startup QB files");
    LoadStartupQBFiles();
    MemSys::PopMemProfile();
}

// 0x005A5B40 ... 0x005A5DF0
void CreateAnimCachePredictorPool(int count, const char* name) { CreatePoolOf(Gfx2D::g_animSlotPools, 0x1E4, count, name); }
void CreateBlendChannelPool(int count, const char* name) { CreatePoolOf(Gfx2D::g_animTrackPools, 0x74, count, name); }
void CreateBonedAnimControllerPool(int count, const char* name) { CreatePoolOf(Gfx2D::g_standardPools, 0x28, count, name); }
void CreateAnimLookupPool(int count, const char* name) { CreatePoolOf(Engine3D::g_frameSourcePools, 0x214, count, name); }
void CreateSpawnedPolyDetailsPool(int count, const char* name) { CreatePoolOf(Engine3D::g_spawnedPolyPools, 0xB8, count, name); }
void CreateEventReceiverListPool(int count, const char* name) { CreatePoolOf(GameObjects::g_scriptListPools, 0x14, count, name); }

// 0x005A5E70
void AllocatePools()
{
    MemSys::PushMemProfile("Script::AllocatePools");
    CreateScriptPools();
    MemSys::PopMemProfile();
    MemSys::PopMemProfile(); // (a second empty call)
    MemSys::PushMemProfile("CAnimCachePredictor");
    CreateAnimCachePredictorPool(200, "CAnimCachePredictor");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("CBlendChannel");
    CreateBlendChannelPool(150, "CBlendChannel");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("CBonedAnimController");
    CreateBonedAnimControllerPool(150, "CBonedAnimController");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("CAnimLookup");
    CreateAnimLookupPool(175, "CAnimLookup");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("sSpawnedPolyDetails");
    CreateSpawnedPolyDetailsPool(75, "sSpawnedPolyDetails");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("Obj::CEventReceiverList");
    CreateEventReceiverListPool(175, "Obj::CEventReceiverList");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("Registering script functions");
    InstallScriptFunctions(g_cfunctionTable, GetNumCFunctions());
    PublishMethodTable(g_memberFunctionNames, CountMethodEntries());
    MemSys::PopMemProfile();
}

} // namespace Script
