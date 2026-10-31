// The park editor's rail editor (rail_editor.h): the rails' and points' lists, pools, packing and the component's life.
// The sectors drawn for them: rail_editor_draw.cpp; the script side: rail_editor_script.cpp.

#include "game/park_editing/rail_editor.h"
#include "engine/core/code_address.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/stubs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "game/park_editing/park.h"
#include "game/park_editing/rail_editor_internal.h"
#include "engine/objects/entity_hub.h"
#include "game/park/park_memory.h"
#include "engine/objects/compositemgr.h"

namespace ParkEditing {

MemSys::PoolPair g_railPointPools;

namespace RailEditorInternal {

MemSys::PoolPair& PointPools() { return g_railPointPools; }
MemSys::PoolPair& RailPools() { return g_railPools; }
bool& UpdateSuperSectors() { return g_railEditorFlag; }

void RemoveSceneSector(uint8_t*& sector)
{
    if (!sector)
        return;
    CParkAssembler* generator = CParkLayout::sSingleton()->m_builder;
    generator->RemoveSceneSector(SectorKeyFromSpec(sector), 0);
    SetCountsDirty();
    if (UpdateSuperSectors())
        CParkLayout::sSingleton()->m_builder->RebuildCollision();
    sector = nullptr;
}

} // namespace RailEditorInternal

using namespace RailEditorInternal;

uint32_t g_nextRailId{1};

namespace {

static VtableSlot kVtable_slots[] = {&ParkEditing::CRailAuthoring::Teardown,
         &ParkEditing::CRailAuthoring::TickFrame,
         Stubs::IgnoreArgument,
         &ParkEditing::CRailAuthoring::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         &ParkEditing::CRailAuthoring::Conceal,
         Stubs::DoNothing,
         &ParkEditing::CRailAuthoring::FetchDiagnostics,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(
             &GameObjects::CObjectModule::InvokeMethodByName),
         &ParkEditing::CRailAuthoring::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
uint32_t& NextRailId() { return g_nextRailId; }

constexpr uint32_t kInput = 0x27D7CD28, kEditorCamera = 0xAC8946E2;

// A pool pair's "made here" flags (one byte each, +0x04).
uint8_t& Owned(MemSys::PoolPair& pair, int which) { return reinterpret_cast<uint8_t*>(&pair.unknown4)[which]; }

// A pool made for a pair's current slot (owned).
void MakePool(MemSys::PoolPair& pair, int itemSize, int count, const char* name)
{
    void* memory = MemSys::Alloc(sizeof(MemSys::SlabBank));
    pair.pools[pair.current] = memory ? new (memory) MemSys::SlabBank(itemSize, count, name) : nullptr;
    Owned(pair, pair.current) = 1;
}

// The current pool freed if it was made here; the slot cleared.
void FreePool(MemSys::PoolPair& pair)
{
    MemSys::SlabBank* pool = pair.pools[pair.current];
    if (!pool)
        return;
    if (Owned(pair, pair.current)) {
        MemSys::Dealloc(pool->m_outset);
        MemSys::Hub::Pull()->Dealloc(pool);
    }
    pair.pools[pair.current] = nullptr;
}

// A rail taken out of the editor's list (the head and the current rail moved on past it).
void Uncouple(CRailAuthoring* editor, CEditedRail* rail)
{
    if (editor->m_railGroups == rail)
        editor->m_railGroups = rail->m_following;
    if (editor->m_activeNode == rail)
        editor->m_activeNode = rail->m_following;
    if (rail->m_following)
        rail->m_following->m_preceding = rail->m_preceding;
    if (rail->m_preceding)
        rail->m_preceding->m_following = rail->m_following;
}

bool Inside(const CEditedRailPoint* p, float x0, float z0, float x1, float z1)
{
    return p->m_atPoint.x > x0 && p->m_atPoint.x < x1 && p->m_atPoint.z > z0 && p->m_atPoint.z < z1;
}

} // namespace

// --- CEditedRailPoint ---------------------------------------------------------------------------------------------

// 0x004F3AD0
void CEditedRailPoint::ClearGlow()
{
    if (!m_highlighted)
        return;
    auto restore = [](uint8_t* sector) {
        ClearSectorColour(sector);
        SetSectorVisibility(sector, 0xFFFFFFFF);
    };
    if (m_post)
        restore(m_post);
    if (m_segment)
        restore(m_segment);
    if (m_preceding && m_preceding->m_segment)
        restore(m_preceding->m_segment);
    if (m_following && !m_following->m_following && m_following->m_post)
        restore(m_following->m_post);
    if (m_preceding && !m_preceding->m_preceding && m_preceding->m_post)
        restore(m_preceding->m_post);
    m_highlighted = false;
}

// 0x004F40A0
void CEditedRailPoint::DeleteSegment() { RemoveSceneSector(m_segment); }

// 0x004F40F0
void CEditedRailPoint::DeletePost() { RemoveSceneSector(m_post); }

// 0x004F4140
void CEditedRailPoint::Pack(uint16_t* record) const
{
    record[4] = m_hasPost;
    if (m_levelY < 0.0f)
        record[3] = 0;
    else
        record[3] = static_cast<uint16_t>(static_cast<int>(static_cast<double>(m_levelY) * 16.0f));
    record[0] = static_cast<uint16_t>(static_cast<int>(static_cast<double>(m_atPoint.x) * 8.0f));
    record[1] = static_cast<uint16_t>(static_cast<int>(static_cast<double>(m_atPoint.y) * 8.0f));
    record[2] = static_cast<uint16_t>(static_cast<int>(static_cast<double>(m_atPoint.z) * 8.0f));
}

// --- CEditedRail --------------------------------------------------------------------------------------------------

// 0x004F70C0
CEditedRail* CEditedRail::MakeNew()
{
    m_ident = NextRailId()++;
    m_pointChain = nullptr;
    m_following = nullptr;
    m_preceding = nullptr;
    Clear();
    return this;
}

// 0x004F6E20
void CEditedRail::Clear()
{
    CEditedRailPoint* point = m_pointChain;
    while (point) {
        CEditedRailPoint* next = point->m_following;
        RemoveSceneSector(point->m_segment);
        RemoveSceneSector(point->m_post);
        PointPools().Dealloc(point);
        point = next;
    }
    m_pointChain = nullptr;
}

// 0x004F3B90
int CEditedRail::RailPointTotal() const
{
    int count = 0;
    for (CEditedRailPoint* p = m_pointChain; p; p = p->m_following)
        ++count;
    return count;
}

// 0x004F3BB0
CEditedRailPoint* CEditedRail::FetchVertexAt(int index) const
{
    CEditedRailPoint* p = m_pointChain;
    while (index != 0) {
        if (!p)
            return nullptr;
        p = p->m_following;
        --index;
    }
    return p;
}

// 0x004F3BD0
void CEditedRail::ClearColours()
{
    for (CEditedRailPoint* p = m_pointChain; p; p = p->m_following) {
        if (p->m_post)
            ClearSectorColour(p->m_post);
        if (p->m_segment)
            ClearSectorColour(p->m_segment);
    }
}

// 0x004F41C0
void CEditedRail::DeleteSegments()
{
    for (CEditedRailPoint* p = m_pointChain; p; p = p->m_following)
        RemoveSceneSector(p->m_segment);
}

// 0x004F4220
void CEditedRail::DeletePosts()
{
    for (CEditedRailPoint* p = m_pointChain; p; p = p->m_following)
        RemoveSceneSector(p->m_post);
}

// 0x004F4280
bool CEditedRail::FindNearestPoint(const Maths::Vec4* position, Maths::Vec4* point, float* distanceSquared, int* index,
                                   int skip) const
{
    const CEditedRailPoint* p = m_pointChain;
    if (!p)
        return false;
    float best = 1e8f;
    for (int i = 0; p; p = p->m_following, ++i) {
        if (i == skip)
            continue;
        double dx = static_cast<double>(p->m_atPoint.x) - position->x;
        double dz = static_cast<double>(p->m_atPoint.z) - position->z;
        double d = dx * dx + dz * dz;
        if (d < best) {
            best = static_cast<float>(d);
            *point = p->m_atPoint;
            *distanceSquared = static_cast<float>(d);
            *index = i;
        }
    }
    return true;
}

// 0x004F4310
int CEditedRail::CountPointsInside(float x0, float z0, float x1, float z1) const
{
    int count = 0;
    for (const CEditedRailPoint* p = m_pointChain; p; p = p->m_following) {
        if (Inside(p, x0, z0, x1, z1))
            ++count;
    }
    return count;
}

// 0x004F4420
void CEditedRail::ChangeColour(int index)
{
    Scripts::CArray* colours = Scripts::FetchList(kGraffitiColors);
    auto* colour = reinterpret_cast<Scripts::CArray*>(colours->FetchPart(static_cast<uint32_t>(index)));
    uint32_t rgba = static_cast<uint8_t>(colour->FetchPart(0)) | static_cast<uint8_t>(colour->FetchPart(1)) << 8 |
                    static_cast<uint32_t>(static_cast<uint8_t>(colour->FetchPart(2))) << 16 | 0x80000000u;
    for (CEditedRailPoint* p = m_pointChain; p; p = p->m_following) {
        if (p->m_post)
            SetSectorColour(p->m_post, rgba);
        if (p->m_segment)
            SetSectorColour(p->m_segment, rgba);
    }
}

// 0x004F44B0
bool CEditedRail::AnyPointOutside(float x0, float z0, float x1, float z1) const
{
    for (const CEditedRailPoint* p = m_pointChain; p; p = p->m_following) {
        if (p->m_atPoint.x < x0 || p->m_atPoint.x > x1 || p->m_atPoint.z < z0 || p->m_atPoint.z > z1)
            return true;
    }
    return false;
}

// 0x004F6240
CEditedRailPoint* CEditedRail::AppendRailVertex()
{
    MemSys::PoolPair& pools = PointPools();
    if (pools.pools[pools.current]->m_takenCount == CRailAuthoring::kMaxPoints)
        return nullptr;
    auto* point = static_cast<CEditedRailPoint*>(pools.Carve(sizeof(CEditedRailPoint)));
    if (point) {
        point->m_atPoint = {0.0f, 0.0f, 0.0f, 1.0f};
        point->m_hasPost = false;
        point->m_highlighted = false;
        point->m_levelY = 0.0f;
        point->m_following = nullptr;
        point->m_preceding = nullptr;
        point->m_segment = nullptr;
        point->m_post = nullptr;
    }
    // (a failed allocation crashes here, as in the original)
    point->m_following = m_pointChain;
    if (m_pointChain)
        m_pointChain->m_preceding = point;
    point->m_preceding = nullptr;
    m_pointChain = point;
    return point;
}

// 0x004F62D0
void CEditedRail::AddPointCopy(const CEditedRailPoint* source)
{
    CEditedRailPoint* point = AppendRailVertex();
    point->m_atPoint = source->m_atPoint;
    point->m_hasPost = source->m_hasPost;
    point->m_levelY = source->m_levelY;
}

// 0x004F6300
CEditedRailPoint* CEditedRail::CutAt(CEditedRailPoint* point)
{
    if (!point)
        return nullptr;
    CEditedRailPoint* prev = point->m_preceding;
    if (prev && point->m_following) {
        MemSys::PoolPair& rails = RailPools();
        if (rails.pools[rails.current]->m_takenCount == CRailAuthoring::kMaxRails)
            return nullptr;
    }
    if (point == m_pointChain) {
        m_pointChain = point->m_following;
        if (point->m_following)
            point->m_following->m_preceding = nullptr;
        point->DeleteSegment();
        point->DeletePost();
        CRailAuthoring::FreePoint(point);
        return nullptr;
    }
    prev->DeleteSegment();
    point->m_preceding->m_following = nullptr;
    CEditedRailPoint* rest = point->m_following;
    point->DeleteSegment();
    point->DeletePost();
    CRailAuthoring::FreePoint(point);
    if (rest)
        rest->m_preceding = nullptr;
    return rest;
}

// 0x004F6F40
void CEditedRail::CopyFrom(const CEditedRail* source)
{
    Clear();
    for (const CEditedRailPoint* p = source->m_pointChain; p; p = p->m_following)
        AddPointCopy(p);
}

// 0x004F7100
CEditedRail* CEditedRail::CopyInside(float x0, float z0, float x1, float z1, CEditedRail* list) const
{
    bool lastInside = false;
    const CEditedRailPoint* first = nullptr; // an inside point waiting for a second
    for (const CEditedRailPoint* p = m_pointChain; p; p = p->m_following) {
        if (!Inside(p, x0, z0, x1, z1)) {
            first = nullptr;
            lastInside = false;
        } else if (!lastInside) {
            first = p;
            lastInside = true;
        } else {
            if (first) {
                void* memory = CRailAuthoring::NewRailMemory(sizeof(CEditedRail));
                auto* rail = memory ? static_cast<CEditedRail*>(memory)->MakeNew() : nullptr;
                rail->m_following = list;
                list = rail;
                rail->AddPointCopy(first);
                first = nullptr;
            }
            list->AddPointCopy(p);
            lastInside = true;
        }
    }
    return list;
}

// --- CRailAuthoring -----------------------------------------------------------------------------------------

// 0x004F4970
CRailAuthoring* __cdecl CRailAuthoring::Sole()
{
    auto* object = reinterpret_cast<GameObjects::CGameEntity*>(
        reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeRegistry)->LookUpEntity(kType));
    return static_cast<CRailAuthoring*>(object->FetchModule(kType));
}

// 0x004F7FB0
GameObjects::CObjectModule* __cdecl CRailAuthoring::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRailAuthoring));
    return memory ? static_cast<CRailAuthoring*>(memory)->MakeNew() : nullptr;
}

// 0x004F7C90
CRailAuthoring* CRailAuthoring::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kType;
    MakePointPool(kMaxPoints, "CEditedRailPoint");
    MakeRailPool(kMaxRails, "CEditedRail");
    m_inputPtr = nullptr;
    m_viewCamera = nullptr;
    m_dottedLine = nullptr;
    m_dottedLineName = 0;
    m_railGroups = nullptr;
    std::memset(&m_packed, 0, sizeof(m_packed));
    Clear();
    return this;
}

// 0x004F7D20
void CRailAuthoring::DestroyBody()
{
    m_vtable = kVtable;
    Clear();
    FreePool(PointPools());
    FreePool(RailPools());
    CObjectModule::DestroyBody();
}

// 0x004F8010
CRailAuthoring* CRailAuthoring::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004F47E0
void CRailAuthoring::Seal()
{
    m_inputPtr = m_entity->FetchModule(kInput);
    m_viewCamera = m_entity->FetchModule(kEditorCamera);
}

// 0x004F4990
void __cdecl CRailAuthoring::MakePointPool(int count, const char* name)
{
    MakePool(PointPools(), sizeof(CEditedRailPoint), count, name);
}

// 0x004F4A70
void __cdecl CRailAuthoring::MakeRailPool(int count, const char* name)
{
    MakePool(RailPools(), sizeof(CEditedRail), count, name);
}

// 0x004F4AF0
void* __cdecl CRailAuthoring::NewRailMemory(unsigned int size) { return RailPools().Carve(size); }

// 0x004F4B30
void __cdecl CRailAuthoring::FreeRail(void* rail) { RailPools().Dealloc(rail); }

// 0x004F4A10
void __cdecl CRailAuthoring::FreePoint(void* point) { PointPools().Dealloc(point); }

// 0x004F6BF0
int __cdecl CRailAuthoring::FreePoints()
{
    MemSys::PoolPair& pools = PointPools();
    return kMaxPoints - pools.pools[pools.current]->m_takenCount;
}

// 0x004F7550
void CRailAuthoring::Clear()
{
    CEditedRail* rail = m_railGroups;
    while (rail) {
        CEditedRail* next = rail->m_following;
        rail->Clear();
        FreeRail(rail);
        rail = next;
    }
    m_railGroups = nullptr;
    m_activeNode = nullptr;
    m_modeSel = 0;
    DeleteDottedLine();
}

// 0x004F72C0
CEditedRail* CRailAuthoring::NewRail()
{
    void* memory = NewRailMemory(sizeof(CEditedRail));
    CEditedRail* rail = memory ? static_cast<CEditedRail*>(memory)->MakeNew() : nullptr;
    // (out of rails: a crash here, as in the original)
    rail->m_following = m_railGroups;
    rail->m_preceding = nullptr;
    if (m_railGroups)
        m_railGroups->m_preceding = rail;
    m_railGroups = rail;
    m_activeNode = rail;
    return rail;
}

// 0x004F7270
void CRailAuthoring::DeleteRail(CEditedRail* rail)
{
    Uncouple(this, rail);
    rail->Clear();
    FreeRail(rail);
}

// 0x004F7370
void CRailAuthoring::DeleteShortRails()
{
    CEditedRail* rail = m_railGroups;
    while (rail) {
        CEditedRail* next = rail->m_following;
        if (!rail->m_pointChain || !rail->m_pointChain->m_following) {
            Uncouple(this, rail);
            rail->Clear();
            FreeRail(rail);
        }
        rail = next;
    }
}

// 0x004F73E0
bool CRailAuthoring::CutRail(CEditedRail* rail, CEditedRailPoint* point)
{
    CEditedRailPoint* rest = rail->CutAt(point);
    if (!rest)
        return false;
    NewRail()->m_pointChain = rest;
    return true;
}

// 0x004F7410
bool CRailAuthoring::CutPointInOrOut(CEditedRail* rail, float x0, float z0, float x1, float z1, int inside)
{
    CEditedRailPoint* p = rail->m_pointChain;
    while (p) {
        CEditedRailPoint* next = p->m_following;
        bool cut = Inside(p, x0, z0, x1, z1) ? inside == 1 : inside == 0;
        if (cut) {
            CEditedRailPoint* rest = rail->CutAt(p);
            if (rest) {
                NewRail()->m_pointChain = rest;
                return true;
            }
        }
        p = next;
    }
    return false;
}

// 0x004F74B0
void CRailAuthoring::DeletePoints(float x0, float z0, float x1, float z1, int inside)
{
    if (x0 > x1) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z0 > z1) {
        float t = z0;
        z0 = z1;
        z1 = t;
    }
    bool changed;
    do {
        changed = false;
        for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
            if (CutPointInOrOut(rail, x0, z0, x1, z1, inside))
                changed = true;
        }
    } while (changed);
    DeleteShortRails();
}

// 0x004F3C50
CEditedRail* CRailAuthoring::FindRail(uint32_t id) const
{
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
        if (rail->m_ident == id)
            return rail;
    }
    return nullptr;
}

// 0x004F3CA0
CEditedRail* CRailAuthoring::FindRailByCluster(uint32_t name) const
{
    int i = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following, ++i) {
        char cluster[52];
        std::snprintf(cluster, sizeof(cluster), "CreatedRailCluster%d", i);
        if (name == Checksums::HashFromText(cluster))
            return rail;
    }
    return nullptr;
}

// 0x004F3D10
void CRailAuthoring::ClearClusterColours(uint32_t name)
{
    if (CEditedRail* rail = FindRailByCluster(name))
        rail->ClearColours();
}

// 0x004F4950
void CRailAuthoring::SetClusterColour(uint32_t name, int colourIndex)
{
    if (CEditedRail* rail = FindRailByCluster(name))
        rail->ChangeColour(colourIndex);
}

// 0x004F3C70
int CRailAuthoring::RailPointTotal() const
{
    int count = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
        for (CEditedRailPoint* p = rail->m_pointChain; p; p = p->m_following)
            ++count;
    }
    return count;
}

// 0x004F3C20
void CRailAuthoring::UnHighlightAll()
{
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
        for (CEditedRailPoint* p = rail->m_pointChain; p; p = p->m_following)
            p->ClearGlow();
    }
}

// 0x004F3D30
void CRailAuthoring::ChangeLive(bool active)
{
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
        for (CEditedRailPoint* p = rail->m_pointChain; p; p = p->m_following) {
            if (p->m_post)
                EnableSector(p->m_post, active);
            if (p->m_segment)
                EnableSector(p->m_segment, active);
        }
    }
}

// 0x004F45C0
void CRailAuthoring::DeleteAllSegments()
{
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
        rail->DeleteSegments();
}

// 0x004F45E0
void CRailAuthoring::DeleteAllPosts()
{
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
        rail->DeletePosts();
}

// 0x004F4600
bool CRailAuthoring::AnyPointOutside(float x0, float z0, float x1, float z1) const
{
    if (x0 > x1) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z0 > z1) {
        float t = z0;
        z0 = z1;
        z1 = t;
    }
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
        if (rail->AnyPointOutside(x0, z0, x1, z1))
            return true;
    }
    return false;
}

// 0x004F4690
int CRailAuthoring::CountPointsInside(float x0, float z0, float x1, float z1) const
{
    if (x0 > x1) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z0 > z1) {
        float t = z0;
        z0 = z1;
        z1 = t;
    }
    int count = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
        count += rail->CountPointsInside(x0, z0, x1, z1);
    return count;
}

// 0x004F4710
bool CRailAuthoring::FindNearestPoint(const Maths::Vec4* position, Maths::Vec4* point, float* distanceSquared,
                                            uint32_t* railId, int* index, uint32_t skipRail, int bypassIdx) const
{
    bool found = false;
    float best = 1e8f;
    // The original keeps the callee's results in locals it never initialises: a rail whose only point is skipped
    // still "finds" one - the distance left from an earlier rail (or, for the first, the bits of the `index` pointer
    // the slot held), the point unset (here zero: what a cleared stack holds).
    Maths::Vec4 nearest = {0.0f, 0.0f, 0.0f, 0.0f};
    float distance;
    std::memcpy(&distance, &index, sizeof(distance));
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
        int i = 0;
        if (rail->FindNearestPoint(position, &nearest, &distance, &i, rail->m_ident == skipRail ? bypassIdx : -1) &&
            distance < best) {
            best = distance;
            *point = nearest;
            *distanceSquared = distance;
            *railId = rail->m_ident;
            *index = i;
            found = true;
        }
    }
    return found;
}

// 0x004F48F0
void CRailAuthoring::DeleteDottedLine()
{
    if (!m_dottedLine)
        return;
    RemoveSceneSector(m_dottedLine);
    m_dottedLineName = 0;
}

// 0x004F4500
void CRailAuthoring::Pack()
{
    std::memset(&m_packed, 0, sizeof(m_packed));
    uint16_t rails = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
        ++rails;
    m_packed.m_railCount = rails;
    CEditedRail* rail = m_railGroups;
    if (!rail)
        return;
    while (rail->m_following)
        rail = rail->m_following;
    uint16_t* count = m_packed.m_pointCounts;
    uint16_t* record = m_packed.m_records[0];
    // From the first rail laid (the tail) back to the head, each rail's points from its first laid.
    for (; rail; rail = rail->m_preceding) {
        uint16_t points = 0;
        for (CEditedRailPoint* p = rail->m_pointChain; p; p = p->m_following)
            ++points;
        *count++ = points;
        CEditedRailPoint* p = rail->m_pointChain;
        if (!p)
            continue;
        while (p->m_following)
            p = p->m_following;
        for (; p; p = p->m_preceding) {
            p->Pack(record);
            record += 5;
        }
    }
}

// 0x004F45B0
RailBuffer* CRailAuthoring::GetPacked()
{
    Pack();
    return &m_packed;
}

// 0x004F3C00
void CRailAuthoring::SetPacked(const RailBuffer* buffer) { std::memcpy(&m_packed, buffer, sizeof(m_packed)); }

// 0x004F7DF0
void CRailAuthoring::Unpack()
{
    Clear();
    const uint16_t* count = m_packed.m_pointCounts;
    const uint16_t* record = m_packed.m_records[0];
    for (unsigned int rails = m_packed.m_railCount; rails != 0; --rails) {
        uint16_t points = *count++;
        CEditedRail* rail = NewRail();
        for (unsigned int i = points; i != 0; --i) {
            if (CEditedRailPoint* p = rail->AppendRailVertex()) {
                p->m_hasPost = record[4] != 0;
                p->m_levelY = static_cast<float>(static_cast<double>(record[3]) * 0.0625f);
                p->m_atPoint.x = static_cast<float>(static_cast<double>(static_cast<int16_t>(record[0])) * 0.125f);
                p->m_atPoint.y = static_cast<float>(static_cast<double>(static_cast<int16_t>(record[1])) * 0.125f);
                p->m_atPoint.z = static_cast<float>(static_cast<double>(static_cast<int16_t>(record[2])) * 0.125f);
            }
            record += 5;
        }
    }
}

// 0x004F7230
CEditedRail* CRailAuthoring::CopyRailsInside(float x0, float z0, float x1, float z1) const
{
    CEditedRail* list = nullptr;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
        list = rail->CopyInside(x0, z0, x1, z1, list);
    return list;
}

} // namespace Ed
