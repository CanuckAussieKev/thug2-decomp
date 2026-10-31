// The scene side of the park (ParkEditing::CParkAssembler, first part: 0x00591C70-0x00592740): the source pieces and the
// pieces cloned from them into the park's scene, the park's restart points' slots, its lights, its scene's bounds and
// collision; and the lists of metapieces the park manager hands out (CMapListNode, CCellChainScratch).

#include "game/park_editing/park.h"

#include <cstdint>
#include <cstring>

#include "game/assets/resource_entry.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/park_editing/park_nx.h"
#include "game/park_editing/park_scene.h"
#include "game/maths/shape_tests.h"
#include "game/park/park_memory.h"
#include "game/script/param_struct.h"
#include "game/park_editing/park_editor.h"

namespace ParkEditing {

int g_cloningPiece;

namespace {

// Whether the generator rebuilds its collision (off while every piece is deleted).
bool& RebuildsCollision() { return g_collisionAllowed; }
// Raised while CreatePiece clones a sector.
int& CloningPiece() { return g_cloningPiece; }

// The members at +0x24 and +0x58 have empty destructors (calls to the empty function 0x005480A0).
inline void DestroyEmptyMember(void* /*member*/) {}

// 0x0049F4B0 (a folded `return false`): a colour to give a new piece's geometry - never.
inline bool GetPieceColor(CParkTile* /*piece*/, uint32_t* /*rgba*/) { return false; }

// The park's main memory a player takes (the bottom-up heap's free memory per player, 0x0008EF1C).
constexpr int kMemoryPerPlayer = 585500;

} // namespace

// --- CMapListNode / CCellChainScratch ---------------------------------------------------------------------------------

void CCellChainScratch::DumpToLog()
{
    DebugOut::Printf("contents of map list:\n"); // "contents of map list:\n"
    for (const CMapListNode* node = list; node; node = node->m_following) {
        DebugOut::Printf("   address=%p area=", node->m_pieceGroup); // "   address=%p area="
        node->m_pieceGroup->m_cellFootprint.DumpToLog();
    }
}

void CCellChainScratch::Teardown()
{
    if (list)
        CMapListNode::FreeNodeChain(list);
}

// --- CParkAssembler ---------------------------------------------------------------------------------------------

namespace {

uint8_t* SceneOf(void* scene) { return static_cast<uint8_t*>(scene); }

} // namespace

void CParkAssembler::DestroyBody()
{
    if (m_sceneHandle)
        m_sceneHandle = nullptr;
    if (m_sourceWorld)
        m_sourceWorld = nullptr;
    // (the node array's owner: 0x00407810 is CResource's destructor body, folded with CNodeTableResource's)
    reinterpret_cast<AssetStore::CResource*>(m_nodeArray)->Destruct();
    DestroyEmptyMember(&m_rails);
    DestroyEmptyMember(&m_sourceRails);
}

void CParkAssembler::GlowEveryPiece(bool on)
{
    for (CParkTile* piece = m_pieces; piece;) {
        CParkTile* next = piece->m_following;
        piece->SetHighlight(on, false);
        piece = next;
    }
}

void CParkAssembler::SetPlayerCap(int count) { m_maxPlayers = count; }

int CParkAssembler::GetPlayerCapLimit()
{
    MemoryStats stats;
    GetMemoryStats(&stats);
    // (the original's 32-bit arithmetic, wrapping)
    const int memory = static_cast<int>(static_cast<uint32_t>(stats.mainHeapFree) +
                                        static_cast<uint32_t>(m_maxPlayers) *
                                            static_cast<uint32_t>(kMemoryPerPlayer) -
                                        static_cast<uint32_t>(kMemoryPerPlayer));
    return memory / kMemoryPerPlayer + 1;
}

CTileTemplate* CParkAssembler::GetPieceSource(uint32_t name)
{
    for (CTileTemplate* source = m_sources; source; source = source->m_following) {
        if (source->m_label == name)
            return source;
    }
    return nullptr;
}

bool CParkAssembler::RemoveSceneSector(uint32_t name, int /*unused*/)
{
    return Scene::RemoveSceneSector(SceneOf(m_sceneHandle), name);
}

void CParkAssembler::DestroyRails()
{
    m_rails.Clear();
    m_rails.FreePools();
}

void CParkAssembler::ConfigureLighting(int count, float ambientR, float ambientG, float ambientB, float falloffR,
                               float falloffG, float falloffB, float cursorAmbience)
{
    m_lightTotal = count;
    m_ambient[0] = ambientR;
    m_ambient[1] = ambientG;
    m_ambient[2] = ambientB;
    m_falloff[0] = falloffR;
    m_falloff[1] = falloffG;
    m_falloff[2] = falloffB;
    std::memcpy(&m_43C, &cursorAmbience, 4); // (the cursor's ambience)
}

void CParkAssembler::FetchSpawnSlots(int type, int* first, int* last)
{
    switch (type) {
    case RestartPoint::kPlayer1:
        *first = 1;
        *last = 1;
        break;
    case RestartPoint::kPlayer2:
        *first = 0;
        *last = 0;
        break;
    case RestartPoint::kHorse:
        *first = 2;
        *last = 7;
        break;
    case RestartPoint::kCrownRestart:
        *first = 8;
        *last = 13;
        break;
    case RestartPoint::kRedFlag: // the four flags, one each
    case RestartPoint::kRedFlag + 1:
    case RestartPoint::kRedFlag + 2:
    case RestartPoint::kYellowFlag:
        *first = 14 + (type - RestartPoint::kRedFlag);
        *last = *first;
        break;
    default: // (left as they were)
        break;
    }
}

bool CParkAssembler::HasFreeRestartSlot(int type)
{
    int first = 0;
    int last = 0;
    FetchSpawnSlots(type, &first, &last);
    for (int i = first; i <= last; ++i) {
        if (!m_restarts[i].m_kindTag || m_restarts[i].m_automatic)
            return true;
    }
    return false;
}

void CParkAssembler::ClearRestarts()
{
    for (RestartPoint& slot : m_restarts)
        slot.m_kindTag = RestartPoint::kFree;
}

void CParkAssembler::PurgeAutoRestartPoints()
{
    for (RestartPoint& slot : m_restarts) {
        if (slot.m_automatic)
            slot.m_kindTag = RestartPoint::kFree;
    }
}

void CParkAssembler::SetUpBounds()
{
    // The most the park can take up: 56 x 56 cells, 16 steps down and 66 up.
    Maths::BBox box;
    Maths::Vec4 low = {-3360.0f, -768.0f, -3360.0f, 1.0f};
    box.AppendSpot(low);
    Maths::Vec4 high = {3360.0f, 3168.0f, 3360.0f, 1.0f};
    box.AppendSpot(high);
    Scene::AssignSceneBounds(SceneOf(m_sceneHandle), &box);
}

void CParkAssembler::RebuildCollision()
{
    if (!RebuildsCollision())
        return;
    m_flagBits &= ~CParkAssembler::kSceneChanged;
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    Scene::UpdateSuperSectors(SceneOf(m_sceneHandle), nullptr);
    MemSys::LeaveHeapScope();
    m_flagBits |= CParkAssembler::kSuperSectorsUpdated;
}

void CParkAssembler::UpdateBounds(uint32_t /*unused*/)
{
    CParkLayout* manager = CParkLayout::sSingleton();
    uint8_t* shell = Scene::LookUpScene(manager->GetShellChecksum());
    if (!shell)
        return;
    // The shell's first box: as big as can be.
    Maths::Vec4 far = {200000.0f, 200000.0f, 200000.0f, 1.0f};
    Maths::Vec4 near = {-200000.0f, -200000.0f, -200000.0f, 1.0f};
    Maths::Seg everything(near, far);
    Scene::SetBox(shell, &everything, 1, 0);
    // The second: the park's bounds, from the middle of its first cell to the middle of its last, every height.
    SCellBox area;
    area.AssignValue(0, 0, 0, 0, 0, 0);
    area = CParkLayout::sSingleton()->m_buildArea;
    Maths::Vec4 corner = CParkLayout::sSingleton()->CellToWorldPos(area, nullptr);
    area.x = static_cast<uint8_t>(area.x + (area.w - 1));
    area.z = static_cast<uint8_t>(area.z + (area.l - 1));
    Maths::Vec4 farCorner = CParkLayout::sSingleton()->CellToWorldPos(area, nullptr);
    corner.x = static_cast<float>(static_cast<double>(corner.x) + 60.0);
    corner.y = -100000.0f;
    corner.z = static_cast<float>(static_cast<double>(corner.z) + 60.0);
    farCorner.y = 100000.0f;
    farCorner.x = static_cast<float>(static_cast<double>(farCorner.x) + 60.0);
    farCorner.z = static_cast<float>(static_cast<double>(farCorner.z) + 60.0);
    Maths::Seg park(corner, farCorner);
    Scene::SetBox(shell, &park, 0, 0);
}

void CParkAssembler::SetPiecesHighlighted(int on)
{
    for (CParkTile* piece = m_pieces; piece;) {
        CParkTile* next = piece->m_following;
        if (piece->m_flagBits & CParkTile::kGap)
            piece->SetVisible(on != 0);
        piece = next;
    }
}

void CParkAssembler::SetRestartPiecesActive(bool active)
{
    for (CParkTile* piece = m_pieces; piece;) {
        CParkTile* next = piece->m_following;
        if (CParkLayout::sSingleton()->GetRestartIndex(piece->m_blendSource->m_label))
            piece->ChangeLive(active);
        piece = next;
    }
}

uint32_t CParkAssembler::DuplicateSceneSector(uint32_t name, int a, int b, int /*unused*/)
{
    m_flagBits |= CParkAssembler::kSceneChanged;
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    uint32_t clone = Scene::DuplicateSceneSector(SceneOf(m_sourceWorld), name, SceneOf(m_sceneHandle), a, b, 0);
    MemSys::LeaveHeapScope();
    return clone;
}

bool CParkAssembler::DestroySector(uint32_t name, int /*unused*/)
{
    bool deleted = Scene::RemoveSceneSector(SceneOf(m_sceneHandle), name);
    RebuildCollision();
    return deleted;
}

CParkTile* CParkAssembler::CreatePiece(CParkTile* source, uint32_t flags)
{
    flags &= CParkTile::kCursor | CParkTile::kNoNode20 | CParkTile::kNoRails | CParkTile::kDestroyNow;
    CTileTemplate* from = (source->m_flagBits & CParkTile::kSource) ? reinterpret_cast<CTileTemplate*>(source) : source->m_blendSource;
    // The source's sector copied into the park's scene (on the bottom-up heap).
    CloningPiece() = 1;
    const uint32_t name = from->m_label;
    m_flagBits |= CParkAssembler::kSceneChanged;
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    uint32_t clone = (flags & CParkTile::kCursor)
                         ? Scene::DuplicateSceneSector(SceneOf(m_sourceWorld), name, SceneOf(m_sceneHandle), 1, 0, 0)
                         : Scene::DuplicateSceneSector(SceneOf(m_sourceWorld), name, SceneOf(m_sceneHandle), 0, 1, 0);
    MemSys::LeaveHeapScope();
    CloningPiece() = 0;
    // The piece itself on the park editor's heap.
    MemSys::EnterHeapScope(GetParkHeap());
    void* memory = MemSys::Alloc(sizeof(CCopiedPiece));
    CCopiedPiece* piece = memory ? static_cast<CCopiedPiece*>(memory)->MakeNew() : nullptr;
    MemSys::LeaveHeapScope();
    piece->m_sector = clone; // (out of memory crashes, as in the original)
    piece->m_ident = m_nextPieceId++;
    piece->m_blendSource = from;
    piece->SetQuarterTurns(0, 0);
    piece->m_flagBits |= flags;
    uint32_t rgba;
    if (GetPieceColor(piece, &rgba)) { // (never)
        if (uint8_t* sector = Scene::LookUpSector(clone)) {
            if (uint8_t* geometry = Scene::FetchShape(sector))
                Scene::ChangeTint(geometry, rgba);
        }
    }
    SetCountsDirty();
    m_flagBits |= CParkAssembler::kPiecesCreated;
    return piece;
}

void CParkAssembler::InsertTile(CParkTile* piece)
{
    piece->m_flagBits |= CParkTile::kInList;
    piece->m_following = m_pieces;
    m_pieces = piece;
    m_flagBits |= CParkAssembler::kHasPieces;
}

void CParkAssembler::DeletePiece(CParkTile* piece, int keepSector)
{
    if (!(piece->m_flagBits & CParkTile::kCursor)) {
        CParkTile* previous = nullptr;
        for (CParkTile* p = m_pieces; p; previous = p, p = p->m_following) {
            if (p != piece)
                continue;
            if (previous)
                previous->m_following = p->m_following;
            else
                m_pieces = p->m_following;
            break;
        }
    }
    if (!keepSector)
        DestroySector(piece->m_sector, static_cast<int>(piece->m_flagBits));
    SetCountsDirty();
    piece->EraseOne();
    if (!m_pieces)
        m_flagBits &= ~CParkAssembler::kHasPieces;
}

void CParkAssembler::ReadPieceFlags(Scripts::CArray* nodes)
{
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(i));
        uint32_t nodeClass = 0;
        node->ReadHashValue(0x12B4E660u, &nodeClass, 0); // Class
        if (nodeClass != 0xBF4D3536u && nodeClass != 0xEF59C100u) // LevelGeometry, GameObject
            continue;
        uint32_t name = 0;
        node->ReadHashValue(0xA1DC81F9u, &name, 0); // name
        if (!name)
            continue;
        CTileTemplate* source = GetPieceSource(name);
        if (!source)
            continue;
        if (nodeClass == 0xEF59C100u) {
            source->m_flagBits |= CTileTemplate::kGameObject;
            if (node->HasBareFlag(0x68910AC6u))
                source->m_flagBits |= CTileTemplate::kAbsentInNetGames;
        }
        uint32_t script = 0;
        node->ReadHashValue(0x2CA8A299u, &script, 0); // TriggerScript
        if (!script)
            continue;
        source->m_triggerScript = script;
        switch (script) { // (four scripts that change their pieces: rebuilt by RebuildMarked)
        case 0x3DEDAFE9u:
        case 0x22C20ED8u:
        case 0x28673697u:
        case 0xF6187E48u:
            source->m_flagBits |= CTileTemplate::kRebuilt;
            break;
        default:
            break;
        }
    }
}

} // namespace Ed

using ParkEditing::CMapListNode;
using ParkEditing::CCellChainScratch;
using ParkEditing::CParkAssembler;
