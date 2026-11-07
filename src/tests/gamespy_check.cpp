// NOT ORIGINAL - thug2_gamespy_check.exe: the GameSpy SDK and its glue (platform/gamespy.h) driven without the
// network, so that a 32-bit and a 64-bit build can be compared byte for byte: its pure helpers (the CD-key response,
// the stats login hash, the peer's nick and user-name mangling, the chat and server-browsing ciphers, base64), the
// peer's queued callbacks, a whole NAT negotiation and a qr2 heartbeat with a query's answer - the last two against
// fake servers on the loopback addresses 127.0.0.1-3 only (nothing is resolved or sent anywhere else). It prints what
// each part produced, the bytes that differ from run to run (ports, the machine's own address, a random key)
// masked, and exits 0 when every part did what the game relies on (1 if one didn't, 2 if a socket couldn't be made).
// Some of the SDK's own sockets (NAT negotiation's, left unbound until it sends) are bound to every interface, though
// nothing goes anywhere but the loopback: the first run of the program from a new path can bring up the firewall's
// "allow access" prompt. Cancel is enough - the loopback needs no rule.

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "base/core/checked_index.h"
#include "base/core/compiler.h"
#include "base/core/gathered_call.h"
#include "base/core/no_destroy.h"
#include "base/core/raw_view.h"
#include "platform/gamespy.h"
#include "tests/no_error_dialogs.h"

THUG2_MSVC_WARNING(push)
THUG2_MSVC_WARNING(disable : 4005) // (gsPlatform.h defines WIN32_LEAN_AND_MEAN again)
#include "Chat/chatCrypt.h"
#include "Peer/peerMangle.h"
#include "common/gsAvailable.h"
#include "common/gsPlatformUtil.h"
#include "natneg/natneg.h"
#include "qr2/qr2.h"
#include "qr2/qr2regkeys.h"
#include "serverbrowsing/sb_crypt.h"
THUG2_MSVC_WARNING(pop)

extern "C" {
extern char* Matchup3Hostname; // (natneg.c; natneg.h declares only the first two)
}

namespace {

// What the SDK's callbacks saw.
struct PeerRecord {
    int connects = 0;
    int group_rooms = 0;
    int nick_errors = 0;
    bool param_kept = true;
};
struct NatRecord {
    int progress_calls = 0;
    bool progress_word_kept = true;
    int completions = 0;
    int result = -1;
    uintptr_t socket = 0;
    int user_data = 0;
    uint16_t port = 0;
};
struct Qr2Record {
    bool user_data_kept = true;
    int errors = 0;
};

// NOT ORIGINAL grouping - the check's state, made on first use and never destroyed (check_state()).
struct CheckState {
    int failures = 0;
    bool no_socket = false; // (a loopback socket couldn't be made: exit code 2)
    PeerRecord peer;
    NatRecord nat;
    Qr2Record qr2; // (its address is the qr2 callbacks' user data)
};
CheckState& check_state()
{
    static core::NoDestroy<CheckState> state;
    return state.get();
}

void expect(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        ++check_state().failures;
    }
}

// The bytes as hex, a masked one as "??".
std::string hex(std::span<const uint8_t> bytes, std::span<const bool> masked = {})
{
    static constexpr std::string_view kDigits = "0123456789abcdef";
    constexpr unsigned kNibble = 4;
    constexpr unsigned kLowNibble = 0xF;
    std::string text;
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        if (at < masked.size() && core::at(masked, at)) {
            text += "??";
        } else {
            text += core::at(kDigits, core::at(bytes, at) >> kNibble);
            text += core::at(kDigits, core::at(bytes, at) & kLowNibble);
        }
    }
    return text;
}

// --- the pure helpers ---

// The sizes the SDK's helpers write: gcd_compute_response's 72 characters, GenerateAuth's 32 (+ the NUL); the cipher
// keys' and texts' lengths.
constexpr std::size_t kCdKeyResponseSize = 73;
constexpr int kCdKeyHashSize = 32; // (the key's MD5; a random number and another MD5 follow)
constexpr std::size_t kStatsAuthSize = 33;
constexpr std::size_t kNickSize = 32;
constexpr std::size_t kTextSize = 32;
constexpr std::size_t kKeySize = 16;
constexpr std::size_t kXcodeKeySize = 8;
constexpr std::size_t kBase64Size = 48;
constexpr std::string_view kPlain = "THUG2 GameSpy check: 32 bytes..";
constexpr std::array<uint8_t, kKeySize> kKey{
    {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'}};

void check_mangling()
{
    constexpr unsigned kLoopbackIp = 0x0100007FU; // (127.0.0.1 in network order)
    constexpr int kProfile = 123456;
    std::array<char, PI_USER_MAX_LEN> user{};
    piMangleUser(user.data(), kLoopbackIp, kProfile);
    unsigned ip = 0;
    int profile = 0;
    const PEERBool demangled = piDemangleUser(user.data(), &ip, &profile);
    std::printf("mangled user: %s -> %08x %d\n", user.data(), ip, profile);
    expect(demangled != PEERFalse && ip == kLoopbackIp && profile == kProfile, "the peer user name round trip");
    constexpr int kGroupId = 7;
    std::array<char, PI_ROOM_MAX_LEN> room{};
    piMangleGroupRoom(room.data(), kGroupId);
    std::printf("group room: %s\n", room.data());
}

void check_ciphers()
{
    std::array<uint8_t, kTextSize> text{};
    std::ranges::copy(kPlain, text.begin());
    gs_crypt_key chat_key{};
    gs_prepare_key(kKey.data(), static_cast<int>(kKey.size()), &chat_key);
    std::array<uint8_t, kTextSize> chat = text;
    gs_crypt(chat.data(), static_cast<int>(chat.size()), &chat_key);
    std::printf("chat cipher: %s\n", hex(chat).c_str());
    std::array<char, kTextSize> xcoded{};
    std::ranges::copy(kPlain, xcoded.begin());
    std::array<char, kXcodeKeySize> xcode_key{{"key1234"}};
    gs_xcode_buf(xcoded.data(), static_cast<int>(xcoded.size()), xcode_key.data());
    std::printf("chat xcode: %s\n", hex({raw::bytes(xcoded.data()), xcoded.size()}).c_str());

    GOACryptState browse{};
    std::array<uint8_t, kKeySize> browse_key = kKey;
    GOACryptInit(&browse, browse_key.data(), static_cast<unsigned char>(browse_key.size()));
    std::array<uint8_t, kTextSize> browsed = text;
    GOAEncrypt(&browse, browsed.data(), static_cast<int>(browsed.size()));
    std::printf("browsing cipher: %s\n", hex(browsed).c_str());

    std::array<char, kBase64Size> base64{};
    B64Encode(raw::chars(text.data()), base64.data(), static_cast<int>(kPlain.size()), 0);
    std::printf("base64: %s\n", base64.data());
}

void check_helpers()
{
    std::array<char, kCdKeyResponseSize> cd_key_response{};
    game_spy::cd_key_compute_response("ABCDE-FGHIJ-KLMNO-PQRST", "x9check", cd_key_response.data());
    std::printf("cd key response: %.*s + <random> (%zu characters)\n", kCdKeyHashSize, cd_key_response.data(),
                std::string_view(cd_key_response.data()).size());

    std::array<char, kStatsAuthSize> stats_auth{};
    game_spy::stats_generate_auth("challenge", "password", stats_auth.data());
    std::printf("stats auth: %s\n", stats_auth.data());

    std::array<char, kNickSize> nick{};
    game_spy::fix_nick(nick.data(), "9 sk8r\\one!");
    std::printf("fixed nick: %s\n", nick.data());

    check_mangling();
    check_ciphers();
}

// --- the peer's queued callbacks (the game's calls that fail without a connection and title: connect and list the group
// rooms; the others assert a connection or return without a callback) ---

void on_nick_error(void* /*peer*/, int type, const char* /*nick*/, int /*count*/, const char** /*nicks*/, void* param)
{
    std::printf("peer nick error %d\n", type);
    ++check_state().peer.nick_errors;
    check_state().peer.param_kept = check_state().peer.param_kept && param == &check_state().peer;
}
void on_connect(void* /*peer*/, int success, int failure, void* param)
{
    std::printf("peer connect callback: success %d, failure %d\n", success, failure);
    ++check_state().peer.connects;
    check_state().peer.param_kept = check_state().peer.param_kept && param == &check_state().peer;
}
void on_group_room(void* /*peer*/, int success, int group, void* server, const char* name, int waiting, int max_waiting,
                   int games, int playing, void* param)
{
    std::printf("peer group room callback: success %d, group %d, server %s, name %s, %d %d %d %d\n", success, group,
                server ? "set" : "null", name ? name : "null", waiting, max_waiting, games, playing);
    ++check_state().peer.group_rooms;
    check_state().peer.param_kept = check_state().peer.param_kept && param == &check_state().peer;
}
void check_peer_callbacks()
{
    // (no title set and not connected: each call fails at once, its callback queued for peerThink - the queue
    // that kept callback addresses in an int)
    game_spy::PeerCallbacks callbacks;
    callbacks.param = &check_state().peer;
    void* peer = game_spy::peer_initialize(callbacks);
    expect(peer != nullptr, "peerInitialize");
    if (peer == nullptr) {
        return;
    }
    game_spy::peer_connect(peer, "x9check", 0, &on_nick_error, &on_connect, &check_state().peer, 0);
    game_spy::peer_list_group_rooms(peer, "\\maxplayers", &on_group_room, &check_state().peer, 0);
    constexpr int kThinks = 10;
    for (int think = 0; think < kThinks; ++think) {
        game_spy::peer_think(peer);
    }
    game_spy::peer_shutdown(peer);
    expect(check_state().peer.connects == 1 && check_state().peer.group_rooms == 1,
           "each queued peer callback called once");
    expect(check_state().peer.nick_errors == 0, "no nick error");
    expect(check_state().peer.param_kept, "the peer callbacks' parameter");
}

// --- loopback sockets ---

constexpr uint32_t kLoopback = 0x7F000001U; // 127.0.0.1 (host order); +1, +2: 127.0.0.2, 127.0.0.3

// An address and port to bind to (host order; port 0: any).
struct Endpoint {
    uint32_t ip = kLoopback;
    uint16_t port = 0;
};

class UdpSocket {
public:
    explicit UdpSocket(Endpoint where) : socket_(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))
    {
        if (!ok()) {
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(where.ip);
        address.sin_port = htons(where.port);
        u_long non_blocking = 1;
        if (::bind(socket_, raw::as<sockaddr>(&address), sizeof(address)) != 0 ||
            ::ioctlsocket(socket_, FIONBIO, &non_blocking) != 0) {
            ::closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return;
        }
        int length = sizeof(address_);
        ::getsockname(socket_, raw::as<sockaddr>(&address_), &length);
    }
    ~UdpSocket()
    {
        if (ok()) {
            ::closesocket(socket_);
        }
    }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&&) = delete;
    UdpSocket& operator=(UdpSocket&&) = delete;

    [[nodiscard]] bool ok() const { return std::cmp_not_equal(socket_, INVALID_SOCKET); }
    [[nodiscard]] SOCKET handle() const { return socket_; }
    [[nodiscard]] const sockaddr_in& address() const { return address_; }
    // A datagram's length (0: none waiting), its sender in `from`.
    int receive(std::span<uint8_t> buffer, sockaddr_in* from) const
    {
        int length = sizeof(*from);
        const int got = ::recvfrom(socket_, raw::chars(buffer.data()), static_cast<int>(buffer.size()), 0,
                                   raw::as<sockaddr>(from), &length);
        return got > 0 ? got : 0;
    }
    void send_to(std::span<const uint8_t> packet, const sockaddr_in& to) const
    {
        ::sendto(socket_, raw::chars(packet.data()), static_cast<int>(packet.size()), 0, raw::as<sockaddr>(&to),
                 sizeof(to));
    }

private:
    SOCKET socket_;
    sockaddr_in address_{};
};

void pause_briefly()
{
    constexpr auto kPoll = std::chrono::milliseconds(5);
    std::this_thread::sleep_for(kPoll);
}

// --- NAT negotiation (natneg) through the glue: three fake matchup servers and the partner, all on the loopback ---

// natneg's packets: the magic (6 bytes), the version, the type, the cookie, then the type's fields.
constexpr std::size_t kNatType = 7;
constexpr std::size_t kNatFields = 12;
constexpr uint8_t kNatInit = 0;
constexpr uint8_t kNatInitAck = 1;
constexpr uint8_t kNatConnect = 5;
constexpr uint8_t kNatConnectAck = 6;
constexpr uint8_t kNatPing = 7;
constexpr uint8_t kNatReport = 13;
constexpr uint8_t kNatReportAck = 14;
constexpr std::size_t kNatPacketMax = 256;
constexpr uint16_t kMatchupPort = 27901;
constexpr int kNatCookie = 0x5E1F0C0A;

void on_nat_progress(int state, void* user_data)
{
    std::printf("nat progress: state %d\n", state);
    ++check_state().nat.progress_calls;
    check_state().nat.progress_word_kept =
        check_state().nat.progress_word_kept && *static_cast<const int*>(user_data) == kNatCookie;
}
// NAT negotiation's completion (gamespy.h's NatCompleted's arguments, in order).
struct NatCompletion {
    int result;
    uintptr_t socket;
    const void* address;
    int user_data;
};
void on_nat_completed(const NatCompletion& completion)
{
    const auto* remote = static_cast<const sockaddr_in*>(completion.address);
    std::printf("nat completed: result %d, user data %08x, remote %08lx\n", completion.result,
                static_cast<unsigned>(completion.user_data), ntohl(remote->sin_addr.s_addr));
    ++check_state().nat.completions;
    check_state().nat.result = completion.result;
    check_state().nat.socket = completion.socket;
    check_state().nat.user_data = completion.user_data;
    check_state().nat.port = ntohs(remote->sin_port);
}

// A natneg packet printed with what differs between runs masked: the machine's own address and the ports (INIT), the
// SDK's uninitialised bytes (CONNECT_ACK, REPORT: fields the SDK never sets), the partner's port (CONNECT_PING).
void print_nat_packet(const char* where, std::span<const uint8_t> packet)
{
    std::array<bool, kNatPacketMax> masked{};
    const uint8_t type = packet.size() > kNatType ? core::at(packet, kNatType) : 0xFF;
    auto mask = [&masked](std::size_t from, std::size_t to) {
        for (std::size_t at = from; at < to; ++at) {
            core::at(masked, at) = true;
        }
    };
    constexpr std::size_t kInitAddress = kNatFields + 3;
    constexpr std::size_t kAddressAndPort = 6;
    constexpr std::size_t kPingPort = kNatFields + 4;
    constexpr std::size_t kPortSize = 2;
    if (type == kNatInit) {
        mask(kInitAddress, kInitAddress + kAddressAndPort);
    } else if (type == kNatConnectAck) {
        mask(kNatFields, kNatFields + 1);
        mask(kNatFields + 2, kInitAddress + kAddressAndPort);
    } else if (type == kNatPing) {
        mask(kPingPort, kPingPort + kPortSize);
    } else if (type == kNatReport) {
        mask(kNatFields, kNatFields + 1);
    }
    std::printf("nat %s: type %u, %zu bytes: %s\n", where, type, packet.size(), hex(packet, masked).c_str());
}

struct NatServers {
    UdpSocket matchup1{Endpoint{.ip = kLoopback, .port = kMatchupPort}};
    UdpSocket matchup2{Endpoint{.ip = kLoopback + 1, .port = kMatchupPort}};
    UdpSocket matchup3{Endpoint{.ip = kLoopback + 2, .port = kMatchupPort}};
    UdpSocket game{Endpoint{}};
    UdpSocket partner{Endpoint{}};
    int init_acks = 0;
    bool connect_sent = false;
    sockaddr_in negotiator{};
};

// One fake matchup server's step: an INIT acknowledged, then (after all four) the partner's address sent as CONNECT
// to the negotiation socket; a REPORT acknowledged.
void serve_matchup(NatServers& servers, const UdpSocket& server, const char* name)
{
    std::array<uint8_t, kNatPacketMax> packet{};
    sockaddr_in from{};
    const int got = server.receive(packet, &from);
    if (std::cmp_less_equal(got, kNatType)) {
        return;
    }
    const std::span<uint8_t> received(packet.data(), static_cast<std::size_t>(got));
    print_nat_packet(name, received);
    const uint8_t type = core::at(received, kNatType);
    if (type == kNatInit) {
        constexpr std::size_t kPortType = kNatFields;
        constexpr uint8_t kNn1 = 1;
        if (core::at(received, kPortType) == kNn1) {
            servers.negotiator = from;
        }
        core::at(received, kNatType) = kNatInitAck;
        server.send_to(received, from);
        ++servers.init_acks;
    } else if (type == kNatReport) {
        core::at(received, kNatType) = kNatReportAck;
        server.send_to(received, from);
    }
    constexpr int kInits = 4; // (GP from the game's socket, NN1-3 from the negotiation socket)
    if (servers.init_acks == kInits && !servers.connect_sent && servers.negotiator.sin_port != 0) {
        constexpr std::size_t kConnectSize = kNatFields + 8;
        std::array<uint8_t, kConnectSize> connect{};
        std::ranges::copy(received.first(kNatFields), connect.begin());
        core::at(connect, kNatType) = kNatConnect;
        const sockaddr_in& partner = servers.partner.address();
        // (the partner's address and port as they are, network order)
        const std::span<const uint8_t> ip(raw::bytes(&partner.sin_addr), sizeof(partner.sin_addr));
        const std::span<const uint8_t> port(raw::bytes(&partner.sin_port), sizeof(partner.sin_port));
        std::ranges::copy(ip, std::span(connect).subspan(kNatFields).begin());
        std::ranges::copy(port, std::span(connect).subspan(kNatFields + ip.size()).begin());
        servers.matchup1.send_to(connect, servers.negotiator);
        servers.connect_sent = true;
    }
}

// The partner answers each ping, saying it has the negotiator's data (its "finished" echoed: once the negotiator is
// finished, the pings stop).
void serve_partner(const NatServers& servers)
{
    std::array<uint8_t, kNatPacketMax> packet{};
    sockaddr_in from{};
    const int got = servers.partner.receive(packet, &from);
    if (std::cmp_less_equal(got, kNatType)) {
        return;
    }
    const std::span<uint8_t> received(packet.data(), static_cast<std::size_t>(got));
    print_nat_packet("partner", received);
    constexpr std::size_t kGotYourData = kNatFields + 6;
    if (core::at(received, kNatType) == kNatPing && received.size() > kGotYourData) {
        core::at(received, kGotYourData) = 1;
        servers.partner.send_to(received, from);
    }
}

// What reaches the game's socket goes to the SDK, as the game's own receive loop does (NNProcessData).
void serve_game_socket(const NatServers& servers)
{
    std::array<uint8_t, kNatPacketMax> packet{};
    sockaddr_in from{};
    const int got = servers.game.receive(packet, &from);
    if (got > 0) {
        game_spy::nat_process_data(raw::chars(packet.data()), got, &from);
    }
}

void check_nat_negotiation()
{
    // (the matchup servers' names as addresses: no lookups)
    constexpr std::size_t kHostSize = 16;
    static std::array<char, kHostSize> matchup1{{"127.0.0.1"}};
    static std::array<char, kHostSize> matchup2{{"127.0.0.2"}};
    static std::array<char, kHostSize> matchup3{{"127.0.0.3"}};
    Matchup1Hostname = matchup1.data();
    Matchup2Hostname = matchup2.data();
    Matchup3Hostname = matchup3.data();

    NatServers servers;
    if (!servers.matchup1.ok() || !servers.matchup2.ok() || !servers.matchup3.ok() || !servers.game.ok() ||
        !servers.partner.ok()) {
        std::printf("nat: a loopback socket couldn't be made (port %u in use?)\n", kMatchupPort);
        check_state().no_socket = true;
        return;
    }
    const auto game_socket = static_cast<uintptr_t>(servers.game.handle());
    const int error = game_spy::nat_begin_with_socket(
        game_socket, kNatCookie, 0, &on_nat_progress,
        &core::gathered<NatCompletion, &on_nat_completed, void, int, uintptr_t, const void*, int>, kNatCookie);
    std::printf("nat begin: %d\n", error);
    expect(error == 0, "NNBeginNegotiationWithSocket");
    // (the SDK waits FINISHED_IDLE_TIME, 5 s, after the pings before its report)
    constexpr auto kTimeout = std::chrono::seconds(15);
    const auto give_up = std::chrono::steady_clock::now() + kTimeout;
    while (check_state().nat.completions == 0 && std::chrono::steady_clock::now() < give_up) {
        serve_matchup(servers, servers.matchup1, "matchup1");
        serve_matchup(servers, servers.matchup2, "matchup2");
        serve_matchup(servers, servers.matchup3, "matchup3");
        serve_partner(servers);
        serve_game_socket(servers);
        game_spy::nat_think();
        pause_briefly();
    }
    game_spy::nat_free_negotiations();
    expect(check_state().nat.completions == 1, "one NAT completion");
    expect(check_state().nat.result == 0, "the negotiation succeeded (nr_success)");
    expect(check_state().nat.socket == game_socket, "the completion's socket is the game's (pointer-sized)");
    expect(check_state().nat.user_data == kNatCookie, "the completion's user word is the cookie");
    expect(check_state().nat.port == ntohs(servers.partner.address().sin_port),
           "the completion's address is the partner's");
    expect(check_state().nat.progress_calls > 0 && check_state().nat.progress_word_kept,
           "the progress callback's user word");
}

// --- qr2: a heartbeat to a fake master and the answer to a query, on the loopback ---

constexpr uint16_t kMasterPort = 27900;
constexpr std::size_t kQr2PacketMax = 1400; // (qr2.c's MAX_DATA_SIZE)
constexpr int kQueryPort = 46150;
// (qr2regkeys.h's ids)
constexpr int kKeyHostname = HOSTNAME_KEY;
constexpr int kKeyNumPlayers = NUMPLAYERS_KEY;
constexpr int kKeyMaxPlayers = MAXPLAYERS_KEY;
constexpr int kKeyGameMode = GAMEMODE_KEY;
constexpr int kKeyPlayer = PLAYER__KEY;
constexpr int kKeyScore = SCORE__KEY;
constexpr int kPlayers = 2;
constexpr int kMaxPlayers = 8;
constexpr int kScoreStep = 1000;

void note_user_data(void* user_data)
{
    check_state().qr2.user_data_kept = check_state().qr2.user_data_kept && user_data == &check_state().qr2;
}

void qr2_server_key(int key, qr2_buffer_t buffer, void* user_data)
{
    note_user_data(user_data);
    if (key == kKeyHostname) {
        game_spy::call_qr2_buffer_add(buffer, "x9 check host");
    } else if (key == kKeyNumPlayers) {
        game_spy::get_qr2_buffer_add_int(buffer, kPlayers);
    } else if (key == kKeyMaxPlayers) {
        game_spy::get_qr2_buffer_add_int(buffer, kMaxPlayers);
    } else if (key == kKeyGameMode) {
        game_spy::call_qr2_buffer_add(buffer, "openplaying");
    } else {
        game_spy::call_qr2_buffer_add(buffer, "");
    }
}
// A player key asked for (qr2_playerteamkeycallback_t's arguments, in order).
struct PlayerKeyQuery {
    int key;
    int index;
    qr2_buffer_t buffer;
    void* user_data;
};
void qr2_player_key(const PlayerKeyQuery& query)
{
    note_user_data(query.user_data);
    if (query.key == kKeyPlayer) {
        game_spy::call_qr2_buffer_add(query.buffer, query.index == 0 ? "skater one" : "skater two");
    } else if (query.key == kKeyScore) {
        game_spy::get_qr2_buffer_add_int(query.buffer, (query.index + 1) * kScoreStep);
    } else {
        game_spy::call_qr2_buffer_add(query.buffer, "");
    }
}
void qr2_team_key(int /*key*/, int /*index*/, qr2_buffer_t buffer, void* user_data)
{
    note_user_data(user_data);
    game_spy::call_qr2_buffer_add(buffer, "");
}
void qr2_key_list(qr2_key_type type, qr2_keybuffer_t keys, void* user_data)
{
    note_user_data(user_data);
    if (type == key_server) {
        for (const int key : {kKeyHostname, kKeyNumPlayers, kKeyMaxPlayers, kKeyGameMode}) {
            game_spy::qr2_key_buffer_add(keys, key);
        }
    } else if (type == key_player) {
        game_spy::qr2_key_buffer_add(keys, kKeyPlayer);
        game_spy::qr2_key_buffer_add(keys, kKeyScore);
    }
}
int qr2_count(qr2_key_type type, void* user_data)
{
    note_user_data(user_data);
    return type == key_player ? kPlayers : 0;
}
void qr2_add_error(qr2_error_t error, gsi_char* message, void* user_data)
{
    note_user_data(user_data);
    std::printf("qr2 add error %d: %s\n", static_cast<int>(error), message);
    ++check_state().qr2.errors;
}

// A heartbeat printed as its type, the instance key (masked: random) and its "key\0value\0" pairs, the machine's own
// addresses and the query port masked.
void print_heartbeat(std::span<const uint8_t> packet)
{
    constexpr std::size_t kHeader = 5; // (type, instance key)
    std::printf("qr2 heartbeat: type %u, %zu bytes:", core::at(packet, 0), packet.size());
    std::string_view rest(raw::chars(packet.data()), packet.size());
    rest.remove_prefix(kHeader);
    bool masked_value = false;
    bool is_key = true;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\0');
        const std::string_view field = rest.substr(0, end);
        if (is_key) {
            masked_value = field.starts_with("localip") || field == "localport";
        }
        const std::string shown(is_key || !masked_value ? field : std::string_view("<masked>"));
        std::printf(" %s", shown.c_str());
        is_key = !is_key;
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
    }
    std::printf("\n");
}

void check_qr2()
{
    static constexpr std::string_view kMaster = "127.0.0.1";
    std::ranges::copy(kMaster, std::begin(qr2_hostname)); // (the master by address: no lookup)
    const UdpSocket master(Endpoint{.ip = kLoopback, .port = kMasterPort});
    const UdpSocket client(Endpoint{});
    if (!master.ok() || !client.ok()) {
        std::printf("qr2: a loopback socket couldn't be made (port %u in use?)\n", kMasterPort);
        check_state().no_socket = true;
        return;
    }
    qr2_t qrec = nullptr;
    const qr2_error_t error =
        qr2_init(&qrec, "127.0.0.1", kQueryPort, "x9check", "secret", 1, 0, &qr2_server_key,
                 &core::gathered<PlayerKeyQuery, &qr2_player_key, void, int, int, qr2_buffer_t, void*>, &qr2_team_key,
                 &qr2_key_list, &qr2_count, &qr2_add_error, &check_state().qr2);
    std::printf("qr2 init: %d\n", static_cast<int>(error));
    expect(error == e_qrnoerror, "qr2_init");
    if (error != e_qrnoerror) {
        return;
    }
    sockaddr_in query_address{};
    query_address.sin_family = AF_INET;
    query_address.sin_addr.s_addr = htonl(kLoopback);
    std::array<uint8_t, kQr2PacketMax> packet{};
    bool heartbeat = false;
    bool answered = false;
    constexpr auto kTimeout = std::chrono::seconds(5);
    const auto give_up = std::chrono::steady_clock::now() + kTimeout;
    while (!answered && std::chrono::steady_clock::now() < give_up) {
        game_spy::get_qr2_think(qrec);
        sockaddr_in from{};
        const int got = master.receive(packet, &from);
        if (got > 0 && !heartbeat) {
            heartbeat = true;
            print_heartbeat({packet.data(), static_cast<std::size_t>(got)});
            // (the query port the SDK bound: the heartbeat came from it)
            query_address.sin_port = from.sin_port;
            // a query for every key: magic FE FD, QUERY (0), the request key, then "all" for each key list
            static constexpr std::array<uint8_t, 10> kQuery{
                {0xFE, 0xFD, 0x00, 0x01, 0x02, 0x03, 0x04, 0xFF, 0xFF, 0xFF}};
            client.send_to(kQuery, query_address);
        }
        const int answer = heartbeat ? client.receive(packet, &from) : 0;
        if (answer > 0) {
            answered = true;
            std::printf("qr2 query answer: %d bytes: %s\n", answer,
                        hex({packet.data(), static_cast<std::size_t>(answer)}).c_str());
        }
        pause_briefly();
    }
    game_spy::get_qr2_shutdown(qrec); // (its last heartbeat, "statechanged 2", goes to the fake master)
    expect(heartbeat, "a qr2 heartbeat");
    expect(answered, "the query's answer");
    expect(check_state().qr2.user_data_kept, "the qr2 callbacks' user data");
    expect(check_state().qr2.errors == 0, "no qr2 error");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    WSADATA winsock{};
    constexpr WORD kWinsock22 = 0x0202;
    if (WSAStartup(kWinsock22, &winsock) != 0) {
        std::printf("Winsock didn't start\n");
        return 2;
    }
    std::printf("thug2_gamespy_check (%zu-bit)\n", sizeof(void*) * CHAR_BIT);
    // (the backend taken as available - peerInitialize and natneg ask - and the game name the availability check
    // would have kept: no check is made)
    __GSIACResult = GSIACAvailable;
    constexpr std::string_view kGameName = "x9check";
    std::ranges::copy(kGameName, std::begin(__GSIACGamename));
    check_helpers();
    check_peer_callbacks();
    check_nat_negotiation();
    check_qr2();
    WSACleanup();
    std::printf("%s: %d failed\n", check_state().failures == 0 && !check_state().no_socket ? "ok" : "FAILED",
                check_state().failures);
    if (check_state().no_socket) {
        return 2;
    }
    return check_state().failures == 0 ? 0 : 1;
}
