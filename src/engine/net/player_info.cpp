// The network game's players: online::PlayerRecord and JoinerRecord (see player_info.h).

#include "engine/net/player_info.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_conn.h"
#include "engine/objects/entity.h"
#include "game/skate/profile_manager.h"
#include "platform/stopwatch.h"
#include "platform/code_address.h"
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
#include "game/rider/skater_score.h"
#include "game/rider/tagging.h"
#include "platform/compiler.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's sprintf, kept)

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kTagging = 0x5B512EDC;      // Tagging (the component)
constexpr uint32_t kSkaterScore = 0xD262802F;  // SkaterScore (the component)
constexpr uint32_t kNetFirefight = 0xBFF33600; // netfirefight

constexpr uint32_t kHitWindow = 5000; // ms

Hub* the_manager() { return g_online_hub; }

objects::ObjectModule* skater_component(objects::Rider* skater, uint32_t type)
{
    return raw::as<objects::GameEntity>(skater)->fetch_module(type);
}

// A new skater profile on the skater info heap (null if out of memory).
objects::RiderIdentity* new_profile()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    void* block = memory::alloc_zeroed(sizeof(objects::RiderIdentity));
    return block ? memory::construct_in<objects::RiderIdentity>(block) : nullptr;
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
JoinerRecord::JoinerRecord() : account_(new_profile()) { memory::Hub::get_pull()->leave_heap_scope(); }

// 0x00543560
JoinerRecord::~JoinerRecord()
{
    if (objects::RiderIdentity* profile = account_) {
        profile->~RiderIdentity();
        memory::Hub::get_pull()->dealloc(profile);
    }
}

// --- PlayerRecord ---

// 0x00543180
void clear_update_records(PlayerRecord* player)
{
    std::ranges::fill(player->sent_.value68_, 0u);
    std::ranges::fill(player->sent_.value_6c_, 0u);
}

// 0x00543740
PlayerRecord::PlayerRecord(uint32_t flags) : ListNode()
{
    make_node(this);
    clear_update_records(this);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    flag_bits_ = 0;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    std::sprintf(label_, "<No Name>"); // (0x0064CCE8)
    for (int i = 0; i < kMaxPlayers; ++i) {
        sent_.value68_[i] = 0xFFFFFFFF;
        sent_.state_[i] = 0xFF;
        sent_.flag74_[i] = 0xFF;
        for (int j = 0; j < 3; ++j) {
            sent_.pos_[i][j] = -1;
            sent_.angles_[i][j] = -1;
        }
    }
    flag_bits_ = flags;
    observer_task_ = nullptr;
    rejoin_group_ = 0;
    face_texture_data_ = nullptr;
    skater_ = nullptr;
    account_ = nullptr;
    join_time_ = 0;
    last_impact_at_ = 0;
    team_ = 0;
    profile_id_ = 0;
    skill_points_ = 0;
    drive_control_mode_ = 0;
    eliminated_time_ = 0;
    num_placed_ = 0;
    last_sent_skater_ = 7;
}

// 0x00543810
PlayerRecord::~PlayerRecord()
{
    if (objects::RiderIdentity* profile = account_) {
        profile->~RiderIdentity();
        memory::Hub::get_pull()->dealloc(profile);
    }
    delete observer_task_;
    if (face_texture_data_) {
        memory::Hub::get_pull()->dealloc(face_texture_data_);
        face_texture_data_ = nullptr;
    }
    unlink_node(this); // (the node's 0x005366A0)
}

// 0x00544750
PlayerRecord* PlayerRecord::teardown(unsigned int flags)
{
    this->~PlayerRecord();
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
        manager->spectate_player(this); // (the camera follows it again)
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
    auto id = static_cast<uint8_t>(skater_->ident_); // (the skater's object id)
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
        core::down_cast<objects::TaggingComponent>(tagging)->custom_tag_ = custom; // (0x004E4000, a setter folded there)
}

// 0x00543090
bool PlayerRecord::has_custom_tag()
{
    if (!skater_)
        return false;
    objects::ObjectModule* tagging = skater_component(skater_, kTagging);
    return tagging ? core::down_cast<objects::TaggingComponent>(tagging)->custom_tag_ : false; // (0x0051D850)
}

// 0x005430B0
void* PlayerRecord::get_graffiti_texture()
{
    if (!skater_)
        return nullptr;
    objects::ObjectModule* tagging = skater_component(skater_, kTagging);
    return tagging ? core::down_cast<objects::TaggingComponent>(tagging)->graffiti_
                   : nullptr; // (0x00553E90, a getter folded there)
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
int PlayerRecord::is_own_player() { return static_cast<int>(flag_bits_ & kPlayerLocal); }
int PlayerRecord::is_host_player() { return static_cast<int>((flag_bits_ >> 9) & 1); }
int PlayerRecord::is_observer() { return static_cast<int>((flag_bits_ >> 2) & 1); }
int PlayerRecord::is_scouting_level() { return static_cast<int>((flag_bits_ >> 16) & 1); }
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
    eliminated_time_ = get_game_mode()->get_round();
    flag_bits_ |= kPlayerEliminated;
}
bool PlayerRecord::is_eliminated()
{
    if (flag_bits_ & kPlayerEliminated)
        return true;
    if (get_game_type() != kNetFirefight || !skater_)
        return false;
    objects::ObjectModule* score = skater_component(skater_, kSkaterScore);
    return score && static_cast<modules::PointTally*>(core::down_cast<objects::RiderPointKeeper>(score)->points_held_)
                            ->get_total_score() == 0; // (its score's lives)
}
int PlayerRecord::holds_crown() { return static_cast<int>((flag_bits_ >> 7) & 1); }
int PlayerRecord::get_link_handle() { return conn_ ? conn_->hdl_ : 0; }
uint32_t PlayerRecord::get_skater_view_slot() { return static_cast<uint32_t>(skater_->view_slot_); }
int PlayerRecord::is_pending_observer() { return static_cast<int>((flag_bits_ >> 3) & 1); }

// --- The score lines' list types ---

// 0x00544720 (0x005435B0 the same with 0x00544700, the node's body, inlined)
ScoreEntry* ScoreEntry::teardown(unsigned int flags)
{
    unlink_node(this);
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

} // namespace online

