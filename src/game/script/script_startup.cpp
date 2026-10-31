// The script system's start-up, 0x005A5660-0x005A5F6F (see script_startup.h).

#include "game/script/script_startup.h"

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/file/pip.h"
#include "game/script/load.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "game/script/script_tables.h"
#include "game/script/struct.h"

namespace Script {

namespace {

constexpr uint32_t kNodeArray = 0xC472ECC5;      // "nodearray": looked up after each unload, the result unused
constexpr uint32_t kDebuggerNames = 0xC2F97BA7;  // "debugger_names": removed once the start-up files are loaded

// Compiled-out debug output (the empty function 0x005480A0): a .qb file's name before it loads, and a call with
// nothing after.
inline void PrintLoading(const char* /*filename*/) {}
inline void PrintDone() {}

// The pool pairs of the classes AllocatePools makes pools for.
void CreatePoolOf(Mem::PoolPair& pools, int itemSize, int count, const char* name) { CreatePool(pools, itemSize, count, name); }

} // namespace

// 0x005A5660
uint32_t GetNumCFunctions() { return 1254; }
// 0x005A5670
uint32_t GetNumMemberFunctions() { return 250; }

CFunctionEntry* FindCFunction(const char* name)
{
    for (CFunctionEntry& entry : g_cfunctionTable) {
        if (std::strcmp(entry.name, name) == 0)
            return &entry;
    }
    return nullptr;
}

void SetCFunction(CFunctionEntry& entry, void* function)
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
    GetArray(kNodeArray);
}

// 0x005A59D0
void LoadStartupQBFiles()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    // (the reference build's name, 0x00657698; THUG2.exe has "qb_scripts.prx" there - the file layer opens a .pre
    // as .prx either way)
    Pip::LoadPre("qb_scripts.pre");
    Mem::Manager::Get()->PopContext();
    auto* text = static_cast<const char*>(Pip::Load("scripts\\qdir.txt", false, nullptr, nullptr, nullptr));
    int size = Pip::GetFileSize("scripts\\qdir.txt");
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
        UnloadQB(Crc::GenerateCRCFromString(line));
        GetArray(kNodeArray);
        PrintLoading(line);
        LoadQB(line, 1, 0);
        PrintDone();
    }
    EndPermanentStrings();
    Pip::Unload("scripts\\qdir.txt");
    Pip::UnloadPre("qb_scripts.pre");
    if (CSymbolTableEntry* symbol = FindSymbol(kDebuggerNames))
        CleanUpAndRemoveSymbol(symbol);
}

// 0x005A5B20
void LoadAllStartupQBFiles()
{
    Mem::PushMemProfile("All startup QB files");
    LoadStartupQBFiles();
    Mem::PopMemProfile();
}

// 0x005A5B40 ... 0x005A5DF0
void CreateAnimCachePredictorPool(int count, const char* name) { CreatePoolOf(Gfx::g_animSlotPools, 0x1E4, count, name); }
void CreateBlendChannelPool(int count, const char* name) { CreatePoolOf(Gfx::g_animTrackPools, 0x74, count, name); }
void CreateBonedAnimControllerPool(int count, const char* name) { CreatePoolOf(Gfx::g_standardPools, 0x28, count, name); }
void CreateAnimLookupPool(int count, const char* name) { CreatePoolOf(Nx::g_frameSourcePools, 0x214, count, name); }
void CreateSpawnedPolyDetailsPool(int count, const char* name) { CreatePoolOf(Nx::g_spawnedPolyPools, 0xB8, count, name); }
void CreateEventReceiverListPool(int count, const char* name) { CreatePoolOf(Obj::g_scriptListPools, 0x14, count, name); }

// 0x005A5E70
void AllocatePools()
{
    Mem::PushMemProfile("Script::AllocatePools");
    CreateScriptPools();
    Mem::PopMemProfile();
    Mem::PopMemProfile(); // (a second empty call)
    Mem::PushMemProfile("CAnimCachePredictor");
    CreateAnimCachePredictorPool(200, "CAnimCachePredictor");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CBlendChannel");
    CreateBlendChannelPool(150, "CBlendChannel");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CBonedAnimController");
    CreateBonedAnimControllerPool(150, "CBonedAnimController");
    Mem::PopMemProfile();
    Mem::PushMemProfile("CAnimLookup");
    CreateAnimLookupPool(175, "CAnimLookup");
    Mem::PopMemProfile();
    Mem::PushMemProfile("sSpawnedPolyDetails");
    CreateSpawnedPolyDetailsPool(75, "sSpawnedPolyDetails");
    Mem::PopMemProfile();
    Mem::PushMemProfile("Obj::CEventReceiverList");
    CreateEventReceiverListPool(175, "Obj::CEventReceiverList");
    Mem::PopMemProfile();
    Mem::PushMemProfile("Registering script functions");
    RegisterCFunctions(g_cfunctionTable, GetNumCFunctions());
    RegisterMemberFunctions(g_memberFunctionNames, GetNumMemberFunctions());
    Mem::PopMemProfile();
}

} // namespace Script
