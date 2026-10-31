// Pieces of the trick object code, 0x005880E0-0x0058818C and 0x00588200-0x005897A6 (the trick objects, the
// manager's lookups, graffiti state and graffiti log).
// See trick_objects.h.

#include "game/obj/trick_objects.h"
#include "game/ed/rail_editor.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/sector.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/skater/skater.h"
#include "game/skate/skate_module.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/sys/game_config.h"

namespace GameObjects {

static_assert(offsetof(CTrickObjectCluster, m_name) == 0x14 && offsetof(CTrickObjectCluster, m_scores) == 0x1C &&
                  offsetof(CTrickObjectCluster, m_owner) == 0x3C && offsetof(CTrickObjectCluster, m_owned) == 0x40,
              "layout must match the original");

GameObjects::CTrickSpotHub* g_trickObjectManager;
uint32_t g_trickObjectManagerReferences;

namespace {

constexpr uint32_t kNetGraffiti = 0x5E2EA50C;         // netgraffiti
constexpr uint32_t kGraffitiTeamScoring = 0xAB80CC2F; // graffiti_team_scoring (team_graffiti_mode)

// Which clusters of the current log changed hands (by their index in its list). THUG2's was a 128-byte global at
// 0x007CE508; enlarged as THUGPRO's DLL enlarged it (its NetLink::InitPatches, "Fix 256 graff tag crash": idea from
// Morten1337's thugpro_proxy, written here as our own buffer) - a list longer than the mask is cut to its size.
uint8_t s_changeMask[CTrickSpotHub::kMaxLoggedClusters];

// The game mode (0x007CE470) and the network manager's players (0x007CCE14): PlayerRecord +0x14 its skater (+0x14 the
// skater's object id), +0x30 its team. Walked with a ListKit::Search<PlayerRecord> (vtable 0x0064C1A8).
void* GameMode() { return static_cast<void*>(GoalSystem::g_gameMode); }
void* NetManager() { return reinterpret_cast<void*>(OnlinePlay::g_gameNetManager); }
struct PlayerSearch {
    void* m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
uint8_t* FirstPlayer(PlayerSearch* search)
{
    return static_cast<uint8_t*>(static_cast<OnlinePlay::Manager*>(NetManager())->BeginPlayerWalk(search, false));
}
uint8_t* NextPlayer(PlayerSearch* search)
{
    return static_cast<uint8_t*>(static_cast<OnlinePlay::Manager*>(NetManager())->StepPlayerWalk(search, false));
}
uint8_t* PlayerByObjectId(int id)
{
    return static_cast<OnlinePlay::Manager*>(NetManager())->PlayerByObjectId(static_cast<uint16_t>(id));
}
int Team(uint8_t* player) { return *reinterpret_cast<int*>(player + 0x30); }
uint8_t* SkaterOf(uint8_t* player) { return *reinterpret_cast<uint8_t**>(player + 0x14); }
int ObjectId(uint8_t* skater) { return *reinterpret_cast<int*>(skater + 0x14); }

// Team graffiti scored by team: the game mode netgraffiti, a team game, team_graffiti_mode graffiti_team_scoring.
bool TeamScoring()
{
    return static_cast<GoalSystem::CMatchRules*>(GameMode())->GetNameKey() == kNetGraffiti &&
           static_cast<GoalSystem::CMatchRules*>(GameMode())->IsSquadMatch() != 0 &&
           static_cast<GoalSystem::CMatchRules*>(GameMode())->GetTeamGraffitiMode() == kGraffitiTeamScoring;
}

} // namespace

// 0x005880E0
void ReleaseTrickObjectManager()
{
    uint32_t& count = g_trickObjectManagerReferences;
    if (count > 1) {
        --count;
        return;
    }
    void** instance = reinterpret_cast<void**>(g_trickObjectManager);
    if (instance)
        reinterpret_cast<void*(__thiscall*)(void*, int)>(static_cast<void**>(*instance)[0])(instance, 1);
    g_trickObjectManager = nullptr;
    count = 0;
}

// 0x00588120
ClusterCount* __cdecl CountObjectInCluster(uint8_t* object, ClusterCount* counter)
{
    if (object[0x40] && counter->cluster == *reinterpret_cast<uint32_t*>(object + 0x3C))
        counter->count += 1;
    return counter;
}

// 0x00588150
void CTrickParamSet::SetParams(Scripts::CStruct* params)
{
    m_params->Clear();
    if (params)
        m_params->MergeStructure(params);
}

// 0x00588170
CTrickObjectList* CTrickObjectList::Construct()
{
    m_count = 0;
    return this;
}

// 0x00588180
bool CTrickObjectList::Clear()
{
    m_count = 0;
    return true;
}

// 0x005886C0
uint32_t CTrickSpotHub::LogTrickObjects(uint32_t count, uint32_t* objects, uint8_t* stolen, int player,
                                              uint32_t score)
{
    DebugOut::Printf("RequestLogTrick called\n");
    if (count > kMaxLoggedClusters) // (ours: the original's mask overran here)
        count = kMaxLoggedClusters;
    if (count)
        std::memset(s_changeMask, 0, count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* cluster = static_cast<CTrickObjectCluster*>(m_objectClusters.GetItem(objects[i]));
        if (!cluster)
            continue;
        GoalSystem::GetObjectiveBoard()->KillCluster(cluster->m_name, static_cast<int>(score));
        if (score > cluster->m_scores[player])
            cluster->m_scores[player] = score;
        uint32_t total = score;
        if (TeamScoring()) {
            if (uint8_t* me = PlayerByObjectId(player)) {
                // The team's score: each of its players' best on this cluster.
                int team = Team(me);
                total = 0;
                PlayerSearch search;
                for (uint8_t* other = FirstPlayer(&search); other; other = NextPlayer(&search)) {
                    if (SkaterOf(other) && Team(other) == team)
                        total += cluster->m_scores[ObjectId(SkaterOf(other))];
                }
            }
        }
        if (total <= cluster->m_bestScore)
            continue;
        bool changed = cluster->m_owner != player;
        if (changed && TeamScoring()) {
            // Taken from a team mate: only if the player beat one of the team's own best scores on it.
            uint8_t* holder = PlayerByObjectId(static_cast<uint16_t>(cluster->m_owner));
            int team = Team(PlayerByObjectId(player));
            if (Team(holder) == team) {
                changed = false;
                PlayerSearch search;
                for (uint8_t* other = FirstPlayer(&search); other; other = NextPlayer(&search)) {
                    if (SkaterOf(other) && Team(other) == team &&
                        score > cluster->m_scores[ObjectId(SkaterOf(other))]) {
                        changed = true;
                        break;
                    }
                }
            }
        }
        if (!cluster->m_owned || changed)
            s_changeMask[i] = 1;
        if (cluster->m_owned && changed)
            stolen[cluster->m_owner] = 1;
        if (changed)
            cluster->m_owner = player;
        cluster->m_bestScore = total;
        cluster->m_owned = true;
    }
    // The list compacted to the objects whose cluster changed hands.
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (s_changeMask[i])
            objects[kept++] = objects[i];
    }
    return kept;
}

// --- the trick objects, the clusters' lists, the manager's lookups and graffiti state ---------------------------

namespace {

extern void** const kTrickObjectVtable;
extern void** const kTrickObjectNodeVtable;
extern void** const kTrickClusterNodeVtable;
extern void** const kTrickClusterVtable;
extern void** const kTrickObjectManagerVtable;
constexpr uint32_t kLoadCreatedPark = 0xB664035D; // load_sk5ed_gameplay (the skate module's level, +0x378)

// The level sector named `name` in any scene (0x0048C510) and its colour: 0x0049E670 cleared, 0x0049E980 read
// (r, g, b, a; zero without a mesh), the mesh's (+0x14) vtable +0x18 set.
uint8_t* FindSector(uint32_t name) { return static_cast<uint8_t*>(Engine3D::Engine::LookUpSector(name)); }
void ClearSectorColor(uint8_t* sector) { reinterpret_cast<Engine3D::CWorldCell*>(sector)->ClearColor(); }
uint32_t* GetSectorColor(uint8_t* sector, uint32_t* out)
{
    return reinterpret_cast<uint32_t*>(reinterpret_cast<Engine3D::CWorldCell*>(sector)->GetColor(reinterpret_cast<uint8_t*>(out)));
}

// Graffiti colour `index`: graffitiColors[index]'s r, g, b.
Scripts::CArray* GraffitiColor(int index)
{
    Scripts::CArray* colors = Scripts::GetArray("graffitiColors");
    return reinterpret_cast<Scripts::CArray*>(colors->GetElement(static_cast<uint32_t>(index)));
}

} // namespace

// 0x00588200
bool CTrickSpot::SetGraffitiColor(int index)
{
    uint8_t* sector = FindSector(m_name);
    Scripts::CArray* color = GraffitiColor(index);
    uint8_t rgba[4];
    rgba[0] = static_cast<uint8_t>(color->GetElement(0));
    rgba[1] = static_cast<uint8_t>(color->GetElement(1));
    rgba[2] = static_cast<uint8_t>(color->GetElement(2));
    rgba[3] = 0x80;
    if (void** mesh = *reinterpret_cast<void***>(sector + 0x14)) {
        uint32_t packed;
        std::memcpy(&packed, rgba, 4);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(static_cast<void**>(*mesh)[6])(mesh, packed);
    }
    return true;
}

// 0x00588270
bool CTrickSpot::ClearGraffitiColor(int index)
{
    uint8_t* sector = FindSector(m_name);
    Scripts::CArray* color = GraffitiColor(index);
    uint8_t r = static_cast<uint8_t>(color->GetElement(0));
    uint8_t g = static_cast<uint8_t>(color->GetElement(1));
    uint8_t b = static_cast<uint8_t>(color->GetElement(2));
    uint32_t packed;
    GetSectorColor(sector, &packed);
    uint8_t current[4];
    std::memcpy(current, &packed, 4);
    if (current[0] == r && current[1] == g && current[2] == b)
        ClearSectorColor(sector);
    return true;
}

// 0x00588590
void TrickObjectNode::DestroyBody() { UnlinkAs(kTrickObjectNodeVtable); }

// 0x005885F0
TrickObjectNode* TrickObjectNode::Destroy(unsigned int flags)
{
    UnlinkAs(kTrickObjectNodeVtable);
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x005885B0
TrickClusterNode* TrickClusterNode::Destroy(unsigned int flags)
{
    UnlinkAs(kTrickClusterNodeVtable);
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00588630
bool CTrickObjectCluster::AddObject(uint32_t name)
{
    auto* object = static_cast<CTrickSpot*>(MemSys::AllocZeroed(sizeof(CTrickSpot)));
    if (!object)
        return false;
    object->m_listLink.data = object;
    object->m_listLink.priority = 0;
    object->m_listLink.next = &object->m_listLink;
    object->m_listLink.prev = &object->m_listLink;
    object->m_listLink.vtable = kTrickObjectVtable;
    object->m_name = name;
    ClearSectorColor(FindSector(name));
    if (!GameConfig::g_fromCD && Scripts::FetchIntValue("show_all_trick_objects"))
        object->SetGraffitiColor(2);
    // Last in the list (before the head).
    object->m_listLink.next = &m_entities;
    object->m_listLink.prev = m_entities.prev;
    m_entities.prev->next = &object->m_listLink;
    m_entities.prev = &object->m_listLink;
    return true;
}

// 0x00588380
CTrickObjectCluster* CTrickSpotHub::GetObjectCluster(uint32_t objectName)
{
    return static_cast<CTrickObjectCluster*>(m_objectClusters.GetItem(objectName));
}

// 0x005883C0
CTrickObjectCluster* CTrickSpotHub::GetCluster(uint32_t name)
{
    return static_cast<CTrickObjectCluster*>(m_clusters.GetItem(name));
}

// 0x00588400
int CTrickSpotHub::CountClustersHeldBy(int player)
{
    ClusterCount counter = {0, static_cast<uint32_t>(player)};
    m_clusters.HandleCallback(reinterpret_cast<ListKit::HashTable::Callback>(&CountObjectInCluster), &counter);
    return counter.count;
}

// 0x00588430
void __cdecl FindClusterIndex(CTrickObjectCluster* cluster, ClusterIndexSearch* search)
{
    if (search->found)
        return;
    if (search->name == cluster->m_name)
        search->found = true;
    else
        ++search->index;
}

// 0x00588450
void __cdecl FindClusterAtIndex(CTrickObjectCluster* cluster, ClusterAtIndex* search)
{
    if (search->found)
        return;
    if (search->countdown == 0) {
        search->found = true;
        search->name = cluster->m_name;
    } else {
        --search->countdown;
    }
}

// 0x00588480
uint32_t CTrickSpotHub::GetClusterName(uint8_t index)
{
    ClusterAtIndex search = {0, index, false};
    m_clusters.HandleCallback(reinterpret_cast<ListKit::HashTable::Callback>(&FindClusterAtIndex), &search);
    return search.name;
}

// 0x005884B0
void __cdecl WriteHeldCluster(CTrickObjectCluster* cluster, GraffitiStateWriter* writer)
{
    if (!cluster->m_owned || cluster->m_owner != writer->player)
        return;
    ClusterIndexSearch search = {cluster->m_name, 0, false};
    writer->manager->m_clusters.HandleCallback(reinterpret_cast<ListKit::HashTable::Callback>(&FindClusterIndex), &search);
    writer->buffer[writer->count + 8] = search.index; // (a byte: past 255 clusters it wraps, as THUG2's and THUGPRO's)
    ++writer->count;
    ++writer->buffer[writer->player];
}

// 0x00588520
int CTrickSpotHub::WriteGraffitiState(uint8_t* buffer)
{
    GraffitiStateWriter writer = {0, 0, buffer, this};
    for (int player = 0; player < 8; ++player) {
        buffer[player] = 0;
        writer.player = player;
        m_clusters.HandleCallback(reinterpret_cast<ListKit::HashTable::Callback>(&WriteHeldCluster), &writer);
        DebugOut::Printf("Building graffiti state message %d\n", buffer[player]);
    }
    return writer.count + 8;
}

// 0x005889F0
bool CTrickSpotHub::RemoveAllClusters()
{
    m_clusters.HandleCallback(reinterpret_cast<ListKit::HashTable::Callback>(&Engine3D::DeleteObject), nullptr);
    m_clusters.EmptyTable();
    m_clusterCount = 0;
    m_objectClusters.EmptyTable();
    return true;
}

// 0x00588A20
bool CTrickObjectList::Add(uint32_t objectName)
{
    const uint32_t listed = static_cast<uint32_t>(m_count) > kCapacity ? kCapacity : static_cast<uint32_t>(m_count);
    CTrickObjectCluster* cluster = g_trickObjectManager->GetObjectCluster(objectName);
    if (!cluster || !cluster->m_name)
        return false;
    for (uint32_t i = 0; i < listed; ++i) {
        if (m_items[i] == cluster->m_name)
            return false;
    }
    GoalSystem::GetObjectiveBoard()->TrickObjectHit(cluster->m_name);
    m_items[m_count & (kCapacity - 1)] = cluster->m_name;
    ++m_count;
    return false;
}

// 0x00588AA0
uint32_t CTrickObjectList::CopyClusterNames(uint32_t* out, uint32_t maxBytes)
{
    const uint32_t listed = static_cast<uint32_t>(m_count) > kCapacity ? kCapacity : static_cast<uint32_t>(m_count);
    const CTrickSpotHub* manager = g_trickObjectManager;
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < listed; ++i) {
        auto* cluster = static_cast<CTrickObjectCluster*>(manager->m_objectClusters.GetItem(m_items[i]));
        if (cluster && cluster->m_name) {
            *out++ = m_items[i];
            bytes += 4;
        }
        if (bytes >= maxBytes)
            break;
    }
    return bytes;
}

// 0x00588BB0
void CTrickSpot::DestroyBody() { m_listLink.UnlinkAs(kTrickClusterNodeVtable); }

// 0x00588B40
CTrickSpot* CTrickSpot::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00588B70
void TrickObjectHead::UnlinkAll()
{
    // (The original reads the first node's data before checking the node for null.)
    ListNode* node = next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    while (node) {
        ListNode* following = node->next;
        node->prev->next = following;
        following->prev = node->prev;
        node->next = node;
        node->prev = node;
        node = following->data == reinterpret_cast<void*>(-1) ? nullptr : following;
    }
}

// 0x00588BD0
CTrickObjectCluster* CTrickObjectCluster::Construct(uint32_t name)
{
    m_listLink.data = this;
    m_listLink.priority = 0;
    m_listLink.next = &m_listLink;
    m_listLink.prev = &m_listLink;
    m_listLink.vtable = kTrickClusterVtable;
    m_entities.vtable = kTrickObjectVtable;
    m_entities.data = reinterpret_cast<void*>(-1); // (the head)
    m_entities.priority = 0;
    m_entities.next = &m_entities;
    m_entities.prev = &m_entities;
    m_name = name;
    for (uint32_t& score : m_scores)
        score = 0;
    m_bestScore = 0;
    m_owner = 0;
    m_owned = false;
    return this;
}

namespace {

// The objects in a cluster's list, and the one at `index` (null past the end). (The original reads each node's data
// before checking it for null.)
uint32_t CountObjects(const MemSys::ListNode& head)
{
    uint32_t count = 0;
    for (const MemSys::ListNode* node = head.next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}
MemSys::ListNode* ObjectAt(const MemSys::ListNode& head, uint32_t index)
{
    for (MemSys::ListNode* node = head.next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next) {
        if (index-- == 0)
            return node;
    }
    return nullptr;
}

} // namespace

// 0x00588C30
void CTrickObjectCluster::DestroyBody()
{
    m_listLink.vtable = kTrickClusterVtable;
    for (uint32_t count = CountObjects(m_entities); count != 0; --count) {
        void* object = ObjectAt(m_entities, 0)->data;
        if (object)
            reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(object))[0])(object, 1);
    }
    static_cast<TrickObjectHead&>(m_entities).UnlinkAll();
    m_entities.UnlinkAs(kTrickClusterNodeVtable);
    m_listLink.UnlinkAs(kTrickObjectNodeVtable);
}

// 0x00588D20
bool CTrickObjectCluster::SetGraffitiColor(int index)
{
    const uint32_t count = CountObjects(m_entities);
    for (uint32_t i = 0; i < count; ++i)
        static_cast<CTrickSpot*>(ObjectAt(m_entities, i)->data)->SetGraffitiColor(index);
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x378) == kLoadCreatedPark) {
        ParkEditing::CRailAuthoring::Instance()->SetClusterColour(m_name, index);
    }
    return true;
}

// 0x00588DC0
CTrickSpotHub* CTrickSpotHub::Construct()
{
    m_vtable = kTrickObjectManagerVtable;
    new (&m_clusters) ListKit::HashTable(8);
    new (&m_objectClusters) ListKit::HashTable(8);
    MemSys::Manager* memory = MemSys::Manager::Get();
    memory->EnterHeapScope(memory->m_scriptHeap);
    Scripts::CStruct* params = Scripts::NewStruct();
    m_params = params ? params->Init() : nullptr;
    memory->LeaveHeapScope();
    return this;
}

// 0x00588E90
void CTrickSpotHub::DestroyBody()
{
    m_vtable = kTrickObjectManagerVtable;
    RemoveAllClusters();
    m_objectClusters.~HashTable();
    m_clusters.~HashTable();
}

// 0x00588F40
bool CTrickObjectCluster::ClearGraffitiColor(int index)
{
    const uint32_t count = CountObjects(m_entities);
    for (uint32_t i = 0; i < count; ++i) {
        if (!static_cast<CTrickSpot*>(ObjectAt(m_entities, i)->data)->ClearGraffitiColor(index))
            return false;
    }
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x378) == kLoadCreatedPark) {
        ParkEditing::CRailAuthoring::Instance()->ClearClusterColours(m_name);
    }
    return true;
}

// 0x00588FF0
bool CTrickSpotHub::SetObjectGraffitiColor(uint32_t objectName, int player)
{
    CTrickObjectCluster* cluster = GetObjectCluster(objectName);
    if (!cluster)
        return false;
    cluster->SetGraffitiColor(player == -1 ? -1 : player + 1);
    return true;
}

// 0x00589030
bool CTrickSpotHub::ClearGraffitiColor(int index)
{
    m_clusters.m_iterBucket = -1;
    m_clusters.m_iterItem = nullptr;
    while (m_clusters.StepIterator() && m_clusters.m_iterItem && m_clusters.m_iterItem->value) {
        if (!static_cast<CTrickObjectCluster*>(m_clusters.m_iterItem->value)->ClearGraffitiColor(index))
            return false;
    }
    return true;
}

// 0x00589080
bool CTrickSpotHub::AddObjectToCluster(uint32_t objectName, uint32_t clusterName)
{
    m_objectClusters.StoreEntry(objectName, GetCluster(clusterName));
    return true;
}

// 0x00588300
void __cdecl ReleaseHeldCluster(CTrickObjectCluster* cluster, HeldClusterRelease* release)
{
    if (!cluster->m_owned || cluster->m_owner != release->player)
        return;
    cluster->m_owned = false;
    cluster->m_bestScore = 0;
    for (uint32_t& score : cluster->m_scores)
        score = 0;
    release->names[release->count++] = cluster->m_name;
}

// 0x00588350
void __cdecl ListFreeCluster(CTrickObjectCluster* cluster, FreeClusterList* list)
{
    if (list->count < 32 && !cluster->m_owned)
        list->names[list->count++] = cluster->m_name;
}

// 0x005890B0
void CTrickSpotHub::ApplyPlayerGraffiti()
{
    for (int player = 0; player < 8; ++player) {
        char name[32];
        std::snprintf(name, sizeof(name), "Skater%d", player);
        Scripts::CArray* objects;
        if (!m_params->GetArray(name, &objects, 0))
            continue;
        for (int i = 0; i < static_cast<int>(objects->m_size); ++i) {
            auto* cluster = static_cast<CTrickObjectCluster*>(m_objectClusters.GetItem(objects->GetElement(i)));
            if (cluster)
                cluster->SetGraffitiColor(player == -1 ? -1 : player + 1);
        }
    }
    m_params->Clear();
}

// 0x00589180
CTrickSpotHub* __cdecl AcquireTrickObjectManager(bool create)
{
    auto*& instance = g_trickObjectManager;
    if (!instance && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CTrickSpotHub), nullptr, 1);
        instance = memory ? static_cast<CTrickSpotHub*>(memory)->Construct() : nullptr;
    }
    ++g_trickObjectManagerReferences;
    return instance;
}

// 0x005891D0
CTrickObjectCluster* CTrickObjectCluster::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00589200
CTrickSpotHub* CTrickSpotHub::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00589230
bool CTrickSpotHub::ReleasePlayerClusters(int player)
{
    DebugOut::Printf("Clearing observer state\n");
    m_params->Clear();
    if (static_cast<OnlinePlay::Manager*>(NetManager())->OnServer()) {
        // (THUG2 lists the released clusters' names in a 128-entry buffer on the stack - a player holding more
        // overran it; the list itself isn't used, only its count.)
        static HeldClusterRelease release;
        release.player = player;
        release.count = 0;
        m_clusters.HandleCallback(reinterpret_cast<ListKit::HashTable::Callback>(&ReleaseHeldCluster), &release);
        if (release.count == 0)
            return false;
        GameModules::g_skateModule->BroadcastPoints(false);
    }
    ClearGraffitiColor(player + 1);
    return true;
}

namespace {

OnlinePlay::PlayerRecord* AsPlayer(void* player) { return static_cast<OnlinePlay::PlayerRecord*>(player); }

} // namespace

// 0x005892C0
void __cdecl ScoreTrickObjects(int player, uint32_t score, uint32_t count, uint32_t* objects, bool send)
{
    auto* net = static_cast<OnlinePlay::Manager*>(NetManager());
    void* skate = static_cast<void*>(GameModules::g_skateModule);
    NetLink::NetMessageInfo desc;
    desc.Construct();
    PlayerSearch search;
    if (send) {
        auto* server = *reinterpret_cast<NetLink::App**>(reinterpret_cast<uint8_t*>(net) + 4);
        NetLink::Conn* conn = nullptr;
        if (auto* skater = reinterpret_cast<uint8_t*>(reinterpret_cast<GameModules::Skate*>(skate)->GetRiderByNumber(static_cast<uint32_t>(player)))) {
            if (uint8_t* info = net->PlayerByObjectId(static_cast<uint16_t>(*reinterpret_cast<uint32_t*>(skater + 0x14))))
                conn = *reinterpret_cast<NetLink::Conn**>(info + 0x18);
        }
        uint8_t stolen[8] = {};
        count = g_trickObjectManager->LogTrickObjects(count, objects, stolen, player, score);
        if (count == 0)
            return;
        for (int victim = 0; victim < 8; ++victim) {
            if (!stolen[victim])
                continue;
            NetLink::NetMessageInfo stolenDesc;
            stolenDesc.Construct();
            TagStolenMessage message = {};
            message.thief = player;
            message.victim = victim;
            message.gameId = net->GetGameId();
            uint8_t* thief = net->PlayerByObjectId(static_cast<uint16_t>(player));
            stolenDesc.m_data = &message;
            stolenDesc.m_byteCount = sizeof(message);
            stolenDesc.m_id = kMsgTagStolen;
            server->PutOnOutbox(AsPlayer(thief)->GetLinkHandle(), &stolenDesc);
            message.thief = player;
            message.victim = victim;
            message.gameId = net->GetGameId();
            if (uint8_t* info = net->PlayerByObjectId(static_cast<uint16_t>(victim)))
                server->PutOnOutbox(AsPlayer(info)->GetLinkHandle(), &desc); // (not stolenDesc: as the original)
        }

        TrickObjectsMessage all = {};
        all.one = 1;
        all.player = player;
        all.score = score;
        all.count = count;
        all.gameId = net->GetGameId();
        std::memcpy(all.objects, objects, count * 4);
        desc.m_data = &all;
        desc.m_byteCount = static_cast<uint16_t>(count * 4 + offsetof(TrickObjectsMessage, objects));
        desc.m_id = kMsgTrickObjects;
        desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        for (void* info = net->BeginPlayerWalk(&search, false); info; info = net->StepPlayerWalk(&search, false))
            server->SendBulkMessage(AsPlayer(info)->GetLinkHandle(), desc.m_id, desc.m_byteCount, desc.m_data,
                                  "TrickObj Buffer", kGroupGame, true, false);

        ObserverTrickObjectsMessage observed = {};
        observed.count = count;
        observed.player = player;
        observed.gameId = net->GetGameId();
        std::memcpy(observed.objects, objects, count * 4);
        desc.m_data = &observed;
        desc.m_byteCount = static_cast<uint16_t>(count * 4 + offsetof(ObserverTrickObjectsMessage, objects));
        desc.m_id = kMsgObserverTrickObjects;
        for (void* info = net->BeginPlayerWalk(&search, true); info; info = net->StepPlayerWalk(&search, true)) {
            if (AsPlayer(info)->IsObserver())
                server->SendBulkMessage(AsPlayer(info)->GetLinkHandle(), desc.m_id, desc.m_byteCount, desc.m_data,
                                      "TrickObj Buffer", kGroupGame, true, false);
        }
        reinterpret_cast<GameModules::Skate*>(skate)->BroadcastPoints(false);
        if (conn->IsLocal()) // (no null check: as the original)
            return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        auto* cluster = static_cast<CTrickObjectCluster*>(
            g_trickObjectManager->m_objectClusters.GetItem(objects[i]));
        if (cluster)
            cluster->SetGraffitiColor(player == -1 ? -1 : player + 1);
    }
}

// 0x00589670
void CTrickSpotHub::GiveFreeClusters(int player)
{
    FreeClusterList list;
    list.count = 0;
    m_clusters.HandleCallback(reinterpret_cast<ListKit::HashTable::Callback>(&ListFreeCluster), &list);
    if (list.count)
        ScoreTrickObjects(player, 1000, list.count, list.names, true);
}

// 0x005896D0
bool CTrickSpotHub::AddCluster(uint32_t name)
{
    if (GetCluster(name))
        return false;
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x378) == kLoadCreatedPark) {
        ParkEditing::CRailAuthoring::Instance()->ClearClusterColours(name);
    }
    void* memory = MemSys::AllocZeroed(sizeof(CTrickObjectCluster));
    m_clusters.StoreEntry(name, memory ? static_cast<CTrickObjectCluster*>(memory)->Construct(name) : nullptr);
    ++m_clusterCount;
    m_objectClusters.StoreEntry(name, GetCluster(name));
    return true;
}

// 0x00589760
bool CTrickSpotHub::AddObject(uint32_t objectName, uint32_t clusterName)
{
    const bool added = GetCluster(clusterName)->AddObject(objectName);
    if (added && objectName != clusterName)
        m_objectClusters.StoreEntry(objectName, GetCluster(clusterName));
    return added;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kTrickClusterNodeVtable_slots[] = {&GameObjects::TrickClusterNode::Destroy};
void** const kTrickClusterNodeVtable = reinterpret_cast<void**>(kTrickClusterNodeVtable_slots);
static VtableSlot kTrickObjectNodeVtable_slots[] = {&GameObjects::TrickObjectNode::Destroy};
void** const kTrickObjectNodeVtable = reinterpret_cast<void**>(kTrickObjectNodeVtable_slots);
static VtableSlot kTrickObjectVtable_slots[] = {&GameObjects::CTrickSpot::Destroy};
void** const kTrickObjectVtable = reinterpret_cast<void**>(kTrickObjectVtable_slots);
static VtableSlot kTrickClusterVtable_slots[] = {&GameObjects::CTrickObjectCluster::Destroy};
void** const kTrickClusterVtable = reinterpret_cast<void**>(kTrickClusterVtable_slots);
static VtableSlot kTrickObjectManagerVtable_slots[] = {&GameObjects::CTrickSpotHub::Destroy};
void** const kTrickObjectManagerVtable = reinterpret_cast<void**>(kTrickObjectManagerVtable_slots);

} // namespace
} // namespace Obj
