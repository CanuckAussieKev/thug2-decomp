// The VehicleSound component: see vehicle_sound.h. From the disassembly of 0x0043C540-0x0043D2AF.

#include "game/obj/vehicle_sound.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/sound_component.h"
#include "game/obj/vehicle_component.h"
#include "game/script/param_struct.h"
#include "game/sound/sfx.h"
#include "game/sys/game_clock.h"
#include "game/sys/stopwatch.h"
#include "game/park_editing/park_editor.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/camera_lookaround.h"
#include "game/skater/vehicle_sound.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kVehicle = 0xE47F1B79;
constexpr uint32_t kSound = 0x07713C7B;
constexpr uint32_t kRates = 0x38EC37AC;       // PlayerVehicleSoundGlobalParameters
constexpr uint32_t kAllSettings = 0x3C2DA7F5; // PlayerVehicleSounds
constexpr uint32_t kParkEditorOff = 0xD443A2BC;

const float kNoWheel = std::bit_cast<float>(0x60AD78ECu);     // 0x0064682C: 1e20
const float kToMph = std::bit_cast<float>(0x3D68BA2Fu);       // 0x00646828: 1 / 17.6
const float kPercent = 100.0f;                                // 0x006463D4
const double kTwoTo32 = 4294967296.0;                         // 0x006453B4 (an unsigned clock difference)

SoundFx::CAudioMixer* AudioMixerHandle() { return SoundFx::g_audioMixer; }
uint32_t ClockOffset() { return static_cast<uint32_t>(g_clockOffset); }

// A float of the global structure kRates (asserted).
float Rate(uint32_t name)
{
    float value;
    Scripts::ReadSubStruct(kRates)->FetchFloat(name, &value, 1);
    return value;
}
float Setting(Scripts::CStruct* settings, uint32_t name)
{
    float value;
    settings->FetchFloat(name, &value, 1);
    return value;
}

// The clock now (with its offset) less `then`, wrapping as the original's 32-bit sums do.
uint32_t Since(int clock, int then) { return static_cast<uint32_t>(clock) + (ClockOffset() - static_cast<uint32_t>(then)); }
int Now(int clock) { return static_cast<int>(static_cast<uint32_t>(clock) + ClockOffset()); }
// An unsigned millisecond difference as a double (the original's FILD and its 2^32 fix for negatives).
double Unsigned(uint32_t difference)
{
    int value = static_cast<int>(difference);
    return value < 0 ? value + kTwoTo32 : value;
}

// A fraction kept to [0, 1] as the original does it: over 1 -> 1 (compared unrounded); then, rounded, under 0 -> 0.
float Fraction(double value)
{
    if (value > 1.0)
        return 1.0f;
    float rounded = static_cast<float>(value);
    return rounded < 0.0f ? 0.0f : rounded;
}

// The Sound component's 0x00433240 (not rewritten yet): the sound a structure describes played.
void PlaySoundFromStructure(CObjectModule* sound, Scripts::CStruct* description)
{
    static_cast<CAudioEmitterModule*>(sound)->EmitSound(description);
}

} // namespace

// 0x0043CFC0
CObjectModule* __cdecl CEngineAudioModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CEngineAudioModule));
    return memory ? static_cast<CEngineAudioModule*>(memory)->MakeNew() : nullptr;
}

// 0x0043C590
CEngineAudioModule* CEngineAudioModule::MakeNew()
{
    CObjectModule::MakeNew();
    mp_vehicle = nullptr;
    mp_sound = nullptr;
    m_settings = nullptr;
    m_parkEditorLevel = false;
    m_speed = 0.0f;
    m_gear = 0;
    m_shiftTime = 0;
    m_unknownA8 = 0;
    m_skid = 0.0f;
    m_collideTime = 0;
    m_engineSound = 0;
    m_skidSound = 0;
    m_vtable = kVtable;
    m_kindTag = kType;
    return this;
}

// 0x0043C5F0
void CEngineAudioModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_engineSound) {
        AudioMixerHandle()->StopSound(m_engineSound);
        m_engineSound = 0;
    }
    if (m_skidSound) {
        AudioMixerHandle()->StopSound(m_skidSound);
        m_skidSound = 0;
    }
    CObjectModule::DestroyBody();
}

// 0x0043D020
CEngineAudioModule* CEngineAudioModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0043C670
void CEngineAudioModule::ConfigureFromData(Scripts::CStruct*)
{
    m_parkEditorLevel = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor) + 0x40) != kParkEditorOff;
}

// 0x0043D050
void CEngineAudioModule::Seal()
{
    mp_vehicle = static_cast<CDrivableModule*>(m_entity->FetchModule(kVehicle));
    mp_sound = m_entity->FetchModule(kSound);
    ReadSettings();
}

// 0x0043D290
void CEngineAudioModule::TickFrame()
{
    if (!m_settings)
        return;
    TickEngine();
    UpdateSkid();
    UpdateCollideSound();
}

// 0x0043C540
void CEngineAudioModule::FetchDiagnostics(Scripts::CStruct* info)
{
    info->PutNameKey(0x2A19E830, m_engineSoundName);
    info->PutNameKey(0xE798FE80, m_skidSoundName);
    info->PutSubStruct(0x36FA68D2, m_collideSound);
    // (then the base's FetchDiagnostics, which is empty: 0x005BAB90)
}

// 0x0043CE70
void CEngineAudioModule::ReadSettings()
{
    Scripts::ReadSubStruct(kAllSettings)->ReadSubStruct(mp_vehicle->m_soundsName, &m_settings, 1);
    m_settings->ReadHashValue(0x306F8A9F, &m_engineSoundName, 0);
    m_settings->ReadHashValue(0x667FA96A, &m_skidSoundName, 0);
    Scripts::CArray* gears;
    m_settings->FetchList(0x83273E6B, &gears, 1);
    m_gearCount = static_cast<int>(gears->m_extent);
    for (int i = 0; i < m_gearCount; ++i) {
        auto* gear = reinterpret_cast<Scripts::CStruct*>(gears->FetchPart(static_cast<uint32_t>(i)));
        gear->FetchFloat(0xFAD20901, &m_gearSteps[i].upAt, 1);
        gear->FetchFloat(0xFCF5C8A2, &m_gearSteps[i].downAt, 1);
        gear->FetchFloat(0x735B5CE4, &m_gearSteps[i].lowRpm, 1);
        gear->FetchFloat(0xE59A89A6, &m_gearSteps[i].highRpm, 1);
    }
    m_settings->ReadSubStruct(0xBF8E0ACE, &m_collideSound, 1);
    if (m_parkEditorLevel) {
        Scripts::CStruct* park = Scripts::ReadSubStruct(kRates);
        park->ReadHashValue(0xF5406E1A, &m_engineSoundName, 1);
        park->ReadHashValue(0x060E93A2, &m_skidSoundName, 1);
        park->ReadSubStruct(0x22995285, &m_collideSound, 1);
    }
}

// 0x0043C690
void CEngineAudioModule::TickEngine()
{
    float frame = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    CDrivableModule* vehicle = mp_vehicle;
    // The slowest driven wheel (always four looked at, the last first).
    float slowest = kNoWheel;
    for (int i = 3; i >= 0; --i) {
        const CDrivableModule::Wheel& wheel = vehicle->m_wheels[i];
        if (!wheel.drive)
            continue;
        float speed = static_cast<float>(std::fabs(static_cast<double>(wheel.spin)) * wheel.radius);
        if (!(slowest < speed))
            slowest = speed;
    }
    float speed = static_cast<float>(slowest * kToMph);
    if (!(vehicle->m_throttle || vehicle->m_reverse) || vehicle->m_brake)
        speed = static_cast<float>(Rate(0xEC67BC97) * static_cast<double>(speed));

    // The speed eased towards the wheels', each step limited by the rates (as the original compares them, a step
    // the wrong way is kept and one the right way isn't).
    double follow = Rate(0x02A7095B) * static_cast<double>(frame);
    double eased = (static_cast<double>(m_speed) - speed) * follow + speed;
    m_speed = static_cast<float>(eased);
    if (eased < speed) {
        double step = static_cast<double>(Rate(0xCA5EFD91)) * frame + m_speed;
        if (!(step > m_speed))
            m_speed = static_cast<float>(step);
    } else {
        double step = static_cast<double>(m_speed) - static_cast<double>(Rate(0x77C8E7C8)) * frame;
        if (!(step < m_speed))
            m_speed = static_cast<float>(step);
    }

    // The gear: shifted at the gear's speeds while a wheel touches; back to the first after a while in the air.
    if (mp_vehicle->m_wheelsInContact != 0) {
        int gear = m_gear;
        if (gear < m_gearCount - 1 && m_speed > m_gearSteps[gear].upAt) {
            m_gear = gear + 1;
            m_shiftTime = Now(FrameClock::GameClockElapsed());
        } else if (gear > 0 && m_speed < m_gearSteps[gear].downAt) {
            m_gear = gear - 1;
            m_shiftTime = Now(FrameClock::GameClockElapsed());
        }
    } else {
        float delay = Rate(0xC0F451D4);
        if (mp_vehicle->m_airTime > delay)
            m_gear = 0;
    }

    // Just after a shift the engine is quieter (from 0xBFA32092 of full, back to full over 0xFB6BD20E ms).
    float shiftTime = Rate(0xFB6BD20E);
    int shifted = m_shiftTime;
    uint32_t sinceShift = Since(FrameClock::GameClockElapsed(), shifted);
    float quietest = Rate(0xBFA32092);
    double recovery = Unsigned(sinceShift) / shiftTime * (1.0 - quietest) + quietest;
    float loudness = recovery > 1.0 ? 1.0f : static_cast<float>(recovery);

    // The engine's speed: where the speed is between the last gear's shift point and this one's, across this gear's
    // pitch range; eased.
    int gear = m_gear;
    double low = gear ? m_gearSteps[gear - 1].upAt : 0.0;
    double across = (static_cast<double>(m_speed) - low) / (static_cast<double>(m_gearSteps[gear].upAt) - low);
    float target = static_cast<float>((static_cast<double>(m_gearSteps[gear].highRpm) - m_gearSteps[gear].lowRpm) * across +
                                      m_gearSteps[gear].lowRpm);
    double ease = static_cast<double>(Rate(0x3DD9C040)) * frame;
    m_rpm = static_cast<float>((static_cast<double>(target) - m_rpm) * ease + m_rpm);
    float highRpm = Setting(m_settings, 0x95DF9449);
    float lowRpm = Setting(m_settings, 0x05AC84A3);
    float rpm = Fraction((static_cast<double>(m_rpm) - lowRpm) / (static_cast<double>(highRpm) - lowRpm));

    SoundFx::volumeRef volume;
    volume.PrepareAll();
    SoundFx::CAudioMixer* sfx = AudioMixerHandle();
    sfx->FetchLoudness(&volume, &m_entity->m_atPoint, sfx->GetDropoffDistance(m_engineSoundName), 0, nullptr, nullptr);
    float loudest = Setting(m_settings, 0x288B029D);
    float quiet = Setting(m_settings, 0xB8F81277);
    volume.PercentTweak(
        static_cast<float>(((static_cast<double>(loudest) - quiet) * rpm + quiet) * loudness));
    float highPitch = Setting(m_settings, 0x9F46344A);
    float lowPitch = Setting(m_settings, 0x2660AF3B);
    float pitch = static_cast<float>((static_cast<double>(highPitch) - lowPitch) * rpm + lowPitch);
    if (!m_engineSound)
        m_engineSound = AudioMixerHandle()->EmitSound(m_engineSoundName, &volume, pitch, 0, nullptr, 0);
    else
        AudioMixerHandle()->SetSoundParamsScaled(m_engineSound, &volume, pitch);
    // (volumeRef's destructor is empty: 0x005480A0)
}

// 0x0043CBC0
void CEngineAudioModule::UpdateSkid()
{
    // The fastest slip of a sliding wheel (always four looked at, the last first), while braking.
    float slip = 0.0f;
    CDrivableModule* vehicle = mp_vehicle;
    if (vehicle->m_brake || vehicle->m_handbrake) {
        for (int i = 3; i >= 0; --i) {
            const CDrivableModule::Wheel& wheel = vehicle->m_wheels[i];
            int state = wheel.state;
            bool sliding = state == CDrivableModule::kWheelStaticToDynamic ||
                           state == CDrivableModule::kWheelDynamic ||
                           ((state == CDrivableModule::kWheelHandbrakeLocked ||
                             state == CDrivableModule::kWheelHandbrakeThrottle) &&
                            wheel.slip > wheel.maxStaticGripVel);
            if (sliding && !(slip > wheel.slip))
                slip = wheel.slip;
        }
    }
    float rate = Rate(0x95FBE774);
    double ease = FrameClock::GetSlomoFrameSeconds() * rate;
    m_skid = static_cast<float>((static_cast<double>(slip) - m_skid) * ease + m_skid);
    float fullSlip = Setting(m_settings, 0x28F1AAD0);
    float level = Fraction(static_cast<double>(m_skid) / fullSlip);

    SoundFx::volumeRef volume;
    volume.PrepareAll();
    SoundFx::CAudioMixer* sfx = AudioMixerHandle();
    sfx->FetchLoudness(&volume, &m_entity->m_atPoint, sfx->GetDropoffDistance(m_skidSoundName), 0, nullptr, nullptr);
    float loudest = Setting(m_settings, 0x42EA5746);
    float quiet = Setting(m_settings, 0x43E6CDF2);
    volume.PercentTweak(static_cast<float>((static_cast<double>(loudest) - quiet) * level + quiet));
    float highPitch = Setting(m_settings, 0xE68BE784);
    float lowPitch = Setting(m_settings, 0x76F8F76E);
    float pitch = static_cast<float>((static_cast<double>(highPitch) - lowPitch) * level + lowPitch);
    if (!volume.IsAudioOff()) {
        if (!m_skidSound)
            m_skidSound = AudioMixerHandle()->EmitSound(m_skidSoundName, &volume, pitch, 0, nullptr, 0);
        else
            AudioMixerHandle()->SetSoundParamsScaled(m_skidSound, &volume, pitch);
    } else if (m_skidSound) {
        AudioMixerHandle()->StopSound(m_skidSound);
        m_skidSound = 0;
    }
}

// 0x0043D090
void CEngineAudioModule::UpdateCollideSound()
{
    int last = m_collideTime;
    float delay = Rate(0x71B4B9FE);
    uint32_t since = Since(FrameClock::GameClockElapsed(), last);
    if (Unsigned(since) < delay)
        return;
    float fullCollision = Setting(m_settings, 0xCEA59D0B);
    float strength = static_cast<float>(static_cast<double>(mp_vehicle->m_collisionImpulse) / fullCollision);
    float cutoff = Rate(0xF6981837);
    if (!(strength > cutoff))
        return;
    m_collideSound->PushFloat(0x9E497FC6, static_cast<float>(strength * kPercent));
    PlaySoundFromStructure(mp_sound, m_collideSound);
    m_collideTime = Now(FrameClock::GameClockElapsed());

    // The skater told how hard (0-1). (Out of memory, the original adds to a null structure - kept.)
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    Scripts::CStruct* event = memory ? memory->PrepareAll() : nullptr;
    float level = strength > 1.0f ? 1.0f : (strength < 0.0f ? 0.0f : strength);
    event->PushFloat(0x29461E27, level);
    mp_vehicle->m_riderRef->PostOwnEvent(0xC4A60423, event);
    if (event) {
        event->Clear();
        Scripts::DeleteStruct(event);
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CEngineAudioModule::Teardown,
         &GameObjects::CEngineAudioModule::TickFrame,
         &GameObjects::CEngineAudioModule::ConfigureFromData,
         &GameObjects::CEngineAudioModule::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         &GameObjects::CEngineAudioModule::FetchDiagnostics,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
