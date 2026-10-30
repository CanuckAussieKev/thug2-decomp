// Script functions (CFuncs) for the save files on the memory card (see save_files_cfuncs.h).

#include "game/mc/save_files_cfuncs.h"
#include "hook/game_code.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/stubs.h"
#include "game/goals/goal_manager.h"
#include "game/mc/memcard.h"
#include "game/mc/memcard_cfuncs.h"
#include "game/mc/save_contents.h"
#include "game/mc/save_types.h"
#include "game/script/struct_buffer.h"
#include "game/pcm/music.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/game_config.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mc {

namespace {

using Script::CScript;
using Script::CStruct;

int Platform() { return GameConfig::g_platform; }
Card* TheCard() { return g_theCard->GetCard(0, 0); }

// The calling script's own parameters (Script::CScript +0x14): where CFuncs leave results for it.
CStruct* ResultParams(CScript* script) { return *reinterpret_cast<CStruct**>(reinterpret_cast<char*>(script) + 0x14); }

// The console builds' save folder (0x005B5F10, no source yet).
const char* SaveFolder() { return GameConfig::GetPathPrefix(); }

// The card's delete of a console file or folder entry: on the PC the shared always-true method (0x005BC7F0).
bool DeleteCardFile(Card* card, const char* name) { return card->AlwaysTrue(reinterpret_cast<int>(name)); }

// A save file's handle deleted (its vtable's scalar deleting destructor).
void DeleteFile(File* file) { reinterpret_cast<void(__thiscall*)(File*, int)>(file->m_node.vtable[0])(file, 1); }

// The pool pairs the temporary pools belong to: components, structures, vectors.
Mem::PoolPair& ComponentPools() { return Script::g_componentPools; }
Mem::PoolPair& StructPools() { return Script::g_structPools; }
Mem::PoolPair& VectorPools() { return Script::g_vectorPools; }

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
    const char* userFileName = GAME_TEXT(0x006469B4, ""); // ""
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
    CStruct*& kept = g_keptParams;
    if (kept) {
        kept->Clear();
        Script::DeleteStruct(kept);
        kept = nullptr;
    }
    g_keptType = 0;
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

// 0x005A8BF0
bool GetSaveInfo(CStruct* params, CScript* script)
{
    uint32_t type = 0;
    params->GetChecksum(0x7321A8D6u, &type, 0); // type
    CStruct* contents = Script::NewStruct();
    if (contents)
        contents = contents->Init();
    WriteSaveContents(contents, type);
    int mainSize = static_cast<int>(Script::CalculateBufferSize(contents, 100000));
    CStruct* summary = Script::NewStruct();
    if (summary)
        summary = summary->Init();
    WriteSummary(contents, summary, type);
    int summarySize = static_cast<int>(Script::CalculateBufferSize(summary, 100000));
    CStruct* results = ResultParams(script);
    results->AddInteger(0x172C1344u, mainSize);                         // MainStructureSize
    results->AddInteger(0x96635475u, summarySize);                      // SummaryInfoSize
    results->AddInteger(0x6AFD2F7Fu, 100);                              // MaxSummaryInfoSize
    results->AddInteger(0xECBB8262u, mainSize + summarySize + 0x14);    // TotalSize
    results->AddInteger(0xB35EB1D1u, TypeFileSize(type));               // MaxTotalSize
    results->AddStructurePointer(0x703E60CAu, summary);                 // SummaryInfo
    results->AddStructurePointer(0x671C9C00u, contents);                // MainStructure
    return true;
}

// --- the directory listing and saving ----------------------------------------------------------------------

namespace {

constexpr int kMaxFiles = 75;
constexpr uint32_t kCorrupt = 0x492676A6;  // "corrupt"
constexpr uint32_t kFileName = 0xC3F4169A; // "FileName"

// A save file's header (0x14 bytes, then the summary and the contents).
struct SaveFileHeader {
    uint32_t checksum;    // of the whole file, this field 0
    uint32_t summaryCrc;  // of the summary
    int summarySize;
    int size;             // the header, the summary and the contents
    int version;
};
static_assert(sizeof(SaveFileHeader) == 0x14, "the summary follows at +0x14");

// The first 0x78 bytes of a listed file are read here (0x007CEC58): the header and the start of the summary.
struct ListedFile {
    SaveFileHeader header;
    uint8_t summary[0x78 - sizeof(SaveFileHeader)]; // (0x007CEC6C)
};
GAME_GLOBAL(ListedFile, g_listedFile, 0x007CEC58);
SaveFileHeader& ListedHeader() { return g_listedFile.header; }
const uint8_t* ListedSummary() { return g_listedFile.summary; }

// The i-th file of a list (its nodes from the head's next; each file is its own node).
File* ListEntry(Mem::ListNode* head, int index)
{
    Mem::ListNode* node = head->next;
    for (int i = 0; i < index; ++i)
        node = node->next;
    return static_cast<File*>(node->data);
}

// A save file's type from its name: on the consoles its letter after the folder, on the PC its extension.
uint32_t ListedFileType(const File* file, int folderLength)
{
    switch (Platform()) {
    case 1:
    case 2:
    case 3:
    case 5:
        return TypeFromLetter(file->m_name[folderLength + 7]);
    case 4:
    case 7:
    case 8: {
        // (the music and sounds kept going while the files are read)
        Pcm::Update();
        Sfx::UpdateVoiceStates();
        const char* extension = file->m_name + std::strlen(file->m_name) - 4;
        if (_stricmp(extension, ".NWS") == 0)
            return kSaveNetworkSettings;
        if (_stricmp(extension, ".SKA") == 0)
            return kSaveOptionsAndPros;
        if (_stricmp(extension, ".PRK") == 0)
            return kSavePark;
        if (_stricmp(extension, ".CAS") == 0)
            return kSaveCas;
        if (_stricmp(extension, ".CAT") == 0)
            return kSaveCat;
        if (_stricmp(extension, ".GOA") == 0)
            return kSaveCreatedGoals;
        if (_stricmp(extension, ".GRF") == 0)
            return kSaveCreatedGraphic;
        return 0;
    }
    default:
        return 0;
    }
}

// The entry marked corrupt, with the damaged-file text as its FileName.
void MarkCorrupt(CStruct* entry, uint32_t text)
{
    entry->AddChecksum(0u, kCorrupt);
    entry->AddString(kFileName, Script::GetString(text));
}

} // namespace

// 0x005A8F70
bool GetMemCardDirectoryListing(CStruct* params, CScript* script)
{
    constexpr uint32_t kDirectoryListing = 0xD8580FB5, kFilesLimitReached = 0x4EEC27B5, kTotalFiles = 0x56B7A7F6;
    ResultParams(script)->RemoveComponent(kDirectoryListing);
    ResultParams(script)->RemoveComponent(kFilesLimitReached);
    ResultParams(script)->RemoveComponent(kTotalFiles);
    uint32_t wantedType = 0;
    params->GetChecksum(0x11093FB5u, &wantedType, 0); // FileType
    Card* card = TheCard();
    if (!card)
        return true;

    // the card's files (a Lst::Head<File> on the stack, vtable 0x00658A60)
    Mem::ListNode head;
    head.vtable = Mc::kFileVtable;
    head.data = reinterpret_cast<void*>(-1);
    head.priority = 0;
    head.next = &head;
    head.prev = &head;
    card->GetFileList("*", reinterpret_cast<File*>(&head));

    // into the temporary component and structure pools, if they exist
    bool switched = false;
    if (ComponentPools().pools[ComponentPools().current + 1]) {
        ++ComponentPools().current;
        ++StructPools().current;
        switched = true;
    }
    int listed = 0;
    CStruct* entries[kMaxFiles];
    const char* folder = SaveFolder();
    int folderLength = static_cast<int>(std::strlen(folder));
    int fileCount = 0;
    for (Mem::ListNode* node = head.next; reinterpret_cast<intptr_t>(node->data) != -1 && node; node = node->next)
        ++fileCount;
    Dbg::Printf(GAME_TEXT(0x00657AA4, "ScriptGetMemCardDirectoryListing: DirList Start\n")); // "ScriptGetMemCardDirectoryListing: DirList Start\n"
    int gameFiles = 0;
    for (int i = 0; i < fileCount; ++i) {
        File* file = ListEntry(&head, i);
        if (std::memcmp(file->m_name, folder, static_cast<size_t>(folderLength)) != 0)
            continue;
        ++gameFiles;
        uint32_t type = ListedFileType(file, folderLength);
        if (wantedType && type != wantedType)
            continue;
        if (listed >= kMaxFiles)
            continue;
        CStruct* entry = Script::NewStruct();
        if (entry)
            entry = entry->Init();
        entry->AddChecksum(0xFADDC0B8u, type); // file_type
        entry->AddInteger(0x7F8C98FEu, listed); // index
        entry->AddInteger(0x447D8CC8u, file->m_year);   // year
        entry->AddInteger(0x7149EFF9u, file->m_month);  // month
        entry->AddInteger(0x1A5FD66Fu, file->m_day);    // day
        entry->AddInteger(0x8FE1EEB1u, file->m_hour);   // hour
        entry->AddInteger(0x5F94B55Cu, file->m_minute); // minutes
        entry->AddInteger(0xD029F619u, file->m_second); // seconds
        char path[100];
        if (Platform() == 5)
            std::snprintf(path, sizeof(path), "%s", file->m_name);
        else
            std::snprintf(path, sizeof(path), "Save/%s", file->m_name);
        entry->AddString(0x13C68F74u, path); // actual_file_name
        constexpr uint32_t kDamagedFile = 0x2C7CD632, kNgcDamagedFile = 0xFD4AA64E;
        File* opened = card->Open(path, 1, 0);
        if (!opened) {
            MarkCorrupt(entry, kDamagedFile);
        } else {
            opened->Seek(0, 0);
            opened->Read(&g_listedFile, 0x78);
            int summarySize = ListedHeader().summarySize;
            bool bad = summarySize > 100 ||
                       Crc::GenerateCRCFromBuffer(ListedSummary(), summarySize) != ListedHeader().summaryCrc;
            int size = static_cast<int>(opened->Seek(0, 2));
            entry->AddInteger(0x3D1BCFEDu, SaveBlocks(type, size)); // total_file_size
            if (bad || ListedHeader().version != TypeVersion(type)) {
                MarkCorrupt(entry, Platform() == 5 ? kNgcDamagedFile : kDamagedFile);
            } else {
                CStruct* summary = Script::NewStruct();
                if (summary)
                    summary = summary->Init();
                Script::ReadFromBuffer(summary, ListedSummary());
                entry->AppendStructureIfAny(summary);
                if (summary) {
                    summary->Clear();
                    Script::DeleteStruct(summary);
                }
            }
            opened->Close();
            DeleteFile(opened);
        }
        entries[listed++] = entry;
    }
    if (switched) {
        --ComponentPools().current;
        --StructPools().current;
    }
    // the list's nodes deleted (each file is its own node)
    for (Mem::ListNode* node = head.next; reinterpret_cast<intptr_t>(node->data) != -1;) {
        Mem::ListNode* next = node->next;
        Mem::ListNode* following = reinterpret_cast<intptr_t>(next->data) == -1 ? nullptr : next;
        reinterpret_cast<void(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1);
        node = following;
        if (!node)
            break;
    }
    ResultParams(script)->AddInteger(kTotalFiles, gameFiles);
    if (gameFiles >= kMaxFiles)
        ResultParams(script)->AddChecksum(0u, kFilesLimitReached);
    if (listed) {
        // newest first (a bubble sort by the dates)
        bool swapped;
        do {
            swapped = false;
            for (int j = 0; j < listed - 1; ++j) {
                if (IsLater(entries[j + 1], entries[j])) {
                    CStruct* later = entries[j + 1];
                    entries[j + 1] = entries[j];
                    entries[j] = later;
                    swapped = true;
                }
            }
        } while (swapped);
        Script::CArray* listing = Script::NewArray();
        if (listing)
            listing = listing->Init();
        listing->SetSizeAndType(static_cast<uint32_t>(listed), Script::ESYMBOLTYPE_STRUCTURE);
        for (int j = 0; j < listed; ++j)
            listing->SetElement(static_cast<uint32_t>(j), reinterpret_cast<uint32_t>(entries[j]));
        ResultParams(script)->AddArrayPointer(kDirectoryListing, listing);
    }
    // the head unlinked (its destructor's body)
    head.prev->next = head.next;
    head.next->prev = head.prev;
    return true;
}

// 0x005A7420: WriteToBuffer's callback while a save is written on the PC: the music and sounds kept going.
void __cdecl KeepSoundGoing()
{
    Pcm::Update();
    Sfx::UpdateVoiceStates();
}

// 0x005A97E0
bool SaveToMemoryCard(CStruct* params, CScript*)
{
    if (Platform() == 5)
        CreateTemporaryMemCardPools(nullptr, nullptr);
    if (Platform() != 4) {
        Pcm::PauseMusic(1);
        Pcm::PauseStream(1);
    }
    bool& outOfSpace = g_outOfSpace;
    outOfSpace = false;
    const char* name = GAME_TEXT(0x006469B4, ""); // ""
    params->GetString("Name", &name, 0);
    uint32_t type = 0;
    params->GetChecksum("Type", &type, 0);
    int vaultData = 0;
    params->GetInteger(0xBB609E73u, &vaultData, 0); // savevaultdata
    if (vaultData) {
        type = g_keptType;
    } else if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        Game::GetGoalManager()->LevelUnload();
    }
    // into the temporary pools, if they exist
    bool switched = false;
    if (ComponentPools().pools[ComponentPools().current + 1]) {
        ++ComponentPools().current;
        ++StructPools().current;
        ++VectorPools().current;
        switched = true;
    }
    CStruct* contents = Script::NewStruct();
    if (contents)
        contents = contents->Init();
    if (vaultData)
        contents->AppendStructureIfAny(g_keptParams);
    else
        WriteSaveContents(contents, type);
    CStruct* summary = Script::NewStruct();
    if (summary)
        summary = summary->Init();
    WriteSummary(contents, summary, type);
    summary->AddString("filename", name);

    File* file = nullptr;
    uint8_t* header = nullptr;
    uint8_t* padding = nullptr;
    bool saved = false;
    int fileSize = TypeFileSize(type);
    uint32_t mainSize = Script::CalculateBufferSize(contents, static_cast<uint32_t>(fileSize));
    uint32_t summarySize = Script::CalculateBufferSize(summary, 100);
    uint32_t total = mainSize + summarySize + sizeof(SaveFileHeader);
    uint32_t paddingSize = static_cast<uint32_t>(fileSize) - total;
    if (paddingSize != 0 && Platform() != 5) {
        padding = static_cast<uint8_t*>(Mem::Malloc(paddingSize));
        if (paddingSize > 0)
            std::memset(padding, 0x69, paddingSize);
    }
    Card* card = TheCard();
    // (the second test is a card method that's the shared always-true stub, 0x005B9760)
    if (card && Stubs::ReturnTrue()) {
        char path[104];
        bool named = true;
        switch (Platform()) {
        case 5: {
            char folderName[104];
            MakeFolderFileName(type, name, folderName);
            std::snprintf(path, sizeof(path), "%s%s", folderName, folderName);
            break;
        }
        case 4:
        case 7: {
            path[0] = '\0';
            const char* typeName = TypeFileName(type, name);
            char* out = path;
            while ((*out++ = *typeName++) != '\0') {
            }
            break;
        }
        default:
            named = false;
            break;
        }
        if (named) {
            file = card->Open(path, 6, fileSize);
            outOfSpace = card->m_error == 2;
        }
        if (file) {
            header = static_cast<uint8_t*>(Mem::Malloc(total));
            std::memset(header, 0, sizeof(SaveFileHeader));
            if (Platform() != 5) {
                auto* fileHeader = reinterpret_cast<SaveFileHeader*>(header);
                fileHeader->version = TypeVersion(type);
                fileHeader->size = static_cast<int>(total);
                fileHeader->summarySize = static_cast<int>(summarySize);
                uint8_t* data = header + sizeof(SaveFileHeader);
                bool pc = Platform() == 4 || Platform() == 7;
                Script::WriteToBuffer(summary, data, summarySize, 0, pc ? KeepSoundGoing : nullptr);
                fileHeader->summaryCrc = Crc::GenerateCRCFromBuffer(data, static_cast<int>(summarySize));
                data += summarySize;
                Script::WriteToBuffer(contents, data, mainSize, 0, nullptr);
                if (type == kSaveNetworkSettings) {
                    for (uint32_t i = 0; i < mainSize; ++i)
                        data[i] += 0x69; // (read back with 0x69 taken off)
                }
                fileHeader->checksum = Crc::GenerateCRCFromBuffer(header, static_cast<int>(total));
                int written = file->Write(header, static_cast<int>(total));
                outOfSpace = card->m_error == 2;
                if (written == static_cast<int>(total)) {
                    if (Platform() == 5 || paddingSize == 0) {
                        saved = true;
                    } else {
                        written = file->Write(padding, static_cast<int>(paddingSize));
                        outOfSpace = card->m_error == 2;
                        saved = written == static_cast<int>(paddingSize);
                    }
                }
            }
        }
    }
    if (padding)
        Mem::FreeMalloc(padding);
    if (header)
        Mem::FreeMalloc(header);
    if (file) {
        if (!file->Flush())
            saved = false;
        if (!file->Close())
            saved = false;
        DeleteFile(file);
    }
    if (summary) {
        summary->Clear();
        Script::DeleteStruct(summary);
    }
    if (contents) {
        contents->Clear();
        Script::DeleteStruct(contents);
    }
    if (switched) {
        --ComponentPools().current;
        --StructPools().current;
        --VectorPools().current;
    }
    if (Platform() != 4) {
        Pcm::PauseMusic(-1);
        Pcm::PauseStream(-1);
    }
    if (Platform() == 5)
        RemoveTemporaryMemCardPools(nullptr, nullptr);
    return saved;
}

} // namespace Mc

REPLACE(0x005A8F70, Mc::GetMemCardDirectoryListing);
REPLACE(0x005A7420, Mc::KeepSoundGoing);
REPLACE(0x005A97E0, Mc::SaveToMemoryCard);
REPLACE(0x005A8BF0, Mc::GetSaveInfo);
REPLACE(0x005A6EA0, Mc::DeleteMemCardFile);
REPLACE(0x005A8500, Mc::GetMemCardSpaceRequired);
REPLACE(0x005A8550, Mc::MemCardFileExists);
REPLACE(0x005A8BB0, Mc::ClearMemCardDataForUpload);
REPLACE(0x005A8DA0, Mc::CreateTemporaryMemCardPools);
REPLACE(0x005A9700, Mc::RemoveTemporaryMemCardPools);
