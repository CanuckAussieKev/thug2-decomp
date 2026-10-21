// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960 (see
// debug_cfuncs.h).

#include "game/script/debug_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

constexpr uint32_t kId = 0x40C698AF;                      // "id"
constexpr uint32_t kFreeMem = 0x06ADEBFC;                 // "free_mem"
constexpr uint32_t kMinFreeMem = 0xE7FB8DC5;              // "min_free_mem"
constexpr uint32_t kUpdateDisplayFreeMemory = 0xF1E14224; // "UpdateDisplayFreeMemory"

// `count` bytes as "%02x " each, then a new line.
void PrintBytes(const void* data, int count)
{
    auto* bytes = static_cast<const uint8_t*>(data);
    for (int i = 0; i < count; ++i)
        Dbg::Printf("%02x ", bytes[i]);
    Dbg::Printf("\n");
}

} // namespace

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

} // namespace CFuncs

REPLACE(0x004489B0, CFuncs::CFunc_DisplayFreeMem);
REPLACE(0x00449110, CFuncs::CFunc_Test);
