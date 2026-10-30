// Self-tests for the loading screen's frame (src/game/gfx/loading_bar.cpp, 0x004B6FF0) - the original against ours
// with the device, the picture and the renderer's cached setters stood in by recorders - and the loading console's
// pure parts (src/game/gfx/loading_console.h; not original: checked against worked-out values).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "game/gfx/loading_bar.h"
#include "game/gfx/loading_console.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using TestStandIns::Branch;
using TestStandIns::Patch;

std::vector<uint32_t> g_frame_log;
HANDLE g_frame_mutex;

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Whether this thread holds the loading screen's lock (released and taken again if so).
uint32_t LockHeld()
{
    if (!ReleaseMutex(g_frame_mutex))
        return 0;
    WaitForSingleObject(g_frame_mutex, INFINITE);
    return 1;
}

long __stdcall FrameBeginScene(void* device)
{
    g_frame_log.insert(g_frame_log.end(), {0xB5u, reinterpret_cast<uint32_t>(device) & 0xFFF, LockHeld()});
    return 0;
}
long __stdcall FrameEndScene(void* device)
{
    g_frame_log.insert(g_frame_log.end(), {0xE5u, reinterpret_cast<uint32_t>(device) & 0xFFF, LockHeld()});
    return 0;
}
long __stdcall FramePresent(void* device, const void* a, const void* b, void* c, const void* d)
{
    g_frame_log.insert(g_frame_log.end(), {0x9Eu, reinterpret_cast<uint32_t>(device) & 0xFFF, reinterpret_cast<uint32_t>(a),
                                           reinterpret_cast<uint32_t>(b), reinterpret_cast<uint32_t>(c),
                                           reinterpret_cast<uint32_t>(d), LockHeld()});
    return 0;
}
long __stdcall FrameRenderState(void* device, uint32_t state, uint32_t value)
{
    g_frame_log.insert(g_frame_log.end(), {0x55u, reinterpret_cast<uint32_t>(device) & 0xFFF, state, value});
    return 0;
}
long __stdcall FrameDraw(void* device, uint32_t type, uint32_t count, const void* vertices, uint32_t stride)
{
    g_frame_log.insert(g_frame_log.end(),
                       {0xD9u, reinterpret_cast<uint32_t>(device) & 0xFFF, type, count, stride, LockHeld()});
    const auto* words = static_cast<const uint32_t*>(vertices);
    g_frame_log.insert(g_frame_log.end(), words, words + 4 * 5);
    return 0;
}
void __cdecl FramePicture() { g_frame_log.insert(g_frame_log.end(), {0x91Cu, LockHeld()}); }
void __cdecl FrameTexture(int stage, void* texture)
{
    g_frame_log.insert(g_frame_log.end(), {0x7Eu, static_cast<uint32_t>(stage), reinterpret_cast<uint32_t>(texture)});
}
void __cdecl FrameVertexFormat(uint32_t fvf) { g_frame_log.insert(g_frame_log.end(), {0xF7u, fvf}); }

void* FrameDevice()
{
    static void* vtable[0x80];
    for (void*& slot : vtable)
        slot = nullptr;
    vtable[0x44 / 4] = reinterpret_cast<void*>(&FramePresent);
    vtable[0xA4 / 4] = reinterpret_cast<void*>(&FrameBeginScene);
    vtable[0xA8 / 4] = reinterpret_cast<void*>(&FrameEndScene);
    vtable[0xE4 / 4] = reinterpret_cast<void*>(&FrameRenderState);
    vtable[0x14C / 4] = reinterpret_cast<void*>(&FrameDraw);
    alignas(16) static void* device[2] = {vtable, nullptr};
    return device;
}

// Every global the frame reads or writes, saved and put back.
struct FrameState {
    static constexpr uint32_t kAddresses[8] = {0x007D69E0, 0x00786D6C, 0x00701680, 0x00705288,
                                               0x007052A0, 0x007052AC, 0x00786D04, 0x00786D08};
    static constexpr uint32_t kSizes[8] = {4, 8, 8, 0x18, 0xC, 4, 1, 4};
    uint8_t bytes[8][0x18];
    FrameState()
    {
        for (int i = 0; i < 8; ++i)
            std::memcpy(bytes[i], reinterpret_cast<void*>(kAddresses[i]), kSizes[i]);
    }
    ~FrameState()
    {
        for (int i = 0; i < 8; ++i)
            std::memcpy(reinterpret_cast<void*>(kAddresses[i]), bytes[i], kSizes[i]);
    }
};

using FrameFn = void(__stdcall*)(unsigned int, unsigned int, unsigned long, unsigned long, unsigned long);

// Original against ours: screens of many heights and widths (the float roundings of the bar's frame and inset), bar
// widths (a bar wider than the screen: an odd negative half), times (filling, full and past it - clamped -, no total
// time, negative and NaN), gradient colours with bits above a byte; the frame not drawn without its timer's id or
// with the screen down. Every device call, its vertices, the elapsed time left, and whether the lock was held.
void TestLoadingScreenFrame()
{
    FrameState state;
    std::vector<Patch*> patches;
    patches.push_back(new Patch(0x004B6CD0, Branch(0xE9, 0x004B6CD0, reinterpret_cast<void*>(&FramePicture))));
    patches.push_back(new Patch(0x004DA730, Branch(0xE9, 0x004DA730, reinterpret_cast<void*>(&FrameTexture))));
    patches.push_back(new Patch(0x004D9DB0, Branch(0xE9, 0x004D9DB0, reinterpret_cast<void*>(&FrameVertexFormat))));
    // (ours calls the renderer's setters directly)
    uint32_t ourTexture = TestStandIns::AddressOf(&Nx::SetTexture);
    uint32_t ourFormat = TestStandIns::AddressOf(&Nx::SetVertexFormat);
    patches.push_back(new Patch(ourTexture, Branch(0xE9, ourTexture, reinterpret_cast<void*>(&FrameTexture))));
    patches.push_back(new Patch(ourFormat, Branch(0xE9, ourFormat, reinterpret_cast<void*>(&FrameVertexFormat))));
    if (uint32_t ourPicture = TestStandIns::OursOf(0x004B6CD0)) // (and our picture call)
        patches.push_back(new Patch(ourPicture, Branch(0xE9, ourPicture, reinterpret_cast<void*>(&FramePicture))));
    g_frame_mutex = CreateMutexA(nullptr, FALSE, nullptr);
    GameVariable<HANDLE>(0x007052AC) = g_frame_mutex;
    GameVariable<void*>(0x007D69E0) = FrameDevice();

    struct Times {
        float tick, elapsed, total;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Times times[] = {{0.0289f, 0.0f, 8.0f},   {0.0289f, 3.3f, 6.75f}, {0.0289f, 7.99f, 8.0f},
                           {0.5f, 12.0f, 8.0f},     {0.0289f, 1.0f, 0.0f},  {0.0289f, -4.0f, 8.0f},
                           {0.0289f, 2.0f, nan},    {0.01f, 0.123f, 0.6f}};
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        FrameFn frame = original ? Original<FrameFn>(0x004B6FF0) : &Nx::DrawLoadingScreenFrame;
        g_frame_log.clear();
        int n = 0;
        // (then every height from 1 to 2400 once: the frame's and the inset's float roundings only show now and then)
        for (int pass = 0; pass < 2; ++pass)
        for (int height = pass == 0 ? 360 : 1; height <= (pass == 0 ? 1600 : 2400); height += pass == 0 ? 37 : 1) {
            int width = height * 16 / 9 + (n % 3);
            for (int barCase = 0; barCase < (pass == 0 ? 3 : 1); ++barCase) {
                int bar = barCase == 0 ? width * 140 / 640 : barCase == 1 ? width / 3 + 1 : width + 7;
                const Times& t = times[n % 8];
                GameVariable<int>(0x00786D6C) = width;
                GameVariable<int>(0x00786D70) = height;
                GameVariable<int>(0x00701684) = bar;
                GameVariable<uint32_t>(0x00701680) = 0x80000000u | static_cast<uint32_t>(n * 0x010203);
                int* colors = &GameVariable<int>(0x00705288); // end r g b, start r g b
                for (int c = 0; c < 6; ++c)
                    colors[c] = (n * 37 + c * 91) % 0x1F0 + (c == 2 ? 0x300 : 0);
                GameVariable<float>(0x007052A0) = t.tick;
                GameVariable<float>(0x007052A4) = t.elapsed;
                GameVariable<float>(0x007052A8) = t.total;
                GameVariable<uint32_t>(0x00786D08) = n % 17 == 5 ? 0u : 7u;
                GameVariable<uint8_t>(0x00786D04) = n % 19 == 3 ? 0 : 1;
                frame(7, 0, 0, 0, 0);
                g_frame_log.insert(g_frame_log.end(), {0xE0Fu, Bits(GameVariable<float>(0x007052A4)), LockHeld()});
                ++n;
            }
        }
        traces[run] = g_frame_log;
    }
    for (auto it = patches.rbegin(); it != patches.rend(); ++it)
        delete *it;
    CloseHandle(g_frame_mutex);
    size_t draws = 0;
    for (uint32_t word : traces[0])
        draws += word == 0xD9u;
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  loading frame entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    if (traces[0].size() != traces[1].size())
        Log("  loading frame trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    Check(traces[0] == traces[1] && draws > 200, "gfx: the loading screen's frame (bar, timing, lock, device calls)");
}

// The loading console's pure parts: file names, sizes, the commands and log lines it shows, and its ring (repeats
// counted, the oldest lines dropped, the newest last).
void TestLoadingConsoleParts()
{
    using namespace Gfx::LoadingConsole;
    bool ok = true;
    ok &= std::strcmp(FileName("levels\\sk5ed\\sk5ed.scn.xbx"), "sk5ed.scn.xbx") == 0;
    ok &= std::strcmp(FileName("pre/qb_scripts.prx"), "qb_scripts.prx") == 0;
    ok &= std::strcmp(FileName("plain.qb"), "plain.qb") == 0;
    ok &= SizeText(512) == "512 B" && SizeText(1024) == "1.0 KB" && SizeText(1536) == "1.5 KB" &&
          SizeText(5 * 1024 * 1024 + 300 * 1024) == "5.3 MB";
    ok &= InterestingCommand("LoadScene") && InterestingCommand("loadcollision") && InterestingCommand("CreateFromStructure") &&
          InterestingCommand("ParseNodeArray") && InterestingCommand("AddScene") && !InterestingCommand("AddParams") &&
          !InterestingCommand("GetTags") && !InterestingCommand(nullptr);
    ok &= InterestingLogLine("start level: Load_sk5ed (free skate)") && !InterestingLogLine("data overlay: x read from y") &&
          !InterestingLogLine("") && !InterestingLogLine("  indented detail");
    static Ring ring;
    ring.count = 0;
    ring.Add("open a.qb", 0.5);
    ring.Add("open a.qb", 0.7);
    ring.Add("open b.qb", 1.3);
    std::vector<std::string> last = ring.Last(5);
    ok &= last.size() == 2 && last[0] == "[   0.7] open a.qb (x2)" && last[1] == "[   1.3] open b.qb";
    for (int i = 0; i < 100; ++i) {
        char text[16];
        std::snprintf(text, sizeof(text), "line %d", i);
        ring.Add(text, i);
    }
    last = ring.Last(3);
    ok &= last.size() == 3 && last[0] == "[  97.0] line 97" && last[2] == "[  99.0] line 99";
    ok &= ring.Last(1000).size() == kRingLines;
    std::string longText(200, 'x');
    ring.Add(longText.c_str(), 1.0);
    ok &= ring.Last(1)[0].size() == 9 + kLineWidth - 1;
    if (!ok)
        for (const std::string& line : ring.Last(3))
            Log("  loading console line: %s", line.c_str());
    Check(ok && !Active(), "loading console: file names, sizes, filters, the ring");
}

} // namespace

SELFTEST(Early, TestLoadingScreenFrame);
SELFTEST(Early, TestLoadingConsoleParts);
