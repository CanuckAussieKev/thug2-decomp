// NOT ORIGINAL - the DirectSound backend's audio::Device (dsound_device.h). Each method makes the DirectSound calls the
// original made at that place (the platform layer's set-up 0x005F59D0 / 0x005F5A00, the graphics device's listing of
// the sound devices in 0x004D7C80, the PC driver's voices 0x0047C720-0x0047D41E), unchanged: the same calls with the
// same arguments, in the same order.

#include "audio_dsound/dsound_device.h"

#include <windows.h>

#include <mmsystem.h>
#include <dsound.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "audio_dsound/dsound_handles.h"
#include "base/core/no_destroy.h"
#include "engine/audio/audio_device.h"
#include "engine/audio/audio_handles.h"

namespace audio::dsound {

namespace {

constexpr size_t kDscapsBytes = 0x60;     // DirectSound's structures as this build's headers lay them out
constexpr size_t kBufferDescBytes = 0x24;
constexpr size_t kGuidBytes = 16;

// DirectSound's GUIDs, as the exe's data has them (dsound.lib's): IID_IDirectSound3DBuffer, IID_IDirectSound3DListener.
const GUID kIidSound3dBuffer{.Data1 = 0x279AFA86, .Data2 = 0x4981, .Data3 = 0x11CE,
                              .Data4 = {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
const GUID kIidSound3dListener{.Data1 = 0x279AFA84, .Data2 = 0x4981, .Data3 = 0x11CE,
                                .Data4 = {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};

// What DirectSoundEnumerateA's callback is handed: the engine's function told each description.
struct Listing {
    OutputListed listed;
};

BOOL CALLBACK output_found(GUID* /*guid*/, const char* description, const char* /*module*/, void* context)
{
    static_cast<const Listing*>(context)->listed(description);
    return TRUE; // (all are listed)
}

class DirectSoundDevice final : public Device {
public:
    // 0x005F59D0
    SoundOutput* open(void* window) override
    {
        IDirectSound8* device = nullptr;
        DirectSoundCreate8(nullptr, &device, nullptr);
        if (device) {
            device->SetCooperativeLevel(static_cast<HWND>(window), DSSCL_PRIORITY);
        }
        return handle(device);
    }

    // 0x005F5A00
    void close(SoundOutput* output) override
    {
        if (output) {
            object(output)->Release();
        }
    }

    // (0x004D7C80's call, through the exe's import of DirectSoundEnumerateA)
    void list_outputs(OutputListed listed) override
    {
        Listing listing{.listed = listed};
        DirectSoundEnumerateA(&output_found, &listing);
    }

    // (0x0047CA50's)
    bool get_caps(SoundOutput* output, OutputCaps& caps) override
    {
        static_assert(sizeof(DSCAPS) == kDscapsBytes);
        DSCAPS read{};
        read.dwSize = sizeof(read);
        const HRESULT result = object(output)->GetCaps(&read);
        caps.free_3d_buffers = read.dwFreeHw3DAllBuffers;
        caps.free_mixing_buffers = read.dwFreeHwMixingAllBuffers;
        return result == DS_OK;
    }

    // (0x0047CA50's: the primary buffer's listener)
    SoundListener* create_listener(SoundOutput* output) override
    {
        static_assert(sizeof(DSBUFFERDESC) == kBufferDescBytes);
        DSBUFFERDESC desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
        IDirectSoundBuffer* primary = nullptr;
        object(output)->CreateSoundBuffer(&desc, &primary, nullptr);
        void* listener = nullptr;
        primary->QueryInterface(kIidSound3dListener, &listener);
        auto* const made = detail::same_address<SoundListener>(listener);
        object(made)->SetPosition(0.0F, 0.0F, 0.0F, DS3D_IMMEDIATE);
        primary->Release();
        return made;
    }

    // (0x0047CD70's)
    void orient_listener(SoundListener* listener, Point3 front) override
    {
        object(listener)->SetOrientation(front.x, front.y, front.z, 0.0F, -1.0F, 0.0F, DS3D_DEFERRED);
    }
    void commit_listener(SoundListener* listener) override { object(listener)->CommitDeferredSettings(); }

    // (0x0047D130's: the buffer made from the wave's description - its format the wave's first fields; the original
    // pointed DirectSound at them in place)
    MadeBuffer create_buffer(SoundOutput* output, const BufferDesc& desc) override
    {
        WAVEFORMATEX format{};
        format.wFormatTag = desc.format.format_tag;
        format.nChannels = desc.format.channels;
        format.nSamplesPerSec = desc.format.samples_per_second_;
        format.nAvgBytesPerSec = desc.format.bytes_per_second_;
        format.nBlockAlign = desc.format.block_align_;
        format.wBitsPerSample = desc.format.bits_per_sample_;
        format.cbSize = desc.format.extra_size_;
        DSBUFFERDESC made_desc{};
        made_desc.dwSize = desc.desc_size_;
        made_desc.dwFlags = desc.flags;
        made_desc.dwBufferBytes = desc.bytes;
        made_desc.dwReserved = desc.reserved;
        made_desc.lpwfxFormat = &format;
        static_assert(sizeof(made_desc.guid3DAlgorithm) == kGuidBytes && sizeof(desc.algorithm) == kGuidBytes);
        std::memcpy(&made_desc.guid3DAlgorithm, desc.algorithm.data(), sizeof(made_desc.guid3DAlgorithm));
        IDirectSoundBuffer* created = nullptr;
        const HRESULT made = object(output)->CreateSoundBuffer(&made_desc, &created, nullptr);
        return {.buffer = handle(created), .made = made == DS_OK};
    }

    SoundBuffer3d* buffer_3d(SoundBuffer* buffer) override
    {
        void* controls = nullptr;
        object(buffer)->QueryInterface(kIidSound3dBuffer, &controls);
        return detail::same_address<SoundBuffer3d>(controls);
    }

    void fill_buffer(SoundBuffer* buffer, std::span<const uint8_t> samples) override
    {
        void* first = nullptr;
        DWORD first_bytes = 0;
        void* second = nullptr;
        DWORD second_bytes = 0;
        if (object(buffer)->Lock(0, static_cast<DWORD>(samples.size()), &first, &first_bytes, &second, &second_bytes,
                                 DSBLOCK_ENTIREBUFFER) == DS_OK) {
            std::memcpy(first, samples.data(), first_bytes);
            if (second) {
                std::memcpy(second, samples.subspan(first_bytes).data(), second_bytes);
            }
            object(buffer)->Unlock(first, first_bytes, second, second_bytes);
        }
    }

    void release_buffer(SoundBuffer* buffer) override { object(buffer)->Release(); }
    void release_buffer_3d(SoundBuffer3d* buffer) override { object(buffer)->Release(); }

    bool play_buffer(SoundBuffer* buffer, bool looping) override
    {
        object(buffer)->SetCurrentPosition(0);
        return object(buffer)->Play(0, 0, looping ? DSBPLAY_LOOPING : 0U) == 0;
    }

    void stop_buffer(SoundBuffer* buffer) override { object(buffer)->Stop(); }

    bool buffer_finished(SoundBuffer* buffer) override
    {
        DWORD status = 0;
        return object(buffer)->GetStatus(&status) == DS_OK && ((status & DSBSTATUS_PLAYING) == 0U);
    }

    void set_buffer_volume(SoundBuffer* buffer, int attenuation) override { object(buffer)->SetVolume(attenuation); }
    void set_buffer_pan(SoundBuffer* buffer, int pan) override { object(buffer)->SetPan(pan); }
    void set_buffer_frequency(SoundBuffer* buffer, uint32_t frequency) override
    {
        object(buffer)->SetFrequency(frequency);
    }

    void place_buffer_3d(SoundBuffer3d* buffer, BufferRange range, Point3 position) override
    {
        object(buffer)->SetMaxDistance(range.max_distance, DS3D_DEFERRED);
        object(buffer)->SetMinDistance(range.min_distance, DS3D_DEFERRED);
        object(buffer)->SetPosition(position.x, position.y, position.z, DS3D_DEFERRED);
    }

    // (NOT ORIGINAL: the FFmpeg codec's streaming buffers - binkw32 made these calls on its own buffers)
    bool buffer_position(SoundBuffer* buffer, uint32_t& position) override
    {
        DWORD play = 0;
        const bool read = object(buffer)->GetCurrentPosition(&play, nullptr) == DS_OK;
        position = play;
        return read;
    }

    void write_buffer(SoundBuffer* buffer, uint32_t at, std::span<const uint8_t> samples) override
    {
        void* first = nullptr;
        DWORD first_bytes = 0;
        void* second = nullptr;
        DWORD second_bytes = 0;
        if (samples.empty() || object(buffer)->Lock(at, static_cast<DWORD>(samples.size()), &first, &first_bytes,
                                                     &second, &second_bytes, 0) != DS_OK) {
            return;
        }
        std::memcpy(first, samples.data(), first_bytes);
        if (second) {
            std::memcpy(second, samples.subspan(first_bytes).data(), second_bytes);
        }
        object(buffer)->Unlock(first, first_bytes, second, second_bytes);
    }

    bool resume_buffer(SoundBuffer* buffer) override { return object(buffer)->Play(0, 0, DSBPLAY_LOOPING) == DS_OK; }

    // Bink's own DirectSound output takes the level as the game set it.
    int track_volume(int volume) override { return volume; }
};

} // namespace

Device& device()
{
    static core::NoDestroy<DirectSoundDevice> made;
    return made.get();
}

} // namespace audio::dsound
