// NetLink::Manager: its constructor and destructor, the singleton, and making / destroying its apps. See
// net_manager.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, inet_ntoa: what the game uses
#include "game/net/net_manager.h"

#include <winsock2.h>

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_client.h"
#include "game/net/net_server.h"

namespace NetLink {

Manager* g_netManager;

namespace {

constexpr int kMaxMsgIds = 255;
constexpr uint8_t kMsgNoLength = 1; // (net_access_c.h)
constexpr uint8_t kMsgAck = 8;
constexpr uint8_t kMsgTimestamp = 9;
constexpr int kConnectionTypeNone = 1; // (1 and 3: no configured IP address; their meaning isn't known yet)
constexpr int kConnectionType3 = 3;
constexpr int kClientMaxConnections = 1;
// The classes' sizes (Server and Client here are views: their fields past App's aren't named yet).
constexpr uint32_t kServerSize = 0x2308;
constexpr uint32_t kClientSize = 0x1F18;

Manager*& Instance() { return g_netManager; }

uint32_t g_netManagerReferences;

uint32_t& RefTally() { return g_netManagerReferences; }

void SetupListSentinel(MemSys::ListNode* head)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kAppListHeadVtable;
}

// `node` at the end of the list.
void AppendLast(MemSys::ListNode* head, MemSys::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

void CopyString(char* out, const char* text) { std::memcpy(out, text, std::strlen(text) + 1); }

// (strncpy: zero-padded, unterminated if `text` is longer)
void CopyName(char* out, const char* text)
{
#pragma warning(suppress : 4996)
    std::strncpy(out, text, 0x40);
}

bool InitApp(App* app) { return reinterpret_cast<bool(__thiscall*)(App*)>(app->m_vtable[4])(app); }

} // namespace

// 0x004556E0
Manager* Manager::Construct()
{
    SetupListSentinel(&m_servers);
    SetupListSentinel(&m_clients);
    for (int id = 0; id < kMaxMsgIds; ++id)
        m_messageFlags[id] = 0;
    m_messageFlags[kMsgTimestamp] = kMsgNoLength;
    m_messageFlags[kMsgAck] = kMsgNoLength;
    CopyString(m_localIP, "<Unknown>");
    CopyString(m_routerAddr, "192.168.55.99");
    CopyString(m_subnetMask, "255.255.0.0");
    m_numApps = 0;
    m_connectionType = 0;
    m_useDHCP = false;
    m_isNetworked = false;
    m_flag2E6 = false;
    m_lastError = 0;
    m_modemState = 5;
    m_unknown2EC = 0;
    for (char* server : m_dnsServers)
        server[0] = '\0';
    m_dialupNumberIndex = 0;
    m_linkSpeed = 400000;
    return this;
}

// 0x004555A0
void Manager::DestroyBody()
{
    m_clients.DestroyBody();
    m_servers.DestroyBody();
}

// 0x00455270
Server* Manager::MakeServerApp(int id, const char* name, int maxClients, uint16_t port, int address,
                                        int flags)
{
    MemSys::Manager::Get()->EnterHeapScope(MemSys::Manager::Get()->m_networkHeap);
    if (m_connectionType != kConnectionType3 && m_connectionType != kConnectionTypeNone && m_useDHCP)
        address = static_cast<int>(inet_addr(m_localIP));
    void* memory = MemSys::New(kServerSize);
    Server* server = memory ? static_cast<Server*>(memory)->Construct(flags) : nullptr;
    server->m_supervisor = this;
    InitApp(server);
    server->m_unknown1EB4 = static_cast<uint32_t>(id);
    CopyName(server->m_name, name);
    server->m_maxConnections = maxClients;
    AppendLast(&m_servers, &server->m_list1414);
    if (server->IsLocal()) {
        server->m_connected = true;
    } else {
        server->Bind(static_cast<uint32_t>(address), port);
        if (flags & 1)
            server->m_connected = true;
    }
    AddAppTasks(server);
    in_addr ip;
    ip.s_addr = static_cast<ULONG>(address);
    DebugOut::Printf("Created new: %s server %p, Max Clients: %d, IP: %s, Port: %d", name, server, maxClients,
                inet_ntoa(ip), port);
    ++m_numApps;
    MemSys::Manager::Get()->LeaveHeapScope();
    return server;
}

// 0x004553F0
Client* Manager::MakeClientApp(int id, const char* name, uint16_t port, int address, int flags)
{
    MemSys::Manager::Get()->EnterHeapScope(MemSys::Manager::Get()->m_networkHeap);
    if (m_connectionType != kConnectionType3 && m_connectionType != kConnectionTypeNone && m_useDHCP)
        address = static_cast<int>(inet_addr(m_localIP));
    void* memory = MemSys::New(kClientSize);
    Client* client = memory ? static_cast<Client*>(memory)->Construct(flags) : nullptr;
    CopyName(client->m_name, name);
    client->m_supervisor = this;
    InitApp(client);
    client->m_unknown1EB4 = static_cast<uint32_t>(id);
    client->m_maxConnections = kClientMaxConnections;
    AppendLast(&m_clients, &client->m_list1414);
    if (!client->IsLocal())
        client->Bind(static_cast<uint32_t>(address), port);
    AddAppTasks(client);
    DebugOut::Printf("Created new: %s client %p on port %d\n", name, client, port);
    ++m_numApps;
    MemSys::Manager::Get()->LeaveHeapScope();
    return client;
}

// 0x00455550
void Manager::ReleaseApp(App* app)
{
    MemSys::ListNode& node = app->m_list1414;
    node.prev->next = node.next;
    node.next->prev = node.prev;
    node.next = &node;
    node.prev = &node;
    reinterpret_cast<void(__thiscall*)(App*)>(app->m_vtable[5])(app); // Deinit (through 0x00456B30)
    if (app) // (checked only now)
        reinterpret_cast<void*(__thiscall*)(App*, unsigned int)>(app->m_vtable[0])(app, 1);
    --m_numApps;
}

// 0x00455880
Manager* CreateManager(bool create)
{
    if (!Instance() && create) {
        if (void* memory = MemSys::AllocZeroed(sizeof(Manager), nullptr, 1)) {
            Instance() = static_cast<Manager*>(memory)->Construct();
            ++RefTally();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++RefTally();
    return Instance();
}

// 0x004558D0
void ReleaseManager()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (Manager* manager = Instance()) {
        manager->DestroyBody();
        MemSys::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    RefTally() = 0;
}

} // namespace Net
