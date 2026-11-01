// The PC's stream classes: a Bink file played for its sound (BinkStream), and the raw pcm.wad stream left from
// the console versions (WadStream).

#include "engine/audio/stream.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/gfx/movie.h"
#include "engine/audio/music_player.h"
#include "engine/sound/mute_audio.h"
#include "platform/winmain.h"
#include "engine/audio/stream_files.h"
#include "platform/crt.h"

namespace audio {

namespace {

// The Bink SDK (binkw32.dll), through the game's import table.
using render::Bink;
Bink* video_open(const void* name, uint32_t flags)
{
    return reinterpret_cast<Bink*(__stdcall*)(const void*, uint32_t)>(
        crt::library_function("binkw32.dll", "_BinkOpen@8"))(name, flags);
}
void video_decode_frame(Bink* bink)
{
    reinterpret_cast<int(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkDoFrame@4"))(bink);
}
void bink_set_pan(Bink* bink, uint32_t track, int pan)
{
    reinterpret_cast<void(__stdcall*)(Bink*, uint32_t, int)>(crt::library_function("binkw32.dll", "_BinkSetPan@12"))(
        bink, track, pan);
}
void bink_goto(Bink* bink, uint32_t frame, int flags)
{
    reinterpret_cast<void(__stdcall*)(Bink*, uint32_t, int)>(crt::library_function("binkw32.dll", "_BinkGoto@12"))(
        bink, frame, flags);
}
void bink_set_io_size(uint32_t size)
{
    reinterpret_cast<void(__stdcall*)(uint32_t)>(crt::library_function("binkw32.dll", "_BinkSetIOSize@4"))(size);
}
void bink_set_video_on_off(Bink* bink, int on)
{
    reinterpret_cast<int(__stdcall*)(Bink*, int)>(crt::library_function("binkw32.dll", "_BinkSetVideoOnOff@8"))(bink,
                                                                                                                on);
}
void* bink_open_direct_sound()
{
    return reinterpret_cast<void*>(crt::library_function("binkw32.dll", "_BinkOpenDirectSound@4"));
} // the function, passed to the next
int bink_set_sound_system(void* open, void* param)
{
    return reinterpret_cast<int(__stdcall*)(void*, void*)>(
        crt::library_function("binkw32.dll", "_BinkSetSoundSystem@8"))(open, param);
}
void video_set_loudness(Bink* bink, uint32_t track, int volume)
{
    reinterpret_cast<void(__stdcall*)(Bink*, uint32_t, int)>(crt::library_function("binkw32.dll", "_BinkSetVolume@12"))(
        bink, track, sound::output_bink_volume(volume));
} // (NOT ORIGINAL: mute_audio)
int video_wait_ready(Bink* bink)
{
    return reinterpret_cast<int(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkWait@4"))(bink);
}
void video_close(Bink* bink)
{
    reinterpret_cast<void(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkClose@4"))(bink);
}
void video_advance_frame(Bink* bink)
{
    reinterpret_cast<void(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkNextFrame@4"))(bink);
}

constexpr uint32_t kBinkIoSize = 0x01000000;     // video_open: use the I/O buffer size set (bink_set_io_size)
constexpr uint32_t kBinkFileHandle = 0x04000000; // video_open: the name is (the address of) an open file handle
constexpr uint32_t kIoSize = 0x40000;
constexpr Result kInvalidArgument = static_cast<Result>(0x80070057);

bool g_pause_locked;
uint32_t g_io_size;

bool& sound_system_set() { return g_bink_sound_set; } // also set by 0x00452C60
uint32_t& io_size() { return g_io_size; }
bool pause_locked() { return g_pause_locked; }
bool get_focused() { return sys::g_app_active; }
void* sound_device() { return g_sound_device; }

// The pcm.wad entries open as Bink streams (16 bytes each, at 0x0069CADC; opened by 0x00452A90): the file,
// when last let go of (the counter at 0x0069CB9C), and how many streams use it.
struct WadFile {
    Hdl* file;
    uint32_t last_use_;
    uint32_t users;
    uint32_t unknownc_;
};
WadFile* wad_files() { return reinterpret_cast<WadFile*>(reinterpret_cast<uint8_t*>(g_stream_files) + 4); }
uint32_t use_counter() { return g_stream_file_uses; }

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

constexpr float kBinkVolumeScale = 327.68f; // 0-100 to Bink's 0-32768
constexpr int kBinkMaxVolume = 0x8000;
constexpr float kBinkPanScale = 65536.0f; // Bink's pan: 0 left, 32768 centre, 65536 right

// HRESULT_FROM_WIN32 (positive codes only).
Result from_win32(DWORD error) { return static_cast<Result>((error & 0xFFFF) | 0x80070000u); }

} // namespace

// --- BinkStream --------------------------------------------------------------------------------------------

// 0x00452D20
Result BinkStream::unseal(const char* path)
{
    if (!sound_system_set()) {
        if (!bink_set_sound_system(bink_open_direct_sound(), sound_device()))
            debug::printf("Bink can't use DSound\n"); // "Bink can't use DSound\n"
        sound_system_set() = true;
    }
    if (io_size() != kIoSize) {
        bink_set_io_size(kIoSize);
        io_size() = kIoSize;
    }
    bink_ = video_open(path, kBinkIoSize);
    if (!bink_)
        return kInvalidArgument;
    bink_set_video_on_off(bink_, 0);
    std::memcpy(from_where_, path, std::strlen(path) + 1); // (unbounded, as the original's strcpy)
    from_handle_ = false;
    return 0;
}

// 0x00452DC0
void BinkStream::get_freeze(int pause)
{
    if (!pause_locked())
        pause_requested_ = pause != 0;
}

// 0x00452DE0
void BinkStream::change_loudness(float volume)
{
    loudness_ = volume;
    int level = 0;
    if (volume > 0.0f)
        level = ftol(static_cast<double>(volume) * kBinkVolumeScale);
    video_set_loudness(bink_, 0, level); // (even with no Bink open)
    silent_ = level == 0;
}

// 0x00452FC0
void BinkStream::set_volume_and_pan(float left, float right)
{
    double sum = static_cast<double>(left) + right;
    loudness_ = static_cast<float>(sum * 0.5f);
    auto total = static_cast<float>(sum);
    int level = 0;
    int pan = 0;
    if (right > 0.0f || left > 0.0f) {
        float louder = left > right ? left : right;
        pan = ftol(static_cast<double>(right) / total * kBinkPanScale);
        if (louder > 0.0f) {
            level = ftol(static_cast<double>(louder) * kBinkVolumeScale);
            if (level > kBinkMaxVolume)
                level = kBinkMaxVolume;
        }
    }
    if (bink_) {
        video_set_loudness(bink_, 0, level);
        bink_set_pan(bink_, 0, pan);
    }
    silent_ = level == 0;
}

// 0x00452E30
void BinkStream::destruct()
{
    Bink* bink = bink_;
    bink_ = nullptr;
    video_close(bink); // (even with none open)
    if (file_ref_ != INVALID_HANDLE_VALUE && !shared_file_)
        CloseHandle(file_ref_);
    if (wad_entry_ >= 0) {
        WadFile& entry = wad_files()[wad_entry_];
        entry.last_use_ = use_counter();
        if (entry.users > 0)
            --entry.users;
    }
    if (memory_)
        memory::dealloc(memory_);
}

// 0x00452EA0
int BinkStream::tick_frame()
{
    if (silent_)
        return 0;
    if (pause_requested_ != is_halted_) {
        if (!pause_requested_) {
            if (!bink_) {
                // Reopen where it was paused.
                bink_ = video_open(from_where_, from_handle_ ? kBinkFileHandle : kBinkIoSize);
                if (bink_) {
                    bink_set_video_on_off(bink_, 0);
                    bink_goto(bink_, resume_frame_, 1);
                    change_loudness(loudness_);
                }
            }
        } else if (bink_) {
            resume_frame_ = bink_->frame_num_;
            video_close(bink_);
            bink_ = nullptr;
        }
        is_halted_ = pause_requested_;
    }
    if (!bink_)
        return 0;
    while (video_wait_ready(bink_) == 0) {
        if (!get_focused())
            break;
        video_decode_frame(bink_);
        video_advance_frame(bink_);
        if (bink_->frame_num_ == bink_->frames) {
            if (plays_ > 0)
                --plays_;
            if (plays_ == 0)
                finished_ = true;
        }
    }
    return 0;
}

// --- WadStream ---------------------------------------------------------------------------------------------

// 0x00454340
WadStream::WadStream(bool unknown15)
{
    unknown90_ = 0;
    unknown20_ = 0;
    unknown24_ = 0;
    octets_ = nullptr;
    unknown04_ = 0;
    file_ref_ = INVALID_HANDLE_VALUE;
    unknown15_ = unknown15;
    unknown5f_ = true;
}

// 0x00454370
Result WadStream::start_read()
{
    reading_ = true;
    read_done_ = false;
    ready_flag_ = 0;
    played_ = 0;
    unknown10_ = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
    async_ctl_->Offset = from_wad_ ? shift_ : 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
    async_ctl_->OffsetHigh = 0;
    if (ReadFile(file_ref_, octets_, 0x2000, nullptr, async_ctl_)) {
        read_done_ = true;
        return 0;
    }
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING)
        return 0;
    if (static_cast<int>(error) <= 0)
        return static_cast<Result>(error);
    return from_win32(error);
}

// 0x00454450
Result WadStream::open_from_wad(Hdl wad, uint32_t offset, uint32_t size, void* buffer)
{
    file_ref_ = wad;
    shift_ = offset;
    unknown50_ = 0;
    from_wad_ = true;
    extent_ = size;
    octets_ = buffer;
    return start_read();
}

// 0x00454490
Result WadStream::unseal(Hdl file, void* buffer)
{
    unknown50_ = 0;
    file_ref_ = file;
    if (file == INVALID_HANDLE_VALUE) {
        // HRESULT_FROM_WIN32(GetLastError()), the macro calling it twice
        if (static_cast<int>(GetLastError()) <= 0)
            return static_cast<Result>(GetLastError());
        return from_win32(GetLastError());
    }
    octets_ = buffer;
    return start_read();
}

// 0x004543F0
bool WadStream::prefetch_complete()
{
    if (cur_phase_ != 1)
        return false;
    if (read_done_)
        return true;
    return ready_flag_ >= played_ + 0x2000;
}

// 0x00454430
void WadStream::get_freeze(int pause) { is_halted_ = static_cast<uint32_t>(pause) > 0; }

// 0x00454440
void WadStream::set_channel_volumes(float, float, float, float, float) {}

} // namespace audio
