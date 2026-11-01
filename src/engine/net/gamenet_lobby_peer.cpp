// The GameSpy lobby's connection, hosting and NAT negotiation (online::RoomManager, 0x0053F570-0x00540C90): the peer
// SDK's callbacks, the lobby's tasks and the packet queue they share. The peer and NAT negotiation SDKs are library
// code, called by address. See gamenet_lobby.h.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamespy.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_manager.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/script/game_cfuncs.h"
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
#include "engine/sys/mlp.h"
#include "engine/util/byte_stream.h"
#include "platform/compiler.h"

THUG2_MSVC_WARNING(disable : 4996) // (sprintf / strcpy: the original's own)

namespace online {

using namespace detail;

namespace {

// Scripts.
constexpr uint32_t kConnectionFailureDialog = 0x209F813A; // create_gamespy_connection_failure_dialog
constexpr uint32_t kLostConnectionToGameSpy = 0xFB1C18E4; // lost_connection_to_gamespy
constexpr uint32_t kCreateNotPostedDialog = 0xE1B4ADEB;   // CreateNotPostedDialog
constexpr uint32_t kNatNegotiationFailed = 0x3A9FB574;    // show_nat_timeout
constexpr uint32_t kHomieStatusHosting = 0xC5A16CE1;      // homie_status_hosting
constexpr uint32_t kServerName = 0x2A5CF019;              // server_name
constexpr uint32_t kUiString = 0x96875C0F;                // ui_string
constexpr uint32_t kNetworkId = 0xCBED746E;               // network_id

constexpr int kStatusHosting = 2;
constexpr uint16_t kHostPort = 0x1421; // 5153
constexpr int kMaxQueued = 199;
constexpr int kNickInUse = 0;
constexpr int kNickInvalid = 1;
constexpr int kNatSuccess = 0;
constexpr uint32_t kNotPostedTime = 90000; // ms hosting without being reported
constexpr uint32_t kReportInterval = 5000;
// qr2's keys.
constexpr int kKeyForPlayer = 21;
constexpr int kKeyPing = 24;
constexpr int kKeyRating = 57;

constexpr int kPacketQuery = 1;
constexpr int kPacketNatNegotiation = 2;
constexpr uint8_t kNatMagic[6] = {0xFD, 0xFC, 0x1E, 0x66, 0x6A, 0xB2};

// PlayerRecord flags (+0x1D0) / JoinerRecord flags (+0x20).
constexpr uint32_t kPlayerObserverCounted = 0x08; // (0x005434C0: an observer reported as a player)
constexpr uint32_t kNewPlayerCounted = 0x18;
constexpr uint32_t kNewPlayerObserver = 0x04;

Hub* net_session_slot() { return g_online_hub; }
tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }
QueuedPacket* get_queue() { return g_queued_packets; }
int& queued_count() { return g_queued_count; }
RoomManager* task_lobby(tasks::Task<RoomManager>* task) { return task->content_; }

// The heaps the lobby's SDK work allocates from.
void push_internet_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_top_down_heap_);
}
void push_internet_bottom_up_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_bottom_up_heap_);
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }

// The game's C runtime (the queue's copies are freed by the original's code too).
void* game_malloc(size_t size) { return crt::alloc_block(size); }
void game_free(void* memory) { crt::dealloc(memory); }

// The queue emptied (its copies freed).
void clear_queue()
{
    for (int i = 0; i < queued_count(); ++i)
        game_free(get_queue()[i].data);
    queued_count() = 0;
}

// The SDKs (gamespy.h).
void buffer_add(void* buffer, const char* text) { game_spy::call_qr2_buffer_add(buffer, text); }
void buffer_add_int(void* buffer, int value) { game_spy::get_qr2_buffer_add_int(buffer, value); }
uint32_t peer_private_ip(void* peer) { return game_spy::peer_get_private_ip(peer); }
void peer_state_changed(void* peer) { game_spy::peer_state_changed(peer); } // (the hosted game's details sent again)
void nat_think() { game_spy::nat_think(); }
void nat_cancel(int cookie) { game_spy::nat_cancel(cookie); }
void nat_free_negotiations() { game_spy::nat_free_negotiations(); }
// (the cookie is the user data)
int nat_begin(uint32_t socket, int cookie, int client_index, void* progress, game_spy::HookFn completed, int user_data)
{
    return game_spy::nat_begin_with_socket(socket, cookie, client_index, progress, completed,
                                           reinterpret_cast<void*>(static_cast<intptr_t>(user_data)));
}
void* const kNoProgress = code_address(stubs::do_nothing); // (an empty function: no progress callback)

} // namespace

bool g_not_posted_shown;
uint32_t g_connection_timer;
uint32_t g_last_report_time;
uint32_t g_hosting_since;
bool g_game_posted;
uint32_t g_connected_time;

// 0x0053F570
void __cdecl RoomManager::disconnected_callback(void*, const char* reason, void* param)
{
    uint32_t elapsed = frame_clock::get_time() - g_connected_time; // since connected
    debug::printf("Disconnected at %d after %d ms. Reason: %s\n", frame_clock::get_time(), elapsed, reason);
    auto* lobby = static_cast<RoomManager*>(param);
    if (lobby->connecting_) {
        debug::printf("******* Connect Callback FAILED!\n");
        get_run(kConnectionFailureDialog);
        lobby->connecting_ = false;
    } else if (!lobby->drop_is_expected_) {
        debug::printf("Wasn't expecting discon.  Attempting to stop hosting game\n");
        get_run(kLostConnectionToGameSpy);
    } else {
        debug::printf("Was expecting discon. Ignore\n");
    }
    lobby->drop_is_expected_ = true;
}

// 0x0053F6C0
void __cdecl RoomManager::queue_packet(char* data, int length, const void* from)
{
    if (queued_count() >= kMaxQueued)
        return;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    int type;
    if ((bytes[0] == 0xFE && bytes[1] == 0xFD) || bytes[0] == '\\')
        type = kPacketQuery;
    else if (length >= 6 && std::memcmp(bytes, kNatMagic, sizeof(kNatMagic)) == 0)
        type = kPacketNatNegotiation;
    else
        return;
    QueuedPacket& packet = get_queue()[queued_count()];
    packet.data = static_cast<char*>(game_malloc(length + 1));
    std::memcpy(packet.data, data, length);
    packet.data[length] = '\0';
    std::memcpy(packet.from, from, sizeof(packet.from));
    packet.type = type;
    packet.length = length;
    ++queued_count();
}

// 0x0053FB80
void __cdecl RoomManager::player_key_callback(void*, int key, int index, void* buffer, void*)
{
    g_game_posted = true;
    if (key == kKeyPing) {
        buffer_add(buffer, "");
        return;
    }
    if (key != kKeyForPlayer && key != kKeyRating)
        return;
    Hub* manager = net_session_slot();
    int count = 0;
    PlayerSearch search;
    NewPlayerSearch new_search;
    for (PlayerRecord* p = start_roster_sweep(manager, &search, true); p;
         p = step_roster_sweep(manager, &search, true)) {
        if (is_observer(p) && !(p->flag_bits_ & kPlayerObserverCounted))
            continue;
        if (count == index) {
            if (key == kKeyForPlayer)
                buffer_add(buffer, player_name(p));
            else
                buffer_add_int(buffer, p->skill_points_); // its rating
            return;
        }
        ++count;
    }
    for (JoinerRecord* info = first_new_player(manager, &new_search); info;
         info = next_new_player(manager, &new_search)) {
        uint32_t flags = info->flag_bits_;
        if (!(flags & kNewPlayerCounted) && (flags & kNewPlayerObserver))
            continue;
        if (count == index) {
            if (key == kKeyForPlayer)
                buffer_add(buffer, info->label_); // (its name)
            else
                buffer_add_int(buffer, info->skill_points_);
            return;
        }
        ++count;
    }
    buffer_add(buffer, "");
}

// 0x0053FDA0
void __cdecl RoomManager::public_address_callback(void*, uint32_t ip, uint16_t port, void* param)
{
    Hub* manager = net_session_slot();
    FriendsManager* buddy = manager->presence_service_.get();
    if (!manager->get_on_server())
        return;
    auto* lobby = static_cast<RoomManager*>(param);
    if (!lobby->remote_)
        return;
    manager->set_entry_port(port);
    manager->set_join_ip(ip);
    manager->set_join_private_ip(peer_private_ip(lobby->remote_));
    const char* name = nullptr;
    manager->get_online_settings()->fetch_setting(kServerName)->fetch_text(kUiString, &name, 1);
    char location[1024];
    std::sprintf(location, "10thug211:%d:%d:%d:%s (%s)", ip, peer_private_ip(lobby->remote_), port, name,
                 lobby->room_name_);
    if (buddy->authenticated_) // (logged in to the presence)
        static_cast<FriendsManager*>(buddy)->change_standing(kStatusHosting, script::fetch_text(kHomieStatusHosting),
                                                             location);
}

// 0x0053FEE0
void __cdecl RoomManager::get_nick_error_callback(void* peer, int type, const char* nick, int, const char**, void*)
{
    push_internet_heap();
    char retry[128];
    if (type == kNickInUse) {
        const char* id = net_session_slot()->get_online_settings()->get_preference_string(kNetworkId, kUiString);
        debug::printf("Nick Error. Nick %s In Use\n", nick);
        std::sprintf(retry, "a%d_%s", frame_clock::get_time(), id);
        game_spy::peer_retry_with_nick(peer, static_cast<const char*>(retry)); // retried with that nick
    } else if (type == kNickInvalid) {
        debug::printf("Nick Error. Nick %s is Invalid\n", nick);
        game_spy::peer_fix_nick(static_cast<char*>(retry), nick); // the nick made valid
        game_spy::peer_retry_with_nick(peer, static_cast<const char*>(retry));
    }
    pop_heap();
}

// 0x0053FFC0
void __cdecl RoomManager::get_connect_callback(void* peer, int success, int, void* param)
{
    auto* lobby = static_cast<RoomManager*>(param);
    lobby->connecting_ = false;
    if (!success) {
        debug::printf("******* Connect Callback FAILED!\n");
        get_run(kConnectionFailureDialog);
        return;
    }
    net::Hub* net = net::Hub::sole();
    debug::printf("****** Connect Callback Successful\n");
    net->set_local_address(game_spy::peer_get_public_ip(peer)); // the peer's public address
    commands::cfunc_start_lobby_list(nullptr, nullptr);
    lobby->drop_is_expected_ = false;
    g_connected_time = frame_clock::get_time(); // connected
}

// 0x00540040
void __cdecl RoomManager::think_task(tasks::Task<RoomManager>* task)
{
    RoomManager* lobby = task_lobby(task);
    push_internet_heap();
    if (game_spy::peer_has_chat(lobby->remote_)) // (its chat connection made)
        game_spy::peer_think(lobby->remote_);
    if (lobby->lost_connection_) {
        lobby->lost_connection_ = false;
        get_run(kLostConnectionToGameSpy);
    }
    pop_heap();
}

// 0x005400B0
void __cdecl RoomManager::nat_negotiation_task(tasks::Task<RoomManager>*)
{
    push_internet_bottom_up_heap();
    nat_think();
    pop_heap();
}

// 0x005400E0
void __cdecl RoomManager::query_task(tasks::Task<RoomManager>* task)
{
    RoomManager* lobby = task_lobby(task);
    uint32_t now = frame_clock::get_time();
    push_internet_heap();
    for (int i = 0; i < queued_count(); ++i) {
        QueuedPacket& packet = get_queue()[i];
        if (packet.type == kPacketQuery)
            game_spy::peer_parse_query(lobby->remote_, static_cast<char*>(packet.data), packet.length,
                                       static_cast<void*>(packet.from));
        else if (packet.type == kPacketNatNegotiation)
            game_spy::nat_process_data(static_cast<char*>(packet.data), packet.length, static_cast<void*>(packet.from));
        game_free(packet.data);
    }
    queued_count() = 0;
    Hub* manager = net_session_slot();
    if (!g_game_posted && manager->in_net_game() && manager->get_on_server()) { // not reported yet
        if (now - g_hosting_since > kNotPostedTime) {
            if (!g_not_posted_shown) {
                get_run(kCreateNotPostedDialog);
                g_not_posted_shown = true;
            }
        } else if (now - g_last_report_time > kReportInterval) {
            peer_state_changed(lobby->remote_);
            g_last_report_time = now;
        }
    }
    pop_heap();
}

// 0x00540220
void RoomManager::start_hosting()
{
    tasks::PileUp* tasks = get_logic_tasks();
    net::AppHost* server = net_session_slot()->host_;
    server->set_outside_packet_hook(reinterpret_cast<net::UnknownPacketCallback>(&RoomManager::queue_packet));
    clear_queue();
    if (game_spy::peer_start_reporting_with_socket(remote_, server->get_socket(), static_cast<int>(kHostPort)) ==
        1) // reporting on its socket
        peer_state_changed(remote_);
    uint32_t now = frame_clock::get_time();
    g_hosting_since = now;    // hosting since
    g_connection_timer = now; // the connection timer
    g_last_report_time = now; // reported last
    g_not_posted_shown = false;
    g_game_posted = false;
    tasks->queue_job(hosting_task_.get());
    hosting_ = true;
}

// 0x00540300
void __cdecl RoomManager::nat_negotiation_completed(int result, int, const void* address, void* user_data)
{
    Hub* manager = net_session_slot();
    if (result == kNatSuccess && !manager->get_on_server()) { // the client joins at the address found
        // (the SDK's sockaddr_in: the family, the port in network order, the address as it is in memory)
        util::ByteReader sockaddr(address, 8);
        sockaddr.skip(2);
        uint8_t port_high = sockaddr.u8();
        uint8_t port_low = sockaddr.u8();
        uint32_t ip = sockaddr.u32();
        uint16_t port = static_cast<uint16_t>(port_high << 8 | port_low); // (ntohs)
        manager->join_server(ip, port, 0);
    }
    auto* lobby = manager->lobby_service_.get();
    if (manager->get_on_server()) {
        if (--lobby->negotiations_ == 0)
            lobby->nat_task_->drop_out();
        return;
    }
    nat_cancel(static_cast<int>(reinterpret_cast<intptr_t>(user_data)));
    nat_think();
    nat_free_negotiations();
    manager->fetch_patron(0)->set_outside_packet_hook(nullptr);
    lobby->nat_task_->drop_out();
    lobby->hosting_task_->drop_out();
    if (result != kNatSuccess)
        get_run(kNatNegotiationFailed);
}

// 0x00540750
void RoomManager::hangup()
{
    push_internet_heap();
    think_task_->drop_out();
    nat_task_->drop_out();
    hosting_task_->drop_out();
    if (remote_)
        game_spy::peer_shutdown(remote_); // the peer shut down
    remote_ = nullptr;
    pop_heap();
}

// 0x00540AB0
unsigned long __stdcall RoomManager::connect_thread(void* lobby_data)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_bottom_up_heap_);
    Hub* manager = net_session_slot();
    char nick[128];
    std::strcpy(nick, manager->get_online_settings()->get_preference_string(kNetworkId, kUiString));
    auto* lobby = static_cast<RoomManager*>(lobby_data);
    lobby->connecting_ = true;
    auto nick_error = &RoomManager::get_nick_error_callback;
    auto connected = &RoomManager::get_connect_callback;
    game_spy::peer_connect(lobby->remote_, static_cast<const char*>(nick),
                           manager->presence_service_->account_ /* its profile */, nick_error, connected, lobby_data,
                           0); // peer connect (not blocking)
    memory::Hub::get_pull()->leave_heap_scope();
    return 0;
}

// 0x00540C90
void __cdecl RoomManager::hole_punch_result_fn(void*, int cookie, void*)
{
    tasks::PileUp* tasks = get_logic_tasks();
    Hub* manager = net_session_slot();
    if (manager->lobby_service_->negotiations_ != 0)
        return; // (one at a time)
    net::AppHost* server = manager->host_;
    push_internet_heap();
    if (nat_begin(server->get_socket(), cookie, 0, kNoProgress, &RoomManager::nat_negotiation_completed, cookie)) {
        nat_cancel(cookie);
        nat_think();
        pop_heap();
        return;
    }
    auto* lobby = manager->lobby_service_.get();
    if (lobby->negotiations_ == 0)
        tasks->queue_job(lobby->nat_task_.get());
    ++lobby->negotiations_;
    pop_heap();
}

} // namespace online
