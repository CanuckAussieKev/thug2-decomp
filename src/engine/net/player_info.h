#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace net {
struct Conn;
} // namespace net
namespace objects {
class Rider;
struct RiderIdentity;
} // namespace objects

// The network game's players (0x00543060-0x00544780): online::PlayerRecord, one per player the manager knows (in its
// player list - begin_player_walk / step_player_walk), and JoinerRecord, a player announced but not yet added.
// The capture-the-flag and king-of-the-hill announcements are in player_flags.cpp.
namespace online {

// The player's flags (PlayerRecord::flag_bits_).
enum PlayerFlag : uint32_t {
    kPlayerLocal = 0x1,      // this machine's player
    kPlayerJustJoined = 0x2, // (with join_time_) still settling in - see IsVulnerable
    kPlayerObserver = 0x4,
    kPlayerPendingObserver = 0x8, // joined to watch: not yet in the observers
    kPlayerFullyIn = 0x20,        // in the game (set_finished_joining)
    kPlayerToldGameState = 0x40,
    kPlayerKing = 0x80, // has the crown (king of the hill)
    kPlayerReady = 0x100,
    kPlayerServer = 0x200,        // the host's own player
    kPlayerCarryingFlag0 = 0x800, // carries team 0's flag (0x800 << team: the four teams' flags)
    kPlayerCarryingFlags = 0x7800,
    kPlayerFaceTextureSent = 0x8000,
    kPlayerSurveying = 0x10000,  // (the byte at +0x1D2, bit 0) watching from the surveyor camera
    kPlayerEliminated = 0x20000, // out of the game (firefight), the time in eliminated_time_
};

// A player not yet added (0x44 bytes, on the stack of the new-player handler 0x0052DB80): its name, object id,
// connection, a skater profile (the appearance), flags, team and the numbers of the new-player message.
struct JoinerRecord {
    char label_[16];     // +0x00
    uint8_t entity_key_; // +0x10
    uint8_t pad11[3];
    net::Conn* conn_; // +0x14
    uint32_t unknown18_;
    objects::RiderIdentity* account_; // +0x1C
    uint32_t flag_bits_;              // +0x20 PlayerFlag
    uint32_t unknown24_;
    int team_;             // +0x28
    int32_t profile_id_;   // +0x2C the GameSpy profile
    int32_t skill_points_; // +0x30
    uint32_t unknown34_;
    uint32_t unknown38_;
    uint32_t unknown3c_;
    int32_t drive_control_mode_; // +0x40

    // 0x005434D0: a new skater profile (made on the skater info heap; null if out of memory) and the numbers +0x24,
    // +0x2C (profile id), +0x30 (rating), +0x38, +0x40 zeroed (the rest is left). Returns this.
    JoinerRecord* make_new();
    // 0x00543560: the destructor: its profile destroyed and freed.
    void destroy_body();
};
static_assert(offsetof(JoinerRecord, account_) == 0x1C && offsetof(JoinerRecord, team_) == 0x28 &&
                  sizeof(JoinerRecord) == 0x44,
              "layout must match the original");

// A player (0x1F0 bytes; vtable 0x0064CCF4; a node of the manager's player list - lists::Elem<PlayerRecord>, its data
// itself). Also the players of a server described by the server list (HostListing's player list: only the name and
// the rating are used there).
class PlayerRecord : public memory::ListNode {
public:
    static constexpr int kMaxPlayers = 8;

    // 0x00543740: an empty player with `flags` (PlayerFlag): no skater, connection or profile,
    // "<No Name>", last_sent_skater_ 7 and the update records cleared (the per-skater ones to -1). Returns this.
    PlayerRecord* make_new(uint32_t flags);
    // 0x00543810: the destructor: its profile (destroyed and freed), the object at +0x1EC (deleted - virtual slot 2),
    // the face texture message (freed); then the node's (out of its list).
    void destroy_body();
    // 0x00544750 (vtable slot 0): the scalar deleting destructor.
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
    int can_be_hit_again();

    // 0x00543120 / 0x00543130: the player whose skater update was sent to this one last (the updates go round,
    // from 7). 0x00543140: how many skaters' updates go to it in one message: 1 on a narrowband connection (a
    // bandwidth type other than 0), else 2.
    int get_last_sent_skater();
    void set_last_sent_skater(int index);
    int get_skaters_per_update();

    // 0x00543150 (4 bytes: source only; one of the getters folded there) / 0x00543160: the score accumulated over
    // the games of a session (AccumulateScores).
    int get_accumulated_score();
    void add_to_accumulated_score(int score);

    // The flags (0 / 1 in EAX).
    int is_own_player();           // 0x00543300
    int is_host_player();          // 0x00543310 (0x200)
    int is_observer();             // 0x00543320
    int is_scouting_level();       // 0x00543330
    void mark_face_texture_sent(); // 0x00543340
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
    // 0x005434B0: the skater's +0x2BC (one of the getters folded there: no null check).
    uint32_t get_skater_value_2b_c();
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

    objects::Rider* skater_;   // +0x14
    net::Conn* conn_;          // +0x18
    uint32_t eliminated_time_; // +0x1C
    int accumulated_score_;    // +0x20
    uint32_t unknown24_;
    uint32_t unknown28_;
    int skill_points_; // +0x2C (a server list's player: its rating)
    int team_;         // +0x30
    int unknown34_;    // +0x34 (scavenger hunt: 5 minus it is its score line's number)
    char label_[16];   // +0x38 ("<No Name>" until named)
    uint32_t unknown48_;
    objects::RiderIdentity* account_; // +0x4C
    uint8_t unknown50_[0x58];
    uint16_t update_records_a_[kMaxPlayers][3]; // +0xA8 (-1 each)
    uint16_t update_records_b_[kMaxPlayers][3]; // +0xD8 (-1 each)
    int sent_updates_[kMaxPlayers];             // +0x108 (-1 each)
    int sent_updates2_[kMaxPlayers];            // +0x128
    uint8_t update_bytes_a_[kMaxPlayers];       // +0x148 (-1 each)
    uint8_t update_bytes_b_[kMaxPlayers];       // +0x150 (-1 each)
    uint8_t unknown158_[0x78];
    uint32_t flag_bits_;        // +0x1D0 PlayerFlag
    uint32_t ready_query_owed_; // +0x1D4
    uint32_t unknown1d8_;
    uint32_t join_time_;         // +0x1DC (mark_just_joined)
    uint32_t last_impact_at_;    // +0x1E0
    int last_sent_skater_;       // +0x1E4
    uint8_t* face_texture_data_; // +0x1E8
    void* object_1e_c_;          // +0x1EC (deleted with it: virtual slot 2)
};
static_assert(offsetof(PlayerRecord, skater_) == 0x14 && offsetof(PlayerRecord, team_) == 0x30 &&
                  offsetof(PlayerRecord, label_) == 0x38 && offsetof(PlayerRecord, account_) == 0x4C &&
                  offsetof(PlayerRecord, update_records_a_) == 0xA8 && offsetof(PlayerRecord, sent_updates_) == 0x108 &&
                  offsetof(PlayerRecord, update_bytes_a_) == 0x148 && offsetof(PlayerRecord, flag_bits_) == 0x1D0 &&
                  offsetof(PlayerRecord, face_texture_data_) == 0x1E8 && sizeof(PlayerRecord) == 0x1F0,
              "layout must match the original");

// The update records at +0x50 (0x00543180, called by the constructor on this + 0x50): its 16 words at +0xB8 (the
// player's +0x108 - +0x147) zeroed. Returns the records.
void* clear_update_records(void* records);

// The list types of the score lines (score_list.cpp) - their vtables hold only the scalar deleting destructor:
// lists::Hunt<ScoreEntry> (0x0064CCE0): 0x00543170 the destructor body (the vtable reset), 0x00543580 the scalar
// deleting one; lists::Elem<ScoreEntry> (0x0064CCE4): 0x00544700 the body (out of its list), 0x005435B0 / 0x00544720
// the scalar deleting ones (inlined / calling it - two node types folded onto one vtable).
struct ScoreSearch {
    void** vtable_;
    memory::ListNode* list_link_;
    void destroy_body();
    ScoreSearch* teardown(unsigned int flags);
};
struct ScoreNode : memory::ListNode {
    void destroy_body();
    ScoreNode* teardown(unsigned int flags);
    ScoreNode* destroy_calling(unsigned int flags);
};

} // namespace online
