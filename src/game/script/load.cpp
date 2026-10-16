#include "game/script/load.h"

#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/script/names.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/util/lzss.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

namespace {

// The checksum of the .qb file being loaded (0 outside loading).
uint32_t& LoadingFile() { return GameVariable<uint32_t>(0x006F2364); }

// Not rewritten yet: the pre-compiled format, restarting a script and the platform's
// file extension for pre-compiled scripts.
void ParsePrecompiledQB(const char* filename, const uint8_t* data, int unused)
{
    GameFunction<void(__cdecl*)(const char*, const uint8_t*, int)>(0x0046FBA0)(filename, data, unused);
}
void RestartScript(CScript* script) { GameFunction<void(__thiscall*)(CScript*)>(0x004747F0)(script); }
const char* PrecompiledExtension() { return GameFunction<const char*(__cdecl*)()>(0x005B6040)(); }

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
    symbol->unknown8 = Crc::GenerateCRCFromString(filename);
    switch (*token) {
    case 0x0E:
    case 0x16: case 0x17: case 0x1A: case 0x1B: case 0x1C: case 0x1E: case 0x1F: {
        CComponent* value = NewComponent();
        const uint8_t* end = *token == 0x0E ? ParseExpression(token, nullptr, value) : ParseValue(token, nullptr, value);
        symbol->type = value->Type();
        symbol->raw = value->raw;
        value->raw = 0;
        FreeComponent(value);
        return end;
    }
    case 0x03: {
        symbol->type = ESYMBOLTYPE_STRUCTURE;
        CStruct* structure = NewStruct();
        structure = structure ? structure->Init() : nullptr;
        symbol->structure = structure;
        return ParseStructure(structure, token, nullptr);
    }
    case 0x05: {
        symbol->type = ESYMBOLTYPE_ARRAY;
        CArray* array = NewArray();
        array = array ? array->Init() : nullptr;
        symbol->raw = reinterpret_cast<uint32_t>(array);
        return ParseArray(array, token, nullptr);
    }
    default:
        return token;
    }
}

// 0x0046FE40 (the size in EAX, the code in EBX)
CSymbolTableEntry* CreateScriptSymbol(uint32_t size, const uint8_t* code, uint32_t name, uint32_t checksum,
                                      const char* filename)
{
    CSymbolTableEntry* symbol = CreateSymbol(name);
    symbol->type = ESYMBOLTYPE_QSCRIPT;
    auto* buffer = static_cast<uint8_t*>(Mem::Malloc(20000));
    int stored = Lzss::Encode(code, buffer, static_cast<int>(size), false);
    if (stored >= static_cast<int>(size)) {
        // (no smaller: kept as it is)
        for (uint32_t i = 0; i < size; ++i)
            buffer[i] = code[i];
        stored = static_cast<int>(size);
    }
    // {checksum, size, stored size, the code}
    auto* block = static_cast<uint32_t*>(Mem::Malloc(static_cast<unsigned int>(stored + 12)));
    block[0] = checksum;
    block[1] = size;
    block[2] = static_cast<uint32_t>(stored);
    auto* bytes = reinterpret_cast<uint8_t*>(block + 3);
    for (int i = 0; i < stored; ++i)
        bytes[i] = buffer[i];
    symbol->raw = reinterpret_cast<uint32_t>(block);
    Mem::FreeMalloc(buffer);
    if (ScriptCache())
        RemoveFromScriptCache(name);
    symbol->unknown8 = Crc::GenerateCRCFromString(filename);
    return symbol;
}

// 0x00472420
void ParseQB(const char* filename, const uint8_t* data, int unused, int useQbNames, int temporary)
{
    (void)unused; // (line numbers are skipped either way)
    FreeScriptChecksumNames();
    if (useQbNames)
        AllocateScriptChecksumNames(false);

    // First the checksums' names.
    for (const uint8_t* p = data; *p;) {
        if (*p == 0x2B) {
            uint32_t checksum = Read4Bytes(p + 1);
            const char* name = reinterpret_cast<const char*>(p + 5);
            if (useQbNames)
                AddScriptChecksumName(checksum, name);
            else
                AddChecksumName(checksum, name);
            p = reinterpret_cast<const uint8_t*>(name + std::strlen(name) + 1);
        } else {
            p = SkipToken(p);
        }
    }
    AddChecksumName(Crc::GenerateCRCFromString(filename), filename);
    LoadingFile() = Crc::GenerateCRCFromString(filename);

    Mem::Manager* manager = Mem::Manager::Get();
    Mem::PushContext(manager->m_scriptHeap);
    const uint8_t* p = data;
    for (;;) {
        p = SkipEndOfLines(p);
        switch (*p) {
        case 0x00:
            Mem::PopContext();
            LoadingFile() = 0;
            return;
        case 0x16: { // name = value
            uint32_t name = Read4Bytes(p + 1);
            p = SkipEndOfLines(p + 5);
            p = SkipEndOfLines(p + 1); // (past the '=')
            CSymbolTableEntry* existing = FindSymbol(name);
            if (existing)
                Replace(existing, temporary);
            p = ParseSymbolDefinition(p, name, filename);
            if (!existing && temporary)
                RemoveSymbolLater(FindSymbol(name));
            break;
        }
        case 0x23: { // script name ... endscript
            uint32_t name = Read4Bytes(p + 2);
            const uint8_t* start = p + 6;
            p = SkipToEndOfScript(start);
            uint32_t size = static_cast<uint32_t>(p - start);
            uint32_t checksum = CalculateScriptChecksum(start);
            CSymbolTableEntry* existing = FindSymbol(name);
            if (existing) {
                // (an unchanged script is left as it is)
                if (existing->type == ESYMBOLTYPE_QSCRIPT && checksum == *reinterpret_cast<uint32_t*>(existing->raw))
                    break;
                Replace(existing, temporary);
                CreateScriptSymbol(size, start, name, checksum, filename);
            } else {
                CSymbolTableEntry* symbol = CreateScriptSymbol(size, start, name, checksum, filename);
                if (temporary)
                    RemoveSymbolLater(symbol);
            }
            break;
        }
        case 0x2B:
            p = SkipToken(p);
            break;
        case 0x45: { // a section marker
            uint32_t marker = Read4Bytes(p + 2);
            p += 6;
            if (marker == 0x0FA33D9B) {
                // The end of a debug section.
                Mem::PopContext();
                Mem::PushContext(manager->m_scriptHeap);
            } else if (marker == 0x70CB0238) {
                // A debug section: kept (on the debug heap) only while checksum names are.
                if (ChecksumNamesEnabled()) {
                    Mem::PopContext();
                    Mem::PushContext(manager->m_debugHeap);
                } else {
                    while (*p != 0x45 && *p != 0x00)
                        p = SkipToken(p);
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
    auto* bytes = reinterpret_cast<const uint8_t*>(script);
    if (*reinterpret_cast<const uint32_t*>(bytes + 0xC8) == name)
        return true;
    // The calls it's in the middle of: 0x30 bytes each, the script's name first.
    int depth = *reinterpret_cast<const int*>(bytes + 0x30);
    auto* frame = *reinterpret_cast<const uint8_t* const*>(bytes + 0x94);
    for (int i = 0; i < depth; ++i, frame += 0x30) {
        if (*reinterpret_cast<const uint32_t*>(frame) == name)
            return true;
    }
    return false;
}

// 0x0046ED20
void RestartChangedScripts()
{
    for (CSymbolTableEntry* symbol = GetNextSymbol(nullptr); symbol; symbol = GetNextSymbol(symbol)) {
        // (byte 0 bit 0: new since the last time - CreateSymbol sets it)
        if ((symbol->unknown0 & 1) && symbol->type == ESYMBOLTYPE_QSCRIPT) {
            for (CScript* script = GetNextScript(nullptr); script; script = GetNextScript(script)) {
                if (ScriptRefersTo(script, symbol->name))
                    RestartScript(script);
            }
            symbol->unknown0 &= 0xFE;
        }
    }
}

// 0x0046ED90
void LoadQBData(const char* filename, const uint8_t* data, int unused, int useQbNames, int temporary)
{
    if (Read4Bytes(data) == 0xB075EF6F)
        ParsePrecompiledQB(filename, data, unused);
    else
        ParseQB(filename, data, unused, useQbNames, temporary);
    RestartChangedScripts();
}

// 0x0046EDE0
void LoadQB(const char* filename, int unused, int temporary)
{
    size_t length = std::strlen(filename);
    const char* extension = PrecompiledExtension();
    Mem::Manager* manager = Mem::Manager::Get();
    if (filename[length - 4] == '.' && _stricmp(filename + length - 3, extension) == 0) {
        // Pre-compiled: kept loaded.
        Mem::PushContext(manager->m_scriptHeap);
        auto* data = static_cast<const uint8_t*>(Pip::Load(filename, false, nullptr, nullptr, nullptr));
        Mem::PopContext();
        ParsePrecompiledQB(filename, data, unused);
        LoadChecksumNames(filename);
    } else {
        Mem::PushContext(manager->m_topDownHeap);
        auto* data = static_cast<const uint8_t*>(Pip::Load(filename, false, nullptr, nullptr, nullptr));
        Mem::PopContext();
        ParseQB(filename, data, unused, 0, temporary);
        Pip::Unload(filename);
    }
    RestartChangedScripts();
}

} // namespace Script

// --- the entry points with register arguments -------------------------------------------------------------

namespace {

const uint8_t* __cdecl ParseSymbolDefinitionRegisters(const uint8_t* token, uint32_t name, const char* filename)
{
    return Script::ParseSymbolDefinition(token, name, filename);
}
Script::CSymbolTableEntry* __cdecl CreateScriptSymbolRegisters(uint32_t size, const uint8_t* code, uint32_t name,
                                                               uint32_t checksum, const char* filename)
{
    return Script::CreateScriptSymbol(size, code, name, checksum, filename);
}

} // namespace

// 0x00472240: the token in ECX, the name and file name on the stack (the caller pops them).
__declspec(naked) void Script::ParseSymbolDefinitionStub()
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
__declspec(naked) void Script::CreateScriptSymbolStub()
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
    bool RefersTo(uint32_t name) { return Script::ScriptRefersTo(reinterpret_cast<Script::CScript*>(this), name); }
};
} // namespace

REPLACE(0x00472420, Script::ParseQB);
REPLACE(0x00472C50, &ScriptAdapter::RefersTo);
REPLACE(0x0046ED20, Script::RestartChangedScripts);
REPLACE(0x0046ED90, Script::LoadQBData);
REPLACE(0x0046EDE0, Script::LoadQB);
REPLACE_UNCHECKED(0x00472240, Script::ParseSymbolDefinitionStub); // arguments in ECX and on the stack
REPLACE_UNCHECKED(0x0046FE40, Script::CreateScriptSymbolStub);    // arguments in EAX, EBX and on the stack
