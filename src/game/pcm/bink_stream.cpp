// The PC's stream classes: a Bink file played for its sound (BinkStream), and the raw pcm.wad stream left from
// the console versions (WadStream).

#include "game/pcm/stream.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/gfx/movie.h"
#include "game/pcm/music.h"
#include "game/sfx/mute_audio.h"
#include "game/sys/winmain.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Pcm {

namespace {

// The Bink SDK (binkw32.dll), through the game's import table until it's linked.
template <typename F>
F Import(uintptr_t slot) { return GameVariable<F>(slot); }
using Nx::Bink;
Bink* BinkOpen(const void* name, uint32_t flags) { return Import<Bink*(__stdcall*)(const void*, uint32_t)>(0x006452E8)(name, flags); }
void BinkDoFrame(Bink* bink) { Import<int(__stdcall*)(Bink*)>(0x006452EC)(bink); }
void BinkSetPan(Bink* bink, uint32_t track, int pan) { Import<void(__stdcall*)(Bink*, uint32_t, int)>(0x006452F0)(bink, track, pan); }
void BinkGoto(Bink* bink, uint32_t frame, int flags) { Import<void(__stdcall*)(Bink*, uint32_t, int)>(0x006452F4)(bink, frame, flags); }
void BinkSetIOSize(uint32_t size) { Import<void(__stdcall*)(uint32_t)>(0x006452F8)(size); }
void BinkSetVideoOnOff(Bink* bink, int on) { Import<int(__stdcall*)(Bink*, int)>(0x006452FC)(bink, on); }
void* BinkOpenDirectSound() { return GameVariable<void*>(0x00645300); } // the function, passed to the next
int BinkSetSoundSystem(void* open, void* param) { return Import<int(__stdcall*)(void*, void*)>(0x00645304)(open, param); }
void BinkSetVolume(Bink* bink, uint32_t track, int volume) { Import<void(__stdcall*)(Bink*, uint32_t, int)>(0x00645310)(bink, track, Sfx::OutputBinkVolume(volume)); } // (NOT ORIGINAL: mute_audio)
int BinkWait(Bink* bink) { return Import<int(__stdcall*)(Bink*)>(0x00645318)(bink); }
void BinkClose(Bink* bink) { Import<void(__stdcall*)(Bink*)>(0x0064531C)(bink); }
void BinkNextFrame(Bink* bink) { Import<void(__stdcall*)(Bink*)>(0x00645320)(bink); }

constexpr uint32_t kBinkIoSize = 0x01000000;     // BinkOpen: use the I/O buffer size set (BinkSetIOSize)
constexpr uint32_t kBinkFileHandle = 0x04000000; // BinkOpen: the name is (the address of) an open file handle
constexpr uint32_t kIoSize = 0x40000;
constexpr Result kInvalidArgument = static_cast<Result>(0x80070057);

GAME_GLOBAL(bool, g_pauseLocked, 0x0069CB98);
GAME_GLOBAL(uint32_t, g_ioSize, 0x0069CBA4);

bool& SoundSystemSet() { return g_binkSoundSet; } // also set by 0x00452C60
uint32_t& IoSize() { return g_ioSize; }
bool PauseLocked() { return g_pauseLocked; }
bool Focused() { return Sys::g_appActive; }
void* SoundDevice() { return g_soundDevice; }

// The pcm.wad entries open as Bink streams (16 bytes each, at 0x0069CADC; opened by 0x00452A90): the file,
// when last let go of (the counter at 0x0069CB9C), and how many streams use it.
struct WadFile {
    Handle* file;
    uint32_t lastUse;
    uint32_t users;
    uint32_t unknownC;
};
WadFile* WadFiles() { return reinterpret_cast<WadFile*>(0x0069CADC); }
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
Result BinkStream::Open(const char* path)
{
    if (!SoundSystemSet()) {
        if (!BinkSetSoundSystem(BinkOpenDirectSound(), SoundDevice()))
            Dbg::Printf(reinterpret_cast<const char*>(0x006472D4)); // "Bink can't use DSound\n"
        SoundSystemSet() = true;
    }
    if (IoSize() != kIoSize) {
        BinkSetIOSize(kIoSize);
        IoSize() = kIoSize;
    }
    m_bink = BinkOpen(path, kBinkIoSize);
    if (!m_bink)
        return kInvalidArgument;
    BinkSetVideoOnOff(m_bink, 0);
    std::memcpy(m_source, path, std::strlen(path) + 1); // (unbounded, as the original's strcpy)
    m_fromHandle = false;
    return 0;
}

// 0x00452DC0
void BinkStream::Pause(int pause)
{
    if (!PauseLocked())
        m_pauseRequested = pause != 0;
}

// 0x00452DE0
void BinkStream::SetVolume(float volume)
{
    m_volume = volume;
    int level = 0;
    if (volume > 0.0f)
        level = Ftol(static_cast<double>(volume) * kBinkVolumeScale);
    BinkSetVolume(m_bink, 0, level); // (even with no Bink open)
    m_silent = level == 0;
}

// 0x00452FC0
void BinkStream::SetVolumeAndPan(float left, float right)
{
    double sum = static_cast<double>(left) + right;
    m_volume = static_cast<float>(sum * 0.5f);
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
        BinkSetVolume(m_bink, 0, level);
        BinkSetPan(m_bink, 0, pan);
    }
    m_silent = level == 0;
}

// 0x00452E30
void BinkStream::Destruct()
{
    Bink* bink = m_bink;
    m_bink = nullptr;
    BinkClose(bink); // (even with none open)
    if (m_file != INVALID_HANDLE_VALUE && !m_sharedFile)
        CloseHandle(m_file);
    if (m_wadEntry >= 0) {
        WadFile& entry = WadFiles()[m_wadEntry];
        entry.lastUse = UseCounter();
        if (entry.users > 0)
            --entry.users;
    }
    if (m_memory)
        Mem::Free(m_memory);
}

// 0x00452EA0
int BinkStream::Update()
{
    if (m_silent)
        return 0;
    if (m_pauseRequested != m_paused) {
        if (!m_pauseRequested) {
            if (!m_bink) {
                // Reopen where it was paused.
                m_bink = BinkOpen(m_source, m_fromHandle ? kBinkFileHandle : kBinkIoSize);
                if (m_bink) {
                    BinkSetVideoOnOff(m_bink, 0);
                    BinkGoto(m_bink, m_resumeFrame, 1);
                    SetVolume(m_volume);
                }
            }
        } else if (m_bink) {
            m_resumeFrame = m_bink->frameNum;
            BinkClose(m_bink);
            m_bink = nullptr;
        }
        m_paused = m_pauseRequested;
    }
    if (!m_bink)
        return 0;
    while (BinkWait(m_bink) == 0) {
        if (!Focused())
            break;
        BinkDoFrame(m_bink);
        BinkNextFrame(m_bink);
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
WadStream* WadStream::Construct(bool unknown15)
{
    m_unknown90 = 0;
    m_unknown20 = 0;
    m_unknown24 = 0;
    m_buffer = nullptr;
    m_unknown04 = 0;
    m_file = INVALID_HANDLE_VALUE;
    m_unknown15 = unknown15;
    m_unknown5F = true;
    return this;
}

// 0x00454370
Result WadStream::StartRead()
{
    m_reading = true;
    m_readDone = false;
    m_loaded = 0;
    m_played = 0;
    m_unknown10 = 0;
    m_overlapped->Offset = m_fromWad ? m_offset : 0;
    m_overlapped->OffsetHigh = 0;
    if (ReadFile(m_file, m_buffer, 0x2000, nullptr, m_overlapped)) {
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
Result WadStream::OpenFromWad(Handle wad, uint32_t offset, uint32_t size, void* buffer)
{
    m_file = wad;
    m_offset = offset;
    m_unknown50 = 0;
    m_fromWad = true;
    m_size = size;
    m_buffer = buffer;
    return StartRead();
}

// 0x00454490
Result WadStream::Open(Handle file, void* buffer)
{
    m_unknown50 = 0;
    m_file = file;
    if (file == INVALID_HANDLE_VALUE) {
        // HRESULT_FROM_WIN32(GetLastError()), the macro calling it twice
        if (static_cast<int>(GetLastError()) <= 0)
            return static_cast<Result>(GetLastError());
        return FromWin32(GetLastError());
    }
    m_buffer = buffer;
    return StartRead();
}

// 0x004543F0
bool WadStream::PreloadDone()
{
    if (m_state != 1)
        return false;
    if (m_readDone)
        return true;
    return m_loaded >= m_played + 0x2000;
}

// 0x00454430
void WadStream::Pause(int pause)
{
    m_paused = static_cast<uint32_t>(pause) > 0;
}

// 0x00454440
void WadStream::SetChannelVolumes(float, float, float, float, float) {}

} // namespace Pcm

REPLACE(0x00452D20, &Pcm::BinkStream::Open);
REPLACE(0x00452DC0, &Pcm::BinkStream::Pause);
REPLACE(0x00452DE0, &Pcm::BinkStream::SetVolume);
REPLACE(0x00452FC0, &Pcm::BinkStream::SetVolumeAndPan);
REPLACE(0x00452E30, &Pcm::BinkStream::Destruct);
REPLACE(0x00452EA0, &Pcm::BinkStream::Update);
REPLACE(0x00454340, &Pcm::WadStream::Construct);
REPLACE(0x00454370, &Pcm::WadStream::StartRead);
REPLACE(0x00454450, &Pcm::WadStream::OpenFromWad);
REPLACE(0x00454490, &Pcm::WadStream::Open);
REPLACE(0x004543F0, &Pcm::WadStream::PreloadDone);
REPLACE(0x00454430, &Pcm::WadStream::Pause);
REPLACE_SOURCE_ONLY(0x00454440, &Pcm::WadStream::SetChannelVolumes); // 3 bytes
