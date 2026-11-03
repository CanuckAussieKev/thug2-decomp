// The Win32 platform layer's system side: see platform_system.h.

#include "platform/platform_system.h"

#include <mmsystem.h>
#include <dsound.h>

#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "platform/mute_audio.h"
#include "engine/audio/music_player.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/maths/rect.h"

namespace os_layer {

constexpr std::size_t kVolumeNameSize = 0x100;
constexpr int kMaxStepIndex = 88; // the ADPCM step table's last index
constexpr int kSampleMax = 32767;
constexpr int kSampleMinMagnitude = 32768;

namespace {

// NOT ORIGINAL grouping - platform_system.cpp's state (the original's globals). Made on first use, never destroyed
// (platform_system_state()).
struct PlatformSystemState {
    int frame_top_{1};
    char cd_drive_slash_{};
    char cd_drive_colon_{};
    int frame_left_{};
    char cd_drive_letter_{};
};

PlatformSystemState& platform_system_state()
{
    static core::NoDestroy<PlatformSystemState> state;
    return state.get();
}

// The CD's drive letter and the ':' / '\' kept with it.
char& cd_drive_letter() { return platform_system_state().cd_drive_letter_; }
char& cd_drive_colon() { return platform_system_state().cd_drive_colon_; }
char& cd_drive_slash() { return platform_system_state().cd_drive_slash_; }

// How far the window's frame reaches left of / above its inside (negated AdjustWindowRect's left / top).
int& get_frame_left() { return platform_system_state().frame_left_; }
int& frame_top() { return platform_system_state().frame_top_; }

IDirectSound8* direct_sound() { return static_cast<IDirectSound8*>(audio::get_sound_device()); }

// IMA ADPCM's tables (0x0068E0C0, 0x0068E0E0).
const std::array<int, 8> kIndexSteps = {{-1, -1, -1, -1, 2, 4, 6, 8}};
const std::array<int, 89> kSteps = {{7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
                        25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
                        88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
                        307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
                        1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
                        3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
                        12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767}};

} // namespace

// 0x005F5740
char find_cd_drive(const char* volume)
{
    cd_drive_colon() = ':';
    std::array<char, 4> root = {{'A', ':', '\\', 0}};
    // (Drives left over when the letters run out count as found: the letter kept from before is given back.)
    DWORD drives = GetLogicalDrives();
    if (drives) {
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if (drives & 1) {
                core::at(root, 0) = letter;
                std::array<char, kVolumeNameSize> name{};
                DWORD serial = 0;
                if (GetDriveTypeA(root.data()) == DRIVE_CDROM &&
                    GetVolumeInformationA(root.data(), name.data(), sizeof(name), &serial, nullptr, nullptr, nullptr, 0) &&
                    std::strcmp(name.data(), volume) == 0) {
                    cd_drive_letter() = letter;
                    break;
                }
            }
            drives >>= 1;
            if (!drives)
                break;
        }
    }
    cd_drive_slash() = '\\';
    if (!drives)
        cd_drive_letter() = 0;
    return cd_drive_letter();
}

// 0x005F5860
HWND create_game_window(const char* class_name, const char* title, maths::PixelPoint at, maths::PixelSize size,
                        WNDPROC window_proc, HINSTANCE instance, int icon, bool full_screen)
{
    const auto width = size.width;
    const auto height = size.height;
    auto x = at.x;
    auto y = at.y;
    WNDCLASSEXA window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    if (icon)
        window_class.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(icon & 0xFFFF));
    window_class.lpszClassName = class_name;
    RegisterClassExA(&window_class);
    DWORD ex_style = 0;
    DWORD style = 0;
    int frame_width = 0;
    int frame_height = 0;
    if (!full_screen) {
        // The frame around a width x height inside; centred when x / y are -1.
        style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        RECT rect = {.left = 0, .top = 0, .right = width, .bottom = height};
        AdjustWindowRect(&rect, style, FALSE);
        frame_width = rect.right - rect.left;
        frame_height = rect.bottom - rect.top;
        get_frame_left() = -rect.left;
        frame_top() = -rect.top;
        if (x == -1)
            x = (GetSystemMetrics(SM_CXSCREEN) - frame_width) / 2;
        if (y == -1)
            y = (GetSystemMetrics(SM_CYSCREEN) - frame_height) / 2;
    } else {
        style = WS_POPUP;
        ex_style = WS_EX_TOPMOST;
        x = y = 0;
        frame_width = GetSystemMetrics(SM_CXSCREEN);
        frame_height = GetSystemMetrics(SM_CYSCREEN);
    }
    SetLastError(0);
    render::renderer().set_game_window(CreateWindowExA(ex_style, class_name, title, style, x, y, frame_width, frame_height, nullptr,
                                    nullptr, instance, nullptr));
    return render::renderer().game_window();
}

// 0x005F59D0
void init_direct_sound(HWND window)
{
    IDirectSound8* device = nullptr;
    DirectSoundCreate8(nullptr, &device, nullptr);
    audio::set_sound_device(device);
    if (direct_sound())
        direct_sound()->SetCooperativeLevel(window, DSSCL_PRIORITY);
    // NOT ORIGINAL: [features] mute_audio=1 - the process's audio session muted before anything plays
    // (platform/mute_audio.h)
    if (sound::audio_muted())
        sound::set_audio_session_muted(true);
}

// 0x005F5A00
void release_direct_sound()
{
    if (direct_sound()) {
        direct_sound()->Release();
        audio::set_sound_device(nullptr);
    }
    // NOT ORIGINAL: unmuted again, or Windows would keep the session muted for the exe's next run
    if (sound::audio_muted())
        sound::set_audio_session_muted(false);
}

// 0x005F5A20
void decode_adpcm(std::span<int16_t> out, std::span<const uint8_t> in)
{
    int sample = 0;
    int index = 0;
    size_t read = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        const int nibble = (i & 1) ? core::at(in, read++) >> 4 : core::at(in, read) & 0xF;
        const int magnitude = nibble & 7;
        index += core::at(kIndexSteps, magnitude);
        if (index < 0)
            index = 0;
        else if (index > kMaxStepIndex)
            index = kMaxStepIndex;
        const int step = core::at(kSteps, index);
        int change = (step * magnitude >> 2) + (step >> 3);
        if (nibble >> 3)
            change = -change;
        sample += change;
        if (sample > kSampleMax)
            sample = kSampleMax;
        else if (sample < -kSampleMinMagnitude)
            sample = -kSampleMinMagnitude;
        core::at(out, i) = static_cast<int16_t>(sample);
    }
}

// 0x005F5AF0
void unswizzle(std::span<uint8_t> out, std::span<const uint8_t> in, TexelLayout layout)
{
    const auto width = layout.width;
    const auto height = layout.height;
    const auto bytes_per_texel = layout.bytes_per_texel_;
    auto count = static_cast<uint32_t>(width * height);
    size_t from = 0; // (the next texel in `in`)
    for (uint32_t i = 0; i < count; ++i) {
        // The index's bits dealt out to x and y in turn (x first) while each has bits left.
        auto x_bits = static_cast<uint32_t>(width >> 1);
        auto y_bits = static_cast<uint32_t>(height >> 1);
        uint32_t bit = 1;
        uint32_t x_bit = 1;
        uint32_t y_bit = 1;
        uint32_t x = 0;
        uint32_t y = 0;
        while (x_bits || y_bits) {
            if (x_bits) {
                x_bits >>= 1;
                if (i & bit)
                    x |= x_bit;
                x_bit <<= 1;
                bit <<= 1;
            }
            if (y_bits) {
                y_bits >>= 1;
                if (i & bit)
                    y |= y_bit;
                y_bit <<= 1;
                bit <<= 1;
            }
        }
        std::memcpy(out.subspan(((y * static_cast<uint32_t>(width)) + x) * bytes_per_texel, bytes_per_texel).data(),
                    in.subspan(from, bytes_per_texel).data(), bytes_per_texel);
        from += bytes_per_texel;
    }
}

} // namespace os_layer
