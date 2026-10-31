// Pieces of the trick object code, 0x005880E0-0x0058818C and 0x00588200-0x005897A6 (the trick objects, the
// manager's lookups, graffiti state and graffiti log).
// See trick_objects.h.

#include "engine/objects/trick_objects.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/sector.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/player_info.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/sys/game_config.h"

namespace objects {

static_assert(offsetof(TrickObjectCluster, label_) == 0x14 && offsetof(TrickObjectCluster, scores_) == 0x1C &&
                  offsetof(TrickObjectCluster, owner_) == 0x3C && offsetof(TrickObjectCluster, owned_) == 0x40,
              "layout must match the original");

objects::TrickSpotHub* g_trick_spot_hub;
uint32_t g_trick_object_manager_references;

namespace {

constexpr uint32_t kNetGraffiti = 0x5E2EA50C;         // netgraffiti
constexpr uint32_t kGraffitiTeamScoring = 0xAB80CC2F; // graffiti_team_scoring (team_graffiti_mode)

// Which clusters of the current log changed hands (by their index in its list). THUG2's was a 128-byte global at
// 0x007CE508; enlarged as THUGPRO's DLL enlarged it (its NetLink::InitPatches, "Fix 256 graff tag crash": idea from
// Morten1337's thugpro_proxy, written here as our own buffer) - a list longer than the mask is cut to its size.
uint8_t s_dirty_clusters[TrickSpotHub::kMaxLoggedClusters];

// The game mode (0x007CE470) and the network manager's players (0x007CCE14): PlayerRecord +0x14 its skater (+0x14 the
// skater's object id), +0x30 its team. Walked with a lists::Hunt<PlayerRecord> (vtable 0x0064C1A8).
void* get_game_mode() { return static_cast<void*>(goals::g_game_mode); }
void* net_session_slot() { return reinterpret_cast<void*>(online::g_online_hub); }
using PlayerSearch = online::PlayerSearch;
uint8_t* start_roster_sweep(PlayerSearch* search)
{
    return static_cast<uint8_t*>(static_cast<online::Hub*>(net_session_slot())->begin_player_walk(search, false));
}
uint8_t* step_roster_sweep(PlayerSearch* search)
{
    return static_cast<uint8_t*>(static_cast<online::Hub*>(net_session_slot())->step_player_walk(search, false));
}
uint8_t* find_player_slot_by_obj(int id)
{
    return static_cast<online::Hub*>(net_session_slot())->find_player_slot_by_obj(static_cast<uint16_t>(id));
}
int get_team(uint8_t* player) { return *reinterpret_cast<int*>(player + 0x30); }
uint8_t* skater_of(uint8_t* player) { return *reinterpret_cast<uint8_t**>(player + 0x14); }
int current_object_id(uint8_t* skater) { return *reinterpret_cast<int*>(skater + 0x14); }

// Team graffiti scored by team: the game mode netgraffiti, a team game, team_graffiti_mode graffiti_team_scoring.
bool team_scoring()
{
    return static_cast<goals::MatchRules*>(get_game_mode())->get_name_key() == kNetGraffiti &&
           static_cast<goals::MatchRules*>(get_game_mode())->is_squad_match() != 0 &&
           static_cast<goals::MatchRules*>(get_game_mode())->get_team_graffiti_mode() == kGraffitiTeamScoring;
}

} // namespace

// 0x005880E0
void release_trick_object_manager()
{
    uint32_t& count = g_trick_object_manager_references;
    if (count > 1) {
        --count;
        return;
    }
    delete g_trick_spot_hub;
    g_trick_spot_hub = nullptr;
    count = 0;
}

// 0x00588120
ClusterCount* __cdecl count_object_in_cluster(uint8_t* object, ClusterCount* counter)
{
    if (object[0x40] && counter->cluster == *reinterpret_cast<uint32_t*>(object + 0x3C))
        counter->count += 1;
    return counter;
}

// 0x00588150
void TrickSpotHub::change_args(script::Struct* params)
{
    args_->get_clear();
    if (params)
        args_->merge_structure(params);
}

// 0x00588170
HitClusterLog* HitClusterLog::make_new()
{
    num_ = 0;
    return this;
}

// 0x00588180
bool HitClusterLog::get_clear()
{
    num_ = 0;
    return true;
}

// 0x005886C0
uint32_t TrickSpotHub::log_trick_objects(uint32_t count, uint32_t* objects, uint8_t* stolen, int player, uint32_t score)
{
    debug::printf("RequestLogTrick called\n");
    if (count > kMaxLoggedClusters) // (ours: the original's mask overran here)
        count = kMaxLoggedClusters;
    if (count)
        std::memset(s_dirty_clusters, 0, count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* cluster = static_cast<TrickObjectCluster*>(object_clusters_.fetch_entry(objects[i]));
        if (!cluster)
            continue;
        goals::get_objective_board()->kill_cluster(cluster->label_, static_cast<int>(score));
        if (score > cluster->scores_[player])
            cluster->scores_[player] = score;
        uint32_t total = score;
        if (team_scoring()) {
            if (uint8_t* me = find_player_slot_by_obj(player)) {
                // The team's score: each of its players' best on this cluster.
                int team = get_team(me);
                total = 0;
                PlayerSearch search;
                for (uint8_t* other = start_roster_sweep(&search); other; other = step_roster_sweep(&search)) {
                    if (skater_of(other) && get_team(other) == team)
                        total += cluster->scores_[current_object_id(skater_of(other))];
                }
            }
        }
        if (total <= cluster->record_points_)
            continue;
        bool changed = cluster->owner_ != player;
        if (changed && team_scoring()) {
            // Taken from a team mate: only if the player beat one of the team's own best scores on it.
            uint8_t* holder = find_player_slot_by_obj(static_cast<uint16_t>(cluster->owner_));
            int team = get_team(find_player_slot_by_obj(player));
            if (get_team(holder) == team) {
                changed = false;
                PlayerSearch search;
                for (uint8_t* other = start_roster_sweep(&search); other; other = step_roster_sweep(&search)) {
                    if (skater_of(other) && get_team(other) == team &&
                        score > cluster->scores_[current_object_id(skater_of(other))]) {
                        changed = true;
                        break;
                    }
                }
            }
        }
        if (!cluster->owned_ || changed)
            s_dirty_clusters[i] = 1;
        if (cluster->owned_ && changed)
            stolen[cluster->owner_] = 1;
        if (changed)
            cluster->owner_ = player;
        cluster->record_points_ = total;
        cluster->owned_ = true;
    }
    // The list compacted to the objects whose cluster changed hands.
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (s_dirty_clusters[i])
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
constexpr uint32_t kLoadCreatedPark = 0xB664035D; // load_sk5ed_gameplay (the skate module's level, +0x378)

// The level sector named `name` in any scene (0x0048C510) and its colour: 0x0049E670 cleared, 0x0049E980 read
// (r, g, b, a; zero without a mesh), the mesh's (+0x14) vtable +0x18 set.
uint8_t* find_sector(uint32_t name) { return static_cast<uint8_t*>(render::engine::look_up_sector(name)); }
void clear_sector_color(uint8_t* sector) { reinterpret_cast<render::WorldCell*>(sector)->wipe_tint(); }
uint32_t* get_sector_color(uint8_t* sector, uint32_t* out)
{
    return reinterpret_cast<uint32_t*>(
        reinterpret_cast<render::WorldCell*>(sector)->fetch_tint(reinterpret_cast<uint8_t*>(out)));
}

// Graffiti colour `index`: graffitiColors[index]'s r, g, b.
script::Array* graffiti_color(int index)
{
    script::Array* colors = script::fetch_list("graffitiColors");
    return reinterpret_cast<script::Array*>(colors->fetch_part(static_cast<uint32_t>(index)));
}

} // namespace

// 0x00588200
bool TrickSpot::set_graffiti_color(int index)
{
    uint8_t* sector = find_sector(label_);
    script::Array* color = graffiti_color(index);
    uint8_t rgba[4];
    rgba[0] = static_cast<uint8_t>(color->fetch_part(0));
    rgba[1] = static_cast<uint8_t>(color->fetch_part(1));
    rgba[2] = static_cast<uint8_t>(color->fetch_part(2));
    rgba[3] = 0x80;
    if (auto* geom = static_cast<render::Geom*>(reinterpret_cast<render::WorldCell*>(sector)->geom_handle_)) {
        uint32_t packed;
        std::memcpy(&packed, rgba, 4);
        geom->change_tint(packed);
    }
    return true;
}

// 0x00588270
bool TrickSpot::clear_graffiti_color(int index)
{
    uint8_t* sector = find_sector(label_);
    script::Array* color = graffiti_color(index);
    uint8_t r = static_cast<uint8_t>(color->fetch_part(0));
    uint8_t g = static_cast<uint8_t>(color->fetch_part(1));
    uint8_t b = static_cast<uint8_t>(color->fetch_part(2));
    uint32_t packed;
    get_sector_color(sector, &packed);
    uint8_t current[4];
    std::memcpy(current, &packed, 4);
    if (current[0] == r && current[1] == g && current[2] == b)
        clear_sector_color(sector);
    return true;
}

// 0x00588590
void TrickObjectNode::destroy_body() { unlink_as(kTrickObjectNodeVtable); }

// 0x005885F0
TrickObjectNode* TrickObjectNode::teardown(unsigned int flags)
{
    unlink_as(kTrickObjectNodeVtable);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x005885B0
TrickClusterNode* TrickClusterNode::teardown(unsigned int flags)
{
    unlink_as(kTrickClusterNodeVtable);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00588630
bool TrickObjectCluster::add_object(uint32_t name)
{
    auto* object = static_cast<TrickSpot*>(memory::alloc_zeroed(sizeof(TrickSpot)));
    if (!object)
        return false;
    object->list_link_.data = object;
    object->list_link_.priority = 0;
    object->list_link_.next = &object->list_link_;
    object->list_link_.prev = &object->list_link_;
    object->list_link_.vtable = kTrickObjectVtable;
    object->label_ = name;
    clear_sector_color(find_sector(name));
    if (!game_config::g_from_cd && script::fetch_int_value("show_all_trick_objects"))
        object->set_graffiti_color(2);
    // Last in the list (before the head).
    object->list_link_.next = &entities_;
    object->list_link_.prev = entities_.prev;
    entities_.prev->next = &object->list_link_;
    entities_.prev = &object->list_link_;
    return true;
}

// 0x00588380
TrickObjectCluster* TrickSpotHub::get_object_cluster(uint32_t object_name)
{
    return static_cast<TrickObjectCluster*>(object_clusters_.fetch_entry(object_name));
}

// 0x005883C0
TrickObjectCluster* TrickSpotHub::get_cluster(uint32_t name)
{
    return static_cast<TrickObjectCluster*>(clusters_.fetch_entry(name));
}

// 0x00588400
int TrickSpotHub::count_clusters_held_by(int player)
{
    ClusterCount counter = {0, static_cast<uint32_t>(player)};
    clusters_.run_hook(reinterpret_cast<lists::KeyMap::HookFn>(&count_object_in_cluster), &counter);
    return counter.count;
}

// 0x00588430
void __cdecl find_cluster_index(TrickObjectCluster* cluster, ClusterIndexSearch* search)
{
    if (search->found)
        return;
    if (search->name == cluster->label_)
        search->found = true;
    else
        ++search->index;
}

// 0x00588450
void __cdecl find_cluster_at_index(TrickObjectCluster* cluster, ClusterAtIndex* search)
{
    if (search->found)
        return;
    if (search->countdown == 0) {
        search->found = true;
        search->name = cluster->label_;
    } else {
        --search->countdown;
    }
}

// 0x00588480
uint32_t TrickSpotHub::get_cluster_name(uint8_t index)
{
    ClusterAtIndex search = {0, index, false};
    clusters_.run_hook(reinterpret_cast<lists::KeyMap::HookFn>(&find_cluster_at_index), &search);
    return search.name;
}

// 0x005884B0
void __cdecl write_held_cluster(TrickObjectCluster* cluster, GraffitiStateWriter* writer)
{
    if (!cluster->owned_ || cluster->owner_ != writer->player)
        return;
    ClusterIndexSearch search = {cluster->label_, 0, false};
    writer->manager->clusters_.run_hook(reinterpret_cast<lists::KeyMap::HookFn>(&find_cluster_index), &search);
    writer->buffer[writer->count + 8] = search.index; // (a byte: past 255 clusters it wraps, as THUG2's and THUGPRO's)
    ++writer->count;
    ++writer->buffer[writer->player];
}

// 0x00588520
int TrickSpotHub::write_graffiti_state(uint8_t* buffer)
{
    GraffitiStateWriter writer = {0, 0, buffer, this};
    for (int player = 0; player < 8; ++player) {
        buffer[player] = 0;
        writer.player = player;
        clusters_.run_hook(reinterpret_cast<lists::KeyMap::HookFn>(&write_held_cluster), &writer);
        debug::printf("Building graffiti state message %d\n", buffer[player]);
    }
    return writer.count + 8;
}

// 0x005889F0
bool TrickSpotHub::remove_all_clusters()
{
    clusters_.run_hook(reinterpret_cast<lists::KeyMap::HookFn>(&render::delete_object), nullptr);
    clusters_.empty_table();
    cluster_count_ = 0;
    object_clusters_.empty_table();
    return true;
}

// 0x00588A20
bool HitClusterLog::append_one(uint32_t object_name)
{
    const uint32_t listed = static_cast<uint32_t>(num_) > kCapacity ? kCapacity : static_cast<uint32_t>(num_);
    TrickObjectCluster* cluster = g_trick_spot_hub->get_object_cluster(object_name);
    if (!cluster || !cluster->label_)
        return false;
    for (uint32_t i = 0; i < listed; ++i) {
        if (items_[i] == cluster->label_)
            return false;
    }
    goals::get_objective_board()->trick_object_hit(cluster->label_);
    items_[num_ & (kCapacity - 1)] = cluster->label_;
    ++num_;
    return false;
}

// 0x00588AA0
uint32_t HitClusterLog::copy_cluster_names(uint32_t* out, uint32_t max_bytes)
{
    const uint32_t listed = static_cast<uint32_t>(num_) > kCapacity ? kCapacity : static_cast<uint32_t>(num_);
    const TrickSpotHub* manager = g_trick_spot_hub;
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < listed; ++i) {
        auto* cluster = static_cast<TrickObjectCluster*>(manager->object_clusters_.fetch_entry(items_[i]));
        if (cluster && cluster->label_) {
            *out++ = items_[i];
            bytes += 4;
        }
        if (bytes >= max_bytes)
            break;
    }
    return bytes;
}

// 0x00588BB0
void TrickSpot::destroy_body() { list_link_.unlink_as(kTrickClusterNodeVtable); }

// 0x00588B40
TrickSpot* TrickSpot::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00588B70
void TrickObjectHead::unlink_all()
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
TrickObjectCluster* TrickObjectCluster::make_new(uint32_t name)
{
    list_link_.data = this;
    list_link_.priority = 0;
    list_link_.next = &list_link_;
    list_link_.prev = &list_link_;
    list_link_.vtable = kTrickClusterVtable;
    entities_.vtable = kTrickObjectVtable;
    entities_.data = reinterpret_cast<void*>(-1); // (the head)
    entities_.priority = 0;
    entities_.next = &entities_;
    entities_.prev = &entities_;
    label_ = name;
    for (uint32_t& score : scores_)
        score = 0;
    record_points_ = 0;
    owner_ = 0;
    owned_ = false;
    return this;
}

namespace {

// The objects in a cluster's list, and the one at `index` (null past the end). (The original reads each node's data
// before checking it for null.)
uint32_t count_objects(const memory::ListNode& head)
{
    uint32_t count = 0;
    for (const memory::ListNode* node = head.next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}
memory::ListNode* object_at(const memory::ListNode& head, uint32_t index)
{
    for (memory::ListNode* node = head.next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next) {
        if (index-- == 0)
            return node;
    }
    return nullptr;
}

} // namespace

// 0x00588C30
void TrickObjectCluster::destroy_body()
{
    list_link_.vtable = kTrickClusterVtable;
    for (uint32_t count = count_objects(entities_); count != 0; --count) {
        void* object = object_at(entities_, 0)->data;
        if (object)
            reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(object))[0])(object, 1);
    }
    static_cast<TrickObjectHead&>(entities_).unlink_all();
    entities_.unlink_as(kTrickClusterNodeVtable);
    list_link_.unlink_as(kTrickObjectNodeVtable);
}

// 0x00588D20
bool TrickObjectCluster::set_graffiti_color(int index)
{
    const uint32_t count = count_objects(entities_);
    for (uint32_t i = 0; i < count; ++i)
        static_cast<TrickSpot*>(object_at(entities_, i)->data)->set_graffiti_color(index);
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(modules::g_skate_module) + 0x378) == kLoadCreatedPark) {
        park_editing::RailAuthoring::sole()->set_cluster_colour(label_, index);
    }
    return true;
}

// 0x00588DC0
TrickSpotHub::TrickSpotHub() : clusters_(8), object_clusters_(8)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->script_heap_);
    script::Struct* params = script::spawn_param_bag();
    args_ = params ? params->prepare_all() : nullptr;
    memory->leave_heap_scope();
}

// 0x00588E90 (the tables' destructors run after it: the objects' table, then the clusters')
TrickSpotHub::~TrickSpotHub() { remove_all_clusters(); }

// 0x00588F40
bool TrickObjectCluster::clear_graffiti_color(int index)
{
    const uint32_t count = count_objects(entities_);
    for (uint32_t i = 0; i < count; ++i) {
        if (!static_cast<TrickSpot*>(object_at(entities_, i)->data)->clear_graffiti_color(index))
            return false;
    }
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(modules::g_skate_module) + 0x378) == kLoadCreatedPark) {
        park_editing::RailAuthoring::sole()->clear_cluster_colours(label_);
    }
    return true;
}

// 0x00588FF0
bool TrickSpotHub::set_object_graffiti_color(uint32_t object_name, int player)
{
    TrickObjectCluster* cluster = get_object_cluster(object_name);
    if (!cluster)
        return false;
    cluster->set_graffiti_color(player == -1 ? -1 : player + 1);
    return true;
}

// 0x00589030
bool TrickSpotHub::clear_graffiti_color(int index)
{
    clusters_.iter_bucket_ = -1;
    clusters_.iter_item_ = nullptr;
    while (clusters_.step_iterator() && clusters_.iter_item_ && clusters_.iter_item_->value) {
        if (!static_cast<TrickObjectCluster*>(clusters_.iter_item_->value)->clear_graffiti_color(index))
            return false;
    }
    return true;
}

// 0x00589080
bool TrickSpotHub::add_object_to_cluster(uint32_t object_name, uint32_t cluster_name)
{
    object_clusters_.store_entry(object_name, get_cluster(cluster_name));
    return true;
}

// 0x00588300
void __cdecl release_held_cluster(TrickObjectCluster* cluster, HeldClusterRelease* release)
{
    if (!cluster->owned_ || cluster->owner_ != release->player)
        return;
    cluster->owned_ = false;
    cluster->record_points_ = 0;
    for (uint32_t& score : cluster->scores_)
        score = 0;
    release->names[release->count++] = cluster->label_;
}

// 0x00588350
void __cdecl list_free_cluster(TrickObjectCluster* cluster, FreeClusterList* list)
{
    if (list->count < 32 && !cluster->owned_)
        list->names[list->count++] = cluster->label_;
}

// 0x005890B0
void TrickSpotHub::apply_player_graffiti()
{
    for (int player = 0; player < 8; ++player) {
        char name[32];
        std::snprintf(name, sizeof(name), "Skater%d", player);
        script::Array* objects;
        if (!args_->fetch_list(name, &objects, 0))
            continue;
        for (int i = 0; i < static_cast<int>(objects->extent_); ++i) {
            auto* cluster = static_cast<TrickObjectCluster*>(object_clusters_.fetch_entry(objects->fetch_part(i)));
            if (cluster)
                cluster->set_graffiti_color(player == -1 ? -1 : player + 1);
        }
    }
    args_->get_clear();
}

// 0x00589180
TrickSpotHub* __cdecl acquire_trick_object_manager(bool create)
{
    auto*& instance = g_trick_spot_hub;
    if (!instance && create) {
        instance = new TrickSpotHub;
    }
    ++g_trick_object_manager_references;
    return instance;
}

// 0x005891D0
TrickObjectCluster* TrickObjectCluster::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00589230
bool TrickSpotHub::release_player_clusters(int player)
{
    debug::printf("Clearing observer state\n");
    args_->get_clear();
    if (static_cast<online::Hub*>(net_session_slot())->get_on_server()) {
        // (THUG2 lists the released clusters' names in a 128-entry buffer on the stack - a player holding more
        // overran it; the list itself isn't used, only its count.)
        static HeldClusterRelease release;
        release.player = player;
        release.count = 0;
        clusters_.run_hook(reinterpret_cast<lists::KeyMap::HookFn>(&release_held_cluster), &release);
        if (release.count == 0)
            return false;
        modules::g_skate_module->broadcast_points(false);
    }
    clear_graffiti_color(player + 1);
    return true;
}

namespace {

online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

} // namespace

// 0x005892C0
void __cdecl score_trick_objects(int player, uint32_t score, uint32_t count, uint32_t* objects, bool send)
{
    auto* net = static_cast<online::Hub*>(net_session_slot());
    void* skate = static_cast<void*>(modules::g_skate_module);
    net::NetMessageInfo desc;
    desc.make_new();
    PlayerSearch search;
    if (send) {
        auto* server = *reinterpret_cast<net::AppHost**>(reinterpret_cast<uint8_t*>(net) + 4);
        net::Conn* conn = nullptr;
        if (auto* skater = reinterpret_cast<uint8_t*>(
                reinterpret_cast<modules::Skate*>(skate)->get_rider_by_number(static_cast<uint32_t>(player)))) {
            if (uint8_t* info =
                    net->find_player_slot_by_obj(static_cast<uint16_t>(*reinterpret_cast<uint32_t*>(skater + 0x14))))
                conn = *reinterpret_cast<net::Conn**>(info + 0x18);
        }
        uint8_t stolen[8] = {};
        count = g_trick_spot_hub->log_trick_objects(count, objects, stolen, player, score);
        if (count == 0)
            return;
        for (int victim = 0; victim < 8; ++victim) {
            if (!stolen[victim])
                continue;
            net::NetMessageInfo stolen_desc;
            stolen_desc.make_new();
            TagStolenMessage message = {};
            message.thief = player;
            message.victim = victim;
            message.game_id_ = net->get_game_id();
            uint8_t* thief = net->find_player_slot_by_obj(static_cast<uint16_t>(player));
            stolen_desc.content_ = &message;
            stolen_desc.byte_count_ = sizeof(message);
            stolen_desc.ident_ = kMsgTagStolen;
            server->put_on_outbox(as_player(thief)->get_link_handle(), &stolen_desc);
            message.thief = player;
            message.victim = victim;
            message.game_id_ = net->get_game_id();
            if (uint8_t* info = net->find_player_slot_by_obj(static_cast<uint16_t>(victim)))
                server->put_on_outbox(as_player(info)->get_link_handle(), &desc); // (not stolen_desc: as the original)
        }

        TrickObjectsMessage all = {};
        all.one = 1;
        all.player = player;
        all.score = score;
        all.count = count;
        all.game_id_ = net->get_game_id();
        std::memcpy(all.objects, objects, count * 4);
        desc.content_ = &all;
        desc.byte_count_ = static_cast<uint16_t>(count * 4 + offsetof(TrickObjectsMessage, objects));
        desc.ident_ = kMsgTrickObjects;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        for (void* info = net->begin_player_walk(&search, false); info; info = net->step_player_walk(&search, false))
            server->send_bulk_message(as_player(info)->get_link_handle(), desc.ident_, desc.byte_count_, desc.content_,
                                      "TrickObj Buffer", kGroupGame, true, false);

        ObserverTrickObjectsMessage observed = {};
        observed.count = count;
        observed.player = player;
        observed.game_id_ = net->get_game_id();
        std::memcpy(observed.objects, objects, count * 4);
        desc.content_ = &observed;
        desc.byte_count_ = static_cast<uint16_t>(count * 4 + offsetof(ObserverTrickObjectsMessage, objects));
        desc.ident_ = kMsgObserverTrickObjects;
        for (void* info = net->begin_player_walk(&search, true); info; info = net->step_player_walk(&search, true)) {
            if (as_player(info)->is_observer())
                server->send_bulk_message(as_player(info)->get_link_handle(), desc.ident_, desc.byte_count_,
                                          desc.content_, "TrickObj Buffer", kGroupGame, true, false);
        }
        reinterpret_cast<modules::Skate*>(skate)->broadcast_points(false);
        if (conn->is_host_local()) // (no null check: as the original)
            return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        auto* cluster = static_cast<TrickObjectCluster*>(g_trick_spot_hub->object_clusters_.fetch_entry(objects[i]));
        if (cluster)
            cluster->set_graffiti_color(player == -1 ? -1 : player + 1);
    }
}

// 0x00589670
void TrickSpotHub::give_free_clusters(int player)
{
    FreeClusterList list;
    list.count = 0;
    clusters_.run_hook(reinterpret_cast<lists::KeyMap::HookFn>(&list_free_cluster), &list);
    if (list.count)
        score_trick_objects(player, 1000, list.count, list.names, true);
}

// 0x005896D0
bool TrickSpotHub::add_cluster(uint32_t name)
{
    if (get_cluster(name))
        return false;
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(modules::g_skate_module) + 0x378) == kLoadCreatedPark) {
        park_editing::RailAuthoring::sole()->clear_cluster_colours(name);
    }
    void* memory = memory::alloc_zeroed(sizeof(TrickObjectCluster));
    clusters_.store_entry(name, memory ? static_cast<TrickObjectCluster*>(memory)->make_new(name) : nullptr);
    ++cluster_count_;
    object_clusters_.store_entry(name, get_cluster(name));
    return true;
}

// 0x00589760
bool TrickSpotHub::add_object(uint32_t object_name, uint32_t cluster_name)
{
    const bool added = get_cluster(cluster_name)->add_object(object_name);
    if (added && object_name != cluster_name)
        object_clusters_.store_entry(object_name, get_cluster(cluster_name));
    return added;
}

} // namespace objects

namespace objects {
namespace {

static VtableSlot g_trick_cluster_node_vtable_slots[] = {&objects::TrickClusterNode::teardown};
void** const kTrickClusterNodeVtable = reinterpret_cast<void**>(g_trick_cluster_node_vtable_slots);
static VtableSlot g_trick_object_node_vtable_slots[] = {&objects::TrickObjectNode::teardown};
void** const kTrickObjectNodeVtable = reinterpret_cast<void**>(g_trick_object_node_vtable_slots);
static VtableSlot g_trick_object_vtable_slots[] = {&objects::TrickSpot::teardown};
void** const kTrickObjectVtable = reinterpret_cast<void**>(g_trick_object_vtable_slots);
static VtableSlot g_trick_cluster_vtable_slots[] = {&objects::TrickObjectCluster::teardown};
void** const kTrickClusterVtable = reinterpret_cast<void**>(g_trick_cluster_vtable_slots);

} // namespace
} // namespace objects
