#pragma once

// The server's reporting to GameSpy's master server through the query & reporting SDK (qr2): its set-up and the
// callbacks qr2 calls to answer queries (gamenet_qr2.cpp). The network manager is the singleton 0x007CCE14; qr2's
// userdata is unused.

namespace GameNet {

// 0x0053B9C0: a server key: hostname (the server_name preference's ui_string), gamename (the online identity's game
// name - the original's "thps6pc"), gamever "1.00", hostport 5153, mapname (the level select's name, else the level's
// own name noted by StartQueryReporting), gametype (its name), numplayers, numteams, maxplayers, gamemode
// (openplaying / openwaiting / closedwaiting by 0x007CCE1E / 0x007CCE1D), password (1 if one is set), groupid
// (0x007CC264); anything else: "".
void __cdecl ServerKeyCallback(int key, void* buffer, void* userData);
// 0x0053BBC0: a player key of player `index`: player_ (its name), ping_ (its connection's average latency, 0 for a
// local one), team_; "" for anything else, an index past the players - and, the original's own bug, for every player
// but the first.
void __cdecl PlayerKeyCallback(int key, int index, void* buffer, void* userData);
// 0x0053BD90: the number of players (type 1) or teams (type 2); 0 otherwise.
int __cdecl CountCallback(int type, void* userData);
// 0x0053BDC0: qr2 started on port 5155 (public, no NAT negotiation) with the online identity's game name and the
// stored secret key, once (0x007CCE1C); unless `keepMapName`, the level's name noted first (0x0067F0A0).
void __cdecl StartQueryReporting(bool keepMapName);

} // namespace GameNet
