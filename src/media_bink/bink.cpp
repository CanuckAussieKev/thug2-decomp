#include "media_bink/bink.h"

#include "platform/crt.h"

#include <cstdint>

namespace bink {

namespace {

// binkw32's export `name` as a function of type F.
template <typename F>
F export_of(const char* name)
{
    return crt::library_function<F>({.file = "binkw32.dll"}, name);
}

using OpenSoundFn = void*(__stdcall*)(uintptr_t);

} // namespace

Movie* open(const void* source, uint32_t flags)
{
    return export_of<Movie*(__stdcall*)(const void*, uint32_t)>("_BinkOpen@8")(source, flags);
}

void close(Movie* movie) { export_of<void(__stdcall*)(Movie*)>("_BinkClose@4")(movie); }

int wait(Movie* movie) { return export_of<int(__stdcall*)(Movie*)>("_BinkWait@4")(movie); }

void do_frame(Movie* movie) { export_of<int(__stdcall*)(Movie*)>("_BinkDoFrame@4")(movie); }

void next_frame(Movie* movie) { export_of<void(__stdcall*)(Movie*)>("_BinkNextFrame@4")(movie); }

void go_to(Movie* movie, FrameJump jump)
{
    const auto frame = jump.frame;
    const auto flags = jump.flags;
    export_of<void(__stdcall*)(Movie*, uint32_t, int)>("_BinkGoto@12")(movie, frame, flags);
}

void pause(Movie* movie, int pause) { export_of<int(__stdcall*)(Movie*, int)>("_BinkPause@8")(movie, pause); }

void set_pan(Movie* movie, TrackPan track_pan)
{
    const auto track = track_pan.track;
    const auto pan = track_pan.pan;
    export_of<void(__stdcall*)(Movie*, uint32_t, int)>("_BinkSetPan@12")(movie, track, pan);
}

void set_volume(Movie* movie, TrackVolume track_volume)
{
    const auto track = track_volume.track;
    const auto volume = track_volume.volume;
    export_of<void(__stdcall*)(Movie*, uint32_t, int)>("_BinkSetVolume@12")(movie, track, volume);
}

void set_io_size(uint32_t size) { export_of<void(__stdcall*)(uint32_t)>("_BinkSetIOSize@4")(size); }

void set_video_on_off(Movie* movie, int on)
{
    export_of<int(__stdcall*)(Movie*, int)>("_BinkSetVideoOnOff@8")(movie, on);
}

void set_sound_on_off(Movie* movie, int on)
{
    export_of<int(__stdcall*)(Movie*, int)>("_BinkSetSoundOnOff@8")(movie, on);
}

void copy_to_buffer(Movie* movie, void* bits, FrameCopy frame_copy)
{
    const auto pitch = frame_copy.pitch;
    const auto height = frame_copy.height;
    const auto x = frame_copy.x;
    const auto y = frame_copy.y;
    const auto flags = frame_copy.flags;
    export_of<int(__stdcall*)(Movie*, void*, int, uint32_t, uint32_t, uint32_t, uint32_t)>("_BinkCopyToBuffer@28")(
        movie, bits, pitch, height, x, y, flags);
}

bool use_direct_sound(void* direct_sound)
{
    const auto open_direct_sound = export_of<OpenSoundFn>("_BinkOpenDirectSound@4");
    return export_of<int(__stdcall*)(OpenSoundFn, void*)>("_BinkSetSoundSystem@8")(open_direct_sound,
                                                                                     direct_sound) != 0;
}

} // namespace bink
