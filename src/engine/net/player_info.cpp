// The network game's players: online::PlayerRecord and JoinerRecord (see player_info.h).

#include "engine/net/player_info.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

#include "engine/core/enum_ops.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_conn.h"
#include "engine/objects/entity.h"
#include "game/skate/profile_manager.h"
#include "platform/stopwatch.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "game/rider/skater_score.h"
#include "game/rider/tagging.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "game/rider/rider.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"

namespace online {

objects::Rider* PlayerRecord::rider() const { return skater_ ? core::down_cast<objects::Rider>(skater_) : nullptr; }

using namespace detail;

namespace {
// A state not sent yet (-1 each).
constexpr uint32_t kUnsentWord = 0xFFFFFFFFU;
constexpr uint8_t kUnsentByte = 0xFF;
constexpr int kLastSkaterSlot = 7;

constexpr uint32_t kTagging = 0x5B512EDC;      // Tagging (the component)
constexpr uint32_t kSkaterScore = 0xD262802F;  // SkaterScore (the component)
constexpr uint32_t kNetFirefight = 0xBFF33600; // netfirefight

constexpr uint32_t kHitWindow = 5000; // ms


// A new skater profile on the skater info heap (null if out of memory).
gsl::owner<objects::RiderIdentity*> new_profile()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap());
    void* block = memory::alloc_zeroed(sizeof(objects::RiderIdentity));
    return block ? memory::construct_in<objects::RiderIdentity>(block) : nullptr;
}

// A node's unlinking, as lists::Elem's destructor does it (the node linked to itself after).
void unlink_node(memory::ListNode* node)
{
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
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
        memory::Hub::dealloc(profile);
    }
}

// --- PlayerRecord ---

// 0x00543180
void clear_update_records(PlayerRecord* player)
{
    std::ranges::fill(player->sent().value68_, 0U);
    std::ranges::fill(player->sent().value_6c_, 0U);
}

// 0x00543740
PlayerRecord::PlayerRecord(PlayerFlag flags) : ListNode()
{
    make_node(this);
    clear_update_records(this);
    text::format_to(label_, "<No Name>"); // (0x0064CCE8)
    for (int i = 0; i < kPlayerSlotCount; ++i) {
        core::at(sent_.value68_, i) = kUnsentWord;
        core::at(sent_.state_, i) = kUnsentByte;
        core::at(sent_.flag74_, i) = kUnsentByte;
        for (int j = 0; j < 3; ++j) {
            core::at(core::at(sent_.pos_, i), j) = -1;
            core::at(core::at(sent_.angles_, i), j) = -1;
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
    last_sent_skater_ = kLastSkaterSlot;
}

// 0x00543810
PlayerRecord::~PlayerRecord()
{
    if (objects::RiderIdentity* profile = account_) {
        profile->~RiderIdentity();
        memory::Hub::dealloc(profile);
    }
    delete observer_task_;
    if (face_texture_data_) {
        memory::Hub::dealloc(face_texture_data_);
        face_texture_data_ = nullptr;
    }
    unlink_node(this); // (the node's 0x005366A0)
}

// 0x00544750
PlayerRecord* PlayerRecord::teardown(unsigned int flags)
{
    this->~PlayerRecord();
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x005438D0
void PlayerRecord::bind_rider(objects::Rider* skater)
{
    Hub* manager = net_game_state().online_hub();
    skater_ = skater;
    if (!core::any(flag_bits_ & PlayerFlag::kLocal) && manager->fetch_viewed_player() == this) {
        manager->spectate_player(this); // (the camera follows it again)
    }
}

// 0x00543270
void PlayerRecord::set_profile(objects::RiderIdentity* profile)
{
    gsl::owner<objects::RiderIdentity*> copy = new_profile();
    account_ = copy;
    copy->copy_from(profile);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x005431F0
void PlayerRecord::set_face_texture_data(const uint8_t* bytes, uint32_t size)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->net_misc_heap());
    auto id = static_cast<uint8_t>(skater_->ident()); // (the skater's object id)
    face_texture_data_ = static_cast<uint8_t*>(memory::alloc(size + 1));
    const std::span<uint8_t> face(face_texture_data_, size + 1);
    core::at(face, 0) = id;
    std::memcpy(face.subspan(1).data(), bytes, size);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00543060
uint8_t* PlayerRecord::get_face_texture_data() { return face_texture_data_; }

// 0x00543070
void PlayerRecord::set_custom_tag(bool custom)
{
    if (!skater_) {
        return;
    }
    if (objects::ObjectModule* tagging = rider()->fetch_module(kTagging)) {
        core::down_cast<objects::TaggingComponent>(tagging)->set_custom_tag(custom); // (0x004E4000, a setter folded there)
    }
}

// 0x00543090
bool PlayerRecord::has_custom_tag()
{
    if (!skater_) {
        return false;
    }
    objects::ObjectModule* tagging = rider()->fetch_module(kTagging);
    return tagging ? core::down_cast<objects::TaggingComponent>(tagging)->custom_tag() : false; // (0x0051D850)
}

// 0x005430B0
void* PlayerRecord::get_graffiti_texture()
{
    if (!skater_) {
        return nullptr;
    }
    objects::ObjectModule* tagging = rider()->fetch_module(kTagging);
    return tagging ? core::down_cast<objects::TaggingComponent>(tagging)->graffiti()
                   : nullptr; // (0x00553E90, a getter folded there)
}

// 0x005430D0
void PlayerRecord::set_ready_query_owed(uint32_t id) { ready_query_owed_ = id; }

// 0x00544670
void PlayerRecord::await_ready_response(uint32_t id)
{
    if (!conn_ || !conn_->is_off_machine()) {
        return;
    }
    if (id != 0U) {
        ready_query_owed_ = id;
    }
    conn_->cur_standing() &= ~net::Conn::kConnReady;
    conn_->cur_standing() |= net::Conn::kStatus2;
}

// 0x005446C0
void PlayerRecord::receive_ready_response(uint32_t id)
{
    if (ready_query_owed_ != id) {
        return;
    }
    if (conn_) {
        conn_->cur_standing() &= ~net::Conn::kStatus2;
        conn_->cur_standing() |= net::Conn::kConnReady;
        conn_->clear_resend_counts();
    }
    ready_query_owed_ = 0;
}

// 0x00543110 / 0x00543100 / 0x005430E0
void PlayerRecord::change_recent_hit_stamp(uint32_t time) { last_impact_at_ = time; }
void PlayerRecord::clear_last_hit_time() { last_impact_at_ = 0; }
int PlayerRecord::can_be_hit_again() const { return frame_clock::get_time() - last_impact_at_ > kHitWindow ? 1 : 0; }

// 0x00543120 / 0x00543130 / 0x00543140
int PlayerRecord::get_last_sent_skater() const { return last_sent_skater_; }
void PlayerRecord::set_last_sent_skater(int index) { last_sent_skater_ = index; }
int PlayerRecord::get_skaters_per_update() { return conn_->get_link_speed_class() != 0 ? 1 : 2; }

// 0x00543150 / 0x00543160
int PlayerRecord::get_accumulated_score() const { return accumulated_score_; }
void PlayerRecord::add_to_accumulated_score(int score) { accumulated_score_ += score; }

// 0x00543300 - 0x005434C0: the flags.
int PlayerRecord::is_own_player() { return static_cast<int>(flag_bits_ & PlayerFlag::kLocal); }
int PlayerRecord::is_host_player() { return static_cast<int>(core::any(flag_bits_ & PlayerFlag::kServer)); }
int PlayerRecord::is_observer() { return static_cast<int>(core::any(flag_bits_ & PlayerFlag::kObserver)); }
int PlayerRecord::is_scouting_level() { return static_cast<int>(core::any(flag_bits_ & PlayerFlag::kSurveying)); }
void PlayerRecord::mark_capture_blocked() { flag_bits_ |= PlayerFlag::kCaptureBlocked; }
void PlayerRecord::mark_just_joined()
{
    join_time_ = frame_clock::get_time();
    flag_bits_ |= PlayerFlag::kJustJoined;
}
void PlayerRecord::clear_just_joined() { flag_bits_ &= ~PlayerFlag::kJustJoined; }
int PlayerRecord::has_finished_joining()
{
    return core::any(flag_bits_ & (PlayerFlag::kFullyIn | PlayerFlag::kLocal)) ? 1 : 0;
}
void PlayerRecord::set_host_player() { flag_bits_ |= PlayerFlag::kServer; }
void PlayerRecord::clear_server_player() { flag_bits_ &= ~PlayerFlag::kServer; }
int PlayerRecord::is_carrying_flag() { return core::any(flag_bits_ & PlayerFlag::kCarryingFlags) ? 1 : 0; }
int PlayerRecord::get_flag_team()
{
    for (int team = 0; team < 4; ++team) {
        if (core::any(flag_bits_ & carrying_flag_of(team))) {
            return team;
        }
    }
    return -1;
}
void PlayerRecord::clear_flags() { flag_bits_ &= ~PlayerFlag::kCarryingFlags; }
void PlayerRecord::mark_eliminated()
{
    eliminated_time_ = get_game_mode()->get_round();
    flag_bits_ |= PlayerFlag::kEliminated;
}
bool PlayerRecord::is_eliminated()
{
    if (core::any(flag_bits_ & PlayerFlag::kEliminated)) {
        return true;
    }
    if (get_game_type() != kNetFirefight || !skater_) {
        return false;
    }
    objects::ObjectModule* score = rider()->fetch_module(kSkaterScore);
    return (score != nullptr) && core::down_cast<objects::RiderPointKeeper>(score)->points_held()->get_total_score() ==
                                     0; // (its score's lives)
}
int PlayerRecord::holds_crown() { return static_cast<int>(core::any(flag_bits_ & PlayerFlag::kKing)); }
int PlayerRecord::get_link_handle() { return conn_ ? conn_->hdl() : 0; }
uint32_t PlayerRecord::get_skater_view_slot() const { return static_cast<uint32_t>(rider()->view_slot()); }
int PlayerRecord::is_pending_observer() { return static_cast<int>(core::any(flag_bits_ & PlayerFlag::kPendingObserver)); }

// --- The score lines' list types ---

// 0x00544720 (0x005435B0 the same with 0x00544700, the node's body, inlined)
ScoreEntry* ScoreEntry::teardown(unsigned int flags)
{
    unlink_node(this);
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

void PlayerRecord::delete_observer_task()
{
    delete observer_task_;
    observer_task_ = nullptr;
}

} // namespace online

