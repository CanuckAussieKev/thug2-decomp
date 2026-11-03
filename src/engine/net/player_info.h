#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/enum_ops.h"

#include <gsl/pointers>
#include <array>
#include "engine/core/arena_alloc.h"
#include "engine/script/param_struct.h"

namespace net {
struct Conn;
} // namespace net
namespace objects {
class Rider;
class GameEntity;
struct RiderIdentity;
} // namespace objects

// The network game's players (0x00543060-0x00544780): online::PlayerRecord, one per player the manager knows (in its
// player list - begin_player_walk / step_player_walk), and JoinerRecord, a player announced but not yet added.
// The capture-the-flag and king-of-the-hill announcements are in player_flags.cpp.
namespace tasks {
class SchedulerTask;
}

namespace online {

// The player's flags (PlayerRecord::flag_bits_).
enum class PlayerFlag : uint32_t {
    kNone = 0,
    kLocal = 0x1, // this machine's player
    kJustJoined = 0x2, // (with join_time_) still settling in - see IsVulnerable
    kObserver = 0x4,
    kPendingObserver = 0x8, // joined to watch: not yet in the observers
    kRejoining = 0x10, // (a pending player added back)
    kFullyIn = 0x20, // in the game (set_finished_joining)
    kToldGameState = 0x40,
    kKing = 0x80, // has the crown (king of the hill)
    kReady = 0x100,
    kServer = 0x200, // the host's own player
    kMarked = 0x400, // chosen in the kick / ban menu (its options dialog is up)
    kCarryingFlag0 = 0x800, // carries team 0's flag (0x800 << team: the four teams' flags)
    kCarryingFlags = 0x7800,
    kCaptureBlocked = 0x8000, // restarted in a flag or crown game: may not capture until it drops what it carries
    kSurveying = 0x10000, // (the byte at +0x1D2, bit 0) watching from the surveyor camera
    kEliminated = 0x20000, // out of the game (firefight), the time in eliminated_time_
};
// Team `team`'s flag carried (kCarryingFlag0 for team 0, the next bits for the others).
[[nodiscard]] constexpr PlayerFlag carrying_flag_of(int team)
{
    return static_cast<PlayerFlag>(core::to_underlying(PlayerFlag::kCarryingFlag0) << team);
}
constexpr bool enable_flag_operators(PlayerFlag) { return true; }

// The player slots of a network game: at most this many players, skater object ids 0 to 7 (the rider slots of
// the skate module, the per-skater records a player keeps).
constexpr int kPlayerSlotCount = 8;

constexpr size_t kJoinerRecordLabelSize = 16;
constexpr PlayerFlag kPlayerCountedFlags = PlayerFlag::kPendingObserver | PlayerFlag::kRejoining; // counted among those joining
// A player not yet added (0x44 bytes, on the stack of the new-player handler 0x0052DB80): its name, object id,
// connection, a skater profile (the appearance), flags, team and the numbers of the new-player message.
struct JoinerRecord {
    // The fields other code reads and sets.
    std::array<char, kJoinerRecordLabelSize>& label() { return label_; }
    [[nodiscard]] const std::array<char, kJoinerRecordLabelSize>& label() const { return label_; }
    [[nodiscard]] uint8_t entity_key() const { return entity_key_; }
    void set_entity_key(uint8_t value) { entity_key_ = value; }
    [[nodiscard]] net::Conn* conn() const { return conn_; }
    void set_conn(net::Conn* value) { conn_ = value; }
    [[nodiscard]] int add_at_frame() const { return add_at_frame_; }
    void set_add_at_frame(int value) { add_at_frame_ = value; }
    [[nodiscard]] objects::RiderIdentity* account() const { return account_; }
    PlayerFlag& flag_bits() { return flag_bits_; }
    [[nodiscard]] PlayerFlag flag_bits() const { return flag_bits_; }
    [[nodiscard]] int rejoin_group() const { return rejoin_group_; }
    void set_rejoin_group(int value) { rejoin_group_ = value; }
    [[nodiscard]] int team() const { return team_; }
    void set_team(int value) { team_ = value; }
    [[nodiscard]] int32_t profile_id() const { return profile_id_; }
    void set_profile_id(int32_t value) { profile_id_ = value; }
    [[nodiscard]] int32_t skill_points() const { return skill_points_; }
    void set_skill_points(int32_t value) { skill_points_ = value; }
    [[nodiscard]] int score() const { return score_; }
    void set_score(int value) { score_ = value; }
    [[nodiscard]] uint32_t accumulated_score() const { return accumulated_score_; }
    void set_accumulated_score(uint32_t value) { accumulated_score_ = value; }
    [[nodiscard]] uint32_t eliminated_time() const { return eliminated_time_; }
    void set_eliminated_time(uint32_t value) { eliminated_time_ = value; }
    [[nodiscard]] int32_t drive_control_mode() const { return drive_control_mode_; }
    void set_drive_control_mode(int32_t value) { drive_control_mode_ = value; }

private:
    std::array<char, kJoinerRecordLabelSize> label_{};     // +0x00
    uint8_t entity_key_{}; // +0x10
    std::array<uint8_t, 3> pad11{};
    net::Conn* conn_{};               // +0x14
    int add_at_frame_{};              // +0x18 the server app's frame from which it may be added
    objects::RiderIdentity* account_; // +0x1C
    PlayerFlag flag_bits_{};          // +0x20
    int rejoin_group_{0};             // +0x24 (a player joining again: the server app's frame when it was listed)
    int team_{};                      // +0x28
    int32_t profile_id_{0};           // +0x2C the GameSpy profile
    int32_t skill_points_{0};         // +0x30
    int score_{};                     // +0x34 (its score component's grand total)
    uint32_t accumulated_score_{0};   // +0x38 (the player's accumulated_score_)
    uint32_t eliminated_time_{};      // +0x3C (the player's eliminated_time_)
    int32_t drive_control_mode_{0};   // +0x40

public:

    // 0x005434D0: a new skater profile (made on the skater info heap; null if out of memory) and the numbers +0x24,
    // +0x2C (profile id), +0x30 (rating), +0x38, +0x40 zeroed (the rest is left).
    JoinerRecord();
    // 0x00543560: the destructor: its profile destroyed and freed.
    ~JoinerRecord();
    JoinerRecord(const JoinerRecord&) = delete;
    JoinerRecord& operator=(const JoinerRecord&) = delete;
};

// What the server last sent a player about each of the 8 skaters (by object id) - the player's +0x50 (SkateNet's
// relay: get_relay_flags, write_object_update).
struct SentSkaterUpdates {
    std::array<uint32_t, kPlayerSlotCount> forced_frame_; // +0x00 the server app's frame when everything was last sent
    std::array<uint32_t, kPlayerSlotCount> time_;         // +0x20 the state entry's time stamp last sent
    std::array<uint8_t, kPlayerSlotCount> value_7c_;      // +0x40
    std::array<uint8_t, kPlayerSlotCount> flag75_;        // +0x48
    std::array<uint8_t, kPlayerSlotCount> flag76_;        // +0x50
    std::array<std::array<int16_t, 3>, kPlayerSlotCount> pos_;        // +0x58 (-1 each)
    std::array<std::array<int16_t, 3>, kPlayerSlotCount> angles_;     // +0x88 (-1 each)
    std::array<uint32_t, kPlayerSlotCount> value68_;      // +0xB8 (-1 each)
    std::array<uint32_t, kPlayerSlotCount> value_6c_;     // +0xD8
    std::array<uint8_t, kPlayerSlotCount> state_;         // +0xF8 (-1 each)
    std::array<uint8_t, kPlayerSlotCount> flag74_;        // +0x100 (-1 each)
    std::array<uint8_t, kPlayerSlotCount> unknown108_;
    std::array<uint32_t, kPlayerSlotCount> value80_; // +0x110
};

constexpr size_t kPlayerRecordLabelSize = 16;
constexpr size_t kPlayerRecordPlacedSize = 5;
// A player (0x1F0 bytes; a node of the manager's player list - lists::Elem<PlayerRecord>, its data itself - whose node
// keeps the original's size: the net code reads players by byte offset). Also the players of a server described by the server list (HostListing's player list: only the name and
// the rating are used there).
class PlayerRecord : public memory::ListNode {
public:
    // 0x00543740: an empty player with `flags` (PlayerFlag): no skater, connection or profile,
    // "<No Name>", last_sent_skater_ 7 and the update records cleared (the per-skater ones to -1).
    explicit PlayerRecord(PlayerFlag flags);
    // 0x00543810: the destructor: its profile (destroyed and freed), the observer task (deleted),
    // the face texture message (freed); then the node's (out of its list).
    ~PlayerRecord();
    PlayerRecord(const PlayerRecord&) = delete;
    PlayerRecord& operator=(const PlayerRecord&) = delete;
    // 0x00544750: the scalar deleting destructor.
    PlayerRecord* teardown(unsigned int flags);

    // 0x005438D0: the player's skater; a remote player the camera follows is followed again (0x0053A910).
    void bind_rider(objects::Rider* skater);
    // 0x00543270: its own copy of `profile` (made on the skater info heap: copied into even if the allocation
    // failed - a null `this`, as the original).
    void set_profile(objects::RiderIdentity* profile);

    // 0x005431F0: the face texture message to pass on to players joining later: the skater's object id, then
    // `size` bytes of `data` (allocated on the net misc heap). 0x00543060: it (null if none).
    void set_face_texture_data(const uint8_t* bytes, uint32_t size);
    uint8_t* get_face_texture_data();

    // The skater's tagging component (none - no skater or component: nothing / false / null): 0x00543070 the custom
    // tag flag set, 0x00543090 read; 0x005430B0 its graffiti texture.
    void set_custom_tag(bool custom);
    bool has_custom_tag();
    void* get_graffiti_texture();

    // 0x005430D0: the ready query's id it owes an answer to. 0x00544670: with a remote connection, waiting for the
    // answer to `id` (0: the one owed already) - its connection not ready (status 2). 0x005446C0: the answer to
    // `id`: if it's the one owed, its connection ready again (resend counts cleared) and nothing owed.
    void set_ready_query_owed(uint32_t id);
    void await_ready_response(uint32_t id);
    void receive_ready_response(uint32_t id);

    // 0x00543110 / 0x00543100: the time a projectile last hit it (frame_clock::GetTime) / forgotten. 0x005430E0: more than
    // 5 seconds ago (1 / 0, unsigned: a hit "in the future" counts as long ago).
    void change_recent_hit_stamp(uint32_t time);
    void clear_last_hit_time();
    [[nodiscard]] int can_be_hit_again() const;

    // 0x00543120 / 0x00543130: the player whose skater update was sent to this one last (the updates go round,
    // from 7). 0x00543140: how many skaters' updates go to it in one message: 1 on a narrowband connection (a
    // bandwidth type other than 0), else 2.
    [[nodiscard]] int get_last_sent_skater() const;
    void set_last_sent_skater(int index);
    int get_skaters_per_update();

    // 0x00543150 (4 bytes: source only; one of the getters folded there) / 0x00543160: the score accumulated over
    // the games of a session (AccumulateScores).
    [[nodiscard]] int get_accumulated_score() const;
    void add_to_accumulated_score(int score);

    // The flags (0 / 1 in EAX).
    int is_own_player();           // 0x00543300
    int is_host_player();          // 0x00543310 (0x200)
    int is_observer();             // 0x00543320
    int is_scouting_level();       // 0x00543330
    void mark_capture_blocked(); // 0x00543340
    void mark_just_joined();       // 0x00543350: and join_time_ the time now
    void clear_just_joined();      // 0x00543370
    int has_finished_joining();    // 0x00543380: fully in, or the local player
    void set_host_player();        // 0x00543390
    void clear_server_player();    // 0x005433A0
    int is_carrying_flag();        // 0x005433B0
    // 0x005433D0: the team whose flag it carries (the lowest), -1 for none.
    int get_flag_team();
    void clear_flags(); // 0x00543410: carries no flag
    // 0x00543420: out of the game - eliminated_time_ the game mode's time (0x00553270).
    void mark_eliminated();
    // 0x00543440: out of the game: marked so, or in firefight with a skater whose SkaterScore says no lives (0x00561B40
    // = 0). The flag tested first (a bool in AL).
    bool is_eliminated();
    int holds_crown(); // 0x00543490
    // 0x005434A0: its connection's handle (AppHost::put_on_outbox's), 0 without a connection.
    int get_link_handle();
    // 0x005434B0: the skater's view slot (one of the getters folded there: no null check).
    [[nodiscard]] uint32_t get_skater_view_slot() const;
    int is_pending_observer(); // 0x005434C0

    // --- player_flags.cpp: the game's announcements about the player ---
    // 0x00543910: whether collisions with other skaters leave it alone (1): its connection not ready, its skater not
    // in play (0x00580050) or not yet 10 frames into its state history, less than a second since it joined, out of
    // lives (netfirefight), eliminated, or the skater's +0x104 bit 2; else for 5 seconds after joining the
    // just-joined flag (then cleared: 0).
    bool is_invulnerable();
    // 0x005439D0: fully in the game (flag 0x20); the server tells a remote player (host mode 1: message 0x5E on
    // the server app, sequenced group 8) or with host mode 2 starts the game (0x0053AAC0) if nothing holds it
    // (0x005373D0).
    void set_finished_joining();
    // The flag announcements (team_<n>_name for team n's name): 0x00543A90 it captured `team`'s flag (and carries
    // none now); 0x00543C70 it stole `team`'s flag (from whoever carried it); 0x00543F00 it took `team`'s flag;
    // 0x00544110 it returned its own team's flag (whoever carried it carries none). Each runs the script for the
    // local player, for the local player's team (not observing) or for the others, with String0 / String1 / team.
    void score_flag_capture(int team);
    void note_flag_steal(int team);
    void note_flag_pickup(int team);
    void note_flag_returned();
    // 0x00544340: the crown (king of the hill): with `king`, taken from the king before it (if any) and announced -
    // NewKingYou / NewKingOther, the crown arrow - and worn (the crown, 0x005782B0); without, given up (the local
    // player's crown arrow shown again in netking / king).
    void give_crown(bool king);

    // The fields other code reads and sets.
    [[nodiscard]] objects::GameEntity* skater() const { return skater_; }
    void set_skater(objects::GameEntity* value) { skater_ = value; }
    [[nodiscard]] net::Conn* conn() const { return conn_; }
    void set_conn(net::Conn* value) { conn_ = value; }
    [[nodiscard]] uint32_t eliminated_time() const { return eliminated_time_; }
    void set_eliminated_time(uint32_t value) { eliminated_time_ = value; }
    [[nodiscard]] int accumulated_score() const { return accumulated_score_; }
    void set_accumulated_score(int value) { accumulated_score_ = value; }
    [[nodiscard]] int best_combo() const { return best_combo_; }
    void set_best_combo(int value) { best_combo_ = value; }
    [[nodiscard]] int32_t profile_id() const { return profile_id_; }
    void set_profile_id(int32_t value) { profile_id_ = value; }
    [[nodiscard]] int skill_points() const { return skill_points_; }
    void set_skill_points(int value) { skill_points_ = value; }
    [[nodiscard]] int team() const { return team_; }
    void set_team(int value) { team_ = value; }
    int& num_placed() { return num_placed_; }
    [[nodiscard]] int num_placed() const { return num_placed_; }
    std::array<char, kPlayerRecordLabelSize>& label() { return label_; }
    [[nodiscard]] const std::array<char, kPlayerRecordLabelSize>& label() const { return label_; }
    [[nodiscard]] int32_t drive_control_mode() const { return drive_control_mode_; }
    void set_drive_control_mode(int32_t value) { drive_control_mode_ = value; }
    [[nodiscard]] objects::RiderIdentity* account() const { return account_; }
    SentSkaterUpdates& sent() { return sent_; }
    [[nodiscard]] const SentSkaterUpdates& sent() const { return sent_; }
    std::array<script::VectorValue, kPlayerRecordPlacedSize>& placed() { return placed_; }
    [[nodiscard]] const std::array<script::VectorValue, kPlayerRecordPlacedSize>& placed() const { return placed_; }
    PlayerFlag& flag_bits() { return flag_bits_; }
    [[nodiscard]] PlayerFlag flag_bits() const { return flag_bits_; }
    [[nodiscard]] int rejoin_group() const { return rejoin_group_; }
    void set_rejoin_group(int value) { rejoin_group_ = value; }
    [[nodiscard]] uint8_t* face_texture_data() const { return face_texture_data_; }
    void assign_face_texture_data(uint8_t* value) { face_texture_data_ = value; }
    [[nodiscard]] tasks::SchedulerTask* observer_task() const { return static_cast<tasks::SchedulerTask*>(observer_task_); } // (a view: it stays the owner's)
    void set_observer_task(gsl::owner<tasks::SchedulerTask*> value) { observer_task_ = value; }
    // The observer task deleted (where observing ends; player_info.cpp).
    void delete_observer_task();

private:
    objects::GameEntity* skater_; // +0x14 (a Rider)

public:
    // The skater as the Rider it is (null without one).
    [[nodiscard]] objects::Rider* rider() const;
private:
    net::Conn* conn_{};          // +0x18
    uint32_t eliminated_time_;   // +0x1C
    int accumulated_score_{};    // +0x20
    int best_combo_{};           // +0x24 the game stats' best combo (zeroed when a game starts)
    int32_t profile_id_;         // +0x28 the GameSpy profile
    int skill_points_;           // +0x2C (a server list's player: its rating)
    int team_;                   // +0x30
    int num_placed_;             // +0x34 scavenger hunt: its objects placed (5 minus it is its score line's number)
    std::array<char, kPlayerRecordLabelSize> label_{};           // +0x38 ("<No Name>" until named)
    int32_t drive_control_mode_; // +0x48 its vehicle control type
    objects::RiderIdentity* account_;     // +0x4C
    SentSkaterUpdates sent_{};            // +0x50 (the server's)
    std::array<script::VectorValue, kPlayerRecordPlacedSize> placed_{};     // +0x180 scavenger hunt: where its objects were placed (num_placed_)
    PlayerFlag flag_bits_{};                                                // +0x1D0
    uint32_t ready_query_owed_{};         // +0x1D4
    int rejoin_group_;                    // +0x1D8 (JoinerRecord::rejoin_group_)
    uint32_t join_time_;                  // +0x1DC (mark_just_joined)
    uint32_t last_impact_at_;             // +0x1E0
    int last_sent_skater_;                // +0x1E4
    uint8_t* face_texture_data_;          // +0x1E8
    gsl::owner<tasks::SchedulerTask*> observer_task_; // +0x1EC while observing (gamenet_observer.h; deleted with it)
};

// 0x00543180 (called by the constructor on the player's +0x50, returning it): the sent records' value68_ and
// value_6c_ zeroed (the 16 words at +0xB8 of it).
void clear_update_records(PlayerRecord* player);

// (The score lines' list types (score_list.cpp): lists::Hunt<ScoreEntry>'s destructors 0x00543170 / 0x00543580 only
// reset its table - not kept; lists::Elem<ScoreEntry>'s 0x00544700 (out of its list) and 0x005435B0 / 0x00544720 are
// ScoreEntry::teardown.)

} // namespace online
