// The GameSpy lobby's script commands: NAT negotiation started / cancelled, leaving the lobby, a cancelled connect,
// the quiet mode, whether this player may host (0x005403E0-0x00540750). See gamenet_lobby.h.

// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // (inet_addr: the original's own call)
#include <winsock2.h>

#include <cstdint>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h" // IWYU pragma: keep - the detail namespace (using-directive)
#include "engine/net/gamenet_lobby.h"
#include "platform/gamespy.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
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
#include "engine/sys/mlp.h"
#include "engine/core/mem.h"
#include "engine/net/net_game_state.h"
#include <gsl/pointers>

namespace online {

using namespace detail;

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
constexpr auto kHostProfiles = std::to_array<int>({0x01A7665B, 0x01A76689, 0x01A766AB, 0x01A76701, 0x01A766CC,
                                 0x01A7672E, 0x01A7675B, 0x01A76773, 0x01A76794, 0x01A767B1});

RoomManager* current_lobby() { return net_game_state().net_session_slot()->lobby_service().get(); }
tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks_); }
void push_internet_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_top_down_heap());
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }
void nat_cancel(int cookie) { game_spy::nat_cancel(cookie); }
void nat_think() { game_spy::nat_think(); }
void nat_free_negotiations() { game_spy::nat_free_negotiations(); }

} // namespace

// 0x00540430
bool cfunc_start_nat_negotiation(script::Struct* params, script::Script*)
{
    tasks::PileUp* tasks = get_logic_tasks();
    Hub* manager = net_game_state().net_session_slot();
    const char* address = nullptr;
    int port = 0;
    push_internet_heap();
    auto* lobby = manager->lobby_service().get();
    params->fetch_text(0u, &address, 0);
    params->fetch_int_value(0u, &port, 0);
    auto cookie = static_cast<uint32_t>(lobby->nat_cookie());
    params->read_hash_value(kCookie, &cookie, 0);
    lobby->set_nat_cookie(static_cast<int>(cookie));
    net::AppHost* client = manager->start_client_session(false, false, true, 0);
    game_spy::peer_send_nat_negotiate_cookie(lobby->remote(), static_cast<uint32_t>(inet_addr(address)), port,
                                             lobby->nat_cookie()); // the cookie to the host
    const int error = game_spy::nat_begin_with_socket(
        client->get_socket(), lobby->nat_cookie(), kClientNatIndex,
        code_address(stubs::do_nothing) /* no progress callback */, &RoomManager::nat_negotiation_completed,
        lobby->nat_cookie()); // negotiation on the client's socket
    if (error) {
        nat_free_negotiations();
        pop_heap();
        return false;
    }
    tasks->queue_job(lobby->nat_task().get());
    client->set_outside_packet_hook([](const uint8_t* data, int size, sockaddr* from) { RoomManager::queue_packet(data, size, from); });
    tasks->queue_job(lobby->hosting_task().get());
    for (int i = 0; i < net_game_state().queued_count(); ++i)
        crt::dealloc(memory::owned_element(core::at(net_game_state().get_queue(), i).data)); // (the game's C runtime free; the queue's copy)
    net_game_state().queued_count() = 0;
    pop_heap();
    return true;
}

// 0x005403E0
bool cfunc_cancel_nat_negotiation(script::Struct*, script::Script*)
{
    const RoomManager* lobby = current_lobby();
    nat_cancel(lobby->nat_cookie());
    nat_think();
    nat_free_negotiations();
    current_lobby()->nat_task()->drop_out();
    current_lobby()->hosting_task()->drop_out();
    return true;
}

// 0x00540590
bool cfunc_leave_lobby(script::Struct* params, script::Script*)
{
    push_internet_heap();
    const Hub* manager = net_game_state().net_session_slot();
    auto* lobby = manager->lobby_service().get();
    FriendsManager* buddy = manager->presence_service().get();
    game_spy::peer_leave_room(lobby->remote(), kRoomGroupKind, nullptr); // the group room left
    lobby->set_in_group_room(false);
    if (buddy->authenticated() && !params->has_entry_named(kPreserveStatus)) {
        const char* text = script::fetch_text(kHomieStatusLoggingIn);
        const char* status = script::fetch_text(kHomieStatusOnline);
        static_cast<FriendsManager*>(buddy)->change_standing(kStatusOnline, status,
                                                             text); // the presence's status (its text, location)
    }
    game_spy::peer_set_group_id(lobby->remote(), lobby->group_id());
    pop_heap();
    return true;
}

// 0x00540640
bool cfunc_cancel_lobby_connect(script::Struct*, script::Script*)
{
    current_lobby()->set_connecting(false);
    current_lobby()->set_drop_is_expected(true);
    return true;
}

// 0x00540660
bool cfunc_set_quiet_mode(script::Struct* params, script::Script*)
{
    push_internet_heap();
    const Hub* manager = net_game_state().net_session_slot();
    const int quiet = params->has_bare_flag(kOff) ? 0 : 1;
    game_spy::peer_set_quiet_mode(manager->lobby_service()->remote(), quiet);
    pop_heap();
    return true;
}

// 0x005406C0
bool cfunc_can_host_game(script::Struct*, script::Script*)
{
    const Hub* manager = net_game_state().net_session_slot();
    const RoomManager* lobby = manager->lobby_service().get();
    if (!lobby->restricted_hosting())
        return lobby->unknown30() < lobby->unknown2c();
    if (!manager->presence_service()->authenticated()) // (logged in to the presence)
        return false;
    const int profile = manager->presence_service()->account();
    for (const int allowed : kHostProfiles)
        if (profile == allowed)
            return true;
    return false;
}

} // namespace online
