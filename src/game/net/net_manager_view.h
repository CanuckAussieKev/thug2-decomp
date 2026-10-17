#pragma once

// Neversoft's Net::Manager (0x37C bytes, a singleton at 0x006EFDD8): the network settings and the apps (servers
// and clients) it made. Its getters and per-message flags functions are 0x004544E0-0x00454FA0 (wt/netD), so
// ManagerView here is a view of the fields this range uses, at their offsets, for the merger to fold into the
// real class.

#include <cstddef>
#include <cstdint>

#include "game/net/net_messages.h"

namespace Net {

class Server;
class Client;
struct App;

class ManagerView {
public:
    // 0x004556E0: empty app lists, every message with a length field but acks (8) and timestamps (9), no IP
    // address yet ("<Unknown>"), 192.168.55.99 and a 255.255.0.0 mask, a 400000 bandwidth.
    ManagerView* Construct();
    // 0x004555A0: the destructor body (the app lists' heads).
    void DestroyBody();

    // 0x00455270: a new server (in the network heap), named and told its manager, initialised and (unless it's
    // local) bound to `port` on `address` - the configured IP address when one is set and the connection type
    // isn't 1 or 3; flags bit 0: its socket counts as connected. Null memory isn't checked (a crash).
    Server* CreateNewAppServer(int id, const char* name, int maxClients, uint16_t port, int address, int flags);
    // 0x004553F0: the same for a client (one connection: the server).
    Client* CreateNewAppClient(int id, const char* name, uint16_t port, int address, int flags);
    // 0x00455550: `app` out of its list, deinitialised (virtual slot 5) and deleted.
    void DestroyApp(App* app);

    AppNode m_servers;            // +0x000 Lst::Head<App> (vtable 0x00647544)
    AppNode m_clients;            // +0x014
    uint8_t m_msgFlags[0x100];    // +0x028 per message id 0-254 (bit 0: sent without its length)
    int m_connectionType;         // +0x128 (1 and 3: the configured IP address isn't used)
    uint32_t m_unknown12C[2];
    char m_localIp[0x14];         // +0x134
    char m_dnsServers[3][0x10];   // +0x148
    char m_gateway[0x10];         // +0x178
    char m_subnetMask[0x10];      // +0x188
    uint32_t m_unknown198;
    uint8_t m_unknown19C[0x2DC - 0x19C];
    int m_numApps;                // +0x2DC
    uint32_t m_unknown2E0;
    bool m_unknown2E4;
    bool m_useLocalIp;            // +0x2E5 the apps are bound to m_localIp
    bool m_unknown2E6;
    uint8_t m_pad2E7;
    uint32_t m_unknown2E8;        // (5)
    uint32_t m_unknown2EC;
    uint8_t m_unknown2F0[0x378 - 0x2F0];
    int m_totalBandwidth;         // +0x378 (0x004545B0 reads it)
};
static_assert(offsetof(ManagerView, m_clients) == 0x14 && offsetof(ManagerView, m_msgFlags) == 0x28 &&
                  offsetof(ManagerView, m_connectionType) == 0x128 && offsetof(ManagerView, m_localIp) == 0x134 &&
                  offsetof(ManagerView, m_dnsServers) == 0x148 && offsetof(ManagerView, m_gateway) == 0x178 &&
                  offsetof(ManagerView, m_subnetMask) == 0x188 && offsetof(ManagerView, m_unknown198) == 0x198 &&
                  offsetof(ManagerView, m_numApps) == 0x2DC && offsetof(ManagerView, m_useLocalIp) == 0x2E5 &&
                  offsetof(ManagerView, m_unknown2E8) == 0x2E8 && offsetof(ManagerView, m_totalBandwidth) == 0x378 &&
                  sizeof(ManagerView) == 0x37C,
              "layout must match the original");

// 0x00455880: the manager singleton, made (zeroed memory) if `create` and there's none; one more reference.
ManagerView* CreateManager(bool create);
// 0x004558D0: one reference less; the last deletes it.
void ReleaseManager();

} // namespace Net
