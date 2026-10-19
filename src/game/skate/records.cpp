// The records, 0x0057ECD0-0x0057FE27. See records.h.

#include "game/skate/records.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Records {

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
constexpr uint32_t kLongestGrind = 0x54C9597A;    // longestgrind
constexpr uint32_t kLongestLipTrick = 0x40590191; // longestliptrick
constexpr uint32_t kLongestManual = 0x82883251;   // longestmanual
constexpr uint32_t kGameRecordsArray = 0xE764EDF2; // (the game records' array, as written)
constexpr uint32_t kDefaultInitialsName = 0x24EB19A8; // (the default initials, as written)
// The type the records' structures are added with (11: not one of Script::ESymbolType's named ones).
constexpr uint32_t kStructureComponent = 11;

CGameRecords*& Instance() { return GameVariable<CGameRecords*>(0x007CE4BC); }
uint32_t& References() { return GameVariable<uint32_t>(0x007CE4C0); }

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
    auto* memory = static_cast<uint32_t*>(Mem::Calloc(4));
    if (memory)
        *memory = 0;
    return reinterpret_cast<char*>(memory);
}

Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}

// A record as a structure: value, number, initials.
Script::CStruct* RecordStructure(const CRecord* record)
{
    Script::CStruct* structure = NewStructure();
    structure->AddComponentOfType(kValue, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(record->m_value));
    structure->AddComponentOfType(kNumber, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(record->m_number));
    structure->AddString(kInitials, Script::ESYMBOLTYPE_STRING, record->m_initials);
    return structure;
}

// A new longest record (its value 0).
CRecord* NewLongestRecord()
{
    auto* record = static_cast<CRecord*>(Mem::Calloc(sizeof(CRecord)));
    if (record)
        record->Construct();
    record->m_value = 0;
    return record;
}

// A new table of `count` records.
CRecordTable* NewTable(int count)
{
    auto* table = static_cast<CRecordTable*>(Mem::Calloc(sizeof(CRecordTable)));
    return table ? table->Construct(count) : nullptr;
}

void DeleteRecord(CRecord* record)
{
    if (record) {
        Mem::Manager::Get()->Free(record->m_initials);
        Mem::Manager::Get()->Free(record);
    }
}

void DeleteTable(CRecordTable* table)
{
    if (table)
        table->Delete();
}

} // namespace

// 0x0057EF20
CRecord* CRecord::Construct()
{
    m_initials = NewInitials();
    CopyInitials(m_initials, "XXX");
    m_value = 1000;
    m_number = 0;
    return this;
}

// 0x0057ECD0
bool CRecord::MaybeNewRecord(int value, int number)
{
    if (value > m_value) {
        m_value = value;
        m_number = number;
        m_new = true;
        return true;
    }
    m_new = false;
    return false;
}

void CRecord::SetInitials(const char* initials) { CopyInitials(m_initials, initials); }

// 0x0057ED00
void CRecord::ReadFromStructure(Script::CStruct* params)
{
    params->GetInteger(kValue, &m_value, 0);
    if (!params->GetInteger(kNumber, &m_number, 0))
        m_number = 0;
    const char* initials = "XXX";
    params->GetString(kInitials, &initials, 0);
    SetInitials(initials);
}

// 0x0057EF60
CRecordTable* CRecordTable::Construct(int count)
{
    m_count = count;
    // (as new[]: the count before the records)
    auto* memory = static_cast<int*>(Mem::Calloc(count * sizeof(CRecord) + 4));
    if (!memory) {
        m_records = nullptr;
        return this;
    }
    *memory = count;
    auto* records = reinterpret_cast<CRecord*>(memory + 1);
    for (int i = 0; i < count; ++i)
        records[i].Construct();
    m_records = records;
    return this;
}

void CRecordTable::Delete()
{
    if (CRecord* records = m_records) {
        int* memory = reinterpret_cast<int*>(records) - 1;
        for (int i = *memory; i-- > 0;) // (as delete[]: the last first)
            Mem::Free(records[i].m_initials);
        Mem::Manager::Get()->Free(memory);
    }
    Mem::Manager::Get()->Free(this);
}

// 0x0057EFE0
int CRecordTable::MaybeNewEntry(int value, int number)
{
    for (int i = 0; i < m_count; ++i)
        m_records[i].m_new = false;
    int count = m_count;
    int index = 0;
    for (; index < count; ++index) {
        if (value > m_records[index].m_value)
            break;
    }
    if (index >= count)
        return -1;
    // The ones after it moved down (the last dropped).
    for (int i = count - 1; i > index; --i) {
        CopyInitials(m_records[i].m_initials, m_records[i - 1].m_initials);
        m_records[i].m_value = m_records[i - 1].m_value;
        m_records[i].m_number = m_records[i - 1].m_number;
    }
    m_records[index].m_value = value;
    m_records[index].m_number = number;
    CopyInitials(m_records[index].m_initials, "XXX");
    m_records[index].m_new = true;
    return index;
}

// 0x0057ED80
void CRecordTable::UpdateInitials(const char* initials)
{
    for (int i = 0; i < m_count; ++i) {
        if (m_records[i].m_new)
            m_records[i].SetInitials(initials);
    }
}

// 0x0057F0C0
void CRecordTable::ReadFromStructure(Script::CStruct* params)
{
    Script::CArray* array = nullptr;
    params->GetArray("RecordTable", &array, 0);
    if (m_count != static_cast<int>(array->m_size))
        return;
    for (int i = 0; i < m_count; ++i)
        m_records[i].ReadFromStructure(reinterpret_cast<Script::CStruct*>(array->GetElement(i)));
}

// 0x0057F4E0
void CRecordTable::WriteToStructure(Script::CStruct* params)
{
    Script::CArray* memory = Script::NewArray();
    Script::CArray* array = memory ? memory->Init() : nullptr;
    array->SetSizeAndType(m_count, Script::ESYMBOLTYPE_STRUCTURE);
    for (int i = 0; i < m_count; ++i)
        array->SetElement(i, reinterpret_cast<uint32_t>(RecordStructure(&m_records[i])));
    params->AddComponentOfType(kRecordTable, Script::ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(array));
}

// 0x0057F120
CLevelRecords* CLevelRecords::Construct()
{
    m_highScores = NewTable(5);
    m_bestCombos = NewTable(5);
    m_arcadeScores = NewTable(10);
    m_longestCombo = NewLongestRecord();
    m_longestGrind = NewLongestRecord();
    m_longestLipTrick = NewLongestRecord();
    m_longestManual = NewLongestRecord();
    return this;
}

// 0x0057F670
void CLevelRecords::DestroyBody()
{
    DeleteRecord(m_longestManual);
    DeleteRecord(m_longestLipTrick);
    DeleteRecord(m_longestGrind);
    DeleteRecord(m_longestCombo);
    DeleteTable(m_bestCombos);
    DeleteTable(m_highScores);
    DeleteTable(m_arcadeScores);
}

// 0x0057EDD0
void CLevelRecords::UpdateInitials(const char* initials)
{
    for (CRecord* record : {m_longestManual, m_longestLipTrick, m_longestGrind, m_longestCombo}) {
        if (record->m_new)
            record->SetInitials(initials);
    }
    m_bestCombos->UpdateInitials(initials);
    m_highScores->UpdateInitials(initials);
}

// 0x0057F340
void CLevelRecords::ReadFromStructure(Script::CStruct* params)
{
    // (one structure pointer for all: a missing one leaves the one before)
    Script::CStruct* structure = nullptr;
    params->GetStructure(kHighScores, &structure, 0);
    m_highScores->ReadFromStructure(structure);
    params->GetStructure(kBestCombos, &structure, 0);
    m_bestCombos->ReadFromStructure(structure);
    if (params->GetStructure(kArcadeScores, &structure, 0))
        m_arcadeScores->ReadFromStructure(structure);
    params->GetStructure(kLongestCombo, &structure, 0);
    m_longestCombo->ReadFromStructure(structure);
    params->GetStructure(kLongestGrind, &structure, 0);
    m_longestGrind->ReadFromStructure(structure);
    params->GetStructure(kLongestLipTrick, &structure, 0);
    m_longestLipTrick->ReadFromStructure(structure);
    params->GetStructure(kLongestManual, &structure, 0);
    m_longestManual->ReadFromStructure(structure);
}

// 0x0057F7B0
void CLevelRecords::WriteToStructure(Script::CStruct* params)
{
    Script::CStruct* structure = NewStructure();
    m_highScores->WriteToStructure(structure);
    params->AddComponentOfType(kHighScores, kStructureComponent, reinterpret_cast<uint32_t>(structure));
    structure = NewStructure();
    m_bestCombos->WriteToStructure(structure);
    params->AddComponentOfType(kBestCombos, kStructureComponent, reinterpret_cast<uint32_t>(structure));
    if (m_arcadeScores) {
        structure = NewStructure();
        m_arcadeScores->WriteToStructure(structure);
        params->AddComponentOfType(kArcadeScores, kStructureComponent, reinterpret_cast<uint32_t>(structure));
    }
    params->AddComponentOfType(kLongestCombo, kStructureComponent, reinterpret_cast<uint32_t>(RecordStructure(m_longestCombo)));
    params->AddComponentOfType(kLongestGrind, kStructureComponent, reinterpret_cast<uint32_t>(RecordStructure(m_longestGrind)));
    params->AddComponentOfType(kLongestLipTrick, kStructureComponent,
                               reinterpret_cast<uint32_t>(RecordStructure(m_longestLipTrick)));
    params->AddComponentOfType(kLongestManual, kStructureComponent,
                               reinterpret_cast<uint32_t>(RecordStructure(m_longestManual)));
}

// 0x0057EE70
CGameRecords* CGameRecords::Construct()
{
    m_levelCount = 0;
    m_defaultInitials = static_cast<char*>(Mem::Calloc(4));
    CopyInitials(m_defaultInitials, "ABC");
    return this;
}

// 0x0057EEA0 / 0x0057EEC0
void CGameRecords::SetDefaultInitials(const char* initials) { CopyInitials(m_defaultInitials, initials); }
CLevelRecords* CGameRecords::GetLevelRecords(int level) { return &m_levels[level]; }

// 0x0057FBC0
void CGameRecords::InitLevels(int count)
{
    m_levelCount = count;
    auto* memory = static_cast<int*>(Mem::Calloc(count * sizeof(CLevelRecords) + 4));
    if (!memory) {
        m_levels = nullptr;
        return;
    }
    *memory = count;
    auto* levels = reinterpret_cast<CLevelRecords*>(memory + 1);
    for (int i = 0; i < count; ++i)
        levels[i].Construct();
    m_levels = levels;
}

// 0x0057F440
void CGameRecords::ReadFromStructure(Script::CStruct* params)
{
    Script::CArray* array = nullptr;
    params->GetArray("GameRecords", &array, 0);
    if (m_levelCount != static_cast<int>(array->m_size))
        return;
    for (int i = 0; i < m_levelCount; ++i)
        m_levels[i].ReadFromStructure(reinterpret_cast<Script::CStruct*>(array->GetElement(i)));
    const char* initials = "ABC";
    params->GetString("DefaultInitials", &initials, 0);
    CopyInitials(m_defaultInitials, initials);
}

// 0x0057FC50
void CGameRecords::WriteToStructure(Script::CStruct* params)
{
    Script::CArray* memory = Script::NewArray();
    Script::CArray* array = memory ? memory->Init() : nullptr;
    array->SetSizeAndType(m_levelCount, Script::ESYMBOLTYPE_STRUCTURE);
    for (int i = 0; i < m_levelCount; ++i) {
        Script::CStruct* structure = NewStructure();
        m_levels[i].WriteToStructure(structure);
        array->SetElement(i, reinterpret_cast<uint32_t>(structure));
    }
    params->AddComponentOfType(kGameRecordsArray, Script::ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(array));
    params->AddString(kDefaultInitialsName, Script::ESYMBOLTYPE_STRING, m_defaultInitials);
}

// 0x0057EED0
CGameRecords* CGameRecords::Create(bool create)
{
    if (!Instance() && create) {
        void* memory = Mem::Calloc(sizeof(CGameRecords), nullptr, 1);
        Instance() = memory ? static_cast<CGameRecords*>(memory)->Construct() : nullptr;
    }
    ++References();
    return Instance();
}

// 0x0057FDB0
void CGameRecords::Release()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (CGameRecords* records = Instance()) {
        if (CLevelRecords* levels = records->m_levels) {
            int* memory = reinterpret_cast<int*>(levels) - 1;
            for (int i = *memory; i-- > 0;) // (as delete[]: the last first)
                levels[i].DestroyBody();
            Mem::Manager::Get()->Free(memory);
        }
        Mem::Manager::Get()->Free(records->m_defaultInitials);
        Mem::Manager::Get()->Free(records);
    }
    Instance() = nullptr;
    References() = 0;
}

} // namespace Records

REPLACE(0x0057ECD0, &Records::CRecord::MaybeNewRecord);
REPLACE(0x0057ED00, &Records::CRecord::ReadFromStructure);
REPLACE(0x0057ED80, &Records::CRecordTable::UpdateInitials);
REPLACE(0x0057EDD0, &Records::CLevelRecords::UpdateInitials);
REPLACE(0x0057EE70, &Records::CGameRecords::Construct);
REPLACE(0x0057EEA0, &Records::CGameRecords::SetDefaultInitials);
REPLACE(0x0057EEC0, &Records::CGameRecords::GetLevelRecords);
REPLACE(0x0057EED0, Records::CGameRecords::Create);
REPLACE(0x0057EF20, &Records::CRecord::Construct);
REPLACE(0x0057EF60, &Records::CRecordTable::Construct);
REPLACE(0x0057EFE0, &Records::CRecordTable::MaybeNewEntry);
REPLACE(0x0057F0C0, &Records::CRecordTable::ReadFromStructure);
REPLACE(0x0057F120, &Records::CLevelRecords::Construct);
REPLACE(0x0057F340, &Records::CLevelRecords::ReadFromStructure);
REPLACE(0x0057F440, &Records::CGameRecords::ReadFromStructure);
REPLACE(0x0057F4E0, &Records::CRecordTable::WriteToStructure);
REPLACE(0x0057F670, &Records::CLevelRecords::DestroyBody);
REPLACE(0x0057F7B0, &Records::CLevelRecords::WriteToStructure);
REPLACE(0x0057FBC0, &Records::CGameRecords::InitLevels);
REPLACE(0x0057FC50, &Records::CGameRecords::WriteToStructure);
REPLACE(0x0057FDB0, Records::CGameRecords::Release);
