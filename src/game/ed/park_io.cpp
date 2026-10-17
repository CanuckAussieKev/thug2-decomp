// The park editor's park set up from the scripts (palette, bounds, road mask, lights) and kept in compressed
// form (the map that's saved and sent), the gaps attached to pieces, and the memory bar.

#include "game/ed/park_manager.h"

#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

Mem::Heap* ParkHeap() { return GameVariable<Mem::Heap*>(0x007CE688); }

// The cell size (120, 48, 120) and the grid's offset in cells used by the gap pieces (28).
float CellWidth() { return GameVariable<float>(0x0064F41C); }
float CellHeight() { return GameVariable<float>(0x0064F420); }
float CellLength() { return GameVariable<float>(0x0064F424); }
float GapGridOffset() { return GameVariable<float>(0x0064F730); }

// The park editor object (0x007CE5D8, 0x7C bytes): the value at +0x78 goes into the compressed map.
uint32_t EditorValue78()
{
    auto* editor = GameVariable<uint8_t*>(0x007CE5D8);
    return *reinterpret_cast<uint32_t*>(editor + 0x78);
}
uint8_t GeneratorMaxPlayers(void* generator) { return *(static_cast<uint8_t*>(generator) + 0x20); }

void* PieceSource(void* generator, uint32_t name)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x00591D90)(generator, name);
}
void GetCellDims(void* piece, GridDims* dims)
{
    GameFunction<void(__thiscall*)(void*, GridDims*)>(0x005904C0)(piece, dims);
}
// A new palette metapiece (0x00590FA0: CAbstractMetaPiece's constructor) on the park heap.
CMetaPiece* NewAbstractMeta()
{
    Mem::PushContext(ParkHeap());
    void* memory = Mem::New(sizeof(CMetaPiece));
    auto* meta = memory ? GameFunction<CMetaPiece*(__thiscall*)(void*)>(0x00590FA0)(memory) : nullptr;
    Mem::PopContext();
    return meta;
}
// Element `i` of a script array of integers, as a byte.
uint8_t ByteAt(Script::CArray* array, uint32_t i) { return static_cast<uint8_t>(array->GetElement(i)); }

} // namespace

void CParkManager::SetUpAbstractMetas()
{
    Script::CArray* all = Script::GetArray("Ed_standard_metapieces"); // (0x00479090, asserting)
    for (uint32_t i = 0; i < all->m_size; ++i) {
        auto* desc = reinterpret_cast<Script::CStruct*>(all->GetElement(i));
        uint32_t single = 0;
        if (desc->GetChecksum("single", &single, 0))
            PieceSource(mp_generator, single); // (loads the piece)
    }

    uint32_t pieceNames[1000]; // every piece a metapiece uses
    uint32_t numPieceNames = 0;
    for (uint32_t n = 0; n < all->m_size; ++n) {
        auto* desc = reinterpret_cast<Script::CStruct*>(all->GetElement(n));
        CMetaPiece* meta = NewAbstractMeta();
        uint32_t single = 0;
        Script::CArray* pieces = nullptr;
        if (desc->GetChecksum("single", &single, 0)) {
            meta->Initialize(1);
            void* source = PieceSource(mp_generator, single);
            GridDims pos;
            pos.Set(0, 0, 0, 0, 0, 0);
            GetCellDims(source, &pos);
            Script::CArray* at = nullptr;
            if (desc->GetArray("pos", &at, 0)) {
                uint8_t z = ByteAt(at, 2);
                uint8_t y = ByteAt(at, 1);
                pos.x = ByteAt(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
            }
            meta->AddPiece(source, &pos, 0, false);
            uint32_t name = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(source) + 4); // the source's name
            pieceNames[numPieceNames++] = name;
            meta->m_flags |= CMetaPiece::kFlagSingle;
            meta->m_nameChecksum = name;
            uint32_t special; // (left unset if there's none, as in the original)
            if (desc->GetChecksum(0xF176A9FD, &special, 0)) { // special_type
                switch (special) {
                case 0x2813AB8A: // gap_placement
                    meta->m_flags |= CMetaPiece::kFlagGapPlacement;
                    break;
                case 0xFC26B6F4: // area_selection
                    meta->m_flags |= CMetaPiece::kFlagAreaSelection;
                    break;
                case 0xFFD81C08: // rail_placement
                    meta->m_flags |= CMetaPiece::kFlagRailPlacement;
                    break;
                case 0x48FA9C7B: // restart_1
                    m_specialPieceNames[0] = single;
                    break;
                case 0x419183B2: // restart_multi
                    m_specialPieceNames[1] = single;
                    break;
                case 0xE69AEFAF: // restart_horse
                    m_specialPieceNames[2] = single;
                    break;
                case 0xA091CE25: // king_of_hill
                    m_specialPieceNames[3] = single;
                    break;
                case 0x74B0F4E9: // red_flag
                    m_specialPieceNames[4] = single;
                    meta->m_flags |= CMetaPiece::kFlagRestart;
                    break;
                case 0xC893ACF8: // green_flag
                    m_specialPieceNames[5] = single;
                    meta->m_flags |= CMetaPiece::kFlagRestart;
                    break;
                case 0x40BDFA7E: // blue_flag
                    m_specialPieceNames[6] = single;
                    meta->m_flags |= CMetaPiece::kFlagRestart;
                    break;
                case 0x3AE047FC: // yellow_flag
                    m_specialPieceNames[7] = single;
                    meta->m_flags |= CMetaPiece::kFlagRestart;
                    break;
                }
            }
        } else if (desc->GetArray("multiple", &pieces, 0)) {
            bool unslidable = false;
            meta->Initialize(static_cast<int>(pieces->m_size));
            for (uint32_t i = 0; i < pieces->m_size; ++i) {
                auto* entry = reinterpret_cast<Script::CStruct*>(pieces->GetElement(i));
                uint32_t name = entry->GetNextComponent(nullptr)->checksum; // the piece's name comes first
                Script::CArray* at = nullptr;
                entry->GetArray(0u, &at, 1); // its unnamed [x y z (rot)]
                void* source = PieceSource(mp_generator, name);
                pieceNames[numPieceNames++] = name;
                GridDims pos;
                pos.Set(0, 0, 0, 0, 0, 0);
                GetCellDims(source, &pos);
                uint8_t z = ByteAt(at, 2);
                uint8_t y = ByteAt(at, 1);
                pos.x = ByteAt(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
                int rot = at->m_size > 3 ? static_cast<int>(at->GetElement(3)) : 0;
                if (entry->ContainsFlag(0x0CD75A3Fu))
                    unslidable = true;
                meta->AddPiece(source, &pos, rot, entry->ContainsFlag(0x0CD75A3Fu));
            }
            if (unslidable)
                meta->m_flags |= CMetaPiece::kFlagUnslidable;
        } else if (desc->GetArray("ack", &pieces, 0)) { // made of other palette metapieces
            meta->Initialize(static_cast<int>(pieces->m_size));
            for (uint32_t i = 0; i < pieces->m_size; ++i) {
                auto* entry = reinterpret_cast<Script::CStruct*>(pieces->GetElement(i));
                uint32_t name = entry->GetNextComponent(nullptr)->checksum;
                Script::CArray* at = nullptr;
                entry->GetArray(0u, &at, 1);
                CMetaPiece* part = nullptr;
                for (CMapListNode* node = mp_abstractMetaList; node; node = node->mp_next) {
                    if (node->GetMeta()->Cast()->m_nameChecksum == name) {
                        part = node->GetMeta()->Cast();
                        break;
                    }
                }
                GridDims pos = part->m_cellArea; // (an unknown name crashes, as in the original)
                uint8_t z = ByteAt(at, 2);
                uint8_t y = ByteAt(at, 1);
                pos.x = ByteAt(at, 0);
                pos.y = static_cast<int8_t>(y);
                pos.z = z;
                int rot = at->m_size > 3 ? static_cast<int>(at->GetElement(3)) : 0;
                // 0x00591100: add a metapiece's pieces
                GameFunction<void(__thiscall*)(CMetaPiece*, CMetaPiece*, GridDims*, int, bool)>(0x00591100)(
                    meta, part, &pos, rot, entry->ContainsFlag(0x0CD75A3Fu));
            }
        }
        if (desc->GetChecksum("name", &meta->m_nameChecksum, 0))
            meta->m_flags &= ~CMetaPiece::kFlagSingle;
        if (desc->ContainsFlag("is_riser"))
            meta->m_flags |= CMetaPiece::kFlagRiser;
        if (desc->ContainsFlag("no_rails"))
            meta->m_flags |= CMetaPiece::kFlagNoRails;
        CMapListNode* node = CMapListNode::Create();
        node->mp_meta = meta; // (out of memory crashes, as in the original)
        node->mp_next = mp_abstractMetaList;
        mp_abstractMetaList = node;
        ++m_numAbstractMetas;
    }

    // a single-piece palette metapiece for every piece used that hasn't one
    for (uint32_t i = 0; i < numPieceNames; ++i) {
        uint32_t name = pieceNames[i];
        bool found = false;
        for (CMapListNode* node = mp_abstractMetaList; node; node = node->mp_next) {
            if (node->GetMeta()->Cast()->m_nameChecksum == name) {
                found = true;
                break;
            }
        }
        if (found)
            continue;
        CMetaPiece* meta = NewAbstractMeta();
        meta->Initialize(1);
        void* source = PieceSource(mp_generator, name);
        GridDims pos;
        pos.Set(0, 0, 0, 0, 0, 0);
        GetCellDims(source, &pos);
        meta->AddPiece(source, &pos, 0, false);
        meta->m_nameChecksum = name;
        meta->m_flags |= CMetaPiece::kFlagSingle;
        CMapListNode* node = CMapListNode::Create();
        node->mp_meta = meta;
        node->mp_next = mp_abstractMetaList;
        mp_abstractMetaList = node;
        ++m_numAbstractMetas;
    }
}

void CParkManager::WriteCompressedMap()
{
    if (m_compressedMapFlags & kMapNewerThanPark)
        return;
    CompressedMap& map = m_compressedMap;
    std::memset(&map, 0, sizeof(map));
    Script::CArray* saveNames = Script::GetArray("Ed_Save_Map"); // the palette names, by save index
    uint32_t numNames = saveNames->m_size;
    map.m_version = CompressedMap::kVersion;
    map.m_theme = static_cast<uint16_t>(m_theme);
    map.unknownA = 0;
    map.m_x = m_parkNearBounds.x;
    map.m_z = m_parkNearBounds.z;
    map.m_w = m_parkNearBounds.w;
    map.m_l = m_parkNearBounds.l;
    map.m_unknown18 = EditorValue78();
    map.m_maxPlayers = GeneratorMaxPlayers(mp_generator);
    char* to = map.m_parkName;
    for (const char* from = m_parkName; (*to++ = *from++) != '\0';) {
    }
    uint8_t* out = map.m_data;
    for (int x = 0; x < kFloorMaxWidth; ++x)
        for (int z = 0; z < kFloorMaxLength; ++z)
            *out++ = static_cast<uint8_t>(m_floor[x][z].m_height);

    int numMetas = 0;
    for (CMapListNode* node = mp_concreteMetaList; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        uint32_t flags = meta->m_flags;
        if ((flags & CMetaPiece::kFlagRiser) || (flags & CMetaPiece::kFlag100) || !(flags & CMetaPiece::kFlagInPark))
            continue;
        GridDims at = meta->m_cellArea;
        uint32_t index = 0;
        while (index < numNames && saveNames->GetElement(index) != meta->m_nameChecksum)
            ++index;
        if (index >= numNames)
            continue;
        auto* words = reinterpret_cast<uint32_t*>(out);
        words[0] = ((static_cast<uint32_t>(at.z) << 8 | at.x) << 10) | (index & 0x3FF);
        words[1] = ((static_cast<uint32_t>(static_cast<uint8_t>(at.y)) - 0x10) & 0x1F) << 2 |
                   (static_cast<uint32_t>(meta->m_rot) & 3);
        out += 8;
        ++numMetas;
    }
    map.m_numMetas = static_cast<uint16_t>(numMetas);

    GapInfo* gaps[32];
    int numGaps = 0;
    for (int i = 0; i < 32; ++i) {
        int half = 0;
        // CGapManager: gap `i`'s info (0x0058CB20)
        auto* gap = GameFunction<GapInfo*(__thiscall*)(void*, int, int*)>(0x0058CB20)(mp_gapManager, i, &half);
        if (gap && half == 0 && gap->m_numCompleteHalves == 2)
            gaps[numGaps++] = gap;
    }
    map.m_numGaps = static_cast<uint16_t>(numGaps);
    for (int i = 0; i < numGaps; ++i) {
        const GapInfo* gap = gaps[i];
        out[8] = 0;
        out[0] = gap->m_pos[0].x;
        out[2] = static_cast<uint8_t>(gap->m_pos[0].y + 8);
        out[4] = gap->m_pos[0].z;
        out[8] |= static_cast<uint8_t>(gap->m_rot[0]);
        out[6] = static_cast<uint8_t>(gap->m_leftExtent[0] << 4);
        out[6] = static_cast<uint8_t>((gap->m_rightExtent[0] & 0xF) | (gap->m_leftExtent[0] << 4));
        out[1] = gap->m_pos[1].x;
        out[3] = static_cast<uint8_t>(gap->m_pos[1].y + 8);
        out[5] = gap->m_pos[1].z;
        out[8] |= static_cast<uint8_t>(gap->m_rot[1] << 4);
        out[7] = static_cast<uint8_t>((gap->m_rightExtent[1] & 0xF) | (gap->m_leftExtent[1] << 4));
        char* text = reinterpret_cast<char*>(out + 0xA);
        for (const char* from = gap->m_text; (*text++ = *from++) != '\0';) {
        }
        *reinterpret_cast<uint16_t*>(out + 0x2A) = static_cast<uint16_t>(gap->m_score);
        *reinterpret_cast<uint32_t*>(out + 0x2C) = gap->m_cancelFlags;
        out += 0x30;
    }
    map.m_size = static_cast<uint16_t>(out - reinterpret_cast<uint8_t*>(&map));
    if (!(m_compressedMapFlags & (kMapValid | kMapNewerThanPark)))
        m_compressedMapFlags |= kMapNotSavedLocal;
    m_compressedMapFlags = (m_compressedMapFlags & ~kMapNewerThanPark) | kMapValid | kMapInSync;
    map.m_checksum = Crc::GenerateCRCFromBuffer(&map.m_size, sizeof(map) - 4);
}

void CParkManager::SetDefaultBounds()
{
    int inner = Script::GetInteger("ed_default_inner_dim"); // (0x004790C0, asserting)
    m_parkFarBounds.Set(1, -4, 1, 56, 8, 56);
    uint8_t corner = static_cast<uint8_t>((56 - inner) / 2 + 1);
    m_parkNearBounds.Set(corner, -4, corner, static_cast<uint8_t>(inner), 8, static_cast<uint8_t>(inner));
}

void CParkManager::SetUpRoadMask()
{
    Script::CArray* roads = Script::GetArray("Ed_Roadmask");
    m_roadMaskLength = roads->m_size;
    // (the first four rectangles are set elsewhere; this reads the array from element 4, past its end, as the
    // original does)
    for (uint32_t i = 4; i < m_roadMaskLength + 4; ++i) {
        auto* rect = reinterpret_cast<Script::CArray*>(roads->GetElement(i));
        GridDims& mask = (&m_roadMask[0])[i]; // (past the eighth: over the length itself, as in the original)
        *mask.Byte(0) = ByteAt(rect, 0);
        *mask.Byte(2) = ByteAt(rect, 1);
        *mask.Byte(3) = ByteAt(rect, 2);
        *mask.Byte(5) = ByteAt(rect, 3);
        *mask.Byte(1) = 0;
        mask.MakeInfiniteOnY();
    }
    m_roadMaskLength += 4;
}

void CParkManager::SetUpLights()
{
    Script::CArray* themes = Script::GetArray("Ed_Light_Info"); // (the original doesn't assert)
    auto* theme = reinterpret_cast<Script::CArray*>(themes->GetElement(0));
    auto* info = reinterpret_cast<Script::CStruct*>(theme->GetElement(0));
    float ambientConst, falloffConst, cursorAmbience;
    Script::VectorValue ambient, falloff;
    Script::CArray* positions;
    info->GetFloat("ambient_const", &ambientConst, 1);
    info->GetVector("ambient_rgb", &ambient, 1);
    info->GetFloat("falloff_const", &falloffConst, 1);
    info->GetVector("falloff_rgb", &falloff, 1);
    info->GetFloat("cursor_ambience", &cursorAmbience, 1);
    info->GetArray("pos", &positions, 1);
    // CParkGenerator's lights (0x00591E00) and their positions (0x00592FE0)
    GameFunction<void(__thiscall*)(void*, uint32_t, float, float, float, float, float, float, float)>(0x00591E00)(
        mp_generator, positions->m_size, ambient.x * ambientConst, ambient.y * ambientConst,
        ambient.z * ambientConst, falloff.x * falloffConst, falloff.y * falloffConst, falloff.z * falloffConst,
        cursorAmbience);
    for (int i = 0; i < static_cast<int>(positions->m_size); ++i) {
        auto* v = reinterpret_cast<float*>(positions->GetElement(static_cast<uint32_t>(i))); // a CVector (x at +4)
        Mth::Vector pos;
        pos.x = CellWidth() * v[1];
        pos.y = CellHeight() * v[2];
        pos.z = CellLength() * v[3];
        pos.w = 1.0f;
        GameFunction<void(__thiscall*)(void*, Mth::Vector*, int)>(0x00592FE0)(mp_generator, &pos, i);
    }
}

void CParkManager::UpdatePercentBar()
{
    uint8_t stats[0x6C];
    // 0x0059E730: the park's memory use (fills the stats at 0x007CE618, returns them)
    auto* use = GameFunction<uint8_t*(__cdecl*)(void*)>(0x0059E730)(stats);
    float full;
    std::memcpy(&full, use + 0x2C, 4);
    float percent = full;
    if (full < 0.0f)
        percent = 0.0f;
    else if (full > 1.0f)
        percent = 1.0f;
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* params = memory ? memory->Init() : nullptr;
    params->AddFloat(0x6D6C056Du, percent); // park_percent (out of memory crashes, as in the original)
    if (GameVariable<float>(0x007CE644) >= 1.0f)
        params->AddChecksum(0u, 0x97A8E59Cu); // park_full
    Script::RunScript(0xE2508960, params, nullptr, false, nullptr); // parked_update_percent_bar
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

void CParkManager::WriteCompressedMapIfChanged()
{
    if (m_parkChanged)
        WriteCompressedMap();
}

void CParkManager::WriteIntoStructure(Script::CStruct* out)
{
    if (m_parkChanged)
        WriteCompressedMap();
    m_compressedMapFlags &= ~kMapNotSavedLocal;
    Script::CArray* memory = Script::NewArray();
    Script::CArray* map = memory ? memory->Init() : nullptr;
    map->SetSizeAndType(0xEA6, Script::ESYMBOLTYPE_INTEGER); // (out of memory crashes, as in the original)
    const auto* words = reinterpret_cast<const uint32_t*>(&m_compressedMap);
    for (uint32_t i = 0; i < 0xEA6; ++i)
        map->SetElement(i, words[i]);
    out->AddArrayPointer(0x337C5289u, map); // Park_editor_map
    Script::CStruct* structMemory = Script::NewStruct();
    Script::CStruct* goals = structMemory ? structMemory->Init() : nullptr;
    // the goal editor (module GoalEditor, 0x004E8470) writes the park's goals (level Load_Sk5Ed)
    void* goalEditor = GameFunction<void*(__cdecl*)()>(0x004E8470)();
    GameFunction<void(__thiscall*)(void*, uint32_t, Script::CStruct*)>(0x004EBA10)(goalEditor, 0xE8B4B836, goals);
    out->AddStructurePointer(0xD8EB825Eu, goals); // Park_editor_goals
    // the rail editor (module RailEditor, 0x004F4970) writes the park's rails
    void* railEditor = GameFunction<void*(__cdecl*)()>(0x004F4970)();
    GameFunction<void(__thiscall*)(void*, Script::CStruct*)>(0x004F6680)(railEditor, out);
    out->AddInteger(0xB7E39B53u, GeneratorMaxPlayers(mp_generator)); // MaxPlayers
}

void CParkManager::SetTheme(int theme)
{
    m_theme = theme;
    WriteCompressedMap();
}

void CParkManager::ClearCompressedMap()
{
    SetDefaultBounds();
    CompressedMap& map = m_compressedMap;
    map.m_version = CompressedMap::kVersion;
    map.m_theme = static_cast<uint16_t>(m_theme);
    map.unknownA = 0;
    map.m_x = m_parkNearBounds.x;
    map.m_z = m_parkNearBounds.z;
    map.m_w = m_parkNearBounds.w;
    map.m_l = m_parkNearBounds.l;
    map.m_unknown18 = GameVariable<void*>(0x007CE5D8) ? EditorValue78() : 0;
    map.m_maxPlayers = 2;
    std::memcpy(map.m_parkName, reinterpret_cast<const void*>(0x0064F734), 13); // "Unnamed park"
    std::memset(map.m_data, 0, kFloorMaxWidth * kFloorMaxLength);
    map.m_numMetas = 0;
    uint32_t used = offsetof(CompressedMap, m_data) + kFloorMaxWidth * kFloorMaxLength;
    // (the checksum is taken before the size and the gap count are set, as in the original)
    uint32_t checksum = Crc::GenerateCRCFromBuffer(&map.m_size, static_cast<int>(used - 4));
    map.m_size = static_cast<uint16_t>(used);
    map.m_checksum = checksum;
    map.m_numGaps = 0;
    if (!(m_compressedMapFlags & (kMapValid | kMapNewerThanPark)))
        m_compressedMapFlags |= kMapNotSavedLocal;
    m_compressedMapFlags = (m_compressedMapFlags & ~kMapNewerThanPark) | kMapValid | kMapInSync;
}

bool CParkManager::AddGap(GapInfo* gap)
{
    // CGapManager: a free slot (0x0058C990)
    int tab = GameFunction<int(__thiscall*)(void*)>(0x0058C990)(mp_gapManager);
    if (tab == -1)
        return false;
    // The trigger's position (x87 arithmetic: x is stored as a float on the way, z isn't). For a rotation past 3
    // the previous half's position is used again (the original's are left unset before the first half).
    Mth::Vector at = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int half = 0; half < gap->m_numCompleteHalves; ++half) {
        GridDims& pos = gap->m_pos[half];
        pos.w = 1;
        pos.h = 1;
        pos.l = 1;
        CMapListTemp metas(GetAreaMetaPieces(pos, 0, nullptr, true));
        if (!metas.GetList())
            return false;
        CMetaPiece* meta = metas.GetList()->GetMeta()->Cast();
        meta->m_flags |= CMetaPiece::kFlagInGap;
        pos = meta->m_cellArea;
        GridDims d = meta->m_cellArea;
        // 0x0058CBF0: the gap's length on this half
        float length = GameFunction<float(__thiscall*)(GapInfo*, int)>(0x0058CBF0)(gap, half);
        const double w = CellWidth(), l = CellLength();
        const double y = static_cast<double>(d.y) * CellHeight();
        int alongX = 0;
        float x = at.x;
        double z = at.z;
        switch (gap->m_rot[half]) {
        case 0:
            alongX = 1;
            x = static_cast<float>((static_cast<double>(d.w + d.x) * w - w * 0.5) - 1.0);
            z = (static_cast<double>(d.z - gap->m_rightExtent[half]) * l + length * 0.5) - l;
            at.y = static_cast<float>(y);
            break;
        case 1:
            x = static_cast<float>((static_cast<double>(d.x - gap->m_rightExtent[half]) * w + length * 0.5) - w);
            z = (static_cast<double>(d.z) * l - l * 0.5) + 1.0;
            at.y = static_cast<float>(y);
            break;
        case 2:
            alongX = 1;
            x = static_cast<float>((static_cast<double>(d.x) * w - w * 0.5) + 1.0);
            z = (static_cast<double>(d.z - gap->m_leftExtent[half]) * l + length * 0.5) - l;
            at.y = static_cast<float>(y);
            break;
        case 3:
            x = static_cast<float>((static_cast<double>(d.x - gap->m_leftExtent[half]) * w + length * 0.5) - w);
            z = (static_cast<double>(d.l + d.z) * l - l * 0.5) - 1.0;
            at.y = static_cast<float>(y);
            break;
        }
        at.x = static_cast<float>(x - w * GapGridOffset());
        at.z = static_cast<float>(z - l * GapGridOffset());
        // CParkGenerator: the gap's trigger piece (0x00593500), its length in cells (0x005906B0)
        void* piece = GameFunction<void*(__thiscall*)(void*, Mth::Vector*, float, int)>(0x00593500)(mp_generator, &at,
                                                                                               length, alongX);
        GameFunction<void(__thiscall*)(void*, float)>(0x005906B0)(piece, static_cast<float>(length / w));
        if (half == 0) // CGapManager: the first half (0x0058D070), the second (0x0058C9B0)
            GameFunction<void(__thiscall*)(void*, void*, CMetaPiece*, int)>(0x0058D070)(mp_gapManager, piece, meta, tab);
        else
            GameFunction<void(__thiscall*)(void*, void*, CMetaPiece*, int)>(0x0058C9B0)(mp_gapManager, piece, meta, tab);
    }
    gap->m_tabIndex = tab;
    GameFunction<void(__thiscall*)(void*, int, GapInfo*)>(0x0058CDA0)(mp_gapManager, tab, gap);
    return true;
}

void CParkManager::RemoveGap(GapInfo* gap)
{
    CMetaPiece* halves[2]; // (unset for a half with nothing there, as in the original)
    for (int half = 0; half < gap->m_numCompleteHalves; ++half) {
        CMapListTemp metas(GetAreaMetaPieces(gap->m_pos[half], 0, nullptr, true));
        if (metas.GetList()->mp_next)
            PrintMapList(metas.GetList());
        for (CMapListNode* node = metas.GetList(); node; node = node->mp_next) {
            CMetaPiece* meta = node->GetMeta()->Cast();
            halves[half] = meta;
            const GridDims& d = meta->m_cellArea;
            if (d.x == gap->m_pos[half].x && d.y == gap->m_pos[half].y && d.z == gap->m_pos[half].z)
                break;
        }
        CMetaPiece* meta = halves[half];
        GameFunction<void(__thiscall*)(CMetaPiece*, bool, int)>(0x00591A50)(meta, false, 0); // not highlighted
        meta->m_flags &= ~CMetaPiece::kFlagInGap;
    }
    int index;
    // CGapManager: the gap of the first half (0x0058CA10), removed (0x0058CDF0)
    if (GameFunction<bool(__thiscall*)(void*, CMetaPiece*, int*)>(0x0058CA10)(mp_gapManager, halves[0], &index))
        GameFunction<void(__thiscall*)(void*, int, int)>(0x0058CDF0)(mp_gapManager, index, 1);
    gap->m_tabIndex = -1;
    gap->m_cancelFlags = static_cast<uint32_t>(Script::GetInteger(0x9A27E74Du)); // CANCEL_GROUND
}

void CParkManager::PrintMapList(const CMapListNode* list)
{
    Dbg::Printf(reinterpret_cast<const char*>(0x0064F404));
    for (const CMapListNode* node = list; node; node = node->mp_next) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064F3F0), node->mp_meta);
        const GridDims& d = node->mp_meta->m_cellArea;
        Dbg::Printf(reinterpret_cast<const char*>(0x0064F394), d.x, static_cast<int>(d.y), d.z, d.w, d.h, d.l);
    }
}

} // namespace Ed

using Ed::CParkManager;

REPLACE(0x005960D0, &CParkManager::SetUpAbstractMetas);
REPLACE(0x00596980, &CParkManager::WriteCompressedMap);
REPLACE(0x00596CD0, &CParkManager::SetDefaultBounds);
REPLACE(0x00596D50, &CParkManager::SetUpRoadMask);
REPLACE(0x00596E50, &CParkManager::SetUpLights);
REPLACE(0x00596FB0, &CParkManager::UpdatePercentBar);
REPLACE(0x005975A0, &CParkManager::WriteCompressedMapIfChanged);
REPLACE(0x005975B0, &CParkManager::WriteIntoStructure);
REPLACE(0x00597720, &CParkManager::SetTheme);
REPLACE(0x005987A0, &CParkManager::ClearCompressedMap);
REPLACE(0x00597BD0, &CParkManager::AddGap);
REPLACE(0x00597FC0, &CParkManager::RemoveGap);
