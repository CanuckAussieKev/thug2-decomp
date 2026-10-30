// The park generator's scene: the source pieces (the "sk5ed" scene's sectors), the placed pieces cloned from them
// into the "cloned" scene and lit by the park's lights, the scene rebuilt, and the restart points. The data is laid
// out in park_scene.h; the node array the park's level runs on is park_nodes.cpp.

#include "game/ed/park_scene.h"

#include "game/ass/asset.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/park.h"
#include "game/ed/park_nx.h"
#include "game/ed/rail_editor.h"
#include "game/gfx/engine_geom.h"
#include "game/lst/hashtable.h"
#include "game/nx/coll_tri.h"
#include "game/park/park_memory.h"
#include "game/script/nodes.h"
#include "game/ed/park_editor.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// The super sectors are brought up to date when sectors are deleted (0x0067F398; off while a rebuild runs).
bool& SceneUpdates() { return g_collisionAllowed; }

// The engine's scenes and sectors (Nx).
void* GetScene(const char* name) { return Nx::GetSceneByName(name); }
void* CloneScene(const char* name, void* textures, int flag)
{
    return Nx::NewScene(name, static_cast<Nx::CTexDict*>(textures), flag != 0);
}
void* GetSector(uint32_t name) { return Nx::Engine::GetSector(name); }
// CScene: a sector deleted (0x0049CF80), the sectors flagged for it deleted (0x0049D030), the super sectors updated
// (0x0049D780), a sector cloned into `scene` (0x0049DF30; returns the clone's name)
void DeleteSector(void* scene, uint32_t name) { static_cast<Nx::CScene*>(scene)->DeleteSectorNamed(name); }
void DeleteMarkedSectors(void* scene) { static_cast<Nx::CScene*>(scene)->DeleteMarkedSectors(); }
void UpdateSuperSectorsOf(void* scene) { static_cast<Nx::CScene*>(scene)->UpdateSuperSectors(nullptr); }
uint32_t CloneSector(void* source, uint32_t name, void* scene, int cursor, int notCursor)
{
    return static_cast<Nx::CScene*>(source)->CloneSectorNamed(name, static_cast<Nx::CScene*>(scene), cursor,
                                                              notCursor != 0, 0);
}
// CSector: placed (0x0049E6C0), turned (0x0049E760), its box (0x0049E6A0: min, max), its geometry (+0x14, 0x004A2BA0)
void SetWorldPosition(void* sector, const Mth::Vector* position)
{
    static_cast<Nx::CSector*>(sector)->SetWorldPosition(position);
}
void SetYRotation(void* sector, int rot) { static_cast<Nx::CSector*>(sector)->SetYRotation(rot); }
const Mth::Vector* GetBBox(void* sector)
{
    return reinterpret_cast<const Mth::Vector*>(static_cast<Nx::CSector*>(sector)->GetBoundingBox());
}
void* GetGeometry(void* sector) { return static_cast<Nx::CSector*>(sector)->mp_geom; }
// The geometry's vertices (virtual calls through its vtable): how many (slot 36, 0x0048DF70), their positions
// (slot 37), their colours set (slot 40) and read (slot 38, 0x0049F3E0), its collision (the word at +8, 0x0049E810)
int VertexCount(void* geometry)
{
    return Scene::GeomSlot<int(__thiscall*)(void*)>(static_cast<uint8_t*>(geometry), 36)(geometry);
}
void GetVertexPositions(void* geometry, Mth::Vector* out)
{
    Scene::GeomSlot<void(__thiscall*)(void*, Mth::Vector*)>(static_cast<uint8_t*>(geometry), 37)(geometry, out);
}
void SetVertexColours(void* geometry, uint32_t* colours)
{
    Scene::GeomSlot<void(__thiscall*)(void*, uint32_t*)>(static_cast<uint8_t*>(geometry), 40)(geometry, colours);
}
void GetVertexColours(void* geometry, uint32_t* out)
{
    Scene::GeomSlot<void(__thiscall*)(void*, uint32_t*)>(static_cast<uint8_t*>(geometry), 38)(geometry, out);
}
Nx::CCollObjTriData* GetCollision(void* geometry)
{
    return static_cast<Nx::CCollObjTriData*>(static_cast<Nx::CGeom*>(geometry)->mp_collTriData);
}

// The rail editor (0x004F4970): its rails taken out of the scene (0x004F45C0, 0x004F45E0), put back (0x004F6650).
CRailEditorComponent* RailEditor() { return CRailEditorComponent::Instance(); }
void RailEditorRemoveA() { RailEditor()->DeleteAllSegments(); }
void RailEditorRemoveB() { RailEditor()->DeleteAllPosts(); }
void RailEditorRebuild() { RailEditor()->Rebuild(); }

// The generator's code in park_generator.cpp: a piece made from a source, a piece taken out and deleted (its sector
// too unless `keepSector`), the restart slots of a type, the bounds, the source pieces' flags from the pieces' node
// array.
ClonedPiece* NewPiece(CParkGenerator* generator, SourcePiece* source, uint32_t flags)
{
    return generator->CreatePiece(source->AsPiece(), flags);
}
void DestroyPieceNow(CParkGenerator* generator, ClonedPiece* piece, int keepSector) { generator->DeletePiece(piece, keepSector); }
void RestartSlots(CParkGenerator* generator, int type, int* first, int* last) { generator->GetRestartSlots(type, first, last); }
void SetUpBounds(CParkGenerator* generator) { generator->SetUpBounds(); }
void ReadPieceFlags(CParkGenerator* generator, Script::CArray* nodes) { generator->ReadPieceFlags(nodes); }
SourcePiece* ConstructSourcePiece(void* memory) { return static_cast<SourcePiece*>(memory)->Construct(); }
// The gap manager: a gap piece placed
void PlaceGapPiece(ClonedPiece* piece) { CGapManager::Instance()->SetTriggerLength(piece); }

// Virtual slot 1 of the pieces: the scalar deleting destructor.
template <typename T>
void DeleteObject(T* object)
{
    reinterpret_cast<void(__thiscall*)(T*, int)>(object->m_vtable[1])(object, 1);
}

// The gap's trigger piece type.
constexpr uint32_t kGapPiece = 0xC0341DF7;
// The park editor's own level (its rails on the top-down heap).
constexpr uint32_t kParkEditorLevel = 0xCD3682C1;
// The lighting: 1 / (160000 + d^2) for each light, times 255 (0x0064F478, 0x00646B88).
constexpr double kLightDistance = 160000.0;
// A source piece's size in cells: (max - min + 2) * (float)(1 / 120) (0x00645448, 0x0064F630), at least 1.
constexpr double kSizeSlack = 2.0;
constexpr float kInverseCell = 0.008333333767950535f;

// The super sectors brought up to date (on the bottom-up heap).
void UpdateSuperSectors(CParkGenerator& d)
{
    d.m_flags &= ~CParkGenerator::kSceneChanged;
    Mem::PushContext(Mem::BottomUpHeap());
    UpdateSuperSectorsOf(d.mp_scene);
    Mem::PopContext();
    d.m_flags |= CParkGenerator::kSuperSectorsUpdated;
}

// Take `piece` out of the list (unless it's the cursor's), delete its sector and the piece (inline in the original, as
// 0x00592570 does).
void DeletePiece(CParkGenerator& d, ClonedPiece* piece)
{
    if (!(piece->m_flags & ClonedPiece::kCursor)) {
        ClonedPiece* previous = nullptr;
        for (ClonedPiece* p = d.mp_pieces; p; previous = p, p = p->mp_next) {
            if (p == piece) {
                if (previous)
                    previous->mp_next = p->mp_next;
                else
                    d.mp_pieces = p->mp_next;
                break;
            }
        }
    }
    DeleteSector(d.mp_scene, piece->m_sector);
    if (SceneUpdates())
        UpdateSuperSectors(d);
    SetCountsDirty(); // (given the piece)
    DeleteObject(piece);
    if (!d.mp_pieces)
        d.m_flags &= ~CParkGenerator::kHasPieces;
}

} // namespace

CParkGenerator* CParkGenerator::Construct()
{
    m_sourceRails.Construct();
    m_rails.Construct();
    for (RestartPoint& restart : m_restarts)
        restart.m_area.Set(0, 0, 0, 0, 0, 0);
    reinterpret_cast<Ass::AssetConstructors*>(m_nodeArray)->NodeArray();
    m_flags = 0;
    m_04 = 0;
    mp_pieces = nullptr;
    mp_sources = nullptr;
    mp_scene = nullptr;
    mp_sourceScene = nullptr;
    m_nextPieceId = 0x20;
    m_maxPlayers = 2;
    return this;
}

void CParkGenerator::SetLightPosition(const Mth::Vector* position, int index)
{
    Mth::Vector& light = m_lights[index];
    light.x = position->x;
    light.y = position->y;
    light.z = position->z;
    light.w = position->w;
}

void CParkGenerator::CalculateLighting(const Mth::Vector* position, uint8_t* rgba)
{
    const float red = static_cast<float>(rgba[0] * static_cast<double>(m_ambient[0]));
    const float green = static_cast<float>(rgba[1] * static_cast<double>(m_ambient[1]));
    const double blue = rgba[2] * static_cast<double>(m_ambient[2]); // (kept unrounded)
    double sum = 0.0;
    for (int i = 0; i < m_numLights; ++i) {
        const double dx = static_cast<double>(position->x) - m_lights[i].x;
        const double dz = static_cast<double>(position->z) - m_lights[i].z;
        sum += 1.0 / (dz * dz + dx * dx + kLightDistance);
    }
    sum *= 255.0;
    // (over 255 is 255; the original's _ftol otherwise, NaN included)
    auto channel = [](double value) {
        return value > 255.0 ? static_cast<uint8_t>(0xFF) : static_cast<uint8_t>(static_cast<int>(value));
    };
    rgba[0] = channel(sum * m_falloff[0] + red);
    rgba[1] = channel(sum * m_falloff[1] + green);
    rgba[2] = channel(sum * m_falloff[2] + blue);
}

void CParkGenerator::LightPiece(CPiece* p)
{
    ClonedPiece* piece = Cloned(p);
    void* geometry = GetGeometry(GetSector(piece->m_sector));
    void* sourceGeometry = GetGeometry(GetSector(piece->mp_source->m_name));
    int count = VertexCount(geometry);
    if (count) {
        // (new[] of vectors and colours: their constructors do nothing)
        auto* positions = static_cast<Mth::Vector*>(Mem::New(count * sizeof(Mth::Vector)));
        auto* colours = static_cast<uint32_t*>(Mem::New(count * sizeof(uint32_t)));
        GetVertexPositions(geometry, positions);
        GetVertexColours(sourceGeometry, colours);
        for (int i = 0; i < count; ++i)
            CalculateLighting(&positions[i], reinterpret_cast<uint8_t*>(&colours[i]));
        SetVertexColours(geometry, colours);
        Mem::Free(positions);
        Mem::Free(colours);
    }
    // the collision's vertex intensities, from the source's (grey)
    Nx::CCollObjTriData* collision = GetCollision(geometry);
    Nx::CCollObjTriData* sourceCollision = GetCollision(sourceGeometry);
    int vertices = collision->m_numVertices;
    for (int i = 0; i < vertices; ++i) {
        uint8_t grey = sourceCollision->m_vertexBytes[i];
        uint8_t rgba[4] = {grey, grey, grey, 0xFF};
        Mth::Vector position = collision->GetRawVertexPos(i);
        CalculateLighting(&position, rgba);
        collision->m_vertexBytes[i] = static_cast<uint8_t>((rgba[0] + rgba[1] + rgba[2]) / 3);
    }
}

bool CParkGenerator::AddRestart(Mth::Vector* position, int rot, GridDims* area, int type, int automatic,
                                uint32_t keepPlaced)
{
    int first = 0, last = 0;
    RestartSlots(this, type, &first, &last);
    Mth::Vector pos = *position;
    GridDims cells = *area;
    const uint8_t isAutomatic = static_cast<uint8_t>(automatic);
    if (isAutomatic && static_cast<uint8_t>(keepPlaced)) {
        if (first > last)
            return false;
        for (int i = first; i <= last; ++i) {
            const RestartPoint& restart = m_restarts[i];
            if (restart.m_type == type && !restart.m_automatic) {
                pos = restart.m_pos;
                cells = restart.m_area;
            }
        }
    }
    for (int i = first; i <= last; ++i) {
        RestartPoint& restart = m_restarts[i];
        if (restart.m_type == RestartPoint::kFree || (restart.m_automatic && !isAutomatic)) {
            restart.m_pos = pos;
            restart.m_rot = rot;
            restart.m_area = cells;
            restart.m_type = type;
            restart.m_automatic = isAutomatic;
            return true;
        }
    }
    return false;
}

int CParkGenerator::CountRestarts(int type)
{
    int first = 0, last = 0;
    RestartSlots(this, type, &first, &last);
    int count = 0;
    for (int i = first; i <= last; ++i) {
        if (m_restarts[i].m_type)
            ++count;
    }
    return count;
}

bool CParkGenerator::NeedsRestarts(int type, int count) { return CountRestarts(type) < count; }

void CParkGenerator::RemoveRestart(GridDims* area, int player)
{
    int first = 0, last = 0;
    RestartSlots(this, player, &first, &last);
    for (int i = first; i <= last; ++i) {
        RestartPoint& restart = m_restarts[i];
        if (restart.m_area.x == area->x && restart.m_area.y == area->y && restart.m_area.z == area->z)
            restart.m_type = RestartPoint::kFree;
    }
}

void* CParkGenerator::CreateGapPiece(Mth::Vector* position, float /*length*/, int rot)
{
    SourcePiece* source = nullptr;
    for (SourcePiece* s = mp_sources; s; s = s->mp_next) {
        if (s->m_name == kGapPiece) {
            source = s;
            break;
        }
    }
    ClonedPiece* piece = NewPiece(this, source, 0);
    reinterpret_cast<CPiece*>(piece)->SetPosition(position, 1);
    reinterpret_cast<CPiece*>(piece)->SetRot(rot, 1);
    piece->m_flags |= ClonedPiece::kGap | ClonedPiece::kInList;
    piece->mp_next = mp_pieces;
    mp_pieces = piece;
    m_flags |= CParkGenerator::kHasPieces;
    return piece;
}

void CParkGenerator::DestroyPieces(int keepRails)
{
    if (!keepRails) {
        RailEditorRemoveA();
        RailEditorRemoveB();
    }
    for (ClonedPiece* piece = mp_pieces; piece;) {
        ClonedPiece* next = piece->mp_next;
        DestroyPieceNow(this, piece, keepRails);
        piece = next;
    }
    if (!keepRails)
        m_flags &= ~CParkGenerator::kPiecesCreated;
    m_flags &= ~CParkGenerator::kHasPieces;
    if (!keepRails)
        DeleteMarkedSectors(mp_scene);
}

void CParkGenerator::DestroyPiece(void* p)
{
    auto* piece = static_cast<ClonedPiece*>(p);
    uint32_t flags = piece->m_flags;
    piece->m_flags = flags | ClonedPiece::kDestroy;
    if (flags & ClonedPiece::kDestroyNow)
        DestroyPieceNow(this, piece, 0);
}

void CParkGenerator::CleanUpOutRefs()
{
    for (ClonedPiece* piece = mp_pieces; piece;) {
        ClonedPiece* next = piece->mp_next;
        if (piece->m_flags & ClonedPiece::kDestroy)
            Ed::DeletePiece(*this, piece);
        piece = next;
    }
}

void CParkGenerator::UnloadPieces()
{
    if (m_flags & CParkGenerator::kHasPieces) {
        RailEditorRemoveA();
        RailEditorRemoveB();
        for (ClonedPiece* piece = mp_pieces; piece;) {
            ClonedPiece* next = piece->mp_next;
            Ed::DeletePiece(*this, piece);
            piece = next;
        }
        m_flags &= ~(CParkGenerator::kPiecesCreated | CParkGenerator::kHasPieces);
        DeleteMarkedSectors(mp_scene);
    }
    for (SourcePiece* source = mp_sources; source;) {
        SourcePiece* next = source->mp_next;
        DeleteObject(source);
        source = next;
    }
    mp_sources = nullptr;
    m_flags &= ~CParkGenerator::kSourcesLoaded;
}

void CParkGenerator::AddPieceToScene(CPiece* p)
{
    ClonedPiece* piece = Cloned(p);
    m_flags |= CParkGenerator::kSceneChanged;
    const int cursor = (piece->m_flags & ClonedPiece::kCursor) ? 1 : 0;
    piece->m_sector = Ed::CloneSector(mp_sourceScene, piece->mp_source->m_name, mp_scene, cursor, !cursor);
    void* sector = GetSector(piece->m_sector);
    Mth::Vector position = piece->m_pos;
    SetWorldPosition(sector, &position);
    SetYRotation(sector, piece->m_rot);
    if (piece->m_flags & ClonedPiece::kGap)
        PlaceGapPiece(piece);
    LightPiece(p);
}

void CParkGenerator::RebuildAll()
{
    SceneUpdates() = false;
    for (ClonedPiece* piece = mp_pieces; piece; piece = piece->mp_next)
        Ed::DeleteSector(mp_scene, piece->m_sector);
    RailEditorRemoveA();
    RailEditorRemoveB();
    CParkManager::sInstance(); // (the result unused)
    RemoveNodeArray();          // (inline in the original)
    SceneUpdates() = true;
    DeleteMarkedSectors(mp_scene);
    for (ClonedPiece* piece = mp_pieces; piece; piece = piece->mp_next)
        AddPieceToScene(reinterpret_cast<CPiece*>(piece));
    if (SceneUpdates())
        UpdateSuperSectors(*this);
    SceneUpdates() = false;
    RailEditorRebuild();
    SceneUpdates() = true;
    UpdateSuperSectors(*this);
    MarkProgress("CleanMainHeap - post cleanup");
}

void CParkGenerator::RebuildMarked()
{
    SceneUpdates() = false;
    for (ClonedPiece* piece = mp_pieces; piece; piece = piece->mp_next) {
        if (piece->mp_source->m_flags & SourcePiece::kRebuilt) {
            Ed::DeleteSector(mp_scene, piece->m_sector);
            if (SceneUpdates())
                UpdateSuperSectors(*this);
        }
    }
    for (ClonedPiece* piece = mp_pieces; piece; piece = piece->mp_next) {
        if (piece->mp_source->m_flags & SourcePiece::kRebuilt)
            AddPieceToScene(reinterpret_cast<CPiece*>(piece));
    }
    SceneUpdates() = true;
    UpdateSuperSectors(*this);
}

void CParkGenerator::CleanUp(int /*type*/)
{
    UnloadPieces();
    m_sourceRails.Clear();
    m_sourceRails.FreePools();
}

void CParkGenerator::LoadSourcePieces(int /*unused*/)
{
    if (m_flags & CParkGenerator::kSourcesLoaded)
        UnloadPieces();
    m_flags |= CParkGenerator::kSourcesLoaded;
    m_nextPieceId = 0x20;
    mp_sourceScene = GetScene("sk5ed");
    // every sector of the scene (its sector table at +0xD0)
    auto* sectors = *reinterpret_cast<Lst::HashTable**>(static_cast<uint8_t*>(mp_sourceScene) + 0xD0);
    sectors->m_iterBucket = -1;
    sectors->m_iterItem = nullptr;
    void* sector;
    while (sectors->IterateNext() && sectors->m_iterItem && (sector = sectors->m_iterItem->value) != nullptr) {
        Mem::PushContext(GetParkHeap());
        void* memory = Mem::New(sizeof(SourcePiece));
        SourcePiece* source = memory ? ConstructSourcePiece(memory) : nullptr;
        Mem::PopContext();
        source->mp_next = mp_sources;
        mp_sources = source;
        source->m_name = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(sector) + 4); // its checksum
        const Mth::Vector* box = GetBBox(sector); // min, max
        const Mth::Vector& min = box[0];
        const Mth::Vector& max = box[1];
        int cells = static_cast<int>((static_cast<double>(max.x) - min.x + kSizeSlack) * kInverseCell);
        if (cells < 1)
            cells = 1;
        source->m_size.x = static_cast<float>(cells * static_cast<double>(kCellWidth));
        cells = static_cast<int>((static_cast<double>(max.z) - min.z + kSizeSlack) * kInverseCell);
        if (cells < 1)
            cells = 1;
        source->m_size.z = static_cast<float>(cells * static_cast<double>(kCellWidth));
        const double height = static_cast<double>(max.y) - min.y;
        source->m_size.y = static_cast<float>(height);
        if (height < kCellHeight && height > 0.0)
            source->m_size.y = kCellHeight; // (at least a step high)
    }
    mp_scene = CloneScene("cloned", *reinterpret_cast<void**>(static_cast<uint8_t*>(mp_sourceScene) + 0x88), 1);
    Ed::SetUpBounds(this);
    for (RestartPoint& restart : m_restarts)
        restart.m_type = RestartPoint::kFree;
}

void CParkGenerator::LoadPieces(uint32_t level, int unused)
{
    LoadSourcePieces(unused);
    Script::CArray* nodes = SkateScript::GetLastNodeArray();
    Ed::ReadPieceFlags(this, nodes);
    if (level == kParkEditorLevel) {
        Mem::PushContext(Mem::TopDownHeap());
        BuildSourceRails(nodes);
        Mem::PopContext();
    } else {
        BuildSourceRails(nodes);
    }
}

} // namespace Ed

REPLACE(0x00593460, &Ed::CParkGenerator::Construct);
REPLACE(0x00592FE0, &Ed::CParkGenerator::SetLightPosition);
REPLACE(0x00593010, &Ed::CParkGenerator::CalculateLighting);
REPLACE(0x00594410, &Ed::CParkGenerator::LightPiece);
REPLACE(0x00593200, &Ed::CParkGenerator::AddRestart);
REPLACE(0x00593370, &Ed::CParkGenerator::CountRestarts);
REPLACE(0x005933D0, &Ed::CParkGenerator::NeedsRestarts);
REPLACE(0x005933F0, &Ed::CParkGenerator::RemoveRestart);
REPLACE(0x00593500, &Ed::CParkGenerator::CreateGapPiece);
REPLACE(0x00593580, &Ed::CParkGenerator::DestroyPieces);
REPLACE(0x005935F0, &Ed::CParkGenerator::DestroyPiece);
REPLACE(0x00593610, &Ed::CParkGenerator::CleanUpOutRefs);
REPLACE(0x00594610, &Ed::CParkGenerator::UnloadPieces);
REPLACE(0x00594730, &Ed::CParkGenerator::AddPieceToScene);
REPLACE(0x005947D0, &Ed::CParkGenerator::RebuildAll);
REPLACE(0x00594910, &Ed::CParkGenerator::RebuildMarked);
REPLACE(0x00594C70, &Ed::CParkGenerator::CleanUp);
REPLACE(0x00594C90, &Ed::CParkGenerator::LoadSourcePieces);
REPLACE(0x00594FD0, &Ed::CParkGenerator::LoadPieces);
