// Self-tests for src/game/net/gamespy.h: pure functions of the exe's GameSpy SDK (2003-04) against IGN's 2007 release,
// which the standalone build links instead - and our FixNick (the game's nick rules, kept in the standalone build)
// against the exe's peerFixNick. Built into thug2_check only (it links the SDK; the hooked DLL never does), so they
// run offline at the Early stage, where the exe's functions that need its C runtime (sprintf...) can't run: the
// MD5 is compared below its hex printing, peerFixNick with a stand-in for its isdigit, and what goes through sprintf
// is compared by reading (docs/gamespy.md).
// What only the network can show (the protocols against the original, OpenSpy) is listed in docs/gamespy.md.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/net/gamespy.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/selftests_skater_physics_harness.h" // (CodePatch / JumpTo)

#pragma warning(push)
#pragma warning(disable : 4005) // (gsPlatform.h defines WIN32_LEAN_AND_MEAN again)
extern "C" {
#include "md5.h"
#include "Chat/chatMain.h"
#include "Peer/peer.h"
#include "qr2/qr2.h"
}
#pragma warning(pop)

namespace {

// --- MD5 (gcd_compute_response, GenerateAuth, GP's login proofs all hash with it) ---

// The exe's MD5 context: the RSA reference's MD5_CTX (state[4], count[2], buffer[64]), as the SDK's.
static_assert(sizeof(MD5_CTX) == 88, "MD5_CTX");

void ExeMd5(const uint8_t* data, unsigned length, uint8_t digest[16])
{
    MD5_CTX context = {};
    context.state[0] = 0x67452301; // (MD5Init, inlined in the exe's MD5Digest 0x00603280)
    context.state[1] = 0xEFCDAB89;
    context.state[2] = 0x98BADCFE;
    context.state[3] = 0x10325476;
    GameFunction<void(__cdecl*)(MD5_CTX*, const uint8_t*, unsigned)>(0x006030F0)(&context, data, length); // MD5Update
    GameFunction<void(__cdecl*)(uint8_t*, MD5_CTX*)>(0x006031B0)(digest, &context);                      // MD5Final
}

void SdkMd5(const uint8_t* data, unsigned length, uint8_t digest[16])
{
    MD5_CTX context;
    MD5Init(&context);
    MD5Update(&context, const_cast<uint8_t*>(data), length);
    MD5Final(digest, &context);
}

void TestGameSpyMd5()
{
    static const unsigned kLengths[] = {0, 1, 3, 55, 56, 57, 63, 64, 65, 119, 120, 128, 1000};
    uint8_t data[1000];
    uint32_t state = 0x1234567;
    for (uint8_t& b : data) {
        state = state * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(state >> 24);
    }
    int differ = 0;
    for (unsigned length : kLengths) {
        uint8_t a[16], b[16];
        ExeMd5(data, length, a);
        SdkMd5(data, length, b);
        if (std::memcmp(a, b, 16) != 0)
            ++differ;
    }
    // A known digest too: MD5("") = d41d8cd98f00b204e9800998ecf8427e (the SDK's MD5Digest prints it).
    char hex[33];
    MD5Digest(reinterpret_cast<unsigned char*>(const_cast<char*>("")), 0, hex);
    Log("gamespy: MD5 of %d inputs: %d differ from the 2007 SDK's", static_cast<int>(sizeof(kLengths) / sizeof(*kLengths)),
        differ);
    Check(differ == 0, "gamespy: the exe's MD5Update / MD5Final match the 2007 SDK's");
    Check(std::strcmp(hex, "d41d8cd98f00b204e9800998ecf8427e") == 0, "gamespy: the 2007 SDK's MD5Digest");
}
SELFTEST(Early, TestGameSpyMd5);

// --- peerFixNick ---

// The exe's isdigit (0x00625DAA) needs its C runtime's locale, not set up offline: a stand-in for the test (the
// "C" locale's answer - what the game always has).
int __cdecl StandInIsDigit(int c) { return c >= '0' && c <= '9'; }

void TestGameSpyFixNick()
{
    SkaterPhysicsTest::CodePatch isDigit(0x00625DAA,
                                         SkaterPhysicsTest::JumpTo(0x00625DAA, reinterpret_cast<void*>(&StandInIsDigit)));
    static const char* const kNicks[] = {
        "Skater", "1Skater", "-dash", "_under", "a b c", "Tony Hawk!", "nick@home", "[THUG]{2}|^`\\-_",
        "123456", "", "@op", "#chan", "+v", ":colon", "abcdefghijklmnopqrstuvwxyz0123456789", "a1234567890_Kevin",
        "dots.and,commas", "tab\there", "~tilde",
    };
    int ours = 0, sdk = 0;
    for (const char* nick : kNicks) {
        char exe[128], mine[128], s[128];
        GameFunction<void(__cdecl*)(char*, const char*)>(0x005FD9F0)(exe, nick);
        GameSpy::FixNick(mine, nick);
        peerFixNick(s, nick);
        if (std::strcmp(exe, mine) != 0) {
            ++ours;
            Log("gamespy: FixNick(\"%s\"): exe \"%s\", ours \"%s\"", nick, exe, mine);
        }
        if (std::strcmp(exe, s) != 0) {
            ++sdk;
            Log("gamespy: (2007 peerFixNick differs) \"%s\": exe \"%s\", 2007 \"%s\"", nick, exe, s);
        }
    }
    Log("gamespy: FixNick over %d nicks: ours differs from the exe's for %d, the 2007 SDK's for %d",
        static_cast<int>(sizeof(kNicks) / sizeof(*kNicks)), ours, sdk);
    Check(ours == 0, "gamespy: FixNick = the exe's peerFixNick");
}
SELFTEST(Early, TestGameSpyFixNick);

// --- nick limits ---

// The game's SDK sends any nick that is not empty (the peer keeps 63 characters); the 2007 SDK, patched by
// cmake/gamespy_sdk.cmake, must not refuse a long nick (PEER_NICK_TOO_LONG, which the game's nick-error callback
// ignores: the connect would stall) or a character the game's rules allow.
void TestGameSpyNickLimits()
{
    char nick[80];
    std::memset(nick, 'n', sizeof(nick));
    nick[20] = '\0';
    Check(ciNickIsValid(nick) == CHAT_NICK_OK, "gamespy: a 20-character nick is valid");
    nick[20] = 'n';
    nick[21] = '\0';
    Check(ciNickIsValid(nick) == CHAT_NICK_OK, "gamespy: a 21-character nick is valid (the 2007 SDK refused it)");
    nick[63] = '\0';
    Check(ciNickIsValid(nick) == CHAT_NICK_OK, "gamespy: a 63-character nick (the peer's most) is valid");
    Check(ciNickIsValid("a1234567890_12345678901") == CHAT_NICK_OK, "gamespy: a retry nick a<time>_<id> is valid");
    Check(ciNickIsValid("back\\slash[]`_^{|}-") == CHAT_NICK_OK, "gamespy: the game's allowed characters are valid");
    Check(ciNickIsValid("") != CHAT_NICK_OK, "gamespy: an empty nick is not valid");
}
SELFTEST(Early, TestGameSpyNickLimits);

// --- qr2's reply buffers (what the game's key callbacks write into) ---

// The exe's qr2_buffer: 0x800 bytes, the length at +0x800; its key buffer: the keys, the count at +0x100.
// The 2007 SDK's (qr2.c, opaque in its header): 1400 bytes (MAX_DATA_SIZE), the length after them; 254 keys, the
// count after them (+0x100 too, by alignment).
constexpr int kExeBufferSize = 0x800;
constexpr int kSdkBufferSize = 1400;

void TestGameSpyQr2Buffers()
{
    alignas(4) static uint8_t exeBuffer[kExeBufferSize + 4];
    alignas(4) static uint8_t sdkBuffer[kSdkBufferSize + 4];
    std::memset(exeBuffer, 0, sizeof(exeBuffer));
    std::memset(sdkBuffer, 0, sizeof(sdkBuffer));
    static const char* const kValues[] = {"thps6pc", "", "Boston", "a value with spaces", "1", "\\slashes\\"};
    for (int round = 0; round < 20; ++round)
        for (const char* value : kValues) {
            GameFunction<void(__cdecl*)(void*, const char*)>(0x005FA010)(exeBuffer, value); // qr2_buffer_add
            qr2_buffer_add(reinterpret_cast<qr2_buffer_t>(sdkBuffer), value);
        }
    int exeLength, sdkLength;
    std::memcpy(&exeLength, exeBuffer + kExeBufferSize, 4);
    std::memcpy(&sdkLength, sdkBuffer + kSdkBufferSize, 4);
    Log("gamespy: qr2 buffers: %d bytes written by the exe's, %d by the 2007 SDK's", exeLength, sdkLength);
    Check(exeLength == sdkLength && exeLength < kSdkBufferSize && std::memcmp(exeBuffer, sdkBuffer, exeLength) == 0,
          "gamespy: qr2_buffer_add writes what the exe's does");

    alignas(4) static uint8_t exeKeys[0x104];
    alignas(4) static uint8_t sdkKeys[0x104];
    std::memset(exeKeys, 0, sizeof(exeKeys));
    std::memset(sdkKeys, 0, sizeof(sdkKeys));
    static const int kKeyIds[] = {0, 1, 2, 50, 253, 254, 255, 256, -1};
    for (int round = 0; round < 40; ++round) // (past the 254 keys either holds)
        for (int key : kKeyIds) {
            GameFunction<void(__cdecl*)(void*, int)>(0x005F9FE0)(exeKeys, key); // qr2_keybuffer_add
            qr2_keybuffer_add(reinterpret_cast<qr2_keybuffer_t>(sdkKeys), key);
        }
    int exeCount, sdkCount;
    std::memcpy(&exeCount, exeKeys + 0x100, 4);
    std::memcpy(&sdkCount, sdkKeys + 0x100, 4);
    Log("gamespy: qr2 key buffers: %d keys kept by the exe's, %d by the 2007 SDK's", exeCount, sdkCount);
    Check(exeCount == sdkCount && std::memcmp(exeKeys, sdkKeys, 0xFE) == 0,
          "gamespy: qr2_keybuffer_add keeps the keys the exe's does");
}
SELFTEST(Early, TestGameSpyQr2Buffers);

// --- the layouts the game builds for the SDK ---

void TestGameSpyLayouts()
{
    // The game fills PEERCallbacks by offset (LobbyMan::InitPeer): the 2007 SDK's fields at the same offsets.
    struct Field {
        size_t sdk, game;
        const char* name;
    };
    const Field kFields[] = {
        {offsetof(PEERCallbacks, disconnected), 0x00, "disconnected"},
        {offsetof(PEERCallbacks, roomMessage), 0x04, "roomMessage"},
        {offsetof(PEERCallbacks, playerMessage), 0x14, "playerMessage"},
        {offsetof(PEERCallbacks, playerJoined), 0x24, "playerJoined"},
        {offsetof(PEERCallbacks, playerLeft), 0x28, "playerLeft"},
        {offsetof(PEERCallbacks, newPlayerList), 0x30, "newPlayerList"},
        {offsetof(PEERCallbacks, playerInfo), 0x38, "playerInfo"},
        {offsetof(PEERCallbacks, roomKeyChanged), 0x4C, "roomKeyChanged"},
        {offsetof(PEERCallbacks, qrServerKey), 0x50, "qrServerKey"},
        {offsetof(PEERCallbacks, qrPlayerKey), 0x54, "qrPlayerKey"},
        {offsetof(PEERCallbacks, qrKeyList), 0x5C, "qrKeyList"},
        {offsetof(PEERCallbacks, qrCount), 0x60, "qrCount"},
        {offsetof(PEERCallbacks, qrAddError), 0x64, "qrAddError"},
        {offsetof(PEERCallbacks, qrNatNegotiateCallback), 0x68, "qrNatNegotiateCallback"},
        {offsetof(PEERCallbacks, qrPublicAddressCallback), 0x6C, "qrPublicAddressCallback"},
        {offsetof(PEERCallbacks, param), 0x70, "param"},
    };
    bool ok = sizeof(PEERCallbacks) == sizeof(GameSpy::PeerCallbacks);
    for (const Field& f : kFields)
        if (f.sdk != f.game) {
            Log("gamespy: PEERCallbacks.%s at +0x%X, the game fills +0x%X", f.name, static_cast<unsigned>(f.sdk),
                static_cast<unsigned>(f.game));
            ok = false;
        }
    Check(ok, "gamespy: PEERCallbacks as the game fills it");
}
SELFTEST(Early, TestGameSpyLayouts);

} // namespace
