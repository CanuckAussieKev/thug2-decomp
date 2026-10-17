// The park manager's compressed map (Ed::CParkManager): the park read from a script structure, a file or the
// map itself, and a new empty park.

#include "game/ed/park.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/script/struct.h"
#include "hook/hook.h"

namespace Ed {

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
void* GoalEditor() { return GameFunction<void*(__cdecl*)()>(0x004E8470)(); }
void ClearGoals(void* goalEditor) { GameFunction<void(__thiscall*)(void*)>(0x004E8280)(goalEditor); }
void ReadGoals(void* goalEditor, Script::CStruct* goals, int flag)
{
    GameFunction<void(__thiscall*)(void*, Script::CStruct*, int)>(0x004EA470)(goalEditor, goals, flag);
}
// The rail editor (0x004F4970 looks it up): its rails read from a structure (0x004F7ED0).
void ReadRails(Script::CStruct* params)
{
    void* railEditor = GameFunction<void*(__cdecl*)()>(0x004F4970)();
    GameFunction<void(__thiscall*)(void*, Script::CStruct*)>(0x004F7ED0)(railEditor, params);
}
// A flag the rail editor reads (cleared while rails are read outside the park editor's level).
bool& RailEditorFlag() { return GameVariable<bool>(0x0067F000); }

// The park editor (the singleton 0x00590160 makes, 0x0058D290 releases; 0x007CE5D8 holds it): its level (+0x40),
// the map's goals handed to it (0x0058D4E0), the theme set (0x0058D700).
void* CreateParkEditor() { return GameFunction<void*(__cdecl*)(bool)>(0x00590160)(false); }
void ReleaseParkEditor() { GameFunction<void(__cdecl*)()>(0x0058D290)(); }
uint32_t EditorLevel(void* editor) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(editor) + 0x40); }
void ReadMapGoals(void* editor, CompressedMap* map, uint16_t size)
{
    GameFunction<void(__thiscall*)(void*, CompressedMap*, uint32_t)>(0x0058D4E0)(editor, map, size);
}
void SetTheme(uint32_t theme)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0058D700)(GameVariable<void*>(0x007CE5D8), theme);
}

// The script structure written to a buffer and read back (0x0047A070 sizes it, 0x00479E20 writes it,
// 0x0047A0C0 reads it).
uint32_t StructureBufferSize(Script::CStruct* structure, uint32_t maximum)
{
    return GameFunction<uint32_t(__cdecl*)(Script::CStruct*, uint32_t)>(0x0047A070)(structure, maximum);
}
void WriteStructure(Script::CStruct* structure, void* buffer, uint32_t size, int a, int b)
{
    GameFunction<void(__cdecl*)(Script::CStruct*, void*, uint32_t, int, int)>(0x00479E20)(structure, buffer, size, a, b);
}
void ReadStructure(Script::CStruct* structure, void* buffer)
{
    GameFunction<void(__cdecl*)(Script::CStruct*, void*)>(0x0047A0C0)(structure, buffer);
}

// strcpy (inline in the original: no limit).
void CopyString(char* dest, const char* source)
{
    while ((*dest++ = *source++) != '\0') {
    }
}

Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

} // namespace

// 0x00598FC0
void CParkManager::ReadFromStructure(Script::CStruct* params, bool applyToEditor)
{
    void* goalEditor = GoalEditor();
    ClearGoals(goalEditor);
    Script::CStruct* goals = nullptr;
    if (params->GetStructure(kParkEditorGoals, &goals, 0))
        ReadGoals(goalEditor, goals, 1);

    void* editor = CreateParkEditor();
    if (EditorLevel(editor) != kEditLevel)
        RailEditorFlag() = false;
    ReadRails(params);
    RailEditorFlag() = true;

    int maxPlayers = 2;
    params->GetInteger(kMaxPlayers, &maxPlayers, 0);
    Instance()->mp_generator->SetMaxPlayers(maxPlayers);

    uint16_t mapSize = (m_flags & kMapLoaded) ? m_map.m_shellSize : 0;
    if (m_parkBuilt)
        WriteCompressedMap();
    Script::CArray* map = nullptr;
    if (params->GetArray("Park_editor_map", &map, 1)) {
        for (int i = 0; i < static_cast<int>(sizeof(CompressedMap) / 4); ++i)
            reinterpret_cast<uint32_t*>(&m_map)[i] = map->GetElement(i);
        if (applyToEditor) {
            void* editorAgain = CreateParkEditor();
            ReadMapGoals(editorAgain, &m_map, mapSize);
            ReleaseParkEditor();
        }
    }
    ReleaseParkEditor();
}

// 0x00599140
void CParkManager::AccessDisk(bool save, int slot)
{
    char name[64];
    std::snprintf(name, sizeof(name), "CustomParks\\custom%d.prk", slot);
    if (save) {
        Script::CStruct* params = NewStructure();
        WriteIntoStructure(params);
        uint32_t size = StructureBufferSize(params, 100000);
        void* buffer = Mem::Malloc(size);
        WriteStructure(params, buffer, size, 1, 0);
        File::Handle file = File::Open(name, "wb");
        File::Write(buffer, static_cast<int>(size), 1, file);
        File::Close(file);
        Mem::FreeMalloc(buffer);
        DeleteStructure(params);
        return;
    }

    File::Handle file = File::Open(name, "rb");
    uint32_t size = File::GetFileSize(file);
    void* buffer = Mem::Malloc(size);
    File::Read(buffer, static_cast<int>(size), 1, file);
    File::Close(file);
    Script::CStruct* params = NewStructure();
    ReadStructure(params, buffer);
    Mem::FreeMalloc(buffer);
    ReadFromStructure(params, false);
    DeleteStructure(params);
    m_flags = (m_flags & ~(kMapUpToDate | kFlag8)) | kMapLoaded | kMapSaved;

    // A premade park keeps its name.
    if (Script::CArray* premade = Script::GetArray(kPremadeParksInfo)) {
        int count = static_cast<int>(premade->m_size);
        for (int i = 0; i < count; ++i) {
            auto* info = reinterpret_cast<Script::CStruct*>(premade->GetElement(i));
            if (!info)
                continue;
            int infoSlot = 0;
            info->GetInteger(kSlot, &infoSlot, 0);
            const char* parkName = nullptr;
            if (slot == infoSlot && info->GetString(kName, &parkName, 0))
                SetParkName(parkName);
        }
    }
    mp_generator->SetMaxPlayers(8);
}

// 0x00599EB0: an older park (gaps of 0x2C bytes, another dead piece).
void CParkManager::ReadFromOldCompressedMap()
{
    ReadCompressedMap(Script::GetArray(kEdSaveMap), false);
}

// 0x0059B750
void CParkManager::ReadFromCompressedMap()
{
    Script::CArray* names = Script::GetArray(kEdSaveMap);
    if (m_map.m_version != kMapVersion) {
        ReadFromOldCompressedMap();
        mp_generator->SetMaxPlayers(1);
        return;
    }
    ReadCompressedMap(names, true);
}

// The two readers' common body (the original has it twice, once for each version). The gap read in starts out
// zeroed here; the original leaves the parts the map doesn't give (+0x4C, and +0x50 in older parks) as they were
// on the stack.
void CParkManager::ReadCompressedMap(Script::CArray* names, bool current)
{
    m_parkBounds.x = m_map.m_x;
    m_parkBounds.z = m_map.m_z;
    m_parkBounds.w = m_map.m_w;
    m_parkBounds.l = m_map.m_l;
    m_shellSize = m_map.m_shellSize;
    SetTheme(m_map.m_theme);
    mp_generator->SetMaxPlayers(m_map.m_maxPlayers);
    const int8_t* heights = m_map.m_floorHeights;
    for (int x = 0; x < kFloorSize; ++x) {
        for (int z = 0; z < kFloorSize; ++z) {
            m_floor[x][z].m_height = *heights++;
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
        CMetaPiece* meta = CreateConcreteMeta(GetAbstractMeta(checksum), 0);
        GridDims pos = GridDims::Make(0, 0, 0, 0, 0, 0);
        pos.x = static_cast<uint8_t>(where >> 10);
        pos.z = static_cast<uint8_t>(where >> 18);
        pos.y = static_cast<int8_t>(((how >> 2) & 0x1F) - 16);
        meta->SetRot(how & 3);
        AddMetaPieceToPark(pos, meta);
    }

    // The gaps (0x30 bytes each, 0x2C in older parks): both ends, their sides, the name and score.
    auto* gap = reinterpret_cast<const uint8_t*>(entry);
    int gapSize = current ? 0x30 : 0x2C;
    for (int i = 0; i < m_map.m_numGaps; ++i, gap += gapSize) {
        GapInfo info = {};
        for (int end = 0; end < 2; ++end) {
            info.m_pos[end] = GridDims::Make(0, 0, 0, 0, 0, 0);
            info.m_pos[end].x = gap[end];
            info.m_pos[end].y = static_cast<int8_t>(gap[2 + end] - 8);
            info.m_pos[end].z = gap[4 + end];
            info.m_rot[end] = (gap[8] >> (end * 4)) & 0xF;
            info.m_leftExtent[end] = gap[6 + end] >> 4;
            info.m_rightExtent[end] = gap[6 + end] & 0xF;
        }
        CopyString(info.m_text, reinterpret_cast<const char*>(gap + 0xA));
        info.m_score = *reinterpret_cast<const uint16_t*>(gap + 0x2A);
        info.m_numCompleteHalves = 2;
        if (current)
            info.m_cancelFlags = *reinterpret_cast<const uint32_t*>(gap + 0x2C);
        AddGap(&info);
    }
    m_flags = (m_flags & ~kMapSaved) | kMapUpToDate;
}

// 0x0059BE20
void CParkManager::ResetPark()
{
    DestroyConcreteMetas(0);
    for (int z = 0; z < kFloorSize; ++z) {
        for (int x = 0; x < kFloorSize; ++x) {
            FloorCell& cell = m_floor[x][z];
            cell.m_height = 0;
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

REPLACE(0x00598FC0, &Ed::CParkManager::ReadFromStructure);
REPLACE(0x00599140, &Ed::CParkManager::AccessDisk);
REPLACE(0x00599EB0, &Ed::CParkManager::ReadFromOldCompressedMap);
REPLACE(0x0059B750, &Ed::CParkManager::ReadFromCompressedMap);
REPLACE(0x0059BE20, &Ed::CParkManager::ResetPark);
