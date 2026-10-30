// Self-tests for the input manager (Inp, game/sio/inpman), the game clock (game/sys/game_clock), the module
// manager (Mdl, game/sys/module) and the music and streams front end (game/pcm/music): the original against
// ours on identical state, with the functions they call stood in for by recording code (standins.h).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

// In the original's run, every call to these addresses (from inside originals too) runs the original.
struct Originals {
    std::vector<std::unique_ptr<OriginalCode>> guards;
    Originals(bool original, std::initializer_list<uint32_t> addresses)
    {
        if (original) {
            for (uint32_t address : addresses)
                guards.push_back(std::make_unique<OriginalCode>(address));
        }
    }
};

template <typename F>
void* Pick(bool original, uint32_t address, F ours)
{
    return original ? OriginalFunction(address) : reinterpret_cast<void*>(AddressOf(ours));
}

uint32_t g_xorshift = 0x2545F491;
uint32_t Random()
{
    g_xorshift ^= g_xorshift << 13;
    g_xorshift ^= g_xorshift >> 17;
    g_xorshift ^= g_xorshift << 5;
    return g_xorshift;
}

alignas(16) uint8_t g_fakeMemoryManager[0x40];
uint32_t FakeManager() { return reinterpret_cast<uint32_t>(g_fakeMemoryManager); }

// --- Inp::Data ------------------------------------------------------------------------------------------------------

alignas(16) uint8_t g_data[0x40];

std::vector<uint32_t> RunInpData(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044E840, 0x0044E860, 0x0044E8C0, 0x0044E900, 0x0044EA30});
    using ThisCall0 = void(__thiscall*)(void*);
    using ThisCall1 = void(__thiscall*)(void*, uint32_t);
    auto mask = reinterpret_cast<ThisCall1>(Pick(original, 0x0044E840, &Inp::Data::MaskDigitalInput));
    auto maskAnalog = reinterpret_cast<ThisCall1>(Pick(original, 0x0044E860, &Inp::Data::MaskAnalogInput));
    auto convert = reinterpret_cast<ThisCall0>(Pick(original, 0x0044E8C0, &Inp::Data::ConvertDigitalToAnalog));
    auto tolerance = reinterpret_cast<ThisCall0>(Pick(original, 0x0044E900, &Inp::Data::HandleAnalogTolerance));
    auto override = reinterpret_cast<ThisCall0>(Pick(original, 0x0044EA30, &Inp::Data::OverridePadWithStick));
    auto fill = [](uint32_t seed) {
        g_xorshift = seed;
        for (uint8_t& b : g_data)
            b = static_cast<uint8_t>(Random());
    };
    auto record = [](uint32_t tag) { Record({tag, HashBytes(g_data, sizeof(g_data))}); };
    for (uint32_t m : {0u, 1u, 0x1000u, 0xF000u, 0x80000001u, 0xFFFFFFFFu, 0x00FF00FFu}) {
        fill(m + 7);
        mask(g_data, m);
        record(0xE840);
    }
    for (uint32_t m : {0u, 1u, 0xFu, 0x3C0000u, 0x1FFFFFFu, 0xFFFFFFFFu, 0x2A5A5A5u, 0x40030u, 0x1C0000Fu}) {
        fill(m + 11);
        maskAnalog(g_data, m);
        record(0xE860);
    }
    for (uint32_t seed = 1; seed < 40; ++seed) {
        fill(seed * 77);
        reinterpret_cast<uint32_t*>(g_data)[7] = seed < 20 ? Random() : (seed & 1 ? 0 : 0xFFFFFFFF);
        convert(g_data);
        record(0xE8C0);
    }
    // The dead zone: values around the edges (50 and 51 from the centre) on each axis.
    const uint8_t values[] = {0x00, 0x4D, 0x4E, 0x4F, 0x7F, 0x80, 0x81, 0xB1, 0xB2, 0xB3, 0xFF};
    for (int round = 0; round < 300; ++round) {
        fill(round * 13 + 5);
        for (int i = 0; i < 4; ++i)
            g_data[i] = values[Random() % sizeof(values)];
        tolerance(g_data);
        record(0xE900);
    }
    // The stick on the d-pad: each direction, centred, and with a d-pad button down.
    const uint8_t sticks[] = {0x00, 0x01, 0x40, 0x7F, 0x80, 0x81, 0xC0, 0xFF};
    for (uint8_t x : sticks) {
        for (uint8_t y : sticks) {
            for (uint32_t buttons : {0u, 0x0FFFu, 0x1000u, 0x2000u, 0x4000u, 0x8000u, 0xFFFF0FFFu}) {
                fill(x * 256 + y + buttons);
                g_data[2] = x;
                g_data[3] = y;
                reinterpret_cast<uint32_t*>(g_data)[7] = buttons;
                override(g_data);
                record(0xEA30);
            }
        }
    }
    return g_trace;
}

// --- Inp::Server and Inp::Manager -----------------------------------------------------------------------------------

alignas(16) uint8_t g_server[0x100];
alignas(16) uint8_t g_device[0x100];
alignas(16) uint8_t g_recording[0x200];
alignas(16) uint8_t g_inpManager[0x140];
uint32_t g_override;
uint32_t g_taskMemory[8];

uint32_t Rel(uint32_t value, const void* base, size_t size)
{
    auto b = reinterpret_cast<uint32_t>(base);
    return value >= b && value < b + size ? 0xA0000000 + (value - b) : value;
}

std::vector<uint32_t> RunInpServer(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044E9E0, 0x0044EAA0, 0x0044E840, 0x0044E860, 0x0044E8C0, 0x0044E900,
                                0x0044EA30});
    using ThisCall0 = uint32_t(__thiscall*)(void*);
    auto construct = reinterpret_cast<ThisCall0>(Pick(original, 0x0044E9E0, &Inp::Server::Construct));
    auto service = reinterpret_cast<ThisCall0>(Pick(original, 0x0044EAA0, &Inp::Server::ServiceHandlers));
    std::memset(g_server, 0xA5, sizeof(g_server));
    uint32_t result = construct(g_server);
    Record({0xE9E0, Rel(result, g_server, sizeof(g_server)), HashBytes(g_server, sizeof(g_server))});

    auto* server = reinterpret_cast<uint32_t*>(g_server);
    auto* buttons = reinterpret_cast<uint32_t*>(0x00696D48);
    auto snapshot = [&](uint32_t tag) {
        Record({tag, HashBytes(g_server, sizeof(g_server)), HashBytes(g_recording, sizeof(g_recording)),
                HashBytes(g_device, sizeof(g_device))});
        for (int i = 0; i < 6; ++i)
            Record({buttons[i]});
        Record({Rel(server[0x74 / 4], g_recording, sizeof(g_recording)),
                Rel(server[0x7C / 4], g_recording, sizeof(g_recording))});
    };
    for (int round = 0; round < 160; ++round) {
        g_xorshift = round * 2654435761u + 1;
        for (uint8_t& b : g_server)
            b = static_cast<uint8_t>(Random());
        for (uint8_t& b : g_device)
            b = static_cast<uint8_t>(Random());
        for (uint8_t& b : g_recording)
            b = static_cast<uint8_t>(Random());
        for (int i = 0; i < 6; ++i)
            buttons[i] = Random();
        int mode = round % 16;
        // The recording played back: none, a valid frame, an invalid one.
        uint32_t in = 0;
        if (mode == 1 || mode == 2) {
            in = reinterpret_cast<uint32_t>(g_recording);
            g_recording[0] = mode == 1 ? 1 : 0;
        }
        server[0x74 / 4] = in;
        server[0x7C / 4] = (round / 16) % 2 ? reinterpret_cast<uint32_t>(g_recording + 0x80) : 0;
        server[0] = mode == 3 ? 0 : reinterpret_cast<uint32_t>(g_device);
        g_device[0x14] = mode == 4 ? 0 : static_cast<uint8_t>(1 + Random() % 3);
        g_device[0x1C] = mode == 5 ? 0xFF : 0;
        static const uint8_t types[] = {0x41, 0x52, 0x73, 0x73, 0x73, 0x61, 0x00, 0x82};
        g_device[0x1D] = types[(round / 3) % 8];
        *reinterpret_cast<uint32_t*>(g_device + 0xC8) = (round / 5) % 3 == 0 ? 1 : (round & 0x20);
        *reinterpret_cast<uint32_t*>(g_device + 0xDC) = round & 1;
        if (round % 7 == 0) // the left stick centred, so the stick doesn't work the d-pad
            g_device[0x22] = g_device[0x23] = 0x80;
        if (round % 11 == 0) // no d-pad button down (the bits are 0 when down)
            g_device[0x1E] |= 0xF0;
        g_override = (round / 2) % 3 != 0;
        service(g_server);
        snapshot(0xEAA0 + mode);
    }
    return g_trace;
}

std::vector<uint32_t> RunInpManager(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044E700, 0x0044E9E0, 0x0044E7F0, 0x0044E6B0});
    std::memset(g_taskMemory, 0x77, sizeof(g_taskMemory));
    using ThisCall0 = uint32_t(__thiscall*)(void*);
    auto construct = reinterpret_cast<ThisCall0>(Pick(original, 0x0044E700, &Inp::Manager::Construct));
    std::memset(g_inpManager, 0x5A, sizeof(g_inpManager));
    uint32_t result = construct(g_inpManager);
    auto* words = reinterpret_cast<uint32_t*>(g_inpManager);
    Record({0xE700, Rel(result, g_inpManager, sizeof(g_inpManager))});
    for (size_t i = 0; i < sizeof(g_inpManager) / 4; ++i)
        Record({Rel(words[i], g_inpManager, sizeof(g_inpManager))});
    // (the task's code: the original stores its 0x0044E660, ours our ServiceTask - the same function)
    const uint32_t ourTask = reinterpret_cast<uint32_t>(&Inp::Manager::ServiceTask);
    for (uint32_t word : g_taskMemory) {
        if (word == ourTask || word == AddressOf(&Inp::Manager::ServiceTask))
            word = 0x0044E660;
        Record({Rel(word, g_inpManager, sizeof(g_inpManager))});
    }
    return g_trace;
}

std::vector<uint32_t> RunInpSingleton(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044E7F0, 0x0044E6B0});
    auto create = [&](bool flag, uint32_t memory) {
        g_override = memory;
        void* code = Pick(original, 0x0044E7F0, &Inp::CreateManager);
        uint32_t r = reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(code)(flag ? 0xAB01u : 0xAB00u);
        Record({0xE7F0, flag, r, GameVariable<uint32_t>(0x00696D40), GameVariable<uint32_t>(0x00696D44)});
    };
    auto release = [&]() {
        void* code = Pick(original, 0x0044E6B0, &Inp::ReleaseManager);
        reinterpret_cast<void(__cdecl*)()>(code)();
        Record({0xE6B0, GameVariable<uint32_t>(0x00696D40), GameVariable<uint32_t>(0x00696D44)});
    };
    const uint32_t memory = reinterpret_cast<uint32_t>(g_inpManager);
    GameVariable<uint32_t>(0x00696D40) = 0;
    GameVariable<uint32_t>(0x00696D44) = 0;
    create(false, memory);
    create(true, 0);
    create(true, memory);
    create(true, memory + 16);
    create(false, 0);
    for (int i = 0; i < 6; ++i)
        release();
    GameVariable<uint32_t>(0x00696D40) = memory + 32;
    release();
    GameVariable<uint32_t>(0x00696D44) = 0x80000002;
    GameVariable<uint32_t>(0x00696D40) = memory + 48;
    release();
    return g_trace;
}

uint32_t g_devices[2] = {0x0DE70000, 0x0DE71111};

void TestInpManager()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x00696D40, 8, {}}, {0x00696D48, 24, {}}, {0x007D1E20, 4, {}}};
    SaveAll(saved);
    GameVariable<uint32_t>(0x007D1800) = FakeManager();
    GameVariable<uint32_t>(0x007D1E20) = 0x51051051;
    GameVariable<uint32_t>(0x00696D40) = 0x3A3A3A3A; // the manager the server asks (0x0044E620, stood in)
    std::vector<uint32_t> data[2], server[2], manager[2], singleton[2];
    {
        data[0] = RunInpData(true);
        data[1] = RunInpData(false);
    }
    {
        auto* s = new StandIns();
        s->At({0x00406E80, AddressOf(&Tsk::Stack::Construct)}, 0, [](Regs& r) {
            Record({0x6E80, Rel(r.ecx, g_server, sizeof(g_server))});
            return r.ecx;
        });
        s->At({0x004070A0, AddressOf(&Tsk::Stack::Process)}, 4, [](Regs& r) {
            auto* d = reinterpret_cast<uint8_t*>(r.ecx) + 0x3C; // the data the handlers see
            Record({0x70A0, Rel(r.ecx, g_server, sizeof(g_server)), r.args[0] & 0xFF, HashBytes(d, 0x34)});
            return 0u;
        });
        s->At({0x0044E620, AddressOf(&Inp::Manager::OverridePadWithStick)}, 0, [](Regs& r) {
            Record({0xE620, r.ecx});
            return g_override;
        });
        server[0] = RunInpServer(true);
        server[1] = RunInpServer(false);
        delete s;
    }
    {
        auto* s = new StandIns();
        s->At({0x00406E80, AddressOf(&Tsk::Stack::Construct)}, 0, [](Regs& r) {
            Record({0x6E80, Rel(r.ecx, g_inpManager, sizeof(g_inpManager))});
            return r.ecx;
        });
        s->At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
            Record({0xCC0, r.args[0]});
            return reinterpret_cast<uint32_t>(g_taskMemory);
        });
        s->At({0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, [](Regs& r) {
            Record({0x6890, r.ecx == reinterpret_cast<uint32_t>(g_taskMemory), r.args[0]});
            return r.ecx;
        });
        s->At({0x005BE4F0, AddressOf(&Sio::Manager::GetDeviceByIndex)}, 4, [](Regs& r) {
            Record({0xE4F0, r.ecx, r.args[0]});
            return g_devices[r.args[0] & 1];
        });
        manager[0] = RunInpManager(true);
        manager[1] = RunInpManager(false);
        delete s;
    }
    {
        auto* s = new StandIns();
        s->At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
              [](Regs& r) {
                  Record({0xD40, r.args[0], r.args[1], r.args[2]});
                  return g_override;
              });
        s->At({0x0044E700, AddressOf(&Inp::Manager::Construct)}, 0, [](Regs& r) {
            Record({0xE700, r.ecx});
            return r.ecx + 4;
        });
        s->At({0x0044E530, AddressOf(&Inp::Manager::Destruct)}, 0, [](Regs& r) {
            Record({0xE530, r.ecx});
            return 0u;
        });
        s->At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
            Record({0xF4EE, r.ecx == FakeManager(), r.args[0]});
            return 0u;
        });
        singleton[0] = RunInpSingleton(true);
        singleton[1] = RunInpSingleton(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(data[0], data[1], "Inp::Data: masks, digital to analogue, the dead zone, the stick on the d-pad");
    Compare(server[0], server[1], "Inp::Server: constructor and ServiceHandlers (playback, devices, pad types, recording)");
    Compare(manager[0], manager[1], "Inp::Manager: constructor");
    Compare(singleton[0], singleton[1], "Inp::Manager: singleton");
}
SELFTEST(Early, TestInpManager, RemoveStandIns);

// --- the game clock -------------------------------------------------------------------------------------------------

uint32_t g_time;

std::vector<uint32_t> RunGameClock(bool original)
{
    g_trace.clear();
    Originals guards(original, {0x0044ECE0, 0x0044ED10, 0x0044ED70});
    auto elapsed = reinterpret_cast<int(__cdecl*)()>(Pick(original, 0x0044ECE0, &Tmr::GameClockElapsed));
    auto pause = reinterpret_cast<void(__cdecl*)(uint32_t)>(Pick(original, 0x0044ED10, &Tmr::PauseGameClock));
    auto speed = reinterpret_cast<void(__cdecl*)(float)>(Pick(original, 0x0044ED70, &Tmr::SetGameClockSpeed));
    auto state = [](uint32_t tag) {
        Record({tag, GameVariable<uint32_t>(0x00696D60), GameVariable<uint32_t>(0x00696D64),
                GameVariable<uint32_t>(0x0067D7C0), GameVariable<uint32_t>(0x00684FC8),
                GameVariable<uint32_t>(0x00684FCC)});
    };
    const float speeds[] = {1.0f, 0.0f, 0.5f, 0.333333f, 2.75f, 1e-7f, 123456.7f, -1.5f};
    const uint32_t times[] = {0, 1, 999, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 123456789, 16};
    for (int round = 0; round < 64; ++round) {
        GameVariable<int>(0x00696D60) = round * 1000 - 5000;
        GameVariable<uint32_t>(0x00696D64) = times[round % 8];
        GameVariable<float>(0x0067D7C0) = speeds[(round / 8) % 8];
        GameVariable<float>(0x00684FC8) = speeds[(round + 3) % 8];
        GameVariable<float>(0x00684FCC) = speeds[(round + 5) % 8];
        g_time = times[(round * 3 + 1) % 8] + round;
        Record({0xECE0, static_cast<uint32_t>(elapsed())});
        state(0xECE0);
        pause(round & 1 ? 0x101u : 0x100u); // (only the low byte counts)
        state(0xED10);
        g_time += 1234567;
        Record({0xECE0, static_cast<uint32_t>(elapsed())});
        speed(speeds[round % 8]);
        state(0xED70);
    }
    return g_trace;
}

void TestGameClock()
{
    std::vector<Saved> saved = {{0x00696D60, 8, {}}, {0x0067D7C0, 4, {}}, {0x00684FC8, 8, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns();
        s->At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
            Record({0xE970});
            return g_time;
        });
        traces[0] = RunGameClock(true);
        traces[1] = RunGameClock(false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "the game clock: elapsed, pause, speed");
}
SELFTEST(Early, TestGameClock, RemoveStandIns);

} // namespace
