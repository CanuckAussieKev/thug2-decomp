// Script functions (CFuncs) for the memory card.
// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt (the PC port's changes in it).

#include "game/mc/memcard_cfuncs.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "game/core/mem.h"
#include "game/mc/memcard.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mc {

// The PC port's free space (based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt): with a card
// (its +0x0C flag), the disk's free 16 KB blocks, clamped to 0x7FFE0001 so that scripts can add to it without
// overflowing (the original's count, card->GetFreeBlocks(), is unclamped).
int32_t __fastcall FreeDiskBlocks(Card* card)
{
    if (!reinterpret_cast<const uint8_t*>(card)[0xC])
        return 0;
    ULARGE_INTEGER freeSpace;
    ULARGE_INTEGER totalSpace;
    if (!GetDiskFreeSpaceExA(nullptr, &freeSpace, &totalSpace, nullptr))
        return 0;
    uint64_t blocks = freeSpace.QuadPart >> 14;
    constexpr uint64_t kLimit = INT32_MAX - UINT16_MAX * 2; // (a number that leaves room for a bit of maths)
    if (blocks > kLimit)
        blocks = kLimit;
    return static_cast<int32_t>(blocks);
}

namespace {

constexpr uint32_t kSpaceAvailable = 0x0C37C363; // "SpaceAvailable"
constexpr uint32_t kFilesLeft = 0x0855B2FC;      // "FilesLeft"

// The calling script's own parameters (Script::CScript +0x14): where CFuncs leave results for it.
Script::CStruct* ResultParams(Script::CScript* script)
{
    return *reinterpret_cast<Script::CStruct**>(reinterpret_cast<char*>(script) + 0x14);
}

} // namespace

// 0x005A6E40. Results for the script: SpaceAvailable (16 KB blocks; 0 until a card is found) and FilesLeft
// (always 1000000). False without a card.
bool GetMemCardSpaceAvailable(Script::CStruct*, Script::CScript* script)
{
    ResultParams(script)->AddInteger(kSpaceAvailable, 0);
    ResultParams(script)->AddInteger(kFilesLeft, 1000000);
    Card* card = GameVariable<Manager*>(0x007D1890)->GetCard(0, 0);
    if (!card)
        return false;
    // PC port (PartyMod's call at 0x005A6E7E): the clamped disk count in place of the original's
    // card->GetFreeBlocks().
    ResultParams(script)->AddInteger(kSpaceAvailable, FreeDiskBlocks(card));
    return true;
}

// --- LoadFromMemoryCard -------------------------------------------------------------------------------------

namespace {

int Platform() { return GameVariable<int>(0x007CEFA8); }

// Pause (1), unpause (0) or restore (-1) the music and the music streams.
void PauseMusic(int pause) { GameFunction<void(__cdecl*)(int)>(0x00450F70)(pause); }
void PauseStream(int pause) { GameFunction<void(__cdecl*)(int)>(0x00451010)(pause); }

Script::CStruct* NewParams()
{
    Script::CStruct* params = Script::NewStruct();
    return params ? params->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// A save file: this header, then two structures written by the script writer (0x0047A0C0 reads one back and
// returns the end of what it read).
struct SaveHeader {
    uint32_t checksum; // CRC of the whole file with this field 0
    uint32_t unknown4;
    uint32_t unknown8;
    int size;          // of the whole file
    int version;       // must match the save type's (0x005A6210)
};
static_assert(sizeof(SaveHeader) == 0x14, "the structures follow at +0x14");
uint8_t* ReadStructure(Script::CStruct* into, uint8_t* data)
{
    return GameFunction<uint8_t*(__cdecl*)(Script::CStruct*, uint8_t*)>(0x0047A0C0)(into, data);
}

// The save type's helpers (register arguments, so called through inline assembly).
// 0x005A5F70: 8 letters made from the name's checksum, the last replaced by the type's letter; returns `buffer`.
void MakeFileName(const char* name, char* buffer, uint32_t saveType)
{
    uint32_t code = 0x005A5F70;
    __asm {
        mov eax, name
        mov esi, buffer
        mov edi, saveType
        call code
    }
}
// 0x005A6390: that file name with the save folder in front (console builds).
void MakeFolderFileName(uint32_t saveType, const char* name, char* buffer)
{
    uint32_t code = 0x005A6390;
    __asm {
        push buffer
        push name
        mov edi, saveType
        call code
        add esp, 8
    }
}
// 0x005A62A0: the type's file name pattern filled in with the name (into a shared buffer, 0x007CECE8).
const char* TypeFileName(uint32_t saveType, const char* name)
{
    uint32_t code = 0x005A62A0;
    const char* result;
    __asm {
        mov eax, saveType
        mov ecx, name
        call code
        mov result, eax
    }
    return result;
}
// 0x005A6210: the type's file version (-1 for an unknown type).
int TypeVersion(uint32_t saveType)
{
    uint32_t code = 0x005A6210;
    int result;
    __asm {
        mov eax, saveType
        call code
        mov result, eax
    }
    return result;
}
// 0x005A7340: the size of a save of this type.
int TypeFileSize(uint32_t saveType)
{
    uint32_t code = 0x005A7340;
    int result;
    __asm {
        mov ecx, saveType
        call code
        mov result, eax
    }
    return result;
}
// 0x005A81B0: put the loaded contents in place (per type: skater, career, settings...).
void ApplyLoadedContents(uint32_t saveType, Script::CStruct* contents, Script::CScript* script, Script::CStruct* params)
{
    uint32_t code = 0x005A81B0;
    __asm {
        push params
        push script
        mov eax, saveType
        mov esi, contents
        call code
        add esp, 8
    }
}

constexpr uint32_t kNoApply = 0x8C6808D4;        // integer parameter: keep the contents in 0x007CECD4 instead
constexpr uint32_t kLoadFailed = 0xFBF9C22A;     // checksum left in the script's parameters on a bad file
constexpr uint32_t kObfuscatedType = 0xCA41692D; // its second structure's bytes are stored plus 0x69
constexpr uint32_t kAlsoApplyType = 0x61A1BC57;  // applied even when kept

} // namespace

// 0x005A8730. LoadFromMemoryCard Name=<string> Type=<checksum> [<kNoApply>=<integer>]: reads the save file and
// applies it (or keeps its contents in 0x007CECD4 and its type in 0x007CECD8 when kNoApply is non-zero). True if
// the file was read. A file of the wrong size, too big, of another version or (originally) with a bad checksum
// leaves kLoadFailed in the script's parameters. PartyMod: the checksum isn't checked.
bool LoadFromMemoryCard(Script::CStruct* params, Script::CScript* script)
{
    if (Platform() != 4) {
        PauseMusic(1);
        PauseStream(1);
    }
    Script::CStruct* header = NewParams();   // the file's first structure (read, then dropped)
    Script::CStruct* contents = NewParams(); // the second: what's loaded
    const char* name = reinterpret_cast<const char*>(0x006469B4); // ""
    uint32_t type = 0;
    uint8_t* data = nullptr;
    bool loaded = false;
    int noApply = 0;
    params->GetInteger(kNoApply, &noApply, 0);
    Card* card = GameVariable<Manager*>(0x007D1890)->GetCard(0, 0);
    // (the second test is a card method that's the shared always-true stub)
    if (card && GameFunction<bool(__thiscall*)(Card*)>(0x005B9760)(card)) {
        params->GetString("Name", &name, 0);
        params->GetChecksum("Type", &type, 0);
        char path[104];
        path[0] = '\0';
        bool named = true;
        switch (Platform()) {
        case 1:
        case 2:
        case 3: { // console folders: /<dir><file>/<dir><file>
            const char* folder = GameFunction<const char*(__cdecl*)()>(0x005B5F10)();
            char file[20];
            MakeFileName(name, file, type);
            std::snprintf(path, sizeof(path), "/%s%s/%s%s", folder, file, folder, file);
            break;
        }
        case 5: {
            char file[104];
            MakeFolderFileName(type, name, file);
            std::snprintf(path, sizeof(path), "%s%s", file, file);
            break;
        }
        case 4:
        case 7: {
            const char* converted = card->ConvertFilename(TypeFileName(type, name));
            if (!converted)
                named = false;
            else
                std::snprintf(path, sizeof(path), "%s", converted);
            break;
        }
        default:
            named = false;
            break;
        }
        File* file = named ? card->Open(path, 1, 0) : nullptr;
        if (file) {
            int size = static_cast<int>(file->Seek(0, 2));
            bool bad = size != TypeFileSize(type);
            if (!bad) {
                data = static_cast<uint8_t*>(Mem::Malloc(size));
                file->Seek(0, 0);
                if (file->Read(data, size) == size) {
                    auto* fileHeader = reinterpret_cast<SaveHeader*>(data);
                    fileHeader->checksum = 0;
                    // The original also rejected the file unless
                    //     Crc::GenerateCRCFromBuffer(data, fileHeader->size) == <the checksum read>
                    // - the test PartyMod NOPs (its patch at 0x005A8A01).
                    bad = fileHeader->size > 500000 || fileHeader->version != TypeVersion(type);
                    if (!bad) {
                        uint8_t* rest = ReadStructure(header, data + sizeof(SaveHeader));
                        if (type == kObfuscatedType) {
                            int count = static_cast<int>(data + fileHeader->size - rest);
                            for (int i = 0; i < count; ++i)
                                rest[i] -= 0x69;
                        }
                        ReadStructure(contents, rest);
                        if (noApply == 0) {
                            ApplyLoadedContents(type, contents, script, params);
                        } else {
                            // (a null structure here would crash, as in the original; the previous one leaks)
                            Script::CStruct* kept = NewParams();
                            GameVariable<Script::CStruct*>(0x007CECD4) = kept;
                            kept->AppendStructureIfAny(contents);
                            GameVariable<uint32_t>(0x007CECD8) = type;
                            if (type == kAlsoApplyType)
                                ApplyLoadedContents(type, contents, script, nullptr);
                        }
                        loaded = true;
                        if (noApply == 0)
                            GameFunction<void(__cdecl*)(uint32_t, Script::CStruct*, void*, int, int)>(0x00475790)(
                                0x0EB9402D, params, nullptr, 0, 0);
                    }
                }
            }
            if (bad)
                ResultParams(script)->AddChecksum(0u, kLoadFailed);
            file->Close();
            reinterpret_cast<void(__thiscall*)(File*, int)>(file->m_node.vtable[0])(file, 1); // delete
            if (data)
                Mem::FreeMalloc(data);
        }
    }
    DeleteParams(header);
    DeleteParams(contents);
    if (Platform() != 4) {
        PauseMusic(-1);
        PauseStream(-1);
    }
    return loaded;
}

} // namespace Mc

REPLACE(0x005A6E40, Mc::GetMemCardSpaceAvailable);
REPLACE(0x005A8730, Mc::LoadFromMemoryCard);
