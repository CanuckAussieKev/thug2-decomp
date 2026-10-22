// The sound effects manager's positional sounds and the PC driver's voices (0x0047B8E0-0x0047D41E). See sfx.h.

#include "game/sfx/sfx.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <share.h>

#include <windows.h>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/pcm/music_api.h"
#include "game/script/struct.h"
#include "game/sys/platform_system.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Sfx {

namespace {

constexpr int kMaxPlaying = 48;
constexpr int kMaxPositional = 128;
constexpr uint32_t kDebugA = 0x6D2E270E;
constexpr uint32_t kDebugB = 0xD7BB618D;

PlayingSound* Playing() { return &GameVariable<PlayingSound>(0x006F7A68); }
PositionalSound*& PositionalHead() { return GameVariable<PositionalSound*>(0x006F85B0); }
int& PositionalCount() { return GameVariable<int>(0x006F85BC); }
WaveTableEntry* PermanentWaves() { return &GameVariable<WaveTableEntry>(0x006F4268); }
int& PermanentCount() { return GameVariable<int>(0x006F85B4); }
float& DefaultDropoff() { return GameVariable<float>(0x0067DA34); }
bool QuietMisses() { return GameVariable<bool>(0x00684FA8); }
CSfxManager*& Instance() { return GameVariable<CSfxManager*>(0x006F85A8); }
uint32_t& References() { return GameVariable<uint32_t>(0x006F85AC); }

// The driver's state.
Voice* Voices() { return &GameVariable<Voice>(0x006F85C8); }
int& PlayingVoices() { return GameVariable<int>(0x006F8A48); }
bool& Has3D() { return GameVariable<bool>(0x006F8A4C); }
int& VoiceCount() { return GameVariable<int>(0x006F8A50); }
int& LastVoice() { return GameVariable<int>(0x006F8A54); }
void*& Listener() { return GameVariable<void*>(0x006F85C0); }
void* Device() { return GameVariable<void*>(0x007D6A38); }
float VoiceVolume() { return GameVariable<float>(0x0067DB08); }

// COM calls by vtable slot.
template <typename R, typename... A>
R Com(void* object, int offset, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__stdcall*)(void*, A...)>(vtable[offset / 4])(object, args...);
}

// Cameras (not rewritten yet).
void* NearestCamera(const Mth::Vector* position, float* distance)
{
    return GameFunction<void*(__cdecl*)(const Mth::Vector*, float*)>(0x004A09E0)(position, distance);
}
void* ViewportCamera(int viewport) { return GameFunction<void*(__cdecl*)(int)>(0x004A0950)(viewport); }
const Mth::Vector* CameraPosition(void* camera)
{
    return GameFunction<const Mth::Vector*(__thiscall*)(void*)>(0x00485A00)(camera);
}
const Mth::Matrix* CameraMatrix(void* camera)
{
    return GameFunction<const Mth::Matrix*(__thiscall*)(void*)>(0x00485A10)(camera);
}
// The objects' sound parts (not rewritten yet).
double Extent(void* extent, const Mth::Vector* from)
{
    return GameFunction<double(__cdecl*)(void*, const Mth::Vector*)>(0x00423680)(extent, from);
}
bool ObjectSoundExtent(Obj::CObject* object, void* camera, void* out)
{
    return GameFunction<bool(__thiscall*)(Obj::CObject*, void*, void*)>(0x00432F10)(object, camera, out);
}
Mth::Vector* ObjectSoundPosition(Obj::CObject* object, Mth::Vector* out, void* camera)
{
    return GameFunction<Mth::Vector*(__thiscall*)(Obj::CObject*, Mth::Vector*, void*)>(0x00432E50)(object, out,
                                                                                                    camera);
}
Mth::Vector* ObjectSoundPrevious(Obj::CObject* object, Mth::Vector* out, void* camera)
{
    return GameFunction<Mth::Vector*(__thiscall*)(Obj::CObject*, Mth::Vector*, void*)>(0x00432EB0)(object, out,
                                                                                                    camera);
}
void KeepLoopingSound(Obj::CObject* object, PlaybackProps* props, uint32_t time)
{
    GameFunction<void(__thiscall*)(Obj::CObject*, PlaybackProps*, uint32_t)>(0x00432D40)(object, props, time);
}
void NoteSoundUpdate(Obj::CObject* object, PlaybackProps* props)
{
    GameFunction<void(__thiscall*)(Obj::CObject*, PlaybackProps*)>(0x00432F70)(object, props);
}
const Mth::Vector* ObjectPosition(Obj::CObject* object)
{
    return reinterpret_cast<const Mth::Vector*>(reinterpret_cast<uint8_t*>(object) + 0x98);
}

// The CRT's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int Ftol(double x)
{
    return static_cast<int>(static_cast<int64_t>(x));
}

// The FPU's logarithms (FYL2X with FLDLG2 / FLDLN2), unrounded.
double X87Log10(double x)
{
    double result;
    __asm {
        fldlg2
        fld x
        fyl2x
        fstp result
    }
    return result;
}
double X87Ln(double x)
{
    double result;
    __asm {
        fldln2
        fld x
        fyl2x
        fstp result
    }
    return result;
}

void ReportMissing()
{
    if (!QuietMisses())
        Script::GetInteger(kDebugA);
}

// A positional sound's sound stopped (when it's playing) - as the original, the slot is found by unique id first.
void StopPositionalVoice(CSfxManager* manager, const PositionalSound* entry)
{
    if (NoSound() || entry->uniqueId == 0)
        return;
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (Playing()[i].uniqueId != entry->uniqueId)
            continue;
        if (IsVoicePlaying(i) && !NoSound()) {
            int index = manager->GetPlayingIndex(entry->uniqueId);
            if (index >= 0)
                StopVoice(index);
        }
        return;
    }
}
void Unlink(PositionalSound* entry)
{
    if (entry->next)
        entry->next->prev = entry->prev;
    if (entry->prev)
        entry->prev->next = entry->next;
    else
        PositionalHead() = entry->next;
    --PositionalCount();
}


} // namespace

// 0x0047B8E0
void CSfxManager::DopplerShift(float* pitch, const Mth::Vector* position, const Mth::Vector* previous,
                               float frameSeconds, void* camera)
{
    if (NoSound())
        return;
    if (!camera) {
        camera = NearestCamera(position, nullptr);
        if (!camera)
            return;
    }
    const Mth::Vector* cameraAt = reinterpret_cast<const Mth::Vector*>(static_cast<uint8_t*>(camera) + 0x4C);
    float px = cameraAt->x - previous->x;
    float py = cameraAt->y - previous->y;
    float pz = cameraAt->z - previous->z;
    const Mth::Vector* listener = CameraPosition(camera);
    double nx = static_cast<double>(listener->x) - position->x;
    double ny = static_cast<double>(listener->y) - position->y;
    double nz = static_cast<double>(listener->z) - position->z;
    double now = std::sqrt(nz * nz + ny * ny + nx * nx);
    double before = std::sqrt(static_cast<double>(pz) * pz + static_cast<double>(py) * py +
                              static_cast<double>(px) * px);
    float change = static_cast<float>(now - before);
    float speed = static_cast<float>(std::fabs(static_cast<double>(change)) * (Tmr::GetSlomoFrameSeconds() * 60.0f));
    double limit = Tmr::GetSlomoFrameSeconds() * 60.0f * 360.0f;
    if (limit < speed) // (equal or unordered: shifted)
        return;
    double p = *pitch;
    *pitch = static_cast<float>(p - change * p / (static_cast<double>(frameSeconds) * 12320.0f));
}

// 0x0047BA00
PlaybackProps* CSfxManager::GetPositionalProps(Obj::CObject* object, uint32_t id, bool byChecksum)
{
    for (PositionalSound* entry = PositionalHead(); entry; entry = entry->next) {
        uint32_t key = byChecksum ? entry->checksum : entry->uniqueId;
        if (entry->object == object && key == id) {
            int index;
            if (!IsSoundPlaying(entry->uniqueId, &index))
                return nullptr;
            return &Playing()[index].props;
        }
    }
    return nullptr;
}

// 0x0047BAB0
bool CSfxManager::IsUniqueIdFree(uint32_t id)
{
    if (!NoSound() && id != 0) {
        for (int i = 0; i < kMaxPlaying; ++i) {
            if (Playing()[i].uniqueId == id) {
                if (IsVoicePlaying(i))
                    return false;
                break;
            }
        }
    }
    for (PositionalSound* entry = PositionalHead(); entry; entry = entry->next) {
        if (entry->uniqueId == id)
            return false;
    }
    return true;
}

// 0x0047BB20
uint32_t CSfxManager::GetUniqueId(uint32_t id)
{
    while (!IsUniqueIdFree(id))
        ++id;
    return id;
}

// 0x0047BB50
void CSfxManager::StopObjectSounds(Obj::CObject* object)
{
    for (PositionalSound* entry = PositionalHead(); entry;) {
        PositionalSound* next = entry->next;
        if (entry->object == object) {
            entry->inUse = 0;
            StopPositionalVoice(this, entry);
            Unlink(entry);
        }
        entry = next;
    }
}

// 0x0047BC30
void CSfxManager::StopObjectSound(Obj::CObject* object, uint32_t checksum)
{
    for (PositionalSound* entry = PositionalHead(); entry;) {
        PositionalSound* next = entry->next;
        if (entry->object == object && (checksum == 0 || entry->checksum == checksum)) {
            entry->inUse = 0;
            StopPositionalVoice(this, entry);
            Unlink(entry);
        }
        entry = next;
    }
}

// 0x0047BD20
uint32_t CSfxManager::PlaySound(uint32_t checksum, sVolume* volume, float pitch, uint32_t uniqueId, void* params,
                                int /*unused*/)
{
    if (NoSound())
        return 0;
    WaveTableEntry* entry = GetWaveTableEntry(checksum);
    if (!entry)
        return 0;
    uint32_t id = uniqueId ? uniqueId : checksum;
    if (!QuietMisses() && Script::GetInteger(kDebugB) != 0 && !params)
        IsUniqueIdFree(id); // (a debug check whose report is compiled out)
    AdjustVolumeAndPitch(volume, &pitch, entry);
    int voice = PlayVoice(&entry->wave, volume, pitch);
    if (voice == -1)
        return 0;
    PlayingSound& sound = Playing()[voice];
    sound.checksum = id;
    sound.uniqueId = GetUniqueId(id);
    sound.entry = entry;
    InitPlaybackProps(&sound.props, entry, static_cast<PlaybackProps*>(params));
    if (!QuietMisses())
        Script::GetInteger(kDebugB);
    return sound.uniqueId;
}

// 0x0047BE10
bool CSfxManager::GetVolume(sVolume* volume, const Mth::Vector* position, float dropoff, int falloff, void* camera,
                            void* extent)
{
    if (NoSound())
        return true;
    volume->SetSilent();
    if (IsPaused())
        return true;
    if (!camera) {
        camera = NearestCamera(position, nullptr);
        if (!camera)
            return true;
    }
    const Mth::Vector* listener = CameraPosition(camera);
    double dx = static_cast<double>(listener->x) - position->x;
    double dy = static_cast<double>(listener->y) - position->y;
    double dz = static_cast<double>(listener->z) - position->z;
    float distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    if (extent)
        dropoff = static_cast<float>(Extent(extent, CameraPosition(camera)) + distance);
    else if (dropoff == 0.0f)
        dropoff = DefaultDropoff();
    if (!(distance < dropoff) && !std::isnan(distance) && !std::isnan(dropoff)) // (unordered: on, as nearer)
        return static_cast<double>(distance) * 1.05f <= dropoff;
    float t = static_cast<float>(static_cast<double>(distance) / dropoff);
    float level = 0.0f;
    switch (falloff) {
    case 0:
        level = static_cast<float>(((1.0f - static_cast<double>(t) * t) + (1.0f - static_cast<double>(t)) * 3.0f) *
                                   0.25f * 100.0f);
        break;
    case 1:
        level = static_cast<float>((1.0f - static_cast<double>(t)) * 100.0f);
        break;
    case 2: {
        double left = 1.0f - static_cast<double>(t);
        level = static_cast<float>(left * left * 100.0f);
        break;
    }
    case 3:
        level = static_cast<float>(std::sqrt(1.0f - static_cast<double>(t)) * 100.0f);
        break;
    default:
        break;
    }
    volume->m_type = sVolume::TYPE_3D;
    float magnitude = std::fabs(level);
    if (std::fabs(volume->GetLoudestChannel()) < magnitude) {
        // which way the sound is: in front of the camera, to its side
        Mth::Vector toSound = *position - *CameraPosition(camera);
        toSound.Normalize();
        Mth::Vector front = -CameraMatrix(camera)->row[0];
        double facing = static_cast<double>(front.z) * toSound.z + static_cast<double>(front.y) * toSound.y +
                        static_cast<double>(front.x) * toSound.x;
        double side = (facing + 1.0f) * 0.5f;
        float right = static_cast<float>((1.0f - side * side) * level);
        double left = (1.0f - (1.0f - side) * (1.0f - side)) * level;
        if (volume->m_type == sVolume::TYPE_POSITIONAL) {
            // (never: the type was just set to 3D)
            bool louder = false;
            if (left > volume->m_channels[0]) {
                volume->m_channels[0] = static_cast<float>(left);
                louder = true;
            }
            if (right > volume->m_channels[1]) {
                volume->m_channels[1] = right;
                louder = true;
            }
            if (louder) {
                Mth::Vector behind = -CameraMatrix(camera)->row[2];
                double ahead = static_cast<double>(behind.z) * toSound.z + static_cast<double>(behind.y) * toSound.y +
                               static_cast<double>(behind.x) * toSound.x;
                if (ahead < 0.0)
                    volume->m_channels[0] = volume->m_channels[0] * -1.0f;
            }
        } else {
            if (left > volume->m_channels[0])
                volume->m_channels[0] = static_cast<float>(left);
            if (right > volume->m_channels[1])
                volume->m_channels[1] = right;
        }
    }
    volume->m_dropoff = dropoff;
    volume->m_position = *position;
    return true;
}

// 0x0047C140
uint32_t CSfxManager::PlayObjectSound(uint32_t checksum, void* params, Obj::CObject* object, bool notPositional,
                                      bool force)
{
    if (NoSound())
        return 0;
    WaveTableEntry* entry = GetWaveTableEntry(checksum);
    if (!entry)
        return 0;
    auto* given = static_cast<float*>(params); // {pitch, volume, dropoff, falloff}
    sVolume volume;
    volume.Init();
    Mth::Vector at = *ObjectPosition(object);
    void* camera = NearestCamera(&at, nullptr);
    if (camera) {
        uint8_t extentData[0x10] = {};
        void* extent = ObjectSoundExtent(object, camera, extentData) ? extentData : nullptr;
        float dropoff = given[2] != 0.0f ? given[2] : entry->dropoff;
        int falloff = reinterpret_cast<int*>(params)[3];
        Mth::Vector heard;
        GetVolume(&volume, ObjectSoundPosition(object, &heard, camera), dropoff, falloff, camera, extent);
    } else {
        volume.SetSilent();
    }
    if (volume.IsSilent() && !force) {
        if (notPositional || !entry->looping)
            return 0;
        uint32_t id = GetUniqueId(checksum);
        PlaybackProps props;
        InitPlaybackProps(&props, entry, static_cast<PlaybackProps*>(params));
        KeepLoopingSound(object, &props, Tmr::GetTime());
        return id;
    }
    volume.PercentageAdjustment(given[1]);
    uint32_t id = PlaySound(checksum, &volume, given[0], 0, params, 0);
    ReportMissing();
    if (!notPositional) {
        AddPositionalSound(id, checksum, object);
        ReportMissing();
    }
    return id;
}

// 0x0047C360
bool CSfxManager::UpdatePositionalSound(Obj::CObject* object, PlayingSound* sound, uint32_t time)
{
    PlaybackProps* props = &sound->props;
    float dropoff = props->dropoff == 0.0f ? DefaultDropoff() : props->dropoff;
    Mth::Vector at = *ObjectPosition(object);
    float distance;
    void* camera = NearestCamera(&at, &distance);
    if (!camera)
        return true;
    uint8_t extentData[0x10];
    void* extent = nullptr;
    if (ObjectSoundExtent(object, camera, extentData)) {
        extent = extentData;
    } else if (sound->unknown38 <= time) {
        if (static_cast<double>(dropoff) + 480.0f <= distance) {
            // well out of range: stopped (a looping one kept by the object to start again)
            if (sound->entry->looping)
                KeepLoopingSound(object, props, time);
            return false;
        }
        sound->unknown38 = time + 2000;
    }
    float pitch = 100.0f;
    sVolume volume;
    CSfxManager* manager = Instance();
    volume.m_type = manager ? manager->m_defaultVolumeType : sVolume::TYPE_STEREO;
    volume.SetSilent();
    if (extent || distance < dropoff) {
        Mth::Vector heard;
        const Mth::Vector* position = ObjectSoundPosition(object, &heard, camera);
        Mth::Vector here = *position;
        if (sound->entry->flags & 1) {
            float frameSeconds = static_cast<float>(Tmr::GetSlomoFrameSeconds());
            Mth::Vector previous;
            DopplerShift(&pitch, &here, ObjectSoundPrevious(object, &previous, camera), frameSeconds, camera);
        }
        GetVolume(&volume, &here, dropoff, static_cast<int>(props->falloff), camera, extent);
    }
    ReportMissing();
    return SetSoundParamsScaled(sound->uniqueId, &volume, pitch);
}

// 0x0047C560
void CSfxManager::UpdatePositionalSounds()
{
    uint32_t time = Tmr::GetTime();
    for (PositionalSound* entry = PositionalHead(); entry;) {
        PositionalSound* next = entry->next;
        int index;
        if (!IsSoundPlaying(entry->uniqueId, &index)) {
            entry->inUse = 0;
            Unlink(entry);
        } else {
            PlayingSound* sound = &Playing()[index];
            NoteSoundUpdate(entry->object, &sound->props);
            if (!UpdatePositionalSound(entry->object, sound, time)) {
                entry->inUse = 0;
                StopPositionalVoice(this, entry);
                Unlink(entry);
            }
        }
        entry = next;
    }
}

// 0x0047BA60
void CreateManager(bool create)
{
    if (!Instance() && create) {
        void* memory = Mem::Calloc(8, nullptr, 1);
        if (memory) {
            Instance() = static_cast<CSfxManager*>(memory)->Construct();
            ++References();
            return;
        }
        Instance() = nullptr;
    }
    ++References();
}

// 0x0047C6C0
void ReleaseManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (CSfxManager* manager = Instance()) {
        if (!NoSound())
            manager->CleanUp();
        Mem::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    References() = 0;
}

// 0x0047C720
int GetFreeVoice(PCWave* wave)
{
    uint32_t last = static_cast<uint32_t>(LastVoice());
    uint32_t count = static_cast<uint32_t>(VoiceCount());
    int chosen = static_cast<int>((last + 1) % count);
    for (int i = 0; i < static_cast<int>(count); ++i) {
        int voice = static_cast<int>((last + i) % count);
        if (Voices()[voice].playing)
            continue;
        if (Voices()[voice].wave == wave) {
            LastVoice() = voice;
            return voice;
        }
        chosen = voice;
    }
    if (chosen >= 0)
        LastVoice() = chosen;
    return chosen;
}

// 0x0047C790
int GroupCount()
{
    return -1;
}

// 0x0047C7A0
void StopVoice(int voice)
{
    if (void* buffer = Voices()[voice].buffer)
        Com<long>(buffer, 0x48); // Stop
}

// 0x0047C7C0
void SetVoiceVolume(float volume)
{
    GameVariable<float>(0x0067DB08) = volume;
}

// 0x0047C7D0
void SetVoicePitch(float pitch)
{
    GameVariable<float>(0x0067DB0C) = pitch;
}

// 0x0047C7E0
int IsVoicePlaying(int voice)
{
    return Voices()[voice].playing && Voices()[voice].buffer ? 1 : 0;
}

// 0x0047C810
void UpdateVoiceStates()
{
    for (int i = 0; i < VoiceCount(); ++i) {
        Voice& voice = Voices()[i];
        if (!voice.playing || !voice.buffer)
            continue;
        DWORD status;
        if (Com<long>(voice.buffer, 0x24, &status) == 0 && !(status & 1)) { // GetStatus: not playing
            --PlayingVoices();
            voice.playing = false;
        }
    }
}

// 0x0047CA50
void InitVoices(CSfxManager* manager)
{
    if (manager)
        manager->m_defaultVolumeType = sVolume::TYPE_STEREO;
    for (int i = 0; i < kMaxPlaying; ++i) {
        Voices()[i].buffer = nullptr;
        Voices()[i].buffer3D = nullptr;
        Voices()[i].wave = nullptr;
        Voices()[i].playing = false;
    }
    uint32_t caps[0x18] = {};
    caps[0] = 0x60;
    long result = Com<long>(Device(), 0x10, caps); // GetCaps
    Has3D() = false;
    if (result != 0) {
        VoiceCount() = 0x30;
        return;
    }
    Has3D() = caps[0x38 / 4] > 0; // free 3D hardware buffers
    uint32_t mixing = caps[0x20 / 4]; // free hardware mixing buffers
    if (mixing <= 0x10) {
        VoiceCount() = 0x30;
    } else {
        VoiceCount() = static_cast<int>(mixing - 4);
        if (VoiceCount() > 0x30)
            VoiceCount() = 0x30;
    }
    if (!Has3D())
        return;
    // the primary buffer's listener
    uint32_t desc[9] = {};
    desc[0] = 0x24;
    desc[1] = 0x11; // DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D
    void* primary = nullptr;
    Com<long>(Device(), 0xC, desc, &primary, nullptr); // CreateSoundBuffer
    Com<long>(primary, 0, reinterpret_cast<const void*>(0x00666B44), &Listener()); // IDirectSound3DListener
    Com<long>(Listener(), 0x38, 0.0f, 0.0f, 0.0f, 0u); // SetPosition
    Com<long>(primary, 8); // Release
}

// 0x0047CBD0
void StopAllVoices()
{
    Pcm::StopMusic();
    Pcm::StopStreams(-1);
    for (int i = 0; i < VoiceCount(); ++i) {
        if (void* buffer = Voices()[i].buffer)
            Com<long>(buffer, 0x48); // Stop
    }
}

// 0x0047CC10
void FreePermanentWaves()
{
    StopAllVoices();
    for (int i = 0; i < PermanentCount(); ++i) {
        if (PCWave* wave = PermanentWaves()[i].wave) {
            if (wave->data)
                Mem::Manager::Get()->Free(wave->data);
            Mem::Manager::Get()->Free(wave);
        }
        PermanentWaves()[i].wave = nullptr;
    }
    PermanentCount() = 0;
}

// 0x0047C870
bool PCWave::Load(const char* fileName, uint32_t flags)
{
    FILE* file = _fsopen(fileName, "rb", _SH_DENYNO); // (fopen)
    if (!file)
        return false;
    uint8_t riff[8];
    uint8_t wave[4];
    std::fread(riff, 1, 8, file);
    std::fread(wave, 1, 4, file);
    void* samples = nullptr;
    looping = false;
    struct {
        uint32_t id;
        uint32_t size;
    } chunk;
    while (std::fread(&chunk, 1, 8, file) == 8) {
        if (chunk.id == 0x20746D66) { // "fmt "
            std::fread(this, 1, chunk.size, file);
            unknown12 = 0x40;
        } else if (chunk.id == 0x61746164) { // "data"
            samples = std::malloc(chunk.size);
            std::fread(samples, 1, chunk.size, file);
        } else if (chunk.id == 0x6C706D73) { // "smpl": loops?
            uint32_t read = chunk.size < 0x24 ? chunk.size : 0x24;
            uint32_t sampler[9];
            std::fread(sampler, 1, read, file);
            if (sampler[7] > 0) // (its number of loops)
                looping = true;
            if (read < chunk.size)
                std::fseek(file, static_cast<long>(chunk.size - read), SEEK_CUR);
        } else {
            std::fseek(file, static_cast<long>(chunk.size), SEEK_CUR);
        }
    }
    uint32_t size = bytesPerSecond;
    bytesPerSecond = bitsPerSample * samplesPerSecond >> 3;
    auto* converted = static_cast<uint8_t*>(Mem::New(size));
    Platform::DecodeAdpcm(reinterpret_cast<int16_t*>(converted), static_cast<const uint8_t*>(samples),
                          static_cast<int>(size) >> 1);
    std::free(samples);
    data = converted;
    dataSize = size;
    descSize = 0;
    descFlags = 0;
    descBytes = 0;
    descReserved = 0;
    descFormat = nullptr;
    std::memset(desc3DAlgorithm, 0, sizeof(desc3DAlgorithm));
    descSize = 0x24;
    descFlags = flags;
    descFormat = this;
    if (blockAlign)
        size -= size % blockAlign;
    descBytes = size;
    std::fclose(file);
    return true;
}

// 0x0047CC70
bool LoadWave(const char* path, uint32_t /*checksum*/, PCWave** slot, int loadType, int /*unused*/)
{
    // data\sounds\<path>.snd, in a static buffer (0x0067DB10)
    char* fileName = &GameVariable<char>(0x0067DB10);
    size_t length = std::strlen(path);
    std::memcpy(fileName + 12, path, length);
    std::memcpy(fileName + 12 + length, ".snd", 5);
    auto* wave = static_cast<PCWave*>(Mem::New(sizeof(PCWave)));
    if (wave) {
        wave->dataSize = 0;
        wave->data = nullptr;
    }
    bool loaded = wave->Load(fileName, 0x400E0);
    if (loaded) {
        slot[0] = wave;
        reinterpret_cast<uint8_t*>(slot)[4] = wave->looping;
        reinterpret_cast<uint8_t*>(slot)[5] = loadType == 0;
        return true;
    }
    OutputDebugStringA(fileName);
    OutputDebugStringA("\n");
    slot[0] = nullptr;
    reinterpret_cast<uint8_t*>(slot)[4] = 0;
    reinterpret_cast<uint8_t*>(slot)[5] = 0;
    if (wave) {
        if (wave->data)
            Mem::Manager::Get()->Free(wave->data);
        Mem::Manager::Get()->Free(wave);
    }
    return false;
}

// 0x0047CD70
void SetVoiceParameters(int index, sVolume* volume, float pitch)
{
    Voice& voice = Voices()[index];
    if (!voice.buffer)
        return;
    if (Has3D() && volume->m_type == sVolume::TYPE_3D && voice.buffer3D) {
        // 3D: the sound placed relative to the camera hearing it, the listener facing the viewport's way
        void* camera = NearestCamera(&volume->m_position, nullptr);
        void* viewport = ViewportCamera(0);
        Mth::Vector relative = volume->m_position - *CameraPosition(camera);
        if (camera != viewport && viewport) {
            Mth::Vector front = -CameraMatrix(viewport)->row[2];
            Com<long>(Listener(), 0x34, front.x, front.y, front.z, 0.0f, -1.0f, 0.0f, 1u); // SetOrientation
            Mth::Vector heard = -CameraMatrix(camera)->row[2];
            Mth::Vector seen = -CameraMatrix(viewport)->row[2];
            double angle = GameFunction<double(__cdecl*)(const Mth::Vector*, const Mth::Vector*)>(0x00406460)(
                &heard, &seen);
            const Mth::Vector* turned = GameFunction<const Mth::Vector*(__thiscall*)(Mth::Vector*, float)>(
                0x00420A90)(&relative, static_cast<float>(angle * 0.017453292f));
            relative = *turned;
        } else {
            Mth::Vector front = -CameraMatrix(camera)->row[2];
            Com<long>(Listener(), 0x34, front.x, front.y, front.z, 0.0f, -1.0f, 0.0f, 1u); // SetOrientation
        }
        float range = volume->m_dropoff;
        Com<long>(voice.buffer3D, 0x40, range, 1u); // SetMaxDistance
        Com<long>(voice.buffer3D, 0x44, 90.0f, 1u); // SetMinDistance
        Com<long>(voice.buffer3D, 0x4C, relative.x, relative.y, relative.z, 1u); // SetPosition
        int attenuation = -10000;
        double level = static_cast<double>(VoiceVolume()) * 0.0099999998f;
        if (level > 0.0) {
            attenuation = Ftol(X87Ln(level) * 14.42695045f) * 100;
            if (attenuation < -10000)
                attenuation = -10000;
            else if (attenuation > 0)
                attenuation = 0;
        }
        bool audible = volume->m_channels[0] > 0.0f || volume->m_channels[1] > 0.0f;
        Com<long>(voice.buffer, 0x3C, audible ? attenuation : -10000); // SetVolume
        if (Listener())
            Com<long>(Listener(), 0x44); // CommitDeferredSettings
        return;
    }
    // stereo: the louder channel's level and the balance between the two as decibels
    int pan = 0;
    int attenuation = -10000;
    float left = volume->m_channels[0];
    float right = volume->m_channels[1];
    if (left > 0.0f || right > 0.0f) {
        double louder;
        double ratio;
        if (!(left < right)) {
            louder = left;
            ratio = static_cast<double>(left) / right;
        } else {
            louder = right;
            ratio = static_cast<double>(right) / left;
        }
        pan = Ftol(X87Log10(ratio) * 1000.0f);
        double level = louder * VoiceVolume() * 0.0099999998f;
        if (level > 0.0) {
            attenuation = Ftol(X87Log10(level * 0.0099999998f) * 2000.0f);
            if (attenuation < -10000)
                attenuation = -10000;
            else if (attenuation > 0)
                attenuation = 0;
        }
    }
    Com<long>(voice.buffer, 0x3C, attenuation); // SetVolume
    Com<long>(voice.buffer, 0x40, pan);         // SetPan
    if (pitch == 0.0f)
        return;
    int frequency;
    if (pitch == 100.0f) {
        frequency = voice.baseFrequency;
    } else {
        frequency = Ftol(static_cast<double>(static_cast<uint32_t>(voice.baseFrequency)) * pitch *
                                     0.0099999998f);
        if (frequency > 200000)
            frequency = 200000;
        else if (frequency < 100)
            frequency = 100;
    }
    if (frequency != voice.frequency) {
        voice.frequency = frequency;
        if (GameVariable<bool>(0x0067DA40))
            Com<long>(voice.buffer, 0x44, frequency); // SetFrequency
    }
}

// 0x0047D130
int PlayVoice(PCWave** slot, sVolume* volume, float pitch)
{
    PCWave* wave = *slot;
    if (!wave)
        return -1;
    int index = GetFreeVoice(wave);
    if (index < 0)
        return -1;
    Voice& voice = Voices()[index];
    if (voice.playing && voice.buffer)
        Com<long>(voice.buffer, 0x48); // Stop
    if (wave != voice.wave) {
        if (voice.buffer3D)
            Com<long>(voice.buffer3D, 8); // Release
        voice.buffer3D = nullptr;
        if (voice.buffer)
            Com<long>(voice.buffer, 8);
        voice.buffer = nullptr;
        voice.wave = nullptr;
        bool threeD = Has3D() && volume->m_type == sVolume::TYPE_3D;
        wave->descFlags = threeD ? 0x400B0 : 0x400E0;
        std::memcpy(wave->desc3DAlgorithm, reinterpret_cast<const void*>(0x00669208), 16);
        if (Com<long>(Device(), 0xC, &wave->descSize, &voice.buffer, nullptr) != 0) // CreateSoundBuffer
            return -1;
        if (Has3D() && volume->m_type == sVolume::TYPE_3D)
            Com<long>(voice.buffer, 0, reinterpret_cast<const void*>(0x00666B34), &voice.buffer3D);
        void* first;
        DWORD firstBytes;
        void* second;
        DWORD secondBytes;
        if (Com<long>(voice.buffer, 0x2C, 0u, wave->descBytes, &first, &firstBytes, &second, &secondBytes, 2u) ==
            0) { // Lock (the entire buffer)
            std::memcpy(first, wave->data, firstBytes);
            if (second)
                std::memcpy(second, wave->data + firstBytes, secondBytes);
            Com<long>(voice.buffer, 0x4C, first, firstBytes, second, secondBytes); // Unlock
        }
        voice.wave = wave;
    }
    voice.baseFrequency = static_cast<int>(wave->samplesPerSecond);
    voice.frequency = 0;
    SetVoiceParameters(index, volume, pitch);
    Com<long>(voice.buffer, 0x34, 0u); // SetCurrentPosition
    long result = Com<long>(voice.buffer, 0x30, 0u, 0u, reinterpret_cast<uint8_t*>(slot)[4] ? 1u : 0u); // Play
    ++PlayingVoices();
    voice.playing = true;
    return result == 0 ? index : -1;
}

// 0x0047D390
void SilenceVoices()
{
    sVolume volume;
    volume.Init();
    volume.SetSilent();
    for (int i = 0; i < VoiceCount(); ++i) {
        if (Voices()[i].playing && Voices()[i].buffer)
            SetVoiceParameters(i, &volume, 0.0f);
    }
    // (then the volume's destructor: empty)
}

} // namespace Sfx

REPLACE(0x0047B8E0, &Sfx::CSfxManager::DopplerShift);
REPLACE(0x0047BA00, &Sfx::CSfxManager::GetPositionalProps);
REPLACE(0x0047BA60, Sfx::CreateManager);
REPLACE(0x0047BAB0, &Sfx::CSfxManager::IsUniqueIdFree);
REPLACE(0x0047BB20, &Sfx::CSfxManager::GetUniqueId);
REPLACE(0x0047BB50, &Sfx::CSfxManager::StopObjectSounds);
REPLACE(0x0047BC30, &Sfx::CSfxManager::StopObjectSound);
REPLACE(0x0047BD20, &Sfx::CSfxManager::PlaySound);
REPLACE(0x0047BE10, &Sfx::CSfxManager::GetVolume);
REPLACE(0x0047C140, &Sfx::CSfxManager::PlayObjectSound);
REPLACE(0x0047C360, &Sfx::CSfxManager::UpdatePositionalSound);
REPLACE(0x0047C560, &Sfx::CSfxManager::UpdatePositionalSounds);
REPLACE(0x0047C6C0, Sfx::ReleaseManager);
REPLACE(0x0047C720, Sfx::GetFreeVoice);
REPLACE_SOURCE_ONLY(0x0047C790, Sfx::GroupCount); // 4 bytes
REPLACE(0x0047C7A0, Sfx::StopVoice);
REPLACE(0x0047C7C0, Sfx::SetVoiceVolume);
REPLACE(0x0047C7D0, Sfx::SetVoicePitch);
REPLACE(0x0047C7E0, Sfx::IsVoicePlaying);
REPLACE(0x0047C810, Sfx::UpdateVoiceStates);
REPLACE_SOURCE_ONLY(0x0047B000, Sfx::UpdateVoiceStates); // a jump to 0x0047C810
REPLACE(0x0047C870, &Sfx::PCWave::Load);
REPLACE(0x0047CA50, Sfx::InitVoices);
REPLACE(0x0047CBD0, Sfx::StopAllVoices);
REPLACE(0x0047CC10, Sfx::FreePermanentWaves);
REPLACE(0x0047CC70, Sfx::LoadWave);
REPLACE(0x0047CD70, Sfx::SetVoiceParameters);
REPLACE(0x0047D130, Sfx::PlayVoice);
REPLACE(0x0047D390, Sfx::SilenceVoices);
