// The sound component (game/obj/sound_component.h).

#include "game/obj/sound_component.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/mth/math.h"
#include "game/obj/emitter.h"
#include "game/obj/proxim.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646514);

constexpr uint32_t kObjAdjustSound = 0x3E5E8023;
constexpr uint32_t kObjStopSound = 0x4E4132D6;
constexpr uint32_t kObjSetSound = 0xE8347171;

constexpr float kPercent = 0.009999999776482582f;      // 0x0064544C
constexpr float kFramesPerSecond = 60.0f;              // 0x00645EA0
constexpr float kInchesPerFoot = 12.0f;                // 0x0064546C
constexpr float kDefaultDropoff = 1020.0f;             // 0x0064655C
constexpr float kHearingMargin = 240.0f;               // 0x00646558
constexpr float kThousand = 1000.0f;                   // 0x006453C0
constexpr float kRetryPerDistance = -0.0016666667070239782f; // 0x00646554
constexpr uint32_t kRetryDelay = 0x14D;                // (ms)
constexpr uint32_t kWaitTime = 2000;                   // (ms)

Sfx::CSfxManager* SfxManager() { return GameVariable<Sfx::CSfxManager*>(0x006F85A8); }
CObject* AsSoundObject(CSoundComponent* component) { return reinterpret_cast<CObject*>(component); }

// The camera nearest to `position` (0x004A09E0; its distance through `distance`) and a camera's position (0x00485A00).
void* NearestCamera(const Mth::Vector* position, float* distance)
{
    return GameFunction<void*(__cdecl*)(const Mth::Vector*, float*)>(0x004A09E0)(position, distance);
}
const Mth::Vector* CameraPosition(void* camera)
{
    return static_cast<Gfx::Camera*>(camera)->GetPos();
}

// The CRT's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int Ftol(double x) { return static_cast<int>(static_cast<int64_t>(x)); }

// The play parameters PlayObjectSound takes.
struct SoundParams {
    float pitch;
    float volume;
    float dropoff;
    int falloff;
};

} // namespace

// 0x00433100
CBaseComponent* __cdecl CSoundComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSoundComponent));
    return memory ? static_cast<CSoundComponent*>(memory)->Construct() : nullptr;
}

// 0x00432C80
void __cdecl CSoundComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00683118), 0x18);
}

// 0x00432D90
CSoundComponent* CSoundComponent::Construct()
{
    CBaseComponent::Construct();
    mp_emitter = nullptr;
    mp_proxim = nullptr;
    m_vtable = kVtable;
    m_type = kType;
    m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
    m_lastPos = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x00432DF0
void CSoundComponent::DestroyBody()
{
    m_vtable = kVtable;
    SfxManager()->StopObjectSounds(AsSoundObject(this));
    CBaseComponent::DestroyBody();
}

// 0x00433160
CSoundComponent* CSoundComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00432CA0
void CSoundComponent::InitFromStructure(Script::CStruct* params)
{
    uint32_t node = reinterpret_cast<uint32_t>(params); // (read into the parameters' own argument slot)
    if (params->GetChecksum(0x08470F2E, &node, 0)) // ProximNode
        mp_proxim = Proxim_GetNode(node);
}

// 0x004335D0
void CSoundComponent::Update()
{
    m_lastPos = m_pos;
    m_pos = m_object->m_pos;
    UpdateWaitingSounds();
}

// 0x00433190
int CSoundComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    if (name == kObjAdjustSound) {
        AdjustSound(params, script);
        return 1;
    }
    if (name == kObjStopSound) {
        uint32_t sound = 0; // (read into the name's argument slot, cleared first)
        params->GetChecksum(0u, &sound, 0);
        SfxManager()->StopObjectSound(AsSoundObject(this), sound);
        if (m_waiting[0].props.checksum == sound)
            m_waiting[0].props.checksum = 0;
        if (m_waiting[1].props.checksum == sound)
            m_waiting[1].props.checksum = 0;
        return 1;
    }
    if (name == kObjSetSound) {
        SetSound(params);
        return 1;
    }
    return 2;
}

// 0x00432D40
void CSoundComponent::KeepLoopingSound(const Sfx::PlaybackProps* props, uint32_t time)
{
    for (WaitingSound& waiting : m_waiting) {
        if (waiting.props.checksum)
            continue;
        // (the original copies 0x30 bytes - 4 past the props - then overwrites them with the time)
        waiting.props = *props;
        waiting.time = time + kWaitTime;
        return;
    }
}

// 0x00432E50
Mth::Vector* CSoundComponent::GetSoundPosition(Mth::Vector* out, void* camera)
{
    if (mp_emitter && camera) {
        mp_emitter->GetClosestPoint(out, CameraPosition(camera));
        return out;
    }
    *out = m_pos;
    return out;
}

// 0x00432EB0
Mth::Vector* CSoundComponent::GetPreviousSoundPosition(Mth::Vector* out, void* camera)
{
    if (mp_emitter && camera) {
        mp_emitter->GetClosestPoint(out, CameraPosition(camera));
        return out;
    }
    *out = m_lastPos;
    return out;
}

// 0x00432F10
bool CSoundComponent::GetSoundExtent(void* camera, Mth::Vector* out)
{
    if (!camera || !mp_proxim)
        return false;
    Mth::Vector nearest;
    *out = *mp_proxim->GetClosestPoint(&nearest, CameraPosition(camera));
    return true;
}

// 0x00432F70
void CSoundComponent::UpdateProps(Sfx::PlaybackProps* props)
{
    if (props->pitch3 != props->pitch) { // (not a number: moved too)
        float step = static_cast<float>(Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * reinterpret_cast<float&>(props->unknown20));
        props->pitch = Mth::KeepWithin(props->pitch3, props->pitch, step);
    }
    if (props->volume3 != props->volume) {
        float step = static_cast<float>(Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * reinterpret_cast<float&>(props->unknown24));
        props->volume = Mth::KeepWithin(props->volume3, props->volume, step);
    }
}

// 0x00433480
void CSoundComponent::UpdateWaitingSounds()
{
    uint32_t now = Tmr::GetTime();
    for (WaitingSound& waiting : m_waiting) {
        Sfx::PlaybackProps& props = waiting.props;
        if (!props.checksum)
            continue;
        UpdateProps(&props);
        if (waiting.time > now)
            continue;
        float distance = 0.0f; // (no camera: the original's is whatever its stack held)
        void* camera = NearestCamera(&m_pos, &distance);
        // (a dropoff of exactly 0 means the default)
        float dropoff = props.dropoff == 0.0f ? kDefaultDropoff : props.dropoff;
        if (camera && static_cast<double>(kHearingMargin) + dropoff > distance) {
            SoundParams params = {props.pitch2, props.volume2, props.dropoff, static_cast<int>(props.falloff)};
            uint32_t id = SfxManager()->PlayObjectSound(props.checksum, &params, AsSoundObject(this), false, true);
            if (!id)
                continue;
            if (Sfx::PlaybackProps* playing = SfxManager()->GetPositionalProps(AsSoundObject(this), id, false)) {
                // Faded in from silence to its volume, at a hundredth of it a frame.
                playing->volume3 = props.volume3;
                playing->pitch3 = props.pitch3;
                reinterpret_cast<float&>(playing->unknown24) = playing->volume3 * kPercent;
                playing->unknown20 = props.unknown20;
                playing->volume = 0.0f;
            }
            props.checksum = 0;
        } else {
            // Looked at again later: the further beyond hearing, the later.
            int sooner = Ftol((static_cast<double>(distance) - dropoff) * kThousand * kRetryPerDistance);
            waiting.time = now - static_cast<uint32_t>(sooner) + kRetryDelay;
        }
    }
}

// 0x00433240
void CSoundComponent::PlaySound(Script::CStruct* params)
{
    uint32_t sound = 0;
    float volume = 100.0f, pitch = 100.0f, dropoff = 0.0f;
    uint32_t emitter = 0;
    bool noPosUpdate = false;
    int falloff = 0;
    params->GetChecksum(0u, &sound, 0);
    params->GetFloat(0xF6A36814, &volume, 0); // vol
    params->GetFloat(0xD8604126, &pitch, 0);  // pitch
    if (params->ContainsComponentNamed(0x9E497FC6)) { // Percent: between the min and max
        float minVolume = 0.0f, maxVolume = 100.0f, minPitch = 100.0f, maxPitch = 100.0f, percent = 0.0f;
        params->GetFloat(0x0693DAAF, &maxVolume, 0); // maxVol
        params->GetFloat(0x4391992D, &minVolume, 0); // minVol
        params->GetFloat(0xFA3E14C5, &maxPitch, 0);  // maxPitch
        params->GetFloat(0x1C5EBB24, &minPitch, 0);  // minPitch
        params->GetFloat(0x9E497FC6, &percent, 1);   // Percent
        double fraction = static_cast<double>(percent) * kPercent;
        volume = static_cast<float>((static_cast<double>(maxVolume) - minVolume) * fraction + minVolume);
        pitch = static_cast<float>((static_cast<double>(maxPitch) - minPitch) * fraction + minPitch);
    }
    if (params->GetFloat(0xFF2020EC, &dropoff, 0)) // dropoff (feet)
        dropoff = dropoff * kInchesPerFoot;
    uint32_t function = 0; // (the original's slot holds Percent's value, or garbage)
    if (params->GetChecksum(0x0C6AC50A, &function, 0)) // dropoff_function
        falloff = Sfx::FalloffFromChecksum(function);
    if (params->ContainsFlag(0xBB39837F)) // NoPosUpdate
        noPosUpdate = true;
    if (params->GetChecksum(0x8A7132CE, &emitter, 0)) // emitter
        mp_emitter = Emitter_GetNode(emitter);
    else
        mp_emitter = nullptr;
    if (!sound) {
        // The sound set for a type (Obj_SetSound). (Read into the parameters' own argument slot.)
        int type = -1;
        params->GetInteger("Type", &type, 0);
        if (type == -1 || type > 8)
            return;
        sound = m_sounds[type]; // (type 8 reads the first waiting sound's checksum, as the original)
        if (!sound)
            return;
    }
    reinterpret_cast<void(__thiscall*)(CSoundComponent*, uint32_t, float, float)>(m_vtable[13])(this, sound, volume,
                                                                                               pitch);
    SoundParams play = {pitch, volume, dropoff, falloff};
    SfxManager()->PlayObjectSound(sound, &play, AsSoundObject(this), noPosUpdate, false);
}

// 0x00432FF0
void CSoundComponent::AdjustSound(Script::CStruct* params, Script::CScript* /*script*/)
{
    float volumePercent = 100.0f, pitchPercent = 100.0f;
    // (read into the parameters' own argument slot: none, the pointer's value)
    uint32_t sound = reinterpret_cast<uint32_t>(params);
    params->GetChecksum(0u, &sound, 0);
    Sfx::PlaybackProps* props = SfxManager()->GetPositionalProps(AsSoundObject(this), sound, true);
    if (!props) {
        for (WaitingSound& waiting : m_waiting) {
            if (waiting.props.checksum == sound) {
                props = &waiting.props;
                break;
            }
        }
        if (!props)
            return;
    }
    if (params->GetFloat(0x330F3415, &volumePercent, 0)) { // volumePercent
        props->volume3 = static_cast<float>(static_cast<double>(volumePercent) * props->volume2 * kPercent);
        if (!params->GetFloat(0xDAA9A3C2, reinterpret_cast<float*>(&props->unknown24), 0)) // volumeStep
            props->volume = props->volume3;
    }
    if (params->GetFloat(0xEE616C13, &pitchPercent, 0)) { // pitchPercent
        props->pitch3 = static_cast<float>(static_cast<double>(pitchPercent) * props->pitch2 * kPercent);
        if (!params->GetFloat(0x7B090774, reinterpret_cast<float*>(&props->unknown20), 0)) // pitchStep
            props->pitch = props->pitch3;
    }
}

// 0x00432CE0
void CSoundComponent::SetSound(Script::CStruct* params)
{
    int type = -1;
    uint32_t sound = 0;
    params->GetInteger(0x7321A8D6, &type, 0); // type
    params->GetChecksum(0u, &sound, 0);
    if (type != -1 && sound && type <= 8)
        m_sounds[type] = sound; // (8 is the first waiting sound's checksum; a negative type writes before the slots)
}

// 0x00433630
bool CSoundComponent::Obj_PlaySound(Script::CStruct* params, Script::CScript* /*script*/)
{
    PlaySound(params);
    return true;
}

} // namespace Obj

REPLACE(0x00433100, Obj::CSoundComponent::Create);
REPLACE(0x00432C80, Obj::CSoundComponent::RegisterMemberFunctions);
REPLACE(0x00432D90, &Obj::CSoundComponent::Construct);
REPLACE(0x00432DF0, &Obj::CSoundComponent::DestroyBody);
REPLACE(0x00433160, &Obj::CSoundComponent::Destroy);
REPLACE(0x00432CA0, &Obj::CSoundComponent::InitFromStructure);
REPLACE(0x004335D0, &Obj::CSoundComponent::Update);
REPLACE(0x00433190, &Obj::CSoundComponent::CallMemberFunction);
REPLACE(0x00432D40, &Obj::CSoundComponent::KeepLoopingSound);
REPLACE(0x00432E50, &Obj::CSoundComponent::GetSoundPosition);
REPLACE(0x00432EB0, &Obj::CSoundComponent::GetPreviousSoundPosition);
REPLACE(0x00432F10, &Obj::CSoundComponent::GetSoundExtent);
REPLACE(0x00432F70, &Obj::CSoundComponent::UpdateProps);
REPLACE(0x00433480, &Obj::CSoundComponent::UpdateWaitingSounds);
REPLACE(0x00433240, &Obj::CSoundComponent::PlaySound);
REPLACE(0x00432FF0, &Obj::CSoundComponent::AdjustSound);
REPLACE(0x00432CE0, &Obj::CSoundComponent::SetSound);
REPLACE(0x00433630, &Obj::CSoundComponent::Obj_PlaySound);
