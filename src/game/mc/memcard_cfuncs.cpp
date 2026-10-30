// Script functions (CFuncs) for the memory card.
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
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Mc {

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
    Card* card = g_theCard->GetCard(0, 0);
    if (!card)
        return false;
    // Originally from PartyMod Fixes (its call at 0x005A6E7E): the clamped disk count in place of the original's
    // card->GetFreeBlocks().
    ResultParams(script)->AddInteger(kSpaceAvailable, FreeDiskBlocks(card));
    return true;
}

// --- LoadFromMemoryCard -------------------------------------------------------------------------------------

namespace {

int Platform() { return GameConfig::g_platform; }

// Pause (1), unpause (0) or restore (-1) the music and the music streams.
void PauseMusic(int pause) { Pcm::PauseMusic(pause); }
void PauseStream(int pause) { Pcm::PauseStream(pause); }

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
    return const_cast<uint8_t*>(Script::ReadFromBuffer(into, data));
}

constexpr uint32_t kNoApply = 0x8C6808D4;        // integer parameter: keep the contents in 0x007CECD4 instead
constexpr uint32_t kLoadFailed = 0xFBF9C22A;     // checksum left in the script's parameters on a bad file
constexpr uint32_t kObfuscatedType = 0xCA41692D; // its second structure's bytes are stored plus 0x69
constexpr uint32_t kAlsoApplyType = 0x61A1BC57;  // applied even when kept

} // namespace

GAME_GLOBAL(bool, g_outOfSpace, 0x007CECD0);
GAME_GLOBAL(Script::CStruct*, g_keptParams, 0x007CECD4);
GAME_GLOBAL(uint32_t, g_keptType, 0x007CECD8);
GAME_GLOBAL(uint32_t, g_sectionsToApplyWhenLoading, 0x007CECDC);
GAME_GLOBAL(bool, g_loadedCustomSkater, 0x007CECE0);

// 0x005A8730. LoadFromMemoryCard Name=<string> Type=<checksum> [<kNoApply>=<integer>]: reads the save file and
// applies it (or keeps its contents in 0x007CECD4 and its type in 0x007CECD8 when kNoApply is non-zero). True if
// the file was read. A file of the wrong size, too big, of another version or (originally) with a bad checksum
// leaves kLoadFailed in the script's parameters. Originally from PartyMod Fixes: the checksum isn't checked.
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
    Card* card = g_theCard->GetCard(0, 0);
    // (the second test is a card method that's the shared always-true stub, 0x005B9760: always true)
    if (card && Stubs::ReturnTrue()) {
        params->GetString("Name", &name, 0);
        params->GetChecksum("Type", &type, 0);
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
                            g_keptParams = kept;
                            kept->AppendStructureIfAny(contents);
                            g_keptType = type;
                            if (type == kAlsoApplyType)
                                ApplyLoadedContents(type, contents, script, nullptr);
                        }
                        loaded = true;
                        if (noApply == 0)
                            Script::RunScript(0x0EB9402Du, params, nullptr, false, nullptr);
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

// --- the save state flags -----------------------------------------------------------------------------------

// 0x005A6550
bool SaveFailedDueToInsufficientSpace(Script::CStruct*, Script::CScript*) { return g_outOfSpace; }

// 0x005A6560
bool SetSectionsToApplyWhenLoading(Script::CStruct* params, Script::CScript*)
{
    Dbg::Printf(reinterpret_cast<const char*>(0x00657848)); // "ScriptSetSectionsToApplyWhenLoading was called..."
    uint32_t& sections = SectionsToApplyWhenLoading();
    sections = 0;
    if (params->ContainsFlag(0x16B506C0u)) // ApplyCustomSkater
        sections |= 8;
    if (params->ContainsFlag(0xDC7EA3CEu)) // ApplyStorySkater
        sections |= 4;
    if (params->ContainsFlag(0x33EC233Fu)) // ApplyStory
        sections |= 2;
    if (params->ContainsFlag(0xB39D7CC4u)) // (name unknown)
        sections |= 1;
    if (params->ContainsFlag(0xC4E78E22u)) // all
        sections = 0xFFFFFFFF;
    return true;
}

// 0x005A6600
bool LoadedCustomSkater(Script::CStruct*, Script::CScript*) { return g_loadedCustomSkater; }

uint32_t& SectionsToApplyWhenLoading() { return g_sectionsToApplyWhenLoading; }

// --- the card and the saves' summaries ----------------------------------------------------------------------

namespace {

Card* TheCard() { return g_theCard->GetCard(0, 0); }

// The script pools' pairs whose current pool the temporary pools switch: components, structures, vectors.
Mem::PoolPair& ComponentPools() { return GameVariable<Mem::PoolPair>(0x006F2338); }
Mem::PoolPair& StructPools() { return GameVariable<Mem::PoolPair>(0x006F3614); }
Mem::PoolPair& VectorPools() { return GameVariable<Mem::PoolPair>(0x006F3654); }

} // namespace

// 0x005A6D30
bool GetMostRecentSave(Script::CStruct* params, Script::CScript* script)
{
    ResultParams(script)->RemoveComponent("MostRecentSave");
    Script::CArray* saves = nullptr;
    params->GetArray(0u, &saves, 0);
    if (!saves)
        return false;
    uint32_t type = 0;
    params->GetChecksum(0u, &type, 0);
    Script::CStruct* best = nullptr;
    for (uint32_t i = 0; i < saves->m_size; ++i) {
        auto* save = reinterpret_cast<Script::CStruct*>(saves->GetElement(i));
        uint32_t fileType = 0;
        save->GetChecksum("file_type", &fileType, 0);
        if (type != 0 && fileType != type) // (any type when none is asked for)
            continue;
        // a bad save only counts on the PC (4) and platform 5
        if ((save->ContainsFlag("BadVersion") || save->ContainsFlag("Corrupt")) && Platform() != 4 && Platform() != 5)
            continue;
        if (!best || IsLater(save, best))
            best = save;
    }
    if (!best)
        return false;
    ResultParams(script)->AddStructure("MostRecentSave", best);
    return true;
}

// 0x005A7210
bool FormatCard(Script::CStruct*, Script::CScript*)
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
bool CardIsInSlot(Script::CStruct*, Script::CScript*) { return TheCard() != nullptr; }

// 0x005A7280
bool CardIsFormatted(Script::CStruct*, Script::CScript*)
{
    if (!TheCard())
        return false;
    return Stubs::ReturnTrue(); // (the card's IsFormatted: the shared always-true stub, 0x005B9760)
}

// 0x005A72A0
bool GetSummaryInfo(Script::CStruct* params, Script::CScript* script)
{
    constexpr uint32_t kVaultData = 0xC78DABF6; // "VaultData": the kept contents' summary
    uint32_t type = 0;
    params->GetChecksum(0u, &type, 0);
    int vaultData = 0;
    params->GetInteger(kVaultData, &vaultData, 0);
    if (vaultData) {
        WriteSummary(g_keptParams, ResultParams(script), g_keptType);
        return true;
    }
    if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        Game::GetGoalManager()->LevelUnload();
    }
    WriteSummary(nullptr, ResultParams(script), type);
    return true;
}

// 0x005A7430
bool GetMemCardDataForUpload(Script::CStruct*, Script::CScript* script)
{
    ResultParams(script)->AddChecksum(0x7321A8D6u, g_keptType); // type
    return true;
}

// 0x005A7450
bool GetMaxTHPS4FilesAllowed(Script::CStruct*, Script::CScript* script)
{
    ResultParams(script)->AddInteger(0x7CC9E431u, 75); // MaxTHPS4FilesAllowed
    return true;
}

// 0x005A7470
bool SwitchToTempPoolsIfTheyExist(Script::CStruct*, Script::CScript*)
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
bool SwitchToRegularPools(Script::CStruct*, Script::CScript*)
{
    if (ComponentPools().current || StructPools().current || VectorPools().current) {
        --ComponentPools().current;
        --StructPools().current;
        --VectorPools().current;
    }
    return true;
}

} // namespace Mc

REPLACE(0x005A6D30, Mc::GetMostRecentSave);
REPLACE(0x005A7210, Mc::FormatCard);
REPLACE(0x005A7260, Mc::CardIsInSlot);
REPLACE(0x005A7280, Mc::CardIsFormatted);
REPLACE(0x005A72A0, Mc::GetSummaryInfo);
REPLACE(0x005A7430, Mc::GetMemCardDataForUpload);
REPLACE(0x005A7450, Mc::GetMaxTHPS4FilesAllowed);
REPLACE(0x005A7470, Mc::SwitchToTempPoolsIfTheyExist);
REPLACE(0x005A74F0, Mc::SwitchToRegularPools);

REPLACE(0x005A6550, Mc::SaveFailedDueToInsufficientSpace);
REPLACE(0x005A6560, Mc::SetSectionsToApplyWhenLoading);
REPLACE(0x005A6600, Mc::LoadedCustomSkater);
REPLACE(0x005A6E40, Mc::GetMemCardSpaceAvailable);
REPLACE(0x005A8730, Mc::LoadFromMemoryCard);
