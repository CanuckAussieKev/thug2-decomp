// NetLink::Hub: its constructor and destructor, the singleton, and making / destroying its apps. See
// net_manager.h.

#include "engine/net/net_manager.h"

#include <winsock2.h>

#include <array>
#include <cstdint>
#include <cstring>
#include "wire/message_ids.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/net_client.h"
#include "engine/net/net_server.h"
#include <gsl/pointers>
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/net/shared_state.h"
#include "engine/net/net_access_c.h"

namespace net {

NetManagerAppsShared& net_manager_apps_shared()
{
    static core::NoDestroy<NetManagerAppsShared> state;
    return state.get();
}

Hub* net_core() { return net_manager_apps_shared().net_core().get(); }

namespace {
constexpr size_t kNameBytes = 0x40;

constexpr int kMsgIdSpace = 255;
constexpr int kConnectionTypeNone = 1; // (1 and 3: no configured IP address; their meaning isn't known yet)
constexpr int kConnectionType3 = 3;
constexpr int kClientMaxConnections = 1;
// The classes' sizes (Server and Client here are views: their fields past App's aren't named yet).

memory::HeapUnique<Hub>& sole() { return net_manager_apps_shared().net_core(); }

uint32_t& ref_tally()
{
    static uint32_t net_manager_references{};
    return net_manager_references;
}

void setup_list_sentinel(memory::ListNode* head) { head->make_head(); }

// `node` at the end of the list.
void append_last(memory::ListNode* head, memory::ListNode* node)
{
    node->set_prev(head->prev());
    node->set_next(head);
    head->prev()->set_next(node);
    head->set_prev(node);
}

void copy_string(char* out, const char* text) { std::memcpy(out, text, std::strlen(text) + 1); }

// (strncpy: zero-padded, unterminated if `text` is longer)
void copy_name(char* out, const char* text)
{
    THUG2_MSVC_WARNING(suppress : 4996)
    std::strncpy(out, text, kNameBytes);
}

} // namespace

// 0x004556E0
Hub::Hub()
{
    setup_list_sentinel(&hosts_);
    setup_list_sentinel(&clients_);
    for (int id = 0; id < kMsgIdSpace; ++id)
        core::at(message_flags_, id) = 0;
    core::at(message_flags_, wire::MessageId::kClockStamp) = kMsgNoLength;
    core::at(message_flags_, wire::MessageId::kAck) = kMsgNoLength;
    copy_string(local_ip_.data(), "<Unknown>");
    copy_string(router_addr_.data(), "192.168.55.99");
    copy_string(subnet_mask_.data(), "255.255.0.0");
    num_apps_ = 0;
    connection_type_ = 0;
    use_dhcp_ = false;
    is_networked_ = false;
    flag_2e_6_ = false;
    last_error_ = 0;
    dialup_phase_ = kDialupStatusDisconnected;
    unknown2ec_ = 0;
    for (std::array<char, kResolverIpBytes>& server : resolver_ips_)
        core::at(server, 0) = '\0';
    dialup_number_index_ = 0;
    link_speed_ = kLinkSpeedBroadband;
}

// 0x004555A0
Hub::~Hub()
{
    clients_.uncouple();
    hosts_.uncouple();
}

// 0x00455270
gsl::owner<HostEnd*> Hub::make_server_app(int id, const char* name, int max_clients, uint16_t port, AppBinding binding)
{
    auto address = binding.address;
    const auto flags = binding.flags;
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->network_heap());
    if (connection_type_ != kConnectionType3 && connection_type_ != kConnectionTypeNone && use_dhcp_)
        address = static_cast<int>(inet_addr(local_ip_.data()));
    auto server = memory::make_owned<HostEnd>(flags);
    server->set_supervisor(this);
    server->prepare_all();
    server->set_unknown1eb4(static_cast<uint32_t>(id));
    copy_name(server->label().data(), name);
    server->set_peer_cap(max_clients);
    append_last(&hosts_, &server->list1414());
    if (server->is_host_local()) {
        server->set_linked_up(true);
    } else {
        server->get_bind(static_cast<uint32_t>(address), port);
        if (flags & 1)
            server->set_linked_up(true);
    }
    add_app_tasks(server);
    in_addr ip{};
    ip.s_addr = static_cast<ULONG>(address);
    debug::printf("Created new: %s server %p, Max Clients: %d, IP: %s, Port: %d", name, server, max_clients,
                  inet_ntoa(ip), port);
    ++num_apps_;
    memory::Hub::get_pull()->leave_heap_scope();
    return server;
}

// 0x004553F0
gsl::owner<Guest*> Hub::make_client_app(int id, const char* name, uint16_t port, Hub::AppBinding binding)
{
    auto address = binding.address;
    const auto flags = binding.flags;
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->network_heap());
    if (connection_type_ != kConnectionType3 && connection_type_ != kConnectionTypeNone && use_dhcp_)
        address = static_cast<int>(inet_addr(local_ip_.data()));
    auto client = memory::make_owned<Guest>(flags);
    copy_name(client->label().data(), name);
    client->set_supervisor(this);
    client->prepare_all();
    client->set_unknown1eb4(static_cast<uint32_t>(id));
    client->set_peer_cap(kClientMaxConnections);
    append_last(&clients_, &client->list1414());
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
    memory::ListNode& node = app->list1414();
    node.prev()->set_next(node.next());
    node.next()->set_prev(node.prev());
    node.set_next(&node);
    node.set_prev(&node);
    app->tear_down_state(); // (through 0x00456B30)
    gsl::owner<decltype(app)> owned = memory::owned_element(app); // (the manager's list's)
    delete owned; // (checked for null only now)
    --num_apps_;
}

// 0x00455880
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            sole() = memory::HeapUnique<Hub>(memory::construct_in<Hub>(memory));
            ++ref_tally();
            return sole().get();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole().get();
}

// 0x004558D0
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    memory::destroy_then_clear(sole()); // (its destructor, then its block given back)
    ref_tally() = 0;
}

} // namespace net
