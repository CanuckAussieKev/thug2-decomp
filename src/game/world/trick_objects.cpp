// Pieces of the trick object code, 0x005880E0-0x0058818C and 0x00588200-0x005897A6 (the trick objects, the
// manager's lookups, graffiti state and graffiti log).
// See trick_objects.h.

#include "game/world/trick_objects.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/sector.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <gsl/pointers>
#include <algorithm>
#include <array>
#include <span>

#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "game/online/gamenet_manager.h"
#include "game/online/hub_roster.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "game/online/player_info.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_config.h"
#include "base/core/ints.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/checked_index.h"
#include "engine/lists/chain_start.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "game/online/net_game_state.h"
#include "base/core/checksums.h"
#include "wire/message_ids.h"

namespace objects {

namespace {
constexpr uint8_t kHalfAlpha = 0x80;
constexpr int kHeaderBytes = 8;
constexpr uint32_t kMaxClusterObjects = 32;
constexpr std::size_t kNameBytes = 32;
constexpr int kObjectScore = 1000;

std::unique_ptr<objects::TrickSpotHub>& stored_trick_spot_hub()
{
    static core::NoDestroy<std::unique_ptr<objects::TrickSpotHub>> trick_spot_hub;
    return trick_spot_hub.get();
}

} // namespace

objects::TrickSpotHub* trick_spot_hub() { return stored_trick_spot_hub().get(); }

namespace {
uint32_t& trick_object_manager_references()
{
    static uint32_t trick_object_manager_references{};
    return trick_object_manager_references;
}

constexpr uint32_t kNetGraffiti = 0x5E2EA50C;         // netgraffiti
constexpr uint32_t kGraffitiTeamScoring = 0xAB80CC2F; // graffiti_team_scoring (team_graffiti_mode)

// Which clusters of the current log changed hands (by their index in its list). THUG2's was a 128-byte global at
// 0x007CE508; enlarged as THUGPRO's DLL enlarged it (its NetLink::InitPatches, "Fix 256 graff tag crash": idea from
// Morten1337's thugpro_proxy, written here as our own buffer) - a list longer than the mask is cut to its size.
std::array<uint8_t, TrickSpotHub::kMaxLoggedClusters>& dirty_clusters()
{
    static std::array<uint8_t, TrickSpotHub::kMaxLoggedClusters> dirty_clusters{};
    return dirty_clusters;
}

// The game mode (0x007CE470) and the network manager's players (0x007CCE14), walked with a lists::Hunt<PlayerRecord>
// (vtable 0x0064C1A8).
void* get_game_mode() { return static_cast<void*>(goals::game_mode()); }
using PlayerSearch = online::PlayerSearch;
using online::PlayerRecord;
PlayerRecord* start_roster_sweep(PlayerSearch* search)
{
    return online::online_hub()->roster().begin_player_walk(search, false);
}
PlayerRecord* step_roster_sweep(PlayerSearch* search)
{
    return online::online_hub()->roster().step_player_walk(search, false);
}
PlayerRecord* find_player_slot_by_obj(int id)
{
    return online::online_hub()->roster().find_player_slot_by_obj(static_cast<uint16_t>(id));
}
int get_team(PlayerRecord* player) { return player->team(); }
objects::Rider* skater_of(PlayerRecord* player) { return player->rider(); }
int current_object_id(objects::Entity* skater) { return static_cast<int>(skater->ident()); }

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
    uint32_t& count = trick_object_manager_references();
    if (count > 1) {
        --count;
        return;
    }
    memory::destroy_then_clear(stored_trick_spot_hub());
    count = 0;
}

// 0x00588120
ClusterCount* count_object_in_cluster(const TrickObjectCluster* cluster, ClusterCount* counter)
{
    if (cluster->owned() && counter->cluster == ints::as_unsigned(cluster->owner())) {
        counter->count += 1;
    }
    return counter;
}

// 0x00588150
void TrickSpotHub::change_args(script::Struct* params)
{
    args_->get_clear();
    if (params) {
        args_->merge_structure(params);
    }
}

// 0x00588170
HitClusterLog::HitClusterLog() = default;

// 0x00588180
bool HitClusterLog::get_clear()
{
    num_ = 0;
    return true;
}

// 0x005886C0
uint32_t TrickSpotHub::log_trick_objects(std::span<uint32_t> objects, std::span<uint8_t> stolen, int player, uint32_t score)
{
    auto count = static_cast<uint32_t>(objects.size());
    debug::printf("RequestLogTrick called\n");
    count = std::min(count, kMaxLoggedClusters); // (ours: the original's mask overran here)
    if (count != 0U) {
        std::memset(dirty_clusters().data(), 0, count);
    }
    for (uint32_t i = 0; i < count; ++i) {
        auto* cluster = static_cast<TrickObjectCluster*>(object_clusters_.fetch_entry(core::at(objects, i)));
        if (!cluster) {
            continue;
        }
        goals::get_objective_board()->kill_cluster(cluster->label(), static_cast<int>(score));
        core::at(cluster->scores(), player) = std::max(core::at(cluster->scores(), player), score);
        uint32_t total = score;
        if (team_scoring()) {
            if (PlayerRecord* me = find_player_slot_by_obj(player)) {
                // The team's score: each of its players' best on this cluster.
                const int team = get_team(me);
                total = 0;
                PlayerSearch search;
                for (PlayerRecord* other = start_roster_sweep(&search); other; other = step_roster_sweep(&search)) {
                    if (skater_of(other) && get_team(other) == team) {
                        total += core::at(cluster->scores(), current_object_id(skater_of(other)));
                    }
                }
            }
        }
        if (total <= cluster->record_points()) {
            continue;
        }
        bool changed = cluster->owner() != player;
        if (changed && team_scoring()) {
            // Taken from a team mate: only if the player beat one of the team's own best scores on it.
            PlayerRecord* holder = find_player_slot_by_obj(static_cast<uint16_t>(cluster->owner()));
            const int team = get_team(find_player_slot_by_obj(player));
            if (get_team(holder) == team) {
                changed = false;
                PlayerSearch search;
                for (PlayerRecord* other = start_roster_sweep(&search); other; other = step_roster_sweep(&search)) {
                    if (skater_of(other) && get_team(other) == team &&
                        score > core::at(cluster->scores(), current_object_id(skater_of(other)))) {
                        changed = true;
                        break;
                    }
                }
            }
        }
        if (!cluster->owned() || changed) {
            core::at(dirty_clusters(), i) = 1;
        }
        if (cluster->owned() && changed) {
            core::at(stolen, cluster->owner()) = 1;
        }
        if (changed) {
            cluster->set_owner(player);
        }
        cluster->set_record_points(total);
        cluster->set_owned(true);
    }
    // The list compacted to the objects whose cluster changed hands.
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (core::at(dirty_clusters(), i) != 0U) {
            core::at(objects, kept++) = core::at(objects, i);
        }
    }
    return kept;
}

// --- the trick objects, the clusters' lists, the manager's lookups and graffiti state ---------------------------

namespace {

constexpr uint32_t kLoadCreatedPark = 0xB664035D; // load_sk5ed_gameplay (the skate module's level, +0x378)

// The level sector named `name` in any scene (0x0048C510) and its colour: 0x0049E670 cleared, 0x0049E980 read
// (r, g, b, a; zero without a mesh), the mesh's (+0x14) vtable +0x18 set.
render::WorldCell* find_sector(uint32_t name) { return render::engine::look_up_sector(name); }
void clear_sector_color(render::WorldCell* sector) { sector->wipe_tint(); }
uint32_t* get_sector_color(render::WorldCell* sector, uint32_t* out)
{
    return sector->fetch_tint(out);
}

// Graffiti colour `index`: graffitiColors[index]'s r, g, b.
script::Array* graffiti_color(int index)
{
    script::Array* colors = script::fetch_list("graffitiColors");
    return colors->array_at(static_cast<uint32_t>(index));
}

} // namespace

// 0x00588200
bool TrickSpot::set_graffiti_color(int index) const
{
    const render::WorldCell* sector = find_sector(label_);
    script::Array* color = graffiti_color(index);
    std::array<uint8_t, 4> rgba{};
    core::at(rgba, 0) = static_cast<uint8_t>(color->fetch_part(0));
    core::at(rgba, 1) = static_cast<uint8_t>(color->fetch_part(1));
    core::at(rgba, 2) = static_cast<uint8_t>(color->fetch_part(2));
    core::at(rgba, 3) = kHalfAlpha;
    if (auto* geom = static_cast<render::Geom*>(sector->geom_handle())) {
        uint32_t packed = 0;
        std::memcpy(&packed, rgba.data(), 4);
        geom->change_tint(packed);
    }
    return true;
}

// 0x00588270
bool TrickSpot::clear_graffiti_color(int index) const
{
    render::WorldCell* sector = find_sector(label_);
    script::Array* color = graffiti_color(index);
    auto red = static_cast<uint8_t>(color->fetch_part(0));
    auto green = static_cast<uint8_t>(color->fetch_part(1));
    auto blue = static_cast<uint8_t>(color->fetch_part(2));
    uint32_t packed = 0;
    get_sector_color(sector, &packed);
    std::array<uint8_t, 4> current{};
    std::memcpy(current.data(), &packed, 4);
    if (core::at(current, 0) == red && core::at(current, 1) == green && core::at(current, 2) == blue) {
        clear_sector_color(sector);
    }
    return true;
}

// 0x00588630
bool TrickObjectCluster::add_object(uint32_t name)
{
    auto* object = static_cast<TrickSpot*>(memory::alloc_zeroed(sizeof(TrickSpot)));
    if (!object) {
        return false;
    }
    object->list_link().make_node(object);
    object->set_label(name);
    clear_sector_color(find_sector(name));
    if (!game_config::from_cd() && (script::fetch_int_value("show_all_trick_objects") != 0)) {
        static_cast<void>(object->set_graffiti_color(2));
    }
    // Last in the list (before the head).
    object->list_link().set_next(&entities_);
    object->list_link().set_prev(entities_.prev());
    entities_.prev()->set_next(&object->list_link());
    entities_.set_prev(&object->list_link());
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
    ClusterCount counter = {.count = 0, .cluster = static_cast<uint32_t>(player)};
    clusters_.run_hook<&count_object_in_cluster>(&counter);
    return counter.count;
}

// 0x00588430
void find_cluster_index(TrickObjectCluster* cluster, ClusterIndexSearch* search)
{
    if (search->found) {
        return;
    }
    if (search->name == cluster->label()) {
        search->found = true;
    } else {
        ++search->index;
    }
}

// 0x00588450
void find_cluster_at_index(TrickObjectCluster* cluster, ClusterAtIndex* search)
{
    if (search->found) {
        return;
    }
    if (search->countdown == 0) {
        search->found = true;
        search->name = cluster->label();
    } else {
        --search->countdown;
    }
}

// 0x00588480
uint32_t TrickSpotHub::get_cluster_name(uint8_t index)
{
    ClusterAtIndex search = {.name = 0, .countdown = index, .found = false};
    clusters_.run_hook<&find_cluster_at_index>(&search);
    return search.name;
}

// 0x005884B0
void write_held_cluster(TrickObjectCluster* cluster, GraffitiStateWriter* writer)
{
    if (!cluster->owned() || cluster->owner() != writer->player) {
        return;
    }
    ClusterIndexSearch search = {.name = cluster->label(), .index = 0, .found = false};
    writer->manager->clusters().run_hook<&find_cluster_index>(&search);
    core::at(writer->buffer, writer->count + kHeaderBytes) = search.index; // (a byte: past 255 clusters it wraps, as THUG2's and THUGPRO's)
    ++writer->count;
    ++core::at(writer->buffer, writer->player);
}

// 0x00588520
int TrickSpotHub::write_graffiti_state(std::span<uint8_t> buffer)
{
    GraffitiStateWriter writer = {.player = 0, .count = 0, .buffer = buffer, .manager = this};
    for (int player = 0; player < online::kPlayerSlotCount; ++player) {
        core::at(buffer, player) = 0;
        writer.player = player;
        clusters_.run_hook<&write_held_cluster>(&writer);
        debug::printf("Building graffiti state message %d\n", core::at(buffer, player));
    }
    return writer.count + kHeaderBytes;
}

// 0x005889F0
bool TrickSpotHub::remove_all_clusters()
{
    clusters_.run_hook(
        [](void* cluster, void*) {
            if (cluster) {
                static_cast<TrickObjectCluster*>(cluster)->teardown(1);
            }
        },
        nullptr);
    clusters_.empty_table();
    cluster_count_ = 0;
    object_clusters_.empty_table();
    return true;
}

// 0x00588A20
bool HitClusterLog::append_one(uint32_t object_name)
{
    const uint32_t listed = ints::as_unsigned(num_) > kCapacity ? kCapacity : static_cast<uint32_t>(num_);
    const TrickObjectCluster* cluster = stored_trick_spot_hub()->get_object_cluster(object_name);
    if (!cluster || (cluster->label() == 0U)) {
        return false;
    }
    for (uint32_t i = 0; i < listed; ++i) {
        if (core::at(items_, i) == cluster->label()) {
            return false;
        }
    }
    goals::get_objective_board()->trick_object_hit(cluster->label());
    core::at(items_, num_ & (kCapacity - 1)) = cluster->label();
    ++num_;
    return false;
}

// 0x00588AA0
uint32_t HitClusterLog::copy_cluster_names(std::span<uint32_t> out, uint32_t max_bytes)
{
    const uint32_t listed = ints::as_unsigned(num_) > kCapacity ? kCapacity : static_cast<uint32_t>(num_);
    const TrickSpotHub* manager = stored_trick_spot_hub().get();
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < listed; ++i) {
        auto* cluster = static_cast<TrickObjectCluster*>(manager->object_clusters().fetch_entry(core::at(items_, i)));
        if (cluster && (cluster->label() != 0U)) {
            core::at(out, bytes / 4) = core::at(items_, i);
            bytes += 4;
        }
        if (bytes >= max_bytes) {
            break;
        }
    }
    return bytes;
}

// 0x00588BB0
void TrickSpot::destroy_body() { list_link_.uncouple(); }

// 0x00588B40
TrickSpot* TrickSpot::teardown(unsigned int flags)
{
    destroy_body();
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x00588BD0
TrickObjectCluster::TrickObjectCluster(uint32_t name) : label_(name)
{
    list_link_.make_node(this);
    entities_.make_head();
    for (uint32_t& score : scores_) {
        score = 0;
    }
}

namespace {

// The objects in a cluster's list, and the one at `index` (null past the end). (The original reads each node's data
// before checking it for null.)
uint32_t count_objects(const memory::ListNode& head)
{
    uint32_t count = 0;
    for (const memory::ListNode* node = head.next(); node->data() != memory::ListNode::head_marker() && node;
         node = node->next()) {
        ++count;
    }
    return count;
}
memory::ListNode* object_at(const memory::ListNode& head, uint32_t index)
{
    for (memory::ListNode* node = head.next(); node->data() != memory::ListNode::head_marker() && node; node = node->next()) {
        if (index-- == 0) {
            return node;
        }
    }
    return nullptr;
}

} // namespace

// 0x00588C30
TrickObjectCluster::~TrickObjectCluster()
{
    for (uint32_t count = count_objects(entities_); count != 0; --count) {
        if (auto* object = static_cast<TrickSpot*>(object_at(entities_, 0)->data())) {
            object->teardown(1);
        }
    }
    lists::unlink_all(entities_);
    entities_.uncouple();
    list_link_.uncouple();
}

// 0x00588D20
bool TrickObjectCluster::set_graffiti_color(int index)
{
    const uint32_t count = count_objects(entities_);
    for (uint32_t i = 0; i < count; ++i) {
        static_cast<void>(static_cast<TrickSpot*>(object_at(entities_, i)->data())->set_graffiti_color(index));
    }
    if (modules::Skate::sole()->level() == kLoadCreatedPark) {
        park_editing::RailAuthoring::sole()->set_cluster_colour(checksums::Name{label_}, index);
    }
    return true;
}

// 0x00588DC0
TrickSpotHub::TrickSpotHub() : clusters_(online::kPlayerSlotCount), object_clusters_(online::kPlayerSlotCount)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->script_heap());
    gsl::owner<script::Struct*> params = script::spawn_param_bag();
    args_ = params ? memory::construct_in<script::Struct>(params) : nullptr;
    memory->leave_heap_scope();
}

// 0x00588E90 (the tables' destructors run after it: the objects' table, then the clusters')
TrickSpotHub::~TrickSpotHub() { remove_all_clusters(); }

// 0x00588F40
bool TrickObjectCluster::clear_graffiti_color(int index)
{
    const uint32_t count = count_objects(entities_);
    for (uint32_t i = 0; i < count; ++i) {
        if (!static_cast<TrickSpot*>(object_at(entities_, i)->data())->clear_graffiti_color(index)) {
            return false;
        }
    }
    if (modules::Skate::sole()->level() == kLoadCreatedPark) {
        park_editing::RailAuthoring::sole()->clear_cluster_colours(label_);
    }
    return true;
}

// 0x00588FF0
bool TrickSpotHub::set_object_graffiti_color(checksums::Name object, int player)
{
    const uint32_t object_name = object.value();
    TrickObjectCluster* cluster = get_object_cluster(object_name);
    if (!cluster) {
        return false;
    }
    cluster->set_graffiti_color(player == -1 ? -1 : player + 1);
    return true;
}

// 0x00589030
bool TrickSpotHub::clear_graffiti_color(int index)
{
    clusters_.set_iter_bucket(-1);
    clusters_.set_iter_item(nullptr);
    while (clusters_.step_iterator() && clusters_.iter_item() && clusters_.iter_item()->value) {
        if (!static_cast<TrickObjectCluster*>(clusters_.iter_item()->value)->clear_graffiti_color(index)) {
            return false;
        }
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
void release_held_cluster(TrickObjectCluster* cluster, HeldClusterRelease* release)
{
    if (!cluster->owned() || cluster->owner() != release->player) {
        return;
    }
    cluster->set_owned(false);
    cluster->set_record_points(0);
    for (uint32_t& score : cluster->scores()) {
        score = 0;
    }
    core::at(release->names, release->count++) = cluster->label();
}

// 0x00588350
void list_free_cluster(TrickObjectCluster* cluster, FreeClusterList* list)
{
    if (list->count < kMaxClusterObjects && !cluster->owned()) {
        core::at(list->names, list->count++) = cluster->label();
    }
}

// 0x005890B0
void TrickSpotHub::apply_player_graffiti()
{
    for (int player = 0; player < online::kPlayerSlotCount; ++player) {
        std::array<char, kNameBytes> name{};
        text::format_to(name, "Skater%d", player);
        script::Array* objects = nullptr;
        if (!args_->fetch_list(name.data(), &objects, 0)) {
            continue;
        }
        for (int i = 0; i < ints::as_signed(objects->extent()); ++i) {
            auto* cluster = static_cast<TrickObjectCluster*>(object_clusters_.fetch_entry(objects->fetch_part(i)));
            if (cluster) {
                cluster->set_graffiti_color(player == -1 ? -1 : player + 1);
            }
        }
    }
    args_->get_clear();
}

// 0x00589180
TrickSpotHub* acquire_trick_object_manager(bool create)
{
    std::unique_ptr<TrickSpotHub>& instance = stored_trick_spot_hub();
    if (!instance && create) {
        instance = std::make_unique<TrickSpotHub>();
    }
    ++trick_object_manager_references();
    return instance.get();
}

// 0x005891D0
TrickObjectCluster* TrickObjectCluster::teardown(unsigned int flags)
{
    this->~TrickObjectCluster();
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x00589230
bool TrickSpotHub::release_player_clusters(int player)
{
    debug::printf("Clearing observer state\n");
    args_->get_clear();
    if (static_cast<online::Hub*>(online::online_hub())->get_on_server()) {
        // (THUG2 lists the released clusters' names in a 128-entry buffer on the stack - a player holding more
        // overran it; the list itself isn't used, only its count.)
        static HeldClusterRelease release;
        release.player = player;
        release.count = 0;
        clusters_.run_hook<&release_held_cluster>(&release);
        if (release.count == 0) {
            return false;
        }
        modules::Skate::sole()->broadcast_points(false);
    }
    clear_graffiti_color(player + 1);
    return true;
}

// 0x005892C0
void score_trick_objects(int player, uint32_t score, std::span<uint32_t> objects, bool send)
{
    auto count = static_cast<uint32_t>(objects.size());
    online::Hub* net = online::online_hub();
    void* skate = static_cast<void*>(modules::Skate::sole());
    net::NetMessageInfo desc;
    PlayerSearch search;
    if (send) {
        net::AppHost* server = net->host();
        const net::Conn* conn = nullptr;
        if (const objects::Entity* skater =
                static_cast<modules::Skate*>(skate)->get_rider_by_number(static_cast<uint32_t>(player))) {
            if (const online::PlayerRecord* info =
                    net->roster().find_player_slot_by_obj(static_cast<uint16_t>(skater->ident()))) {
                conn = info->conn();
            }
        }
        std::array<uint8_t, online::kPlayerSlotCount> stolen = {};
        count = stored_trick_spot_hub()->log_trick_objects(objects, stolen, player, score);
        objects = objects.first(count);
        if (count == 0) {
            return;
        }
        for (int victim = 0; victim < online::kPlayerSlotCount; ++victim) {
            if (core::at(stolen, victim) == 0U) {
                continue;
            }
            net::NetMessageInfo stolen_desc;
            online::TagStolenMessage message;
            message.thief_id = static_cast<uint32_t>(player);
            message.victim_id = static_cast<uint32_t>(victim);
            message.game_id = net->get_game_id();
            std::array<uint8_t, online::kTagStolenSize> stolen_data{};
            util::ByteWriter stolen_writer(stolen_data.data(), sizeof(stolen_data));
            online::write_tag_stolen(stolen_writer, message);
            online::PlayerRecord* thief = net->roster().find_player_slot_by_obj(static_cast<uint16_t>(player));
            stolen_desc.set_content(stolen_data.data());
            stolen_desc.set_byte_count(sizeof(stolen_data));
            stolen_desc.set_ident(online::MessageId::kTagStolen);
            server->put_on_outbox(thief->get_link_handle(), &stolen_desc);
            if (online::PlayerRecord* info = net->roster().find_player_slot_by_obj(static_cast<uint16_t>(victim))) {
                server->put_on_outbox(info->get_link_handle(), &desc); // (not stolen_desc: as the original)
            }
        }

        // (NOT ORIGINAL: no more names than the message holds - the original copied a remote count on past its
        // buffer)
        count = std::min(count, online::kMaxTrickObjects);
        auto all = std::make_unique<online::TrickObjectsMessage>();
        all->player = player;
        all->score = score;
        all->count = count;
        all->game_id = net->get_game_id();
        std::ranges::copy(objects.first(count), all->objects.begin());
        std::array<uint8_t, online::kTrickObjectsHeaderSize + sizeof(all->objects)> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_trick_objects(writer, *all);
        desc.set_content(data.data());
        desc.set_byte_count(static_cast<uint16_t>(writer.position()));
        desc.set_ident(online::MessageId::kTrickObjects);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        for (online::PlayerRecord* info = net->roster().begin_player_walk(&search, false); info;
             info = net->roster().step_player_walk(&search, false)) {
            server->send_bulk_message(info->get_link_handle(), {.msg_id_ = desc.ident(), .size = desc.byte_count()}, data.data(),
                                      "TrickObj Buffer", online::MessageGroup::kGame, true, false);
        }

        util::ByteWriter observed_writer(data.data(), sizeof(data));
        online::write_observed_trick_objects(observed_writer, *all);
        desc.set_content(data.data());
        desc.set_byte_count(static_cast<uint16_t>(observed_writer.position()));
        desc.set_ident(online::MessageId::kObservedTrickObjects);
        for (online::PlayerRecord* info = net->roster().begin_player_walk(&search, true); info;
             info = net->roster().step_player_walk(&search, true)) {
            if (info->is_observer() != 0) {
                server->send_bulk_message(info->get_link_handle(), {.msg_id_ = desc.ident(), .size = desc.byte_count()}, data.data(),
                                          "TrickObj Buffer", online::MessageGroup::kGame, true, false);
            }
        }
        static_cast<modules::Skate*>(skate)->broadcast_points(false);
        if (conn->is_host_local()) { // (no null check: as the original)
            return;
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        auto* cluster = static_cast<TrickObjectCluster*>(stored_trick_spot_hub()->object_clusters().fetch_entry(core::at(objects, i)));
        if (cluster) {
            cluster->set_graffiti_color(player == -1 ? -1 : player + 1);
        }
    }
}

// 0x00589670
void TrickSpotHub::give_free_clusters(int player)
{
    FreeClusterList list{};
    list.count = 0;
    clusters_.run_hook<&list_free_cluster>(&list);
    if (list.count != 0U) {
        score_trick_objects(player, kObjectScore, std::span<uint32_t>(list.names).first(list.count), true);
    }
}

// 0x005896D0
bool TrickSpotHub::add_cluster(uint32_t name)
{
    if (get_cluster(name)) {
        return false;
    }
    if (modules::Skate::sole()->level() == kLoadCreatedPark) {
        park_editing::RailAuthoring::sole()->clear_cluster_colours(name);
    }
    void* memory = memory::alloc_zeroed(sizeof(TrickObjectCluster));
    clusters_.store_entry(name, memory ? memory::construct_in<TrickObjectCluster>(memory, name) : nullptr);
    ++cluster_count_;
    object_clusters_.store_entry(name, get_cluster(name));
    return true;
}

// 0x00589760
bool TrickSpotHub::add_object(uint32_t object_name, uint32_t cluster_name)
{
    const bool added = get_cluster(cluster_name)->add_object(object_name);
    if (added && object_name != cluster_name) {
        object_clusters_.store_entry(object_name, get_cluster(cluster_name));
    }
    return added;
}

} // namespace objects
