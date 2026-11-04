#pragma once

// NOT ORIGINAL - the sound output's objects as the engine holds them: a pointer to an incomplete type per kind of
// object, so no engine header names a sound API (docs/architecture.md "The audio backend"). The pointer is the
// backend's own object - with DirectSound its COM interface, the same address the original kept -, so a handle costs
// exactly what the original's pointer did. Only the DirectSound backend sees what a handle is
// (audio_dsound/dsound_handles.h converts: a cast of the pointer's type, nothing at run time).
namespace audio {

struct SoundOutput;   // the output device (DirectSound: IDirectSound8)
struct SoundBuffer;   // a voice's buffer (IDirectSoundBuffer)
struct SoundBuffer3d; // a buffer's 3D controls (IDirectSound3DBuffer)
struct SoundListener; // the 3D listener (IDirectSound3DListener)

} // namespace audio
