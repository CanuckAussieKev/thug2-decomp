#pragma once

#include <cstdint>

#include "engine/memory/arena_alloc.h"

namespace online {

// NOT ORIGINAL (the class; its fields are the network manager's +0x158-+0x17F): the servers the server list found
// (HostListing, server_list.h) - those listed in the menu and those found but not shown yet - and the searches over
// them. Finding servers, showing them and emptying the lists stay with the hub (the server-list client, the menu's
// scripts). Owned by the hub, in its place there.
class ServerListing {
public:
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x00546010: no server chosen (+0x8B) / 0x00546070: the servers in the menu (+0x8A).
    void clear_chosen_servers();
    int num_servers_in_menu();
    // 0x00546400: whether a server named `name` (case ignored) at `ip` is listed or waiting.
    bool has_server(const char* name, uint32_t ip);
    // 0x005464D0: the listed server with id `id` (internet: its SBServer; LAN: itself), or null.
    void* fetch_host(uint32_t id);

    memory::ListNode& servers() { return servers_; }
    [[nodiscard]] const memory::ListNode& servers() const { return servers_; }
    memory::ListNode& waiting_servers() { return waiting_servers_; }
    [[nodiscard]] const memory::ListNode& waiting_servers() const { return waiting_servers_; }

private:
    memory::ListNode servers_{};         // +0x00 lists::Foremost<HostListing>: the listed servers (0x10 bytes, 0x14 there)
    memory::ListNode waiting_servers_{}; // +0x14 in the original: lists::Foremost<HostListing>, found, not shown yet
};

} // namespace online
