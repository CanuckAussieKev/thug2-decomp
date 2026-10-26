#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Net {
struct Conn;
} // namespace Net
namespace Obj {
class CSkater;
struct CSkaterProfile;
} // namespace Obj

// The network game's players (0x00543060-0x00544780): GameNet::PlayerInfo, one per player the manager knows (in its
// player list - FirstPlayerInfo / NextPlayerInfo), and NewPlayerInfo, a player announced but not yet added.
// The capture-the-flag and king-of-the-hill announcements are in player_flags.cpp.
namespace GameNet {

// The player's flags (PlayerInfo::m_flags).
enum PlayerFlag : uint32_t {
    kPlayerLocal = 0x1,            // this machine's player
    kPlayerJustJoined = 0x2,       // (with m_joinTime) still settling in - see IsVulnerable
    kPlayerObserver = 0x4,
    kPlayerPendingObserver = 0x8,  // joined to watch: not yet in the observers
    kPlayerFullyIn = 0x20,         // in the game (MarkAsFullyIn)
    kPlayerToldGameState = 0x40,
    kPlayerKing = 0x80,            // has the crown (king of the hill)
    kPlayerReady = 0x100,
    kPlayerServer = 0x200,         // the host's own player
    kPlayerCarryingFlag0 = 0x800,  // carries team 0's flag (0x800 << team: the four teams' flags)
    kPlayerCarryingFlags = 0x7800,
    kPlayerFaceTextureSent = 0x8000,
    kPlayerSurveying = 0x10000,    // (the byte at +0x1D2, bit 0) watching from the surveyor camera
    kPlayerEliminated = 0x20000,   // out of the game (firefight), the time in m_eliminatedTime
};

// A player not yet added (0x44 bytes, on the stack of the new-player handler 0x0052DB80): its name, object id,
// connection, a skater profile (the appearance), flags, team and the numbers of the new-player message.
struct NewPlayerInfo {
    char m_name[16];                  // +0x00
    uint8_t m_objectId;               // +0x10
    uint8_t pad11[3];
    Net::Conn* m_conn;                // +0x14
    uint32_t m_unknown18;
    Obj::CSkaterProfile* m_profile;   // +0x1C
    uint32_t m_flags;                 // +0x20 PlayerFlag
    uint32_t m_unknown24;
    int m_team;                       // +0x28
    int32_t m_profileId;              // +0x2C the GameSpy profile
    int32_t m_rating;                 // +0x30
    uint32_t m_unknown34;
    uint32_t m_unknown38;
    uint32_t m_unknown3C;
    int32_t m_vehicleControlType;     // +0x40

    // 0x005434D0: a new skater profile (made on the skater info heap; null if out of memory) and the numbers +0x24,
    // +0x2C (profile id), +0x30 (rating), +0x38, +0x40 zeroed (the rest is left). Returns this.
    NewPlayerInfo* Construct();
    // 0x00543560: the destructor: its profile destroyed and freed.
    void DestroyBody();
};
static_assert(offsetof(NewPlayerInfo, m_profile) == 0x1C && offsetof(NewPlayerInfo, m_team) == 0x28 &&
                  sizeof(NewPlayerInfo) == 0x44,
              "layout must match the original");

// A player (0x1F0 bytes; vtable 0x0064CCF4; a node of the manager's player list - Lst::Node<PlayerInfo>, its data
// itself). Also the players of a server described by the server list (ServerInfo's player list: only the name and
// the rating are used there).
class PlayerInfo : public Mem::ListNode {
public:
    static constexpr int kMaxPlayers = 8;

    // 0x00543740: an empty player with `flags` (PlayerFlag): no skater, connection or profile, 
    // "<No Name>", m_lastSentSkater 7 and the update records cleared (the per-skater ones to -1). Returns this.
    PlayerInfo* Construct(uint32_t flags);
    // 0x00543810: the destructor: its profile (destroyed and freed), the object at +0x1EC (deleted - virtual slot 2),
    // the face texture message (freed); then the node's (out of its list).
    void DestroyBody();
    // 0x00544750 (vtable slot 0): the scalar deleting destructor.
    PlayerInfo* Destroy(unsigned int flags);

    // 0x005438D0: the player's skater; a remote player the camera follows is followed again (0x0053A910).
    void SetSkater(Obj::CSkater* skater);
    // 0x00543270: its own copy of `profile` (made on the skater info heap: copied into even if the allocation
    // failed - a null `this`, as the original).
    void SetProfile(Obj::CSkaterProfile* profile);

    // 0x005431F0: the face texture message to pass on to players joining later: the skater's object id, then
    // `size` bytes of `data` (allocated on the net misc heap). 0x00543060: it (null if none).
    void SetFaceTextureData(const uint8_t* bytes, uint32_t size);
    uint8_t* GetFaceTextureData();

    // The skater's tagging component (none - no skater or component: nothing / false / null): 0x00543070 the custom
    // tag flag set, 0x00543090 read; 0x005430B0 its graffiti texture.
    void SetCustomTag(bool custom);
    bool HasCustomTag();
    void* GetGraffitiTexture();

    // 0x005430D0: the ready query's id it owes an answer to. 0x00544670: with a remote connection, waiting for the
    // answer to `id` (0: the one owed already) - its connection not ready (status 2). 0x005446C0: the answer to
    // `id`: if it's the one owed, its connection ready again (resend counts cleared) and nothing owed.
    void SetReadyQueryOwed(uint32_t id);
    void AwaitReadyResponse(uint32_t id);
    void ReceiveReadyResponse(uint32_t id);

    // 0x00543110 / 0x00543100: the time a projectile last hit it (Tmr::GetTime) / forgotten. 0x005430E0: more than
    // 5 seconds ago (1 / 0, unsigned: a hit "in the future" counts as long ago).
    void SetLastHitTime(uint32_t time);
    void ClearLastHitTime();
    int CanBeHitAgain();

    // 0x00543120 / 0x00543130: the player whose skater update was sent to this one last (the updates go round,
    // from 7). 0x00543140: how many skaters' updates go to it in one message: 1 on a narrowband connection (a
    // bandwidth type other than 0), else 2.
    int GetLastSentSkater();
    void SetLastSentSkater(int index);
    int GetSkatersPerUpdate();

    // 0x00543150 (4 bytes: source only; one of the getters folded there) / 0x00543160: the score accumulated over
    // the games of a session (AccumulateScores).
    int GetAccumulatedScore();
    void AddToAccumulatedScore(int score);

    // The flags (0 / 1 in EAX).
    int IsLocalPlayer();       // 0x00543300
    int IsServerPlayer();      // 0x00543310 (0x200)
    int IsObserver();          // 0x00543320
    int IsSurveying();         // 0x00543330
    void MarkFaceTextureSent(); // 0x00543340
    void MarkJustJoined();     // 0x00543350: and m_joinTime the time now
    void ClearJustJoined();    // 0x00543370
    int IsFullyIn();           // 0x00543380: fully in, or the local player
    void MarkAsServerPlayer(); // 0x00543390
    void ClearServerPlayer();  // 0x005433A0
    int IsCarryingFlag();      // 0x005433B0
    // 0x005433D0: the team whose flag it carries (the lowest), -1 for none.
    int GetFlagTeam();
    void ClearFlags();         // 0x00543410: carries no flag
    // 0x00543420: out of the game - m_eliminatedTime the game mode's time (0x00553270).
    void MarkEliminated();
    // 0x00543440: out of the game: marked so, or in firefight with a skater whose SkaterScore says no lives (0x00561B40
    // = 0). The flag tested first (a bool in AL).
    bool IsEliminated();
    int IsKing();              // 0x00543490
    // 0x005434A0: its connection's handle (App::EnqueueMessage's), 0 without a connection.
    int GetConnHandle();
    // 0x005434B0: the skater's +0x2BC (one of the getters folded there: no null check).
    uint32_t GetSkaterValue2BC();
    int IsPendingObserver();   // 0x005434C0

    // --- player_flags.cpp: the game's announcements about the player ---
    // 0x00543910: whether collisions with other skaters leave it alone (1): its connection not ready, its skater not
    // in play (0x00580050) or not yet 10 frames into its state history, less than a second since it joined, out of
    // lives (netfirefight), eliminated, or the skater's +0x104 bit 2; else for 5 seconds after joining the
    // just-joined flag (then cleared: 0).
    bool IsInvulnerable();
    // 0x005439D0: fully in the game (flag 0x20); the server tells a remote player (host mode 1: message 0x5E on
    // the server app, sequenced group 8) or with host mode 2 starts the game (0x0053AAC0) if nothing holds it
    // (0x005373D0).
    void MarkAsFullyIn();
    // The flag announcements (team_<n>_name for team n's name): 0x00543A90 it captured `team`'s flag (and carries
    // none now); 0x00543C70 it stole `team`'s flag (from whoever carried it); 0x00543F00 it took `team`'s flag;
    // 0x00544110 it returned its own team's flag (whoever carried it carries none). Each runs the script for the
    // local player, for the local player's team (not observing) or for the others, with String0 / String1 / team.
    void CapturedFlag(int team);
    void StoleFlag(int team);
    void TookFlag(int team);
    void RetrievedFlag();
    // 0x00544340: the crown (king of the hill): with `king`, taken from the king before it (if any) and announced -
    // NewKingYou / NewKingOther, the crown arrow - and worn (the crown, 0x005782B0); without, given up (the local
    // player's crown arrow shown again in netking / king).
    void MarkAsKing(bool king);

    Obj::CSkater* m_skater;             // +0x14
    Net::Conn* m_conn;                  // +0x18
    uint32_t m_eliminatedTime;          // +0x1C
    int m_accumulatedScore;             // +0x20
    uint32_t m_unknown24;
    uint32_t m_unknown28;
    int m_rating;                       // +0x2C (a server list's player: its rating)
    int m_team;                         // +0x30
    int m_unknown34;                    // +0x34 (scavenger hunt: 5 minus it is its score line's number)
    char m_name[16];                    // +0x38 ("<No Name>" until named)
    uint32_t m_unknown48;
    Obj::CSkaterProfile* m_profile;     // +0x4C
    uint8_t m_unknown50[0x58];
    uint16_t m_updateRecordsA[kMaxPlayers][3]; // +0xA8 (-1 each)
    uint16_t m_updateRecordsB[kMaxPlayers][3]; // +0xD8 (-1 each)
    int m_sentUpdates[kMaxPlayers];     // +0x108 (-1 each)
    int m_sentUpdates2[kMaxPlayers];    // +0x128
    uint8_t m_updateBytesA[kMaxPlayers]; // +0x148 (-1 each)
    uint8_t m_updateBytesB[kMaxPlayers]; // +0x150 (-1 each)
    uint8_t m_unknown158[0x78];
    uint32_t m_flags;                   // +0x1D0 PlayerFlag
    uint32_t m_readyQueryOwed;          // +0x1D4
    uint32_t m_unknown1D8;
    uint32_t m_joinTime;                // +0x1DC (MarkJustJoined)
    uint32_t m_lastHitTime;             // +0x1E0
    int m_lastSentSkater;               // +0x1E4
    uint8_t* m_faceTextureData;         // +0x1E8
    void* m_object1EC;                  // +0x1EC (deleted with it: virtual slot 2)
};
static_assert(offsetof(PlayerInfo, m_skater) == 0x14 && offsetof(PlayerInfo, m_team) == 0x30 &&
                  offsetof(PlayerInfo, m_name) == 0x38 && offsetof(PlayerInfo, m_profile) == 0x4C &&
                  offsetof(PlayerInfo, m_updateRecordsA) == 0xA8 && offsetof(PlayerInfo, m_sentUpdates) == 0x108 &&
                  offsetof(PlayerInfo, m_updateBytesA) == 0x148 && offsetof(PlayerInfo, m_flags) == 0x1D0 &&
                  offsetof(PlayerInfo, m_faceTextureData) == 0x1E8 && sizeof(PlayerInfo) == 0x1F0,
              "layout must match the original");

// The update records at +0x50 (0x00543180, called by the constructor on this + 0x50): its 16 words at +0xB8 (the
// player's +0x108 - +0x147) zeroed. Returns the records.
void* ClearUpdateRecords(void* records);

// The list types of the score lines (score_list.cpp) - their vtables hold only the scalar deleting destructor:
// Lst::Search<ScoreEntry> (0x0064CCE0): 0x00543170 the destructor body (the vtable reset), 0x00543580 the scalar
// deleting one; Lst::Node<ScoreEntry> (0x0064CCE4): 0x00544700 the body (out of its list), 0x005435B0 / 0x00544720
// the scalar deleting ones (inlined / calling it - two node types folded onto one vtable).
struct ScoreSearch {
    void** m_vtable;
    Mem::ListNode* m_node;
    void DestroyBody();
    ScoreSearch* Destroy(unsigned int flags);
};
struct ScoreNode : Mem::ListNode {
    void DestroyBody();
    ScoreNode* Destroy(unsigned int flags);
    ScoreNode* DestroyCalling(unsigned int flags);
};

} // namespace GameNet
