// The park generator's scene: the source pieces (the "sk5ed" scene's sectors), the placed pieces cloned from them
// into the "cloned" scene and lit by the park's lights, the scene rebuilt, and the restart points. The data is laid
// out in park_scene.h; the node array the park's level runs on is park_nodes.cpp.

#include <cstdint>

#include "game/park_editing/park_scene.h"

#include "engine/assets/resource_entry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_nx.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine_geom.h"
#include "engine/lists/key_map.h"
#include "engine/render/coll_tri.h"
#include "game/park/park_memory.h"
#include "engine/script/nodes.h"
#include "game/park_editing/park_editor.h"

namespace ParkEditing {

namespace {

// The super sectors are brought up to date when sectors are deleted (0x0067F398; off while a rebuild runs).
bool& SceneUpdates() { return g_collisionAllowed; }

// The engine's scenes and sectors (Nx).
void* LookUpScene(const char* name) { return Engine3D::GetSceneByName(name); }
void* CloneScene(const char* name, void* textures, int flag)
{
    return Engine3D::BuildWorldScene(name, static_cast<Engine3D::CTextureSet*>(textures), flag != 0);
}
void* LookUpSector(uint32_t name) { return Engine3D::Engine::LookUpSector(name); }
// CWorldScene: a sector deleted (0x0049CF80), the sectors flagged for it deleted (0x0049D030), the super sectors updated
// (0x0049D780), a sector cloned into `scene` (0x0049DF30; returns the clone's name)
void RemoveSceneSector(void* scene, uint32_t name) { static_cast<Engine3D::CWorldScene*>(scene)->DeleteSectorNamed(name); }
void DeleteMarkedSectors(void* scene) { static_cast<Engine3D::CWorldScene*>(scene)->DeleteMarkedSectors(); }
void UpdateSuperSectorsOf(void* scene) { static_cast<Engine3D::CWorldScene*>(scene)->UpdateSuperSectors(nullptr); }
uint32_t DuplicateSceneSector(void* source, uint32_t name, void* scene, int cursor, int notCursor)
{
    return static_cast<Engine3D::CWorldScene*>(source)->CloneSectorNamed(name, static_cast<Engine3D::CWorldScene*>(scene), cursor,
                                                              notCursor != 0, 0);
}
// CWorldCell: placed (0x0049E6C0), turned (0x0049E760), its box (0x0049E6A0: min, max), its geometry (+0x14, 0x004A2BA0)
void PlaceAtAbsolutePoint(void* sector, const Maths::Vec4* position)
{
    static_cast<Engine3D::CWorldCell*>(sector)->PlaceAtAbsolutePoint(position);
}
void SpinAroundUp(void* sector, int rot) { static_cast<Engine3D::CWorldCell*>(sector)->SpinAroundUp(rot); }
const Maths::Vec4* GetLocalBounds(void* sector)
{
    return reinterpret_cast<const Maths::Vec4*>(static_cast<Engine3D::CWorldCell*>(sector)->FetchBounds());
}
void* FetchShape(void* sector) { return static_cast<Engine3D::CWorldCell*>(sector)->m_geomHandle; }
// The geometry's vertices (virtual calls through its vtable): how many (slot 36, 0x0048DF70), their positions
// (slot 37), their colours set (slot 40) and read (slot 38, 0x0049F3E0), its collision (the word at +8, 0x0049E810)
int VertexCount(void* geometry)
{
    return Scene::GeomSlot<int(__thiscall*)(void*)>(static_cast<uint8_t*>(geometry), 36)(geometry);
}
void GetVertexPositions(void* geometry, Maths::Vec4* out)
{
    Scene::GeomSlot<void(__thiscall*)(void*, Maths::Vec4*)>(static_cast<uint8_t*>(geometry), 37)(geometry, out);
}
void SetVertexColours(void* geometry, uint32_t* colours)
{
    Scene::GeomSlot<void(__thiscall*)(void*, uint32_t*)>(static_cast<uint8_t*>(geometry), 40)(geometry, colours);
}
void GetVertexColours(void* geometry, uint32_t* out)
{
    Scene::GeomSlot<void(__thiscall*)(void*, uint32_t*)>(static_cast<uint8_t*>(geometry), 38)(geometry, out);
}
Engine3D::CTriMeshSource* GetHitShape(void* geometry)
{
    return static_cast<Engine3D::CTriMeshSource*>(static_cast<Engine3D::CGeom*>(geometry)->m_hitTriangles);
}

// The rail editor (0x004F4970): its rails taken out of the scene (0x004F45C0, 0x004F45E0), put back (0x004F6650).
CRailAuthoring* RailEditor() { return CRailAuthoring::Sole(); }
void RailEditorRemoveA() { RailEditor()->DeleteAllSegments(); }
void RailEditorRemoveB() { RailEditor()->DeleteAllPosts(); }
void RailEditorRebuild() { RailEditor()->Reassemble(); }

// The generator's code in park_generator.cpp: a piece made from a source, a piece taken out and deleted (its sector
// too unless `keepSector`), the restart slots of a type, the bounds, the source pieces' flags from the pieces' node
// array.
DuplicatedTile* MakeTileCopy(CParkAssembler* generator, TemplateTile* source, uint32_t flags)
{
    return generator->CreatePiece(source->AsPiece(), flags);
}
void DestroyPieceNow(CParkAssembler* generator, DuplicatedTile* piece, int keepSector) { generator->DeletePiece(piece, keepSector); }
void SpawnSlotArray(CParkAssembler* generator, int type, int* first, int* last) { generator->FetchSpawnSlots(type, first, last); }
void SetUpBounds(CParkAssembler* generator) { generator->SetUpBounds(); }
void ReadPieceFlags(CParkAssembler* generator, Scripts::CArray* nodes) { generator->ReadPieceFlags(nodes); }
TemplateTile* ConstructSourcePiece(void* memory) { return static_cast<TemplateTile*>(memory)->MakeNew(); }
// The gap manager: a gap piece placed
void PlaceGapPiece(DuplicatedTile* piece) { CGapRegistry::Sole()->SetTriggerLength(piece); }

// Virtual slot 1 of the pieces: the scalar deleting destructor.
template <typename T>
void DeleteObject(T* object)
{
    reinterpret_cast<void(__thiscall*)(T*, int)>(object->m_vtable[1])(object, 1);
}

// The gap's trigger piece type.
constexpr uint32_t kGapMarkerLabel = 0xC0341DF7;
// The park editor's own level (its rails on the top-down heap).
constexpr uint32_t kParkEditorLevel = 0xCD3682C1;
// The lighting: 1 / (160000 + d^2) for each light, times 255 (0x0064F478, 0x00646B88).
constexpr double kLightDistance = 160000.0;
// A source piece's size in cells: (max - min + 2) * (float)(1 / 120) (0x00645448, 0x0064F630), at least 1.
constexpr double kSizeSlack = 2.0;
constexpr float kInverseCell = 0.008333333767950535f;

// The super sectors brought up to date (on the bottom-up heap).
void UpdateSuperSectors(CParkAssembler& d)
{
    d.m_flagBits &= ~CParkAssembler::kSceneChanged;
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    UpdateSuperSectorsOf(d.m_sceneHandle);
    MemSys::LeaveHeapScope();
    d.m_flagBits |= CParkAssembler::kSuperSectorsUpdated;
}

// Take `piece` out of the list (unless it's the cursor's), delete its sector and the piece (inline in the original, as
// 0x00592570 does).
void DeletePiece(CParkAssembler& d, DuplicatedTile* piece)
{
    if (!(piece->m_flagBits & DuplicatedTile::kCursor)) {
        DuplicatedTile* previous = nullptr;
        for (DuplicatedTile* p = d.m_pieces; p; previous = p, p = p->m_following) {
            if (p == piece) {
                if (previous)
                    previous->m_following = p->m_following;
                else
                    d.m_pieces = p->m_following;
                break;
            }
        }
    }
    RemoveSceneSector(d.m_sceneHandle, piece->m_sector);
    if (SceneUpdates())
        UpdateSuperSectors(d);
    SetCountsDirty(); // (given the piece)
    DeleteObject(piece);
    if (!d.m_pieces)
        d.m_flagBits &= ~CParkAssembler::kHasPieces;
}

} // namespace

CParkAssembler* CParkAssembler::MakeNew()
{
    m_sourceRails.MakeNew();
    m_rails.MakeNew();
    for (RestartPoint& restart : m_restarts)
        restart.m_editSpan.AssignValue(0, 0, 0, 0, 0, 0);
    reinterpret_cast<AssetStore::AssetConstructors*>(m_nodeArray)->NodeArray();
    m_flagBits = 0;
    m_04 = 0;
    m_pieces = nullptr;
    m_sources = nullptr;
    m_sceneHandle = nullptr;
    m_sourceWorld = nullptr;
    m_nextPieceId = 0x20;
    m_maxPlayers = 2;
    return this;
}

void CParkAssembler::MoveLightSource(const Maths::Vec4* position, int index)
{
    Maths::Vec4& light = m_lights[index];
    light.x = position->x;
    light.y = position->y;
    light.z = position->z;
    light.w = position->w;
}

void CParkAssembler::SampleLightColour(const Maths::Vec4* position, uint8_t* rgba)
{
    const float red = static_cast<float>(rgba[0] * static_cast<double>(m_ambient[0]));
    const float green = static_cast<float>(rgba[1] * static_cast<double>(m_ambient[1]));
    const double blue = rgba[2] * static_cast<double>(m_ambient[2]); // (kept unrounded)
    double sum = 0.0;
    for (int i = 0; i < m_lightTotal; ++i) {
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

void CParkAssembler::LightPiece(CParkTile* p)
{
    DuplicatedTile* piece = Cloned(p);
    void* geometry = FetchShape(LookUpSector(piece->m_sector));
    void* sourceGeometry = FetchShape(LookUpSector(piece->m_blendSource->m_label));
    int count = VertexCount(geometry);
    if (count) {
        // (new[] of vectors and colours: their constructors do nothing)
        auto* positions = static_cast<Maths::Vec4*>(MemSys::Alloc(count * sizeof(Maths::Vec4)));
        auto* colours = static_cast<uint32_t*>(MemSys::Alloc(count * sizeof(uint32_t)));
        GetVertexPositions(geometry, positions);
        GetVertexColours(sourceGeometry, colours);
        for (int i = 0; i < count; ++i)
            SampleLightColour(&positions[i], reinterpret_cast<uint8_t*>(&colours[i]));
        SetVertexColours(geometry, colours);
        MemSys::Dealloc(positions);
        MemSys::Dealloc(colours);
    }
    // the collision's vertex intensities, from the source's (grey)
    Engine3D::CTriMeshSource* collision = GetHitShape(geometry);
    Engine3D::CTriMeshSource* sourceCollision = GetHitShape(sourceGeometry);
    int vertices = collision->m_numVertices;
    for (int i = 0; i < vertices; ++i) {
        uint8_t grey = sourceCollision->m_vertexBytes[i];
        uint8_t rgba[4] = {grey, grey, grey, 0xFF};
        Maths::Vec4 position = collision->ReadStoredVertex(i);
        SampleLightColour(&position, rgba);
        collision->m_vertexBytes[i] = static_cast<uint8_t>((rgba[0] + rgba[1] + rgba[2]) / 3);
    }
}

bool CParkAssembler::PlantRestartPoint(Maths::Vec4* position, int rot, SCellBox* area, int type, int automatic,
                                uint32_t keepPlaced)
{
    int first = 0, last = 0;
    SpawnSlotArray(this, type, &first, &last);
    Maths::Vec4 pos = *position;
    SCellBox cells = *area;
    const uint8_t isAutomatic = static_cast<uint8_t>(automatic);
    if (isAutomatic && static_cast<uint8_t>(keepPlaced)) {
        if (first > last)
            return false;
        for (int i = first; i <= last; ++i) {
            const RestartPoint& restart = m_restarts[i];
            if (restart.m_kindTag == type && !restart.m_automatic) {
                pos = restart.m_atPoint;
                cells = restart.m_editSpan;
            }
        }
    }
    for (int i = first; i <= last; ++i) {
        RestartPoint& restart = m_restarts[i];
        if (restart.m_kindTag == RestartPoint::kFree || (restart.m_automatic && !isAutomatic)) {
            restart.m_atPoint = pos;
            restart.m_quarterTurns = rot;
            restart.m_editSpan = cells;
            restart.m_kindTag = type;
            restart.m_automatic = isAutomatic;
            return true;
        }
    }
    return false;
}

int CParkAssembler::CountRestarts(int type)
{
    int first = 0, last = 0;
    SpawnSlotArray(this, type, &first, &last);
    int count = 0;
    for (int i = first; i <= last; ++i) {
        if (m_restarts[i].m_kindTag)
            ++count;
    }
    return count;
}

bool CParkAssembler::NeedsRestarts(int type, int count) { return CountRestarts(type) < count; }

void CParkAssembler::UnplantRestartPoint(SCellBox* area, int player)
{
    int first = 0, last = 0;
    SpawnSlotArray(this, player, &first, &last);
    for (int i = first; i <= last; ++i) {
        RestartPoint& restart = m_restarts[i];
        if (restart.m_editSpan.x == area->x && restart.m_editSpan.y == area->y && restart.m_editSpan.z == area->z)
            restart.m_kindTag = RestartPoint::kFree;
    }
}

void* CParkAssembler::MakeGapMarkerPiece(Maths::Vec4* position, float /*length*/, int rot)
{
    TemplateTile* source = nullptr;
    for (TemplateTile* s = m_sources; s; s = s->m_following) {
        if (s->m_label == kGapMarkerLabel) {
            source = s;
            break;
        }
    }
    DuplicatedTile* piece = MakeTileCopy(this, source, 0);
    reinterpret_cast<CParkTile*>(piece)->ChangePlace(position, 1);
    reinterpret_cast<CParkTile*>(piece)->SetQuarterTurns(rot, 1);
    piece->m_flagBits |= DuplicatedTile::kGap | DuplicatedTile::kInList;
    piece->m_following = m_pieces;
    m_pieces = piece;
    m_flagBits |= CParkAssembler::kHasPieces;
    return piece;
}

void CParkAssembler::DestroyPieces(int keepRails)
{
    if (!keepRails) {
        RailEditorRemoveA();
        RailEditorRemoveB();
    }
    for (DuplicatedTile* piece = m_pieces; piece;) {
        DuplicatedTile* next = piece->m_following;
        DestroyPieceNow(this, piece, keepRails);
        piece = next;
    }
    if (!keepRails)
        m_flagBits &= ~CParkAssembler::kPiecesCreated;
    m_flagBits &= ~CParkAssembler::kHasPieces;
    if (!keepRails)
        DeleteMarkedSectors(m_sceneHandle);
}

void CParkAssembler::DestroyPiece(void* p)
{
    auto* piece = static_cast<DuplicatedTile*>(p);
    uint32_t flags = piece->m_flagBits;
    piece->m_flagBits = flags | DuplicatedTile::kDestroy;
    if (flags & DuplicatedTile::kDestroyNow)
        DestroyPieceNow(this, piece, 0);
}

void CParkAssembler::CleanUpOutRefs()
{
    for (DuplicatedTile* piece = m_pieces; piece;) {
        DuplicatedTile* next = piece->m_following;
        if (piece->m_flagBits & DuplicatedTile::kDestroy)
            ParkEditing::DeletePiece(*this, piece);
        piece = next;
    }
}

void CParkAssembler::UnloadPieces()
{
    if (m_flagBits & CParkAssembler::kHasPieces) {
        RailEditorRemoveA();
        RailEditorRemoveB();
        for (DuplicatedTile* piece = m_pieces; piece;) {
            DuplicatedTile* next = piece->m_following;
            ParkEditing::DeletePiece(*this, piece);
            piece = next;
        }
        m_flagBits &= ~(CParkAssembler::kPiecesCreated | CParkAssembler::kHasPieces);
        DeleteMarkedSectors(m_sceneHandle);
    }
    for (TemplateTile* source = m_sources; source;) {
        TemplateTile* next = source->m_following;
        DeleteObject(source);
        source = next;
    }
    m_sources = nullptr;
    m_flagBits &= ~CParkAssembler::kSourcesLoaded;
}

void CParkAssembler::AddPieceToScene(CParkTile* p)
{
    DuplicatedTile* piece = Cloned(p);
    m_flagBits |= CParkAssembler::kSceneChanged;
    const int cursor = (piece->m_flagBits & DuplicatedTile::kCursor) ? 1 : 0;
    piece->m_sector = ParkEditing::DuplicateSceneSector(m_sourceWorld, piece->m_blendSource->m_label, m_sceneHandle, cursor, !cursor);
    void* sector = LookUpSector(piece->m_sector);
    Maths::Vec4 position = piece->m_atPoint;
    PlaceAtAbsolutePoint(sector, &position);
    SpinAroundUp(sector, piece->m_quarterTurns);
    if (piece->m_flagBits & DuplicatedTile::kGap)
        PlaceGapPiece(piece);
    LightPiece(p);
}

void CParkAssembler::RebuildAll()
{
    SceneUpdates() = false;
    for (DuplicatedTile* piece = m_pieces; piece; piece = piece->m_following)
        ParkEditing::RemoveSceneSector(m_sceneHandle, piece->m_sector);
    RailEditorRemoveA();
    RailEditorRemoveB();
    CParkLayout::sSingleton(); // (the result unused)
    RemoveNodeArray();          // (inline in the original)
    SceneUpdates() = true;
    DeleteMarkedSectors(m_sceneHandle);
    for (DuplicatedTile* piece = m_pieces; piece; piece = piece->m_following)
        AddPieceToScene(reinterpret_cast<CParkTile*>(piece));
    if (SceneUpdates())
        UpdateSuperSectors(*this);
    SceneUpdates() = false;
    RailEditorRebuild();
    SceneUpdates() = true;
    UpdateSuperSectors(*this);
    MarkProgress("CleanMainHeap - post cleanup");
}

void CParkAssembler::RebuildMarked()
{
    SceneUpdates() = false;
    for (DuplicatedTile* piece = m_pieces; piece; piece = piece->m_following) {
        if (piece->m_blendSource->m_flagBits & TemplateTile::kRebuilt) {
            ParkEditing::RemoveSceneSector(m_sceneHandle, piece->m_sector);
            if (SceneUpdates())
                UpdateSuperSectors(*this);
        }
    }
    for (DuplicatedTile* piece = m_pieces; piece; piece = piece->m_following) {
        if (piece->m_blendSource->m_flagBits & TemplateTile::kRebuilt)
            AddPieceToScene(reinterpret_cast<CParkTile*>(piece));
    }
    SceneUpdates() = true;
    UpdateSuperSectors(*this);
}

void CParkAssembler::SweepUp(int /*type*/)
{
    UnloadPieces();
    m_sourceRails.Clear();
    m_sourceRails.FreePools();
}

void CParkAssembler::LoadSourcePieces(int /*unused*/)
{
    if (m_flagBits & CParkAssembler::kSourcesLoaded)
        UnloadPieces();
    m_flagBits |= CParkAssembler::kSourcesLoaded;
    m_nextPieceId = 0x20;
    m_sourceWorld = LookUpScene("sk5ed");
    // every sector of the scene (its sector table at +0xD0)
    auto* sectors = *reinterpret_cast<ListKit::KeyMap**>(static_cast<uint8_t*>(m_sourceWorld) + 0xD0);
    sectors->m_iterBucket = -1;
    sectors->m_iterItem = nullptr;
    void* sector;
    while (sectors->StepIterator() && sectors->m_iterItem && (sector = sectors->m_iterItem->value) != nullptr) {
        MemSys::EnterHeapScope(GetParkHeap());
        void* memory = MemSys::Alloc(sizeof(TemplateTile));
        TemplateTile* source = memory ? ConstructSourcePiece(memory) : nullptr;
        MemSys::LeaveHeapScope();
        source->m_following = m_sources;
        m_sources = source;
        source->m_label = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(sector) + 4); // its checksum
        const Maths::Vec4* box = GetLocalBounds(sector); // min, max
        const Maths::Vec4& min = box[0];
        const Maths::Vec4& max = box[1];
        int cells = static_cast<int>((static_cast<double>(max.x) - min.x + kSizeSlack) * kInverseCell);
        if (cells < 1)
            cells = 1;
        source->m_extent.x = static_cast<float>(cells * static_cast<double>(kGridPitch));
        cells = static_cast<int>((static_cast<double>(max.z) - min.z + kSizeSlack) * kInverseCell);
        if (cells < 1)
            cells = 1;
        source->m_extent.z = static_cast<float>(cells * static_cast<double>(kGridPitch));
        const double height = static_cast<double>(max.y) - min.y;
        source->m_extent.y = static_cast<float>(height);
        if (height < kGridStepUp && height > 0.0)
            source->m_extent.y = kGridStepUp; // (at least a step high)
    }
    m_sceneHandle = CloneScene("cloned", *reinterpret_cast<void**>(static_cast<uint8_t*>(m_sourceWorld) + 0x88), 1);
    ParkEditing::SetUpBounds(this);
    for (RestartPoint& restart : m_restarts)
        restart.m_kindTag = RestartPoint::kFree;
}

void CParkAssembler::LoadPieces(uint32_t level, int unused)
{
    LoadSourcePieces(unused);
    Scripts::CArray* nodes = LevelNodes::GetLastNodeArray();
    ParkEditing::ReadPieceFlags(this, nodes);
    if (level == kParkEditorLevel) {
        MemSys::EnterHeapScope(MemSys::TopDownHeap());
        BuildSourceRails(nodes);
        MemSys::LeaveHeapScope();
    } else {
        BuildSourceRails(nodes);
    }
}

} // namespace Ed
