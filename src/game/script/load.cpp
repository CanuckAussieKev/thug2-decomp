// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/script/load.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/packs.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/token_reader.h"
#include "game/script/patched.h"
#include "game/script/precompiled.h"
#include "game/script/qb.h"
#include "game/util/lzss.h"
#include "game/sys/game_config.h"

namespace Scripts {

uint32_t g_loadingFile;

namespace {

// The checksum of the .qb file being loaded (0 outside loading).
uint32_t& LoadingFile() { return g_loadingFile; }

// Not rewritten yet: the platform's file extension for pre-compiled scripts.
const char* PrecompiledExtension() { return GameConfig::FetchPlatformSuffix(); }

// The symbol `existing` makes way for a new definition: set aside (temporary loads) or removed.
void Replace(CSymbolTableEntry* existing, int temporary)
{
    if (temporary) {
        ReplaceSymbolLater(existing);
        UnlinkSymbol(existing);
    } else {
        CleanUpAndRemoveSymbol(existing);
    }
}

} // namespace

// 0x00472240 (the token in ECX)
const uint8_t* ParseSymbolDefinition(const uint8_t* token, uint32_t name, const char* filename)
{
    CSymbolTableEntry* symbol = CreateSymbol(name);
    symbol->unknown8 = Checksums::HashFromText(filename);
    switch (*token) {
    case 0x0E:
    case 0x16: case 0x17: case 0x1A: case 0x1B: case 0x1C: case 0x1E: case 0x1F: {
        CComponent* value = NewComponent();
        const uint8_t* end = *token == 0x0E ? ParseExpression(token, nullptr, value) : ParseValue(token, nullptr, value);
        symbol->type = value->KindTag();
        symbol->raw = value->raw;
        value->raw = 0;
        FreeComponent(value);
        return end;
    }
    case 0x03: {
        symbol->type = QBVAL_STRUCT;
        CStruct* structure = SpawnParamBag();
        structure = structure ? structure->PrepareAll() : nullptr;
        symbol->structure = structure;
        return ParseStructure(structure, token, nullptr);
    }
    case 0x05: {
        symbol->type = QBVAL_ARRAY;
        CArray* array = AllocScriptArray();
        array = array ? array->PrepareAll() : nullptr;
        symbol->raw = reinterpret_cast<uint32_t>(array);
        return ParseArray(array, token, nullptr);
    }
    default:
        return token;
    }
}

// 0x0046FE40 (the size in EAX, the code in EBX)
CSymbolTableEntry* DefineScriptSymbol(uint32_t size, const uint8_t* code, uint32_t name, uint32_t checksum,
                                      const char* filename)
{
    CSymbolTableEntry* symbol = CreateSymbol(name);
    symbol->type = QBVAL_SCRIPT_BODY;
    auto* buffer = static_cast<uint8_t*>(MemSys::AllocBlock(20000));
    int stored = Lzss::PackUp(code, buffer, static_cast<int>(size), false);
    if (stored >= static_cast<int>(size)) {
        // (no smaller: kept as it is)
        for (uint32_t i = 0; i < size; ++i)
            buffer[i] = code[i];
        stored = static_cast<int>(size);
    }
    // {checksum, size, stored size, the code}
    auto* block = static_cast<uint32_t*>(MemSys::AllocBlock(static_cast<unsigned int>(stored + 12)));
    block[0] = checksum;
    block[1] = size;
    block[2] = static_cast<uint32_t>(stored);
    auto* bytes = reinterpret_cast<uint8_t*>(block + 3);
    for (int i = 0; i < stored; ++i)
        bytes[i] = buffer[i];
    symbol->raw = reinterpret_cast<uint32_t>(block);
    MemSys::FreeMalloc(buffer);
    if (ScriptStoreInstance())
        RemoveFromScriptCache(name);
    symbol->unknown8 = Checksums::HashFromText(filename);
    return symbol;
}

// 0x00472420
void ReadQbFile(const char* filename, const uint8_t* data, int unused, int useQbNames, int temporary)
{
    (void)unused; // (line numbers are skipped either way)
    FreeScriptChecksumNames();
    if (useQbNames)
        AllocateScriptChecksumNames(false);

    // First the checksums' names.
    for (const uint8_t* p = data; *p;) {
        if (*p == 0x2B) {
            uint32_t checksum = FetchWord32(p + 1);
            const char* name = reinterpret_cast<const char*>(p + 5);
            if (useQbNames)
                AddScriptChecksumName(checksum, name);
            else
                RegisterNameHash(checksum, name);
            NoteQbName(checksum, name); // (ours: kept for the level editor when it's on)
            p = reinterpret_cast<const uint8_t*>(name + std::strlen(name) + 1);
        } else {
            p = StepOverToken(p);
        }
    }
    RegisterNameHash(Checksums::HashFromText(filename), filename);
    LoadingFile() = Checksums::HashFromText(filename);

    MemSys::Hub* manager = MemSys::Hub::Pull();
    MemSys::EnterHeapScope(manager->m_scriptHeap);
    const uint8_t* p = data;
    for (;;) {
        p = SkipNewlineTokens(p);
        switch (*p) {
        case 0x00:
            MemSys::LeaveHeapScope();
            LoadingFile() = 0;
            return;
        case 0x16: { // name = value
            uint32_t name = FetchWord32(p + 1);
            p = SkipNewlineTokens(p + 5);
            p = SkipNewlineTokens(p + 1); // (past the '=')
            CSymbolTableEntry* existing = FindSymbol(name);
            if (existing)
                Replace(existing, temporary);
            p = ParseSymbolDefinition(p, name, filename);
            if (!existing && temporary)
                RemoveSymbolLater(FindSymbol(name));
            break;
        }
        case 0x23: { // script name ... endscript
            uint32_t name = FetchWord32(p + 2);
            const uint8_t* start = p + 6;
            p = SkipToEndOfScript(start);
            uint32_t size = static_cast<uint32_t>(p - start);
            uint32_t checksum = CalculateScriptChecksum(start);
            CSymbolTableEntry* existing = FindSymbol(name);
            if (existing) {
                // (an unchanged script is left as it is)
                if (existing->type == QBVAL_SCRIPT_BODY && checksum == *reinterpret_cast<uint32_t*>(existing->raw))
                    break;
                Replace(existing, temporary);
                DefineScriptSymbol(size, start, name, checksum, filename);
            } else {
                CSymbolTableEntry* symbol = DefineScriptSymbol(size, start, name, checksum, filename);
                if (temporary)
                    RemoveSymbolLater(symbol);
            }
            break;
        }
        case 0x2B:
            p = StepOverToken(p);
            break;
        case 0x45: { // a section marker
            uint32_t marker = FetchWord32(p + 2);
            p += 6;
            if (marker == 0x0FA33D9B) {
                // The end of a debug section.
                MemSys::LeaveHeapScope();
                MemSys::EnterHeapScope(manager->m_scriptHeap);
            } else if (marker == 0x70CB0238) {
                // A debug section: kept (on the debug heap) only while checksum names are.
                if (ChecksumNamesEnabled()) {
                    MemSys::LeaveHeapScope();
                    MemSys::EnterHeapScope(manager->m_debugHeap);
                } else {
                    while (*p != 0x45 && *p != 0x00)
                        p = StepOverToken(p);
                }
            }
            break;
        }
        default:
            // (other tokens don't move on: the original loops here)
            break;
        }
    }
}

// 0x00472C50
bool ScriptRefersTo(const CScript* script, uint32_t name)
{
    if (script->m_scriptTitle == name)
        return true;
    // The calls it's in the middle of.
    for (int i = 0; i < script->m_callDepth; ++i) {
        if (script->m_frameRecords[i].m_scriptId == name)
            return true;
    }
    return false;
}

// 0x0046ED20
void RestartChangedScripts()
{
    for (CSymbolTableEntry* symbol = GetNextSymbol(nullptr); symbol; symbol = GetNextSymbol(symbol)) {
        // (byte 0 bit 0: new since the last time - CreateSymbol sets it)
        if ((symbol->unknown0 & 1) && symbol->type == QBVAL_SCRIPT_BODY) {
            for (CScript* script = StepScriptList(nullptr); script; script = StepScriptList(script)) {
                if (ScriptRefersTo(script, symbol->name))
                    script->Relaunch();
            }
            symbol->unknown0 &= 0xFE;
        }
    }
}

// 0x0046ED90
void LoadQBData(const char* filename, const uint8_t* data, int unused, int useQbNames, int temporary)
{
    if (FetchWord32(data) == 0xB075EF6F)
        ParsePrecompiledQB(filename, const_cast<uint8_t*>(data), unused); // (relocated in place)
    else
        ReadQbFile(filename, data, unused, useQbNames, temporary);
    RestartChangedScripts();
}

// 0x0046EDE0
void LoadQB(const char* filename, int unused, int temporary)
{
    size_t length = std::strlen(filename);
    const char* extension = PrecompiledExtension();
    MemSys::Hub* manager = MemSys::Hub::Pull();
    if (filename[length - 4] == '.' && _stricmp(filename + length - 3, extension) == 0) {
        // Pre-compiled: kept loaded.
        MemSys::EnterHeapScope(manager->m_scriptHeap);
        auto* data = static_cast<uint8_t*>(Packs::ReadIn(filename, false, nullptr, nullptr, nullptr));
        MemSys::LeaveHeapScope();
        ParsePrecompiledQB(filename, data, unused);
        LoadChecksumNames(filename);
    } else {
        MemSys::EnterHeapScope(manager->m_topDownHeap);
        // Originally from PartyMod Fixes (its call at 0x0046EE80): Packs::ReadIn, then the binary patch for the file if there is one
        // (scripts\engine\menu\keyboard.qb), in place of the original's plain Packs::ReadIn.
        auto* data = LoadPatchedScript(filename);
        MemSys::LeaveHeapScope();
        ReadQbFile(filename, data, unused, 0, temporary);
        Packs::Offload(filename);
    }
    RestartChangedScripts();
}

// 0x0046EEC0
void UnloadQB(uint32_t fileChecksum)
{
    if (!fileChecksum)
        return;
    // The file's symbols: listed first (removing them changes the table), then removed.
    int count = 0;
    for (CSymbolTableEntry* symbol = GetNextSymbol(nullptr); symbol; symbol = GetNextSymbol(symbol)) {
        if (symbol->unknown8 == fileChecksum)
            ++count;
    }
    if (count) {
        MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_topDownHeap);
        auto** symbols = static_cast<CSymbolTableEntry**>(MemSys::AllocBlock(static_cast<unsigned int>(count) * 4));
        MemSys::LeaveHeapScope();
        CSymbolTableEntry** next = symbols;
        for (CSymbolTableEntry* symbol = GetNextSymbol(nullptr); symbol; symbol = GetNextSymbol(symbol)) {
            if (symbol->unknown8 == fileChecksum)
                *next++ = symbol;
        }
        for (int i = 0; i < count; ++i)
            CleanUpAndRemoveSymbol(symbols[i]);
        MemSys::FreeMalloc(symbols);
    }
    // Scripts left running code that's gone are stopped.
    for (CScript* script = StepScriptList(nullptr); script; script = StepScriptList(script)) {
        if (!script->AllScriptsStillDefined()) {
            script->ResetScriptState();
            script->ReportStopped();
        }
    }
    // (a pre-compiled file stays loaded until now)
    Packs::Offload(fileChecksum);
}

} // namespace Script

// --- the entry points with register arguments -------------------------------------------------------------

namespace {

const uint8_t* __cdecl ParseSymbolDefinitionRegisters(const uint8_t* token, uint32_t name, const char* filename)
{
    return Scripts::ParseSymbolDefinition(token, name, filename);
}
Scripts::CSymbolTableEntry* __cdecl CreateScriptSymbolRegisters(uint32_t size, const uint8_t* code, uint32_t name,
                                                               uint32_t checksum, const char* filename)
{
    return Scripts::DefineScriptSymbol(size, code, name, checksum, filename);
}

} // namespace

// 0x00472240: the token in ECX, the name and file name on the stack (the caller pops them).
__declspec(naked) void Scripts::ParseSymbolDefinitionStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        call ParseSymbolDefinitionRegisters
        add esp, 12
        ret
    }
}

// 0x0046FE40: the size in EAX, the code in EBX, the name, checksum and file name on the stack (the caller pops
// them).
__declspec(naked) void Scripts::CreateScriptSymbolStub()
{
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push ebx
        push eax
        call CreateScriptSymbolRegisters
        add esp, 20
        ret
    }
}

namespace {
struct ScriptAdapter {
    bool RefersTo(uint32_t name) { return Scripts::ScriptRefersTo(reinterpret_cast<Scripts::CScript*>(this), name); }
};
} // namespace
