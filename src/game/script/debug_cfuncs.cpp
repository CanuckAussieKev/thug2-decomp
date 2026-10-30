// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960 (see
// debug_cfuncs.h).

#include "game/script/debug_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/script/cscript.h"
#include "game/sys/mlp.h"
#include "game/script/struct.h"
#include "game/script/script_startup.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

constexpr uint32_t kId = 0x40C698AF;                      // "id"
constexpr uint32_t kFreeMem = 0x06ADEBFC;                 // "free_mem"
constexpr uint32_t kMinFreeMem = 0xE7FB8DC5;              // "min_free_mem"
constexpr uint32_t kUpdateDisplayFreeMemory = 0xF1E14224; // "UpdateDisplayFreeMemory"
constexpr uint32_t kLevelSpecific = 0x6DD1DECF;           // "LevelSpecific"
constexpr uint32_t kFileName = 0xC3F4169A;                // "FileName"
constexpr uint32_t kSound = 0x07713C7B;                   // "sound" (the component type)
constexpr uint32_t kStream = 0x0F1641E3;                  // "stream" (the component type)

// The game's language (0x007CEFA0): 2 English, 3 French, 4 Spanish, 5 German, 6 Italian.
int Language() { return GameVariable<int>(0x007CEFA0); }

// A .qb file loaded (0x005A5960; `levelSpecific`: unloaded with the level) / unloaded by its name's checksum
// (0x005A59B0).
void LoadQbFile(const char* name, bool levelSpecific)
{
    Script::LoadQBFile(name, levelSpecific ? 1 : 0);
}
void UnloadQbFile(uint32_t name) { Script::UnloadQBFile(name); }

// A screenshot written to `name` (0x0048C100, a jump to 0x004B24E0).
void TakeScreenShot(const char* name, int flags) { Nx::ScreenShot(name, flags); }
// The renderer's metrics display switched on / off (0x0048C0F0: the flag 0x006FA7D6 toggled).
void ToggleMetricsDisplay() { Nx::Engine::ToggleMetrics(); }

// `count` bytes as "%02x " each, then a new line.
void PrintBytes(const void* data, int count)
{
    auto* bytes = static_cast<const uint8_t*>(data);
    for (int i = 0; i < count; ++i)
        Dbg::Printf("%02x ", bytes[i]);
    Dbg::Printf("\n");
}

} // namespace

uint32_t* LevelSpecificQbFiles() { return &GameVariable<uint32_t>(0x00696900); }

// 0x004489B0: DisplayFreeMem: for every heap of the memory manager, run UpdateDisplayFreeMemory with id=<the
// heap's name checksum> free_mem=<the space left in its region> min_free_mem=<the least there has been>.
bool __cdecl CFunc_DisplayFreeMem(Script::CStruct*, Script::CScript*)
{
    Mem::Manager* manager = Mem::Manager::Get();
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* params = memory ? memory->Init() : nullptr;
    for (Mem::Allocator* heap = manager->FirstHeap(); heap; heap = manager->NextHeap(heap)) {
        Mem::Region* region = heap->m_region;
        params->Clear();
        params->AddChecksum(kId, Crc::GenerateCRCFromString(heap->m_name));
        params->AddInteger(kFreeMem, region->MemAvailable());
        params->AddInteger(kMinFreeMem, region->m_minAvailable);
        Script::RunScript(kUpdateDisplayFreeMemory, params, nullptr, false, nullptr);
    }
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    return true;
}

// 0x00449110: Test: prints the bytes of a symbol table entry (a structure named 0xC1C935B2,
// flags 4) after setting flag bits 1 and 2 in turn, then of a component (byte 0x57, word 1000, name 0x480A2607,
// value 0x01020304) - a check of the layouts. Always true.
bool __cdecl CFunc_Test(Script::CStruct*, Script::CScript*)
{
    Script::CSymbolTableEntry entry;
    entry.Init();
    std::memset(&entry, 0, sizeof(entry));
    entry.unknown0 = 4;
    entry.type = Script::ESYMBOLTYPE_STRUCTURE;
    entry.name = 0xC1C935B2;
    Dbg::Printf("CSymbolTableEntry:\n");
    PrintBytes(&entry, sizeof(entry));
    entry.unknown0 |= 1;
    PrintBytes(&entry, sizeof(entry));
    entry.unknown0 |= 2;
    PrintBytes(&entry, sizeof(entry));
    entry.unknown0 &= ~4;

    Script::CComponent component;
    Script::ConstructComponent(&component);
    std::memset(&component, 0, sizeof(component));
    component.typeAndFlag = 0x57;
    component.unknown2 = 1000;
    component.name = 0x480A2607;
    component.raw = 0x01020304;
    Dbg::Printf("CComponent:\n");
    PrintBytes(&component, sizeof(component));
    return true;
}

// 0x004493B0: LoadQB "<file>" [LevelSpecific]: the .qb file loaded; LevelSpecific ones are noted in the first free
// slot of the table (by the name's checksum; all 15 taken: not noted, loaded all the same).
bool __cdecl CFunc_LoadQB(Script::CStruct* params, Script::CScript*)
{
    const char* name = nullptr;
    params->GetString(0u, &name, 0);
    if (!params->ContainsFlag(kLevelSpecific)) {
        LoadQbFile(name, false);
        return true;
    }
    uint32_t* files = LevelSpecificQbFiles();
    for (int i = 0; i < kLevelSpecificQbFiles; ++i) {
        if (!files[i]) {
            files[i] = Crc::GenerateCRCFromString(name);
            break;
        }
    }
    LoadQbFile(name, true);
    return true;
}

// 0x00449450: UnloadQB "<file>": the .qb file unloaded, and forgotten by the LevelSpecific table (every slot).
bool __cdecl CFunc_UnloadQB(Script::CStruct* params, Script::CScript*)
{
    const char* name = nullptr;
    params->GetString(0u, &name, 0);
    uint32_t checksum = Crc::GenerateCRCFromString(name);
    UnloadQbFile(checksum);
    uint32_t* files = LevelSpecificQbFiles();
    for (int i = 0; i < kLevelSpecificQbFiles; ++i) {
        if (files[i] == checksum)
            files[i] = 0;
    }
    return true;
}

// 0x004494A0: ScreenShot [FileName="<name>" ("screen")].
bool __cdecl CFunc_ScreenShot(Script::CStruct* params, Script::CScript*)
{
    const char* name;
    if (!params->GetString(kFileName, &name, 0))
        name = "screen";
    TakeScreenShot(name, 0);
    return true;
}

// 0x004494E0: ToggleMetrics.
bool __cdecl CFunc_ToggleMetrics(Script::CStruct*, Script::CScript*)
{
    ToggleMetricsDisplay();
    return true;
}

// 0x004494F0 / 0x00449500 / 0x00449510 / 0x00449520 / 0x00449530: German / French / Spanish / Italian / English:
// whether the game runs in that language.
bool __cdecl CFunc_German(Script::CStruct*, Script::CScript*) { return Language() == 5; }
bool __cdecl CFunc_French(Script::CStruct*, Script::CScript*) { return Language() == 3; }
bool __cdecl CFunc_Spanish(Script::CStruct*, Script::CScript*) { return Language() == 4; }
bool __cdecl CFunc_Italian(Script::CStruct*, Script::CScript*) { return Language() == 6; }
bool __cdecl CFunc_English(Script::CStruct*, Script::CScript*) { return Language() == 2; }

// 0x004498F0: ProximCleanup: every sound component's +0xBC and stream component's +0x28 cleared (what they keep of
// the proximity nodes), then every proximity node freed.
bool __cdecl CFunc_ProximCleanup(Script::CStruct*, Script::CScript*)
{
    for (Obj::CBaseComponent* c = Obj::CompositeObjectManager()->GetFirstComponentByType(kSound); c; c = c->mp_nextOfType)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(c) + 0xBC) = 0;
    for (Obj::CBaseComponent* c = Obj::CompositeObjectManager()->GetFirstComponentByType(kStream); c; c = c->mp_nextOfType)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(c) + 0x28) = 0;
    Obj::Proxim_Cleanup();
    return true;
}

// 0x00449950: Inside: whether the proximity node script running now was started by entering (not leaving). (No
// check that the proximity manager exists.)
bool __cdecl CFunc_Inside(Script::CStruct*, Script::CScript*)
{
    return GameVariable<Obj::ProximManager*>(0x006F2268)->m_entering;
}

// 0x00449960: ResetEngine: the main loop ends after this frame (and the game starts again).
bool __cdecl CFunc_ResetEngine(Script::CStruct*, Script::CScript*)
{
    GameVariable<Mlp::Manager*>(0x00696D68)->StopLoop();
    return true;
}

} // namespace CFuncs

REPLACE(0x004489B0, CFuncs::CFunc_DisplayFreeMem);
REPLACE(0x00449110, CFuncs::CFunc_Test);
REPLACE(0x004493B0, CFuncs::CFunc_LoadQB);
REPLACE(0x00449450, CFuncs::CFunc_UnloadQB);
REPLACE(0x004494A0, CFuncs::CFunc_ScreenShot);
REPLACE(0x004494E0, CFuncs::CFunc_ToggleMetrics);
REPLACE(0x004494F0, CFuncs::CFunc_German);
REPLACE(0x00449500, CFuncs::CFunc_French);
REPLACE(0x00449510, CFuncs::CFunc_Spanish);
REPLACE(0x00449520, CFuncs::CFunc_Italian);
REPLACE(0x00449530, CFuncs::CFunc_English);
REPLACE(0x004498F0, CFuncs::CFunc_ProximCleanup);
REPLACE(0x00449950, CFuncs::CFunc_Inside);
REPLACE(0x00449960, CFuncs::CFunc_ResetEngine);
