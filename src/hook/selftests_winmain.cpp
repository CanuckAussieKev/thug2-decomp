// Self-tests for the program's entry point (WinMain, 0x004E2680) and its command line's network options
// (ParseNetworkParams, 0x004E1220). WinMain runs with PartyMod's patch bytes written into the original, against
// ours, with every function it calls (Win32 included) stood in for by recording code; ParseNetworkParams over a
// range of command lines, the game's C runtime calls stood in for by what they do (offline) or not at all (Late).

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/net/online_identity.h"
#include "game/sys/main.h"
#include "game/sys/pc_setup.h"
#include "game/sys/platform.h"
#include "game/sys/registry.h"
#include "game/sys/sdl_window.h"
#include "game/sys/winmain.h"
#include "game/gfx/loading_bar_cfuncs.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "game/sys/platform_d3d.h"
#include "game/sys/platform_input.h"
#include "game/sys/platform_system.h"
#include "game/gfx/loading_bar_cfuncs.h"
#include "hook/standins.h"

namespace {

// PartyMod's call at 0x004E26DC in place of ParseNetworkParams: its set-up (Sys::InitPcSetup), then the original
// (through its address, so stood in like ours).
void __cdecl PcSetupThenParseNetworkParams(char* commandLine)
{
    Sys::InitPcSetup();
    GameFunction<void(__cdecl*)(char*)>(0x004E1220)(commandLine);
}

using namespace TestStandIns;

// --- ParseNetworkParams (0x004E1220) ------------------------------------------------------------------------

// What it writes: the server's address ... skill level; the hosting flag and group; the server, joining flag,
// e-mail, PPASS, name, password, teams; the join password.
std::vector<Saved> NetworkGlobals()
{
    return {{0x0067EFB8, 0x20, {}}, {0x007CC260, 0x08, {}}, {0x007CC980, 0x264, {}}, {0x007CCDFC, 0x10, {}}};
}

struct NetworkCase {
    const char* commandLine;
    bool offlineOnly; // a server name to look up (the Late test leaves those out: no real lookups)
};
const NetworkCase kNetworkCases[] = {
    {"", false},
    {"-host", false}, // ("-HOST " needs the space)
    {"-host -hostname MyServer", false},
    {"-HOSTNAME   \"My Server Name Over Fifteen\" -x", false},
    {"-hostname \"unterminated quote", false},
    {"-HOSTNAME", false},
    {"-hostname ab\"cd ef", false},
    {"-email someone@example.com -ppass Secret12345678901234", false},
    {"-email \"a b c\" -PPASS \"\"", false},
    {"-connect 10.0.0.1 -password abc", false},
    {"-CONNECTx10.0.0.2 -name Bob", false},
    {"-connect  myhost.example.com -password \"pass word long\"", true},
    {"-connect nohost.example", true},
    {"-host -connect 1.2.3.4 -password pw", false},
    {"-connect 255.255.255.255", true}, // (INADDR_NONE: looked up as a name)
    {"-maxplayers 1", false},
    {"-maxplayers 2", false},
    {"-maxplayers 5", false},
    {"-maxplayers 8", false},
    {"-maxplayers 9", false},
    {"-maxplayers -3", false},
    {"-MAXPLAYERS   +7", false},
    {"-maxplayers abc", false},
    {"-group 12345", false},
    {"-group12 -nteams3 -skilllevel21 -maxplayers34 -name\"q r\" -emailme -ppassxy -password\"p\"", false},
    {"-group\t-77", false},
    {"-nteams 0", false},
    {"-nteams 3", false},
    {"-nteams 4", false},
    {"-nteams 5", false},
    {"-nteams -1", false},
    {"-skilllevel 2", false},
    {"-skilllevel 4", false},
    {"-skilllevel 7", false},
    {"-skilllevel -2", false},
    {"-name \"Tony Hawk\" -password \"pass word long\"", false},
    {"-name \"ab\"cd -password 123456789012", false},
    {"-NAME Abcdefghijklmnopqrstu", false},
    {"-password secret", false},
    {"-Host -HostName h1 -Email e@x -PPass pp -MaxPlayers 6 -Group 3 -NTeams 2 -SkillLevel 1 -Name nn "
     "-Password pw",
     false},
    {"-connect 192.168.1.20 -maxplayers 4 -nteams 2 -skilllevel 3 -name Player -password joinme -email m@n", false},
};

// The game's C runtime isn't started offline: its _strupr and atol stood in for by what they do in the "C" locale
// (VC7.1's: a-z only; spaces, sign, digits, wrapping round) - an expectation independent of our C runtime.
uint32_t StandInStrupr(Regs& r)
{
    for (auto* p = reinterpret_cast<char*>(r.args[0]); *p; ++p)
        if (*p >= 'a' && *p <= 'z')
            *p = static_cast<char>(*p - 'a' + 'A');
    return r.args[0];
}
uint32_t StandInAtol(Regs& r)
{
    auto* p = reinterpret_cast<const char*>(r.args[0]);
    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        ++p;
    bool negative = *p == '-';
    if (*p == '-' || *p == '+')
        ++p;
    uint32_t value = 0;
    for (; *p >= '0' && *p <= '9'; ++p)
        value = value * 10 + static_cast<uint32_t>(*p - '0');
    return negative ? 0u - value : value;
}

// Name lookups: numbers (four parts) are addresses; names starting "nohost" aren't found; others are, at an
// address from the name (bytes with their top bits set too).
uint8_t g_hostAddress[4];
char* g_hostAddresses[2] = {reinterpret_cast<char*>(g_hostAddress), nullptr};
hostent g_host = {const_cast<char*>("host"), nullptr, AF_INET, 4, g_hostAddresses};

uint32_t StandInInetAddr(Regs& r)
{
    auto* name = reinterpret_cast<const char*>(r.args[0]);
    Record({0x1AE7, Hash(name)});
    int dots = 0;
    for (const char* p = name; *p; ++p) {
        if (*p == '.')
            ++dots;
        else if (*p < '0' || *p > '9')
            return INADDR_NONE;
    }
    return dots == 3 ? Hash(name) | 1 : INADDR_NONE;
}
uint32_t StandInGetHostByName(Regs& r)
{
    auto* name = reinterpret_cast<const char*>(r.args[0]);
    Record({0x6E7, Hash(name)});
    if (std::strncmp(name, "nohost", 6) == 0)
        return 0;
    uint32_t hash = Hash(name);
    g_hostAddress[0] = static_cast<uint8_t>(hash | 0x80);
    g_hostAddress[1] = static_cast<uint8_t>((hash >> 8) | 0x81);
    g_hostAddress[2] = static_cast<uint8_t>((hash >> 16) | 0x82);
    g_hostAddress[3] = static_cast<uint8_t>((hash >> 24) | 0x83);
    return reinterpret_cast<uint32_t>(&g_host);
}

// The globals as each run leaves them, every case from the same patterned state, with the hosting and joining
// flags set beforehand or not.
std::vector<uint32_t> RunNetworkCases(bool original, bool late)
{
    std::vector<Saved> globals = NetworkGlobals();
    g_trace.clear();
    for (const NetworkCase& c : kNetworkCases) {
        if (late && c.offlineOnly)
            continue;
        for (int flags = 0; flags < 4; ++flags) {
            for (Saved& g : globals)
                for (uint32_t i = 0; i < g.size; ++i)
                    reinterpret_cast<uint8_t*>(g.address)[i] = static_cast<uint8_t>(0xA1 + i * 7 + flags);
            GameVariable<bool>(0x007CC260) = (flags & 1) != 0;
            GameVariable<bool>(0x007CCA80) = (flags & 2) != 0;
            char commandLine[256];
            strcpy_s(commandLine, c.commandLine);
            if (original)
                Original<void(__cdecl*)(const char*)>(0x004E1220)(commandLine);
            else
                Sys::ReadNetworkParams(commandLine);
            Record({0xCA5E, Hash(commandLine)});
            for (Saved& g : globals)
                for (uint32_t i = 0; i < g.size; i += 4) {
                    uint32_t word;
                    std::memcpy(&word, reinterpret_cast<const uint8_t*>(g.address) + i, 4);
                    Record({word});
                }
        }
    }
    return g_trace;
}

void TestParseNetworkParams()
{
    std::vector<Saved> saved = NetworkGlobals();
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        s->At({0x00624EFE}, 0, StandInStrupr);
        s->At({0x00624E52}, 0, StandInAtol);
        StandInApi(*s, 0x006452A0, reinterpret_cast<const void*>(&inet_addr), 4, StandInInetAddr);
        StandInApi(*s, 0x006452A8, reinterpret_cast<const void*>(&gethostbyname), 4, StandInGetHostByName);
        traces[0] = RunNetworkCases(true, false);
        traces[1] = RunNetworkCases(false, false);
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1],
            "ParseNetworkParams: every option, quotes, lengths, ranges, lookups, hosting/joining before or not");
}

// In the game: the original with the game's own C runtime and lookups (numbers only), against ours.
void TestParseNetworkParamsLate()
{
    std::vector<Saved> saved = NetworkGlobals();
    SaveAll(saved);
    std::vector<uint32_t> expected = RunNetworkCases(true, true);
    std::vector<uint32_t> actual = RunNetworkCases(false, true);
    RestoreAll(saved);
    Compare(expected, actual, "ParseNetworkParams (the game's C runtime): every option, numbers only");
}

// --- WinMain (0x004E2680) -----------------------------------------------------------------------------------

// The game's _exit doesn't return: its stand-in throws back to the test.
struct GameExit {
    uint32_t code;
};

char* g_commandLine; // the run's command line buffer
const HINSTANCE kInstance = reinterpret_cast<HINSTANCE>(0x11110000);

struct WinMainCase {
    const char* commandLine;
    int gsi[4];       // what GSIAvailableCheckThink returns in turn (-1 ends; then 1)
    uint32_t running; // FindWindowExA's answer
    uint32_t window1; // the window Sys::CreateSdlWindow makes
    uint32_t window2; // ... as the window events leave it
};
const WinMainCase kWinMainCases[] = {
    {"", {1, -1}, 0, 0x1000, 0x1000},
    {"demo", {0, 0, 1, -1}, 0, 0x2000, 0x2004},
    {"  lead  two   spaces ", {2, -1}, 0, 0x3000, 0x3000},
    {"a b c d e f g h i j k l m n o p q r s t u v w x y", {0, 3, -1}, 0, 0x4000, 0x4000},
    {"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19", {1, -1}, 0, 0x5000, 0x5000},
    {"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20", {1, -1}, 0, 0x5100, 0x5100},
    {"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 ", {1, -1}, 0, 0x5200, 0x5200},
    {"-connect x", {1, -1}, 0x7777, 0x6000, 0x6000}, // already running
    {"no window", {0, -1}, 0, 0, 0},
    {"window gone", {1, -1}, 0, 0x8000, 0},
    {"window late", {1, -1}, 0, 0, 0x9000},
    {" ", {1, -1}, 0, 0xA000, 0xA000},
};

int g_gsiIndex;
const WinMainCase* g_case;
int g_loads;
uint32_t g_loadName;

uint32_t& Window() { return GameVariable<uint32_t>(0x007D6A28); }

uint32_t HashWide(const wchar_t* text)
{
    uint32_t hash = 2166136261u;
    for (; *text; ++text)
        hash = (hash ^ static_cast<uint16_t>(*text)) * 16777619u;
    return hash;
}

// What MapVirtualKeyA answers: scan code -> key -> a character, now and then the accent (B4) or the semicolon
// that are renamed, also with bits above the character (as dead keys have), none, and others.
uint32_t StandInMapVirtualKey(Regs& r)
{
    uint32_t code = r.args[0], type = r.args[1];
    Record({0x3A9, code, type});
    if (type == MAPVK_VSC_TO_VK)
        return (code * 5 + 3) ^ 0x2300;
    switch (code % 9) {
    case 0:
        return 0xB4;
    case 1:
        return 0x3B;
    case 2:
        return 0x800000B4;
    case 3:
        return 0x0000013B;
    case 4:
        return 0;
    case 5:
        return 0xE9;
    case 6:
        return 0x27;
    case 7:
        return 0x3A;
    default:
        return 'A' + code % 26;
    }
}

void TestWinMain()
{
    // (compared with the original, which always has the already-running check: allow_multiple_instances off)
    struct RestoreSetting {
        bool value = Sys::AllowMultipleInstances();
        ~RestoreSetting() { Sys::AllowMultipleInstances() = value; }
    } restoreSetting;
    Sys::AllowMultipleInstances() = false;
    std::vector<Saved> saved = {{0x007D6A28, 4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    int loads[2] = {};
    uint32_t loadNames[2] = {};
    {
        auto* s = new StandIns;
        auto plain = [](uint32_t tag) {
            return [tag](Regs&) {
                Record({tag});
                return 0u;
            };
        };
        // (the game name: the original's at 0x0064B1CC stands for the one [Miscellaneous] OnlineGameName picks -
        // game/net/online_identity.h; ours passes that)
        s->At({0x005F8690}, 0, [](Regs& r) {
            const char* name = reinterpret_cast<const char*>(r.args[0]);
            Record({0x8690, Hash(name == reinterpret_cast<const char*>(0x0064B1CC) ? Net::GameName() : name)});
            return 0u;
        });
        s->At({0x005F8840}, 0, [](Regs&) {
            Record({0x8840});
            int answer = g_case->gsi[g_gsiIndex];
            if (answer < 0)
                return 1u;
            ++g_gsiIndex;
            return static_cast<uint32_t>(answer);
        });
        s->At({0x005F8160}, 0, [](Regs& r) { Record({0x8160, r.args[0]}); return 0u; });
        s->At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
            Record({0xDB6, Hash(reinterpret_cast<const char*>(r.args[0]))});
            return 0u;
        });
        // LoadLibraryA: only the game's import (ours doesn't load anything)
        void* load = s->Thunk(4, [](Regs& r) {
            ++g_loads;
            g_loadName = Hash(reinterpret_cast<const char*>(r.args[0]));
            return 0x10000000u;
        });
        s->Bytes(0x00645130, Dword(reinterpret_cast<uint32_t>(load)));
        s->At({Resolve(reinterpret_cast<const void*>(&Sys::InitPcSetup))}, 0, plain(0x1417));
        s->At({0x004E1220, AddressOf(&Sys::ParseNetworkParams)}, 0, [](Regs& r) {
            Record({0x1220, r.args[0] == reinterpret_cast<uint32_t>(g_commandLine),
                    Hash(reinterpret_cast<const char*>(r.args[0]))});
            return 0u;
        });
        StandInApi(*s, 0x00645258, reinterpret_cast<const void*>(&FindWindowExA), 16, [](Regs& r) {
            Record({0xF1, r.args[0], r.args[1], r.args[2], Hash(reinterpret_cast<const char*>(r.args[3]))});
            return g_case->running;
        });
        StandInApi(*s, 0x00645228, reinterpret_cast<const void*>(&ShowWindow), 8, [](Regs& r) {
            Record({0x5A0, r.args[0], r.args[1]});
            return 0u;
        });
        StandInApi(*s, 0x0064522C, reinterpret_cast<const void*>(&BringWindowToTop), 4, [](Regs& r) {
            Record({0xB2, r.args[0]});
            return 1u;
        });
        StandInApi(*s, 0x00645210, reinterpret_cast<const void*>(&SetForegroundWindow), 4, [](Regs& r) {
            Record({0xF0E, r.args[0]});
            return 1u;
        });
        StandInApi(*s, 0x00645230, reinterpret_cast<const void*>(&InvalidateRect), 12, [](Regs& r) {
            Record({0x1AB, r.args[0], r.args[1], r.args[2]});
            return 1u;
        });
        StandInApi(*s, 0x0064520C, reinterpret_cast<const void*>(&MapVirtualKeyA), 8, StandInMapVirtualKey);
        s->At({Resolve(reinterpret_cast<const void*>(&Sys::CreateSdlWindow))}, 0, [](Regs&) {
            Record({0xCDE});
            Window() = g_case->window1;
            return 0u;
        });
        s->At({0x005F56E0, AddressOf(&Platform::ProcessWindowEvents)}, 0, [](Regs&) {
            Record({0xE7E, Window()});
            Window() = g_case->window2;
            return 0u;
        });
        s->At({0x00625138}, 0, [](Regs& r) { Record({0x7173, r.args[0]}); return 0x5EED0000u + Window(); });
        s->At({0x00623B96}, 0, [](Regs& r) { Record({0x5EED, r.args[0]}); return 0u; });
        s->At({0x005F5740, AddressOf(&Platform::FindCdDrive)}, 0, [](Regs& r) {
            Record({0xCD, Hash(reinterpret_cast<const char*>(r.args[0]))});
            Window() += 0x10; // (each use of the window reads it again)
            return static_cast<uint32_t>('D');
        });
        s->At({0x005F59D0, AddressOf(&Platform::InitDirectSound)}, 0, [](Regs& r) {
            Record({0xD5, r.args[0]});
            return 0u;
        });
        s->At({0x006234FE}, 0, StandInSprintf);
        s->At({0x004E1810, AddressOf(&Sys::WriteDefaultSetting)}, 0, [](Regs& r) {
            Record({0x1810, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
            ++Window();
            return 0u;
        });
        s->At({0x005F44F0, AddressOf(&Platform::InitInput)}, 0, [](Regs& r) {
            Record({0x44F0, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
            return 1u;
        });
        // mbstowcs as VC7.1's does it in the "C" locale (each byte widened), independent of our C runtime
        s->At({0x0062510D}, 0, [](Regs& r) {
            auto* to = reinterpret_cast<wchar_t*>(r.args[0]);
            auto* from = reinterpret_cast<const uint8_t*>(r.args[1]);
            uint32_t count = 0;
            for (; count < r.args[2]; ++count) {
                to[count] = from[count];
                if (!from[count])
                    break;
            }
            return count;
        });
        s->At({0x005F3BF0, AddressOf(&Platform::SetKeyName)}, 0, [](Regs& r) {
            Record({0x3BF0, r.args[0], HashWide(reinterpret_cast<const wchar_t*>(r.args[1]))});
            return 0u;
        });
        s->At({0x004E1EF0, AddressOf(&Sys::Main)}, 0, [](Regs& r) {
            int argc = static_cast<int>(r.args[0]);
            auto** argv = reinterpret_cast<char**>(r.args[1]);
            Record({0x3A1, r.args[0]});
            for (int i = 0; i < argc; ++i)
                Record({static_cast<uint32_t>(argv[i] - g_commandLine), Hash(argv[i])});
            Record({argv[argc] == nullptr, HashBytes(reinterpret_cast<const uint8_t*>(g_commandLine), 256)});
            Window() = 0xF000;
            return 0u;
        });
        s->At({0x005F5A00, AddressOf(&Platform::ReleaseDirectSound)}, 0, plain(0x5A00));
        s->At({0x005F39E0}, 0, plain(0x39E0));
        s->At({0x004908C0, AddressOf(&Nx::RemoveLoadingScreen)}, 0, plain(0x08C0));
        s->At({0x005F55E0, AddressOf(&Platform::ReleaseDirect3D)}, 0, plain(0x55E0));
        s->At({0x00624B20}, 0, plain(0xCE));
        s->At({0x005BC340, AddressOf(&Mem::ShutDownManager)}, 0, plain(0xC340));
        s->At({0x00624B0F}, 0, [](Regs& r) -> uint32_t {
            Record({0xE817, r.args[0]});
            throw GameExit{r.args[0]};
        });

        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            g_loads = 0;
            g_loadName = 0;
            if (original) {
                // PartyMod: its set-up before the command line, its window, the window not shown, DirectInput
                // not released
                RunPatch(0x004E26DC, Branch(0xE8, 0x004E26DC, reinterpret_cast<const void*>(&PcSetupThenParseNetworkParams)));
                RunPatch(0x004E2767, Branch(0xE8, 0x004E2767, reinterpret_cast<const void*>(&Sys::CreateSdlWindow)));
                RunPatch(0x004E277D, std::vector<uint8_t>(20, 0x90));
                RunPatch(0x004E2C16, std::vector<uint8_t>(5, 0x90));
            }
            for (const WinMainCase& c : kWinMainCases) {
                g_case = &c;
                g_gsiIndex = 0;
                Window() = 0x0BAD;
                char commandLine[256] = {};
                strcpy_s(commandLine, c.commandLine);
                g_commandLine = commandLine;
                try {
                    int result = original ? Original<int(WINAPI*)(HINSTANCE, HINSTANCE, char*, int)>(0x004E2680)(
                                                kInstance, nullptr, commandLine, SW_SHOWDEFAULT)
                                          : Sys::WinMain(kInstance, nullptr, commandLine, SW_SHOWDEFAULT);
                    Record({0xCA5E, static_cast<uint32_t>(result)});
                } catch (const GameExit& exit) {
                    Record({0xE817, exit.code});
                }
                Record({HashBytes(reinterpret_cast<const uint8_t*>(commandLine), sizeof(commandLine)), Window()});
            }
            EndRunPatches();
            traces[run] = g_trace;
            loads[run] = g_loads;
            loadNames[run] = g_loadName;
        }
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1],
            "WinMain: back end up or not, running already, no window, arguments (spaces, 20 at most), defaults, "
            "key names, PartyMod's set-up and window, the shutdown");
    // PartyMod's exe patch loads its DLL (the name at 0x0064B198) in the original (once per run that gets that
    // far); ours doesn't
    Check(loads[0] == static_cast<int>(sizeof(kWinMainCases) / sizeof(kWinMainCases[0])) &&
              loadNames[0] == Hash(reinterpret_cast<const char*>(0x0064B198)) && loads[1] == 0,
          "WinMain: the original loads the exe patch's DLL, ours doesn't");
}

} // namespace

SELFTEST(Early, TestParseNetworkParams, RemoveStandIns);
SELFTEST(Late, TestParseNetworkParamsLate);
SELFTEST(Early, TestWinMain, RemoveStandIns);
