// The PC's stream classes: a Bink file played for its sound (BinkStream), and the raw pcm.wad stream left from
// the console versions (WadStream).

#include "game/audio/stream.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/gfx/movie.h"
#include "game/audio/music_player.h"
#include "game/sound/mute_audio.h"
#include "game/sys/winmain.h"
#include "game/audio/stream_files.h"
#include "game/sys/crt.h"

namespace AudioStream {

namespace {

// The Bink SDK (binkw32.dll), through the game's import table.
using Engine3D::Bink;
Bink* VideoOpen(const void* name, uint32_t flags) { return reinterpret_cast<Bink*(__stdcall*)(const void*, uint32_t)>(Crt::LibraryFunction("binkw32.dll", "_BinkOpen@8"))(name, flags); }
void VideoDecodeFrame(Bink* bink) { reinterpret_cast<int(__stdcall*)(Bink*)>(Crt::LibraryFunction("binkw32.dll", "_BinkDoFrame@4"))(bink); }
void BinkSetPan(Bink* bink, uint32_t track, int pan) { reinterpret_cast<void(__stdcall*)(Bink*, uint32_t, int)>(Crt::LibraryFunction("binkw32.dll", "_BinkSetPan@12"))(bink, track, pan); }
void BinkGoto(Bink* bink, uint32_t frame, int flags) { reinterpret_cast<void(__stdcall*)(Bink*, uint32_t, int)>(Crt::LibraryFunction("binkw32.dll", "_BinkGoto@12"))(bink, frame, flags); }
void BinkSetIOSize(uint32_t size) { reinterpret_cast<void(__stdcall*)(uint32_t)>(Crt::LibraryFunction("binkw32.dll", "_BinkSetIOSize@4"))(size); }
void BinkSetVideoOnOff(Bink* bink, int on) { reinterpret_cast<int(__stdcall*)(Bink*, int)>(Crt::LibraryFunction("binkw32.dll", "_BinkSetVideoOnOff@8"))(bink, on); }
void* BinkOpenDirectSound() { return reinterpret_cast<void*>(Crt::LibraryFunction("binkw32.dll", "_BinkOpenDirectSound@4")); } // the function, passed to the next
int BinkSetSoundSystem(void* open, void* param) { return reinterpret_cast<int(__stdcall*)(void*, void*)>(Crt::LibraryFunction("binkw32.dll", "_BinkSetSoundSystem@8"))(open, param); }
void VideoSetLoudness(Bink* bink, uint32_t track, int volume) { reinterpret_cast<void(__stdcall*)(Bink*, uint32_t, int)>(Crt::LibraryFunction("binkw32.dll", "_BinkSetVolume@12"))(bink, track, SoundFx::OutputBinkVolume(volume)); } // (NOT ORIGINAL: mute_audio)
int VideoWaitReady(Bink* bink) { return reinterpret_cast<int(__stdcall*)(Bink*)>(Crt::LibraryFunction("binkw32.dll", "_BinkWait@4"))(bink); }
void VideoClose(Bink* bink) { reinterpret_cast<void(__stdcall*)(Bink*)>(Crt::LibraryFunction("binkw32.dll", "_BinkClose@4"))(bink); }
void VideoAdvanceFrame(Bink* bink) { reinterpret_cast<void(__stdcall*)(Bink*)>(Crt::LibraryFunction("binkw32.dll", "_BinkNextFrame@4"))(bink); }

constexpr uint32_t kBinkIoSize = 0x01000000;     // VideoOpen: use the I/O buffer size set (BinkSetIOSize)
constexpr uint32_t kBinkFileHandle = 0x04000000; // VideoOpen: the name is (the address of) an open file handle
constexpr uint32_t kIoSize = 0x40000;
constexpr Result kInvalidArgument = static_cast<Result>(0x80070057);

bool g_pauseLocked;
uint32_t g_ioSize;

bool& SoundSystemSet() { return g_binkSoundSet; } // also set by 0x00452C60
uint32_t& IoSize() { return g_ioSize; }
bool PauseLocked() { return g_pauseLocked; }
bool Focused() { return SystemCore::g_appActive; }
void* SoundDevice() { return g_soundDevice; }

// The pcm.wad entries open as Bink streams (16 bytes each, at 0x0069CADC; opened by 0x00452A90): the file,
// when last let go of (the counter at 0x0069CB9C), and how many streams use it.
struct WadFile {
    Hdl* file;
    uint32_t lastUse;
    uint32_t users;
    uint32_t unknownC;
};
WadFile* WadFiles() { return reinterpret_cast<WadFile*>(reinterpret_cast<uint8_t*>(g_streamFiles) + 4); }
uint32_t UseCounter() { return g_streamFileUses; }

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

constexpr float kBinkVolumeScale = 327.68f; // 0-100 to Bink's 0-32768
constexpr int kBinkMaxVolume = 0x8000;
constexpr float kBinkPanScale = 65536.0f;   // Bink's pan: 0 left, 32768 centre, 65536 right

// HRESULT_FROM_WIN32 (positive codes only).
Result FromWin32(DWORD error) { return static_cast<Result>((error & 0xFFFF) | 0x80070000u); }

} // namespace

// --- BinkStream --------------------------------------------------------------------------------------------

// 0x00452D20
Result BinkStream::Unseal(const char* path)
{
    if (!SoundSystemSet()) {
        if (!BinkSetSoundSystem(BinkOpenDirectSound(), SoundDevice()))
            DebugOut::Printf("Bink can't use DSound\n"); // "Bink can't use DSound\n"
        SoundSystemSet() = true;
    }
    if (IoSize() != kIoSize) {
        BinkSetIOSize(kIoSize);
        IoSize() = kIoSize;
    }
    m_bink = VideoOpen(path, kBinkIoSize);
    if (!m_bink)
        return kInvalidArgument;
    BinkSetVideoOnOff(m_bink, 0);
    std::memcpy(m_fromWhere, path, std::strlen(path) + 1); // (unbounded, as the original's strcpy)
    m_fromHandle = false;
    return 0;
}

// 0x00452DC0
void BinkStream::Freeze(int pause)
{
    if (!PauseLocked())
        m_pauseRequested = pause != 0;
}

// 0x00452DE0
void BinkStream::ChangeLoudness(float volume)
{
    m_loudness = volume;
    int level = 0;
    if (volume > 0.0f)
        level = Ftol(static_cast<double>(volume) * kBinkVolumeScale);
    VideoSetLoudness(m_bink, 0, level); // (even with no Bink open)
    m_silent = level == 0;
}

// 0x00452FC0
void BinkStream::SetVolumeAndPan(float left, float right)
{
    double sum = static_cast<double>(left) + right;
    m_loudness = static_cast<float>(sum * 0.5f);
    float total = static_cast<float>(sum);
    int level = 0;
    int pan = 0;
    if (right > 0.0f || left > 0.0f) {
        float louder = left > right ? left : right;
        pan = Ftol(static_cast<double>(right) / total * kBinkPanScale);
        if (louder > 0.0f) {
            level = Ftol(static_cast<double>(louder) * kBinkVolumeScale);
            if (level > kBinkMaxVolume)
                level = kBinkMaxVolume;
        }
    }
    if (m_bink) {
        VideoSetLoudness(m_bink, 0, level);
        BinkSetPan(m_bink, 0, pan);
    }
    m_silent = level == 0;
}

// 0x00452E30
void BinkStream::Destruct()
{
    Bink* bink = m_bink;
    m_bink = nullptr;
    VideoClose(bink); // (even with none open)
    if (m_fileRef != INVALID_HANDLE_VALUE && !m_sharedFile)
        CloseHandle(m_fileRef);
    if (m_wadEntry >= 0) {
        WadFile& entry = WadFiles()[m_wadEntry];
        entry.lastUse = UseCounter();
        if (entry.users > 0)
            --entry.users;
    }
    if (m_memory)
        MemSys::Dealloc(m_memory);
}

// 0x00452EA0
int BinkStream::TickFrame()
{
    if (m_silent)
        return 0;
    if (m_pauseRequested != m_isHalted) {
        if (!m_pauseRequested) {
            if (!m_bink) {
                // Reopen where it was paused.
                m_bink = VideoOpen(m_fromWhere, m_fromHandle ? kBinkFileHandle : kBinkIoSize);
                if (m_bink) {
                    BinkSetVideoOnOff(m_bink, 0);
                    BinkGoto(m_bink, m_resumeFrame, 1);
                    ChangeLoudness(m_loudness);
                }
            }
        } else if (m_bink) {
            m_resumeFrame = m_bink->frameNum;
            VideoClose(m_bink);
            m_bink = nullptr;
        }
        m_isHalted = m_pauseRequested;
    }
    if (!m_bink)
        return 0;
    while (VideoWaitReady(m_bink) == 0) {
        if (!Focused())
            break;
        VideoDecodeFrame(m_bink);
        VideoAdvanceFrame(m_bink);
        if (m_bink->frameNum == m_bink->frames) {
            if (m_plays > 0)
                --m_plays;
            if (m_plays == 0)
                m_finished = true;
        }
    }
    return 0;
}

// --- WadStream ---------------------------------------------------------------------------------------------

// 0x00454340
WadStream* WadStream::MakeNew(bool unknown15)
{
    m_unknown90 = 0;
    m_unknown20 = 0;
    m_unknown24 = 0;
    m_octets = nullptr;
    m_unknown04 = 0;
    m_fileRef = INVALID_HANDLE_VALUE;
    m_unknown15 = unknown15;
    m_unknown5F = true;
    return this;
}

// 0x00454370
Result WadStream::StartRead()
{
    m_reading = true;
    m_readDone = false;
    m_readyFlag = 0;
    m_played = 0;
    m_unknown10 = 0;
    m_asyncCtl->Offset = m_fromWad ? m_shift : 0;
    m_asyncCtl->OffsetHigh = 0;
    if (ReadFile(m_fileRef, m_octets, 0x2000, nullptr, m_asyncCtl)) {
        m_readDone = true;
        return 0;
    }
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING)
        return 0;
    if (static_cast<int>(error) <= 0)
        return static_cast<Result>(error);
    return FromWin32(error);
}

// 0x00454450
Result WadStream::OpenFromWad(Hdl wad, uint32_t offset, uint32_t size, void* buffer)
{
    m_fileRef = wad;
    m_shift = offset;
    m_unknown50 = 0;
    m_fromWad = true;
    m_extent = size;
    m_octets = buffer;
    return StartRead();
}

// 0x00454490
Result WadStream::Unseal(Hdl file, void* buffer)
{
    m_unknown50 = 0;
    m_fileRef = file;
    if (file == INVALID_HANDLE_VALUE) {
        // HRESULT_FROM_WIN32(GetLastError()), the macro calling it twice
        if (static_cast<int>(GetLastError()) <= 0)
            return static_cast<Result>(GetLastError());
        return FromWin32(GetLastError());
    }
    m_octets = buffer;
    return StartRead();
}

// 0x004543F0
bool WadStream::PreloadDone()
{
    if (m_curPhase != 1)
        return false;
    if (m_readDone)
        return true;
    return m_readyFlag >= m_played + 0x2000;
}

// 0x00454430
void WadStream::Freeze(int pause)
{
    m_isHalted = static_cast<uint32_t>(pause) > 0;
}

// 0x00454440
void WadStream::SetChannelVolumes(float, float, float, float, float) {}

} // namespace Pcm
