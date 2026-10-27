// The CameraLookAround component (Obj::CCameraLookAroundComponent): see camera_lookaround.h. From the
// disassembly of 0x004E3F60-0x004E4336 (0x004E3FA0 isn't a Ghidra function).

#include "game/obj/camera_lookaround.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/control_pad.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064B28C);
constexpr uint32_t kCameraLookAround = 0x81C7122D;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kTurnTo = 0xB8F23899, kStopTurning = 0xE3B327C0;
constexpr uint32_t kLookaroundOnDPadOn = 0xDB02EE92, kLookaroundOnDPadOff = 0x72D2BEE3; // (THAW's members)
constexpr uint32_t kHeading = 0xFD4BC03E, kTilt = 0xE3C07609, kTime = 0x906B67BA, kFactor = 0x48D4868B;
constexpr uint32_t kSecondPad = 0x702247A5, kNoButton = 0xF3E055E1; // script globals
constexpr uint32_t kSpecialObject = 0x967C138C;
constexpr uint32_t kSpecialScript = 0x60871393, kScript = 0xA1B1146D;

constexpr float kUpScale = 0.699999988079071f;     // 0x0064B2C8
constexpr float kDownScale = 0.5f;                  // 0x006453BC
constexpr float kTiltScale = -1.399999976158142f;   // 0x0064B2C4
constexpr float kHeadingScale = 3.0f;               // 0x006467A8
constexpr float kNear = 0.0010000000474974513f;    // 0x006461D8
constexpr float kEase = 3.75f;                     // 0x0064B2C0

// A button's press this frame forgotten.
void TakePress(uint8_t* button) { reinterpret_cast<CControlButton*>(button)->ClearTrigger(); }

// One angle eased toward `target` (the original's x87 sequence: the difference rounded, its size unrounded).
void Ease(float& angle, double target, float frame)
{
    double difference = target - angle;
    float rounded = static_cast<float>(difference);
    if (std::fabs(difference) > kNear)
        angle = static_cast<float>(static_cast<double>(rounded) * frame * kEase + angle);
    else
        angle = static_cast<float>(target);
}

} // namespace

// 0x004E42E0
CBaseComponent* __cdecl CCameraLookAroundComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CCameraLookAroundComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CCameraLookAroundComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kCameraLookAround;
    return component;
}

// 0x004E3F60
void CCameraLookAroundComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x004E4010
CCameraLookAroundComponent* CCameraLookAroundComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004E3F70
void CCameraLookAroundComponent::InitFromStructure(Script::CStruct* /*params*/)
{
    m_tilt = 0.0f;
    m_heading = 0.0f;
    m_factor = 1.0f;
    m_locked = false;
    m_turning = false;
}

// 0x004E4040
void CCameraLookAroundComponent::Finalize()
{
    mp_input = reinterpret_cast<uint8_t*>(m_object->GetComponent(kInput));
}

// 0x004E4000
void CCameraLookAroundComponent::SetLocked(bool locked)
{
    m_locked = locked;
}

// 0x004E3FA0
void CCameraLookAroundComponent::TurnTo(float heading, float tilt, float time, float factor)
{
    m_targetHeading = heading;
    m_turning = true;
    m_targetTilt = tilt;
    if (time > 0.0f) {
        double inverse = 1.0 / time;
        m_headingRate = static_cast<float>((static_cast<double>(heading) - m_heading) * inverse);
        m_tiltRate = static_cast<float>((static_cast<double>(tilt) - m_tilt) * inverse);
    }
    m_timeLeft = time;
    m_factor = factor;
}

// 0x004E4060
void CCameraLookAroundComponent::Update()
{
    uint8_t* pad = mp_input + (Script::GetInteger(kSecondPad) ? 0x258 : 0x20);
    if (!Script::GetInteger(kNoButton) && pad[0x1F0]) {
        TakePress(pad + 0x1E0);
        Script::RunScript(m_object->m_id == kSpecialObject ? kSpecialScript : kScript, nullptr, nullptr, false,
                          nullptr);
    }
    if (pad[0x130]) {
        m_locked = !m_locked;
        TakePress(pad + 0x120);
    }
    float frame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    if (!m_locked && !m_turning && m_dpadLook && Skater::ThawBmx::Enabled()) {
        // NOT ORIGINAL - THAW's BMX (THAW 0x5B08F0 with its +0x40 set): the d-pad's inputs instead of
        // the right stick's - with THAW's default inversion -, eased as the stick's (the heading without the stick's
        // x 3). Original behaviour: the stick's, below, always.
        auto* p = reinterpret_cast<CControlPad*>(pad);
        Skater::ThawBmx::LookInputs in =
            Skater::ThawBmx::DPadLookInputs(p->m_up.m_pressed != 0, p->m_down.m_pressed != 0, p->m_left.m_pressed != 0,
                                            p->m_right.m_pressed != 0, Skater::ThawBmx::kLookInvertDefault);
        float tilt = in.tilt * (in.tilt < 0.0f ? kUpScale : kDownScale) * kTiltScale;
        Ease(m_tilt, tilt, frame);
        Ease(m_heading, in.heading, frame);
    } else if (!m_locked && !m_turning) {
        float stickY = *reinterpret_cast<float*>(pad + 0x214);
        double tilt = static_cast<double>(stickY) * (stickY < 0.0f ? kUpScale : kDownScale) * kTiltScale;
        Ease(m_tilt, tilt, frame);
        double heading = static_cast<double>(*reinterpret_cast<float*>(pad + 0x210)) * kHeadingScale;
        Ease(m_heading, heading, frame);
    }
    float step = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    if (m_turning && m_timeLeft > 0.0f) {
        if (m_timeLeft > step) {
            m_heading = static_cast<float>(static_cast<double>(step) * m_headingRate + m_heading);
            m_tilt = static_cast<float>(static_cast<double>(step) * m_tiltRate + m_tilt);
            m_timeLeft = m_timeLeft - step;
        } else {
            m_heading = m_targetHeading;
            m_tilt = m_targetTilt;
            m_timeLeft = 0.0f;
        }
    }
}

// 0x004E4220
int CCameraLookAroundComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* /*script*/)
{
    if (name == kTurnTo) {
        float heading, tilt, time, factor = 1.0f;
        params->GetFloat(kHeading, &heading, 1);
        params->GetFloat(kTilt, &tilt, 1);
        params->GetFloat(kTime, &time, 1);
        params->GetFloat(kFactor, &factor, 0);
        TurnTo(heading, tilt, time, factor);
        return 1;
    }
    if (name == kStopTurning) {
        m_turning = false;
        return 1;
    }
    // NOT ORIGINAL - THAW's BMX: THAW's members (its 0x5B0B50 / 0x5B0B60: +0x40 on / off), bike_start's and
    // bmx_finish_off's - only while the feature is on (else 2, as THUG2's).
    if ((name == kLookaroundOnDPadOn || name == kLookaroundOnDPadOff) && Skater::ThawBmx::Enabled()) {
        m_dpadLook = name == kLookaroundOnDPadOn;
        Skater::ThawBmx::NoteDPadCamera(m_dpadLook);
        return 1;
    }
    return 2;
}

namespace {

// 0x004E3F90: a tail jump to the component's slot 2 (its arguments left as they are).
__declspec(naked) void RefreshFromStructure()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 8]
    }
}

} // namespace

} // namespace Obj

REPLACE(0x004E42E0, Obj::CCameraLookAroundComponent::Create);
REPLACE(0x004E3F60, &Obj::CCameraLookAroundComponent::DestroyBody);
REPLACE(0x004E4010, &Obj::CCameraLookAroundComponent::Destroy);
REPLACE(0x004E3F70, &Obj::CCameraLookAroundComponent::InitFromStructure);
REPLACE(0x004E4040, &Obj::CCameraLookAroundComponent::Finalize);
REPLACE(0x004E4000, &Obj::CCameraLookAroundComponent::SetLocked);
REPLACE(0x004E3FA0, &Obj::CCameraLookAroundComponent::TurnTo);
REPLACE(0x004E4060, &Obj::CCameraLookAroundComponent::Update);
REPLACE(0x004E4220, &Obj::CCameraLookAroundComponent::CallMemberFunction);
REPLACE_UNCHECKED(0x004E3F90, Obj::RefreshFromStructure); // (a tail jump: the arguments are slot 2's)
