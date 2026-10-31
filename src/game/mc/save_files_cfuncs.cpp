// Script functions (ScriptCommands) for the save files on the memory card (see save_files_cfuncs.h).

#include "game/mc/save_files_cfuncs.h"

#include <cstddef>
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

namespace MemCard {

namespace {

using Scripts::CScript;
using Scripts::CStruct;

int OsLayer() { return GameConfig::g_platform; }
SaveCard* TheCard() { return g_theCard->GetStorageSlot(0, 0); }

// The calling script's own parameters (Scripts::CScript +0x14): where ScriptCommands leave results for it.
CStruct* ResultParams(CScript* script) { return *reinterpret_cast<CStruct**>(reinterpret_cast<char*>(script) + 0x14); }

// The console builds' save folder (0x005B5F10, no source yet).
const char* SaveFolder() { return GameConfig::GetPathPrefix(); }

// The card's delete of a console file or folder entry: on the PC the shared always-true method (0x005BC7F0).
bool DeleteCardFile(SaveCard* card, const char* name) { return card->AlwaysTrue(reinterpret_cast<int>(name)); }

// A save file's handle deleted (its vtable's scalar deleting destructor).
void DeleteFile(FileLoad* file) { reinterpret_cast<void(__thiscall*)(FileLoad*, int)>(file->m_listLink.vtable[0])(file, 1); }

// The pool pairs the temporary pools belong to: components, structures, vectors.
MemSys::PoolPair& ComponentPools() { return Scripts::g_componentPools; }
MemSys::PoolPair& StructPools() { return Scripts::g_structPools; }
MemSys::PoolPair& VectorPools() { return Scripts::g_vectorPools; }

// A pair's second pool made (as the pair's own creator, 0x0046F110 / 0x0046F190 / 0x0046F210, does).
void CreateReservePool(MemSys::PoolPair& pools, int itemSize, int count, const char* name)
{
    ++pools.current;
    Scripts::CreatePool(pools, itemSize, count, name);
    --pools.current;
}
// A pair's second pool deleted: its block and itself, if the pair made it.
void DeleteReservePool(MemSys::PoolPair& pools)
{
    ++pools.current;
    if (MemSys::SlabBank* pool = pools.pools[pools.current]) {
        if (reinterpret_cast<uint8_t*>(&pools.unknown4)[pools.current]) {
            MemSys::Hub::Pull()->Dealloc(pool->m_outset); // (its destructor body, 0x005BA4E0)
            MemSys::Hub::Pull()->Dealloc(pool);
        }
        pools.pools[pools.current] = nullptr;
    }
    --pools.current;
}

} // namespace

// 0x005A6EA0
bool DeleteMemCardFile(CStruct* params, CScript*)
{
    if (OsLayer() != 4) {
        AudioStream::PauseMusic(1);
        AudioStream::FreezeStream(1);
    }
    SaveCard* card = TheCard();
    if (!card) {
        if (OsLayer() != 4) {
            AudioStream::PauseMusic(-1);
            AudioStream::FreezeStream(-1);
        }
        return false;
    }
    bool deleted = true;
    const char* xboxDirectory = nullptr; // (read, not used on the PC)
    params->FetchText("XBoxDirectoryName", &xboxDirectory, 0);
    const char* cardFileName = nullptr;
    params->FetchText("CardFileName", &cardFileName, 0);
    uint32_t type = 0;
    params->ReadHashValue("Type", &type, 0);
    const char* userFileName = ""; // ""
    params->FetchText("UserFileName", &userFileName, 0);

    bool removed = false;
    char file[20];
    switch (OsLayer()) {
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
    if (OsLayer() != 4) {
        AudioStream::PauseMusic(-1);
        AudioStream::FreezeStream(-1);
    }
    return deleted;
}

// 0x005A8500
bool GetMemCardSpaceRequired(CStruct* params, CScript* script)
{
    uint32_t type = 0;
    params->ReadHashValue(0u, &type, 0);
    int size = TypeFileSize(type);
    ResultParams(script)->PutInt(0xEB3860C3u, SaveBlocks(type, size)); // SpaceRequired
    return true;
}

// 0x005A8550
bool MemCardFileExists(CStruct* params, CScript* script)
{
    SaveCard* card = TheCard();
    if (!card)
        return false;
    const char* name = nullptr;
    params->FetchText("Name", &name, 0);
    uint32_t type = 0;
    params->ReadHashValue("Type", &type, 0);
    if (!name || !type)
        return false;
    const char* typeFileName = nullptr; // (only made on the PC and platform 7)
    char path[100];
    path[0] = '\0';
    switch (OsLayer()) {
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
    FileLoad* file = card->Unseal(path, 1, 0);
    if (!file) {
        // (on the PC, the file that wouldn't open is removed - by the type's name made above)
        if (OsLayer() == 4)
            card->RemoveFile(typeFileName);
        return false;
    }
    int size = static_cast<int>(file->SeekTo(0, 2));
    ResultParams(script)->PutInt("total_file_size", SaveBlocks(type, size));
    file->Drain();
    file->Shut();
    DeleteFile(file);
    return true;
}

// 0x005A8BB0
bool ClearMemCardDataForUpload(CStruct*, CScript*)
{
    CStruct*& kept = g_keptParams;
    if (kept) {
        kept->Clear();
        Scripts::DeleteStruct(kept);
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
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    MemSys::PushMemProfile("Mem card CComponent");
    CreateReservePool(ComponentPools(), 0x10, 50000, "Reserve CComponent");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("Mem card CStruct");
    CreateReservePool(StructPools(), 0x08, 50000, "Reserve CStruct");
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("Mem card CVector");
    CreateReservePool(VectorPools(), 0x10, 10000, "Reserve CVector");
    MemSys::PopMemProfile();
    MemSys::Hub::Pull()->LeaveHeapScope();
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
    params->ReadHashValue(0x7321A8D6u, &type, 0); // type
    CStruct* contents = Scripts::NewStruct();
    if (contents)
        contents = contents->PrepareAll();
    WriteSaveContents(contents, type);
    int mainSize = static_cast<int>(Scripts::MeasureBufferBytes(contents, 100000));
    CStruct* summary = Scripts::NewStruct();
    if (summary)
        summary = summary->PrepareAll();
    WriteSummary(contents, summary, type);
    int summarySize = static_cast<int>(Scripts::MeasureBufferBytes(summary, 100000));
    CStruct* results = ResultParams(script);
    results->PutInt(0x172C1344u, mainSize);                         // MainStructureSize
    results->PutInt(0x96635475u, summarySize);                      // SummaryInfoSize
    results->PutInt(0x6AFD2F7Fu, 100);                              // MaxSummaryInfoSize
    results->PutInt(0xECBB8262u, mainSize + summarySize + 0x14);    // TotalSize
    results->PutInt(0xB35EB1D1u, TypeFileSize(type));               // MaxTotalSize
    results->AppendStructRef(0x703E60CAu, summary);                 // SummaryInfo
    results->AppendStructRef(0x671C9C00u, contents);                // MainStructure
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
ListedFile g_listedFile;
SaveFileHeader& ListedHeader() { return g_listedFile.header; }
const uint8_t* ListedSummary() { return g_listedFile.summary; }

// The i-th file of a list (its nodes from the head's next; each file is its own node).
FileLoad* ListEntry(MemSys::ListNode* head, int index)
{
    MemSys::ListNode* node = head->next;
    for (int i = 0; i < index; ++i)
        node = node->next;
    return static_cast<FileLoad*>(node->data);
}

// A save file's type from its name: on the consoles its letter after the folder, on the PC its extension.
uint32_t ListedFileType(const FileLoad* file, int folderLength)
{
    switch (OsLayer()) {
    case 1:
    case 2:
    case 3:
    case 5:
        return TypeFromLetter(file->m_label[folderLength + 7]);
    case 4:
    case 7:
    case 8: {
        // (the music and sounds kept going while the files are read)
        AudioStream::TickFrame();
        SoundFx::UpdateVoiceStates();
        const char* extension = file->m_label + std::strlen(file->m_label) - 4;
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
    entry->PutNameKey(0u, kCorrupt);
    entry->AppendWord(kFileName, Scripts::FetchText(text));
}

} // namespace

// 0x005A8F70
bool GetMemCardDirectoryListing(CStruct* params, CScript* script)
{
    constexpr uint32_t kDirectoryListing = 0xD8580FB5, kFilesLimitReached = 0x4EEC27B5, kTotalFiles = 0x56B7A7F6;
    ResultParams(script)->EraseModule(kDirectoryListing);
    ResultParams(script)->EraseModule(kFilesLimitReached);
    ResultParams(script)->EraseModule(kTotalFiles);
    uint32_t wantedType = 0;
    params->ReadHashValue(0x11093FB5u, &wantedType, 0); // FileType
    SaveCard* card = TheCard();
    if (!card)
        return true;

    // the card's files (a ListKit::Foremost<File> on the stack, vtable 0x00658A60)
    MemSys::ListNode head;
    head.vtable = MemCard::kFileVtable;
    head.data = reinterpret_cast<void*>(-1);
    head.priority = 0;
    head.next = &head;
    head.prev = &head;
    card->ListSaveFiles("*", reinterpret_cast<FileLoad*>(&head));

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
    for (MemSys::ListNode* node = head.next; reinterpret_cast<intptr_t>(node->data) != -1 && node; node = node->next)
        ++fileCount;
    DebugOut::Printf("ScriptGetMemCardDirectoryListing: DirList Start\n"); // "ScriptGetMemCardDirectoryListing: DirList Start\n"
    int gameFiles = 0;
    for (int i = 0; i < fileCount; ++i) {
        FileLoad* file = ListEntry(&head, i);
        if (std::memcmp(file->m_label, folder, static_cast<size_t>(folderLength)) != 0)
            continue;
        ++gameFiles;
        uint32_t type = ListedFileType(file, folderLength);
        if (wantedType && type != wantedType)
            continue;
        if (listed >= kMaxFiles)
            continue;
        CStruct* entry = Scripts::NewStruct();
        if (entry)
            entry = entry->PrepareAll();
        entry->PutNameKey(0xFADDC0B8u, type); // file_type
        entry->PutInt(0x7F8C98FEu, listed); // index
        entry->PutInt(0x447D8CC8u, file->m_year);   // year
        entry->PutInt(0x7149EFF9u, file->m_month);  // month
        entry->PutInt(0x1A5FD66Fu, file->m_day);    // day
        entry->PutInt(0x8FE1EEB1u, file->m_hour);   // hour
        entry->PutInt(0x5F94B55Cu, file->m_minute); // minutes
        entry->PutInt(0xD029F619u, file->m_second); // seconds
        char path[100];
        if (OsLayer() == 5)
            std::snprintf(path, sizeof(path), "%s", file->m_label);
        else
            std::snprintf(path, sizeof(path), "Save/%s", file->m_label);
        entry->AppendWord(0x13C68F74u, path); // actual_file_name
        constexpr uint32_t kDamagedFile = 0x2C7CD632, kNgcDamagedFile = 0xFD4AA64E;
        FileLoad* opened = card->Unseal(path, 1, 0);
        if (!opened) {
            MarkCorrupt(entry, kDamagedFile);
        } else {
            opened->SeekTo(0, 0);
            opened->TakeIn(&g_listedFile, 0x78);
            int summarySize = ListedHeader().summarySize;
            bool bad = summarySize > 100 ||
                       Checksums::GenerateCRCFromBuffer(ListedSummary(), summarySize) != ListedHeader().summaryCrc;
            int size = static_cast<int>(opened->SeekTo(0, 2));
            entry->PutInt(0x3D1BCFEDu, SaveBlocks(type, size)); // total_file_size
            if (bad || ListedHeader().version != TypeVersion(type)) {
                MarkCorrupt(entry, OsLayer() == 5 ? kNgcDamagedFile : kDamagedFile);
            } else {
                CStruct* summary = Scripts::NewStruct();
                if (summary)
                    summary = summary->PrepareAll();
                Scripts::DecodeFromBytes(summary, ListedSummary());
                entry->AppendStructureIfAny(summary);
                if (summary) {
                    summary->Clear();
                    Scripts::DeleteStruct(summary);
                }
            }
            opened->Shut();
            DeleteFile(opened);
        }
        entries[listed++] = entry;
    }
    if (switched) {
        --ComponentPools().current;
        --StructPools().current;
    }
    // the list's nodes deleted (each file is its own node)
    for (MemSys::ListNode* node = head.next; reinterpret_cast<intptr_t>(node->data) != -1;) {
        MemSys::ListNode* next = node->next;
        MemSys::ListNode* following = reinterpret_cast<intptr_t>(next->data) == -1 ? nullptr : next;
        reinterpret_cast<void(__thiscall*)(MemSys::ListNode*, int)>(node->vtable[0])(node, 1);
        node = following;
        if (!node)
            break;
    }
    ResultParams(script)->PutInt(kTotalFiles, gameFiles);
    if (gameFiles >= kMaxFiles)
        ResultParams(script)->PutNameKey(0u, kFilesLimitReached);
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
        Scripts::CArray* listing = Scripts::NewArray();
        if (listing)
            listing = listing->PrepareAll();
        listing->ConfigureSizeKind(static_cast<uint32_t>(listed), Scripts::QBVAL_STRUCT);
        for (int j = 0; j < listed; ++j)
            listing->SetElement(static_cast<uint32_t>(j), reinterpret_cast<uint32_t>(entries[j]));
        ResultParams(script)->AppendArrayRef(kDirectoryListing, listing);
    }
    // the head unlinked (its destructor's body)
    head.prev->next = head.next;
    head.next->prev = head.prev;
    return true;
}

// 0x005A7420: EmitIntoBuffer's callback while a save is written on the PC: the music and sounds kept going.
void __cdecl KeepSoundGoing()
{
    AudioStream::TickFrame();
    SoundFx::UpdateVoiceStates();
}

// 0x005A97E0
bool SaveToMemoryCard(CStruct* params, CScript*)
{
    if (OsLayer() == 5)
        CreateTemporaryMemCardPools(nullptr, nullptr);
    if (OsLayer() != 4) {
        AudioStream::PauseMusic(1);
        AudioStream::FreezeStream(1);
    }
    bool& outOfSpace = g_outOfSpace;
    outOfSpace = false;
    const char* name = ""; // ""
    params->FetchText("Name", &name, 0);
    uint32_t type = 0;
    params->ReadHashValue("Type", &type, 0);
    int vaultData = 0;
    params->FetchIntValue(0xBB609E73u, &vaultData, 0); // savevaultdata
    if (vaultData) {
        type = g_keptType;
    } else if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        GoalSystem::GetObjectiveBoard()->LevelUnload();
    }
    // into the temporary pools, if they exist
    bool switched = false;
    if (ComponentPools().pools[ComponentPools().current + 1]) {
        ++ComponentPools().current;
        ++StructPools().current;
        ++VectorPools().current;
        switched = true;
    }
    CStruct* contents = Scripts::NewStruct();
    if (contents)
        contents = contents->PrepareAll();
    if (vaultData)
        contents->AppendStructureIfAny(g_keptParams);
    else
        WriteSaveContents(contents, type);
    CStruct* summary = Scripts::NewStruct();
    if (summary)
        summary = summary->PrepareAll();
    WriteSummary(contents, summary, type);
    summary->AppendWord("filename", name);

    FileLoad* file = nullptr;
    uint8_t* header = nullptr;
    uint8_t* padding = nullptr;
    bool saved = false;
    int fileSize = TypeFileSize(type);
    uint32_t mainSize = Scripts::MeasureBufferBytes(contents, static_cast<uint32_t>(fileSize));
    uint32_t summarySize = Scripts::MeasureBufferBytes(summary, 100);
    uint32_t total = mainSize + summarySize + sizeof(SaveFileHeader);
    uint32_t paddingSize = static_cast<uint32_t>(fileSize) - total;
    if (paddingSize != 0 && OsLayer() != 5) {
        padding = static_cast<uint8_t*>(MemSys::AllocBlock(paddingSize));
        if (paddingSize > 0)
            std::memset(padding, 0x69, paddingSize);
    }
    SaveCard* card = TheCard();
    // (the second test is a card method that's the shared always-true stub, 0x005B9760)
    if (card && Stubs::ReturnTrue()) {
        char path[104];
        bool named = true;
        switch (OsLayer()) {
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
            file = card->Unseal(path, 6, fileSize);
            outOfSpace = card->m_fault == 2;
        }
        if (file) {
            header = static_cast<uint8_t*>(MemSys::AllocBlock(total));
            std::memset(header, 0, sizeof(SaveFileHeader));
            if (OsLayer() != 5) {
                auto* fileHeader = reinterpret_cast<SaveFileHeader*>(header);
                fileHeader->version = TypeVersion(type);
                fileHeader->size = static_cast<int>(total);
                fileHeader->summarySize = static_cast<int>(summarySize);
                uint8_t* data = header + sizeof(SaveFileHeader);
                bool pc = OsLayer() == 4 || OsLayer() == 7;
                Scripts::EmitIntoBuffer(summary, data, summarySize, 0, pc ? KeepSoundGoing : nullptr);
                fileHeader->summaryCrc = Checksums::GenerateCRCFromBuffer(data, static_cast<int>(summarySize));
                data += summarySize;
                Scripts::EmitIntoBuffer(contents, data, mainSize, 0, nullptr);
                if (type == kSaveNetworkSettings) {
                    for (uint32_t i = 0; i < mainSize; ++i)
                        data[i] += 0x69; // (read back with 0x69 taken off)
                }
                fileHeader->checksum = Checksums::GenerateCRCFromBuffer(header, static_cast<int>(total));
                int written = file->EmitOut(header, static_cast<int>(total));
                outOfSpace = card->m_fault == 2;
                if (written == static_cast<int>(total)) {
                    if (OsLayer() == 5 || paddingSize == 0) {
                        saved = true;
                    } else {
                        written = file->EmitOut(padding, static_cast<int>(paddingSize));
                        outOfSpace = card->m_fault == 2;
                        saved = written == static_cast<int>(paddingSize);
                    }
                }
            }
        }
    }
    if (padding)
        MemSys::FreeMalloc(padding);
    if (header)
        MemSys::FreeMalloc(header);
    if (file) {
        if (!file->Drain())
            saved = false;
        if (!file->Shut())
            saved = false;
        DeleteFile(file);
    }
    if (summary) {
        summary->Clear();
        Scripts::DeleteStruct(summary);
    }
    if (contents) {
        contents->Clear();
        Scripts::DeleteStruct(contents);
    }
    if (switched) {
        --ComponentPools().current;
        --StructPools().current;
        --VectorPools().current;
    }
    if (OsLayer() != 4) {
        AudioStream::PauseMusic(-1);
        AudioStream::FreezeStream(-1);
    }
    if (OsLayer() == 5)
        RemoveTemporaryMemCardPools(nullptr, nullptr);
    return saved;
}

} // namespace Mc
