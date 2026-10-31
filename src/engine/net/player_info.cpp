// The network game's players: online::PlayerRecord and JoinerRecord (see player_info.h).

#include "engine/net/player_info.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_conn.h"
#include "engine/objects/entity.h"
#include "game/skate/profile_manager.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/net/gamenet_lists.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kTagging = 0x5B512EDC;      // Tagging (the component)
constexpr uint32_t kSkaterScore = 0xD262802F;  // SkaterScore (the component)
constexpr uint32_t kNetFirefight = 0xBFF33600; // netfirefight

constexpr uint32_t kHitWindow = 5000; // ms

static VtableSlot g_player_vtable_slots[] = {&PlayerRecord::teardown};
void** const kPlayerVtable = reinterpret_cast<void**>(g_player_vtable_slots); // PlayerRecord
static VtableSlot g_score_node_vtable_slots[] = {&online::ScoreNode::teardown};
void** const kScoreNodeVtable = reinterpret_cast<void**>(g_score_node_vtable_slots);

Hub* the_manager() { return g_online_hub; }

objects::ObjectModule* skater_component(objects::Rider* skater, uint32_t type)
{
    return reinterpret_cast<objects::GameEntity*>(skater)->fetch_module(type);
}

// A new skater profile on the skater info heap (null if out of memory).
objects::RiderIdentity* new_profile()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    void* block = memory::alloc_zeroed(sizeof(objects::RiderIdentity));
    return block ? static_cast<objects::RiderIdentity*>(block)->make_new() : nullptr;
}

// A node's unlinking, as lists::Elem's destructor does it (the node linked to itself after).
void unlink_node(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// --- JoinerRecord ---

// 0x005434D0
JoinerRecord* JoinerRecord::make_new()
{
    account_ = new_profile();
    unknown24_ = 0;
    profile_id_ = 0;
    skill_points_ = 0;
    unknown38_ = 0;
    drive_control_mode_ = 0;
    memory::Hub::get_pull()->leave_heap_scope();
    return this;
}

// 0x00543560
void JoinerRecord::destroy_body()
{
    if (objects::RiderIdentity* profile = account_) {
        profile->destroy_body();
        memory::Hub::get_pull()->dealloc(profile);
    }
}

// --- PlayerRecord ---

// 0x00543180
void* clear_update_records(void* records)
{
    std::memset(static_cast<uint8_t*>(records) + 0xB8, 0, 16 * sizeof(uint32_t));
    return records;
}

// 0x00543740
PlayerRecord* PlayerRecord::make_new(uint32_t flags)
{
    data = this;
    priority = 0;
    next = this;
    prev = this;
    vtable = kPlayerVtable;
    clear_update_records(unknown50_);
    flag_bits_ = 0;
    std::sprintf(label_, "<No Name>"); // (0x0064CCE8)
    for (int i = 0; i < kMaxPlayers; ++i) {
        sent_updates_[i] = -1;
        update_bytes_a_[i] = 0xFF;
        update_bytes_b_[i] = 0xFF;
        for (int j = 0; j < 3; ++j) {
            update_records_a_[i][j] = 0xFFFF;
            update_records_b_[i][j] = 0xFFFF;
        }
    }
    flag_bits_ = flags;
    object_1e_c_ = nullptr;
    unknown1d8_ = 0;
    face_texture_data_ = nullptr;
    skater_ = nullptr;
    account_ = nullptr;
    join_time_ = 0;
    last_impact_at_ = 0;
    team_ = 0;
    unknown28_ = 0;
    skill_points_ = 0;
    unknown48_ = 0;
    eliminated_time_ = 0;
    unknown34_ = 0;
    last_sent_skater_ = 7;
    return this;
}

// 0x00543810
void PlayerRecord::destroy_body()
{
    vtable = kPlayerVtable;
    if (objects::RiderIdentity* profile = account_) {
        profile->destroy_body();
        memory::Hub::get_pull()->dealloc(profile);
    }
    if (object_1e_c_) {
        void** object_vtable = *static_cast<void***>(object_1e_c_);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(object_vtable[2])(object_1e_c_, 1);
    }
    if (face_texture_data_) {
        memory::Hub::get_pull()->dealloc(face_texture_data_);
        face_texture_data_ = nullptr;
    }
    vtable = kPlayerNodeVtable;
    unlink_node(this);
}

// 0x00544750
PlayerRecord* PlayerRecord::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x005438D0
void PlayerRecord::bind_rider(objects::Rider* skater)
{
    Hub* manager = the_manager();
    skater_ = skater;
    if (!(flag_bits_ & kPlayerLocal) && manager->fetch_viewed_player() == this)
        manager->spectate_player(reinterpret_cast<uint8_t*>(this)); // (the camera follows it again)
}

// 0x00543270
void PlayerRecord::set_profile(objects::RiderIdentity* profile)
{
    objects::RiderIdentity* copy = new_profile();
    account_ = copy;
    copy->copy_from(profile);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x005431F0
void PlayerRecord::set_face_texture_data(const uint8_t* bytes, uint32_t size)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->net_misc_heap_);
    uint8_t id = prop<uint8_t>(skater_, 0x14); // (the skater's object id)
    face_texture_data_ = static_cast<uint8_t*>(memory::alloc(size + 1));
    face_texture_data_[0] = id;
    std::memcpy(face_texture_data_ + 1, bytes, size);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00543060
uint8_t* PlayerRecord::get_face_texture_data() { return face_texture_data_; }

// 0x00543070
void PlayerRecord::set_custom_tag(bool custom)
{
    if (!skater_)
        return;
    if (objects::ObjectModule* tagging = skater_component(skater_, kTagging))
        prop<bool>(tagging, 0x18) = custom; // (0x004E4000, a setter folded there)
}

// 0x00543090
bool PlayerRecord::has_custom_tag()
{
    if (!skater_)
        return false;
    objects::ObjectModule* tagging = skater_component(skater_, kTagging);
    return tagging ? prop<bool>(tagging, 0x18) : false; // (0x0051D850)
}

// 0x005430B0
void* PlayerRecord::get_graffiti_texture()
{
    if (!skater_)
        return nullptr;
    objects::ObjectModule* tagging = skater_component(skater_, kTagging);
    return tagging ? prop<void*>(tagging, 0x1C) : nullptr; // (0x00553E90, a getter folded there)
}

// 0x005430D0
void PlayerRecord::set_ready_query_owed(uint32_t id) { ready_query_owed_ = id; }

// 0x00544670
void PlayerRecord::await_ready_response(uint32_t id)
{
    if (!conn_ || !conn_->is_off_machine())
        return;
    if (id)
        ready_query_owed_ = id;
    conn_->cur_standing_ &= ~net::Conn::kConnReady;
    conn_->cur_standing_ |= net::Conn::kStatus2;
}

// 0x005446C0
void PlayerRecord::receive_ready_response(uint32_t id)
{
    if (ready_query_owed_ != id)
        return;
    if (conn_) {
        conn_->cur_standing_ &= ~net::Conn::kStatus2;
        conn_->cur_standing_ |= net::Conn::kConnReady;
        conn_->clear_resend_counts();
    }
    ready_query_owed_ = 0;
}

// 0x00543110 / 0x00543100 / 0x005430E0
void PlayerRecord::change_recent_hit_stamp(uint32_t time) { last_impact_at_ = time; }
void PlayerRecord::clear_last_hit_time() { last_impact_at_ = 0; }
int PlayerRecord::can_be_hit_again() { return frame_clock::get_time() - last_impact_at_ > kHitWindow ? 1 : 0; }

// 0x00543120 / 0x00543130 / 0x00543140
int PlayerRecord::get_last_sent_skater() { return last_sent_skater_; }
void PlayerRecord::set_last_sent_skater(int index) { last_sent_skater_ = index; }
int PlayerRecord::get_skaters_per_update() { return conn_->get_link_speed_class() != 0 ? 1 : 2; }

// 0x00543150 / 0x00543160
int PlayerRecord::get_accumulated_score() { return accumulated_score_; }
void PlayerRecord::add_to_accumulated_score(int score) { accumulated_score_ += score; }

// 0x00543300 - 0x005434C0: the flags.
int PlayerRecord::is_own_player() { return flag_bits_ & kPlayerLocal; }
int PlayerRecord::is_host_player() { return (flag_bits_ >> 9) & 1; }
int PlayerRecord::is_observer() { return (flag_bits_ >> 2) & 1; }
int PlayerRecord::is_scouting_level() { return (flag_bits_ >> 16) & 1; }
void PlayerRecord::mark_face_texture_sent() { flag_bits_ |= kPlayerFaceTextureSent; }
void PlayerRecord::mark_just_joined()
{
    join_time_ = frame_clock::get_time();
    flag_bits_ |= kPlayerJustJoined;
}
void PlayerRecord::clear_just_joined() { flag_bits_ &= ~kPlayerJustJoined; }
int PlayerRecord::has_finished_joining() { return (flag_bits_ & (kPlayerFullyIn | kPlayerLocal)) != 0 ? 1 : 0; }
void PlayerRecord::set_host_player() { flag_bits_ |= kPlayerServer; }
void PlayerRecord::clear_server_player() { flag_bits_ &= ~kPlayerServer; }
int PlayerRecord::is_carrying_flag() { return (flag_bits_ & kPlayerCarryingFlags) != 0 ? 1 : 0; }
int PlayerRecord::get_flag_team()
{
    for (int team = 0; team < 4; ++team) {
        if (flag_bits_ & (kPlayerCarryingFlag0 << team))
            return team;
    }
    return -1;
}
void PlayerRecord::clear_flags() { flag_bits_ &= ~kPlayerCarryingFlags; }
void PlayerRecord::mark_eliminated()
{
    eliminated_time_ = get_call<uint32_t>(code_address(&goals::MatchRules::get_round), get_game_mode());
    flag_bits_ |= kPlayerEliminated;
}
bool PlayerRecord::is_eliminated()
{
    if (flag_bits_ & kPlayerEliminated)
        return true;
    if (get_game_type() != kNetFirefight || !skater_)
        return false;
    objects::ObjectModule* score = skater_component(skater_, kSkaterScore);
    return score && get_call<int>(code_address(&modules::PointTally::get_total_score), prop<void*>(score, 0x18)) ==
                        0; // (its score's lives)
}
int PlayerRecord::holds_crown() { return (flag_bits_ >> 7) & 1; }
int PlayerRecord::get_link_handle() { return conn_ ? prop<int>(conn_, 0x3C) : 0; }
uint32_t PlayerRecord::get_skater_value_2b_c() { return prop<uint32_t>(skater_, 0x2BC); }
int PlayerRecord::is_pending_observer() { return (flag_bits_ >> 3) & 1; }

// --- The score lines' list types ---

// 0x00544700 / 0x005435B0 / 0x00544720
void ScoreNode::destroy_body()
{
    vtable = kScoreNodeVtable;
    unlink_node(this);
}
ScoreNode* ScoreNode::teardown(unsigned int flags)
{
    vtable = kScoreNodeVtable;
    unlink_node(this);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}
ScoreNode* ScoreNode::destroy_calling(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

} // namespace online

using online::JoinerRecord;
using online::PlayerRecord;
