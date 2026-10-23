// The sound effects manager (0x0047AC10-0x0047D41E). See sfx.h.

#include "game/sfx/sfx.h"

#include <cmath>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/pcm/music.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Sfx {

namespace {

constexpr int kMaxPlaying = 48;
constexpr int kMaxPositional = 128;
constexpr int kMaxWaves = 256;

// Script globals the original reads (their values unused here; debug switches).
constexpr uint32_t kDebugA = 0x6D2E270E;
constexpr uint32_t kDebugB = 0xD7BB618D;

PlayingSound* Playing() { return &GameVariable<PlayingSound>(0x006F7A68); }
PositionalSound* PositionalPool() { return &GameVariable<PositionalSound>(0x006F3668); }
PositionalSound*& PositionalHead() { return GameVariable<PositionalSound*>(0x006F85B0); }
int& PositionalCount() { return GameVariable<int>(0x006F85BC); }
WaveTableEntry* PermanentWaves() { return &GameVariable<WaveTableEntry>(0x006F4268); }
int& PermanentCount() { return GameVariable<int>(0x006F85B4); }
WaveTableEntry* LevelWaves() { return &GameVariable<WaveTableEntry>(0x006F5E68); }
int& LevelCount() { return GameVariable<int>(0x006F85B8); }
float& DefaultDropoff() { return GameVariable<float>(0x0067DA34); }
float& MainVolume() { return GameVariable<float>(0x0067DA38); }
float& Pitch() { return GameVariable<float>(0x0067DA3C); }
// Whether a missing sound goes unreported (the report itself is compiled out; only the global is read).
bool QuietMisses() { return GameVariable<bool>(0x00684FA8); }

// The missing-sound report (compiled out; the global still read).
void ReportMissing()
{
    if (!QuietMisses())
        Script::GetInteger(kDebugA);
}

// (debug output, compiled out)
inline void PrintSoundDebug(int, int, int) {}
inline void PrintGroup(int) {}

} // namespace

// 0x0047AC10
int FalloffFromChecksum(uint32_t checksum)
{
    switch (checksum) {
    case 0xF221474C: return 1;
    case 0x9706104B: return 2;
    case 0x0CA64C41: return 3;
    case 0xEF082878: // (named, but the default)
    default: return 0;
    }
}

// 0x0047B030
sVolume* sVolume::Init()
{
    CSfxManager* manager = GameVariable<CSfxManager*>(0x006F85A8);
    m_type = manager ? manager->m_defaultVolumeType : TYPE_STEREO;
    SetSilent();
    return this;
}

// 0x0047B070
sVolume* sVolume::SetType(int type)
{
    m_type = type;
    return this;
}

// 0x0047AC50
void sVolume::PercentageAdjustment(float percent)
{
    for (int i = 0; i < ChannelCount(); ++i)
        m_channels[i] = static_cast<float>(static_cast<double>(percent) * m_channels[i] * 0.0099999998f);
}

// 0x0047ACF0
bool sVolume::IsSilent()
{
    for (int i = 0; i < ChannelCount(); ++i) {
        if (m_channels[i] != 0.0f)
            return false;
    }
    return m_type != TYPE_3D;
}

// 0x0047AD60
void sVolume::SetSilent()
{
    for (float& channel : m_channels)
        channel = 0.0f;
}

// 0x0047B080
float sVolume::GetLoudestChannel()
{
    float loudest = m_channels[0];
    for (int i = 1; i < ChannelCount(); ++i) {
        if (std::fabs(loudest) < std::fabs(m_channels[i]))
            loudest = m_channels[i];
    }
    return loudest;
}

// 0x0047B010
bool NoSound()
{
    if (!GameVariable<void*>(0x007D6A38))
        return true;
    int platform = GameVariable<int>(0x007CEFA8);
    return platform == 2 || platform == 6;
}

// 0x0047AFD0
float GetMainVolume()
{
    return MainVolume();
}

// 0x0047AFE0
int CountPlayingVoices()
{
    int playing = 0;
    for (int voice = 0; voice < kMaxPlaying; ++voice) {
        if (IsVoicePlaying(voice))
            ++playing;
    }
    return playing;
}

// 0x0047B160
CSfxManager* CSfxManager::Construct()
{
    if (!NoSound()) {
        m_defaultVolumeType = sVolume::TYPE_STEREO;
        InitVoices(this);
        for (int i = 0; i < kMaxPlaying; ++i) {
            Playing()[i].uniqueId = 0;
            Playing()[i].checksum = 0;
        }
    }
    return this;
}

// 0x0047AD80
int CSfxManager::GetPlayingIndex(uint32_t id)
{
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (Playing()[i].uniqueId == id && IsVoicePlaying(i))
            return i;
    }
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (Playing()[i].checksum == id && IsVoicePlaying(i)) {
            // (a debug report, compiled out)
            if (Script::GetInteger(kDebugA) == 0)
                Script::GetInteger(kDebugB);
            return i;
        }
    }
    return -1;
}

// 0x0047AE20
void CSfxManager::AddPositionalSound(uint32_t uniqueId, uint32_t checksum, Obj::CObject* object)
{
    if (uniqueId == 0 || PositionalCount() == kMaxPositional)
        return;
    ++PositionalCount(); // (counted even when no entry is free, as the original)
    PositionalSound* entry = nullptr;
    for (int i = 0; i < kMaxPositional; ++i) {
        if (!(PositionalPool()[i].inUse & 1)) {
            entry = &PositionalPool()[i];
            break;
        }
    }
    if (!entry)
        return;
    if (PositionalSound* head = PositionalHead())
        head->prev = entry;
    entry->next = PositionalHead();
    entry->inUse = 1;
    entry->prev = nullptr;
    PositionalHead() = entry;
    entry->uniqueId = uniqueId;
    entry->checksum = checksum;
    entry->object = object;
}

// 0x0047AE90
void CSfxManager::StopGroup(int group)
{
    (void)group;
    GroupCount();
}

// 0x0047AEA0
void CSfxManager::UnloadGroup(int group)
{
    int count = PermanentCount();
    for (int i = 0; i < count;) {
        WaveTableEntry& entry = PermanentWaves()[i];
        if (entry.loadType == 2 && entry.group == static_cast<uint32_t>(group)) {
            --count;
            entry = PermanentWaves()[count]; // (the last moves into its place)
        } else {
            ++i;
        }
    }
    if (PermanentCount() > 0)
        PermanentCount() = count;
    PrintGroup(group);
}

// 0x0047AF10
void CSfxManager::UnloadAllGroups()
{
    for (int group = 0; group < GroupCount(); ++group)
        UnloadGroup(group);
}

// 0x0047AF40
void CSfxManager::InitPlaybackProps(PlaybackProps* props, WaveTableEntry* entry, PlaybackProps* params)
{
    if (params) {
        // (the parameters: {pitch, volume, dropoff, falloff} in the props' first four words' order)
        auto* given = reinterpret_cast<uint32_t*>(params);
        std::memcpy(&props->pitch, &given[0], 4);
        std::memcpy(&props->volume, &given[1], 4);
        float dropoff;
        std::memcpy(&dropoff, &given[2], 4);
        props->dropoff = dropoff != 0.0f ? dropoff : entry->dropoff;
        props->unknown20 = 0;
        props->unknown24 = 0;
        props->pitch3 = props->pitch;
        props->pitch2 = props->pitch;
        props->volume3 = props->volume;
        props->volume2 = props->volume;
        props->falloff = given[3];
    } else {
        props->pitch = 100.0f;
        props->volume = 100.0f;
        props->dropoff = 0.0f;
        props->falloff = 0;
    }
    props->checksum = entry->checksum;
}

// 0x0047B1B0
void CSfxManager::CleanUp()
{
    if (NoSound())
        return;
    PositionalHead() = nullptr;
    PositionalCount() = 0;
    for (int i = 0; i < kMaxPositional; ++i)
        PositionalPool()[i].inUse = 0;
    FreePermanentWaves();
    PermanentCount() = 0;
    DefaultDropoff() = 1020.0f;
}

// 0x0047B210
void CSfxManager::SetDefaultDropoff(float dropoff)
{
    if (!NoSound())
        DefaultDropoff() = dropoff;
}

// 0x0047B240
void CSfxManager::StopAllSounds()
{
    if (NoSound())
        return;
    StopAllVoices();
    PositionalHead() = nullptr;
    PositionalCount() = 0;
    for (int i = 0; i < kMaxPositional; ++i)
        PositionalPool()[i].inUse = 0;
}

// 0x0047B2A0
void CSfxManager::UpdateVoiceVolumes()
{
    if (!NoSound())
        SilenceVoices();
}

// 0x0047B2C0
WaveTableEntry* CSfxManager::GetWaveTableEntry(uint32_t checksum)
{
    if (NoSound())
        return nullptr;
    for (int i = 0; i < LevelCount(); ++i) {
        if (LevelWaves()[i].checksum == checksum)
            return &LevelWaves()[i];
    }
    for (int i = 0; i < PermanentCount(); ++i) {
        if (PermanentWaves()[i].checksum == checksum)
            return &PermanentWaves()[i];
    }
    return nullptr;
}

// 0x0047B340
bool CSfxManager::IsPaused()
{
    if (m_muteOverride)
        return false;
    return GameVariable<bool>(0x006FC49C) || GameVariable<bool>(0x006FA7D4);
}

// 0x0047B360
bool CSfxManager::LoadSound(const char* path, uint16_t flags, int loadType, int group, float dropoff, float pitch,
                            float volume)
{
    if (NoSound())
        return false;
    // the name without its folders
    const char* name = path;
    int length = static_cast<int>(std::strlen(path));
    for (int i = 0; i < length; ++i) {
        if (path[i] == '\\' || path[i] == '/')
            name = path + i + 1;
    }
    uint32_t checksum = Crc::GenerateCRCFromString(name);
    if (GetWaveTableEntry(checksum))
        return false;
    WaveTableEntry* entry;
    if (loadType == 0) {
        if (LevelCount() >= kMaxWaves)
            return false;
        entry = &LevelWaves()[LevelCount()];
        if (!LoadWave(path, checksum, &entry->wave, loadType, group))
            return false;
        ++LevelCount();
    } else {
        if (PermanentCount() >= kMaxWaves)
            return false;
        entry = &PermanentWaves()[PermanentCount()];
        if (!LoadWave(path, checksum, &entry->wave, loadType, group))
            return false;
        ++PermanentCount();
    }
    entry->pitch = pitch;
    entry->checksum = checksum;
    entry->dropoff = dropoff;
    entry->loadType = static_cast<uint8_t>(loadType);
    entry->group = static_cast<uint8_t>(group);
    entry->volume = volume;
    entry->flags = flags;
    return true;
}

// 0x0047B480
void CSfxManager::AdjustVolumeAndPitch(sVolume* volume, float* pitch, WaveTableEntry* entry)
{
    if (NoSound())
        return;
    volume->PercentageAdjustment(50.0f);
    if (entry->volume != 100.0f)
        volume->PercentageAdjustment(entry->volume);
    if (entry->pitch != 100.0f)
        *pitch = static_cast<float>(static_cast<double>(*pitch) * entry->pitch * 0.0099999998f);
}

// 0x0047B4E0
bool CSfxManager::StopSound(uint32_t id)
{
    if (NoSound())
        return true;
    int index = GetPlayingIndex(id);
    if (index < 0)
        return false;
    StopVoice(index);
    return true;
}

// 0x0047B520
bool CSfxManager::SetSoundParams(uint32_t id, sVolume* volume, float pitch)
{
    if (NoSound())
        return true;
    int index = GetPlayingIndex(id);
    if (index < 0) {
        ReportMissing();
        return false;
    }
    WaveTableEntry* entry = Playing()[index].entry;
    if (!entry)
        return false;
    if (!volume->IsSilent())
        AdjustVolumeAndPitch(volume, &pitch, entry);
    SetVoiceParameters(index, volume, pitch);
    return true;
}

// 0x0047B5D0
void CSfxManager::SetMainVolume(float volume)
{
    if (NoSound())
        return;
    Script::GetInteger(kDebugB);
    MainVolume() = volume;
    float scaled = static_cast<float>(static_cast<double>(GameVariable<float>(0x00684FAC)) * volume * 0.0099999998f);
    if (scaled < 0.1f)
        scaled = 0.0f;
    SetVoiceVolume(scaled);
    Pcm::SetUserSoundtrackVolume(scaled);
}

// 0x0047B650
void CSfxManager::SetPitch(float pitch, int /*unused1*/, int /*unused2*/)
{
    if (NoSound())
        return;
    Script::GetInteger(kDebugB);
    if (!(pitch > 0.0099999998f) && !std::isnan(pitch))
        pitch = 0.0099999998f;
    Pitch() = pitch;
    SetVoicePitch(pitch);
    // (then a call with the pitch to 0x004504F0, a jump to the folded stub returning false: nothing to do)
}

// 0x0047B6B0
void CSfxManager::PrintSounds(int a, int b, int c)
{
    if (!NoSound())
        PrintSoundDebug(a, b, c);
}

// 0x0047B6F0
bool CSfxManager::IsSoundPlaying(uint32_t uniqueId, int* index)
{
    if (NoSound() || uniqueId == 0)
        return false;
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (Playing()[i].uniqueId == uniqueId) {
            if (!IsVoicePlaying(i))
                return false;
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

// 0x0047B760
float CSfxManager::GetDropoffDistance(uint32_t checksum)
{
    if (NoSound())
        return DefaultDropoff();
    WaveTableEntry* entry = GetWaveTableEntry(checksum);
    if (!entry)
        return 0.0f;
    return entry->dropoff == 0.0f ? DefaultDropoff() : entry->dropoff;
}

// 0x0047B7C0
bool CSfxManager::SetSoundParamsScaled(uint32_t uniqueId, sVolume* volume, float pitch)
{
    if (NoSound())
        return true;
    int index = 0;
    for (; index < kMaxPlaying; ++index) {
        if (Playing()[index].uniqueId == uniqueId)
            break;
    }
    if (index == kMaxPlaying || !IsVoicePlaying(index)) {
        ReportMissing();
        return false;
    }
    PlayingSound& sound = Playing()[index];
    if (!volume->IsSilent()) {
        if (sound.props.volume != 100.0f)
            volume->PercentageAdjustment(sound.props.volume);
        if (sound.props.pitch != 100.0f)
            pitch = static_cast<float>(static_cast<double>(sound.props.pitch) * pitch * 0.0099999998f);
    }
    WaveTableEntry* entry = sound.entry;
    if (!entry)
        return false;
    if (!volume->IsSilent())
        AdjustVolumeAndPitch(volume, &pitch, entry);
    SetVoiceParameters(index, volume, pitch);
    return true;
}

} // namespace Sfx

REPLACE(0x0047AC10, Sfx::FalloffFromChecksum);
REPLACE(0x0047AC50, &Sfx::sVolume::PercentageAdjustment);
REPLACE(0x0047ACF0, &Sfx::sVolume::IsSilent);
REPLACE(0x0047AD60, &Sfx::sVolume::SetSilent);
REPLACE(0x0047AD80, &Sfx::CSfxManager::GetPlayingIndex);
REPLACE(0x0047AE20, &Sfx::CSfxManager::AddPositionalSound);
REPLACE(0x0047AE90, &Sfx::CSfxManager::StopGroup);
REPLACE(0x0047AEA0, &Sfx::CSfxManager::UnloadGroup);
REPLACE(0x0047AF10, &Sfx::CSfxManager::UnloadAllGroups);
REPLACE(0x0047AF40, &Sfx::CSfxManager::InitPlaybackProps);
REPLACE(0x0047AFD0, Sfx::GetMainVolume);
REPLACE(0x0047AFE0, Sfx::CountPlayingVoices);
REPLACE(0x0047B010, Sfx::NoSound);
REPLACE(0x0047B030, &Sfx::sVolume::Init);
REPLACE(0x0047B070, &Sfx::sVolume::SetType);
REPLACE(0x0047B080, &Sfx::sVolume::GetLoudestChannel);
REPLACE(0x0047B160, &Sfx::CSfxManager::Construct);
REPLACE(0x0047B1B0, &Sfx::CSfxManager::CleanUp);
REPLACE(0x0047B210, &Sfx::CSfxManager::SetDefaultDropoff);
REPLACE(0x0047B240, &Sfx::CSfxManager::StopAllSounds);
REPLACE(0x0047B2A0, &Sfx::CSfxManager::UpdateVoiceVolumes);
REPLACE(0x0047B2C0, &Sfx::CSfxManager::GetWaveTableEntry);
REPLACE(0x0047B340, &Sfx::CSfxManager::IsPaused);
REPLACE(0x0047B360, &Sfx::CSfxManager::LoadSound);
REPLACE(0x0047B480, &Sfx::CSfxManager::AdjustVolumeAndPitch);
REPLACE(0x0047B4E0, &Sfx::CSfxManager::StopSound);
REPLACE(0x0047B520, &Sfx::CSfxManager::SetSoundParams);
REPLACE(0x0047B5D0, &Sfx::CSfxManager::SetMainVolume);
REPLACE(0x0047B650, &Sfx::CSfxManager::SetPitch);
REPLACE(0x0047B6B0, &Sfx::CSfxManager::PrintSounds);
REPLACE(0x0047B6F0, &Sfx::CSfxManager::IsSoundPlaying);
REPLACE(0x0047B760, &Sfx::CSfxManager::GetDropoffDistance);
REPLACE(0x0047B7C0, &Sfx::CSfxManager::SetSoundParamsScaled);
