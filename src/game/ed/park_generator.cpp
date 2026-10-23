// The scene side of the park (Ed::CParkGenerator, first part: 0x00591C70-0x00592740): the source pieces and the
// pieces cloned from them into the park's scene, the park's restart points' slots, its lights, its scene's bounds and
// collision; and the lists of metapieces the park manager hands out (CMapListNode, CMapListTemp).

#include "game/ed/park.h"

#include <cstring>

#include "game/ass/asset.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/ed/park_nx.h"
#include "game/ed/park_scene.h"
#include "game/mth/geometry.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// Whether the generator rebuilds its collision (off while every piece is deleted).
bool& RebuildsCollision() { return GameVariable<bool>(0x0067F398); }
// Raised while CreatePiece clones a sector.
int& CloningPiece() { return GameVariable<int>(0x007CE614); }

// The members at +0x24 and +0x58 have empty destructors (calls to the empty function 0x005480A0).
inline void DestroyEmptyMember(void* /*member*/) {}

// 0x0049F4B0 (a folded `return false`): a colour to give a new piece's geometry - never.
inline bool GetPieceColor(CPiece* /*piece*/, uint32_t* /*rgba*/) { return false; }

// The park's main memory a player takes (the bottom-up heap's free memory per player, 0x0008EF1C).
constexpr int kMemoryPerPlayer = 585500;

} // namespace

// --- CMapListNode / CMapListTemp ---------------------------------------------------------------------------------

void CMapListTemp::PrintContents()
{
    Dbg::Printf(reinterpret_cast<const char*>(0x0064F404)); // "contents of map list:\n"
    for (const CMapListNode* node = list; node; node = node->mp_next) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064F3F0), node->mp_meta); // "   address=%p area="
        node->mp_meta->m_cell_area.PrintContents();
    }
}

void CMapListTemp::Destroy()
{
    if (list)
        CMapListNode::DestroyList(list);
}

// --- CParkGenerator ---------------------------------------------------------------------------------------------

namespace {

uint8_t* SceneOf(void* scene) { return static_cast<uint8_t*>(scene); }

} // namespace

void CParkGenerator::DestroyBody()
{
    if (mp_scene)
        mp_scene = nullptr;
    if (mp_sourceScene)
        mp_sourceScene = nullptr;
    // (the node array's owner: 0x00407810 is CAsset's destructor body, folded with CNodeArrayAsset's)
    reinterpret_cast<Ass::CAsset*>(m_nodeArray)->Destruct();
    DestroyEmptyMember(&m_rails);
    DestroyEmptyMember(&m_sourceRails);
}

void CParkGenerator::HighlightAllPieces(bool on)
{
    for (CPiece* piece = mp_pieces; piece;) {
        CPiece* next = piece->mp_next;
        piece->SetHighlight(on, false);
        piece = next;
    }
}

void CParkGenerator::SetMaxPlayers(int count) { m_maxPlayers = count; }

int CParkGenerator::GetMaxPlayersPossible()
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

CSourcePiece* CParkGenerator::GetPieceSource(uint32_t name)
{
    for (CSourcePiece* source = mp_sources; source; source = source->mp_next) {
        if (source->m_name == name)
            return source;
    }
    return nullptr;
}

bool CParkGenerator::DeleteSector(uint32_t name, int /*unused*/)
{
    return Scene::DeleteSector(SceneOf(mp_scene), name);
}

void CParkGenerator::DestroyRails()
{
    m_rails.Clear();
    m_rails.FreePools();
}

void CParkGenerator::SetLights(int count, float ambientR, float ambientG, float ambientB, float falloffR,
                               float falloffG, float falloffB, float cursorAmbience)
{
    m_numLights = count;
    m_ambient[0] = ambientR;
    m_ambient[1] = ambientG;
    m_ambient[2] = ambientB;
    m_falloff[0] = falloffR;
    m_falloff[1] = falloffG;
    m_falloff[2] = falloffB;
    std::memcpy(&m_43C, &cursorAmbience, 4); // (the cursor's ambience)
}

void CParkGenerator::GetRestartSlots(int type, int* first, int* last)
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
    case RestartPoint::kKingOfTheHill:
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

bool CParkGenerator::HasFreeRestartSlot(int type)
{
    int first = 0;
    int last = 0;
    GetRestartSlots(type, &first, &last);
    for (int i = first; i <= last; ++i) {
        if (!m_restarts[i].m_type || m_restarts[i].m_automatic)
            return true;
    }
    return false;
}

void CParkGenerator::ClearRestarts()
{
    for (RestartPoint& slot : m_restarts)
        slot.m_type = RestartPoint::kFree;
}

void CParkGenerator::ClearAutomaticRestarts()
{
    for (RestartPoint& slot : m_restarts) {
        if (slot.m_automatic)
            slot.m_type = RestartPoint::kFree;
    }
}

void CParkGenerator::SetUpBounds()
{
    // The most the park can take up: 56 x 56 cells, 16 steps down and 66 up.
    Mth::BBox box;
    Mth::Vector low = {-3360.0f, -768.0f, -3360.0f, 1.0f};
    box.AddPoint(low);
    Mth::Vector high = {3360.0f, 3168.0f, 3360.0f, 1.0f};
    box.AddPoint(high);
    Scene::SetBoundingBox(SceneOf(mp_scene), &box);
}

void CParkGenerator::RebuildCollision()
{
    if (!RebuildsCollision())
        return;
    m_flags &= ~CParkGenerator::kSceneChanged;
    Mem::PushContext(Mem::BottomUpHeap());
    Scene::UpdateSuperSectors(SceneOf(mp_scene), nullptr);
    Mem::PopContext();
    m_flags |= CParkGenerator::kSuperSectorsUpdated;
}

void CParkGenerator::UpdateBounds(uint32_t /*unused*/)
{
    CParkManager* manager = CParkManager::sInstance();
    uint8_t* shell = Scene::GetScene(manager->GetShellChecksum());
    if (!shell)
        return;
    // The shell's first box: as big as can be.
    Mth::Vector far = {200000.0f, 200000.0f, 200000.0f, 1.0f};
    Mth::Vector near = {-200000.0f, -200000.0f, -200000.0f, 1.0f};
    Mth::Line everything(near, far);
    Scene::SetBox(shell, &everything, 1, 0);
    // The second: the park's bounds, from the middle of its first cell to the middle of its last, every height.
    GridDims area;
    area.Set(0, 0, 0, 0, 0, 0);
    area = CParkManager::sInstance()->m_parkBounds;
    Mth::Vector corner = CParkManager::sInstance()->GridCoordinatesToWorld(area, nullptr);
    area.x = static_cast<uint8_t>(area.x + (area.w - 1));
    area.z = static_cast<uint8_t>(area.z + (area.l - 1));
    Mth::Vector farCorner = CParkManager::sInstance()->GridCoordinatesToWorld(area, nullptr);
    corner.x = static_cast<float>(static_cast<double>(corner.x) + 60.0);
    corner.y = -100000.0f;
    corner.z = static_cast<float>(static_cast<double>(corner.z) + 60.0);
    farCorner.y = 100000.0f;
    farCorner.x = static_cast<float>(static_cast<double>(farCorner.x) + 60.0);
    farCorner.z = static_cast<float>(static_cast<double>(farCorner.z) + 60.0);
    Mth::Line park(corner, farCorner);
    Scene::SetBox(shell, &park, 0, 0);
}

void CParkGenerator::SetPiecesHighlighted(int on)
{
    for (CPiece* piece = mp_pieces; piece;) {
        CPiece* next = piece->mp_next;
        if (piece->m_flags & CPiece::kGap)
            piece->SetVisible(on != 0);
        piece = next;
    }
}

void CParkGenerator::SetRestartPiecesActive(bool active)
{
    for (CPiece* piece = mp_pieces; piece;) {
        CPiece* next = piece->mp_next;
        if (CParkManager::sInstance()->GetRestartIndex(piece->mp_source->m_name))
            piece->SetActive(active);
        piece = next;
    }
}

uint32_t CParkGenerator::CloneSector(uint32_t name, int a, int b, int /*unused*/)
{
    m_flags |= CParkGenerator::kSceneChanged;
    Mem::PushContext(Mem::BottomUpHeap());
    uint32_t clone = Scene::CloneSector(SceneOf(mp_sourceScene), name, SceneOf(mp_scene), a, b, 0);
    Mem::PopContext();
    return clone;
}

bool CParkGenerator::DestroySector(uint32_t name, int /*unused*/)
{
    bool deleted = Scene::DeleteSector(SceneOf(mp_scene), name);
    RebuildCollision();
    return deleted;
}

CPiece* CParkGenerator::CreatePiece(CPiece* source, uint32_t flags)
{
    flags &= CPiece::kCursor | CPiece::kNoNode20 | CPiece::kNoRails | CPiece::kDestroyNow;
    CSourcePiece* from = (source->m_flags & CPiece::kSource) ? reinterpret_cast<CSourcePiece*>(source) : source->mp_source;
    // The source's sector copied into the park's scene (on the bottom-up heap).
    CloningPiece() = 1;
    const uint32_t name = from->m_name;
    m_flags |= CParkGenerator::kSceneChanged;
    Mem::PushContext(Mem::BottomUpHeap());
    uint32_t clone = (flags & CPiece::kCursor)
                         ? Scene::CloneSector(SceneOf(mp_sourceScene), name, SceneOf(mp_scene), 1, 0, 0)
                         : Scene::CloneSector(SceneOf(mp_sourceScene), name, SceneOf(mp_scene), 0, 1, 0);
    Mem::PopContext();
    CloningPiece() = 0;
    // The piece itself on the park editor's heap.
    Mem::PushContext(GetParkHeap());
    void* memory = Mem::New(sizeof(CClonedPiece));
    CClonedPiece* piece = memory ? static_cast<CClonedPiece*>(memory)->Construct() : nullptr;
    Mem::PopContext();
    piece->m_sector = clone; // (out of memory crashes, as in the original)
    piece->m_id = m_nextPieceId++;
    piece->mp_source = from;
    piece->SetRot(0, 0);
    piece->m_flags |= flags;
    uint32_t rgba;
    if (GetPieceColor(piece, &rgba)) { // (never)
        if (uint8_t* sector = Scene::GetSector(clone)) {
            if (uint8_t* geometry = Scene::GetGeometry(sector))
                Scene::SetColor(geometry, rgba);
        }
    }
    SetCountsDirty();
    m_flags |= CParkGenerator::kPiecesCreated;
    return piece;
}

void CParkGenerator::AddPiece(CPiece* piece)
{
    piece->m_flags |= CPiece::kInList;
    piece->mp_next = mp_pieces;
    mp_pieces = piece;
    m_flags |= CParkGenerator::kHasPieces;
}

void CParkGenerator::DeletePiece(CPiece* piece, int keepSector)
{
    if (!(piece->m_flags & CPiece::kCursor)) {
        CPiece* previous = nullptr;
        for (CPiece* p = mp_pieces; p; previous = p, p = p->mp_next) {
            if (p != piece)
                continue;
            if (previous)
                previous->mp_next = p->mp_next;
            else
                mp_pieces = p->mp_next;
            break;
        }
    }
    if (!keepSector)
        DestroySector(piece->m_sector, static_cast<int>(piece->m_flags));
    SetCountsDirty();
    piece->Delete();
    if (!mp_pieces)
        m_flags &= ~CParkGenerator::kHasPieces;
}

void CParkGenerator::ReadPieceFlags(Script::CArray* nodes)
{
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        auto* node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(i));
        uint32_t nodeClass = 0;
        node->GetChecksum(0x12B4E660u, &nodeClass, 0); // Class
        if (nodeClass != 0xBF4D3536u && nodeClass != 0xEF59C100u) // LevelGeometry, GameObject
            continue;
        uint32_t name = 0;
        node->GetChecksum(0xA1DC81F9u, &name, 0); // name
        if (!name)
            continue;
        CSourcePiece* source = GetPieceSource(name);
        if (!source)
            continue;
        if (nodeClass == 0xEF59C100u) {
            source->m_flags |= CSourcePiece::kGameObject;
            if (node->ContainsFlag(0x68910AC6u))
                source->m_flags |= CSourcePiece::kAbsentInNetGames;
        }
        uint32_t script = 0;
        node->GetChecksum(0x2CA8A299u, &script, 0); // TriggerScript
        if (!script)
            continue;
        source->m_triggerScript = script;
        switch (script) { // (four scripts that change their pieces: rebuilt by RebuildMarked)
        case 0x3DEDAFE9u:
        case 0x22C20ED8u:
        case 0x28673697u:
        case 0xF6187E48u:
            source->m_flags |= CSourcePiece::kRebuilt;
            break;
        default:
            break;
        }
    }
}

} // namespace Ed

using Ed::CMapListNode;
using Ed::CMapListTemp;
using Ed::CParkGenerator;

REPLACE(0x00590470, CMapListNode::VerifyIntegrity);
REPLACE(0x00591450, &CMapListTemp::PrintContents);
REPLACE(0x005914B0, CMapListNode::Free);
REPLACE(0x00591BE0, CMapListNode::DestroyList);
REPLACE(0x00591C60, &CMapListTemp::Destroy);
REPLACE(0x00591C70, &CParkGenerator::DestroyBody);
REPLACE(0x00591CF0, &CParkGenerator::HighlightAllPieces);
REPLACE(0x00591D20, &CParkGenerator::SetMaxPlayers);
REPLACE(0x00591D30, &CParkGenerator::GetMaxPlayersPossible);
REPLACE(0x00591D90, &CParkGenerator::GetPieceSource);
REPLACE(0x00591DD0, &CParkGenerator::DeleteSector);
REPLACE(0x00591DE0, &CParkGenerator::DestroyRails);
REPLACE(0x00591E00, &CParkGenerator::SetLights);
REPLACE(0x00591E60, &CParkGenerator::GetRestartSlots);
REPLACE(0x00591F50, &CParkGenerator::HasFreeRestartSlot);
REPLACE(0x00591FC0, &CParkGenerator::ClearRestarts);
REPLACE(0x00591FE0, &CParkGenerator::ClearAutomaticRestarts);
REPLACE(0x00592010, &CParkGenerator::SetUpBounds);
REPLACE(0x00592090, &CParkGenerator::RebuildCollision);
REPLACE(0x005920D0, &CParkGenerator::UpdateBounds);
REPLACE(0x00592290, &CParkGenerator::SetPiecesHighlighted);
REPLACE(0x005922D0, &CParkGenerator::SetRestartPiecesActive);
REPLACE(0x00592320, &CParkGenerator::CloneSector);
REPLACE(0x00592370, &CParkGenerator::DestroySector);
REPLACE(0x005923C0, &CParkGenerator::CreatePiece);
REPLACE(0x00592550, &CParkGenerator::AddPiece);
REPLACE(0x00592570, &CParkGenerator::DeletePiece);
REPLACE(0x005925E0, &CParkGenerator::ReadPieceFlags);
