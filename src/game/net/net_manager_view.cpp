// Net::Manager: its constructor and destructor, the singleton, and making / destroying its apps. See
// net_manager_view.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, inet_ntoa: what the game uses
#include "game/net/net_manager_view.h"

#include <winsock2.h>

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_client.h"
#include "game/net/net_server.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Net {

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

ManagerView*& Instance() { return GameVariable<ManagerView*>(0x006EFDD8); }
uint32_t& References() { return GameVariable<uint32_t>(0x006EFDDC); }

void InitHead(Mem::ListNode* head)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kAppListHeadVtable;
}

// `node` at the end of the list.
void AddToTail(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// -> 0x00454CB0 (wt/netD): the app's four tasks added to the main loop.
void AddAppTasks(ManagerView* manager, App* app)
{
    GameFunction<void(__thiscall*)(ManagerView*, App*)>(0x00454CB0)(manager, app);
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
ManagerView* ManagerView::Construct()
{
    InitHead(&m_servers);
    InitHead(&m_clients);
    for (int id = 0; id < kMaxMsgIds; ++id)
        m_msgFlags[id] = 0;
    m_msgFlags[kMsgTimestamp] = kMsgNoLength;
    m_msgFlags[kMsgAck] = kMsgNoLength;
    CopyString(m_localIp, "<Unknown>");
    CopyString(m_gateway, "192.168.55.99");
    CopyString(m_subnetMask, "255.255.0.0");
    m_numApps = 0;
    m_connectionType = 0;
    m_useLocalIp = false;
    m_unknown2E4 = false;
    m_unknown2E6 = false;
    m_unknown2E0 = 0;
    m_unknown2E8 = 5;
    m_unknown2EC = 0;
    for (char* server : m_dnsServers)
        server[0] = '\0';
    m_unknown198 = 0;
    m_totalBandwidth = 400000;
    return this;
}

// 0x004555A0
void ManagerView::DestroyBody()
{
    m_clients.DestroyBody();
    m_servers.DestroyBody();
}

// 0x00455270
Server* ManagerView::CreateNewAppServer(int id, const char* name, int maxClients, uint16_t port, int address,
                                        int flags)
{
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_networkHeap);
    if (m_connectionType != kConnectionType3 && m_connectionType != kConnectionTypeNone && m_useLocalIp)
        address = static_cast<int>(inet_addr(m_localIp));
    void* memory = Mem::New(kServerSize);
    Server* server = memory ? static_cast<Server*>(memory)->Construct(flags) : nullptr;
    server->m_manager = this;
    InitApp(server);
    server->m_unknown1EB4 = static_cast<uint32_t>(id);
    CopyName(server->m_name, name);
    server->m_maxConnections = maxClients;
    AddToTail(&m_servers, &server->m_list1414);
    if (server->IsLocal()) {
        server->m_connected = true;
    } else {
        server->Bind(static_cast<uint32_t>(address), port);
        if (flags & 1)
            server->m_connected = true;
    }
    AddAppTasks(this, server);
    in_addr ip;
    ip.s_addr = static_cast<ULONG>(address);
    Dbg::Printf("Created new: %s server %p, Max Clients: %d, IP: %s, Port: %d", name, server, maxClients,
                inet_ntoa(ip), port);
    ++m_numApps;
    Mem::Manager::Get()->PopContext();
    return server;
}

// 0x004553F0
Client* ManagerView::CreateNewAppClient(int id, const char* name, uint16_t port, int address, int flags)
{
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_networkHeap);
    if (m_connectionType != kConnectionType3 && m_connectionType != kConnectionTypeNone && m_useLocalIp)
        address = static_cast<int>(inet_addr(m_localIp));
    void* memory = Mem::New(kClientSize);
    Client* client = memory ? static_cast<Client*>(memory)->Construct(flags) : nullptr;
    CopyName(client->m_name, name);
    client->m_manager = this;
    InitApp(client);
    client->m_unknown1EB4 = static_cast<uint32_t>(id);
    client->m_maxConnections = kClientMaxConnections;
    AddToTail(&m_clients, &client->m_list1414);
    if (!client->IsLocal())
        client->Bind(static_cast<uint32_t>(address), port);
    AddAppTasks(this, client);
    Dbg::Printf("Created new: %s client %p on port %d\n", name, client, port);
    ++m_numApps;
    Mem::Manager::Get()->PopContext();
    return client;
}

// 0x00455550
void ManagerView::DestroyApp(App* app)
{
    Mem::ListNode& node = app->m_list1414;
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
ManagerView* CreateManager(bool create)
{
    if (!Instance() && create) {
        if (void* memory = Mem::Calloc(sizeof(ManagerView), nullptr, 1)) {
            Instance() = static_cast<ManagerView*>(memory)->Construct();
            ++References();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++References();
    return Instance();
}

// 0x004558D0
void ReleaseManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (ManagerView* manager = Instance()) {
        manager->DestroyBody();
        Mem::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    References() = 0;
}

} // namespace Net

REPLACE(0x004556E0, &Net::ManagerView::Construct);
REPLACE(0x004555A0, &Net::ManagerView::DestroyBody);
REPLACE(0x00455270, &Net::ManagerView::CreateNewAppServer);
REPLACE(0x004553F0, &Net::ManagerView::CreateNewAppClient);
REPLACE(0x00455550, &Net::ManagerView::DestroyApp);
REPLACE(0x00455880, Net::CreateManager);
REPLACE(0x004558D0, Net::ReleaseManager);
