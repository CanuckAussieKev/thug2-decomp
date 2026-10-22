// Self-tests for the platform layer's DirectInput side (src/game/sys/platform_input.cpp, 0x005F2E00-0x005F3C10):
// the original and ours over the same game memory, with recording stand-ins for DirectInput, its devices and
// effects (scripted results: polls, acquires and reads failing or not, devices made or not) - the reads, the
// bindings, the joystick's controls, the keys, the enumeration callbacks, the release, the registry loader (under
// a stand-in HKEY_CURRENT_USER) and the key names.

#include <windows.h>

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

#include "game/sys/platform_input.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AppMainTests;

// Argument counts (after `this`) of IDirectInput8A, IDirectInputDevice8A and IDirectInputEffect, in vtable order.
const uint8_t kDirectInputArgs[11] = {2, 0, 0, 3, 4, 1, 2, 2, 3, 5, 4};
const uint8_t kInputDeviceArgs[32] = {2, 0, 0, 1, 3, 2, 2, 0, 0, 2, 4, 1, 1, 2, 3, 1,
                                      2, 3, 4, 3, 2, 1, 1, 3, 1, 0, 4, 4, 4, 3, 3, 1};
const uint8_t kEffectArgs[13] = {2, 0, 0, 3, 1, 2, 2, 2, 0, 1, 0, 0, 1};

enum Slot { kRelease = 2, kCreateDevice = 3, kEnumObjects = 4, kSetProperty = 6, kAcquire = 7, kGetDeviceState = 9,
            kSetDataFormat = 11, kSetCooperativeLevel = 13, kCreateEffect = 18, kPoll = 25 };
enum Tag : uint32_t { kDirectInputTag = 0xD1000000, kDeviceTag = 0xD1100000, kEffectTag = 0xD1200000 };

// The game memory the code uses: the DirectInput side's globals (0x007D42C8-0x007D69DC) and the window.
const uint32_t kInputStart = 0x007D42C8, kInputEnd = 0x007D69DC;

// What the stand-ins answer, in order (S_OK once a list runs out), and what reads fill in.
struct Script {
    std::deque<HRESULT> poll, acquire, state, create, effect;
    std::vector<uint8_t> content; // a read's bytes (repeated)
    std::vector<DIDEVICEOBJECTINSTANCEA> objects; // EnumObjects's
    bool newDevices = true;
    static HRESULT Next(std::deque<HRESULT>& list)
    {
        if (list.empty())
            return S_OK;
        HRESULT result = list.front();
        list.pop_front();
        return result;
    }
};
Script g_script;

// The original's and our axis callback, recorded as one value.
uint32_t g_callbacks[3];

int g_mismatches;
bool CompareRuns(const char* what, const std::function<void(bool)>& body)
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        g_trace.clear();
        body(run == 0);
        traces[run] = g_trace;
    }
    if (traces[0] == traces[1])
        return true;
    if (++g_mismatches <= 6) {
        Log("  %s: trace sizes original %u ours %u", what, static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
        for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 6; ++i) {
            if (traces[0][i] != traces[1][i]) {
                Log("  %s entry %u: original %08X ours %08X", what, static_cast<unsigned>(i), traces[0][i],
                    traces[1][i]);
                ++shown;
            }
        }
    }
    return false;
}

// The game's C runtime isn't started offline: the original loader's sprintf and isdigit stood in.
int __cdecl StandInSprintf(char* buffer, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int n = std::vsnprintf(buffer, 64, format, args);
    va_end(args);
    return n;
}
int __cdecl StandInIsDigit(int c) { return c >= '0' && c <= '9' ? 4 : 0; }

// HKEY_CURRENT_USER redirected onto a scratch key for the registry loader (never the real one).
struct ScratchRegistry {
    HKEY root = nullptr;
    bool ok = false;
    char name[64];
    ScratchRegistry()
    {
        std::snprintf(name, sizeof(name), "Software\\thug2-decomp-test-input-%lu", GetCurrentProcessId());
        if (RegCreateKeyExA(HKEY_CURRENT_USER, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr) !=
            ERROR_SUCCESS)
            return;
        ok = RegOverridePredefKey(HKEY_CURRENT_USER, root) == ERROR_SUCCESS;
    }
    ~ScratchRegistry()
    {
        if (!root)
            return;
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegDeleteTreeA(root, nullptr);
        RegCloseKey(root);
        RegDeleteKeyA(HKEY_CURRENT_USER, name);
    }
};

void TestPlatformInput()
{
    g_mismatches = 0;
    GameMemory memory({{kInputStart, kInputEnd - kInputStart}, {0x007D6A28, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* directInput = com.Make(kDirectInputTag, kDirectInputArgs, 11);
    FakeCom* devices[6];
    for (int i = 0; i < 6; ++i)
        devices[i] = com.Make(kDeviceTag + 0x10000 * i, kInputDeviceArgs, 32);
    FakeCom* effects[4];
    for (int i = 0; i < 4; ++i)
        effects[i] = com.Make(kEffectTag + 0x10000 * i, kEffectArgs, 13);
    int nextDevice = 0, nextEffect = 0;

    g_callbacks[0] = 0x005F3130;
    g_callbacks[1] = reinterpret_cast<uint32_t>(&Platform::AddJoystickAxis);
    g_callbacks[2] = AddressOf(&Platform::AddJoystickAxis);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) {
        for (uint32_t callback : g_callbacks) {
            if (word == callback)
                return 0xCA11BAC0u;
        }
        return standard(word);
    };

    for (int i = 0; i < 6; ++i) {
        uint32_t tag = kDeviceTag + 0x10000 * i;
        com.On(tag, kPoll, [](Regs&) { return static_cast<uint32_t>(Script::Next(g_script.poll)); });
        com.On(tag, kAcquire, [](Regs&) { return static_cast<uint32_t>(Script::Next(g_script.acquire)); });
        com.On(tag, kGetDeviceState, [](Regs& r) {
            auto* out = reinterpret_cast<uint8_t*>(r.args[2]);
            for (uint32_t i = 0; i < r.args[1] && !g_script.content.empty(); ++i)
                out[i] = g_script.content[i % g_script.content.size()];
            return static_cast<uint32_t>(Script::Next(g_script.state));
        });
        com.On(tag, kSetProperty, [](Regs& r) {
            Record({r.args[1]});
            auto* words = reinterpret_cast<const uint32_t*>(r.args[2]);
            for (int i = 0; i < 6; ++i)
                Record({words[i]});
            return 0u;
        });
        com.On(tag, kEnumObjects, [](Regs& r) {
            using Callback = BOOL(CALLBACK*)(LPCDIDEVICEOBJECTINSTANCEA, LPVOID);
            auto callback = reinterpret_cast<Callback>(r.args[1]);
            for (const DIDEVICEOBJECTINSTANCEA& object : g_script.objects)
                Record({static_cast<uint32_t>(callback(&object, reinterpret_cast<void*>(r.args[2])))});
            return 0u;
        });
        com.On(tag, kCreateEffect, [&](Regs& r) {
            auto* effect = reinterpret_cast<const DIEFFECT*>(r.args[2]);
            Record({effect->dwSize, effect->dwFlags, effect->dwDuration, effect->dwSamplePeriod, effect->dwGain,
                    effect->dwTriggerButton, effect->dwTriggerRepeatInterval, effect->cAxes, effect->rgdwAxes[0],
                    effect->rgdwAxes[1], static_cast<uint32_t>(effect->rglDirection[0]),
                    static_cast<uint32_t>(effect->rglDirection[1]), reinterpret_cast<uint32_t>(effect->lpEnvelope),
                    effect->cbTypeSpecificParams, *static_cast<const uint32_t*>(effect->lpvTypeSpecificParams),
                    effect->dwStartDelay});
            HRESULT result = Script::Next(g_script.effect);
            if (SUCCEEDED(result))
                *reinterpret_cast<void**>(r.args[3]) = effects[nextEffect++ % 4];
            return static_cast<uint32_t>(result);
        });
    }
    com.On(kDirectInputTag, kCreateDevice, [&](Regs& r) {
        auto* guid = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({guid[0], guid[1], guid[2], guid[3]});
        HRESULT result = Script::Next(g_script.create);
        if (SUCCEEDED(result))
            *reinterpret_cast<void**>(r.args[2]) = devices[2 + nextDevice++ % 4];
        return static_cast<uint32_t>(result);
    });

    auto joysticks = [] { return reinterpret_cast<FakeCom**>(0x007D6318); };
    auto& keyboard = GameVariable<FakeCom*>(0x007D69D4);
    auto& mouse = GameVariable<FakeCom*>(0x007D69D8);
    Lcg random(0); // (reseeded for each run: both see the same values)
    auto start = [&](uint32_t seed) {
        random = Lcg(seed * 2654435761u);
        g_script = Script();
        nextDevice = nextEffect = 0;
        memory.Fill(seed);
        GameVariable<FakeCom*>(0x007D69D0) = directInput;
        joysticks()[0] = joysticks()[1] = nullptr;
        keyboard = mouse = nullptr;
        GameVariable<FakeCom*>(0x007D6668) = GameVariable<FakeCom*>(0x007D666C) = nullptr;
        GameVariable<uint32_t>(0x007D42C8) = GameVariable<uint32_t>(0x007D42CC) = 0;
    };
    auto finish = [&]() { memory.Dump(g_trace); };
    bool ok = true;

    // The joystick and mouse reads: no device, every step failing in turn, the last state kept.
    const HRESULT kFail = static_cast<HRESULT>(0x8007001E);
    for (int c = 0; c < 64; ++c) {
        ok &= CompareRuns("ReadJoystick / ReadMouse", [&](bool original) {
            start(0x10000 + c);
            int port = c & 1;
            if (c % 8 != 7)
                joysticks()[port] = devices[port];
            if (c % 8 != 6)
                mouse = devices[1];
            // (each list its own failures: the poll, the acquire and the read fail in every combination)
            int shift = 1;
            for (std::deque<HRESULT>* list : {&g_script.poll, &g_script.acquire, &g_script.state}) {
                for (int i = 0; i < 4; ++i)
                    list->push_back(random.Below(3) == 0 || ((c >> shift) & 1 && i == 0) ? kFail : S_OK);
                ++shift;
            }
            // The read: a still mouse (x and y 0) half the time, the last movement anything from -6 to 6 or far.
            g_script.content.resize(0x110);
            for (uint8_t& b : g_script.content)
                b = static_cast<uint8_t>(random.Next());
            if (c & 16)
                std::memset(g_script.content.data(), 0, 8); // still
            else if (c & 32)
                std::memset(g_script.content.data() + 4 * (c & 1), 0, 4); // x or y still
            const LONG last[] = {0, 1, 2, 3, -1, -2, -3, 6, -6, 100, -100, 0x7FFFFFFF};
            GameVariable<LONG>(0x007D6420) = last[random.Below(12)];
            GameVariable<LONG>(0x007D6424) = last[random.Below(12)];
            void* joystick = original ? Original<void*(__cdecl*)(int)>(0x005F2E00)(port)
                                      : static_cast<void*>(Platform::ReadJoystick(port));
            Record({reinterpret_cast<uint32_t>(joystick)});
            void* read = original ? Original<void*(__cdecl*)()>(0x005F2E90)()
                                  : static_cast<void*>(Platform::ReadMouse());
            Record({reinterpret_cast<uint32_t>(read)});
            finish();
        });
    }

    // The bindings: none, one anywhere in the 24 (the last too), one just after them.
    for (int c = 0; c < 60; ++c) {
        ok &= CompareRuns("HasKeyBindings / HasPadD0Bindings", [&](bool original) {
            start(0x20000 + c);
            std::memset(reinterpret_cast<void*>(0x007D6770), 0xFF, 0x260);
            if (c % 5)
                GameVariable<int32_t>(0x007D6770 + 4 * random.Below(0x98)) = static_cast<int32_t>(random.Below(300));
            if (c % 7 == 0)
                GameVariable<int32_t>(0x007D6770 + 0x130 * (c & 1) + 4 * (c % 3 ? 23 : 24)) = 0;
            for (int port = 0; port < 2; ++port) {
                bool keys = original ? Original<bool(__cdecl*)(int)>(0x005F2F80)(port) : Platform::HasKeyBindings(port);
                bool pad = original ? Original<bool(__cdecl*)(int)>(0x005F2FC0)(port)
                                    : Platform::HasPadD0Bindings(port);
                Record({keys, pad});
            }
            finish();
        });
    }

    // The joystick's controls, the hat at every boundary.
    ok &= CompareRuns("JoystickAxis", [&](bool original) {
        start(0x30000);
        DIJOYSTATE2 state;
        auto* bytes = reinterpret_cast<uint8_t*>(&state);
        for (size_t i = 0; i < sizeof(state); ++i)
            bytes[i] = static_cast<uint8_t>(random.Next());
        const DWORD hats[] = {0xFFFFFFFF, 0,     2249,  2250,  6750,  6751,  11249, 11250, 15750, 15751, 20249,
                              20250,      24750, 24751, 29249, 29250, 33750, 33751, 35999, 36000, 9000,  0x80000000};
        for (DWORD hat : hats) {
            state.rgdwPOV[0] = hat;
            for (int control = -1; control <= 12; ++control) {
                int value = original ? Original<int(__cdecl*)(const DIJOYSTATE2*, int)>(0x005F3000)(&state, control)
                                     : Platform::JoystickAxis(&state, control);
                Record({static_cast<uint32_t>(value)});
            }
        }
        finish();
    });

    // The keys: Escape down / up this and last frame, -1, the others.
    for (int c = 0; c < 8; ++c) {
        ok &= CompareRuns("KeyDown / KeyName", [&](bool original) {
            start(0x40000 + c);
            GameVariable<uint32_t>(0x007D666C) = 0x80000000; // (the byte before the keys: not a key)
            GameVariable<uint8_t>(0x007D6671) = c & 1 ? 0x80 : 0;
            GameVariable<uint8_t>(0x007D6321) = c & 2 ? 0x80 : 0;
            for (int code = -1; code < 256; ++code) {
                bool down = original ? Original<bool(__cdecl*)(int)>(0x005F30E0)(code) : Platform::KeyDown(code);
                Record({down});
                if (code >= 0) {
                    void* name = original ? Original<void*(__cdecl*)(int)>(0x005F3120)(code)
                                          : static_cast<void*>(Platform::KeyName(code));
                    Record({reinterpret_cast<uint32_t>(name)});
                }
            }
            finish();
        });
    }

    // The axis callback on its own: either joystick or another device, force feedback or not, counts 0-3.
    for (int c = 0; c < 48; ++c) {
        ok &= CompareRuns("AddJoystickAxis", [&](bool original) {
            start(0x50000 + c);
            joysticks()[0] = devices[0];
            joysticks()[1] = devices[1];
            int which = c % 3;
            GameVariable<uint32_t>(0x007D42C8 + 4 * (which & 1)) = (c / 3) % 4;
            DIDEVICEOBJECTINSTANCEA object{};
            object.dwType = random.Next();
            object.dwFlags = (c / 12) % 2 ? DIDOI_FFACTUATOR | 0x100 : 0x100;
            object.dwFFForceResolution = random.Next();
            using Callback = BOOL(CALLBACK*)(LPCDIDEVICEOBJECTINSTANCEA, LPVOID);
            Callback callback = original ? reinterpret_cast<Callback>(OriginalFunction(0x005F3130))
                                         : &Platform::AddJoystickAxis;
            Record({static_cast<uint32_t>(callback(&object, devices[which]))});
            finish();
        });
    }

    // The device callback: joysticks and gamepads named by port 0, 1, both or neither (the type's upper bits
    // set or not), keyboards, mice, other types; devices made or not; already there or not; force feedback.
    const DWORD kTypes[] = {DI8DEVTYPE_JOYSTICK, DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_KEYBOARD, DI8DEVTYPE_MOUSE,
                            DI8DEVTYPE_DRIVING, 0x1A, 0x10};
    for (int c = 0; c < 224; ++c) {
        ok &= CompareRuns("AddInputDevice", [&](bool original) {
            start(0x60000 + c);
            DIDEVICEINSTANCEA device{};
            device.dwSize = sizeof(device);
            auto* guidWords = reinterpret_cast<uint32_t*>(&device.guidInstance);
            for (int i = 0; i < 4; ++i)
                guidWords[i] = random.Next();
            device.dwDevType = kTypes[c % 7] | (c & 8 ? 0x10200 : 0);
            int named = (c / 7) % 4; // none, port 0, port 1, both
            for (int port = 0; port < 2; ++port) {
                GUID* guid = reinterpret_cast<GUID*>(0x007D67D0 + 0x130 * port);
                if (named & (port + 1))
                    *guid = device.guidInstance;
                else if (c & 16)
                    *guid = GUID{device.guidInstance.Data1 ^ 1, device.guidInstance.Data2, device.guidInstance.Data3,
                                 {device.guidInstance.Data4[0], 0, 0, 0, 0, 0, 0, 0}};
            }
            if (c & 32)
                keyboard = devices[0];
            if (c & 64)
                mouse = devices[1];
            if (c % 5 == 4)
                g_script.create.push_back(E_FAIL);
            if (c % 3 == 2)
                g_script.effect.push_back(E_FAIL);
            for (int i = 0; i < (c / 2) % 4; ++i) {
                DIDEVICEOBJECTINSTANCEA object{};
                object.dwType = 0x100 * i + c;
                object.dwFlags = i % 2 ? 0 : DIDOI_FFACTUATOR;
                object.dwFFForceResolution = 1000 + i;
                g_script.objects.push_back(object);
            }
            using Callback = BOOL(CALLBACK*)(LPCDIDEVICEINSTANCEA, LPVOID);
            Callback callback = original ? reinterpret_cast<Callback>(OriginalFunction(0x005F31D0))
                                         : &Platform::AddInputDevice;
            Record({static_cast<uint32_t>(callback(&device, reinterpret_cast<void*>(0x1234)))});
            finish();
        });
    }

    // The keyboard read: read, lost / not acquired (acquired again or not), another error.
    const HRESULT kReads[] = {S_OK, static_cast<HRESULT>(DIERR_INPUTLOST), static_cast<HRESULT>(DIERR_NOTACQUIRED),
                              E_FAIL};
    for (int c = 0; c < 16; ++c) {
        ok &= CompareRuns("ReadKeyboard", [&](bool original) {
            start(0x70000 + c);
            keyboard = devices[0];
            g_script.state = {kReads[c % 4], (c & 8) ? E_FAIL : S_OK};
            g_script.acquire = {(c & 4) ? E_FAIL : S_OK};
            g_script.content = {static_cast<uint8_t>(c), 0x80, 0, 0x81, 0x40};
            if (original)
                Original<void(__cdecl*)()>(0x005F3400)();
            else
                Platform::ReadKeyboard();
            finish();
        });
    }

    // The hex numbers: digits, upper- and lower-case letters, other characters, overflow.
    const char* kHex[] = {"", "0", "7FFF", "DEADbeef", "1234ABCDEF9", "G1", "-1", "0x10", "FF:FF", "  12", "9AZ"};
    s.Bytes(0x00625DAA, Branch(0xE9, 0x00625DAA, reinterpret_cast<void*>(&StandInIsDigit)));
    ok &= CompareRuns("ParseHex", [&](bool original) {
        start(0x80000);
        for (const char* text : kHex) {
            const char* cursor = text;
            uint32_t value = original ? Original<uint32_t(__cdecl*)(const char**)>(0x005F3470)(&cursor)
                                      : Platform::ParseHex(&cursor);
            Record({value, static_cast<uint32_t>(cursor - text)});
        }
        finish();
    });

    // The release: each of the joysticks, effects, keyboard, mouse and DirectInput there or not.
    for (int c = 0; c < 64; ++c) {
        ok &= CompareRuns("ReleaseInput", [&](bool original) {
            start(0x90000 + c);
            if (c & 1)
                joysticks()[0] = devices[0];
            if (c & 2)
                joysticks()[1] = devices[1];
            if (c & 4)
                GameVariable<FakeCom*>(0x007D6668) = effects[0];
            if (c & 8)
                GameVariable<FakeCom*>(0x007D666C) = effects[1];
            if (c & 16)
                keyboard = devices[2];
            mouse = devices[3];
            if (c & 32)
                GameVariable<FakeCom*>(0x007D69D0) = nullptr;
            if (original)
                Original<void(__cdecl*)()>(0x005F39E0)();
            else
                Platform::ReleaseInput();
            finish();
        });
    }

    // The key names: the whole table, then single names (the entry returned).
    ok &= CompareRuns("InitKeyNames / SetKeyName", [&](bool original) {
        start(0xA0000);
        if (original)
            Original<void(__cdecl*)()>(0x005F3C10)();
        else
            Platform::InitKeyNames();
        const wchar_t* names[] = {L"", L"X", L"Fourteen chars", L"\x00E4\x00F6"};
        for (int code : {0, 1, 0x39, 0xFF}) {
            for (const wchar_t* name : names) {
                void* entry = original ? Original<void*(__cdecl*)(int, const wchar_t*)>(0x005F3BF0)(code, name)
                                       : static_cast<void*>(Platform::SetKeyName(code, name));
                Record({reinterpret_cast<uint32_t>(entry)});
            }
        }
        finish();
    });

    // The registry loader, under a stand-in HKEY_CURRENT_USER: no key, values missing, in and out of range,
    // the wrong type, GUIDs whole and short, bindings for both ports.
    {
        ScratchRegistry registry;
        if (!registry.ok) {
            Log("self-test: the platform layer's registry loader skipped (no stand-in HKEY_CURRENT_USER)");
        } else {
            s.Bytes(0x006234FE, Branch(0xE9, 0x006234FE, reinterpret_cast<void*>(&StandInSprintf)));
            const char kKey[] = "Test\\Settings";
            const DWORD kDistances[] = {0, 1, 50, 100, 101, 0xFFFFFFFF};
            const char* kGuids[] = {"",
                                    "0",
                                    "01234567-89AB-CDEF-0123-456789ABCDEF",
                                    "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF",
                                    "6F1D2B60-D5A0-11CF-BFC7-444553540000",
                                    "abcdef01-2345-6789-abcd-ef0123456789",
                                    "12345678-9ABC"};
            for (int c = 0; c < 40; ++c) {
                ok &= CompareRuns("LoadRegistryOptions", [&](bool original) {
                    start(0xB0000 + c);
                    RegDeleteTreeA(registry.root, nullptr);
                    HKEY key = nullptr;
                    if (c % 10 != 9)
                        RegCreateKeyExA(registry.root, kKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
                    if (key) {
                        auto setDword = [&](const char* name, DWORD value) {
                            RegSetValueExA(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), 4);
                        };
                        if (c % 4 != 1)
                            setDword("distance", kDistances[c % 6]);
                        if (c % 3 != 2)
                            setDword("options", (c * 0x2B) & 0x1FF);
                        if (c % 5 != 3)
                            setDword("resolution", c % 6);
                        if (c % 7 == 6) {
                            const char wrong[] = "12";
                            RegSetValueExA(key, "resolution", 0, REG_SZ, reinterpret_cast<const BYTE*>(wrong), 3);
                        }
                        // (pad0's buffer isn't cleared first: only whole GUIDs or none there; pad1's is)
                        const char* guid0 = kGuids[2 + c % 4];
                        if (c % 6)
                            RegSetValueExA(key, "pad0", 0, REG_SZ, reinterpret_cast<const BYTE*>(guid0),
                                           static_cast<DWORD>(std::strlen(guid0) + 1));
                        else
                            RegSetValueExA(key, "pad0", 0, REG_SZ, reinterpret_cast<const BYTE*>(""), 1);
                        const char* guid1 = kGuids[c % 7];
                        if (c % 8 == 7) // one byte, no terminator (the buffer is clear after it)
                            RegSetValueExA(key, "pad1", 0, REG_BINARY, reinterpret_cast<const BYTE*>("1"), 1);
                        else if (c % 8 != 5)
                            RegSetValueExA(key, "pad1", 0, REG_SZ, reinterpret_cast<const BYTE*>(guid1),
                                           static_cast<DWORD>(std::strlen(guid1) + 1));
                        for (int port = 0; port < 2; ++port) {
                            for (int i = 0; i < 24; i += 1 + (c % 3)) {
                                char name[16];
                                std::snprintf(name, sizeof(name), "k%i_%i", port, i);
                                setDword(name, static_cast<DWORD>(c * 7 + i));
                                std::snprintf(name, sizeof(name), "gp%i_%i", port, (i + c) % 25);
                                setDword(name, static_cast<DWORD>(0x100 + c + i));
                            }
                        }
                        RegCloseKey(key);
                    }
                    if (original)
                        Original<void(__cdecl*)(const char*)>(0x005F34D0)(kKey);
                    else
                        Platform::LoadRegistryOptions(kKey);
                    finish();
                });
            }
        }
    }
    g_relocate = nullptr;
    delete &s;
    Check(ok, "platform: the DirectInput side - reads, bindings, controls, keys, callbacks, release, registry, "
              "key names");
}

} // namespace

SELFTEST(Early, TestPlatformInput);
