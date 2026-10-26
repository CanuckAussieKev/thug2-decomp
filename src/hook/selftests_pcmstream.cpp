// Self-tests for the PC's streamed audio (src/game/pcm/stream.h): the stream classes (BinkStream, WadStream)
// and the platform layer over the music and the three streams. Each runs the original and ours against
// recording stand-ins (standins.h) for binkw32 (through its import slots), Win32 and the game functions they
// call, and compares the call traces and the memory they change.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
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
using Pcm::WadStream;

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Float(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// Stand-in Bink movies (the header fields read: frames +8, frame +0xC, frame rate +0x14 / +0x18).
struct FakeBink {
    uint32_t width, height, frames, frameNum, lastFrameNum, frameRate, frameRateDiv;
    uint8_t rest[0x40];
};
FakeBink g_binks[4];
int g_nextBink;         // the one BinkOpen gives next (-1: fails)
int g_soundSystemResult;
std::vector<int> g_waits; // BinkWait's results in turn (then 1)
size_t g_waitAt;
std::vector<uint32_t> g_errors; // GetLastError's results in turn (then 0)
size_t g_errorAt;
BOOL g_readResult;

void Slot(StandIns& s, uint32_t slot, uint16_t pop, Handler handler)
{
    s.Bytes(slot, Dword(reinterpret_cast<uint32_t>(s.Thunk(pop, std::move(handler)))));
}

uint32_t BinkIndex(uint32_t bink)
{
    for (uint32_t i = 0; i < 4; ++i)
        if (bink == reinterpret_cast<uint32_t>(&g_binks[i]))
            return i;
    return bink == 0 ? 0xFFu : 0xEEu;
}

// binkw32, Win32 and the game functions the stream classes call.
void StandInStreamCalls(StandIns& s)
{
    Slot(s, 0x006452E8, 8, [](Regs& r) { // BinkOpen
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0x0BE, r.args[1], r.args[1] == 0x04000000 ? *reinterpret_cast<const uint32_t*>(name) : Hash(name)});
        if (g_nextBink < 0)
            return 0u;
        return reinterpret_cast<uint32_t>(&g_binks[g_nextBink]);
    });
    Slot(s, 0x006452EC, 4, [](Regs& r) { // BinkDoFrame
        Record({0xD0F, BinkIndex(r.args[0])});
        return 0u;
    });
    Slot(s, 0x006452F0, 12, [](Regs& r) { // BinkSetPan
        Record({0x9A7, BinkIndex(r.args[0]), r.args[1], r.args[2]});
        return 0u;
    });
    Slot(s, 0x006452F4, 12, [](Regs& r) { // BinkGoto
        Record({0x6070, BinkIndex(r.args[0]), r.args[1], r.args[2]});
        return 0u;
    });
    Slot(s, 0x006452F8, 4, [](Regs& r) { // BinkSetIOSize
        Record({0x105, r.args[0]});
        return 0u;
    });
    Slot(s, 0x006452FC, 8, [](Regs& r) { // BinkSetVideoOnOff
        Record({0x51D, BinkIndex(r.args[0]), r.args[1]});
        return 0u;
    });
    Slot(s, 0x00645304, 8, [](Regs& r) { // BinkSetSoundSystem
        Record({0x555, r.args[0], r.args[1]});
        return static_cast<uint32_t>(g_soundSystemResult);
    });
    Slot(s, 0x00645310, 12, [](Regs& r) { // BinkSetVolume
        Record({0x701, BinkIndex(r.args[0]), r.args[1], r.args[2]});
        return 0u;
    });
    Slot(s, 0x00645318, 4, [](Regs& r) { // BinkWait
        int result = g_waitAt < g_waits.size() ? g_waits[g_waitAt++] : 1;
        Record({0x3A17, BinkIndex(r.args[0]), static_cast<uint32_t>(result)});
        return static_cast<uint32_t>(result);
    });
    Slot(s, 0x0064531C, 4, [](Regs& r) { // BinkClose
        Record({0xC105, BinkIndex(r.args[0])});
        return 0u;
    });
    Slot(s, 0x00645320, 4, [](Regs& r) { // BinkNextFrame
        Record({0x4E7, BinkIndex(r.args[0])});
        auto* bink = reinterpret_cast<FakeBink*>(r.args[0]);
        bink->frameNum = bink->frameNum >= bink->frames ? 1 : bink->frameNum + 1;
        return 0u;
    });
    StandInApi(s, 0x006450B4, reinterpret_cast<const void*>(&CloseHandle), 4, [](Regs& r) {
        Record({0xC1C, r.args[0]});
        return 1u;
    });
    StandInApi(s, 0x006450CC, reinterpret_cast<const void*>(&ReadFile), 20, [](Regs& r) {
        auto* overlapped = reinterpret_cast<OVERLAPPED*>(r.args[4]);
        Record({0x4EAD, r.args[0], r.args[1], r.args[2], r.args[3], overlapped->Offset, overlapped->OffsetHigh});
        return static_cast<uint32_t>(g_readResult);
    });
    StandInApi(s, 0x006450D4, reinterpret_cast<const void*>(&GetLastError), 0, [](Regs&) {
        uint32_t error = g_errorAt < g_errors.size() ? g_errors[g_errorAt++] : 0;
        Record({0xE44, error});
        return error;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0xDB6, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE, r.args[0]});
        return 0u;
    });
}

alignas(16) uint8_t g_object[0x200];
OVERLAPPED g_overlapped;

std::vector<Saved> StreamClassGlobals()
{
    return {{0x0069CADC, 0xC4, {}}, {0x007CCBE4, 1, {}}, {0x007D6A38, 4, {}}};
}

void RecordObject()
{
    Record({HashBytes(g_object, sizeof(g_object)), HashBytes(reinterpret_cast<const uint8_t*>(&g_overlapped), sizeof(g_overlapped)),
            HashBytes(reinterpret_cast<const uint8_t*>(g_binks), sizeof(g_binks)),
            HashBytes(reinterpret_cast<const uint8_t*>(0x0069CADC), 0xC4), GameVariable<uint8_t>(0x007CCBE4)});
}

// A stand-in stream: pattern-filled, then the fields a case sets.
BinkStream* FreshBink()
{
    for (size_t i = 0; i < sizeof(g_object); ++i)
        g_object[i] = static_cast<uint8_t>(0x5A + i * 7);
    auto* stream = reinterpret_cast<BinkStream*>(g_object);
    stream->m_bink = nullptr;
    stream->m_silent = false;
    stream->m_paused = false;
    stream->m_pauseRequested = false;
    return stream;
}

void ResetBinks()
{
    for (int i = 0; i < 4; ++i) {
        std::memset(&g_binks[i], 0, sizeof(g_binks[i]));
        g_binks[i].frames = 3 + i;
        g_binks[i].frameNum = 1 + i;
        g_binks[i].frameRate = 30000 + i;
        g_binks[i].frameRateDiv = 1001;
    }
}

// BinkStream's methods: Open (the sound system set up or not and failing, the I/O size set or not, the open
// failing), Pause (locked out or not; 0, 1, 0x100), SetVolume (below 0, 0, tiny, mid, 100, huge, NaN, with and
// without a Bink), SetVolumeAndPan (zero, negative, one side, both, capped, a zero total, NaNs, no Bink), the
// destructor (files shared or not, wad entries with and without users, memory), and the thread's Update (silent,
// pausing with and without a Bink, resuming by reopening from a path or a handle - failing too - the window
// without focus, plays counting down through several last frames: 2, 1, 0 and -1 plays).
void TestBinkStream()
{
    std::vector<Saved> saved = StreamClassGlobals();
    saved.push_back({0x0069CB98, 0x10, {}});
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInStreamCalls(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            for (Saved& sv : saved)
                std::memset(reinterpret_cast<void*>(sv.address), 0, sv.size);
            GameVariable<uint32_t>(0x007D6A38) = 0xD50D50;
            GameVariable<uint32_t>(0x0069CB9C) = 0x1234;
            ResetBinks();
            g_waits.clear();
            g_waitAt = 0;
            std::vector<OriginalCode*> guards;
            if (original)
                guards.push_back(new OriginalCode(0x00452DE0));

            auto open = [&](BinkStream* b, const char* path) {
                return static_cast<uint32_t>(original ? Original<long(__thiscall*)(BinkStream*, const char*)>(0x00452D20)(b, path) : b->Open(path));
            };
            // Open
            const struct { bool set; int system; uint32_t io; int bink; } opens[] = {
                {false, 0, 0, -1}, {false, 1, 0x40000, 2}, {true, 0, 0x30000, 1}, {true, 1, 0x40000, 0}, {false, 1, 5, 3},
            };
            for (const auto& o : opens) {
                BinkStream* b = FreshBink();
                GameVariable<bool>(0x0069CB99) = o.set;
                g_soundSystemResult = o.system;
                GameVariable<uint32_t>(0x0069CBA4) = o.io;
                g_nextBink = o.bink;
                uint32_t result = open(b, "data\\streams\\music\\track.bik");
                Record({0x0BE0, result, GameVariable<uint32_t>(0x0069CB98), GameVariable<uint32_t>(0x0069CBA4)});
                RecordObject();
            }
            // Pause
            for (int locked = 0; locked < 2; ++locked) {
                for (int pause : {0, 1, 0x100, 0}) {
                    BinkStream* b = FreshBink();
                    b->m_pauseRequested = pause == 0;
                    GameVariable<bool>(0x0069CB98) = locked != 0;
                    if (original)
                        Original<void(__thiscall*)(BinkStream*, int)>(0x00452DC0)(b, pause);
                    else
                        b->Pause(pause);
                    RecordObject();
                }
            }
            GameVariable<bool>(0x0069CB98) = false;
            // SetVolume
            const float volumes[] = {-1.0f, 0.0f, 0.001f, 0.004f, 37.3f, 100.0f, 1.0e9f, 1.0e30f,
                                     std::numeric_limits<float>::quiet_NaN(), 66.6667f, 1.5f, 37.31f};
            for (int withBink = 0; withBink < 2; ++withBink) {
                for (float v : volumes) {
                    BinkStream* b = FreshBink();
                    b->m_bink = withBink ? reinterpret_cast<Nx::Bink*>(&g_binks[1]) : nullptr;
                    if (original)
                        Original<void(__thiscall*)(BinkStream*, float)>(0x00452DE0)(b, v);
                    else
                        b->SetVolume(v);
                    RecordObject();
                }
            }
            // SetVolumeAndPan
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float pairs[][2] = {{0, 0}, {-1, -1}, {1, 0}, {0, 1}, {30, 70}, {70, 30}, {200, 200}, {-1, 1},
                                      {1, -1}, {nan, 5}, {5, nan}, {0.003f, 0.001f}, {33.3f, 66.71f}, {150, -3},
                                      {-5, 0.5f}, {1e20f, 3}, {100.00306f, 1}, {1, 16777216.0f}};
            for (int withBink = 0; withBink < 2; ++withBink) {
                for (const auto& p : pairs) {
                    BinkStream* b = FreshBink();
                    b->m_bink = withBink ? reinterpret_cast<Nx::Bink*>(&g_binks[2]) : nullptr;
                    if (original)
                        Original<void(__thiscall*)(BinkStream*, float, float)>(0x00452FC0)(b, p[0], p[1]);
                    else
                        b->SetVolumeAndPan(p[0], p[1]);
                    RecordObject();
                }
            }
            // Destructor
            for (int c = 0; c < 8; ++c) {
                BinkStream* b = FreshBink();
                b->m_bink = (c & 1) ? reinterpret_cast<Nx::Bink*>(&g_binks[0]) : nullptr;
                b->m_file = c == 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<void*>(0x4400 + c);
                b->m_sharedFile = c == 3 || c == 6;
                b->m_wadEntry = c < 2 ? -1 : (c == 7 ? 11 : (c == 2 ? 0 : c));
                b->m_memory = (c & 2) ? reinterpret_cast<void*>(0x3300 + c) : nullptr;
                auto* entries = reinterpret_cast<uint32_t*>(0x0069CADC);
                entries[4 * (b->m_wadEntry < 0 ? 0 : b->m_wadEntry) + 1] = 0x99;
                entries[4 * (b->m_wadEntry < 0 ? 0 : b->m_wadEntry) + 2] = c == 4 ? 0 : (c == 5 ? 0x80000000u : 3);
                if (original)
                    Original<void(__thiscall*)(BinkStream*)>(0x00452E30)(b);
                else
                    b->Destruct();
                RecordObject();
            }
            // Update
            struct UpdateCase {
                bool silent, requested, paused, open, handle, focus;
                int reopen, plays;
                std::vector<int> waits;
                uint32_t frame;
            };
            const UpdateCase updates[] = {
                {true, true, false, true, false, true, 0, 1, {0, 0}, 1},
                {false, true, false, true, false, true, 0, 1, {0}, 2},   // pause: closed, frame noted
                {false, true, false, false, false, true, 0, 1, {0}, 2},  // pause with none open
                {false, false, true, false, false, true, 1, 1, {0, 0, 1}, 2},  // resume: reopened by path
                {false, false, true, false, true, true, 2, 1, {1}, 2},   // resume from a handle
                {false, false, true, false, true, true, -1, 1, {0}, 2},  // reopen fails
                {false, false, true, true, false, true, -1, 1, {0, 0}, 2},   // resume with one open
                {false, false, false, true, false, false, 0, 1, {0, 0}, 1},  // no focus
                {false, false, false, true, false, true, 0, 2, {0, 0, 0, 0, 0, 0, 0, 0, 1}, 1},
                {false, false, false, true, false, true, 0, 1, {0, 0, 0, 0, 1}, 2},
                {false, false, false, true, false, true, 0, 0, {0, 0, 0, 0, 0, 1}, 1},
                {false, false, false, true, false, true, 0, -1, {0, 0, 0, 0, 0, 0, 0, 1}, 1},
                {false, false, false, true, false, true, 3, 5, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 1},
            };
            for (const UpdateCase& u : updates) {
                BinkStream* b = FreshBink();
                ResetBinks();
                b->m_silent = u.silent;
                b->m_pauseRequested = u.requested;
                b->m_paused = u.paused;
                b->m_bink = u.open ? reinterpret_cast<Nx::Bink*>(&g_binks[3]) : nullptr;
                g_binks[3].frameNum = u.frame;
                b->m_fromHandle = u.handle;
                std::memcpy(b->m_source, u.handle ? "\x44\x33\x22\x11" : "music.bik", 10);
                b->m_resumeFrame = 0x17;
                b->m_volume = 42.5f;
                b->m_plays = u.plays;
                b->m_finished = false;
                GameVariable<bool>(0x007CCBE4) = u.focus;
                g_nextBink = u.reopen;
                g_waits = u.waits;
                g_waitAt = 0;
                uint32_t result = static_cast<uint32_t>(original ? Original<int(__thiscall*)(BinkStream*)>(0x00452EA0)(b) : b->Update());
                Record({0x0D, result});
                RecordObject();
            }
            for (OriginalCode* guard : guards)
                delete guard;
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "pcm: BinkStream's open, pause, volumes, destructor and update");
    Check(traces[0].size() > 600, "pcm: BinkStream test ran its cases");
}

// WadStream's methods: the constructor, StartRead (the read done at once or pending, failing with an error
// above 0, 0 or below; from the wad's offset or the start), OpenFromWad, Open (an invalid handle with the last
// error positive or not - read twice - and a valid one), PreloadDone (each condition), Pause (0, 1, 0x100).
void TestWadStream()
{
    std::vector<Saved> saved = StreamClassGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInStreamCalls(*s);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            std::vector<OriginalCode*> guards;
            if (original)
                guards.push_back(new OriginalCode(0x00454370));
            auto fresh = [] {
                for (size_t i = 0; i < sizeof(g_object); ++i)
                    g_object[i] = static_cast<uint8_t>(0xA5 + i * 3);
                std::memset(&g_overlapped, 0x6C, sizeof(g_overlapped));
                auto* w = reinterpret_cast<WadStream*>(g_object);
                w->m_overlapped = reinterpret_cast<_OVERLAPPED*>(&g_overlapped);
                return w;
            };
            for (bool flag : {false, true}) {
                WadStream* w = fresh();
                WadStream* r = original ? Original<WadStream*(__thiscall*)(WadStream*, bool)>(0x00454340)(w, flag) : w->Construct(flag);
                Record({r == w});
                RecordObject();
            }
            const struct { BOOL read; std::vector<uint32_t> errors; bool fromWad; } reads[] = {
                {TRUE, {}, false}, {TRUE, {}, true}, {FALSE, {997}, true}, {FALSE, {5}, false},
                {FALSE, {0}, true}, {FALSE, {0x80004005u}, false}, {FALSE, {0x12345}, true}, {FALSE, {0x89ABC}, false},
            };
            for (const auto& c : reads) {
                g_readResult = c.read;
                g_errors = c.errors;
                for (int how = 0; how < 3; ++how) {
                    g_errorAt = 0;
                    WadStream* w = fresh();
                    w->m_fromWad = c.fromWad;
                    w->m_offset = 0x8800;
                    w->m_file = reinterpret_cast<void*>(0x60);
                    w->m_buffer = reinterpret_cast<void*>(0x7000);
                    long result;
                    if (how == 0)
                        result = original ? Original<long(__thiscall*)(WadStream*)>(0x00454370)(w) : w->StartRead();
                    else if (how == 1)
                        result = original ? Original<long(__thiscall*)(WadStream*, void*, uint32_t, uint32_t, void*)>(0x00454450)(
                                                w, reinterpret_cast<void*>(0x61), 0x1000, 0x2345, reinterpret_cast<void*>(0x7100))
                                          : w->OpenFromWad(reinterpret_cast<void*>(0x61), 0x1000, 0x2345, reinterpret_cast<void*>(0x7100));
                    else
                        result = original ? Original<long(__thiscall*)(WadStream*, void*, void*)>(0x00454490)(w, reinterpret_cast<void*>(0x62), reinterpret_cast<void*>(0x7200))
                                          : w->Open(reinterpret_cast<void*>(0x62), reinterpret_cast<void*>(0x7200));
                    Record({0x5EAD, static_cast<uint32_t>(result)});
                    RecordObject();
                }
            }
            const std::vector<uint32_t> invalidErrors[] = {{5, 6}, {0, 7}, {0x80000001u, 9}, {3, 0}};
            for (const auto& errors : invalidErrors) {
                g_errors = errors;
                g_errorAt = 0;
                WadStream* w = fresh();
                long result = original ? Original<long(__thiscall*)(WadStream*, void*, void*)>(0x00454490)(w, INVALID_HANDLE_VALUE, reinterpret_cast<void*>(0x7300))
                                       : w->Open(INVALID_HANDLE_VALUE, reinterpret_cast<void*>(0x7300));
                Record({0x1BAD, static_cast<uint32_t>(result)});
                RecordObject();
            }
            const struct { int state; bool done; int loaded, played; } preloads[] = {
                {0, false, 0, 0}, {2, false, 0, 0}, {1, true, 0, 0}, {1, false, 0x2000, 0}, {1, false, 0x1FFF, 0},
                {1, false, 0x3000, 0x1000}, {1, false, 0x2FFF, 0x1000}, {1, false, -5, -0x3000}, {-1, false, 0, 0},
                {0, true, 0, 0}, {-1, true, 0x3000, 0}, {3, false, 0x3000, 0},
            };
            for (const auto& p : preloads) {
                WadStream* w = fresh();
                w->m_state = p.state;
                w->m_readDone = p.done;
                w->m_loaded = p.loaded;
                w->m_played = p.played;
                uint32_t r = original ? Original<uint32_t(__thiscall*)(WadStream*)>(0x004543F0)(w) & 0xFF : w->PreloadDone();
                Record({0x9E, r});
            }
            for (int pause : {0, 1, 0x100, 0, -1}) {
                WadStream* w = fresh();
                if (original)
                    Original<void(__thiscall*)(WadStream*, int)>(0x00454430)(w, pause);
                else
                    w->Pause(pause);
                RecordObject();
            }
            for (OriginalCode* guard : guards)
                delete guard;
            traces[run] = g_trace;
        }
        delete s;
    }
    g_trace.clear();
    RestoreAll(saved);
    Compare(traces[0], traces[1], "pcm: WadStream's constructor, reads, opens, preload check and pause");
    Check(traces[0].size() > 150, "pcm: WadStream test ran its cases");
}

} // namespace

SELFTEST(Early, TestBinkStream, RemoveStandIns);
SELFTEST(Early, TestWadStream, RemoveStandIns);
