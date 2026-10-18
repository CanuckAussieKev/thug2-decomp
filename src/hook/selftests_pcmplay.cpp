// Self-tests for the PC's streamed audio's platform layer (src/game/pcm/stream.cpp, stream_play.cpp): the slots,
// the stream thread, start-up and pcm.wad's index, the OVERLAPPED ring, pausing, freeing what finished, the user
// soundtracks, and loading / starting / stopping / the volumes of the music and the streams. The original and
// ours run against recording stand-ins (standins.h) for the stream classes' methods, the memory manager, Win32,
// binkw32 and the C runtime, and the traces and memory they change are compared.

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/movie.h"
#include "game/pcm/stream.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Pcm::BinkStream;
using Pcm::StreamSlot;
using Pcm::WadStream;

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// The memory the stream objects are allocated from (a bump allocator; both runs use the same memory).
alignas(16) uint8_t g_heap[0x4000];
uint32_t g_heapUsed;
bool g_allocFails;
uint32_t Rel(uint32_t p)
{
    auto base = reinterpret_cast<uint32_t>(g_heap);
    return p >= base && p < base + sizeof(g_heap) ? 0xA0000000u + (p - base) : p;
}

struct FakeBink {
    uint32_t width, height, frames, frameNum, lastFrameNum, frameRate, frameRateDiv;
    uint8_t rest[0x40];
};
FakeBink g_bink;

// Scripted results.
uint32_t g_openResult;     // the streams' opens
uint32_t g_waitResult;     // WaitForSingleObject
int g_sleepsLeft;          // Sleep: after this many the thread is told to stop
float g_soundVolume;
std::vector<uint8_t> g_indexFile; // pcm.dat
size_t g_indexAt;
bool g_indexOpens;

float __fastcall FakeSoundVolume(void*)
{
    Record({0x50D});
    return g_soundVolume;
}

void StandInPlatform(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 0xC, [](Regs& r) {
        Record({0xA110, r.args[0], r.args[1], r.args[2]});
        uint32_t at = (g_heapUsed + 15) & ~15u;
        if (g_allocFails || at + r.args[0] > sizeof(g_heap))
            return 0u;
        g_heapUsed = at + r.args[0];
        std::memset(g_heap + at, 0xCD, r.args[0]);
        return reinterpret_cast<uint32_t>(g_heap + at);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004E1950, AddressOf(&Mem::EnsureManager)}, 0, [](Regs&) {
        Record({0xE45});
        return 0u;
    });
    // The stream classes.
    s.At({0x00452C00}, 8, [](Regs& r) {
        Record({0xC7B, Rel(r.ecx), r.args[0] & 0xFF, r.args[1]});
        reinterpret_cast<BinkStream*>(r.ecx)->m_unknown6B = true; // (as the constructor)
        return r.ecx;
    });
    s.At({0x00452C60}, 0x10, [](Regs& r) {
        Record({0x0B0, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3]});
        return g_openResult;
    });
    s.At({0x00454340, AddressOf(&WadStream::Construct)}, 4, [](Regs& r) {
        Record({0xC7A, Rel(r.ecx), r.args[0] & 0xFF});
        reinterpret_cast<WadStream*>(r.ecx)->m_unknown5F = true;
        return r.ecx;
    });
    s.At({0x00454450, AddressOf(&WadStream::OpenFromWad)}, 0x10, [](Regs& r) {
        Record({0x0A0, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
        return g_openResult;
    });
    s.At({0x00454490, AddressOf(&WadStream::Open)}, 8, [](Regs& r) {
        Record({0x0A1, Rel(r.ecx), r.args[0], r.args[1]});
        return g_openResult;
    });
    s.At({0x004543F0, AddressOf(&WadStream::PreloadDone)}, 0, [](Regs& r) {
        Record({0x9E, Rel(r.ecx)});
        return reinterpret_cast<WadStream*>(r.ecx)->m_unknown04 & 1;
    });
    s.At({0x00454430, AddressOf(&WadStream::Pause)}, 4, [](Regs& r) {
        Record({0x9A0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00452DC0, AddressOf(&BinkStream::Pause)}, 4, [](Regs& r) {
        Record({0x9A1, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00452DE0, AddressOf(&BinkStream::SetVolume)}, 4, [](Regs& r) {
        Record({0x701, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00452FC0, AddressOf(&BinkStream::SetVolumeAndPan)}, 8, [](Regs& r) {
        Record({0x702, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00452E30, AddressOf(&BinkStream::Destruct)}, 0, [](Regs& r) {
        Record({0xD7, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00452EA0, AddressOf(&BinkStream::Update)}, 0, [](Regs& r) {
        Record({0x0D, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00452D20, AddressOf(&BinkStream::Open)}, 4, [](Regs& r) {
        Record({0x0BE, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_openResult;
    });
    // Elsewhere in the game.
    s.At({0x005B6250}, 0, [](Regs&) {
        Record({0x5B62});
        return reinterpret_cast<uint32_t>("ROOT\\");
    });
    s.At({0x0047C790}, 0, [](Regs& r) {
        Record({0x47C7, r.args[0], r.args[1]});
        return g_openResult == 0 ? 0x4321u : 0xFFFFFFFFu;
    });
    s.At({0x0047BA60}, 0, [](Regs& r) {
        Record({0xBA60, r.args[0] & 0xFF});
        return 0x50C0u;
    });
    s.At({0x0047C6C0}, 0, [](Regs&) {
        Record({0xC6C0});
        return 0u;
    });
    s.Bytes(0x0047AFD0, Branch(0xE9, 0x0047AFD0, reinterpret_cast<const void*>(&FakeSoundVolume)));
    s.At({0x006234FE}, 0, [](Regs& r) { // sprintf
        return StandInSprintf(r); // (ours uses its own C runtime: the paths are compared where they're used)
    });
    s.At({0x00624200}, 0, [](Regs& r) { // strrchr
        return reinterpret_cast<uint32_t>(std::strrchr(reinterpret_cast<const char*>(r.args[0]), static_cast<int>(r.args[1])));
    });
    s.Bytes(0x006452F4, Dword(reinterpret_cast<uint32_t>(s.Thunk(12, [](Regs& r) { // BinkGoto
        Record({0x6070, r.args[0] == reinterpret_cast<uint32_t>(&g_bink), r.args[1], r.args[2]});
        return 0u;
    }))));
    // Win32.
    StandInApi(s, 0x006450BC, reinterpret_cast<const void*>(&WaitForSingleObject), 8, [](Regs& r) {
        Record({0x3A17, r.args[0], r.args[1]});
        return g_waitResult;
    });
    StandInApi(s, 0x006450C0, reinterpret_cast<const void*>(&ReleaseMutex), 4, [](Regs& r) {
        Record({0x4E1, r.args[0]});
        return 1u;
    });
    StandInApi(s, 0x006450A8, reinterpret_cast<const void*>(&Sleep), 4, [](Regs& r) {
        Record({0x5EE, r.args[0]});
        if (--g_sleepsLeft <= 0)
            GameVariable<bool>(0x006EE018) = true;
        return 0u;
    });
    StandInApi(s, 0x006450D0, reinterpret_cast<const void*>(&CreateFileA), 28, [](Regs& r) {
        Record({0xC4F, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6]});
        bool index = std::strstr(reinterpret_cast<const char*>(r.args[0]), ".dat") != nullptr;
        if (index)
            return g_indexOpens ? 0x1D1Du : 0xFFFFFFFFu;
        return 0x3AD0u;
    });
    StandInApi(s, 0x006450CC, reinterpret_cast<const void*>(&ReadFile), 20, [](Regs& r) {
        Record({0x4EAD, r.args[0], Rel(r.args[1]) == r.args[1] ? (r.args[1] == 0x006EE010 ? 1u : 2u) : Rel(r.args[1]), r.args[2], r.args[4]});
        uint32_t n = r.args[2];
        if (g_indexAt + n > g_indexFile.size())
            n = static_cast<uint32_t>(g_indexFile.size() - g_indexAt);
        std::memcpy(reinterpret_cast<void*>(r.args[1]), g_indexFile.data() + g_indexAt, n);
        g_indexAt += n;
        *reinterpret_cast<DWORD*>(r.args[3]) = n;
        return 1u;
    });
    StandInApi(s, 0x006450B4, reinterpret_cast<const void*>(&CloseHandle), 4, [](Regs& r) {
        Record({0xC1C, r.args[0]});
        return 1u;
    });
    StandInApi(s, 0x006450C8, reinterpret_cast<const void*>(&CreateThread), 24, [](Regs& r) {
        uint32_t function = r.args[2] == 0x00453100 || Resolve(reinterpret_cast<const void*>(r.args[2])) == AddressOf(&Pcm::StreamThread) ? 1u : r.args[2];
        Record({0xC7D, r.args[0], r.args[1], function, r.args[3], r.args[4], r.args[5] ? 1u : 0u});
        if (r.args[5])
            *reinterpret_cast<DWORD*>(r.args[5]) = 77;
        return 0x7D7Du;
    });
    StandInApi(s, 0x006450C4, reinterpret_cast<const void*>(&SetThreadPriority), 8, [](Regs& r) {
        Record({0x9410, r.args[0], r.args[1]});
        return 1u;
    });
}

// Every global the layer touches.
std::vector<Saved> PlatformGlobals()
{
    return {{0x006EDFB8, 0x68, {}},  {0x006EE024, 0x1DB4, {}}, {0x0069CBB8, 0x1400, {}}, {0x0067D7FC, 4, {}},
            {0x0069CBA8, 0x10, {}},  {0x007CC979, 1, {}},      {0x007CEFAD, 1, {}}};
}

StreamSlot* Slots() { return reinterpret_cast<StreamSlot*>(0x006EDFB8); }
StreamSlot& Music() { return Slots()[3]; }

void RecordState()
{
    Record({HashBytes(reinterpret_cast<const uint8_t*>(0x006EDFB8), 0x68), HashBytes(g_heap, sizeof(g_heap)),
            HashBytes(reinterpret_cast<const uint8_t*>(0x0069CBA8), 0x10),
            HashBytes(reinterpret_cast<const uint8_t*>(0x006EE024), 0x1DB4), GameVariable<uint32_t>(0x0067D7FC)});
    for (int i = 0; i < 4; ++i)
        Record({Rel(reinterpret_cast<uint32_t>(Slots()[i].wad)), Rel(reinterpret_cast<uint32_t>(Slots()[i].bink))});
}

// A stream object for a slot, from the heap (the fields a case wants set after).
template <typename Stream>
Stream* NewStream(uint8_t fill)
{
    uint32_t at = (g_heapUsed + 15) & ~15u;
    g_heapUsed = at + sizeof(Stream);
    std::memset(g_heap + at, fill, sizeof(Stream));
    auto* stream = reinterpret_cast<Stream*>(g_heap + at);
    stream->m_finished = false;
    stream->m_stopRequested = false;
    stream->m_statusIndex = 0;
    return stream;
}

void ResetPlatform(std::vector<Saved>& saved)
{
    for (Saved& sv : saved)
        std::memset(reinterpret_cast<void*>(sv.address), 0, sv.size);
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<uint32_t>(0x006EE01C) = 0x10C4;
    std::memset(g_heap, 0, sizeof(g_heap));
    g_heapUsed = 0;
    g_allocFails = false;
    g_openResult = 0;
    g_waitResult = 0;
}

// The addresses rewritten here that the originals under test call (kept original during the original's run).
const uint32_t kOwnFunctions[] = {0x00453500, 0x00453090, 0x004530E0, 0x004538F0, 0x004535E0, 0x004533A0,
                                  0x00453270, 0x00453250, 0x00453940, 0x00453C20, 0x00453DF0, 0x00454080,
                                  0x00453B30, 0x004542D0};

int CallFind(void* code, uint32_t checksum)
{
    int result;
    __asm {
        push ebx
        mov ebx, checksum
        call code
        pop ebx
        mov result, eax
    }
    return result;
}

// The slots (Create of each type - the allocation failing too - Open on either class or none, the volume and
// pan, Free), the thread (the lock taken or timing out, each slot's class, stopping), StopStreamThread,
// NewOverlapped (blocks in use by the music and streams, wrapping round), Init (pcm.dat opening or not,
// entries out of order - unsigned - and repeated, the thread made only once), FindWadEntry through its
// register stub (every entry, between, below, above, an empty index), StreamExists, the status, preload and pause
// calls (the music and each slot, with each class or none), UpdateStreams (finished and pending or not, asked to
// stop) and the user soundtrack lookups.
void TestStreamPlatform()
{
    std::vector<Saved> saved = PlatformGlobals();
    saved.push_back({0x006EE00C, 8, {}});
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInPlatform(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            std::vector<OriginalCode*> guards;
            if (original)
                for (uint32_t address : kOwnFunctions)
                    guards.push_back(new OriginalCode(address));

            // Create / Open / SetVolumeAndPan / Free.
            for (int type = -1; type < 3; ++type) {
                for (int fail = 0; fail < 2; ++fail) {
                    ResetPlatform(saved);
                    g_allocFails = fail != 0;
                    StreamSlot* slot = &Slots()[1];
                    if (original)
                        Original<void(__thiscall*)(StreamSlot*, int, bool, int)>(0x00453500)(slot, type, type == 0, 5 + type);
                    else
                        slot->Create(type, type == 0, 5 + type);
                    RecordState();
                    for (uint32_t result : {0u, 0x80070057u}) {
                        g_openResult = result;
                        long r = original ? Original<long(__thiscall*)(StreamSlot*, uint32_t, const char*, void*, uint32_t, uint32_t, void*)>(0x00453090)(
                                                slot, 0xC4EC, "a\\path", reinterpret_cast<void*>(0x3AD0), 0x800, 0x1200, reinterpret_cast<void*>(0x9000))
                                          : slot->Open(0xC4EC, "a\\path", reinterpret_cast<void*>(0x3AD0), 0x800, 0x1200, reinterpret_cast<void*>(0x9000));
                        Record({0x09E, static_cast<uint32_t>(r)});
                    }
                    if (original)
                        Original<void(__thiscall*)(StreamSlot*, float, float)>(0x004530E0)(slot, 12.5f, 80.25f);
                    else
                        slot->SetVolumeAndPan(12.5f, 80.25f);
                    if (original)
                        Original<void(__thiscall*)(StreamSlot*)>(0x004538F0)(slot);
                    else
                        slot->Free();
                    RecordState();
                }
            }
            // Open, volume and Free with both classes in a slot.
            ResetPlatform(saved);
            Slots()[2].wad = NewStream<WadStream>(0x11);
            Slots()[2].bink = NewStream<BinkStream>(0x22);
            {
                long r = original ? Original<long(__thiscall*)(StreamSlot*, uint32_t, const char*, void*, uint32_t, uint32_t, void*)>(0x00453090)(
                                        &Slots()[2], 0xC4ED, "b", reinterpret_cast<void*>(0x3AD1), 0x801, 0x1201, reinterpret_cast<void*>(0x9001))
                                  : Slots()[2].Open(0xC4ED, "b", reinterpret_cast<void*>(0x3AD1), 0x801, 0x1201, reinterpret_cast<void*>(0x9001));
                Record({0x09F, static_cast<uint32_t>(r)});
                if (original)
                    Original<void(__thiscall*)(StreamSlot*, float, float)>(0x004530E0)(&Slots()[2], 1.0f, 2.0f);
                else
                    Slots()[2].SetVolumeAndPan(1.0f, 2.0f);
            }
            if (original)
                Original<void(__thiscall*)(StreamSlot*)>(0x004538F0)(&Slots()[2]);
            else
                Slots()[2].Free();
            RecordState();

            // The thread.
            for (int c = 0; c < 4; ++c) {
                ResetPlatform(saved);
                if (c != 1)
                    Music().bink = NewStream<BinkStream>(0x31);
                if (c == 2)
                    Music().wad = NewStream<WadStream>(0x32);
                Slots()[0].bink = NewStream<BinkStream>(0x33);
                Slots()[1].wad = NewStream<WadStream>(0x34);
                if (c == 3) {
                    Slots()[1].bink = NewStream<BinkStream>(0x35);
                    Slots()[2].bink = NewStream<BinkStream>(0x36);
                }
                g_waitResult = c == 1 ? WAIT_TIMEOUT : 0;
                g_sleepsLeft = c + 1;
                GameVariable<bool>(0x006EE018) = false;
                uint32_t r = original ? Original<uint32_t(__stdcall*)(void*)>(0x00453100)(nullptr) : Pcm::StreamThread(nullptr);
                Record({0x7D, r});
                RecordState();
            }
            ResetPlatform(saved);
            GameVariable<bool>(0x006EE018) = true;
            uint32_t r0 = original ? Original<uint32_t(__stdcall*)(void*)>(0x00453100)(nullptr) : Pcm::StreamThread(nullptr);
            Record({0x7E, r0});
            for (uint32_t thread : {0u, 0x7D7Du}) {
                ResetPlatform(saved);
                GameVariable<uint32_t>(0x006EE014) = thread;
                if (original)
                    Original<void(__cdecl*)()>(0x004531A0)();
                else
                    Pcm::StopStreamThread();
                Record({GameVariable<uint32_t>(0x006EE014), GameVariable<uint8_t>(0x006EE018)});
            }

            // NewOverlapped.
            {
                ResetPlatform(saved);
                auto* ring = reinterpret_cast<OVERLAPPED*>(0x0069CBB8);
                std::memset(ring, 0x77, 0x1400);
                Music().wad = NewStream<WadStream>(0x40);
                Music().wad->m_overlapped = reinterpret_cast<_OVERLAPPED*>(&ring[254]);
                Music().bink = NewStream<BinkStream>(0x41);
                Music().bink->m_overlapped = reinterpret_cast<_OVERLAPPED*>(&ring[255]); // (not looked at: the wad is)
                Slots()[0].bink = NewStream<BinkStream>(0x42);
                Slots()[0].bink->m_overlapped = reinterpret_cast<_OVERLAPPED*>(&ring[0]);
                Slots()[1].wad = NewStream<WadStream>(0x43);
                Slots()[1].wad->m_overlapped = reinterpret_cast<_OVERLAPPED*>(&ring[2]);
                Slots()[1].bink = NewStream<BinkStream>(0x44);
                Slots()[1].bink->m_overlapped = reinterpret_cast<_OVERLAPPED*>(&ring[1]);
                Slots()[2].bink = NewStream<BinkStream>(0x45);
                Slots()[2].bink->m_overlapped = reinterpret_cast<_OVERLAPPED*>(&ring[4]);
                GameVariable<int>(0x006EE008) = 253;
                for (int i = 0; i < 6; ++i) {
                    _OVERLAPPED* o = original ? Original<_OVERLAPPED*(__cdecl*)()>(0x004535E0)() : Pcm::NewOverlapped();
                    Record({static_cast<uint32_t>(reinterpret_cast<OVERLAPPED*>(o) - ring), GameVariable<uint32_t>(0x006EE008)});
                }
                Record({HashBytes(reinterpret_cast<const uint8_t*>(ring), 0x1400)});
            }

            // Init.
            const uint32_t entries[][3] = {{0x90000000u, 1, 2}, {5, 3, 4}, {0x7FFFFFFFu, 5, 6}, {5, 7, 8}, {1, 9, 10},
                                           {0xFFFFFFFFu, 11, 12}, {3, 13, 14}};
            for (int c = 0; c < 4; ++c) {
                ResetPlatform(saved);
                g_indexOpens = c != 1;
                g_indexFile.clear();
                uint32_t count = c == 2 ? 0 : 7;
                g_indexFile.insert(g_indexFile.end(), reinterpret_cast<uint8_t*>(&count), reinterpret_cast<uint8_t*>(&count) + 4);
                for (uint32_t i = 0; i < count; ++i)
                    g_indexFile.insert(g_indexFile.end(), reinterpret_cast<const uint8_t*>(entries[i]), reinterpret_cast<const uint8_t*>(entries[i]) + 12);
                g_indexAt = 0;
                GameVariable<uint32_t>(0x006EE014) = c == 3 ? 0x5555u : 0u;
                GameVariable<bool>(0x007CC979) = c != 0;
                std::memset(reinterpret_cast<void*>(0x006EDFB8), 0x3C, 0x50); // the slots (not the index after them)
                if (original)
                    Original<void(__cdecl*)()>(0x00453670)();
                else
                    Pcm::Init();
                RecordState();
                Record({GameVariable<uint32_t>(0x006EE010), Rel(GameVariable<uint32_t>(0x006EE00C)), GameVariable<uint32_t>(0x006EE014)});
            }
            // FindWadEntry (on the sorted index Init left) and StreamExists.
            for (uint32_t count : {7u, 1u, 0u}) {
                GameVariable<uint32_t>(0x006EE010) = count;
                for (uint32_t checksum : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 0x7FFFFFFFu, 0x80000000u, 0x90000000u, 0xFFFFFFFEu, 0xFFFFFFFFu}) {
                    int found = CallFind(original ? reinterpret_cast<void*>(0x004533A0) : reinterpret_cast<void*>(&Pcm::FindWadEntryStub), checksum);
                    uint32_t exists = 0, existsMusic = 0;
                    exists = original ? Original<uint32_t(__cdecl*)(uint32_t, int)>(0x004533E0)(checksum, 0) : Pcm::StreamExists(checksum, 0);
                    existsMusic = original ? Original<uint32_t(__cdecl*)(uint32_t, int)>(0x004533E0)(checksum, 1) : Pcm::StreamExists(checksum, 1);
                    Record({0xF1D, static_cast<uint32_t>(found), exists, existsMusic});
                }
            }

            // Status, preloads, pausing.
            for (int c = 0; c < 6; ++c) {
                ResetPlatform(saved);
                // c: which slots hold what (music and the three streams)
                auto fill = [&](StreamSlot& slot, int what, uint8_t tag) {
                    if (what & 1) {
                        slot.wad = NewStream<WadStream>(tag);
                        slot.wad->m_unknown04 = (tag + c) & 1;
                    }
                    if (what & 2)
                        slot.bink = NewStream<BinkStream>(static_cast<uint8_t>(tag + 1));
                };
                fill(Music(), c % 4, 0x50);
                fill(Slots()[0], (c + 1) % 4, 0x52);
                fill(Slots()[1], (c + 2) % 4, 0x54);
                fill(Slots()[2], c == 5 ? 0 : (c + 3) % 4, 0x56);
                uint32_t status = static_cast<uint32_t>(original ? Original<int(__cdecl*)()>(0x00453250)() : Pcm::MusicStatus());
                Record({0x5A7, status});
                for (int slot = -1; slot < 3; ++slot) {
                    uint32_t st = static_cast<uint32_t>(original ? Original<int(__cdecl*)(int)>(0x00453270)(slot) : Pcm::StreamStatus(slot));
                    Record({0x5A8, st});
                }
                uint32_t preload = (original ? Original<uint32_t(__cdecl*)()>(0x00453220)() : Pcm::MusicPreloadDone()) & 0xFF;
                Record({0x9E1, preload});
                for (int slot = 0; slot < 3; ++slot) {
                    uint32_t p = (original ? Original<uint32_t(__cdecl*)(int)>(0x004531D0)(slot) : Pcm::StreamPreloadDone(slot)) & 0xFF;
                    Record({0x9E2, p});
                }
                for (int which : {1, 0, 2, 1}) {
                    for (bool pause : {true, false}) {
                        if (original)
                            Original<void(__cdecl*)(bool, int)>(0x004532C0)(pause, which);
                        else
                            Pcm::PauseStreams(pause, which);
                        RecordState();
                    }
                }
            }

            // UpdateStreams.
            for (int c = 0; c < 8; ++c) {
                ResetPlatform(saved);
                auto set = [&](StreamSlot& slot, int what, int n) {
                    bool finished = (n & 1) != 0, pending = (n & 2) != 0, stop = (n & 4) != 0;
                    if (what == 1 || what == 3) {
                        WadStream* w = NewStream<WadStream>(static_cast<uint8_t>(0x60 + n));
                        w->m_finished = finished;
                        w->m_statusIndex = n & 3;
                        w->m_status[n & 3] = pending ? 0x8000000Au : 0x8000000Bu;
                        w->m_stopRequested = stop;
                        slot.wad = w;
                    }
                    if (what == 3)
                        finished = !finished, stop = !stop;
                    if (what == 2 || what == 3) {
                        BinkStream* b = NewStream<BinkStream>(static_cast<uint8_t>(0x70 + n));
                        b->m_finished = finished;
                        b->m_statusIndex = (n & 3) + 1;
                        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(b) + 0x34 + 4 * b->m_statusIndex) = pending ? 0x8000000Au : 0;
                        b->m_stopRequested = stop;
                        slot.bink = b;
                    }
                };
                set(Music(), 1 + c % 3, c);
                set(Slots()[0], 1 + (c + 1) % 3, (c + 3) & 7);
                set(Slots()[1], c == 6 ? 0 : 1 + (c + 2) % 3, (c + 5) & 7);
                set(Slots()[2], 1 + c % 2, (c * 3) & 7);
                uint32_t r = static_cast<uint32_t>(original ? Original<int(__cdecl*)()>(0x00453940)() : Pcm::UpdateStreams());
                Record({0x0D5, r});
                RecordState();
            }

            // User soundtracks.
            ResetPlatform(saved);
            for (int c = 0; c < 2; ++c) {
                uint32_t n = static_cast<uint32_t>(original ? Original<int(__cdecl*)()>(0x004542D0)() : Pcm::UserSoundtrackCount());
                Record({0x4E4D, n, GameVariable<uint32_t>(0x006EFDD0) & 0xFF});
            }
            GameVariable<int>(0x006EFDD4) = 3;
            for (int i = 0; i < 5; ++i)
                GameVariable<int>(0x006EE024 + i * 0x4C) = 10 + i;
            for (int index : {-1, 0, 2, 3, 4}) {
                uint32_t n = static_cast<uint32_t>(original ? Original<int(__cdecl*)()>(0x004542D0)() : Pcm::UserSoundtrackCount());
                auto name = reinterpret_cast<uint32_t>(original ? Original<const wchar_t*(__cdecl*)(int)>(0x00454300)(index) : Pcm::UserSoundtrackName(index));
                auto tracks = static_cast<uint32_t>(original ? Original<int(__cdecl*)(int)>(0x00454320)(index) : Pcm::UserSoundtrackTracks(index));
                Record({0x4E4E, n, name, tracks});
            }

            for (OriginalCode* guard : guards)
                delete guard;
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "pcm: the stream slots, thread, start-up, wad index, pausing and update");
    Check(traces[0].size() > 700, "pcm: stream platform test ran its cases");
}

// The music (LoadMusicTrack by checksum and by name - paths with either slash - PlayMusicTrack,
// StartPreloadedMusic, PlayUserTrack, StopMusic, SetMusicVolume, SyncMusicToTime) and the streams (LoadStream:
// unknown checksums, busy slots, opens failing, preloads, volumes; PlayStream, StartPreloadedStream,
// SetStreamVolume of each type, StopStream, StopStreams), with the music and streams in each state.
void TestStreamPlay()
{
    std::vector<Saved> saved = PlatformGlobals();
    saved.push_back({0x006EE00C, 8, {}});
    SaveAll(saved);
    static Pcm::WadEntry index[] = {{2, 0x100, 0x200}, {9, 0x300, 0x400}, {0x80000000u, 0x500, 0x600}};
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInPlatform(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            std::vector<OriginalCode*> guards;
            if (original)
                for (uint32_t address : kOwnFunctions)
                    guards.push_back(new OriginalCode(address));
            auto reset = [&] {
                ResetPlatform(saved);
                GameVariable<Pcm::WadEntry*>(0x006EE00C) = index;
                GameVariable<uint32_t>(0x006EE010) = 3;
                g_soundVolume = 87.5f;
            };

            // The music.
            for (int c = 0; c < 8; ++c) {
                reset();
                if (c == 1)
                    Music().bink = NewStream<BinkStream>(0x80);
                Music().paused = (c & 2) != 0;
                Music().volume = 33.0f + c;
                g_openResult = (c & 4) ? (c == 5 ? 5u : 0x80070057u) : 0;
                g_allocFails = c == 6;
                bool r;
                if (c & 1)
                    r = original ? Original<bool(__cdecl*)(uint32_t)>(0x00453DB0)(0xABC00 + c) : Pcm::PlayMusicTrack(0xABC00 + c);
                else
                    r = original ? Original<bool(__cdecl*)(uint32_t)>(0x00453C20)(0xABC00 + c) : Pcm::LoadMusicTrack(0xABC00 + c);
                Record({0x111, r});
                RecordState();
                bool started = original ? Original<bool(__cdecl*)()>(0x00453BF0)() : Pcm::StartPreloadedMusic();
                Record({0x112, started});
                RecordState();
                uint32_t v = static_cast<uint32_t>(original ? Original<int(__cdecl*)(float)>(0x00453400)(61.5f) : Pcm::SetMusicVolume(61.5f));
                Record({0x113, v});
                RecordState();
                if (original)
                    Original<void(__cdecl*)()>(0x00453AA0)();
                else
                    Pcm::StopMusic();
                RecordState();
            }
            for (const char* name : {"music\\Track01.bik", "a/b/C", "x\\y/z.BIK", "plain", "dir/"}) {
                reset();
                bool r = original ? Original<bool(__cdecl*)(const char*)>(0x00453D70)(name) : Pcm::LoadMusicTrackByName(name);
                Record({0x114, r});
                RecordState();
            }
            for (int c = 0; c < 4; ++c) {
                reset();
                if (c == 3)
                    Music().bink = NewStream<BinkStream>(0x81);
                Music().paused = c == 2;
                g_openResult = c == 1 ? 5u : 0u;
                bool r = original ? Original<bool(__cdecl*)(int, int)>(0x00453850)(3, 7 + c) : Pcm::PlayUserTrack(3, 7 + c);
                Record({0x115, r});
                RecordState();
            }
            // Sync to time: frames within 5 or not.
            for (float seconds : {0.0f, 1.0f, 2.5f, 10.0f, 0.2f, 1.268f, 0.9345f, 1.3013f, 0.9011f}) {
                reset();
                Music().bink = NewStream<BinkStream>(0x82);
                std::memset(&g_bink, 0, sizeof(g_bink));
                g_bink.frameRate = 30000;
                g_bink.frameRateDiv = 1001;
                g_bink.frameNum = 33;
                Music().bink->m_bink = reinterpret_cast<Nx::Bink*>(&g_bink);
                if (original)
                    Original<void(__cdecl*)(float)>(0x00453470)(seconds);
                else
                    Pcm::SyncMusicToTime(seconds);
            }
            reset();
            if (original)
                Original<void(__cdecl*)(float)>(0x00453470)(3.0f);
            else
                Pcm::SyncMusicToTime(3.0f);

            // Volumes: many levels and sound volumes (the rounding of the five channels' mean).
            {
                reset();
                Slots()[0].bink = NewStream<BinkStream>(0x83);
                uint32_t seed = 12345;
                auto next = [&] {
                    seed = seed * 1103515245u + 12345u;
                    return static_cast<float>((seed >> 8) & 0xFFFF) / 655.37f;
                };
                for (int i = 0; i < 60; ++i) {
                    g_soundVolume = next();
                    Pcm::StreamVolume v = {{next(), next(), next(), next(), next()}, i % 3 == 0 ? 1 : 2};
                    if (original)
                        Original<bool(__cdecl*)(const Pcm::StreamVolume*, int)>(0x00453DF0)(&v, 0);
                    else
                        Pcm::SetStreamVolume(&v, 0);
                    Record({Bits(Slots()[0].volume)});
                }
                g_soundVolume = 87.5f;
            }

            // The streams.
            const Pcm::StreamVolume volumes[] = {
                {{40, 60, 0, 0, 0}, 0}, {{12.5f, 99, 0, 0, 0}, 1}, {{10, 20, 30, 40, 55.5f}, 2}, {{70, 3, 0, 0, 0}, 3}, {{1, 2, 3, 4, 5}, 4},
            };
            for (int c = 0; c < 10; ++c) {
                reset();
                if (c == 2)
                    Slots()[1].wad = NewStream<WadStream>(0x90);
                g_openResult = c == 3 ? 0x80070057u : 0;
                uint32_t checksum = c == 4 ? 7u : (c == 5 ? 0x80000000u : 9u);
                const Pcm::StreamVolume* volume = (c & 1) ? &volumes[c % 5] : nullptr;
                bool preload = c >= 6;
                bool r = original ? Original<bool(__cdecl*)(uint32_t, int, const Pcm::StreamVolume*, int, int, bool, int)>(0x00454080)(
                                        checksum, 1, volume, 0x44, 0x55, preload, c - 3)
                                  : Pcm::LoadStream(checksum, 1, volume, 0x44, 0x55, preload, c - 3);
                Record({0x221, r});
                RecordState();
                bool started = original ? Original<bool(__cdecl*)(int, const Pcm::StreamVolume*)>(0x00454270)(c % 3, &volumes[(c + 2) % 5])
                                        : Pcm::StartPreloadedStream(c % 3, &volumes[(c + 2) % 5]);
                Record({0x222, started});
                RecordState();
                bool p = original ? Original<bool(__cdecl*)(uint32_t, int, int)>(0x00454200)(c == 8 ? 3u : 2u, (c + 1) % 3, 0x66)
                                  : Pcm::PlayStream(c == 8 ? 3u : 2u, (c + 1) % 3, 0x66);
                Record({0x223, p});
                RecordState();
                if (c == 7)
                    Slots()[0].wad = NewStream<WadStream>(0x91);
                for (int slot = 0; slot < 3; ++slot) {
                    for (const Pcm::StreamVolume& v : volumes) {
                        bool sv = original ? Original<bool(__cdecl*)(const Pcm::StreamVolume*, int)>(0x00453DF0)(&v, slot)
                                           : Pcm::SetStreamVolume(&v, slot);
                        Record({0x224, sv});
                        RecordState();
                    }
                }
                if (c & 1) {
                    if (original)
                        Original<void(__cdecl*)(int, int)>(0x00453B30)(1, 0);
                    else
                        Pcm::StopStream(1, 0);
                } else {
                    if (original)
                        Original<void(__cdecl*)()>(0x00453BD0)();
                    else
                        Pcm::StopStreams();
                }
                RecordState();
            }

            for (OriginalCode* guard : guards)
                delete guard;
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "pcm: loading, starting, stopping and the volumes of the music and streams");
    Check(traces[0].size() > 2000, "pcm: stream play test ran its cases");
}

} // namespace

SELFTEST(Early, TestStreamPlatform, RemoveStandIns);
SELFTEST(Early, TestStreamPlay, RemoveStandIns);
