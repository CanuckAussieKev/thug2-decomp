#pragma once

#include <cstdint>

#include "game/util/bitstream.h"

// The skate module's network side (0x0056C920-0x0056F090): the message handlers it gives the server and client
// apps - the game's start, the scores, the scavenger hunt, graffiti colours, scripts run on every machine, the
// other skaters' positions -, the two tasks it runs in the main loop, and StartServer / AddClientHandlers that
// register them. Mdl::Skate itself (the singleton at 0x007CE478) isn't rewritten: only the fields used here.
// From the disassembly; the handlers are __cdecl Net::MsgHandlerCode (context in, 1 = carry on; 2 / 3 stop the
// packet - the client was told to leave; 4 = not handled).
namespace Net {
struct App;
struct MsgHandlerContext;
} // namespace Net
namespace Obj {
class CObject;
} // namespace Obj

namespace Mdl {

class SkateNet {
public:
    // 0x0056E2B0: the two Tsk::Task<Skate>s (vtable 0x006476F0, priority -1000, from the default heap) the server
    // adds to the main loop: +0x18 runs 0x00569BB0, +0x1C 0x0056BE10 (null when out of memory).
    void CreateNetworkTasks();
    // 0x0056DEE0 (CFunc StartServer): the network manager's server app (0x0053C380; whatever 0x0044E4E0 reports
    // is stopped first, 0x005399F0, and the app made with 0 instead of 1); the tasks into the main loop; the
    // server's handlers (and, when it isn't only local, the ones that relay the clients' messages).
    void StartServer();
    // 0x0056EB50 (JoinServer; a split-screen game's second client): the client's handlers, with the skaters'
    // own (their object updates, the score events...) for the skaters of client `index`; client 0 also gets the
    // scavenger hunt's 0x91 / 0x92 / 0x94.
    void AddClientHandlers(Net::App* client, int index);

    // 0x0056C920 (0x51, 0x56): the server removed us ('Q') or moved on without us ('V'): the dialog, then back
    // to the main menu (2 when one is already on its way, otherwise 3 and the game left).
    static int __cdecl HandleServerRemovedYou(Net::MsgHandlerContext* context);
    // 0x0056CA70 (0x91-0x95): the scavenger hunt - time added; the hunt / interval / collect period goals started;
    // 0x95: objects left (one or five; [0] the player or team, [1] the count) announced.
    static int __cdecl HandleScavengerHunt(Net::MsgHandlerContext* context);
    // 0x0056CDF0 (0x90): an object of the hunt picked up: ScavengerHunt_KillObject id=[0].
    static int __cdecl HandleScavengerKillObject(Net::MsgHandlerContext* context);
    // 0x0056CEC0 (0x8F): a player's objects dropped ([0] the player, [4] the count, [8] 16-byte positions):
    // DropSHObject on its skater for each. 4 when the player isn't known.
    static int __cdecl HandleScavengerDropObjects(Net::MsgHandlerContext* context);
    // 0x0056D030 (0x5C): the graffiti colours of each of 8 players (a count byte each, then the colour indexes):
    // arrays Skater<n> given to the profile manager (0x007CE500). 4 when this isn't a graffiti game.
    static int __cdecl HandleGraffitiColors(Net::MsgHandlerContext* context);
    // 0x0056D250 (0x40): the scores: each skater's (and the elimination of those out of netfirefight), the
    // cheating check, then the leader (player or team) announced or, at the end ([8] set), won / lost.
    static int __cdecl HandleScores(Net::MsgHandlerContext* context);
    // 0x0056D810 (0x33): the game's start: the network settings, the level requested, the game mode and its
    // victory conditions, the mode's goal_script started.
    static int __cdecl HandleStartInfo(Net::MsgHandlerContext* context);
    // 0x0056E380 (0x48): a script run on every machine ([0] the script, [4] the object, [8] the parameters as
    // written by the script writer), from the top-down heap.
    static int __cdecl HandleRunScript(Net::MsgHandlerContext* context);
    // 0x0056E470 (0x23): the other skaters' updates - a bit stream: a mask of 8 objects, then for each a time
    // stamp, the fields that changed (position, angles, state, flags) - added to each object's state history
    // (+0x27C: 20 entries, the newest at count % 20).
    static int __cdecl HandleObjectUpdates(Net::MsgHandlerContext* context);

    // The server's side of the skaters' updates (outside this file's range, with them for the big-level positions,
    // TP-R6 - game/net/thugpro_compat.h): positions in half units (y in quarters), whole units in a big level.
    // 0x0056BE60 (0x23 on the server): a client's own skater's update (a 32-bit time stamp, then the fields as above)
    // into its state history; 4 (dropped) when it's older than the newest. From a player that isn't known or ready
    // (its connection's +0x60 bit 0), or a late / foreign packet: skipped, its length worked out from its fields.
    static int __cdecl HandleClientObjectUpdate(Net::MsgHandlerContext* context);
    // What the server sends one client (the server task 0x00569BB0): a bit stream and the client's record of what it
    // was last sent about each skater (+0x1C).
    struct RelayStream {
        Util::BitStream m_stream;
        uint32_t m_unknown18;
        void* m_sent;
    };
    // 0x005696C0: which of skater `index`'s fields (in `stateEntry`, its newest) differ from what `sentRecord` says
    // was sent - every field once 60 of the server app's frames have gone by since the last time (then counted again).
    static uint32_t __cdecl GetRelayFlags(void* sentRecord, void* stateEntry, uint8_t index);
    // 0x005698F0: skater `object`'s newest state written to `out` (its 16-bit time stamp, the 9 field bits, the
    // changed fields), and recorded as sent.
    static void __cdecl WriteObjectUpdate(Obj::CObject* object, RelayStream* out);
};

} // namespace Mdl
