// The VehicleSound component: see vehicle_sound.h. From the disassembly of 0x0043C540-0x0043D2AF.

#include "game/obj/vehicle_sound.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/sound_component.h"
#include "game/obj/vehicle_component.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/ed/park_editor.h"
#include "game/obj/control_pad.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

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

Sfx::CSfxManager* SfxManager() { return Sfx::g_sfxManager; }
uint32_t ClockOffset() { return static_cast<uint32_t>(g_clockOffset); }

// A float of the global structure kRates (asserted).
float Rate(uint32_t name)
{
    float value;
    Script::GetStructure(kRates)->GetFloat(name, &value, 1);
    return value;
}
float Setting(Script::CStruct* settings, uint32_t name)
{
    float value;
    settings->GetFloat(name, &value, 1);
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
void PlaySoundFromStructure(CBaseComponent* sound, Script::CStruct* description)
{
    static_cast<CSoundComponent*>(sound)->PlaySound(description);
}

} // namespace

// 0x0043CFC0
CBaseComponent* __cdecl CVehicleSoundComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CVehicleSoundComponent));
    return memory ? static_cast<CVehicleSoundComponent*>(memory)->Construct() : nullptr;
}

// 0x0043C590
CVehicleSoundComponent* CVehicleSoundComponent::Construct()
{
    CBaseComponent::Construct();
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
    m_type = kType;
    return this;
}

// 0x0043C5F0
void CVehicleSoundComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (m_engineSound) {
        SfxManager()->StopSound(m_engineSound);
        m_engineSound = 0;
    }
    if (m_skidSound) {
        SfxManager()->StopSound(m_skidSound);
        m_skidSound = 0;
    }
    CBaseComponent::DestroyBody();
}

// 0x0043D020
CVehicleSoundComponent* CVehicleSoundComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0043C670
void CVehicleSoundComponent::InitFromStructure(Script::CStruct*)
{
    m_parkEditorLevel = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(Ed::g_parkEditor) + 0x40) != kParkEditorOff;
}

// 0x0043D050
void CVehicleSoundComponent::Finalize()
{
    mp_vehicle = static_cast<CVehicleComponent*>(m_object->GetComponent(kVehicle));
    mp_sound = m_object->GetComponent(kSound);
    ReadSettings();
}

// 0x0043D290
void CVehicleSoundComponent::Update()
{
    if (!m_settings)
        return;
    UpdateEngine();
    UpdateSkid();
    UpdateCollideSound();
}

// 0x0043C540
void CVehicleSoundComponent::GetDebugInfo(Script::CStruct* info)
{
    info->AddChecksum(0x2A19E830, m_engineSoundName);
    info->AddChecksum(0xE798FE80, m_skidSoundName);
    info->AddStructure(0x36FA68D2, m_collideSound);
    // (then the base's GetDebugInfo, which is empty: 0x005BAB90)
}

// 0x0043CE70
void CVehicleSoundComponent::ReadSettings()
{
    Script::GetStructure(kAllSettings)->GetStructure(mp_vehicle->m_soundsName, &m_settings, 1);
    m_settings->GetChecksum(0x306F8A9F, &m_engineSoundName, 0);
    m_settings->GetChecksum(0x667FA96A, &m_skidSoundName, 0);
    Script::CArray* gears;
    m_settings->GetArray(0x83273E6B, &gears, 1);
    m_gearCount = static_cast<int>(gears->m_size);
    for (int i = 0; i < m_gearCount; ++i) {
        auto* gear = reinterpret_cast<Script::CStruct*>(gears->GetElement(static_cast<uint32_t>(i)));
        gear->GetFloat(0xFAD20901, &m_gears[i].upAt, 1);
        gear->GetFloat(0xFCF5C8A2, &m_gears[i].downAt, 1);
        gear->GetFloat(0x735B5CE4, &m_gears[i].lowRpm, 1);
        gear->GetFloat(0xE59A89A6, &m_gears[i].highRpm, 1);
    }
    m_settings->GetStructure(0xBF8E0ACE, &m_collideSound, 1);
    if (m_parkEditorLevel) {
        Script::CStruct* park = Script::GetStructure(kRates);
        park->GetChecksum(0xF5406E1A, &m_engineSoundName, 1);
        park->GetChecksum(0x060E93A2, &m_skidSoundName, 1);
        park->GetStructure(0x22995285, &m_collideSound, 1);
    }
}

// 0x0043C690
void CVehicleSoundComponent::UpdateEngine()
{
    float frame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    CVehicleComponent* vehicle = mp_vehicle;
    // The slowest driven wheel (always four looked at, the last first).
    float slowest = kNoWheel;
    for (int i = 3; i >= 0; --i) {
        const CVehicleComponent::Wheel& wheel = vehicle->m_wheels[i];
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
        if (gear < m_gearCount - 1 && m_speed > m_gears[gear].upAt) {
            m_gear = gear + 1;
            m_shiftTime = Now(Tmr::GameClockElapsed());
        } else if (gear > 0 && m_speed < m_gears[gear].downAt) {
            m_gear = gear - 1;
            m_shiftTime = Now(Tmr::GameClockElapsed());
        }
    } else {
        float delay = Rate(0xC0F451D4);
        if (mp_vehicle->m_airTime > delay)
            m_gear = 0;
    }

    // Just after a shift the engine is quieter (from 0xBFA32092 of full, back to full over 0xFB6BD20E ms).
    float shiftTime = Rate(0xFB6BD20E);
    int shifted = m_shiftTime;
    uint32_t sinceShift = Since(Tmr::GameClockElapsed(), shifted);
    float quietest = Rate(0xBFA32092);
    double recovery = Unsigned(sinceShift) / shiftTime * (1.0 - quietest) + quietest;
    float loudness = recovery > 1.0 ? 1.0f : static_cast<float>(recovery);

    // The engine's speed: where the speed is between the last gear's shift point and this one's, across this gear's
    // pitch range; eased.
    int gear = m_gear;
    double low = gear ? m_gears[gear - 1].upAt : 0.0;
    double across = (static_cast<double>(m_speed) - low) / (static_cast<double>(m_gears[gear].upAt) - low);
    float target = static_cast<float>((static_cast<double>(m_gears[gear].highRpm) - m_gears[gear].lowRpm) * across +
                                      m_gears[gear].lowRpm);
    double ease = static_cast<double>(Rate(0x3DD9C040)) * frame;
    m_rpm = static_cast<float>((static_cast<double>(target) - m_rpm) * ease + m_rpm);
    float highRpm = Setting(m_settings, 0x95DF9449);
    float lowRpm = Setting(m_settings, 0x05AC84A3);
    float rpm = Fraction((static_cast<double>(m_rpm) - lowRpm) / (static_cast<double>(highRpm) - lowRpm));

    Sfx::sVolume volume;
    volume.Init();
    Sfx::CSfxManager* sfx = SfxManager();
    sfx->GetVolume(&volume, &m_object->m_pos, sfx->GetDropoffDistance(m_engineSoundName), 0, nullptr, nullptr);
    float loudest = Setting(m_settings, 0x288B029D);
    float quiet = Setting(m_settings, 0xB8F81277);
    volume.PercentageAdjustment(
        static_cast<float>(((static_cast<double>(loudest) - quiet) * rpm + quiet) * loudness));
    float highPitch = Setting(m_settings, 0x9F46344A);
    float lowPitch = Setting(m_settings, 0x2660AF3B);
    float pitch = static_cast<float>((static_cast<double>(highPitch) - lowPitch) * rpm + lowPitch);
    if (!m_engineSound)
        m_engineSound = SfxManager()->PlaySound(m_engineSoundName, &volume, pitch, 0, nullptr, 0);
    else
        SfxManager()->SetSoundParamsScaled(m_engineSound, &volume, pitch);
    // (sVolume's destructor is empty: 0x005480A0)
}

// 0x0043CBC0
void CVehicleSoundComponent::UpdateSkid()
{
    // The fastest slip of a sliding wheel (always four looked at, the last first), while braking.
    float slip = 0.0f;
    CVehicleComponent* vehicle = mp_vehicle;
    if (vehicle->m_brake || vehicle->m_handbrake) {
        for (int i = 3; i >= 0; --i) {
            const CVehicleComponent::Wheel& wheel = vehicle->m_wheels[i];
            int state = wheel.state;
            bool sliding = state == CVehicleComponent::kWheelStaticToDynamic ||
                           state == CVehicleComponent::kWheelDynamic ||
                           ((state == CVehicleComponent::kWheelHandbrakeLocked ||
                             state == CVehicleComponent::kWheelHandbrakeThrottle) &&
                            wheel.slip > wheel.maxStaticGripVel);
            if (sliding && !(slip > wheel.slip))
                slip = wheel.slip;
        }
    }
    float rate = Rate(0x95FBE774);
    double ease = Tmr::GetSlomoFrameSeconds() * rate;
    m_skid = static_cast<float>((static_cast<double>(slip) - m_skid) * ease + m_skid);
    float fullSlip = Setting(m_settings, 0x28F1AAD0);
    float level = Fraction(static_cast<double>(m_skid) / fullSlip);

    Sfx::sVolume volume;
    volume.Init();
    Sfx::CSfxManager* sfx = SfxManager();
    sfx->GetVolume(&volume, &m_object->m_pos, sfx->GetDropoffDistance(m_skidSoundName), 0, nullptr, nullptr);
    float loudest = Setting(m_settings, 0x42EA5746);
    float quiet = Setting(m_settings, 0x43E6CDF2);
    volume.PercentageAdjustment(static_cast<float>((static_cast<double>(loudest) - quiet) * level + quiet));
    float highPitch = Setting(m_settings, 0xE68BE784);
    float lowPitch = Setting(m_settings, 0x76F8F76E);
    float pitch = static_cast<float>((static_cast<double>(highPitch) - lowPitch) * level + lowPitch);
    if (!volume.IsSilent()) {
        if (!m_skidSound)
            m_skidSound = SfxManager()->PlaySound(m_skidSoundName, &volume, pitch, 0, nullptr, 0);
        else
            SfxManager()->SetSoundParamsScaled(m_skidSound, &volume, pitch);
    } else if (m_skidSound) {
        SfxManager()->StopSound(m_skidSound);
        m_skidSound = 0;
    }
}

// 0x0043D090
void CVehicleSoundComponent::UpdateCollideSound()
{
    int last = m_collideTime;
    float delay = Rate(0x71B4B9FE);
    uint32_t since = Since(Tmr::GameClockElapsed(), last);
    if (Unsigned(since) < delay)
        return;
    float fullCollision = Setting(m_settings, 0xCEA59D0B);
    float strength = static_cast<float>(static_cast<double>(mp_vehicle->m_collisionImpulse) / fullCollision);
    float cutoff = Rate(0xF6981837);
    if (!(strength > cutoff))
        return;
    m_collideSound->AddFloat(0x9E497FC6, static_cast<float>(strength * kPercent));
    PlaySoundFromStructure(mp_sound, m_collideSound);
    m_collideTime = Now(Tmr::GameClockElapsed());

    // The skater told how hard (0-1). (Out of memory, the original adds to a null structure - kept.)
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* event = memory ? memory->Init() : nullptr;
    float level = strength > 1.0f ? 1.0f : (strength < 0.0f ? 0.0f : strength);
    event->AddFloat(0x29461E27, level);
    mp_vehicle->mp_skater->SelfEvent(0xC4A60423, event);
    if (event) {
        event->Clear();
        Script::DeleteStruct(event);
    }
}

} // namespace Obj

REPLACE(0x0043CFC0, Obj::CVehicleSoundComponent::Create);
REPLACE(0x0043C590, &Obj::CVehicleSoundComponent::Construct);
REPLACE(0x0043C5F0, &Obj::CVehicleSoundComponent::DestroyBody);
REPLACE(0x0043D020, &Obj::CVehicleSoundComponent::Destroy);
REPLACE(0x0043C670, &Obj::CVehicleSoundComponent::InitFromStructure);
REPLACE(0x0043D050, &Obj::CVehicleSoundComponent::Finalize);
REPLACE(0x0043D290, &Obj::CVehicleSoundComponent::Update);
REPLACE(0x0043C540, &Obj::CVehicleSoundComponent::GetDebugInfo);
REPLACE(0x0043CE70, &Obj::CVehicleSoundComponent::ReadSettings);
REPLACE(0x0043C690, &Obj::CVehicleSoundComponent::UpdateEngine);
REPLACE(0x0043CBC0, &Obj::CVehicleSoundComponent::UpdateSkid);
REPLACE(0x0043D090, &Obj::CVehicleSoundComponent::UpdateCollideSound);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x006467F4,
    &Obj::CVehicleSoundComponent::Destroy,
    &Obj::CVehicleSoundComponent::Update,
    &Obj::CVehicleSoundComponent::InitFromStructure,
    &Obj::CVehicleSoundComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2);

} // namespace
} // namespace Obj
