// Self-tests for THUGPRO's compression (LZHAM, method 3 - game/net/net_lzham.h, register TP-R11) and the bounds on
// received compressed messages (App::HandleCompressedMessages 0x0045ABF0): LZHAM round trips with THUGPRO's
// parameters, a fixed stream (the parameters can't drift unnoticed), malformed streams, CompressMessage's method 3
// (and LZSS going as LZHAM in the peer mode), and every malformed compressed message dropped.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_app.h"
#include "game/net/net_compress.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lzham.h"
#include "game/net/thugpro_peer.h"
#include "game/util/lzss.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

namespace TP = GameNet::Thugpro;
using TestStandIns::AddressOf;
using TestStandIns::HashBytes;
using TestStandIns::Regs;
using TestStandIns::StandIns;

std::string Hex(const void* data, size_t size)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < size; ++i) {
        std::snprintf(b, sizeof(b), "%02X", static_cast<const uint8_t*>(data)[i]);
        out += b;
    }
    return out;
}

std::vector<uint8_t> Pattern(size_t size, uint32_t seed, bool repetitive)
{
    std::vector<uint8_t> out(size);
    for (size_t i = 0; i < size; ++i) {
        seed = seed * 1103515245u + 12345u;
        out[i] = repetitive ? static_cast<uint8_t>("THUG2 appearance "[i % 17] ^ ((i / 200) & 3))
                            : static_cast<uint8_t>(seed >> 16);
    }
    return out;
}

// --- LZHAM ---

// The stream THUGPRO's parameters make of "THUGPRO THUGPRO THUGPRO ..." (40 bytes): computed once with this codec and
// these parameters; a change to either (the dictionary, the level, the table settings) changes it.
constexpr char kGoldenInput[] = "THUGPRO THUGPRO THUGPRO THUGPRO THUGPRO ";
const char* const kGoldenStream = "406CA20005448554750524F200056068B0C0ED990B6E";

void TestThugproLzham()
{
    // Round trips: empty-ish, tiny, a connection request's size, repetitive and random appearances, the most a
    // message holds.
    for (size_t size : {size_t(1), size_t(3), size_t(48), size_t(124), size_t(700), size_t(4096 + 0x78), size_t(0xFFFF)}) {
        for (bool repetitive : {true, false}) {
            std::vector<uint8_t> in = Pattern(size, static_cast<uint32_t>(size), repetitive);
            std::vector<uint8_t> packed(size * 2 + 256);
            size_t packedSize = Net::Lzham::Compress(in.data(), in.size(), packed.data(), packed.size());
            std::vector<uint8_t> out(size + 16, 0xCD);
            size_t outSize = 0;
            bool ok = packedSize != 0 &&
                      Net::Lzham::Decompress(packed.data(), packedSize, out.data(), size, &outSize) && outSize == size &&
                      std::memcmp(out.data(), in.data(), size) == 0 && out[size] == 0xCD;
            char what[96];
            std::snprintf(what, sizeof(what), "LZHAM: %u %s bytes round-trip (THUGPRO's parameters)",
                          static_cast<unsigned>(size), repetitive ? "repetitive" : "random");
            Check(ok, what);
            if (repetitive && size >= 700)
                Check(packedSize < size / 4, "LZHAM: repetitive data compresses");
        }
    }

    // The fixed stream.
    {
        uint8_t packed[256];
        size_t packedSize = Net::Lzham::Compress(reinterpret_cast<const uint8_t*>(kGoldenInput), sizeof(kGoldenInput) - 1,
                                                 packed, sizeof(packed));
        std::string hex = Hex(packed, packedSize);
        if (kGoldenStream[0] == '\0')
            Log("LZHAM golden stream: %s", hex.c_str());
        Check(hex == kGoldenStream, "LZHAM: THUGPRO's parameters make the fixed stream");
        uint8_t out[64];
        size_t outSize = 0;
        std::vector<uint8_t> golden;
        for (size_t i = 0; kGoldenStream[i] && kGoldenStream[i + 1]; i += 2)
            golden.push_back(static_cast<uint8_t>(std::strtoul(std::string(kGoldenStream + i, 2).c_str(), nullptr, 16)));
        Check(!golden.empty() && Net::Lzham::Decompress(golden.data(), golden.size(), out, sizeof(out), &outSize) &&
                  outSize == sizeof(kGoldenInput) - 1 && std::memcmp(out, kGoldenInput, outSize) == 0,
              "LZHAM: the fixed stream expands");

        // Malformed: cut short anywhere, a changed byte (the stream's adler32 or its codes), too little room,
        // nothing at all - all refused, nothing written past the room given.
        int refused = 0, cases = 0;
        for (size_t cut = 0; cut < golden.size(); ++cut) {
            uint8_t room[64 + 8];
            std::memset(room, 0xEE, sizeof(room));
            size_t size = 12345;
            ++cases;
            if (!Net::Lzham::Decompress(golden.data(), cut, room, 64, &size) && size == 12345 && room[64] == 0xEE)
                ++refused;
        }
        for (size_t at = 0; at < golden.size(); ++at) {
            std::vector<uint8_t> bad = golden;
            bad[at] ^= 0x5A;
            uint8_t room[64 + 8];
            std::memset(room, 0xEE, sizeof(room));
            size_t size = 0;
            ++cases;
            bool ok = Net::Lzham::Decompress(bad.data(), bad.size(), room, 64, &size);
            if ((!ok || (size == outSize && std::memcmp(room, kGoldenInput, size) == 0)) && room[64] == 0xEE)
                ++refused; // (a change the codec can't see and that still expands right is harmless)
        }
        for (size_t roomSize : {size_t(0), size_t(1), outSize > 0 ? outSize - 1 : size_t(0)}) {
            std::vector<uint8_t> room(roomSize + 8, 0xEE);
            size_t size = 777;
            ++cases;
            if (!Net::Lzham::Decompress(golden.data(), golden.size(), room.data(), roomSize, &size) && size == 777 &&
                room[roomSize] == 0xEE)
                ++refused;
        }
        Check(refused == cases, "LZHAM: cut, changed and too-big streams refused, nothing written past the room");
        if (refused != cases)
            Log("  LZHAM malformed: %d of %d refused", refused, cases);
        Check(!Net::Lzham::Decompress(nullptr, 5, out, sizeof(out), &outSize) &&
                  !Net::Lzham::Decompress(golden.data(), 0, out, sizeof(out), &outSize),
              "LZHAM: no stream refused");
        // Compressing into too little room fails (0), not past it.
        uint8_t small[8 + 4];
        std::memset(small, 0xEE, sizeof(small));
        std::vector<uint8_t> noise = Pattern(500, 7, false);
        Check(Net::Lzham::Compress(noise.data(), noise.size(), small, 8) == 0 && small[8] == 0xEE,
              "LZHAM: compressing into too little room fails");
    }
}

// --- Compressed messages: CompressMessage's method 3 and HandleCompressedMessages' bounds ---

constexpr uint32_t kArenaSize = 0x80000;
constexpr uint32_t kHeapOffset = 0x1000;
uint8_t* g_arena;
std::vector<TestStandIns::Saved> g_saved;
bool g_swapped = false;
std::vector<std::string> g_dispatched;

void SetUpArena()
{
    std::memset(g_arena, 0, kHeapOffset);
    std::memset(g_arena + kHeapOffset, 0xCD, kArenaSize - kHeapOffset);
    GameVariable<bool>(0x007CC979) = true;
    auto* manager = reinterpret_cast<Mem::Manager*>(g_arena);
    GameVariable<void*>(0x007D1800) = manager;
    auto* region = new (g_arena + 0x600) Mem::Region(g_arena + kHeapOffset, g_arena + kArenaSize);
    auto* heap = new (g_arena + 0x620) Mem::Heap(region, 1, "default");
    manager->m_heaps[0] = heap;
    manager->m_heapCount = 1;
    manager->m_topDownHeap = heap;
    manager->PushContext(heap);
}

void CleanUpCompression()
{
    TestStandIns::RemoveStandIns();
    TP::SetPeerModeOverride(-1);
    if (g_swapped) {
        TestStandIns::RestoreAll(g_saved);
        g_swapped = false;
    }
    if (g_arena) {
        VirtualFree(g_arena, 0, MEM_RELEASE);
        g_arena = nullptr;
    }
}

// One compressed message (0x12) through ours: what was dispatched ("id length hash"), or the handler's result.
std::string Receive(const std::vector<uint8_t>& message, uint32_t length)
{
    static Net::App app; // (only its dispatcher's address is used: DispatchMsg is stood in)
    std::vector<uint8_t> buffer(message.size() + 64, 0xAB);
    std::memcpy(buffer.data(), message.data(), message.size());
    Net::MsgHandlerContext context;
    std::memset(&context, 0, sizeof(context));
    context.m_Msg = reinterpret_cast<char*>(buffer.data());
    context.m_MsgId = 0x12;
    context.m_MsgLength = length;
    context.m_App = &app;
    g_dispatched.clear();
    int result = Net::App::HandleCompressedMessages(&context);
    std::string out = g_dispatched.empty() ? "none" : g_dispatched[0];
    return out + " -> " + std::to_string(result);
}

std::vector<uint8_t> Header(uint8_t method, uint16_t size)
{
    return {method, static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8)};
}

void TestThugproCompressedMessages()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestThugproCompressedMessages skipped (the memory manager is already set up)");
        return;
    }
    g_saved = {{0x007CC979, 1, {}}, {0x007D1800, 4, {}}};
    TestStandIns::SaveAll(g_saved);
    g_swapped = true;
    g_arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* s = new StandIns;
    s->At({0x00401C30}, 0, [](Regs&) { return 0u; }); // the debug output
    s->At({0x0045A750, AddressOf(&Net::Dispatcher::DispatchMsg)}, 4, [](Regs& r) {
        auto* inner = reinterpret_cast<Net::MsgHandlerContext*>(r.args[0]);
        char line[64];
        std::snprintf(line, sizeof(line), "%02X %u %08X", inner->m_MsgId, inner->m_MsgLength,
                      HashBytes(reinterpret_cast<const uint8_t*>(inner->m_Msg), inner->m_MsgLength));
        g_dispatched.push_back(line);
        return 1u;
    });
    SetUpArena();

    // CompressMessage: method 3 makes an LZHAM message that expands back (and goes through the handler); LZSS goes
    // as LZHAM in the peer mode and stays LZSS without it; incompressible data stays uncompressed.
    std::vector<uint8_t> appearance = Pattern(1200, 3, true);
    auto compress = [&](char method, const std::vector<uint8_t>& data, uint8_t* id, uint16_t* length) {
        *id = 0x22;
        *length = static_cast<uint16_t>(data.size());
        return Net::CompressMessage(id, length, data.data(), method);
    };
    TP::SetPeerModeOverride(0);
    uint8_t id;
    uint16_t length;
    uint8_t* packed = compress(3, appearance, &id, &length);
    char expected[64];
    std::snprintf(expected, sizeof(expected), "22 1200 %08X -> 1", HashBytes(appearance.data(), appearance.size()));
    Check(packed && id == 0x12 && packed[0] == 3 && (packed[1] | (packed[2] << 8)) == 1203 && length < 400,
          "CompressMessage: method 3 is LZHAM (header 3, the expanded size)");
    if (packed) {
        std::vector<uint8_t> message(packed, packed + length);
        Check(Receive(message, length) == expected, "HandleCompressedMessages: an LZHAM message expands and dispatches");
        // The same stream cut short, or with a wrong expanded size: dropped.
        Check(Receive(message, length - 1) == "none -> 4", "HandleCompressedMessages: a cut LZHAM stream dropped");
        message[1] = static_cast<uint8_t>(message[1] + 1);
        Check(Receive(message, length) == "none -> 4", "HandleCompressedMessages: an LZHAM size mismatch dropped");
        Mem::Free(packed);
    }
    packed = compress(2, appearance, &id, &length);
    Check(packed && packed[0] == 2, "CompressMessage: LZSS stays LZSS outside the peer mode");
    if (packed) {
        std::vector<uint8_t> message(packed, packed + length);
        Check(Receive(message, length) == expected, "HandleCompressedMessages: an LZSS message expands and dispatches");
        Mem::Free(packed);
    }
    TP::SetPeerModeOverride(1);
    packed = compress(2, appearance, &id, &length);
    Check(packed && packed[0] == 3, "CompressMessage: LZSS goes as LZHAM in the peer mode (THUGPRO reads no LZSS)");
    if (packed)
        Mem::Free(packed);
    packed = compress(1, std::vector<uint8_t>(300, 0x41), &id, &length);
    Check(packed && packed[0] == 1, "CompressMessage: RLE stays RLE in the peer mode");
    if (packed)
        Mem::Free(packed);
    std::vector<uint8_t> noise = Pattern(60, 9, false);
    packed = compress(3, noise, &id, &length);
    Check(!packed && id == 0x22 && length == 60, "CompressMessage: LZHAM no smaller - sent uncompressed");
    std::vector<uint8_t> tiny = {1, 2};
    packed = compress(3, tiny, &id, &length);
    Check(!packed && length == 2, "CompressMessage: a tiny message's LZHAM stream doesn't fit - sent uncompressed");
    TP::SetPeerModeOverride(0);

    // HandleCompressedMessages' bounds (NOT ORIGINAL): every malformed message dropped (4), nothing dispatched.
    {
        // A valid RLE message (id 0x33, 4 bytes) for reference: literal of 7 bytes.
        std::vector<uint8_t> good = Header(1, 7);
        good.insert(good.end(), {uint8_t(6), uint8_t(0x33), uint8_t(4), uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4)});
        Check(Receive(good, static_cast<uint32_t>(good.size())).compare(0, 5, "33 4 ") == 0 &&
                  Receive(good, static_cast<uint32_t>(good.size())).find("-> 1") != std::string::npos,
              "HandleCompressedMessages: a good RLE message dispatched");
        Check(Receive(good, 2) == "none -> 4" && Receive(good, 0) == "none -> 4",
              "HandleCompressedMessages: shorter than its header - dropped");
        Check(Receive(Header(1, 2), 3) == "none -> 4", "HandleCompressedMessages: an expanded size under 3 - dropped");
        std::vector<uint8_t> cut = good;
        Check(Receive(cut, static_cast<uint32_t>(cut.size() - 1)) == "none -> 4",
              "HandleCompressedMessages: an RLE literal past the stream's end - dropped");
        std::vector<uint8_t> inner = good;
        inner[5] = 9; // the inner message's length: 9 > 7 - 3
        Check(Receive(inner, static_cast<uint32_t>(inner.size())) == "none -> 4",
              "HandleCompressedMessages: an inner length past the expanded data - dropped");
        std::vector<uint8_t> unknown = good;
        unknown[0] = 4;
        Check(Receive(unknown, static_cast<uint32_t>(unknown.size())) == "none -> 4" &&
                  (unknown[0] = 0, Receive(unknown, static_cast<uint32_t>(unknown.size())) == "none -> 4"),
              "HandleCompressedMessages: an unknown method - dropped (the original dispatched uninitialised memory)");
        // LZSS: a reference expanding past the size (18 bytes for 7), a reference cut after its first byte.
        std::vector<uint8_t> big = Header(2, 7);
        big.insert(big.end(), {uint8_t(0xFE), uint8_t(0x00), uint8_t(0x0F)});
        Check(Receive(big, static_cast<uint32_t>(big.size())) == "none -> 4",
              "HandleCompressedMessages: an LZSS stream expanding past its size - dropped (nothing written past it)");
        std::vector<uint8_t> half = Header(2, 7);
        half.insert(half.end(), {uint8_t(0x3F), uint8_t(0x33), uint8_t(4), uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(0x00)});
        Check(Receive(half, static_cast<uint32_t>(half.size())) == "none -> 4",
              "HandleCompressedMessages: an LZSS reference cut short - dropped");
        std::vector<uint8_t> lzss = Header(2, 7);
        lzss.insert(lzss.end(), {uint8_t(0x7F), uint8_t(0x33), uint8_t(4), uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4)});
        Check(Receive(lzss, static_cast<uint32_t>(lzss.size())).find("-> 1") != std::string::npos,
              "HandleCompressedMessages: a good LZSS message dispatched");
        std::vector<uint8_t> lzham = Header(3, 7);
        lzham.insert(lzham.end(), {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(5), uint8_t(6)});
        Check(Receive(lzham, static_cast<uint32_t>(lzham.size())) == "none -> 4",
              "HandleCompressedMessages: a bad LZHAM stream - dropped");
    }
    delete s;
    GameVariable<void*>(0x007D1800) = nullptr;
    CleanUpCompression();
}

} // namespace

SELFTEST(Early, TestThugproLzham);
SELFTEST(Early, TestThugproCompressedMessages, CleanUpCompression);
