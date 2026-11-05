#pragma once

#include "engine/memory/mem.h"

namespace online {

class RoomManager;
class VaultMan;
class FriendsManager;
class RankingsManager;

// NOT ORIGINAL (the class; its fields are the network manager's +0x10-+0x1C): the session's links to the online
// services - the GameSpy lobby (RoomManager, gamenet_lobby.h), the content vault and its HTTP transfers (VaultMan),
// the GameSpy presence and buddies (FriendsManager, buddy_man.h) and the player's rating and stats
// (RankingsManager, stats_man.h): each made from the current heap with the hub and destroyed with it. Owned by the
// hub, in its place there.
class ServiceManagers {
public:
    // The four made, in the order of the hub's constructor (0x0053EE90): the lobby, the vault, the presence, the stats.
    void make_all();
    // The four destroyed, in that order (the hub's destructor, 0x00536D70) - each while the hub still points at it.
    void destroy_all();
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    memory::HeapUnique<RoomManager>& lobby_service() { return lobby_service_; }
    [[nodiscard]] const memory::HeapUnique<RoomManager>& lobby_service() const { return lobby_service_; }
    memory::HeapUnique<VaultMan>& vault_man() { return vault_man_; }
    [[nodiscard]] const memory::HeapUnique<VaultMan>& vault_man() const { return vault_man_; }
    memory::HeapUnique<FriendsManager>& presence_service() { return presence_service_; }
    [[nodiscard]] const memory::HeapUnique<FriendsManager>& presence_service() const { return presence_service_; }
    memory::HeapUnique<RankingsManager>& rating_service() { return rating_service_; }
    [[nodiscard]] const memory::HeapUnique<RankingsManager>& rating_service() const { return rating_service_; }

private:
    memory::HeapUnique<RoomManager> lobby_service_;       // +0x00 the GameSpy lobby
    memory::HeapUnique<VaultMan> vault_man_;              // +0x04 the content vault and HTTP transfers
    memory::HeapUnique<FriendsManager> presence_service_; // +0x08 the GameSpy presence
    memory::HeapUnique<RankingsManager> rating_service_;  // +0x0C the player's rating and stats
};

} // namespace online
