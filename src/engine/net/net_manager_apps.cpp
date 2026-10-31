// NetLink::Hub: its constructor and destructor, the singleton, and making / destroying its apps. See
// net_manager.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, inet_ntoa: what the game uses
#include "engine/net/net_manager.h"

#include <winsock2.h>

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/net_client.h"
#include "engine/net/net_server.h"

namespace net {

Hub* g_net_core;

namespace {

constexpr int kMsgIdSpace = 255;
constexpr uint8_t kMsgNoLength = 1; // (net_access_c.h)
constexpr uint8_t kMsgAck = 8;
constexpr uint8_t kMsgClockStamp = 9;
constexpr int kConnectionTypeNone = 1; // (1 and 3: no configured IP address; their meaning isn't known yet)
constexpr int kConnectionType3 = 3;
constexpr int kClientMaxConnections = 1;
// The classes' sizes (Server and Client here are views: their fields past App's aren't named yet).
constexpr uint32_t kServerSize = 0x2308;
constexpr uint32_t kClientSize = 0x1F18;

Hub*& sole() { return g_net_core; }

uint32_t g_net_manager_references;

uint32_t& ref_tally() { return g_net_manager_references; }

void setup_list_sentinel(memory::ListNode* head)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kAppListHeadVtable;
}

// `node` at the end of the list.
void append_last(memory::ListNode* head, memory::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

void copy_string(char* out, const char* text) { std::memcpy(out, text, std::strlen(text) + 1); }

// (strncpy: zero-padded, unterminated if `text` is longer)
void copy_name(char* out, const char* text)
{
#pragma warning(suppress : 4996)
    std::strncpy(out, text, 0x40);
}

bool init_app(AppHost* app) { return reinterpret_cast<bool(__thiscall*)(AppHost*)>(app->vtable_[4])(app); }

} // namespace

// 0x004556E0
Hub* Hub::make_new()
{
    setup_list_sentinel(&hosts_);
    setup_list_sentinel(&clients_);
    for (int id = 0; id < kMsgIdSpace; ++id)
        message_flags_[id] = 0;
    message_flags_[kMsgClockStamp] = kMsgNoLength;
    message_flags_[kMsgAck] = kMsgNoLength;
    copy_string(local_ip_, "<Unknown>");
    copy_string(router_addr_, "192.168.55.99");
    copy_string(subnet_mask_, "255.255.0.0");
    num_apps_ = 0;
    connection_type_ = 0;
    use_dhcp_ = false;
    is_networked_ = false;
    flag_2e_6_ = false;
    last_error_ = 0;
    dialup_phase_ = 5;
    unknown2ec_ = 0;
    for (char* server : resolver_ips_)
        server[0] = '\0';
    dialup_number_index_ = 0;
    link_speed_ = 400000;
    return this;
}

// 0x004555A0
void Hub::destroy_body()
{
    clients_.destroy_body();
    hosts_.destroy_body();
}

// 0x00455270
HostEnd* Hub::make_server_app(int id, const char* name, int max_clients, uint16_t port, int address, int flags)
{
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->network_heap_);
    if (connection_type_ != kConnectionType3 && connection_type_ != kConnectionTypeNone && use_dhcp_)
        address = static_cast<int>(inet_addr(local_ip_));
    void* memory = memory::alloc(kServerSize);
    HostEnd* server = memory ? static_cast<HostEnd*>(memory)->make_new(flags) : nullptr;
    server->supervisor_ = this;
    init_app(server);
    server->unknown1eb4_ = static_cast<uint32_t>(id);
    copy_name(server->label_, name);
    server->peer_cap_ = max_clients;
    append_last(&hosts_, &server->list1414_);
    if (server->is_host_local()) {
        server->linked_up_ = true;
    } else {
        server->get_bind(static_cast<uint32_t>(address), port);
        if (flags & 1)
            server->linked_up_ = true;
    }
    add_app_tasks(server);
    in_addr ip;
    ip.s_addr = static_cast<ULONG>(address);
    debug::printf("Created new: %s server %p, Max Clients: %d, IP: %s, Port: %d", name, server, max_clients,
                  inet_ntoa(ip), port);
    ++num_apps_;
    memory::Hub::get_pull()->leave_heap_scope();
    return server;
}

// 0x004553F0
Guest* Hub::make_client_app(int id, const char* name, uint16_t port, int address, int flags)
{
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->network_heap_);
    if (connection_type_ != kConnectionType3 && connection_type_ != kConnectionTypeNone && use_dhcp_)
        address = static_cast<int>(inet_addr(local_ip_));
    void* memory = memory::alloc(kClientSize);
    Guest* client = memory ? static_cast<Guest*>(memory)->make_new(flags) : nullptr;
    copy_name(client->label_, name);
    client->supervisor_ = this;
    init_app(client);
    client->unknown1eb4_ = static_cast<uint32_t>(id);
    client->peer_cap_ = kClientMaxConnections;
    append_last(&clients_, &client->list1414_);
    if (!client->is_host_local())
        client->get_bind(static_cast<uint32_t>(address), port);
    add_app_tasks(client);
    debug::printf("Created new: %s client %p on port %d\n", name, client, port);
    ++num_apps_;
    memory::Hub::get_pull()->leave_heap_scope();
    return client;
}

// 0x00455550
void Hub::release_app(AppHost* app)
{
    memory::ListNode& node = app->list1414_;
    node.prev->next = node.next;
    node.next->prev = node.prev;
    node.next = &node;
    node.prev = &node;
    reinterpret_cast<void(__thiscall*)(AppHost*)>(app->vtable_[5])(app); // Deinit (through 0x00456B30)
    if (app)                                                             // (checked only now)
        reinterpret_cast<void*(__thiscall*)(AppHost*, unsigned int)>(app->vtable_[0])(app, 1);
    --num_apps_;
}

// 0x00455880
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            sole() = static_cast<Hub*>(memory)->make_new();
            ++ref_tally();
            return sole();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole();
}

// 0x004558D0
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (Hub* manager = sole()) {
        manager->destroy_body();
        memory::Hub::get_pull()->dealloc(manager);
    }
    sole() = nullptr;
    ref_tally() = 0;
}

} // namespace net
