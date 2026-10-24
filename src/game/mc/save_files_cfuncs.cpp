// Script functions (CFuncs) for the save files on the memory card (see save_files_cfuncs.h).

#include "game/mc/save_files_cfuncs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/mc/memcard.h"
#include "game/mc/save_types.h"
#include "game/pcm/music.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mc {

namespace {

using Script::CScript;
using Script::CStruct;

int Platform() { return GameVariable<int>(0x007CEFA8); }
Card* TheCard() { return GameVariable<Manager*>(0x007D1890)->GetCard(0, 0); }

// The calling script's own parameters (Script::CScript +0x14): where CFuncs leave results for it.
CStruct* ResultParams(CScript* script) { return *reinterpret_cast<CStruct**>(reinterpret_cast<char*>(script) + 0x14); }

// The console builds' save folder (0x005B5F10, no source yet).
const char* SaveFolder() { return GameFunction<const char*(__cdecl*)()>(0x005B5F10)(); }

// The card's delete of a console file or folder entry: on the PC the shared always-true method (0x005BC7F0).
bool DeleteCardFile(Card* card, const char* name) { return card->AlwaysTrue(reinterpret_cast<int>(name)); }

// A save file's handle deleted (its vtable's scalar deleting destructor).
void DeleteFile(File* file) { reinterpret_cast<void(__thiscall*)(File*, int)>(file->m_node.vtable[0])(file, 1); }

// The pool pairs the temporary pools belong to: components, structures, vectors.
Mem::PoolPair& ComponentPools() { return GameVariable<Mem::PoolPair>(0x006F2338); }
Mem::PoolPair& StructPools() { return GameVariable<Mem::PoolPair>(0x006F3614); }
Mem::PoolPair& VectorPools() { return GameVariable<Mem::PoolPair>(0x006F3654); }

// A pair's second pool made (as the pair's own creator, 0x0046F110 / 0x0046F190 / 0x0046F210, does).
void CreateReservePool(Mem::PoolPair& pools, int itemSize, int count, const char* name)
{
    ++pools.current;
    Script::CreatePool(pools, itemSize, count, name);
    --pools.current;
}
// A pair's second pool deleted: its block and itself, if the pair made it.
void DeleteReservePool(Mem::PoolPair& pools)
{
    ++pools.current;
    if (Mem::Pool* pool = pools.pools[pools.current]) {
        if (reinterpret_cast<uint8_t*>(&pools.unknown4)[pools.current]) {
            Mem::Manager::Get()->Free(pool->m_start); // (its destructor body, 0x005BA4E0)
            Mem::Manager::Get()->Free(pool);
        }
        pools.pools[pools.current] = nullptr;
    }
    --pools.current;
}

} // namespace

// 0x005A6EA0
bool DeleteMemCardFile(CStruct* params, CScript*)
{
    if (Platform() != 4) {
        Pcm::PauseMusic(1);
        Pcm::PauseStream(1);
    }
    Card* card = TheCard();
    if (!card) {
        if (Platform() != 4) {
            Pcm::PauseMusic(-1);
            Pcm::PauseStream(-1);
        }
        return false;
    }
    bool deleted = true;
    const char* xboxDirectory = nullptr; // (read, not used on the PC)
    params->GetString("XBoxDirectoryName", &xboxDirectory, 0);
    const char* cardFileName = nullptr;
    params->GetString("CardFileName", &cardFileName, 0);
    uint32_t type = 0;
    params->GetChecksum("Type", &type, 0);
    const char* userFileName = reinterpret_cast<const char*>(0x006469B4); // ""
    params->GetString("UserFileName", &userFileName, 0);

    bool removed = false;
    char file[20];
    switch (Platform()) {
    case 1:
    case 2:
    case 3: {
        // the file in its folder: /<folder><file>/<folder><file>, the type from its last letter
        char fullName[104];
        const char* path = cardFileName;
        if (!path) {
            const char* folder = SaveFolder();
            MakeFileName(userFileName, file, type);
            std::snprintf(fullName, sizeof(fullName), "/%s%s/%s%s", folder, file, folder, file);
            path = fullName;
        }
        uint32_t fileType = TypeFromLetter(path[std::strlen(path) - 1]);
        // the folder: the first character, then up to the next '/'
        char directory[100];
        directory[0] = path[0];
        int length = 1;
        for (const char* p = path + 1; *p && *p != '/'; ++p)
            directory[length++] = *p;
        directory[length] = '\0';
        if (!DeleteCardFile(card, path))
            deleted = false;
        char icon[104];
        std::snprintf(icon, sizeof(icon), "%s/icon.sys", directory);
        if (!DeleteCardFile(card, icon))
            deleted = false;
        const char* iconFile = nullptr;
        switch (fileType) {
        case kSaveCreatedGoals: iconFile = "%s/goals.ico"; break;
        case kSaveCat: iconFile = "%s/tricks.ico"; break;
        case kSavePark: iconFile = "%s/parked.ico"; break;
        case kSaveCreatedGraphic: iconFile = "%s/grafitti.ico"; break;
        case kSaveNetworkSettings: iconFile = "%s/network.ico"; break;
        case kSaveOptionsAndPros: iconFile = "%s/thps5.ico"; break;
        default: break; // (icon.sys a second time)
        }
        if (iconFile)
            std::snprintf(icon, sizeof(icon), iconFile, directory);
        if (!DeleteCardFile(card, icon))
            deleted = false;
        removed = card->RemoveFile(directory);
        break;
    }
    case 4:
    case 7:
        if (!cardFileName) {
            deleted = false;
            break;
        }
        removed = card->RemoveFile(cardFileName);
        break;
    case 5: {
        if (cardFileName) {
            removed = DeleteCardFile(card, cardFileName);
            break;
        }
        const char* folder = SaveFolder();
        MakeFileName(userFileName, file, type);
        char fullName[104];
        std::snprintf(fullName, sizeof(fullName), "%s%s%s%s", folder, file, folder, file);
        removed = DeleteCardFile(card, fullName);
        break;
    }
    default:
        deleted = false;
        break;
    }
    if (deleted && !removed)
        deleted = false;
    if (Platform() != 4) {
        Pcm::PauseMusic(-1);
        Pcm::PauseStream(-1);
    }
    return deleted;
}

// 0x005A8500
bool GetMemCardSpaceRequired(CStruct* params, CScript* script)
{
    uint32_t type = 0;
    params->GetChecksum(0u, &type, 0);
    int size = TypeFileSize(type);
    ResultParams(script)->AddInteger(0xEB3860C3u, SaveBlocks(type, size)); // SpaceRequired
    return true;
}

// 0x005A8550
bool MemCardFileExists(CStruct* params, CScript* script)
{
    Card* card = TheCard();
    if (!card)
        return false;
    const char* name = nullptr;
    params->GetString("Name", &name, 0);
    uint32_t type = 0;
    params->GetChecksum("Type", &type, 0);
    if (!name || !type)
        return false;
    const char* typeFileName = nullptr; // (only made on the PC and platform 7)
    char path[100];
    path[0] = '\0';
    switch (Platform()) {
    case 4:
    case 7: {
        typeFileName = TypeFileName(type, name);
        const char* converted = card->ConvertFilename(typeFileName);
        if (!converted)
            return false;
        std::snprintf(path, sizeof(path), "%s", converted);
        break;
    }
    case 1:
    case 2:
    case 3:
    case 5: {
        const char* folder = SaveFolder();
        char file[20];
        MakeFileName(name, file, type);
        std::snprintf(path, sizeof(path), "/%s%s/%s%s", folder, file, folder, file);
        break;
    }
    default:
        break;
    }
    File* file = card->Open(path, 1, 0);
    if (!file) {
        // (on the PC, the file that wouldn't open is removed - by the type's name made above)
        if (Platform() == 4)
            card->RemoveFile(typeFileName);
        return false;
    }
    int size = static_cast<int>(file->Seek(0, 2));
    ResultParams(script)->AddInteger("total_file_size", SaveBlocks(type, size));
    file->Flush();
    file->Close();
    DeleteFile(file);
    return true;
}

// 0x005A8BB0
bool ClearMemCardDataForUpload(CStruct*, CScript*)
{
    CStruct*& kept = GameVariable<CStruct*>(0x007CECD4);
    if (kept) {
        kept->Clear();
        Script::DeleteStruct(kept);
        kept = nullptr;
    }
    GameVariable<uint32_t>(0x007CECD8) = 0;
    return true;
}

// 0x005A8DA0
bool CreateTemporaryMemCardPools(CStruct*, CScript*)
{
    if (ComponentPools().pools[ComponentPools().current + 1])
        return true;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    Mem::PushMemProfile("Mem card CComponent");
    CreateReservePool(ComponentPools(), 0x10, 50000, "Reserve CComponent");
    Mem::PopMemProfile();
    Mem::PushMemProfile("Mem card CStruct");
    CreateReservePool(StructPools(), 0x08, 50000, "Reserve CStruct");
    Mem::PopMemProfile();
    Mem::PushMemProfile("Mem card CVector");
    CreateReservePool(VectorPools(), 0x10, 10000, "Reserve CVector");
    Mem::PopMemProfile();
    Mem::Manager::Get()->PopContext();
    return true;
}

// 0x005A9700
bool RemoveTemporaryMemCardPools(CStruct*, CScript*)
{
    DeleteReservePool(ComponentPools());
    DeleteReservePool(StructPools());
    DeleteReservePool(VectorPools());
    return true;
}

} // namespace Mc

REPLACE(0x005A6EA0, Mc::DeleteMemCardFile);
REPLACE(0x005A8500, Mc::GetMemCardSpaceRequired);
REPLACE(0x005A8550, Mc::MemCardFileExists);
REPLACE(0x005A8BB0, Mc::ClearMemCardDataForUpload);
REPLACE(0x005A8DA0, Mc::CreateTemporaryMemCardPools);
REPLACE(0x005A9700, Mc::RemoveTemporaryMemCardPools);
