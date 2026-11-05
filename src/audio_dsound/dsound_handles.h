#pragma once

// NOT ORIGINAL - the sound handles (engine/audio/audio_handles.h) as DirectSound's objects, for the DirectSound
// backend: each handle is its object's own pointer, so the conversions only change the pointer's type (the same
// address, nothing done at run time). Included only by the DirectSound backend's files.
#include <windows.h>

#include <mmsystem.h>
#include <dsound.h>

#include "engine/audio/audio_handles.h"

namespace audio::dsound {

namespace detail {
// The same address as a pointer to To (a handle and its object: never dereferenced as the other's type).
template <typename To, typename From>
To* same_address(From* pointer) noexcept
{
    void* const address = static_cast<void*>(pointer);
    return static_cast<To*>(address);
}
} // namespace detail

// The DirectSound object behind a handle.
inline IDirectSound8* object(SoundOutput* handle) noexcept { return detail::same_address<IDirectSound8>(handle); }
inline IDirectSoundBuffer* object(SoundBuffer* handle) noexcept
{
    return detail::same_address<IDirectSoundBuffer>(handle);
}
inline IDirectSound3DBuffer* object(SoundBuffer3d* handle) noexcept
{
    return detail::same_address<IDirectSound3DBuffer>(handle);
}
inline IDirectSound3DListener* object(SoundListener* handle) noexcept
{
    return detail::same_address<IDirectSound3DListener>(handle);
}

// The handle of a DirectSound object.
inline SoundOutput* handle(IDirectSound8* object) noexcept { return detail::same_address<SoundOutput>(object); }
inline SoundBuffer* handle(IDirectSoundBuffer* object) noexcept { return detail::same_address<SoundBuffer>(object); }

} // namespace audio::dsound
