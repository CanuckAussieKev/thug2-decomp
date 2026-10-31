// The GameSpy lobby's script commands: NAT negotiation started / cancelled, leaving the lobby, a cancelled connect,
// the quiet mode, whether this player may host (0x005403E0-0x00540750). See gamenet_lobby.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // (inet_addr: the original's own call)
#include <winsock2.h>

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamespy.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/stubs.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/stats_man.h"
#include "platform/crt.h"

namespace online {

using namespace detail;

int g_queued_count;

QueuedPacket g_queued_packets[200];

namespace {

constexpr uint32_t kCookie = 0x751F4599;               // cookie
constexpr uint32_t kPreserveStatus = 0xB2AB4DAB;       // preserve_status
constexpr uint32_t kHomieStatusOnline = 0xFF257E72;    // homie_status_online
constexpr uint32_t kHomieStatusLoggingIn = 0x1E40E143; // homie_status_logging_in
constexpr uint32_t kOff = 0xD443A2BC;                  // off
constexpr int kStatusOnline = 1;
constexpr int kRoomGroupKind = 1; // (the peer SDK's room types)
constexpr int kClientNatIndex = 1;
// The GameSpy profiles that may host while hosting is restricted (+0x135).
constexpr int kHostProfiles[] = {0x01A7665B, 0x01A76689, 0x01A766AB, 0x01A76701, 0x01A766CC,
                                 0x01A7672E, 0x01A7675B, 0x01A76773, 0x01A76794, 0x01A767B1};

Hub* net_session_slot() { return g_online_hub; }
RoomManager* current_lobby() { return static_cast<RoomManager*>(net_session_slot()->lobby_service_); }
tasks::PileUp* get_logic_tasks()
{
    return reinterpret_cast<tasks::PileUp*>(
        reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(assets::g_main_loop_manager)) + 4);
}
void push_internet_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_top_down_heap_);
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }
void nat_cancel(int cookie) { game_spy::nat_cancel(cookie); }
void nat_think() { game_spy::nat_think(); }
void nat_free_negotiations() { game_spy::nat_free_negotiations(); }
QueuedPacket* get_queue() { return g_queued_packets; }
int& queued_count() { return g_queued_count; }

} // namespace

// 0x00540430
bool __cdecl cfunc_start_nat_negotiation(script::Struct* params, script::Script*)
{
    tasks::PileUp* tasks = get_logic_tasks();
    Hub* manager = net_session_slot();
    const char* address = nullptr;
    int port = 0;
    push_internet_heap();
    auto* lobby = static_cast<RoomManager*>(manager->lobby_service_);
    params->fetch_text(0u, &address, 0);
    params->fetch_int_value(0u, &port, 0);
    params->read_hash_value(kCookie, reinterpret_cast<uint32_t*>(&lobby->nat_cookie_), 0);
    net::AppHost* client = manager->start_client_session(false, false, true, 0);
    game_spy::peer_send_nat_negotiate_cookie(lobby->remote_, static_cast<uint32_t>(inet_addr(address)), port,
                                             lobby->nat_cookie_); // the cookie to the host
    int error = game_spy::nat_begin_with_socket(
        client->get_socket(), lobby->nat_cookie_, kClientNatIndex,
        code_address(stubs::do_nothing) /* no progress callback */, &RoomManager::nat_negotiation_completed,
        reinterpret_cast<void*>(static_cast<intptr_t>(lobby->nat_cookie_))); // negotiation on the client's socket
    if (error) {
        nat_free_negotiations();
        pop_heap();
        return false;
    }
    tasks->queue_job(lobby->nat_task_);
    client->set_outside_packet_hook(reinterpret_cast<net::UnknownPacketCallback>(&RoomManager::queue_packet));
    tasks->queue_job(lobby->hosting_task_);
    for (int i = 0; i < queued_count(); ++i)
        crt::dealloc(get_queue()[i].data); // (the game's C runtime free)
    queued_count() = 0;
    pop_heap();
    return true;
}

// 0x005403E0
bool __cdecl cfunc_cancel_nat_negotiation(script::Struct*, script::Script*)
{
    RoomManager* lobby = current_lobby();
    nat_cancel(lobby->nat_cookie_);
    nat_think();
    nat_free_negotiations();
    current_lobby()->nat_task_->drop_out();
    current_lobby()->hosting_task_->drop_out();
    return true;
}

// 0x00540590
bool __cdecl cfunc_leave_lobby(script::Struct* params, script::Script*)
{
    push_internet_heap();
    Hub* manager = net_session_slot();
    auto* lobby = static_cast<RoomManager*>(manager->lobby_service_);
    void* buddy = manager->presence_service_;
    game_spy::peer_leave_room(lobby->remote_, kRoomGroupKind, nullptr); // the group room left
    lobby->in_group_room_ = false;
    if (prop<bool>(buddy, 0x10) && !params->has_entry_named(kPreserveStatus)) {
        const char* text = script::fetch_text(kHomieStatusLoggingIn);
        const char* status = script::fetch_text(kHomieStatusOnline);
        static_cast<FriendsManager*>(buddy)->change_standing(kStatusOnline, status,
                                                             text); // the presence's status (its text, location)
    }
    game_spy::peer_set_group_id(lobby->remote_, lobby->group_id_);
    pop_heap();
    return true;
}

// 0x00540640
bool __cdecl cfunc_cancel_lobby_connect(script::Struct*, script::Script*)
{
    current_lobby()->connecting_ = false;
    current_lobby()->drop_is_expected_ = true;
    return true;
}

// 0x00540660
bool __cdecl cfunc_set_quiet_mode(script::Struct* params, script::Script*)
{
    push_internet_heap();
    Hub* manager = net_session_slot();
    int quiet = params->has_bare_flag(kOff) ? 0 : 1;
    game_spy::peer_set_quiet_mode(static_cast<RoomManager*>(manager->lobby_service_)->remote_, quiet);
    pop_heap();
    return true;
}

// 0x005406C0
bool __cdecl cfunc_can_host_game(script::Struct*, script::Script*)
{
    Hub* manager = net_session_slot();
    RoomManager* lobby = static_cast<RoomManager*>(manager->lobby_service_);
    if (!lobby->restricted_hosting_)
        return lobby->unknown30_ < lobby->unknown2c_;
    if (!prop<bool>(manager->presence_service_, 0x10)) // (logged in to the presence)
        return false;
    int profile = prop<int>(manager->presence_service_, 4);
    for (int allowed : kHostProfiles)
        if (profile == allowed)
            return true;
    return false;
}

} // namespace online
