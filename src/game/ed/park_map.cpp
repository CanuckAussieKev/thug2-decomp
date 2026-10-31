// The park manager's compressed map (ParkEditing::CParkLayout): the park read from a script structure, a file or the
// map itself, and a new empty park.

#include "game/ed/park.h"
#include "game/ed/park_editor.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/ed/rail_editor.h"
#include "game/ed/goal_editor.h"

namespace ParkEditing {

bool g_railEditorFlag{true};

namespace {

constexpr uint16_t kMapVersion = 0x4E24;
constexpr uint32_t kParkEditorGoals = 0xD8EB825E; // "Park_editor_goals"
constexpr uint32_t kMaxPlayers = 0xB7E39B53;      // "MaxPlayers"
constexpr uint32_t kPremadeParksInfo = 0x9FAE26EE; // "premade_parks_info"
constexpr uint32_t kSlot = 0x53F1DF98;            // "slot"
constexpr uint32_t kName = 0xA1DC81F9;            // "name"
constexpr uint32_t kEdSaveMap = 0xBC29F55A;       // "Ed_Save_Map": the metapieces' names by saved index
constexpr uint32_t kDeadPiece = 0xA0560CBD;       // "Sk6Ed_dead": a saved piece that's gone
constexpr uint32_t kOldDeadPiece = 0xE2730BC0;    // the older parks' dead piece
constexpr uint32_t kEditLevel = 0x7ECA21E5;       // "edit": the park editor's level

// The goal editor (0x004E8470 looks it up): its goals cleared (0x004E8280), read from a structure (0x004EA470).
void* GoalEditor() { return CObjectiveAuthoring::Get(); }
void ClearGoals(void* goalEditor) { static_cast<CObjectiveAuthoring*>(goalEditor)->ClearParkGoals(); }
void ReadGoals(void* goalEditor, Scripts::CStruct* goals, int flag)
{
    static_cast<CObjectiveAuthoring*>(goalEditor)->ReadGoals(goals, flag);
}
// The rail editor (0x004F4970 looks it up): its rails read from a structure (0x004F7ED0).
void ReadRails(Scripts::CStruct* params)
{
    CRailAuthoring::Instance()->DecodeFromStruct(params);
}
// A flag the rail editor reads (cleared while rails are read outside the park editor's level).
bool& RailEditorFlag() { return g_railEditorFlag; }

// The park editor's theme.
void SetTheme(uint32_t theme) { CParkBuilderMode::Instance()->SetDaylightScript(theme); }

// The script structure written to a buffer and read back (0x0047A070 sizes it, 0x00479E20 writes it,
// 0x0047A0C0 reads it).
uint32_t StructureBufferSize(Scripts::CStruct* structure, uint32_t maximum)
{
    return Scripts::MeasureBufferBytes(structure, maximum);
}
void WriteStructure(Scripts::CStruct* structure, void* buffer, uint32_t size, int a, int b)
{
    Scripts::EmitIntoBuffer(structure, static_cast<uint8_t*>(buffer), size, a, reinterpret_cast<void (*)()>(b));
}
void ReadStructure(Scripts::CStruct* structure, void* buffer)
{
    Scripts::DecodeFromBytes(structure, static_cast<const uint8_t*>(buffer));
}

// strcpy (inline in the original: no limit).
void CopyString(char* dest, const char* source)
{
    while ((*dest++ = *source++) != '\0') {
    }
}

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

} // namespace

// 0x00598FC0
void CParkLayout::DecodeFromStruct(Scripts::CStruct* params, bool applyToEditor)
{
    void* goalEditor = GoalEditor();
    ClearGoals(goalEditor);
    Scripts::CStruct* goals = nullptr;
    if (params->ReadSubStruct(kParkEditorGoals, &goals, 0))
        ReadGoals(goalEditor, goals, 1);

    CParkBuilderMode* editor = CParkBuilderMode::Create(false);
    if (editor->m_state != kEditLevel)
        RailEditorFlag() = false;
    ReadRails(params);
    RailEditorFlag() = true;

    int maxPlayers = 2;
    params->FetchIntValue(kMaxPlayers, &maxPlayers, 0);
    Instance()->m_builder->SetPlayerCap(maxPlayers);

    uint16_t mapSize = (m_flags & kMapLoaded) ? m_map.m_shellSize : 0;
    if (m_parkBuilt)
        WriteCompressedMap();
    Scripts::CArray* map = nullptr;
    if (params->GetArray("Park_editor_map", &map, 1)) {
        for (int i = 0; i < static_cast<int>(sizeof(CompressedMap) / 4); ++i)
            reinterpret_cast<uint32_t*>(&m_map)[i] = map->GetElement(i);
        if (applyToEditor) {
            CParkBuilderMode::Create(false)->ApplyMap(&m_map, mapSize);
            CParkBuilderMode::Release();
        }
    }
    CParkBuilderMode::Release();
}

// 0x00599140
void CParkLayout::TransferParkFile(bool save, int slot)
{
    char name[64];
    std::snprintf(name, sizeof(name), "CustomParks\\custom%d.prk", slot);
    if (save) {
        Scripts::CStruct* params = NewStructure();
        ExportToStruct(params);
        uint32_t size = StructureBufferSize(params, 100000);
        void* buffer = MemSys::AllocBlock(size);
        WriteStructure(params, buffer, size, 1, 0);
        FileLoad::Handle file = FileLoad::Open(name, "wb");
        FileLoad::Write(buffer, static_cast<int>(size), 1, file);
        FileLoad::Close(file);
        MemSys::FreeMalloc(buffer);
        DeleteStructure(params);
        return;
    }

    FileLoad::Handle file = FileLoad::Open(name, "rb");
    uint32_t size = FileLoad::GetFileSize(file);
    void* buffer = MemSys::AllocBlock(size);
    FileLoad::Read(buffer, static_cast<int>(size), 1, file);
    FileLoad::Close(file);
    Scripts::CStruct* params = NewStructure();
    ReadStructure(params, buffer);
    MemSys::FreeMalloc(buffer);
    DecodeFromStruct(params, false);
    DeleteStructure(params);
    m_flags = (m_flags & ~(kMapUpToDate | kFlag8)) | kMapLoaded | kMapSaved;

    // A premade park keeps its name.
    if (Scripts::CArray* premade = Scripts::GetArray(kPremadeParksInfo)) {
        int count = static_cast<int>(premade->m_size);
        for (int i = 0; i < count; ++i) {
            auto* info = reinterpret_cast<Scripts::CStruct*>(premade->GetElement(i));
            if (!info)
                continue;
            int infoSlot = 0;
            info->FetchIntValue(kSlot, &infoSlot, 0);
            const char* parkName = nullptr;
            if (slot == infoSlot && info->GetString(kName, &parkName, 0))
                SetParkName(parkName);
        }
    }
    m_builder->SetPlayerCap(8);
}

// 0x00599EB0: an older park (gaps of 0x2C bytes, another dead piece).
void CParkLayout::ReadFromOldCompressedMap()
{
    ReadCompressedMap(Scripts::GetArray(kEdSaveMap), false);
}

// 0x0059B750
void CParkLayout::ReadFromCompressedMap()
{
    Scripts::CArray* names = Scripts::GetArray(kEdSaveMap);
    if (m_map.m_version != kMapVersion) {
        ReadFromOldCompressedMap();
        m_builder->SetPlayerCap(1);
        return;
    }
    ReadCompressedMap(names, true);
}

// The two readers' common body (the original has it twice, once for each version). The gap read in starts out
// zeroed here; the original leaves the parts the map doesn't give (+0x4C, and +0x50 in older parks) as they were
// on the stack.
void CParkLayout::ReadCompressedMap(Scripts::CArray* names, bool current)
{
    m_parkBounds.x = m_map.m_posX;
    m_parkBounds.z = m_map.m_depthCell;
    m_parkBounds.w = m_map.m_w;
    m_parkBounds.l = m_map.m_l;
    m_shellSize = m_map.m_shellSize;
    SetTheme(m_map.m_styleSet);
    m_builder->SetPlayerCap(m_map.m_maxPlayers);
    const int8_t* heights = m_map.m_floorHeights;
    for (int x = 0; x < kFloorSize; ++x) {
        for (int z = 0; z < kFloorSize; ++z) {
            m_floor[x][z].m_levelY = *heights++;
            m_floor[x][z].m_heightsUsed = 0;
        }
    }

    // The metapieces (8 bytes each): the name's index, x, z; the height and rotation.
    auto* entry = reinterpret_cast<const uint32_t*>(heights);
    for (int i = 0; i < m_map.m_numMetas; ++i, entry += 2) {
        uint32_t where = entry[0];
        uint32_t how = entry[1];
        uint32_t checksum = names->GetElement(where & 0x3FF);
        if (checksum == (current ? kDeadPiece : kOldDeadPiece))
            continue;
        CPieceGroup* meta = MakePlacedPiece(FindPieceTemplate(checksum), 0);
        SCellBox pos = SCellBox::Make(0, 0, 0, 0, 0, 0);
        pos.x = static_cast<uint8_t>(where >> 10);
        pos.z = static_cast<uint8_t>(where >> 18);
        pos.y = static_cast<int8_t>(((how >> 2) & 0x1F) - 16);
        meta->SetQuarterTurns(how & 3);
        PlaceMetaPieceInPark(pos, meta);
    }

    // The gaps (0x30 bytes each, 0x2C in older parks): both ends, their sides, the name and score.
    auto* gap = reinterpret_cast<const uint8_t*>(entry);
    int gapSize = current ? 0x30 : 0x2C;
    for (int i = 0; i < m_map.m_numGaps; ++i, gap += gapSize) {
        SGapDetails info = {};
        for (int end = 0; end < 2; ++end) {
            info.m_pos[end] = SCellBox::Make(0, 0, 0, 0, 0, 0);
            info.m_pos[end].x = gap[end];
            info.m_pos[end].y = static_cast<int8_t>(gap[2 + end] - 8);
            info.m_pos[end].z = gap[4 + end];
            info.m_quarterTurns[end] = (gap[8] >> (end * 4)) & 0xF;
            info.m_leftExtent[end] = gap[6 + end] >> 4;
            info.m_rightExtent[end] = gap[6 + end] & 0xF;
        }
        CopyString(info.m_text, reinterpret_cast<const char*>(gap + 0xA));
        info.m_pointsHeld = *reinterpret_cast<const uint16_t*>(gap + 0x2A);
        info.m_numCompleteHalves = 2;
        if (current)
            info.m_cancelFlags = *reinterpret_cast<const uint32_t*>(gap + 0x2C);
        RegisterGapEntry(&info);
    }
    m_flags = (m_flags & ~kMapSaved) | kMapUpToDate;
}

// 0x0059BE20
void CParkLayout::ResetPark()
{
    DestroyConcreteMetas(0);
    for (int z = 0; z < kFloorSize; ++z) {
        for (int x = 0; x < kFloorSize; ++x) {
            FloorCell& cell = m_floor[x][z];
            cell.m_levelY = 0;
            cell.m_heightsUsed = 0;
            cell.m_touched = false;
            cell.m_delta = 0;
        }
    }
    WriteCompressedMap();
    if (m_map.m_version == kMapVersion) {
        CopyString(m_map.m_name, "unnamed park");
        CopyString(m_parkName, "unnamed park");
    }
    m_flags = (m_flags & ~kFlag8) | kMapUpToDate;
}

} // namespace Ed
