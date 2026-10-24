// Self-tests for the game's configuration 0x005B5C90-0x005B6370 (the command line's options, the registry's language,
// the names): each original against ours on identical stand-in state; the registry functions, the C runtime's
// tolower / strcmp / sprintf (not started offline) and the debug output are stood in for by recording code.

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/sys/game_config.h"
#include "hook/gs4_testkit.h"

namespace {

using namespace Gs4Tests;

LSTATUS g_cfgOpen[2]; // HKCU's, HKLM's
DWORD g_cfgLanguage[2];
int g_cfgQueries;

bool CallStartsWith(void* code, const char* argument, const char* option)
{
    bool result;
    __asm {
        push ebx
        mov ecx, argument
        mov ebx, option
        call code
        pop ebx
        mov result, al
    }
    return result;
}

void StandInConfigCalls(StandIns& s)
{
    s.At({0x006254C9}, 0, [](Regs& r) { return static_cast<uint32_t>(std::tolower(static_cast<int>(r.args[0]))); });
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(std::strcmp(reinterpret_cast<const char*>(r.args[0]),
                                                 reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    StandInApi(s, 0x00645000, reinterpret_cast<const void*>(&RegCreateKeyA), 12, [](Regs& r) {
        int which = r.args[0] == reinterpret_cast<uint32_t>(HKEY_CURRENT_USER) ? 0 : 1;
        Record({0x0C1E, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        if (g_cfgOpen[which] == ERROR_SUCCESS)
            *reinterpret_cast<HKEY*>(r.args[2]) = reinterpret_cast<HKEY>(0x4E7A0010 + which);
        return static_cast<uint32_t>(g_cfgOpen[which]);
    });
    StandInApi(s, 0x00645008, reinterpret_cast<const void*>(&RegQueryValueExA), 24, [](Regs& r) {
        auto* size = reinterpret_cast<DWORD*>(r.args[5]);
        Record({0x0C1F, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3] != 0, *size});
        ++g_cfgQueries;
        int which = r.args[0] == 0x4E7A0010 ? 0 : 1;
        *reinterpret_cast<DWORD*>(r.args[3]) = REG_DWORD;
        std::memcpy(reinterpret_cast<void*>(r.args[4]), &g_cfgLanguage[which], 4);
        return 0u;
    });
    StandInApi(s, 0x00645014, reinterpret_cast<const void*>(&RegCloseKey), 4, [](Regs& r) {
        Record({0x0C20, r.args[0]});
        return 0u;
    });
}

// The settings it keeps, recorded.
void RecordSettings()
{
    Record({static_cast<uint32_t>(GameConfig::Language()), static_cast<uint32_t>(GameConfig::Territory()),
            static_cast<uint32_t>(GameConfig::Hardware()), static_cast<uint32_t>(GameConfig::DisplayType()),
            static_cast<uint32_t>(GameConfig::FrameRate()), GameConfig::FromCD(), GameConfig::ExtraMemory(),
            GameConfig::Bootstrap(), GameConfig::NtscFlag(), Hash(&GameVariable<char>(0x007CEE20)),
            Hash(&GameVariable<char>(0x007CEEA0)), Hash(&GameVariable<char>(0x007CEF20))});
}

void TestGameConfig()
{
    SaveGlobals({{0x007CEFA0, 0x10, {}}, {0x00684FA8, 1, {}}, {0x00684FB0, 8, {}}, {0x007CEE20, 0x180, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInConfigCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto reset = [] {
            std::memset(&GameVariable<uint8_t>(0x007CEFA0), 0x5A, 0x10);
            GameVariable<bool>(0x00684FA8) = false;
            GameConfig::DisplayType() = 1;
            GameConfig::FrameRate() = 30;
            std::memset(&GameVariable<uint8_t>(0x007CEE20), 'x', 0x180);
            GameVariable<char>(0x007CEE20 + 0x7F) = 0;
            GameVariable<char>(0x007CEEA0 + 0x7F) = 0;
            GameVariable<char>(0x007CEF20 + 0x7F) = 0;
        };
        // ArgumentStartsWith (registers), GetArgumentValue, GotArgument
        {
            const char* pairs[][2] = {{"FrameRate=50", "FrameRate"}, {"Frame", "FrameRate"}, {"framerate", "FrameRate"},
                                      {"", ""}, {"abc", ""}, {"FrameRate", "FrameRate"}};
            for (auto& p : pairs) {
                void* code = original ? reinterpret_cast<void*>(0x005B5C90)
                                      : reinterpret_cast<void*>(&GameConfig::ArgumentStartsWithStub);
                Record({CallStartsWith(code, p[0], p[1])});
            }
            void* code = original ? reinterpret_cast<void*>(0x005B5C90)
                                  : reinterpret_cast<void*>(&GameConfig::ArgumentStartsWithStub);
            Record({CallStartsWith(code, nullptr, "a"), CallStartsWith(code, "a", nullptr)});
            char* lines[][6] = { // (rows end in nulls)
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate=60"), const_cast<char*>("cd"), nullptr},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate="), const_cast<char*>("50"),
                 const_cast<char*>("NotCD"), nullptr},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate"), const_cast<char*>("=60"),
                 const_cast<char*>("Pal"), nullptr},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate"), const_cast<char*>("="),
                 const_cast<char*>("50"), const_cast<char*>("FrameRate=70"), nullptr},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate"), nullptr},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate="), nullptr},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRateX=1"), const_cast<char*>("BOOTSTRAP"),
                 const_cast<char*>("menu"), const_cast<char*>("args"), nullptr},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("bootstrap"), const_cast<char*>("back"),
                 const_cast<char*>("NotBootstrap"), const_cast<char*>("NTSC"), const_cast<char*>("NoExtraMem")},
                // (the arguments past argc are there: reading them would show)
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate="), const_cast<char*>("BEYOND")},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate"), const_cast<char*>("="),
                 const_cast<char*>("BEYOND")},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("Bootstrap"), const_cast<char*>("dest"),
                 const_cast<char*>("BEYOND")},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("Bootstrap"), const_cast<char*>("dest"),
                 const_cast<char*>("args"), const_cast<char*>("BEYOND")},
                {const_cast<char*>("thug2.exe"), const_cast<char*>("FrameRate=500"), nullptr},
            };
            int counts[] = {3, 4, 4, 5, 2, 2, 5, 6, 2, 3, 3, 4, 2};
            for (int i = 0; i < 13; ++i) {
                char** argv = lines[i];
                const char* value = original
                                        ? Original<const char*(__cdecl*)(const char*, int, char**)>(0x005B5D30)("FrameRate", counts[i], argv)
                                        : GameConfig::GetArgumentValue("FrameRate", counts[i], argv);
                Record({value ? Hash(value) : 0u});
                for (const char* option : {"CD", "notcd", "Bootstrap", "NTSC", "Pal", "zzz"}) {
                    bool got = original ? Original<bool(__cdecl*)(const char*, int, char**)>(0x005B5DD0)(option, counts[i], argv)
                                        : GameConfig::GotArgument(option, counts[i], argv);
                    Record({got});
                }
                bool none = original ? Original<bool(__cdecl*)(const char*, int, char**)>(0x005B5DD0)(nullptr, counts[i], argv)
                                     : GameConfig::GotArgument(nullptr, counts[i], argv);
                Record({none});
                // Init: the registry (both keys, languages) and the command line
                for (int registry = 0; registry < 4; ++registry) {
                    reset();
                    g_cfgOpen[0] = registry == 1 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
                    g_cfgOpen[1] = registry == 3 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
                    g_cfgLanguage[0] = registry == 0 ? 2 : 0;
                    g_cfgLanguage[1] = registry == 2 ? 3 : registry == 1 ? 9 : 0;
                    g_cfgQueries = 0;
                    if (original)
                        Original<void(__cdecl*)(int, char**)>(0x005B60B0)(counts[i], argv);
                    else
                        GameConfig::Init(counts[i], argv);
                    Record({static_cast<uint32_t>(g_cfgQueries)});
                    RecordSettings();
                }
            }
        }
        // the names
        {
            using NameFn = const char*(__cdecl*)();
            struct Name {
                uint32_t address;
                NameFn ours;
                int& (*setting)();
            };
            const Name names[] = {{0x005B5F20, GameConfig::GetLanguageName, GameConfig::Language},
                                  {0x005B5F90, GameConfig::GetTerritoryName, GameConfig::Territory},
                                  {0x005B5FC0, GameConfig::GetHardwareName, GameConfig::Hardware},
                                  {0x005B6020, GameConfig::GetDisplayTypeName, GameConfig::DisplayType},
                                  {0x005B6040, GameConfig::GetPlatformExtension, GameConfig::Hardware}};
            for (const Name& n : names) {
                for (int value = -1; value <= 10; ++value) {
                    n.setting() = value;
                    const char* text = original ? Original<NameFn>(n.address)() : n.ours();
                    Record({reinterpret_cast<uint32_t>(text)});
                }
            }
            for (int display : {0, 1, 2}) {
                for (bool on : {true, false}) {
                    GameConfig::DisplayType() = display;
                    GameConfig::NtscFlag() = !on;
                    if (original)
                        Original<void(__cdecl*)(bool)>(0x005B6090)(on);
                    else
                        GameConfig::SetNtscFlag(on);
                    Record({GameConfig::NtscFlag()});
                }
            }
            for (bool bootstrap : {false, true}) {
                GameConfig::Bootstrap() = bootstrap;
                Record({reinterpret_cast<uint32_t>(original ? Original<NameFn>(0x005B6250)() : GameConfig::GetBootstrapPrefix()),
                        reinterpret_cast<uint32_t>(original ? Original<NameFn>(0x005B5F10)() : GameConfig::GetPathPrefix())});
            }
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "the game's configuration: command line, registry, names (0x005B5C90-0x005B6370)");
}

} // namespace

SELFTEST(Early, TestGameConfig, Gs4Tests::RestoreGlobals);
