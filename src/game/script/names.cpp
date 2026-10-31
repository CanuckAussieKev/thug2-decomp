#include "game/script/names.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/script/struct.h"
#include "game/script/cfuncs_scene.h"

namespace Script {

char g_checksumNameBuffer[100];
char* g_namesStart;
char* g_namesNext;

namespace {

struct ChecksumName {
    uint32_t checksum;
    const char* name;
};
constexpr uint32_t kBucketSize = 10;

ChecksumName* g_checksumNameTable;
ChecksumName*& NameTable() { return g_checksumNameTable; }
char*& NamesStart() { return g_namesStart; }
char*& NamesNext() { return g_namesNext; }

ChecksumName* Bucket(uint32_t checksum) { return NameTable() + (checksum & 0xFFFF) * kBucketSize; }

// A hex digit's value (anything else counts as its character code, as in the original).
int HexDigit(char c)
{
    int value = c;
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return value;
}

} // namespace

bool ChecksumNamesEnabled() { return CFuncs::g_gotExtraMemory; }

// 0x0046CF00
void AllocateChecksumNameLookupTables()
{
    if (!ChecksumNamesEnabled())
        return;
    NameTable() = static_cast<ChecksumName*>(Mem::Malloc(0x10000 * kBucketSize * sizeof(ChecksumName)));
    char* names = static_cast<char*>(Mem::Malloc(5000000));
    NamesStart() = names;
    NamesNext() = names;
    for (uint32_t i = 0; i < 0x10000 * kBucketSize; ++i)
        NameTable()[i] = {0, nullptr};
    names[0] = 0;
}

// 0x0046CF60
void AddChecksumName(uint32_t checksum, const char* name)
{
    if (!ChecksumNamesEnabled())
        return;
    // (a full bucket runs on into the next one, as in the original)
    ChecksumName* entry = Bucket(checksum);
    for (; entry->name; ++entry) {
        if (entry->checksum == checksum) {
            // (the original compares the names here - a checksum clash check whose result is ignored)
            return;
        }
    }
    entry->checksum = checksum;
    char* copy = NamesNext();
    entry->name = copy;
    while (*name)
        *copy++ = *name++;
    *copy++ = 0;
    NamesNext() = copy;
}

// 0x0046D0C0
const char* FindChecksumName(uint32_t checksum)
{
    if (!ChecksumNamesEnabled())
        return nullptr;
    ChecksumName* entry = Bucket(checksum);
    for (uint32_t i = 0; i < kBucketSize && entry->name; ++i, ++entry) {
        if (entry->checksum == checksum)
            return entry->name;
    }
    return nullptr;
}

// 0x0046D110
const char* FindChecksumNameOrUnknown(uint32_t checksum)
{
    if (const char* name = FindChecksumName(checksum))
        return name;
    char* buffer = g_checksumNameBuffer;
    std::snprintf(buffer, 32, "Unknown(0x%08x)", checksum);
    return buffer;
}

// 0x0046CD90
void ParseChecksumNames(const char* text, int size, void (*add)(uint32_t checksum, const char* name))
{
    const char* end = text + size;
    while (text != end) {
        text += 2; // "0x"
        uint32_t checksum = 0;
        for (int i = 0; i < 8; ++i)
            checksum = (checksum << 4) + static_cast<uint32_t>(HexDigit(text[i]));
        text += 8 + 1; // and the separator
        char name[256];
        int length = 0;
        while (*text != '\r')
            name[length++] = *text++;
        name[length] = 0;
        add(checksum, name);
        text += 2; // "\r\n"
    }
}

// 0x0046CFE0
void LoadChecksumNames(const char* file)
{
    if (!ChecksumNamesEnabled())
        return;
    char name[260];
    std::memcpy(name, file, std::strlen(file) + 1);
    char* extension = name;
    while (*extension && *extension != '.')
        ++extension;
    std::memcpy(extension, ".chk", 5);
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    auto* text = static_cast<const char*>(Pip::Load(name, false, nullptr, nullptr, nullptr));
    Mem::PopContext();
    ParseChecksumNames(text, Pip::GetFileSize(name), AddChecksumName);
    Pip::Unload(name);
}

// 0x0046EFD0
void RegisterCFunctions(const CFunctionEntry* functions, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t checksum = Crc::GenerateCRCFromString(functions[i].name);
        AddChecksumName(checksum, functions[i].name);
        CSymbolTableEntry* symbol = CreateSymbol(checksum);
        symbol->type = ESYMBOLTYPE_CFUNCTION;
        symbol->raw = reinterpret_cast<uint32_t>(functions[i].function);
    }
}

// 0x0046F020
void RegisterMemberFunctions(const char* const* names, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t checksum = Crc::GenerateCRCFromString(names[i]);
        AddChecksumName(checksum, names[i]);
        CreateSymbol(checksum)->type = ESYMBOLTYPE_MEMBERFUNCTION;
    }
}

// 0x0046F060
void RegisterMemberFunctionTable(void*, const uint32_t* records, int stride)
{
    while (*records) {
        CSymbolTableEntry* symbol = CreateSymbol(*records);
        symbol->raw = reinterpret_cast<uint32_t>(records);
        records = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(records) + stride);
        symbol->type = ESYMBOLTYPE_MEMBERFUNCTION;
    }
}

void FillMemberFunctionCode(uint32_t* records, void* const* code, int count)
{
    for (int i = 0; i < count; ++i) {
        uint32_t* record = records + 6 * i;
        record[2] = reinterpret_cast<uint32_t>(code[i]);
        record[3] = 0;
        record[4] = 0;
        record[5] = 0;
    }
    uint32_t* terminator = records + 6 * count;
    terminator[2] = 0;
    terminator[3] = 0;
    terminator[4] = 0;
    terminator[5] = 0xFFFFFFFFu;
}

} // namespace Script
