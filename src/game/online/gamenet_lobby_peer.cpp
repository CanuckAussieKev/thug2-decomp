// The GameSpy lobby's connection, hosting and NAT negotiation (online::RoomManager, 0x0053F570-0x00540C90): the peer
// SDK's callbacks, the lobby's tasks and the packet queue they share. The peer and NAT negotiation SDKs are library
// code, called by address. See gamenet_lobby.h.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <gsl/pointers>
#include <span>

#include "base/core/checked_index.h"
#include "game/online/player_info.h"
#include "base/core/enum_ops.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/task_list.h"
#include "game/online/buddy_man.h"
#include "game/online/gamenet_handler_util.h"
#include "game/online/gamenet_lobby.h"
#include "platform/gamespy.h"
#include "game/online/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_manager.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "game/commands/game_cfuncs.h"
#include "platform/code_address.h"
#include "engine/assets/resource_registry.h"
#include "base/core/stubs.h"
#include "engine/memory/fwd.h"
#include "game/online/fwd.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/online/server_list.h"
#include "game/online/score_list.h"
#include "game/online/stats_man.h"
#include "platform/crt.h"
#include "engine/sys/mlp.h"
#include "formats/byte_stream.h"
#include "base/core/compiler.h"
#include "engine/memory/mem.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/core/str.h"
#include "game/online/net_game_state.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's own)

namespace online {

using namespace detail;

namespace {
// A server-query packet starts 0xFE 0xFD (or a backslash); a NAT packet is at least as long as its magic.
constexpr uint8_t kQueryMagic0 = 0xFE;
constexpr uint8_t kQueryMagic1 = 0xFD;
constexpr int kNatPacketMinimum = 6;
constexpr size_t kSockaddrBytes = 8;
constexpr int kByteBits = 8;

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
constexpr std::array<uint8_t, 6> kNatMagic = {{0xFD, 0xFC, 0x1E, 0x66, 0x6A, 0xB2}};

// PlayerRecord flags (+0x1D0) / JoinerRecord flags (+0x20).

tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

RoomManager* task_lobby(tasks::Task<RoomManager>* task) { return task->content(); }

// The heaps the lobby's SDK work allocates from.
void push_internet_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_top_down_heap());
}
void push_internet_bottom_up_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_bottom_up_heap());
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }

// The game's C runtime (the queue's copies are freed by the original's code too).
gsl::owner<void*> game_malloc(size_t size) { return crt::alloc_block(size); }
void game_free(gsl::owner<void*> memory) { crt::dealloc(memory); }

// The queue emptied (its copies freed).
void clear_queue()
{
    for (int i = 0; i < net_game_state().queued_count(); ++i) {
        game_free(memory::owned_element(core::at(net_game_state().get_queue(), i).data));
    }
    net_game_state().queued_count() = 0;
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
int nat_begin(uint32_t socket, int cookie, int client_index, void* progress, game_spy::NatCompleted completed,
              int user_data)
{
    return game_spy::nat_begin_with_socket(socket, cookie, client_index, progress, completed, user_data);
}
// An empty function: no progress callback.
void* no_progress() { return code_address(stubs::do_nothing); }

// NOT ORIGINAL grouping - gamenet_lobby_peer.cpp's state (the original's globals). Made on first use, never destroyed
// (gamenet_lobby_peer_state()).
struct GamenetLobbyPeerState {
    bool not_posted_shown_{};
    uint32_t last_report_time_{};
    uint32_t hosting_since_{};
    uint32_t connected_time_{};
};

GamenetLobbyPeerState& gamenet_lobby_peer_state()
{
    static core::NoDestroy<GamenetLobbyPeerState> state;
    return state.get();
}

} // namespace

// 0x0053F570
void RoomManager::disconnected_callback(void* /*unused*/, const char* reason, void* param)
{
    const uint32_t elapsed = frame_clock::get_time() - gamenet_lobby_peer_state().connected_time_; // since connected
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
void RoomManager::queue_packet(const uint8_t* data, int length, const void* from)
{
    if (net_game_state().queued_count() >= kMaxQueued) {
        return;
    }
    // (a byte of the packet; the original read the one or two bytes it asked for even past a shorter packet: 0 here)
    const std::span<const uint8_t> bytes(data, length > 0 ? static_cast<size_t>(length) : 0);
    auto byte_at = [&](size_t index) { return index < bytes.size() ? core::at(bytes, index) : uint8_t{0}; };
    int type = 0;
    if ((byte_at(0) == kQueryMagic0 && byte_at(1) == kQueryMagic1) || byte_at(0) == '\\') {
        type = kPacketQuery;
    } else if (length >= kNatPacketMinimum && std::memcmp(bytes.data(), kNatMagic.data(), sizeof(kNatMagic)) == 0) {
        type = kPacketNatNegotiation;
    } else {
        return;
    }
    QueuedPacket& packet = core::at(net_game_state().get_queue(), net_game_state().queued_count());
    packet.data = static_cast<char*>(game_malloc(length + 1));
    std::memcpy(packet.data, data, length);
    core::at(std::span<char>(packet.data, static_cast<size_t>(length) + 1), static_cast<size_t>(length)) = '\0';
    std::memcpy(packet.from.data(), from, sizeof(packet.from));
    packet.type = type;
    packet.length = length;
    ++net_game_state().queued_count();
}

// 0x0053FB80
void RoomManager::player_key_callback(const PlayerKeyQuery& query)
{
    const auto key = query.key;
    const auto index = query.index;
    auto* const buffer = query.buffer;
    net_game_state().set_game_posted(true);
    if (key == kKeyPing) {
        buffer_add(buffer, "");
        return;
    }
    if (key != kKeyForPlayer && key != kKeyRating) {
        return;
    }
    Hub* manager = net_game_state().online_hub();
    int count = 0;
    PlayerSearch search;
    NewPlayerSearch new_search;
    for (PlayerRecord* player = start_roster_sweep(manager, &search, true); player;
         player = step_roster_sweep(manager, &search, true)) {
        if (is_observer(player) && !core::any(player->flag_bits() & PlayerFlag::kPendingObserver)) {
            continue;
        }
        if (count == index) {
            if (key == kKeyForPlayer) {
                buffer_add(buffer, player_name(player));
            } else {
                buffer_add_int(buffer, player->skill_points()); // its rating
            }
            return;
        }
        ++count;
    }
    for (JoinerRecord* info = first_new_player(manager, &new_search); info;
         info = next_new_player(manager, &new_search)) {
        const PlayerFlag flags = info->flag_bits();
        if (!core::any(flags & kPlayerCountedFlags) && core::any(flags & PlayerFlag::kObserver)) {
            continue;
        }
        if (count == index) {
            if (key == kKeyForPlayer) {
                buffer_add(buffer, info->label().data()); // (its name)
            } else {
                buffer_add_int(buffer, info->skill_points());
            }
            return;
        }
        ++count;
    }
    buffer_add(buffer, "");
}

// 0x0053FDA0
void RoomManager::public_address_callback(void* /*unused*/, uint32_t ip, uint16_t port, void* param)
{
    Hub* manager = net_game_state().online_hub();
    FriendsManager* buddy = manager->presence_service().get();
    if (!manager->get_on_server()) {
        return;
    }
    auto* lobby = static_cast<RoomManager*>(param);
    if (!lobby->remote_) {
        return;
    }
    manager->set_entry_port(port);
    manager->set_join_ip(ip);
    manager->set_join_private_ip(peer_private_ip(lobby->remote_));
    const char* name = nullptr;
    manager->get_online_settings()->fetch_setting(kServerName)->fetch_text(kUiString, &name, 1);
    constexpr size_t kLocationSize = 1024;
    std::array<char, kLocationSize> location{};
    text::format_to(location, "10thug211:%d:%d:%d:%s (%s)", ip, peer_private_ip(lobby->remote_), port, name,
                    lobby->room_name_.data());
    if (buddy->authenticated()) { // (logged in to the presence)
        buddy->change_standing(kStatusHosting, script::fetch_text(kHomieStatusHosting), location.data());
    }
}

// 0x0053FEE0
void RoomManager::get_nick_error_callback(void* peer, int type, const char* nick, int /*unused*/,
                                          const char** /*unused*/, void* /*unused*/)
{
    push_internet_heap();
    constexpr size_t kRetrySize = 128;
    std::array<char, kRetrySize> retry{};
    if (type == kNickInUse) {
        const char* id = net_game_state().online_hub()->get_online_settings()->get_preference_string({.preference = kNetworkId, .field = kUiString});
        debug::printf("Nick Error. Nick %s In Use\n", nick);
        text::format_to(retry, "a%d_%s", frame_clock::get_time(), id);
        game_spy::peer_retry_with_nick(peer, static_cast<const char*>(retry.data())); // retried with that nick
    } else if (type == kNickInvalid) {
        debug::printf("Nick Error. Nick %s is Invalid\n", nick);
        game_spy::peer_fix_nick(static_cast<char*>(retry.data()), nick); // the nick made valid
        game_spy::peer_retry_with_nick(peer, static_cast<const char*>(retry.data()));
    }
    pop_heap();
}

// 0x0053FFC0
void RoomManager::get_connect_callback(void* peer, int success, int /*unused*/, void* param)
{
    auto* lobby = static_cast<RoomManager*>(param);
    lobby->connecting_ = false;
    if (success == 0) {
        debug::printf("******* Connect Callback FAILED!\n");
        get_run(kConnectionFailureDialog);
        return;
    }
    net::Hub* net = net::Hub::sole();
    debug::printf("****** Connect Callback Successful\n");
    net->set_local_address(game_spy::peer_get_public_ip(peer)); // the peer's public address
    commands::cfunc_start_lobby_list(nullptr, nullptr);
    lobby->drop_is_expected_ = false;
    gamenet_lobby_peer_state().connected_time_ = frame_clock::get_time(); // connected
}

// 0x00540040
void RoomManager::think_task(tasks::Task<RoomManager>* task)
{
    RoomManager* lobby = task_lobby(task);
    push_internet_heap();
    if (game_spy::peer_has_chat(lobby->remote_)) { // (its chat connection made)
        game_spy::peer_think(lobby->remote_);
    }
    if (lobby->lost_connection_) {
        lobby->lost_connection_ = false;
        get_run(kLostConnectionToGameSpy);
    }
    pop_heap();
}

// 0x005400B0
void RoomManager::nat_negotiation_task(tasks::Task<RoomManager>* /*unused*/)
{
    push_internet_bottom_up_heap();
    nat_think();
    pop_heap();
}

// 0x005400E0
void RoomManager::query_task(tasks::Task<RoomManager>* task)
{
    const RoomManager* lobby = task_lobby(task);
    const uint32_t now = frame_clock::get_time();
    push_internet_heap();
    for (int i = 0; i < net_game_state().queued_count(); ++i) {
        QueuedPacket& packet = core::at(net_game_state().get_queue(), i);
        if (packet.type == kPacketQuery) {
            game_spy::peer_parse_query(lobby->remote_, packet.data, packet.length,
                                       static_cast<void*>(packet.from.data()));
        } else if (packet.type == kPacketNatNegotiation) {
            game_spy::nat_process_data(packet.data, packet.length, static_cast<void*>(packet.from.data()));
        }
        game_free(memory::owned_element(packet.data)); // (the queue's copy)
    }
    net_game_state().queued_count() = 0;
    Hub* manager = net_game_state().online_hub();
    if (!net_game_state().game_posted() && manager->in_net_game() && manager->get_on_server()) { // not reported yet
        if (now - gamenet_lobby_peer_state().hosting_since_ > kNotPostedTime) {
            if (!gamenet_lobby_peer_state().not_posted_shown_) {
                get_run(kCreateNotPostedDialog);
                gamenet_lobby_peer_state().not_posted_shown_ = true;
            }
        } else if (now - gamenet_lobby_peer_state().last_report_time_ > kReportInterval) {
            peer_state_changed(lobby->remote_);
            gamenet_lobby_peer_state().last_report_time_ = now;
        }
    }
    pop_heap();
}

// 0x00540220
void RoomManager::start_hosting()
{
    tasks::PileUp* tasks = get_logic_tasks();
    net::AppHost* server = net_game_state().online_hub()->host();
    server->set_outside_packet_hook([](const uint8_t* data, int size, sockaddr* from) { RoomManager::queue_packet(data, size, from); });
    clear_queue();
    if (game_spy::peer_start_reporting_with_socket(remote_, server->get_socket(), static_cast<int>(kHostPort)) ==
        1) { // reporting on its socket
        peer_state_changed(remote_);
    }
    const uint32_t now = frame_clock::get_time();
    gamenet_lobby_peer_state().hosting_since_ = now;    // hosting since
    net_game_state().set_connection_timer(now); // the connection timer
    gamenet_lobby_peer_state().last_report_time_ = now; // reported last
    gamenet_lobby_peer_state().not_posted_shown_ = false;
    net_game_state().set_game_posted(false);
    tasks->queue_job(hosting_task_.get());
    hosting_ = true;
}

// 0x00540300
void RoomManager::nat_negotiation_completed(int result, int /*unused*/, const void* address, int user_data)
{
    Hub* manager = net_game_state().online_hub();
    if (result == kNatSuccess && !manager->get_on_server()) { // the client joins at the address found
        // (the SDK's sockaddr_in: the family, the port in network order, the address as it is in memory)
        util::ByteReader sockaddr(address, kSockaddrBytes);
        sockaddr.skip(2);
        const uint8_t port_high = sockaddr.u8();
        const uint8_t port_low = sockaddr.u8();
        const uint32_t ip = sockaddr.u32();
        auto port = static_cast<uint16_t>(port_high << kByteBits | port_low); // (ntohs)
        manager->join_server({.ip = ip, .port = port}, 0);
    }
    auto* lobby = manager->lobby_service().get();
    if (manager->get_on_server()) {
        if (--lobby->negotiations_ == 0) {
            lobby->nat_task_->drop_out();
        }
        return;
    }
    nat_cancel(user_data);
    nat_think();
    nat_free_negotiations();
    manager->fetch_patron(0)->set_outside_packet_hook(nullptr);
    lobby->nat_task_->drop_out();
    lobby->hosting_task_->drop_out();
    if (result != kNatSuccess) {
        get_run(kNatNegotiationFailed);
    }
}

// 0x00540750
void RoomManager::hangup()
{
    push_internet_heap();
    think_task_->drop_out();
    nat_task_->drop_out();
    hosting_task_->drop_out();
    if (remote_) {
        game_spy::peer_shutdown(remote_); // the peer shut down
    }
    remote_ = nullptr;
    pop_heap();
}

// 0x00540AB0
unsigned long __stdcall RoomManager::connect_thread(void* lobby_data)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_bottom_up_heap());
    Hub* manager = net_game_state().online_hub();
    constexpr size_t kNickSize = 128;
    std::array<char, kNickSize> nick{};
    text::copy_into(nick, manager->get_online_settings()->get_preference_string({.preference = kNetworkId, .field = kUiString}));
    auto* lobby = static_cast<RoomManager*>(lobby_data);
    lobby->connecting_ = true;
    auto nick_error = &RoomManager::get_nick_error_callback;
    auto connected = &RoomManager::get_connect_callback;
    game_spy::peer_connect(lobby->remote_, static_cast<const char*>(nick.data()),
                           manager->presence_service()->account() /* its profile */, nick_error, connected, lobby_data,
                           0); // peer connect (not blocking)
    memory::Hub::get_pull()->leave_heap_scope();
    return 0;
}

// 0x00540C90
void RoomManager::hole_punch_result_fn(void* /*unused*/, int cookie, void* /*unused*/)
{
    tasks::PileUp* tasks = get_logic_tasks();
    const Hub* manager = net_game_state().online_hub();
    if (manager->lobby_service()->negotiations_ != 0) {
        return; // (one at a time)
    }
    const net::AppHost* server = manager->host();
    push_internet_heap();
    if (nat_begin(server->get_socket(), cookie, 0, no_progress(), &RoomManager::nat_negotiation_completed, cookie) !=
        0) {
        nat_cancel(cookie);
        nat_think();
        pop_heap();
        return;
    }
    auto* lobby = manager->lobby_service().get();
    if (lobby->negotiations_ == 0) {
        tasks->queue_job(lobby->nat_task_.get());
    }
    ++lobby->negotiations_;
    pop_heap();
}

} // namespace online
