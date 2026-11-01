#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/net/net_messages.h"
#include <gsl/pointers>

// The network manager (0x37C bytes; the instance at 0x006EFDD8, made and released by create_manager /
// release_manager): the apps (servers and clients) it made and the network settings online copies from the network
// preferences (0x00535040). Mostly a console's settings (DHCP, dial-up numbers, a modem): on the PC few of them
// reach a socket. Its getters and setters are 0x00454530 - 0x004549C0, 0x00454CB0 and 0x00454DE0
// (net_manager.cpp); its constructor, destructor and app making 0x00455270 - 0x004558D0 (net_manager_apps.cpp).

namespace net {

class HostEnd;
class Guest;
struct AppHost;

class Hub {
public:
    // connection_type_: what the connection is. Setting it sets the bandwidth (bytes a second).
    static constexpr int kConnectionModem = 1; // 4200: the dial-up numbers are used
    static constexpr int kConnection2 = 2;     // 400000
    static constexpr int kConnection3 = 3;     // 300000; no dial-up number
    static constexpr int kConnection4 = 4;     // 400000
    static constexpr int kNumDnsServers = 3;
    static constexpr int kNumDialupNumbers = 3;

    // The instance (0x006EFDD8; its reference count is 0x006EFDDC).
    static Hub* sole();

    // 0x004556E0: empty app lists, every message with a length field but acks (8) and timestamps (9), no IP
    // address yet ("<Unknown>"), 192.168.55.99 and a 255.255.0.0 mask, a 400000 bandwidth.
    Hub();
    // 0x004555A0: the destructor body (the app lists' heads).
    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;
    // 0x00455270: a new server (in the network heap), named and told its manager, initialised and (unless it's
    // local) bound to `port` on `address` - the local IP when DHCP is set and the connection type isn't 1 or 3;
    // flags bit 0: its socket counts as connected. Null memory isn't checked (a crash).
    HostEnd* make_server_app(int id, const char* name, int max_clients, uint16_t port, int address, int flags);
    // 0x004553F0: the same for a client (one connection: the server).
    Guest* make_client_app(int id, const char* name, uint16_t port, int address, int flags);
    // 0x00455550: `app` out of its list, deinitialised (virtual slot 5) and deleted.
    void release_app(AppHost* app);

    // 0x00454580: (nothing to do on the PC) online. True.
    bool connect_to_internet();
    // 0x00454590: offline. True.
    bool disconnect_from_internet();
    // 0x004545A0
    int get_dialup_speed() { return dialup_bps_; }
    // 0x004545B0: the bandwidth allowed over all connections (bytes a second).
    int fetch_throughput() { return link_speed_; }
    // 0x004545C0
    bool is_online() { return is_networked_; }
    // 0x004545D0
    void set_dialup_number(int index, const char* number);
    // 0x00454600
    void set_dialup_user_name(const char* name);
    // 0x00454620
    void set_dialup_password(const char* password);
    // 0x00454640 / 0x004546A0 / 0x00454710: each copied only if it differs.
    void set_router_addr(const char* ip);
    void set_address_mask(const char* ip);
    void set_own_address(const char* ip);
    // 0x00454700: this address in place of the local IP's (0 = the local IP's).
    void set_local_address(uint32_t address) { local_address_ = address; }
    // 0x00454770: also the bandwidth (see the connection types).
    void set_net_link_kind(int type);
    // 0x004547D0
    void set_adapter_kind(int type);
    // 0x004547F0
    void set_auto_address(bool use_dhcp);
    // 0x00454810 (the network preference dialup_authentication - checksum 0xE5ADE642 - being boolean_true)
    void set_flag_2e_6(bool on) { flag_2e_6_ = on; }
    // 0x00454820: copied only if it differs.
    void set_name_resolver(int index, const char* ip);
    // 0x00454890 / 0x004548C0
    void set_machine_label(const char* name);
    void set_network_domain(const char* name);
    // 0x004548F0: the local address (set_local_address's, or the local IP's).
    uint32_t get_local_address();
    // 0x00454910: this machine's address as text ("localhost"'s host name looked up), "<Unknown>" if even
    // localhost can't be. (Doesn't use the manager.)
    const char* fetch_own_address();
    // 0x00454960
    int get_net_link_kind() { return connection_type_; }
    // 0x00454970 (shown by the TestNetSetup script command: 2-7 are set-up errors)
    int get_last_error() { return last_error_; }
    // 0x00454980 / 0x00454990: a message id's flags.
    char get_msg_options(uint8_t msg_id) { return static_cast<char>(message_flags_[msg_id]); }
    void set_msg_options(uint8_t msg_id, char flags) { message_flags_[msg_id] = static_cast<uint8_t>(flags); }
    // 0x004549A0 / 0x004549B0: the online state machine's state (0x00533810: 0-8; 5 at first).
    int get_dialup_status() { return dialup_phase_; }
    void set_dialup_status(int state) { dialup_phase_ = state; }
    // 0x004549C0: (a console's network drivers, set up with the viewer's address - the LoadNetworkDrivers
    // script command: nothing to load on the PC) true.
    bool load_network_drivers(const char* ip, const char* subnet_mask, const char* gateway);
    // 0x00454CB0: the app's receive, send, process and metrics tasks into the main loop's logic tasks.
    void add_app_tasks(AppHost* app);
    // 0x00454DE0: the dial-up number in use ("" for connection type 3).
    const char* get_dialup_number();

    memory::ListNode hosts_{};       // +0x000 lists::Foremost<App>
    memory::ListNode clients_{};     // +0x014
    uint8_t message_flags_[0x100]{}; // +0x028 per message id (bit 0: sent without its length; the
                                     //        constructor: 8 and 9 get 1, 0-254 0)
    int connection_type_;          // +0x128
    uint32_t unknown12c_{};
    int device_type_{};                            // +0x130
    char local_ip_[0x10]{};                        // +0x134 ("<Unknown>" at first)
    uint32_t local_address_{};                     // +0x144
    char resolver_ips_[kNumDnsServers][0x10]{};    // +0x148
    char router_addr_[0x10]{};                     // +0x178 ("192.168.55.99")
    char subnet_mask_[0x10]{};                     // +0x188 ("255.255.0.0")
    int dialup_number_index_;                      // +0x198 which dial-up number is in use
    char dialup_numbers_[kNumDialupNumbers][0x20]{}; // +0x19C
    char dialup_user_name_[0x50]{};                  // +0x1FC
    char dialup_password_[0x50]{};                   // +0x24C
    char host_name_[0x20]{};                         // +0x29C
    char domain_name_[0x20]{};                       // +0x2BC
    int num_apps_;                                 // +0x2DC apps made
    int last_error_;                               // +0x2E0
    bool is_networked_;                            // +0x2E4
    bool use_dhcp_;                                // +0x2E5 (the apps are then bound to local_ip_)
    bool flag_2e_6_;                               // +0x2E6
    int dialup_phase_;                             // +0x2E8
    int unknown2ec_;                               // +0x2EC (0 from the constructor)
    uint8_t unknown2f0_[0x374 - 0x2F0]{};
    int dialup_bps_{}; // +0x374
    int link_speed_; // +0x378 (400000 at first)
};

// 0x00455880: the manager singleton, made (zeroed memory) if `create` and there's none; one more reference.
Hub* create_manager(bool create);
// 0x004558D0: one reference less; the last deletes it.
void release_manager();

extern gsl::owner<Hub*> g_net_core;

} // namespace net
