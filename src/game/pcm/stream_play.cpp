// The PC's streamed audio: loading, starting, stopping and the volumes of the music and the three streams.

#include "game/pcm/stream.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/gfx/movie.h"
#include "game/sfx/sfx.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pcm {

namespace {

HANDLE StreamLock() { return GameVariable<HANDLE>(0x006EE01C); }
HANDLE WadFile() { return GameVariable<HANDLE>(0x0067D7FC); }
WadEntry* WadEntries() { return GameVariable<WadEntry*>(0x006EE00C); }
uint32_t& LastMusicTrack() { return GameVariable<uint32_t>(0x0069CBA8); }
uint32_t* LastStreams() { return reinterpret_cast<uint32_t*>(0x0069CBAC); }
// Where a user soundtrack's track is read to, and each stream's buffer (0x14000 bytes each).
void* UserTrackBuffer() { return reinterpret_cast<void*>(0x006D9FB8); }
void* StreamBuffer(int slot) { return reinterpret_cast<uint8_t*>(0x0069DFB8) + slot * 0x14000; }

// 0x005B6250: the root data paths are under ("THUG2\" or "", by 0x007CEFAD; not rewritten yet).
const char* DataRoot() { return GameFunction<const char*(__cdecl*)()>(0x005B6250)(); }
// The sound effects manager (a reference-counted singleton) and its main volume (the original calls it as a
// method of the manager; it reads a global).
void CreateSoundManager() { Sfx::CreateManager(false); }
void ReleaseSoundManager() { Sfx::ReleaseManager(); }
float SoundVolume() { return Sfx::GetMainVolume(); }

void BinkGoto(Nx::Bink* bink, uint32_t frame, int flags)
{
    GameVariable<void(__stdcall*)(Nx::Bink*, uint32_t, int)>(0x006452F4)(bink, frame, flags);
}

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

constexpr float kPercent = 0.01f;
constexpr float kFifth = 0.2f;

void PauseStream(StreamSlot& slot)
{
    if (slot.wad)
        slot.wad->Pause(1);
    else if (slot.bink)
        slot.bink->Pause(1);
}

// The flag the "start" calls set and loading clears (nothing on PC reads it).
void SetStarted(StreamSlot& slot, bool started)
{
    if (slot.wad)
        slot.wad->m_unknown5F = started;
    else if (slot.bink)
        slot.bink->m_unknown6B = started;
}

// Freed now if it may go, else asked to stop (UpdateStreams frees it later).
void Stop(StreamSlot& slot)
{
    WaitForSingleObject(StreamLock(), INFINITE);
    if (slot.Playing()) {
        bool ready = slot.wad ? slot.wad->ReadyToFree() : slot.bink->ReadyToFree();
        if (ready)
            slot.Free();
        else if (slot.wad)
            slot.wad->m_stopRequested = true;
        else if (slot.bink)
            slot.bink->m_stopRequested = true;
    }
    ReleaseMutex(StreamLock());
}

} // namespace

// The original calls 0x0047C790, a linker-folded `return -1` (also Sfx::GroupCount).
Handle __cdecl OpenUserTrack(int, int) { return INVALID_HANDLE_VALUE; }

// --- The music ---------------------------------------------------------------------------------------------

// 0x00453C20
bool __cdecl LoadMusicTrack(uint32_t checksum)
{
    StreamSlot& music = MusicSlot();
    if (music.Playing())
        return false;
    WaitForSingleObject(StreamLock(), INFINITE);
    music.Create(1, false, 1);
    char path[256];
    std::snprintf(path, sizeof(path), "%s%s%.8x.bik", DataRoot(), "data\\streams\\music\\", checksum);
    Result result = music.bink ? music.bink->Open(path) : -1;
    if (music.wad)
        music.wad->SetVolume(music.volume);
    else if (music.bink)
        music.bink->SetVolume(music.volume);
    if (result != 0) {
        music.Free();
        ReleaseMutex(StreamLock());
        return false;
    }
    if (music.paused) {
        music.paused = true;
        PauseStream(music);
    }
    ReleaseMutex(StreamLock());
    return true;
}

// 0x00453D70
bool __cdecl LoadMusicTrackByName(const char* path)
{
    if (const char* slash = std::strrchr(path, '\\'))
        path = slash + 1;
    if (const char* slash = std::strrchr(path, '/'))
        path = slash + 1;
    return LoadMusicTrack(Crc::GenerateCRCFromString(path));
}

// 0x00453DB0
bool __cdecl PlayMusicTrack(uint32_t checksum)
{
    LastMusicTrack() = checksum;
    if (!LoadMusicTrack(checksum))
        return false;
    SetStarted(MusicSlot(), false);
    return true;
}

// 0x00453BF0
bool __cdecl StartPreloadedMusic()
{
    if (!MusicSlot().Playing())
        return false;
    SetStarted(MusicSlot(), true);
    UpdateStreams();
    return true;
}

// 0x00453850
bool __cdecl PlayUserTrack(int soundtrack, int track)
{
    StreamSlot& music = MusicSlot();
    if (music.Playing())
        return false;
    music.Create(0, false, 1);
    Handle file = OpenUserTrack(soundtrack, track);
    if (file == INVALID_HANDLE_VALUE || !music.wad)
        return false;
    if (music.wad->Open(file, UserTrackBuffer()) != 0)
        return false;
    if (music.paused) {
        music.paused = true;
        PauseStream(music);
    }
    return true;
}

// 0x00453AA0
void __cdecl PlatformStopMusic()
{
    Stop(MusicSlot());
}

// 0x00453400
int __cdecl PlatformSetMusicVolume(float volume)
{
    WaitForSingleObject(StreamLock(), INFINITE);
    StreamSlot& music = MusicSlot();
    if (music.Playing()) {
        music.volume = volume;
        if (music.wad)
            music.wad->SetVolume(volume);
        else if (music.bink)
            music.bink->SetVolume(volume);
    }
    ReleaseMutex(StreamLock());
    return 1;
}

// 0x00453470
void __cdecl SyncMusicToTime(float seconds)
{
    if (!MusicSlot().Playing())
        return;
    WaitForSingleObject(StreamLock(), INFINITE);
    Nx::Bink* bink = MusicSlot().bink->m_bink; // (a WadStream, or a paused Bink stream, would crash here)
    int frame = Ftol(static_cast<double>(bink->frameRate) * seconds / static_cast<double>(bink->frameRateDiv));
    uint32_t off = bink->frameNum - static_cast<uint32_t>(frame);
    if (static_cast<int>(off) < 0)
        off = 0u - off;
    if (static_cast<int>(off) > 5)
        BinkGoto(bink, static_cast<uint32_t>(frame), 1);
    ReleaseMutex(StreamLock());
}

// --- The sound streams -------------------------------------------------------------------------------------

// 0x00453DF0
bool __cdecl SetStreamVolume(const StreamVolume* volume, int slotIndex)
{
    WaitForSingleObject(StreamLock(), INFINITE);
    StreamSlot& slot = StreamSlots()[slotIndex];
    if (slot.Playing()) {
        CreateSoundManager();
        switch (volume->type) {
        case 2: {
            // Five channels; the slot's volume is their mean (the last added unrounded, as the original's x87
            // still held it).
            float level[5];
            double last = 0;
            for (int i = 0; i < 5; ++i) {
                double scaled = static_cast<double>(SoundVolume()) * volume->level[i] * kPercent;
                level[i] = static_cast<float>(scaled);
                last = scaled;
            }
            double sum = last + level[3] + level[2] + level[1] + level[0];
            slot.volume = static_cast<float>(sum * kFifth);
            if (slot.wad)
                slot.wad->SetChannelVolumes(level[0], level[1], level[2], level[3], level[4]);
            else if (slot.bink)
                slot.bink->SetChannelVolumes(level[0], level[1], level[2], level[3], level[4]);
            break;
        }
        case 0:
        case 1:
        case 3: {
            // Left and right (the original has this twice: through StreamSlot::SetVolumeAndPan for 1, inline
            // for 0 and 3).
            float left = static_cast<float>(static_cast<double>(SoundVolume()) * volume->level[0] * kPercent);
            float right = static_cast<float>(static_cast<double>(SoundVolume()) * volume->level[1] * kPercent);
            slot.volume = left;
            slot.SetVolumeAndPan(left, right);
            break;
        }
        default:
            break;
        }
        ReleaseSoundManager();
    }
    ReleaseMutex(StreamLock());
    return true;
}

// 0x00454080
bool __cdecl LoadStream(uint32_t checksum, int slotIndex, const StreamVolume* volume, int, int, bool preload, int plays)
{
    int entry = FindWadEntry(checksum);
    if (entry < 0)
        return false;
    uint32_t offset = WadEntries()[entry].offset;
    uint32_t size = WadEntries()[entry].size;
    if (StreamStatus(slotIndex) != 1)
        return false;
    WaitForSingleObject(StreamLock(), INFINITE);
    StreamSlot& slot = StreamSlots()[slotIndex];
    slot.Create(1, true, plays);
    if (preload)
        SetStarted(slot, false);
    char path[256];
    std::snprintf(path, sizeof(path), "%s%s", DataRoot(), "data\\streams\\pcm\\pcm.wad");
    if (slot.Open(checksum, path, WadFile(), offset, size, StreamBuffer(slotIndex)) != 0) {
        slot.Free();
        ReleaseMutex(StreamLock());
        return false;
    }
    if (volume)
        SetStreamVolume(volume, slotIndex);
    ReleaseMutex(StreamLock());
    return true;
}

// 0x00454200
bool __cdecl PlayStream(uint32_t checksum, int slot, int unused)
{
    LastStreams()[slot] = checksum;
    if (!LoadStream(checksum, slot, nullptr, 0, unused, false, 1))
        return false;
    SetStarted(StreamSlots()[slot], false);
    return true;
}

// 0x00454270
bool __cdecl StartPreloadedStream(int slot, const StreamVolume* volume)
{
    if (!StreamSlots()[slot].Playing())
        return false;
    SetStarted(StreamSlots()[slot], true);
    SetStreamVolume(volume, slot);
    return true;
}

// 0x00453B30
void __cdecl StopStream(int slot, int)
{
    Stop(StreamSlots()[slot]);
}

// 0x00453BD0
void __cdecl StopStreams()
{
    for (int i = 0; i < kNumStreams; ++i)
        StopStream(i, 0);
}

} // namespace Pcm

REPLACE(0x00453C20, Pcm::LoadMusicTrack);
REPLACE(0x00453D70, Pcm::LoadMusicTrackByName);
REPLACE(0x00453DB0, Pcm::PlayMusicTrack);
REPLACE(0x00453BF0, Pcm::StartPreloadedMusic);
REPLACE(0x00453850, Pcm::PlayUserTrack);
REPLACE(0x00453AA0, Pcm::PlatformStopMusic);
REPLACE(0x00453400, Pcm::PlatformSetMusicVolume);
REPLACE(0x00453470, Pcm::SyncMusicToTime);
REPLACE(0x00453DF0, Pcm::SetStreamVolume);
REPLACE(0x00454080, Pcm::LoadStream);
REPLACE(0x00454200, Pcm::PlayStream);
REPLACE(0x00454270, Pcm::StartPreloadedStream);
REPLACE(0x00453B30, Pcm::StopStream);
REPLACE(0x00453BD0, Pcm::StopStreams);
