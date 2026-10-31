// The network library's manager (NetLink::Hub): its settings getters and setters. See net_manager.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, gethostbyname, inet_ntoa: what the game uses

#include "engine/net/net_manager.h"

#include <winsock2.h>

#include <cstdint>
#include <cstring>

#include "engine/core/task_list.h"
#include "engine/net/net_app.h"
#include "engine/sys/mlp.h"
#include "engine/assets/resource_registry.h"

namespace net {

namespace {

// strcpy: the settings are copied with no length limit.
void copy_string(char* out, const char* source)
{
    while ((*out++ = *source++) != '\0') {
    }
}

// The setters that copy only a different string.
void copy_if_different(char* out, const char* source)
{
    if (std::strcmp(source, out) != 0)
        copy_string(out, source);
}

// Adds a task to the main loop's logic tasks (frame_loop::Hub +0x04).
void register_logic_task(uint8_t* mlp, void* task)
{
    reinterpret_cast<frame_loop::Hub*>(mlp)->logic_tasks_.queue_job(static_cast<tasks::SchedulerTask*>(task));
}

} // namespace

Hub* Hub::sole() { return g_net_core; }

// 0x00454580
bool Hub::connect_to_internet()
{
    is_networked_ = true;
    return true;
}

// 0x00454590
bool Hub::disconnect_from_internet()
{
    is_networked_ = false;
    return true;
}

// 0x004545D0
void Hub::set_dialup_number(int index, const char* number) { copy_string(dialup_numbers_[index], number); }

// 0x00454600
void Hub::set_dialup_user_name(const char* name) { copy_string(dialup_user_name_, name); }

// 0x00454620
void Hub::set_dialup_password(const char* password) { copy_string(dialup_password_, password); }

// 0x00454640
void Hub::set_router_addr(const char* ip) { copy_if_different(router_addr_, ip); }

// 0x004546A0
void Hub::set_address_mask(const char* ip) { copy_if_different(subnet_mask_, ip); }

// 0x00454710
void Hub::set_own_address(const char* ip) { copy_if_different(local_ip_, ip); }

// 0x00454770
void Hub::set_net_link_kind(int type)
{
    if (connection_type_ == type)
        return;
    connection_type_ = type;
    switch (type) {
    case kConnectionModem:
        link_speed_ = 4200;
        break;
    case kConnection2:
    case kConnection4:
        link_speed_ = 400000;
        break;
    case kConnection3:
        link_speed_ = 300000;
        break;
    default: // (the bandwidth stays)
        break;
    }
}

// 0x004547D0
void Hub::set_adapter_kind(int type)
{
    if (device_type_ != type)
        device_type_ = type;
}

// 0x004547F0
void Hub::set_auto_address(bool use_dhcp)
{
    if (use_dhcp_ != use_dhcp)
        use_dhcp_ = use_dhcp;
}

// 0x00454820
void Hub::set_name_resolver(int index, const char* ip) { copy_if_different(resolver_ips_[index], ip); }

// 0x00454890
void Hub::set_machine_label(const char* name)
{
    if (!name)
        host_name_[0] = '\0'; // (and the copy below still reads from the null pointer, as in the original)
    copy_string(host_name_, name);
}

// 0x004548C0
void Hub::set_network_domain(const char* name)
{
    if (!name)
        domain_name_[0] = '\0'; // (likewise)
    copy_string(domain_name_, name);
}

// 0x004548F0
uint32_t Hub::get_local_address()
{
    if (local_address_)
        return local_address_;
    return inet_addr(local_ip_);
}

// 0x00454910
const char* Hub::fetch_own_address()
{
    hostent* localhost = gethostbyname("localhost");
    if (!localhost)
        return "<Unknown>";
    hostent* host = gethostbyname(localhost->h_name);
    // (Without the host, localhost's address - read from Winsock's per-thread hostent after the second lookup
    // has used it, as the original does.)
    if (!host)
        host = localhost;
    return inet_ntoa(*reinterpret_cast<in_addr*>(host->h_addr_list[0]));
}

// 0x004549C0
bool Hub::load_network_drivers(const char* /*ip*/, const char* /*subnet_mask*/, const char* /*gateway*/)
{
    return true;
}

// 0x00454CB0
void Hub::add_app_tasks(AppHost* app)
{
    uint8_t* mlp = reinterpret_cast<uint8_t*>(assets::g_main_loop_manager); // the frame_loop::Hub
    register_logic_task(mlp, app->receive_task());
    register_logic_task(mlp, app->send_task());
    register_logic_task(mlp, app->process_task());
    register_logic_task(mlp, app->metrics_task());
}

// 0x00454DE0
const char* Hub::get_dialup_number()
{
    if (connection_type_ == kConnection3)
        return "";
    return dialup_numbers_[dialup_number_index_];
}

} // namespace net
