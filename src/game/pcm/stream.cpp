// The PC's streamed audio: the music's and the three streams' slots, the thread servicing them, pcm.wad's
// index, pausing, freeing what finished, and the (absent) user soundtracks.

#include "game/pcm/stream.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/pcm/stream_commands.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/sys/startup_init.h"

namespace Pcm {

GAME_GLOBAL(void*, g_wadFile, 0x0067D7FC, INVALID_HANDLE_VALUE);
GAME_GLOBAL(WadEntry*, g_wadEntries, 0x006EE00C);
GAME_GLOBAL(void*, g_streamLock, 0x006EE01C); // a mutex (HANDLE), made at start-up

namespace {

// The lock the game thread and the stream thread take around the streams (a mutex made at start-up).
HANDLE StreamLock() { return g_streamLock; }

GAME_GLOBAL(int, g_nextOverlapped, 0x006EE008);
GAME_GLOBAL(uint32_t, g_wadEntryCount, 0x006EE010);
GAME_GLOBAL(HANDLE, g_threadHandle, 0x006EE014);
GAME_GLOBAL(bool, g_stopThread, 0x006EE018);
GAME_GLOBAL(bool, g_soundtracksCounted, 0x006EFDD0);
GAME_GLOBAL(int, g_soundtrackCount, 0x006EFDD4);

HANDLE& ThreadHandle() { return g_threadHandle; }
bool& StopThread() { return g_stopThread; }

// pcm.wad (opened for streaming) and pcm.dat's index of it.
HANDLE& WadFile() { return g_wadFile; }
WadEntry*& WadEntries() { return g_wadEntries; }
uint32_t& WadEntryCount() { return g_wadEntryCount; }

// The ring of OVERLAPPED blocks streams read with, and the next one to try.
GAME_GLOBAL(OVERLAPPED[256], g_overlapped, 0x0069CBB8);
OVERLAPPED* Overlapped() { return g_overlapped; }
constexpr int kNumOverlapped = 256;
int& NextOverlapped() { return g_nextOverlapped; }

// The user soundtracks (0x4C bytes each at 0x006EE024: the track count, then the name at +8).
bool& SoundtracksCounted() { return g_soundtracksCounted; }
int& SoundtrackCount() { return g_soundtrackCount; }
// (Never filled on PC - the count stays 0 - so the slots are only the original's room: 99 of them before the next variable.)
constexpr int kSoundtrackSize = 0x4C, kSoundtrackSlots = 99;
GAME_GLOBAL(uint8_t[kSoundtrackSlots * kSoundtrackSize], g_soundtracks, 0x006EE024);
uint8_t* Soundtrack(int index) { return g_soundtracks + index * kSoundtrackSize; }

constexpr uint32_t kPending = 0x8000000Au; // E_PENDING

// The BinkStream constructor and its open of a pcm.wad entry (written over BinkStreamView, stream_commands.h).
BinkStream* ConstructBinkStream(void* memory, bool unknown, int plays)
{
    return reinterpret_cast<BinkStream*>(static_cast<BinkStreamView*>(memory)->Construct(unknown, plays));
}
Result OpenBinkFromWad(BinkStream* bink, uint32_t checksum, const char* path, uint32_t offset, uint32_t size)
{
    return reinterpret_cast<BinkStreamView*>(bink)->Open(checksum, path, static_cast<long>(offset), size);
}

// Whether the slot's stream uses this OVERLAPPED block.
bool UsesOverlapped(const StreamSlot& slot, const OVERLAPPED* overlapped)
{
    if (slot.wad)
        return slot.wad->m_overlapped == overlapped;
    if (slot.bink)
        return slot.bink->m_overlapped == overlapped;
    return false;
}

// A stream that finished (and isn't pending) or was asked to stop and may go.
template <typename Stream>
bool Over(Stream* stream)
{
    if (stream->m_finished && stream->Status() != kPending)
        return true;
    return stream->m_stopRequested && stream->ReadyToFree();
}
bool Over(const StreamSlot& slot)
{
    if (slot.wad)
        return Over(slot.wad);
    if (slot.bink)
        return Over(slot.bink);
    return false;
}

bool PreloadDone(const StreamSlot& slot)
{
    if (slot.wad)
        return slot.wad->PreloadDone();
    if (slot.bink)
        return slot.bink->PreloadDone();
    return true;
}

} // namespace

GAME_GLOBAL(StreamSlot[3], g_streamSlots, 0x006EDFB8);

StreamSlot* StreamSlots() { return g_streamSlots; }

GAME_GLOBAL(StreamSlot, g_musicSlot, 0x006EDFF4);

StreamSlot& MusicSlot() { return g_musicSlot; }

// --- StreamSlot --------------------------------------------------------------------------------------------

// 0x00453500
void StreamSlot::Create(int type, bool unknown, int plays)
{
    if (type == 0) {
        void* memory = Mem::Calloc(sizeof(WadStream));
        wad = memory ? static_cast<WadStream*>(memory)->Construct(unknown) : nullptr;
    } else if (type == 1) {
        void* memory = Mem::Calloc(sizeof(BinkStream));
        bink = memory ? ConstructBinkStream(memory, unknown, plays) : nullptr;
    }
}

// 0x00453090
Result StreamSlot::Open(uint32_t checksum, const char* path, Handle wadFile, uint32_t offset, uint32_t size, void* buffer)
{
    if (wad)
        return wad->OpenFromWad(wadFile, offset, size, buffer);
    if (bink)
        return OpenBinkFromWad(bink, checksum, path, offset, size);
    return -1;
}

// 0x004530E0
void StreamSlot::SetVolumeAndPan(float left, float right)
{
    if (wad)
        wad->SetVolumeAndPan(left, right);
    else if (bink)
        bink->SetVolumeAndPan(left, right);
}

// 0x004538F0
void StreamSlot::Free()
{
    if (WadStream* stream = wad) {
        stream->Destruct();
        Mem::Free(stream);
        wad = nullptr;
    }
    if (BinkStream* stream = bink) {
        stream->Destruct();
        Mem::Free(stream);
        bink = nullptr;
    }
}

// --- The stream thread -------------------------------------------------------------------------------------

namespace {

void UpdateSlot(StreamSlot& slot)
{
    if (slot.wad)
        slot.wad->Update();
    else if (slot.bink)
        slot.bink->Update();
}

} // namespace

// 0x00453100
unsigned long __stdcall StreamThread(void*)
{
    while (!StopThread()) {
        if (WaitForSingleObject(StreamLock(), INFINITE) != WAIT_TIMEOUT) {
            UpdateSlot(MusicSlot());
            for (int i = 0; i < kNumStreams; ++i)
                UpdateSlot(StreamSlots()[i]);
            ReleaseMutex(StreamLock());
        }
        Sleep(5);
    }
    return 0;
}

// 0x004531A0
void __cdecl StopStreamThread()
{
    if (HANDLE thread = ThreadHandle()) {
        StopThread() = true;
        WaitForSingleObject(thread, INFINITE);
        ThreadHandle() = nullptr;
    }
}

// 0x004535E0
_OVERLAPPED* __cdecl NewOverlapped()
{
    for (;;) {
        // (loops for ever if all 256 are in use)
        OVERLAPPED* overlapped = &Overlapped()[NextOverlapped()];
        int next = NextOverlapped() + 1;
        NextOverlapped() = next >= kNumOverlapped ? 0 : next;
        if (UsesOverlapped(MusicSlot(), overlapped))
            continue;
        bool used = false;
        for (int i = 0; i < kNumStreams && !used; ++i)
            used = UsesOverlapped(StreamSlots()[i], overlapped);
        if (used)
            continue;
        std::memset(overlapped, 0, sizeof(*overlapped));
        return overlapped;
    }
}

// 0x00453670
void __cdecl InitPlatform()
{
    std::memset(&MusicSlot(), 0, sizeof(StreamSlot));
    std::memset(StreamSlots(), 0, kNumStreams * sizeof(StreamSlot));
    UserSoundtrackCount();
    WadFile() = CreateFileA("data\\streams\\pcm\\pcm.wad", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
    HANDLE index = CreateFileA("data\\streams\\pcm\\pcm.dat", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (index != INVALID_HANDLE_VALUE) {
        DWORD read;
        ReadFile(index, &WadEntryCount(), 4, &read, nullptr);
        WadEntries() = static_cast<WadEntry*>(Mem::New(WadEntryCount() * sizeof(WadEntry)));
        ReadFile(index, WadEntries(), WadEntryCount() * sizeof(WadEntry), &read, nullptr);
        CloseHandle(index);
    }
    // Sorted by checksum for FindWadEntry (an exchange sort).
    WadEntry* entries = WadEntries();
    for (uint32_t i = 0; i < WadEntryCount(); ++i) {
        for (uint32_t j = i + 1; j < WadEntryCount(); ++j) {
            if (entries[i].checksum > entries[j].checksum) {
                WadEntry swap = entries[i];
                entries[i] = entries[j];
                entries[j] = swap;
            }
        }
    }
    if (!ThreadHandle()) {
        DWORD id = 0;
        ThreadHandle() = CreateThread(nullptr, 0, StreamThread, nullptr, 0, &id);
        SetThreadPriority(ThreadHandle(), THREAD_PRIORITY_ABOVE_NORMAL);
    }
}

// --- pcm.wad's index ---------------------------------------------------------------------------------------

// 0x004533A0
int FindWadEntry(uint32_t checksum)
{
    int low = 0;
    int high = static_cast<int>(WadEntryCount()) - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        uint32_t found = WadEntries()[middle].checksum;
        if (found < checksum)
            low = middle + 1;
        else if (found == checksum)
            return middle;
        else
            high = middle - 1;
    }
    return -1;
}

// 0x004533A0: the checksum in EBX (ECX and EDX not kept).
__declspec(naked) void FindWadEntryStub()
{
    __asm {
        push ebx
        call FindWadEntry
        add esp, 4
        ret
    }
}

// 0x004533E0
uint32_t __cdecl StreamExists(uint32_t checksum, int music)
{
    if (music)
        return 0;
    return FindWadEntry(checksum) != -1 ? checksum : 0;
}

// --- State -------------------------------------------------------------------------------------------------

// 0x00453250
int __cdecl MusicStatus()
{
    return MusicSlot().Playing() ? 2 : 1;
}

// 0x00453270
int __cdecl StreamStatus(int slot)
{
    int first = slot == -1 ? 0 : slot;
    int end = slot == -1 ? kNumStreams : slot + 1;
    for (int i = first; i < end; ++i)
        if (!StreamSlots()[i].Playing())
            return 1;
    return 2;
}

// 0x00453220
bool __cdecl MusicPreloadDone()
{
    return PreloadDone(MusicSlot());
}

// 0x004531D0
bool __cdecl StreamPreloadDone(int slot)
{
    return PreloadDone(StreamSlots()[slot]);
}

// 0x004532C0
void __cdecl PauseStreams(bool pause, int which)
{
    if (which == 1) {
        StreamSlot& music = MusicSlot();
        music.paused = pause;
        if (music.wad)
            music.wad->Pause(pause ? 1 : 0);
        else if (music.bink)
            music.bink->Pause(pause ? 1 : 0);
        return;
    }
    for (int i = 0; i < kNumStreams; ++i) {
        StreamSlot& slot = StreamSlots()[i];
        if (!slot.Playing())
            continue;
        if (slot.wad)
            slot.wad->Pause(pause ? 1 : 0);
        else if (slot.bink)
            slot.bink->Pause(pause ? 1 : 0);
        slot.paused = pause;
    }
}

// 0x00453940
int __cdecl UpdateStreams()
{
    WaitForSingleObject(StreamLock(), INFINITE);
    if (Over(MusicSlot()))
        MusicSlot().Free();
    for (int i = 0; i < kNumStreams; ++i)
        if (Over(StreamSlots()[i]))
            StreamSlots()[i].Free();
    ReleaseMutex(StreamLock());
    return 0;
}

// --- User soundtracks --------------------------------------------------------------------------------------

// 0x004542D0
int __cdecl UserSoundtrackCount()
{
    if (!SoundtracksCounted()) {
        SoundtracksCounted() = true;
        SoundtrackCount() = 0; // (none on PC)
    }
    return SoundtrackCount();
}

// 0x00454300
const wchar_t* __cdecl UserSoundtrackName(int index)
{
    return index < SoundtrackCount() ? reinterpret_cast<const wchar_t*>(Soundtrack(index) + 8) : nullptr;
}

// 0x00454320
int __cdecl UserSoundtrackTracks(int index)
{
    return index < SoundtrackCount() ? *reinterpret_cast<int*>(Soundtrack(index)) : 0;
}

} // namespace Pcm

REPLACE(0x00453500, &Pcm::StreamSlot::Create);
REPLACE(0x00453090, &Pcm::StreamSlot::Open);
REPLACE(0x004530E0, &Pcm::StreamSlot::SetVolumeAndPan);
REPLACE(0x004538F0, &Pcm::StreamSlot::Free);
REPLACE(0x00453100, Pcm::StreamThread);
REPLACE(0x004531A0, Pcm::StopStreamThread);
REPLACE(0x004535E0, Pcm::NewOverlapped);
REPLACE(0x00453670, Pcm::InitPlatform);
REPLACE_UNCHECKED(0x004533A0, Pcm::FindWadEntryStub);
REPLACE(0x004533E0, Pcm::StreamExists);
REPLACE(0x00453250, Pcm::MusicStatus);
REPLACE(0x00453270, Pcm::StreamStatus);
REPLACE(0x00453220, Pcm::MusicPreloadDone);
REPLACE(0x004531D0, Pcm::StreamPreloadDone);
REPLACE(0x004532C0, Pcm::PauseStreams);
REPLACE(0x00453940, Pcm::UpdateStreams);
REPLACE(0x004542D0, Pcm::UserSoundtrackCount);
REPLACE(0x00454300, Pcm::UserSoundtrackName);
REPLACE(0x00454320, Pcm::UserSoundtrackTracks);

namespace Pcm {

// 0x006409D0 (a static initialiser): the streams' lock, a mutex nobody owns.
void CreateStreamLock() { g_streamLock = CreateMutexA(nullptr, FALSE, nullptr); }

} // namespace Pcm
