// The Win32 platform layer's system side: see platform_system.h.

#include "game/sys/platform_system.h"

#include <mmsystem.h>
#include <dsound.h>

#include <cstring>

#include "game/sfx/mute_audio.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/device.h"
#include "game/pcm/music.h"

namespace Platform {

namespace {

GAME_GLOBAL(int, g_frameTop, 0x0068E0BC, 1);
GAME_GLOBAL(char, g_cdDriveSlash, 0x007D6A25);
GAME_GLOBAL(char, g_cdDriveColon, 0x007D6A27);
GAME_GLOBAL(int, g_frameLeft, 0x007D6A30);
GAME_GLOBAL(char, g_cdDriveLetter, 0x007D6A34);

// The CD's drive letter and the ':' / '\' kept with it.
char& CdDriveLetter() { return g_cdDriveLetter; }
char& CdDriveColon() { return g_cdDriveColon; }
char& CdDriveSlash() { return g_cdDriveSlash; }

HWND& GameWindow() { return Nx::g_gameWindow; }
// How far the window's frame reaches left of / above its inside (negated AdjustWindowRect's left / top).
int& FrameLeft() { return g_frameLeft; }
int& FrameTop() { return g_frameTop; }

IDirectSound8*& DirectSound() { return reinterpret_cast<IDirectSound8*&>(Pcm::g_soundDevice); }

// IMA ADPCM's tables (0x0068E0C0, 0x0068E0E0).
const int kIndexSteps[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int kSteps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

} // namespace

// 0x005F5740
char FindCdDrive(const char* volume)
{
    CdDriveColon() = ':';
    char root[4] = {'A', ':', '\\', 0};
    // (Drives left over when the letters run out count as found: the letter kept from before is given back.)
    DWORD drives = GetLogicalDrives();
    if (drives) {
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if (drives & 1) {
                root[0] = letter;
                char name[0x100];
                DWORD serial;
                if (GetDriveTypeA(root) == DRIVE_CDROM &&
                    GetVolumeInformationA(root, name, sizeof(name), &serial, nullptr, nullptr, nullptr, 0) &&
                    std::strcmp(name, volume) == 0) {
                    CdDriveLetter() = letter;
                    break;
                }
            }
            drives >>= 1;
            if (!drives)
                break;
        }
    }
    CdDriveSlash() = '\\';
    if (!drives)
        CdDriveLetter() = 0;
    return CdDriveLetter();
}

// 0x005F5860
HWND CreateGameWindow(const char* className, const char* title, int x, int y, int width, int height,
                      WNDPROC windowProc, HINSTANCE instance, int icon, bool fullScreen)
{
    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    if (icon)
        windowClass.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(icon & 0xFFFF));
    windowClass.lpszClassName = className;
    RegisterClassExA(&windowClass);
    DWORD exStyle = 0, style;
    int frameWidth, frameHeight;
    if (!fullScreen) {
        // The frame around a width x height inside; centred when x / y are -1.
        style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        RECT rect = {0, 0, width, height};
        AdjustWindowRect(&rect, style, FALSE);
        frameWidth = rect.right - rect.left;
        frameHeight = rect.bottom - rect.top;
        FrameLeft() = -rect.left;
        FrameTop() = -rect.top;
        if (x == -1)
            x = (GetSystemMetrics(SM_CXSCREEN) - frameWidth) / 2;
        if (y == -1)
            y = (GetSystemMetrics(SM_CYSCREEN) - frameHeight) / 2;
    } else {
        style = WS_POPUP;
        exStyle = WS_EX_TOPMOST;
        x = y = 0;
        frameWidth = GetSystemMetrics(SM_CXSCREEN);
        frameHeight = GetSystemMetrics(SM_CYSCREEN);
    }
    SetLastError(0);
    GameWindow() = CreateWindowExA(exStyle, className, title, style, x, y, frameWidth, frameHeight, nullptr, nullptr,
                                   instance, nullptr);
    return GameWindow();
}

// 0x005F59D0
void InitDirectSound(HWND window)
{
    DirectSoundCreate8(nullptr, &DirectSound(), nullptr);
    if (DirectSound())
        DirectSound()->SetCooperativeLevel(window, DSSCL_PRIORITY);
    // NOT ORIGINAL: [features] mute_audio=1 - the process's audio session muted before anything plays
    // (game/sfx/mute_audio.h)
    if (Sfx::AudioMuted())
        Sfx::SetAudioSessionMuted(true);
}

// 0x005F5A00
void ReleaseDirectSound()
{
    if (DirectSound()) {
        DirectSound()->Release();
        DirectSound() = nullptr;
    }
    // NOT ORIGINAL: unmuted again, or Windows would keep the session muted for the exe's next run
    if (Sfx::AudioMuted())
        Sfx::SetAudioSessionMuted(false);
}

// 0x005F5A20
void DecodeAdpcm(int16_t* out, const uint8_t* in, int count)
{
    int sample = 0, index = 0;
    for (int i = 0; i < count; ++i) {
        int nibble = (i & 1) ? *in++ >> 4 : *in & 0xF;
        int magnitude = nibble & 7;
        index += kIndexSteps[magnitude];
        if (index < 0)
            index = 0;
        else if (index > 88)
            index = 88;
        int step = kSteps[index];
        int change = (step * magnitude >> 2) + (step >> 3);
        if (nibble >> 3)
            change = -change;
        sample += change;
        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;
        *out++ = static_cast<int16_t>(sample);
    }
}

// 0x005F5AF0
void* Unswizzle(void* out, const void* in, int width, int height, uint32_t bytesPerTexel)
{
    uint32_t count = static_cast<uint32_t>(width * height);
    auto* from = static_cast<const uint8_t*>(in);
    for (uint32_t i = 0; i < count; ++i) {
        // The index's bits dealt out to x and y in turn (x first) while each has bits left.
        auto xBits = static_cast<uint32_t>(width >> 1), yBits = static_cast<uint32_t>(height >> 1);
        uint32_t bit = 1, xBit = 1, yBit = 1, x = 0, y = 0;
        while (xBits || yBits) {
            if (xBits) {
                xBits >>= 1;
                if (i & bit)
                    x |= xBit;
                xBit <<= 1;
                bit <<= 1;
            }
            if (yBits) {
                yBits >>= 1;
                if (i & bit)
                    y |= yBit;
                yBit <<= 1;
                bit <<= 1;
            }
        }
        std::memcpy(static_cast<uint8_t*>(out) + (y * static_cast<uint32_t>(width) + x) * bytesPerTexel, from,
                    bytesPerTexel);
        from += bytesPerTexel;
    }
    return out;
}

} // namespace Platform

REPLACE(0x005F5740, Platform::FindCdDrive);
REPLACE(0x005F5860, Platform::CreateGameWindow);
REPLACE(0x005F59D0, Platform::InitDirectSound);
REPLACE(0x005F5A00, Platform::ReleaseDirectSound);
REPLACE(0x005F5A20, Platform::DecodeAdpcm);
REPLACE(0x005F5AF0, Platform::Unswizzle);
