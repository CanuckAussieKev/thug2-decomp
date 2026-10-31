// The records, 0x0057ECD0-0x0057FE27, and the script commands UpdateRecords (0x005AEBC0), UpdateArcadeScores, UpdateInitials
// and NewRecord. See highscores.h.

#include "game/skate/highscores.h"

#include <cstdint>
#include <initializer_list>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/level_editor_layout.h"
#include "game/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/skate_module.h"
#include "game/sys/run_log.h"

namespace Highscores {

Highscores::CBestResults* g_gameRecords;
uint32_t g_recordsReferences;

namespace {

// Names.
constexpr uint32_t kValue = 0xE288A7CB;           // value
constexpr uint32_t kNumber = 0x696FE0AB;          // number
constexpr uint32_t kInitials = 0xA226A990;        // initials
constexpr uint32_t kRecordTable = 0xB1449176;     // recordtable
constexpr uint32_t kHighScores = 0xFA60D92F;      // highscores
constexpr uint32_t kBestCombos = 0xD91F1F72;      // bestcombos
constexpr uint32_t kArcadeScores = 0xDBE683AF;    // arcadescores
constexpr uint32_t kLongestCombo = 0x3F544EA0;    // longestcombo
constexpr uint32_t kBestGrindKey = 0x54C9597A;    // longestgrind
constexpr uint32_t kLongestLipTrick = 0x40590191; // longestliptrick
constexpr uint32_t kLongestManual = 0x82883251;   // longestmanual
constexpr uint32_t kGameRecordsArray = 0xE764EDF2; // (the game records' array, as written)
constexpr uint32_t kDefaultInitialsName = 0x24EB19A8; // (the default initials, as written)
// The type the records' structures are added with (11: not one of Scripts::EQbValueKind's named ones).
constexpr uint32_t kStructureComponent = 11;

CBestResults*& Sole() { return g_gameRecords; }
uint32_t& RefTally() { return g_recordsReferences; }

void CopyInitials(char* to, const char* from)
{
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
    to[3] = 0;
}

// A 4-byte buffer for initials (zeroed if it was made).
char* NewInitials()
{
    auto* memory = static_cast<uint32_t*>(MemSys::AllocZeroed(4));
    if (memory)
        *memory = 0;
    return reinterpret_cast<char*>(memory);
}

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}

// A record as a structure: value, number, initials.
Scripts::CStruct* RecordStructure(const CBestEntry* record)
{
    Scripts::CStruct* structure = SpawnParamTable();
    structure->AddComponentOfType(kValue, Scripts::QBVAL_INTEGER, static_cast<uint32_t>(record->m_val));
    structure->AddComponentOfType(kNumber, Scripts::QBVAL_INTEGER, static_cast<uint32_t>(record->m_slotNo));
    structure->AppendWord(kInitials, Scripts::QBVAL_STRING, record->m_tag);
    return structure;
}

// A new longest record (its value 0).
CBestEntry* NewLongestRecord()
{
    auto* record = static_cast<CBestEntry*>(MemSys::AllocZeroed(sizeof(CBestEntry)));
    if (record)
        record->MakeNew();
    record->m_val = 0;
    return record;
}

// A new table of `count` records.
CBestEntryList* NewTable(int count)
{
    auto* table = static_cast<CBestEntryList*>(MemSys::AllocZeroed(sizeof(CBestEntryList)));
    return table ? table->MakeNew(count) : nullptr;
}

void DeleteRecord(CBestEntry* record)
{
    if (record) {
        MemSys::Hub::Pull()->Dealloc(record->m_tag);
        MemSys::Hub::Pull()->Dealloc(record);
    }
}

void DeleteTable(CBestEntryList* table)
{
    if (table)
        table->EraseOne();
}

} // namespace

// 0x0057EF20
CBestEntry* CBestEntry::MakeNew()
{
    m_tag = NewInitials();
    CopyInitials(m_tag, "XXX");
    m_val = 1000;
    m_slotNo = 0;
    return this;
}

// 0x0057ECD0
bool CBestEntry::TryBeatRecord(int value, int number)
{
    if (value > m_val) {
        m_val = value;
        m_slotNo = number;
        m_isFresh = true;
        return true;
    }
    m_isFresh = false;
    return false;
}

void CBestEntry::SetPlayerTag(const char* initials) { CopyInitials(m_tag, initials); }

// 0x0057ED00
void CBestEntry::DecodeFromStruct(Scripts::CStruct* params)
{
    params->FetchIntValue(kValue, &m_val, 0);
    if (!params->FetchIntValue(kNumber, &m_slotNo, 0))
        m_slotNo = 0;
    const char* initials = "XXX";
    params->FetchText(kInitials, &initials, 0);
    SetPlayerTag(initials);
}

// 0x0057EF60
CBestEntryList* CBestEntryList::MakeNew(int count)
{
    m_num = count;
    // (as new[]: the count before the records)
    auto* memory = static_cast<int*>(MemSys::AllocZeroed(count * sizeof(CBestEntry) + 4));
    if (!memory) {
        m_records = nullptr;
        return this;
    }
    *memory = count;
    auto* records = reinterpret_cast<CBestEntry*>(memory + 1);
    for (int i = 0; i < count; ++i)
        records[i].MakeNew();
    m_records = records;
    return this;
}

void CBestEntryList::EraseOne()
{
    if (CBestEntry* records = m_records) {
        int* memory = reinterpret_cast<int*>(records) - 1;
        for (int i = *memory; i-- > 0;) // (as delete[]: the last first)
            MemSys::Dealloc(records[i].m_tag);
        MemSys::Hub::Pull()->Dealloc(memory);
    }
    MemSys::Hub::Pull()->Dealloc(this);
}

// 0x0057EFE0
int CBestEntryList::TryInsertEntry(int value, int number)
{
    for (int i = 0; i < m_num; ++i)
        m_records[i].m_isFresh = false;
    int count = m_num;
    int index = 0;
    for (; index < count; ++index) {
        if (value > m_records[index].m_val)
            break;
    }
    if (index >= count)
        return -1;
    // The ones after it moved down (the last dropped).
    for (int i = count - 1; i > index; --i) {
        CopyInitials(m_records[i].m_tag, m_records[i - 1].m_tag);
        m_records[i].m_val = m_records[i - 1].m_val;
        m_records[i].m_slotNo = m_records[i - 1].m_slotNo;
    }
    m_records[index].m_val = value;
    m_records[index].m_slotNo = number;
    CopyInitials(m_records[index].m_tag, "XXX");
    m_records[index].m_isFresh = true;
    return index;
}

// 0x0057ED80
void CBestEntryList::UpdateInitials(const char* initials)
{
    for (int i = 0; i < m_num; ++i) {
        if (m_records[i].m_isFresh)
            m_records[i].SetPlayerTag(initials);
    }
}

// 0x0057F0C0
void CBestEntryList::DecodeFromStruct(Scripts::CStruct* params)
{
    Scripts::CArray* array = nullptr;
    params->FetchList("RecordTable", &array, 0);
    if (m_num != static_cast<int>(array->m_extent))
        return;
    for (int i = 0; i < m_num; ++i)
        m_records[i].DecodeFromStruct(reinterpret_cast<Scripts::CStruct*>(array->FetchPart(i)));
}

// 0x0057F4E0
void CBestEntryList::WriteToStructure(Scripts::CStruct* params)
{
    Scripts::CArray* memory = Scripts::NewArray();
    Scripts::CArray* array = memory ? memory->PrepareAll() : nullptr;
    array->ConfigureSizeKind(m_num, Scripts::QBVAL_STRUCT);
    for (int i = 0; i < m_num; ++i)
        array->SetElement(i, reinterpret_cast<uint32_t>(RecordStructure(&m_records[i])));
    params->AddComponentOfType(kRecordTable, Scripts::QBVAL_ARRAY, reinterpret_cast<uint32_t>(array));
}

// 0x0057F120
CStageBests* CStageBests::MakeNew()
{
    m_highScores = NewTable(5);
    m_bestCombos = NewTable(5);
    m_arcadeScores = NewTable(10);
    m_biggestChain = NewLongestRecord();
    m_grindRecord = NewLongestRecord();
    m_longestLipTrick = NewLongestRecord();
    m_longestManual = NewLongestRecord();
    return this;
}

// 0x0057F670
void CStageBests::DestroyBody()
{
    DeleteRecord(m_longestManual);
    DeleteRecord(m_longestLipTrick);
    DeleteRecord(m_grindRecord);
    DeleteRecord(m_biggestChain);
    DeleteTable(m_bestCombos);
    DeleteTable(m_highScores);
    DeleteTable(m_arcadeScores);
}

// 0x0057EDD0
void CStageBests::UpdateInitials(const char* initials)
{
    for (CBestEntry* record : {m_longestManual, m_longestLipTrick, m_grindRecord, m_biggestChain}) {
        if (record->m_isFresh)
            record->SetPlayerTag(initials);
    }
    m_bestCombos->UpdateInitials(initials);
    m_highScores->UpdateInitials(initials);
}

// 0x0057F340
void CStageBests::DecodeFromStruct(Scripts::CStruct* params)
{
    // (one structure pointer for all: a missing one leaves the one before)
    Scripts::CStruct* structure = nullptr;
    params->ReadSubStruct(kHighScores, &structure, 0);
    m_highScores->DecodeFromStruct(structure);
    params->ReadSubStruct(kBestCombos, &structure, 0);
    m_bestCombos->DecodeFromStruct(structure);
    if (params->ReadSubStruct(kArcadeScores, &structure, 0))
        m_arcadeScores->DecodeFromStruct(structure);
    params->ReadSubStruct(kLongestCombo, &structure, 0);
    m_biggestChain->DecodeFromStruct(structure);
    params->ReadSubStruct(kBestGrindKey, &structure, 0);
    m_grindRecord->DecodeFromStruct(structure);
    params->ReadSubStruct(kLongestLipTrick, &structure, 0);
    m_longestLipTrick->DecodeFromStruct(structure);
    params->ReadSubStruct(kLongestManual, &structure, 0);
    m_longestManual->DecodeFromStruct(structure);
}

// 0x0057F7B0
void CStageBests::WriteToStructure(Scripts::CStruct* params)
{
    Scripts::CStruct* structure = SpawnParamTable();
    m_highScores->WriteToStructure(structure);
    params->AddComponentOfType(kHighScores, kStructureComponent, reinterpret_cast<uint32_t>(structure));
    structure = SpawnParamTable();
    m_bestCombos->WriteToStructure(structure);
    params->AddComponentOfType(kBestCombos, kStructureComponent, reinterpret_cast<uint32_t>(structure));
    if (m_arcadeScores) {
        structure = SpawnParamTable();
        m_arcadeScores->WriteToStructure(structure);
        params->AddComponentOfType(kArcadeScores, kStructureComponent, reinterpret_cast<uint32_t>(structure));
    }
    params->AddComponentOfType(kLongestCombo, kStructureComponent, reinterpret_cast<uint32_t>(RecordStructure(m_biggestChain)));
    params->AddComponentOfType(kBestGrindKey, kStructureComponent, reinterpret_cast<uint32_t>(RecordStructure(m_grindRecord)));
    params->AddComponentOfType(kLongestLipTrick, kStructureComponent,
                               reinterpret_cast<uint32_t>(RecordStructure(m_longestLipTrick)));
    params->AddComponentOfType(kLongestManual, kStructureComponent,
                               reinterpret_cast<uint32_t>(RecordStructure(m_longestManual)));
}

// 0x0057EE70
CBestResults* CBestResults::MakeNew()
{
    m_levelCount = 0;
    m_defaultInitials = static_cast<char*>(MemSys::AllocZeroed(4));
    CopyInitials(m_defaultInitials, "ABC");
    return this;
}

// 0x0057EEA0 / 0x0057EEC0
void CBestResults::SetStockInitials(const char* initials) { CopyInitials(m_defaultInitials, initials); }
CStageBests* CBestResults::GetLevelRecords(int level) { return &m_levels[level]; }

// 0x0057FBC0
void CBestResults::InitLevels(int count)
{
    m_levelCount = count;
    auto* memory = static_cast<int*>(MemSys::AllocZeroed(count * sizeof(CStageBests) + 4));
    if (!memory) {
        m_levels = nullptr;
        return;
    }
    *memory = count;
    auto* levels = reinterpret_cast<CStageBests*>(memory + 1);
    for (int i = 0; i < count; ++i)
        levels[i].MakeNew();
    m_levels = levels;
}

// 0x0057F440
void CBestResults::DecodeFromStruct(Scripts::CStruct* params)
{
    Scripts::CArray* array = nullptr;
    params->FetchList("GameRecords", &array, 0);
    if (m_levelCount != static_cast<int>(array->m_extent))
        return;
    for (int i = 0; i < m_levelCount; ++i)
        m_levels[i].DecodeFromStruct(reinterpret_cast<Scripts::CStruct*>(array->FetchPart(i)));
    const char* initials = "ABC";
    params->FetchText("DefaultInitials", &initials, 0);
    CopyInitials(m_defaultInitials, initials);
}

// 0x0057FC50
void CBestResults::WriteToStructure(Scripts::CStruct* params)
{
    Scripts::CArray* memory = Scripts::NewArray();
    Scripts::CArray* array = memory ? memory->PrepareAll() : nullptr;
    array->ConfigureSizeKind(m_levelCount, Scripts::QBVAL_STRUCT);
    for (int i = 0; i < m_levelCount; ++i) {
        Scripts::CStruct* structure = SpawnParamTable();
        m_levels[i].WriteToStructure(structure);
        array->SetElement(i, reinterpret_cast<uint32_t>(structure));
    }
    params->AddComponentOfType(kGameRecordsArray, Scripts::QBVAL_ARRAY, reinterpret_cast<uint32_t>(array));
    params->AppendWord(kDefaultInitialsName, Scripts::QBVAL_STRING, m_defaultInitials);
}

// 0x0057EED0
CBestResults* CBestResults::Fabricate(bool create)
{
    if (!Sole() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CBestResults), nullptr, 1);
        Sole() = memory ? static_cast<CBestResults*>(memory)->MakeNew() : nullptr;
    }
    ++RefTally();
    return Sole();
}

// 0x0057FDB0
void CBestResults::Release()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (CBestResults* records = Sole()) {
        if (CStageBests* levels = records->m_levels) {
            int* memory = reinterpret_cast<int*>(levels) - 1;
            for (int i = *memory; i-- > 0;) // (as delete[]: the last first)
                levels[i].DestroyBody();
            MemSys::Hub::Pull()->Dealloc(memory);
        }
        MemSys::Hub::Pull()->Dealloc(records->m_defaultInitials);
        MemSys::Hub::Pull()->Dealloc(records);
    }
    Sole() = nullptr;
    RefTally() = 0;
}

bool UpdateRecords(bool editedCopy)
{
    uint8_t* skate = reinterpret_cast<uint8_t*>(GameModules::g_skateModule);
    if (editedCopy) {
        if (skate)
            skate[0x384] = 0; // (no record new: as 0x00566F40 sets it before comparing)
        Log("records: not updated - the level is the level editor's edited copy (nothing done in it reaches the "
            "records or the save)");
        return true;
    }
    reinterpret_cast<GameModules::Skate*>(skate)->UpdateRecords();
    return true;
}

// 0x005AEBC0
bool __cdecl CFunc_UpdateRecords(Scripts::CStruct*, Scripts::CScript*)
{
    return UpdateRecords(MenuSystem::LevelEditor::Layout::EditingCopy());
}

// 0x005AEBD0
bool __cdecl CFunc_UpdateArcadeScores(Scripts::CStruct* params, Scripts::CScript*)
{
    int level = 0; // (uninitialised in the original; always set below)
    if (!params->FetchIntValue("level", &level, 0))
        level = SkateCalls::Career()->FetchStage();
    Sole()->GetLevelRecords(level)->m_arcadeScores->DecodeFromStruct(params);
    return true;
}

// 0x005AFAF0
bool __cdecl CFunc_UpdateInitials(Scripts::CStruct*, Scripts::CScript*)
{
    int level = SkateCalls::Career()->FetchStage();
    CBestResults* records = Sole();
    records->GetLevelRecords(level)->UpdateInitials(records->m_defaultInitials);
    return true;
}

// 0x005AFB30
bool __cdecl CFunc_NewRecord(Scripts::CStruct*, Scripts::CScript*) { return SkateCalls::SkateField<bool>(0x384); }

} // namespace Records
