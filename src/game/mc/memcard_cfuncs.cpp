// Script functions (ScriptCommands) for the memory card.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/mc/memcard_cfuncs.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/stubs.h"
#include "game/goals/goal_manager.h"
#include "game/mc/memcard.h"
#include "game/mc/save_contents.h"
#include "game/mc/save_types.h"
#include "game/pcm/music.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/sys/game_config.h"

namespace MemCard {

// PartyMod's free space (code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md): with a card
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

// The calling script's own parameters (Scripts::CScript +0x14): where ScriptCommands leave results for it.
Scripts::CStruct* ResultParams(Scripts::CScript* script)
{
    return *reinterpret_cast<Scripts::CStruct**>(reinterpret_cast<char*>(script) + 0x14);
}

} // namespace

// 0x005A6E40. Results for the script: SpaceAvailable (16 KB blocks; 0 until a card is found) and FilesLeft
// (always 1000000). False without a card.
bool GetMemCardSpaceAvailable(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultParams(script)->PutInt(kSpaceAvailable, 0);
    ResultParams(script)->PutInt(kFilesLeft, 1000000);
    Card* card = g_theCard->GetStorageSlot(0, 0);
    if (!card)
        return false;
    // Originally from PartyMod Fixes (its call at 0x005A6E7E): the clamped disk count in place of the original's
    // card->GetFreeBlocks().
    ResultParams(script)->PutInt(kSpaceAvailable, FreeDiskBlocks(card));
    return true;
}

// --- LoadFromMemoryCard -------------------------------------------------------------------------------------

namespace {

int Platform() { return GameConfig::g_platform; }

// Pause (1), unpause (0) or restore (-1) the music and the music streams.
void PauseMusic(int pause) { AudioStream::PauseMusic(pause); }
void PauseStream(int pause) { AudioStream::PauseStream(pause); }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* params = Scripts::NewStruct();
    return params ? params->Init() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
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
uint8_t* ReadStructure(Scripts::CStruct* into, uint8_t* data)
{
    return const_cast<uint8_t*>(Scripts::DecodeFromBytes(into, data));
}

constexpr uint32_t kNoApply = 0x8C6808D4;        // integer parameter: keep the contents in 0x007CECD4 instead
constexpr uint32_t kLoadFailed = 0xFBF9C22A;     // checksum left in the script's parameters on a bad file
constexpr uint32_t kObfuscatedType = 0xCA41692D; // its second structure's bytes are stored plus 0x69
constexpr uint32_t kAlsoApplyType = 0x61A1BC57;  // applied even when kept

} // namespace

bool g_outOfSpace;
Scripts::CStruct* g_keptParams;
uint32_t g_keptType;
uint32_t g_sectionsToApplyWhenLoading;
bool g_loadedCustomSkater;

// 0x005A8730. LoadFromMemoryCard Name=<string> Type=<checksum> [<kNoApply>=<integer>]: reads the save file and
// applies it (or keeps its contents in 0x007CECD4 and its type in 0x007CECD8 when kNoApply is non-zero). True if
// the file was read. A file of the wrong size, too big, of another version or (originally) with a bad checksum
// leaves kLoadFailed in the script's parameters. Originally from PartyMod Fixes: the checksum isn't checked.
bool LoadFromMemoryCard(Scripts::CStruct* params, Scripts::CScript* script)
{
    if (Platform() != 4) {
        PauseMusic(1);
        PauseStream(1);
    }
    Scripts::CStruct* header = NewParams();   // the file's first structure (read, then dropped)
    Scripts::CStruct* contents = NewParams(); // the second: what's loaded
    const char* name = ""; // ""
    uint32_t type = 0;
    uint8_t* data = nullptr;
    bool loaded = false;
    int noApply = 0;
    params->FetchIntValue(kNoApply, &noApply, 0);
    Card* card = g_theCard->GetStorageSlot(0, 0);
    // (the second test is a card method that's the shared always-true stub, 0x005B9760: always true)
    if (card && Stubs::ReturnTrue()) {
        params->GetString("Name", &name, 0);
        params->ReadHashValue("Type", &type, 0);
        char path[104];
        path[0] = '\0';
        bool named = true;
        switch (Platform()) {
        case 1:
        case 2:
        case 3: { // console folders: /<dir><file>/<dir><file>
            const char* folder = GameConfig::GetPathPrefix();
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
        FileLoad* file = named ? card->Open(path, 1, 0) : nullptr;
        if (file) {
            int size = static_cast<int>(file->Seek(0, 2));
            bool bad = size != TypeFileSize(type);
            if (!bad) {
                data = static_cast<uint8_t*>(MemSys::AllocBlock(size));
                file->Seek(0, 0);
                if (file->Read(data, size) == size) {
                    auto* fileHeader = reinterpret_cast<SaveHeader*>(data);
                    fileHeader->checksum = 0;
                    // The original also rejected the file unless
                    //     Checksums::GenerateCRCFromBuffer(data, fileHeader->size) == <the checksum read>
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
                            Scripts::CStruct* kept = NewParams();
                            g_keptParams = kept;
                            kept->AppendStructureIfAny(contents);
                            g_keptType = type;
                            if (type == kAlsoApplyType)
                                ApplyLoadedContents(type, contents, script, nullptr);
                        }
                        loaded = true;
                        if (noApply == 0)
                            Scripts::LaunchScript(0x0EB9402Du, params, nullptr, false, nullptr);
                    }
                }
            }
            if (bad)
                ResultParams(script)->PutNameKey(0u, kLoadFailed);
            file->Close();
            reinterpret_cast<void(__thiscall*)(FileLoad*, int)>(file->m_listLink.vtable[0])(file, 1); // delete
            if (data)
                MemSys::FreeMalloc(data);
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

// --- the save state flags -----------------------------------------------------------------------------------

// 0x005A6550
bool SaveFailedDueToInsufficientSpace(Scripts::CStruct*, Scripts::CScript*) { return g_outOfSpace; }

// 0x005A6560
bool SetSectionsToApplyWhenLoading(Scripts::CStruct* params, Scripts::CScript*)
{
    DebugOut::Printf("ScriptSetSectionsToApplyWhenLoading was called.........................\n"); // "ScriptSetSectionsToApplyWhenLoading was called..."
    uint32_t& sections = SectionsToApplyWhenLoading();
    sections = 0;
    if (params->HasBareFlag(0x16B506C0u)) // ApplyCustomSkater
        sections |= 8;
    if (params->HasBareFlag(0xDC7EA3CEu)) // ApplyStorySkater
        sections |= 4;
    if (params->HasBareFlag(0x33EC233Fu)) // ApplyStory
        sections |= 2;
    if (params->HasBareFlag(0xB39D7CC4u)) // (name unknown)
        sections |= 1;
    if (params->HasBareFlag(0xC4E78E22u)) // all
        sections = 0xFFFFFFFF;
    return true;
}

// 0x005A6600
bool LoadedCustomSkater(Scripts::CStruct*, Scripts::CScript*) { return g_loadedCustomSkater; }

uint32_t& SectionsToApplyWhenLoading() { return g_sectionsToApplyWhenLoading; }

// --- the card and the saves' summaries ----------------------------------------------------------------------

namespace {

Card* TheCard() { return g_theCard->GetStorageSlot(0, 0); }

// The script pools' pairs whose current pool the temporary pools switch: components, structures, vectors.
MemSys::PoolPair& ComponentPools() { return Scripts::g_componentPools; }
MemSys::PoolPair& StructPools() { return Scripts::g_structPools; }
MemSys::PoolPair& VectorPools() { return Scripts::g_vectorPools; }

} // namespace

// 0x005A6D30
bool GetMostRecentSave(Scripts::CStruct* params, Scripts::CScript* script)
{
    ResultParams(script)->RemoveComponent("MostRecentSave");
    Scripts::CArray* saves = nullptr;
    params->GetArray(0u, &saves, 0);
    if (!saves)
        return false;
    uint32_t type = 0;
    params->ReadHashValue(0u, &type, 0);
    Scripts::CStruct* best = nullptr;
    for (uint32_t i = 0; i < saves->m_size; ++i) {
        auto* save = reinterpret_cast<Scripts::CStruct*>(saves->GetElement(i));
        uint32_t fileType = 0;
        save->ReadHashValue("file_type", &fileType, 0);
        if (type != 0 && fileType != type) // (any type when none is asked for)
            continue;
        // a bad save only counts on the PC (4) and platform 5
        if ((save->HasBareFlag("BadVersion") || save->HasBareFlag("Corrupt")) && Platform() != 4 && Platform() != 5)
            continue;
        if (!best || IsLater(save, best))
            best = save;
    }
    if (!best)
        return false;
    ResultParams(script)->PutSubStruct("MostRecentSave", best);
    return true;
}

// 0x005A7210
bool FormatCard(Scripts::CStruct*, Scripts::CScript*)
{
    PauseMusic(1);
    PauseStream(1);
    bool formatted = false;
    if (TheCard())
        formatted = Stubs::ReturnFalse(); // (the card's Format: the shared always-false stub, 0x005B9E50)
    PauseMusic(-1);
    PauseStream(-1);
    return formatted;
}

// 0x005A7260
bool CardIsInSlot(Scripts::CStruct*, Scripts::CScript*) { return TheCard() != nullptr; }

// 0x005A7280
bool CardIsFormatted(Scripts::CStruct*, Scripts::CScript*)
{
    if (!TheCard())
        return false;
    return Stubs::ReturnTrue(); // (the card's IsFormatted: the shared always-true stub, 0x005B9760)
}

// 0x005A72A0
bool GetSummaryInfo(Scripts::CStruct* params, Scripts::CScript* script)
{
    constexpr uint32_t kVaultData = 0xC78DABF6; // "VaultData": the kept contents' summary
    uint32_t type = 0;
    params->ReadHashValue(0u, &type, 0);
    int vaultData = 0;
    params->FetchIntValue(kVaultData, &vaultData, 0);
    if (vaultData) {
        WriteSummary(g_keptParams, ResultParams(script), g_keptType);
        return true;
    }
    if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        GoalSystem::GetObjectiveBoard()->LevelUnload();
    }
    WriteSummary(nullptr, ResultParams(script), type);
    return true;
}

// 0x005A7430
bool GetMemCardDataForUpload(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultParams(script)->PutNameKey(0x7321A8D6u, g_keptType); // type
    return true;
}

// 0x005A7450
bool GetMaxTHPS4FilesAllowed(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultParams(script)->PutInt(0x7CC9E431u, 75); // MaxTHPS4FilesAllowed
    return true;
}

// 0x005A7470
bool SwitchToTempPoolsIfTheyExist(Scripts::CStruct*, Scripts::CScript*)
{
    ++ComponentPools().current;
    ++StructPools().current;
    ++VectorPools().current;
    if (ComponentPools().pools[ComponentPools().current] && StructPools().pools[StructPools().current] &&
        VectorPools().pools[VectorPools().current])
        return true;
    --ComponentPools().current;
    --StructPools().current;
    --VectorPools().current;
    return false;
}

// 0x005A74F0
bool SwitchToRegularPools(Scripts::CStruct*, Scripts::CScript*)
{
    if (ComponentPools().current || StructPools().current || VectorPools().current) {
        --ComponentPools().current;
        --StructPools().current;
        --VectorPools().current;
    }
    return true;
}

} // namespace Mc
