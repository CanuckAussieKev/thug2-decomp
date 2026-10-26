#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/net/net_messages.h"

// Neversoft's Net::Manager (0x37C bytes; the instance at 0x006EFDD8, made and released by CreateManager /
// ReleaseManager): the apps (servers and clients) it made and the network settings GameNet copies from the network
// preferences (0x00535040). Mostly a console's settings (DHCP, dial-up numbers, a modem): on the PC few of them
// reach a socket. Its getters and setters are 0x00454530 - 0x004549C0, 0x00454CB0 and 0x00454DE0
// (net_manager.cpp); its constructor, destructor and app making 0x00455270 - 0x004558D0 (net_manager_apps.cpp).

namespace Net {

class Server;
class Client;
struct App;

class Manager {
public:
    // m_connectionType: what the connection is. Setting it sets the bandwidth (bytes a second).
    static constexpr int kConnectionModem = 1;   // 4200: the dial-up numbers are used
    static constexpr int kConnection2 = 2;       // 400000
    static constexpr int kConnection3 = 3;       // 300000; no dial-up number
    static constexpr int kConnection4 = 4;       // 400000
    static constexpr int kNumDNSServers = 3;
    static constexpr int kNumDialupNumbers = 3;

    // The instance (0x006EFDD8; its reference count is 0x006EFDDC).
    static Manager* Instance();

    // 0x004556E0: empty app lists, every message with a length field but acks (8) and timestamps (9), no IP
    // address yet ("<Unknown>"), 192.168.55.99 and a 255.255.0.0 mask, a 400000 bandwidth.
    Manager* Construct();
    // 0x004555A0: the destructor body (the app lists' heads).
    void DestroyBody();
    // 0x00455270: a new server (in the network heap), named and told its manager, initialised and (unless it's
    // local) bound to `port` on `address` - the local IP when DHCP is set and the connection type isn't 1 or 3;
    // flags bit 0: its socket counts as connected. Null memory isn't checked (a crash).
    Server* CreateNewAppServer(int id, const char* name, int maxClients, uint16_t port, int address, int flags);
    // 0x004553F0: the same for a client (one connection: the server).
    Client* CreateNewAppClient(int id, const char* name, uint16_t port, int address, int flags);
    // 0x00455550: `app` out of its list, deinitialised (virtual slot 5) and deleted.
    void DestroyApp(App* app);

    // 0x00454580: (nothing to do on the PC) online. True.
    bool ConnectToInternet();
    // 0x00454590: offline. True.
    bool DisconnectFromInternet();
    // 0x004545A0
    int GetModemBaudRate() { return m_modemBaudRate; }
    // 0x004545B0: the bandwidth allowed over all connections (bytes a second).
    int GetBandwidth() { return m_bandwidth; }
    // 0x004545C0
    bool IsOnline() { return m_online; }
    // 0x004545D0
    void SetDialupNumber(int index, const char* number);
    // 0x00454600
    void SetDialupUserName(const char* name);
    // 0x00454620
    void SetDialupPassword(const char* password);
    // 0x00454640 / 0x004546A0 / 0x00454710: each copied only if it differs.
    void SetGateway(const char* ip);
    void SetSubnetMask(const char* ip);
    void SetLocalIP(const char* ip);
    // 0x00454700: this address in place of the local IP's (0 = the local IP's).
    void SetLocalAddress(uint32_t address) { m_localAddress = address; }
    // 0x00454770: also the bandwidth (see the connection types).
    void SetConnectionType(int type);
    // 0x004547D0
    void SetDeviceType(int type);
    // 0x004547F0
    void SetDHCP(bool useDHCP);
    // 0x00454810 (the network preference dialup_authentication - checksum 0xE5ADE642 - being boolean_true)
    void SetFlag2E6(bool on) { m_flag2E6 = on; }
    // 0x00454820: copied only if it differs.
    void SetDNSServer(int index, const char* ip);
    // 0x00454890 / 0x004548C0
    void SetHostName(const char* name);
    void SetDomainName(const char* name);
    // 0x004548F0: the local address (SetLocalAddress's, or the local IP's).
    uint32_t GetLocalAddress();
    // 0x00454910: this machine's address as text ("localhost"'s host name looked up), "<Unknown>" if even
    // localhost can't be. (Doesn't use the manager.)
    const char* GetLocalIP();
    // 0x00454960
    int GetConnectionType() { return m_connectionType; }
    // 0x00454970 (shown by the TestNetSetup script command: 2-7 are set-up errors)
    int GetLastError() { return m_lastError; }
    // 0x00454980 / 0x00454990: a message id's flags.
    char GetMessageFlags(uint8_t msgId) { return static_cast<char>(m_messageFlags[msgId]); }
    void SetMessageFlags(uint8_t msgId, char flags) { m_messageFlags[msgId] = static_cast<uint8_t>(flags); }
    // 0x004549A0 / 0x004549B0: the online state machine's state (0x00533810: 0-8; 5 at first).
    int GetModemState() { return m_modemState; }
    void SetModemState(int state) { m_modemState = state; }
    // 0x004549C0: (a console's network drivers, set up with the viewer's address - the LoadNetworkDrivers
    // script command: nothing to load on the PC) true.
    bool LoadNetworkDrivers(const char* ip, const char* subnetMask, const char* gateway);
    // 0x00454CB0: the app's receive, send, process and metrics tasks into the main loop's logic tasks.
    void AddAppTasks(App* app);
    // 0x00454DE0: the dial-up number in use ("" for connection type 3).
    const char* GetDialupNumber();

    AppNode m_servers;                    // +0x000 Lst::Head<App> (vtable 0x00647544)
    AppNode m_clients;                    // +0x014
    uint8_t m_messageFlags[0x100];        // +0x028 per message id (bit 0: sent without its length; the
                                          //        constructor: 8 and 9 get 1, 0-254 0)
    int m_connectionType;                 // +0x128
    uint32_t m_unknown12C;
    int m_deviceType;                     // +0x130
    char m_localIP[0x10];                 // +0x134 ("<Unknown>" at first)
    uint32_t m_localAddress;              // +0x144
    char m_dnsServers[kNumDNSServers][0x10]; // +0x148
    char m_gateway[0x10];                 // +0x178 ("192.168.55.99")
    char m_subnetMask[0x10];              // +0x188 ("255.255.0.0")
    int m_dialupNumberIndex;              // +0x198 which dial-up number is in use
    char m_dialupNumbers[kNumDialupNumbers][0x20]; // +0x19C
    char m_dialupUserName[0x50];          // +0x1FC
    char m_dialupPassword[0x50];          // +0x24C
    char m_hostName[0x20];                // +0x29C
    char m_domainName[0x20];              // +0x2BC
    int m_numApps;                        // +0x2DC apps made
    int m_lastError;                      // +0x2E0
    bool m_online;                        // +0x2E4
    bool m_useDHCP;                       // +0x2E5 (the apps are then bound to m_localIP)
    bool m_flag2E6;                       // +0x2E6
    uint8_t m_pad2E7;
    int m_modemState;                     // +0x2E8
    int m_unknown2EC;                     // +0x2EC (0 from the constructor)
    uint8_t m_unknown2F0[0x374 - 0x2F0];
    int m_modemBaudRate;                  // +0x374
    int m_bandwidth;                      // +0x378 (400000 at first)
};
static_assert(offsetof(Manager, m_clients) == 0x14 && offsetof(Manager, m_messageFlags) == 0x28 &&
                  offsetof(Manager, m_connectionType) == 0x128 && offsetof(Manager, m_deviceType) == 0x130 &&
                  offsetof(Manager, m_localIP) == 0x134 && offsetof(Manager, m_localAddress) == 0x144 &&
                  offsetof(Manager, m_dnsServers) == 0x148 && offsetof(Manager, m_gateway) == 0x178 &&
                  offsetof(Manager, m_subnetMask) == 0x188 && offsetof(Manager, m_dialupNumberIndex) == 0x198 &&
                  offsetof(Manager, m_dialupNumbers) == 0x19C && offsetof(Manager, m_dialupUserName) == 0x1FC &&
                  offsetof(Manager, m_dialupPassword) == 0x24C && offsetof(Manager, m_hostName) == 0x29C &&
                  offsetof(Manager, m_domainName) == 0x2BC && offsetof(Manager, m_numApps) == 0x2DC &&
                  offsetof(Manager, m_lastError) == 0x2E0 && offsetof(Manager, m_online) == 0x2E4 &&
                  offsetof(Manager, m_useDHCP) == 0x2E5 && offsetof(Manager, m_flag2E6) == 0x2E6 &&
                  offsetof(Manager, m_modemState) == 0x2E8 && offsetof(Manager, m_modemBaudRate) == 0x374 &&
                  offsetof(Manager, m_bandwidth) == 0x378 && sizeof(Manager) == 0x37C,
              "layout must match the original");

// 0x00455880: the manager singleton, made (zeroed memory) if `create` and there's none; one more reference.
Manager* CreateManager(bool create);
// 0x004558D0: one reference less; the last deletes it.
void ReleaseManager();

} // namespace Net
