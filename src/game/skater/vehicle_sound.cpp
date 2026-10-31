// The skater's vehicle sound component: its engine and skid sounds. See vehicle_sound.h.

#include "game/skater/vehicle_sound.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/ed/park_editor.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

uint32_t g_vehicleSoundMemberFunctions[24]{
            0xB8451886, 0x1859F772, 0, 0, 0, 0,
            0x0F6E8120, 0x1859F772, 0, 0, 0, 0,
            0xF876FAE9, 0x1859F772, 0, 0, 0, 0,
            0, 0x1859F772, 0, 0, 0, 0};

namespace {

extern void** const kVtable;

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kSound = 0x07713C7B;
constexpr uint32_t kInput = 0x27D7CD28;

constexpr uint32_t kRates = 0x38EC37AC;       // the global structure of rates and limits (and the park sounds)
constexpr uint32_t kAllSettings = 0x3C2DA7F5; // the global structure of vehicles' settings
constexpr uint32_t kParkEditorOff = 0xD443A2BC;

const float kSpeedScale = std::bit_cast<float>(0x40400000u); // 0x006467A8: 3
const float kToMph = std::bit_cast<float>(0x3D68BA2Fu);      // 0x00646828: 1 / 17.6
const double kTwoTo32 = 4294967296.0;                        // 0x006453B4 (an unsigned clock difference)

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

Sfx::CSfxManager* SfxManager() { return Sfx::g_sfxManager; }

// The game clock (ms) with its offset, as the original reads them: the clock, then the offset.
int Clock() { return Tmr::GameClockElapsed(); }
int ClockOffset() { return g_clockOffset; }

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

// The object's speed as the original computes it (x, y, then z), unrounded, times 3.
double Speed(const Mth::Vector& vel)
{
    double x = vel.x, y = vel.y, z = vel.z;
    return std::sqrt((x * x + y * y) + z * z) * kSpeedScale;
}

// The clock now (with its offset) less `then`, wrapping as the original's 32-bit sums do.
int Since(int clock, int offset, int then)
{
    return static_cast<int>(static_cast<uint32_t>(clock) + (static_cast<uint32_t>(offset) - static_cast<uint32_t>(then)));
}
int Now(int clock, int offset) { return static_cast<int>(static_cast<uint32_t>(clock) + static_cast<uint32_t>(offset)); }

// An unsigned millisecond difference as a double (the original's FILD and its 2^32 fix for negatives).
double Unsigned(int difference) { return difference < 0 ? difference + kTwoTo32 : difference; }

// A fraction kept to [0, 1] as both sounds do it: over 1 -> 1 (compared unrounded); then, rounded, under 0 -> 0.
float Fraction(double value)
{
    if (value > 1.0)
        return 1.0f;
    float rounded = static_cast<float>(value);
    return rounded < 0.0f ? 0.0f : rounded;
}

} // namespace

// 0x0051ADC0
int ComponentWithoutMemberFunctions::CallMemberFunction(uint32_t, Script::CStruct*, Script::CScript*) { return 2; }

// 0x0051B880
CBaseComponent* __cdecl CSkaterVehicleSoundComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterVehicleSoundComponent));
    return memory ? static_cast<CSkaterVehicleSoundComponent*>(memory)->Construct() : nullptr;
}

// 0x0051AD90
void __cdecl CSkaterVehicleSoundComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_vehicleSoundMemberFunctions, 0x18);
}

// 0x0051ADD0
CSkaterVehicleSoundComponent* CSkaterVehicleSoundComponent::Construct()
{
    CBaseComponent::Construct();
    m_sound = nullptr;
    m_settings = nullptr;
    m_parkEditorLevel = false;
    m_speed = 0.0f;
    m_gear = 0;
    m_shiftTime = 0;
    m_unknownB4 = 0;
    m_skid = 0.0f;
    m_unknownBC = 0;
    m_engineSound = 0;
    m_skidSound = 0;
    m_vtable = kVtable;
    m_type = kType;
    m_settingsName = 0x1CA1FF20;
    m_silenced = true;
    return this;
}

// 0x0051ADB0
void CSkaterVehicleSoundComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0051AE30
CSkaterVehicleSoundComponent* CSkaterVehicleSoundComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0051AE60
void CSkaterVehicleSoundComponent::InitFromStructure(Script::CStruct*)
{
    m_parkEditorLevel = Field<uint32_t>(static_cast<void*>(Ed::g_parkEditor), 0x40) != kParkEditorOff;
}

// 0x0051B8E0
void CSkaterVehicleSoundComponent::Finalize()
{
    m_corePhysics = m_object->GetComponent(kSkaterCorePhysics);
    m_sound = m_object->GetComponent(kSound);
    m_input = reinterpret_cast<uint8_t*>(m_object->GetComponent(kInput));
    ReadSettings();
}

// 0x0051B920
void CSkaterVehicleSoundComponent::Update()
{
    if (!Field<bool>(m_corePhysics, 0x2ED) || m_silenced || !m_settings)
        return;
    UpdateEngine();
    UpdateSkid();
    UpdateUnused();
}

// 0x0051B670
void CSkaterVehicleSoundComponent::ReadSettings()
{
    Script::GetStructure(kAllSettings)->GetStructure(m_settingsName, &m_settings, 1);
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
    m_settings->GetStructure(0xBF8E0ACE, &m_extra, 1);
    if (m_parkEditorLevel) {
        Script::CStruct* park = Script::GetStructure(kRates);
        park->GetChecksum(0xF5406E1A, &m_engineSoundName, 1);
        park->GetChecksum(0x060E93A2, &m_skidSoundName, 1);
        park->GetStructure(0x22995285, &m_extra, 1);
    }
}

// 0x0051AE80
void CSkaterVehicleSoundComponent::UpdateEngine()
{
    float frame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    float speed = static_cast<float>(Speed(m_object->m_vel) * kToMph);
    if (!Field<bool>(m_input, 0x1C0) || Field<bool>(m_input, 0x40))
        speed = static_cast<float>(Rate(0xEC67BC97) * static_cast<double>(speed));

    // The speed eased towards the object's, each step limited by the rates (as the original compares them, a step
    // the wrong way is kept and one the right way isn't).
    double follow = Rate(0x02A7095B);
    double eased = (static_cast<double>(m_speed) - speed) * (follow * frame) + speed;
    m_speed = static_cast<float>(eased);
    if (eased < speed) {
        double step = static_cast<double>(m_speed) + static_cast<double>(Rate(0xCA5EFD91)) * frame;
        if (!(step > m_speed))
            m_speed = static_cast<float>(step);
    } else {
        double step = static_cast<double>(m_speed) - static_cast<double>(Rate(0x77C8E7C8)) * frame;
        if (!(step < m_speed))
            m_speed = static_cast<float>(step);
    }

    // The gear: back to the first a while after driving stopped, else shifted at the gear's speeds.
    if (Field<int>(Field<uint8_t*>(m_corePhysics, 0x1528), 0x18) == 0) {
        int lastDriven = Field<int>(m_corePhysics, 0x380);
        float delay = Rate(0xC0F451D4);
        int clock = Clock();
        if (Unsigned(Since(clock, ClockOffset(), lastDriven)) > delay)
            m_gear = 0;
    } else {
        int gear = m_gear;
        if (gear < m_gearCount - 1 && m_speed > m_gears[gear].upAt) {
            m_gear = gear + 1;
            m_shiftTime = Now(Clock(), ClockOffset());
        } else if (gear > 0 && m_speed < m_gears[gear].downAt) {
            m_gear = gear - 1;
            m_shiftTime = Now(Clock(), ClockOffset());
        }
    }

    // Just after a shift the engine is quieter (from 0xBFA32092 of full, back to full over 0xFB6BD20E ms).
    float shiftTime = Rate(0xFB6BD20E);
    int shifted = m_shiftTime;
    int clock = Clock();
    int sinceShift = Since(clock, ClockOffset(), shifted);
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
        m_engineSound = sfx->PlaySound(m_engineSoundName, &volume, pitch, 0, nullptr, 0);
    else
        sfx->SetSoundParamsScaled(m_engineSound, &volume, pitch);
    // (sVolume's destructor is empty: 0x005480A0)
}

// 0x0051B3E0
void CSkaterVehicleSoundComponent::UpdateSkid()
{
    float skid = 0.0f;
    if (Field<bool>(m_input, 0x40) && Field<int>(Field<uint8_t*>(m_corePhysics, 0x1528), 0x18) == 0) {
        double speed = Speed(m_object->m_vel);
        if (!(0.0 > speed))
            skid = static_cast<float>(speed);
    }
    float rate = Rate(0x95FBE774);
    double ease = Tmr::GetSlomoFrameSeconds() * rate;
    m_skid = static_cast<float>((static_cast<double>(skid) - m_skid) * ease + m_skid);
    float loudestAt = Setting(m_settings, 0x28F1AAD0);
    float level = Fraction(static_cast<double>(m_skid) / loudestAt);

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
            m_skidSound = sfx->PlaySound(m_skidSoundName, &volume, pitch, 0, nullptr, 0);
        else
            sfx->SetSoundParamsScaled(m_skidSound, &volume, pitch);
    } else if (m_skidSound) {
        sfx->StopSound(m_skidSound);
        m_skidSound = 0;
    }
}

// 0x0051B3B0
void CSkaterVehicleSoundComponent::UpdateUnused()
{
    Rate(0x71B4B9FE);
    Clock();
}

// 0x0051B7C0
bool CSkaterVehicleSoundComponent::SetSkaterVehicleSounds(Script::CStruct* params, Script::CScript*)
{
    params->GetChecksum(0u, &m_settingsName, 0);
    if (m_engineSound)
        SfxManager()->StopSound(m_engineSound);
    m_engineSound = 0;
    m_engineSoundName = 0;
    m_skidSoundName = 0;
    m_gearCount = 0;
    m_extra = nullptr;
    ReadSettings();
    m_silenced = false;
    return true;
}

// 0x0051B820
bool CSkaterVehicleSoundComponent::NoSkaterVehicleSounds(Script::CStruct*, Script::CScript*)
{
    if (m_engineSound)
        SfxManager()->StopSound(m_engineSound);
    m_engineSound = 0;
    m_engineSoundName = 0;
    m_skidSoundName = 0;
    m_gearCount = 0;
    m_extra = nullptr;
    m_silenced = true;
    return true;
}

// 0x0051B860
bool CSkaterVehicleSoundComponent::StopSkaterVehicleSounds(Script::CStruct*, Script::CScript*)
{
    if (m_engineSound)
        SfxManager()->StopSound(m_engineSound);
    return true;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CSkaterVehicleSoundComponent::Destroy,
         &Obj::CSkaterVehicleSoundComponent::Update,
         &Obj::CSkaterVehicleSoundComponent::InitFromStructure,
         &Obj::CSkaterVehicleSoundComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &Obj::CBaseComponent::RunMemberFunction,
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
