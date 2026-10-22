// Pieces of the trick object code, 0x005880E0-0x0058818C and 0x00588200-0x00588BCF (the trick objects, the
// manager's lookups, graffiti state and graffiti log).
// See trick_objects.h.

#include "game/obj/trick_objects.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

static_assert(offsetof(CTrickObjectCluster, m_name) == 0x14 && offsetof(CTrickObjectCluster, m_scores) == 0x1C &&
                  offsetof(CTrickObjectCluster, m_owner) == 0x3C && offsetof(CTrickObjectCluster, m_owned) == 0x40,
              "layout must match the original");


namespace {

constexpr uint32_t kNetGraffiti = 0x5E2EA50C;         // netgraffiti
constexpr uint32_t kGraffitiTeamScoring = 0xAB80CC2F; // graffiti_team_scoring (team_graffiti_mode)

// Which clusters of the current log changed hands (by their index in its list). THUG2's was a 128-byte global at
// 0x007CE508; enlarged as THUGPRO's DLL enlarged it (its Net::InitPatches, "Fix 256 graff tag crash": idea from
// Morten1337's thugpro_proxy, written here as our own buffer) - a list longer than the mask is cut to its size.
uint8_t s_changeMask[CTrickObjectManager::kMaxLoggedClusters];

// The game mode (0x007CE470) and the network manager's players (0x007CCE14): PlayerInfo +0x14 its skater (+0x14 the
// skater's object id), +0x30 its team. Walked with a Lst::Search<PlayerInfo> (vtable 0x0064C1A8).
void* GameMode() { return GameVariable<void*>(0x007CE470); }
void* NetManager() { return GameVariable<void*>(0x007CCE14); }
struct PlayerSearch {
    void* m_vtable = reinterpret_cast<void*>(0x0064C1A8);
    void* m_node = nullptr;
};
uint8_t* FirstPlayer(PlayerSearch* search)
{
    return GameFunction<uint8_t*(__thiscall*)(void*, PlayerSearch*, int)>(0x005372C0)(NetManager(), search, 0);
}
uint8_t* NextPlayer(PlayerSearch* search)
{
    return GameFunction<uint8_t*(__thiscall*)(void*, PlayerSearch*, int)>(0x00537010)(NetManager(), search, 0);
}
uint8_t* PlayerByObjectId(int id)
{
    return GameFunction<uint8_t*(__thiscall*)(void*, int)>(0x0053AEA0)(NetManager(), id);
}
int Team(uint8_t* player) { return *reinterpret_cast<int*>(player + 0x30); }
uint8_t* SkaterOf(uint8_t* player) { return *reinterpret_cast<uint8_t**>(player + 0x14); }
int ObjectId(uint8_t* skater) { return *reinterpret_cast<int*>(skater + 0x14); }

// Team graffiti scored by team: the game mode netgraffiti, a team game, team_graffiti_mode graffiti_team_scoring.
bool TeamScoring()
{
    return GameFunction<uint32_t(__thiscall*)(void*)>(0x005532A0)(GameMode()) == kNetGraffiti &&
           GameFunction<bool(__thiscall*)(void*)>(0x005535F0)(GameMode()) &&
           GameFunction<uint32_t(__thiscall*)(void*)>(0x00553300)(GameMode()) == kGraffitiTeamScoring;
}

} // namespace

// 0x005880E0
void ReleaseTrickObjectManager()
{
    uint32_t& count = GameVariable<uint32_t>(0x007CE504);
    if (count > 1) {
        --count;
        return;
    }
    void** instance = GameVariable<void**>(0x007CE500);
    if (instance)
        reinterpret_cast<void*(__thiscall*)(void*, int)>(static_cast<void**>(*instance)[0])(instance, 1);
    GameVariable<void*>(0x007CE500) = nullptr;
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
void CTrickCluster::SetParams(Script::CStruct* params)
{
    m_params->Clear();
    if (params)
        m_params->AppendStructure(params);
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
uint32_t CTrickObjectManager::LogTrickObjects(uint32_t count, uint32_t* objects, uint8_t* stolen, int player,
                                              uint32_t score)
{
    Dbg::Printf("RequestLogTrick called\n");
    if (count > kMaxLoggedClusters) // (ours: the original's mask overran here)
        count = kMaxLoggedClusters;
    if (count)
        std::memset(s_changeMask, 0, count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* cluster = static_cast<CTrickObjectCluster*>(m_objectClusters.GetItem(objects[i]));
        if (!cluster)
            continue;
        uint8_t* goals = GameFunction<uint8_t*(__cdecl*)()>(0x005591B0)();
        GameFunction<void(__thiscall*)(uint8_t*, uint32_t, uint32_t)>(0x00558770)(goals, cluster->m_name, score);
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

void** const kTrickObjectVtable = reinterpret_cast<void**>(0x0064F0D8);
void** const kTrickObjectNodeVtable = reinterpret_cast<void**>(0x0064F0D4);
void** const kTrickClusterNodeVtable = reinterpret_cast<void**>(0x0064F0D0);

// The level sector named `name` in any scene (0x0048C510) and its colour: 0x0049E670 cleared, 0x0049E980 read
// (r, g, b, a; zero without a mesh), the mesh's (+0x14) vtable +0x18 set.
uint8_t* FindSector(uint32_t name) { return GameFunction<uint8_t*(__cdecl*)(uint32_t)>(0x0048C510)(name); }
void ClearSectorColor(uint8_t* sector) { GameFunction<void(__thiscall*)(uint8_t*)>(0x0049E670)(sector); }
uint32_t* GetSectorColor(uint8_t* sector, uint32_t* out)
{
    return GameFunction<uint32_t*(__thiscall*)(uint8_t*, uint32_t*)>(0x0049E980)(sector, out);
}

// Graffiti colour `index`: graffitiColors[index]'s r, g, b.
Script::CArray* GraffitiColor(int index)
{
    Script::CArray* colors = Script::GetArray("graffitiColors");
    return reinterpret_cast<Script::CArray*>(colors->GetElement(static_cast<uint32_t>(index)));
}

} // namespace

// 0x00588200
bool CTrickObject::SetGraffitiColor(int index)
{
    uint8_t* sector = FindSector(m_name);
    Script::CArray* color = GraffitiColor(index);
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
bool CTrickObject::ClearGraffitiColor(int index)
{
    uint8_t* sector = FindSector(m_name);
    Script::CArray* color = GraffitiColor(index);
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
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005885B0
TrickClusterNode* TrickClusterNode::Destroy(unsigned int flags)
{
    UnlinkAs(kTrickClusterNodeVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00588630
bool CTrickObjectCluster::AddObject(uint32_t name)
{
    auto* object = static_cast<CTrickObject*>(Mem::Calloc(sizeof(CTrickObject)));
    if (!object)
        return false;
    object->m_node.data = object;
    object->m_node.priority = 0;
    object->m_node.next = &object->m_node;
    object->m_node.prev = &object->m_node;
    object->m_node.vtable = kTrickObjectVtable;
    object->m_name = name;
    ClearSectorColor(FindSector(name));
    if (!GameVariable<bool>(0x00684FA8) && Script::GetInteger("show_all_trick_objects"))
        object->SetGraffitiColor(2);
    // Last in the list (before the head).
    object->m_node.next = &m_objects;
    object->m_node.prev = m_objects.prev;
    m_objects.prev->next = &object->m_node;
    m_objects.prev = &object->m_node;
    return true;
}

// 0x00588380
CTrickObjectCluster* CTrickObjectManager::GetObjectCluster(uint32_t objectName)
{
    return static_cast<CTrickObjectCluster*>(m_objectClusters.GetItem(objectName));
}

// 0x005883C0
CTrickObjectCluster* CTrickObjectManager::GetCluster(uint32_t name)
{
    return static_cast<CTrickObjectCluster*>(m_clusters.GetItem(name));
}

// 0x00588400
int CTrickObjectManager::CountClustersHeldBy(int player)
{
    ClusterCount counter = {0, static_cast<uint32_t>(player)};
    m_clusters.HandleCallback(reinterpret_cast<Lst::HashTable::Callback>(&CountObjectInCluster), &counter);
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
uint32_t CTrickObjectManager::GetClusterName(uint8_t index)
{
    ClusterAtIndex search = {0, index, false};
    m_clusters.HandleCallback(reinterpret_cast<Lst::HashTable::Callback>(&FindClusterAtIndex), &search);
    return search.name;
}

// 0x005884B0
void __cdecl WriteHeldCluster(CTrickObjectCluster* cluster, GraffitiStateWriter* writer)
{
    if (!cluster->m_owned || cluster->m_owner != writer->player)
        return;
    ClusterIndexSearch search = {cluster->m_name, 0, false};
    writer->manager->m_clusters.HandleCallback(reinterpret_cast<Lst::HashTable::Callback>(&FindClusterIndex), &search);
    writer->buffer[writer->count + 8] = search.index; // (a byte: past 255 clusters it wraps, as THUG2's and THUGPRO's)
    ++writer->count;
    ++writer->buffer[writer->player];
}

// 0x00588520
int CTrickObjectManager::WriteGraffitiState(uint8_t* buffer)
{
    GraffitiStateWriter writer = {0, 0, buffer, this};
    for (int player = 0; player < 8; ++player) {
        buffer[player] = 0;
        writer.player = player;
        m_clusters.HandleCallback(reinterpret_cast<Lst::HashTable::Callback>(&WriteHeldCluster), &writer);
        Dbg::Printf("Building graffiti state message %d\n", buffer[player]);
    }
    return writer.count + 8;
}

// 0x005889F0
bool CTrickObjectManager::RemoveAllClusters()
{
    m_clusters.HandleCallback(reinterpret_cast<Lst::HashTable::Callback>(0x004B22A0), nullptr); // (Nx::DeleteObject)
    m_clusters.FlushAllItems();
    m_clusterCount = 0;
    m_objectClusters.FlushAllItems();
    return true;
}

// 0x00588A20
bool CTrickObjectList::Add(uint32_t objectName)
{
    const uint32_t listed = static_cast<uint32_t>(m_count) > kCapacity ? kCapacity : static_cast<uint32_t>(m_count);
    CTrickObjectCluster* cluster = GameVariable<CTrickObjectManager*>(0x007CE500)->GetObjectCluster(objectName);
    if (!cluster || !cluster->m_name)
        return false;
    for (uint32_t i = 0; i < listed; ++i) {
        if (m_items[i] == cluster->m_name)
            return false;
    }
    void* goals = GameFunction<void*(__cdecl*)()>(0x005591B0)(); // the goal manager
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x00559150)(goals, cluster->m_name);
    m_items[m_count & (kCapacity - 1)] = cluster->m_name;
    ++m_count;
    return false;
}

// 0x00588AA0
uint32_t CTrickObjectList::CopyClusterNames(uint32_t* out, uint32_t maxBytes)
{
    const uint32_t listed = static_cast<uint32_t>(m_count) > kCapacity ? kCapacity : static_cast<uint32_t>(m_count);
    const CTrickObjectManager* manager = GameVariable<CTrickObjectManager*>(0x007CE500);
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
void CTrickObject::DestroyBody() { m_node.UnlinkAs(kTrickClusterNodeVtable); }

// 0x00588B40
CTrickObject* CTrickObject::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
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

} // namespace Obj

REPLACE(0x005886C0, &Obj::CTrickObjectManager::LogTrickObjects);
REPLACE(0x00588200, &Obj::CTrickObject::SetGraffitiColor);
REPLACE(0x00588270, &Obj::CTrickObject::ClearGraffitiColor);
REPLACE(0x00588380, &Obj::CTrickObjectManager::GetObjectCluster);
REPLACE(0x005883C0, &Obj::CTrickObjectManager::GetCluster);
REPLACE_SOURCE_ONLY(0x005889E0, &Obj::CTrickObjectManager::GetCluster); // a jump to 0x005883C0
REPLACE(0x00588400, &Obj::CTrickObjectManager::CountClustersHeldBy);
REPLACE(0x00588430, Obj::FindClusterIndex);
REPLACE(0x00588450, Obj::FindClusterAtIndex);
REPLACE(0x00588480, &Obj::CTrickObjectManager::GetClusterName);
REPLACE(0x005884B0, Obj::WriteHeldCluster);
REPLACE(0x00588520, &Obj::CTrickObjectManager::WriteGraffitiState);
REPLACE(0x00588590, &Obj::TrickObjectNode::DestroyBody);
REPLACE(0x005885B0, &Obj::TrickClusterNode::Destroy);
REPLACE(0x005885F0, &Obj::TrickObjectNode::Destroy);
REPLACE(0x00588630, &Obj::CTrickObjectCluster::AddObject);
REPLACE(0x005880E0, Obj::ReleaseTrickObjectManager);
REPLACE(0x00588120, Obj::CountObjectInCluster);
REPLACE(0x00588150, &Obj::CTrickCluster::SetParams);
REPLACE(0x00588170, &Obj::CTrickObjectList::Construct);
REPLACE(0x00588180, &Obj::CTrickObjectList::Clear);
REPLACE(0x005889F0, &Obj::CTrickObjectManager::RemoveAllClusters);
REPLACE(0x00588A20, &Obj::CTrickObjectList::Add);
REPLACE(0x00588AA0, &Obj::CTrickObjectList::CopyClusterNames);
REPLACE(0x00588B40, &Obj::CTrickObject::Destroy);
REPLACE(0x00588B70, &Obj::TrickObjectHead::UnlinkAll);
REPLACE(0x00588BB0, &Obj::CTrickObject::DestroyBody);
