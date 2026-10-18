#pragma once

#include <cstddef>
#include <cstdint>

// Streams: sounds streamed from data\streams\pcm\pcm.wad (speech, effects) on the three stream channels; the
// records of what each plays are Pcm::Stream (music.h). Each command does nothing (returns 0 / false) while
// streamed sound is off: no sound, the null sound platform (0x007CEFA8 == 2), or the stream header not loaded.
namespace Pcm {

// A stream's frame amplitudes (data\fam\<checksum>.fam: one byte a frame, for lip sync), loaded by
// LoadStreamFrameAmp. One slot (at 0x0069BAC8); a checksum of 0 marks it free.
struct FrameAmp {
    uint32_t checksum;
    int size;
    uint8_t data[0x1000];
};
static_assert(sizeof(FrameAmp) == 0x1008, "layout must match the original");

// 0x00451010: pause (1) or unpause (0) the streams; -1 unpauses only if they weren't paused when last paused
// with 1. (As PauseMusic, for the streams.)
void __cdecl PauseStream(int pause);
// 0x00451620: load the streams' header (PC: nothing to load) - streamed sound is on while it succeeds.
bool __cdecl LoadStreamHeader(const char* name);
// 0x00451660: whether a stream with this id (or else this name) is playing.
bool __cdecl IsStreamPlaying(uint32_t id);

// 0x004516A0: load the frame amplitudes of stream `checksum` into the free slot; the slot, or null (none free,
// no file, or more than 0x1000 bytes).
FrameAmp* __cdecl LoadStreamFrameAmp(uint32_t checksum);
// 0x004516E0: the slot holding stream `checksum`'s frame amplitudes (with 0: a free slot), or null.
FrameAmp* __cdecl GetStreamFrameAmp(uint32_t checksum);
// 0x00451720: free the slot of stream `checksum`; false if none (or checksum 0).
bool __cdecl FreeStreamFrameAmp(uint32_t checksum);

// 0x00451770: start stream `checksum` on a free channel or else on the one playing the lowest priority (the
// oldest of those) if `priority` is at least that - stopped first; preloaded only, or played at `volume`
// (an Sfx::sVolume) and `pitch`. Records it (under `name`, with a new id from `name` up); the channel, or -1.
int __cdecl StartStream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t name,
                        bool preload);
// 0x00451960: the PlayStream command: play stream `checksum` (volume 100, dropoff 1020, one name) under `id` (0:
// the checksum); the id it gets, 0 if it doesn't play. Not while the streams are paused or blocked (0x0069BAAA).
// `option` is the script's integer 0xE46111E0 (default 0), passed on to the platform. (Callers push a seventh
// argument it ignores.)
uint32_t __cdecl PlayStream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t id);
// 0x00451A50: preload stream `checksum` (silent, under its own checksum); its id, or 0.
uint32_t __cdecl PreLoadStream(uint32_t checksum, int priority);
// 0x00451B80: the AddStream command: give stream `stream` (the one `owner` played, or for no owner the one playing
// with that id or name) another name, with that name's frame amplitudes. False if it isn't playing.
// (Original quirk: no limit on the 8 names a stream has room for.)
bool __cdecl AddStream(uint32_t stream, uint32_t name, void* owner);
// 0x00451E20 (Ghidra splits it at 0x00451E7D): play stream `checksum` from an object's stream component (an
// Obj::CStreamComponent: its object at +0xC, the ids it plays at +0x18): with `usePosInfo`, at the volume the
// sound manager gives the nearest emitter's position (`dropoff`, `dropoffFunction`) - unless it's under 2 and
// the manager says it isn't audible; else at `volume`. The component becomes the stream's owner (the one before
// lets go). A volume other than 100 scales the stream's and sets its dropoff. The id, or 0.
uint32_t __cdecl PlayStreamFromObject(void* component, uint32_t checksum, float dropoff, float volume, float pitch,
                                      int option, int priority, int usePosInfo, int dropoffFunction, uint32_t id);

// The PC's streams are Bink files (sound only) opened from memory.

// 0x00452A90: stream file `key` in memory - read now (`size` bytes at `offset` in `path`, with the game's C
// runtime) into a free slot (the last one) or else the one unused longest, unless it's there already; the slot
// (12, at 0x0069CAD8; its users counted), or -1 (no slot, no file).
int __cdecl LoadStreamFile(uint32_t key, const char* path, long offset, size_t size);

// The Bink stream (0x1C0 bytes; made by the platform's stream start, 0x00453500): only its constructor and Open,
// over the fields they set (by offset: the class's other methods, 0x00452D20 on, are the platform streams' -
// fold this into that class when it's written). +0xC the stream file's slot, +0x28 the Bink, +0x88 opened,
// +0x89 the file's first four bytes.
class BinkStreamView {
public:
    // 0x00452C00: the fields cleared (slots -1); +0x19 and +0x24 from the arguments; +8 the next free stream
    // buffer (0x004535E0).
    BinkStreamView* Construct(bool unknown19, int unknown24);
    // 0x00452C60: open stream file `key` (LoadStreamFile) with Bink from memory, video off; S_OK, or E_INVALIDARG
    // if it can't be read or opened. Bink is told to use DirectSound the first time ("Bink can't use DSound").
    long Open(uint32_t key, const char* path, long offset, size_t size);

private:
    template <typename T>
    T& At(uint32_t offset) { return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) + offset); }
};

} // namespace Pcm
