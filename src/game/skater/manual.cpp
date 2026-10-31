// The balance of a balance trick, 0x00579850-0x0057A177. See manual.h.

#include "game/skater/manual.h"

#include <cmath>
#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/mth/math.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/stats_manager.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/skate/score.h"
#include "game/skater/balance_trick.h"
#include "game/core/code_address.h"

namespace Obj {

namespace {

// Names.
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3;
constexpr uint32_t kSkaterScore = 0xD262802F;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kStatsManager = 0x4FF8DAC6;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kResetBalanceIndicator = 0x18F77EA1; // reset_balance_indicator (a script)
constexpr uint32_t kShowBalanceIndicator = 0x5D07E2BE;  // (a script integer)
constexpr uint32_t kGrind = 0x255ED86F;
constexpr uint32_t kSlide = 0x8D10119D;
constexpr uint32_t kNosemanual = 0x0AC90769;
constexpr uint32_t kManual = 0xEF24413B;
constexpr uint32_t kUp = 0xBC6B118F;
constexpr uint32_t kDown = 0xE3006FC4;
constexpr uint32_t kOffMeterTop = 0xD77B6FF9;
constexpr uint32_t kOffMeterBottom = 0x47D44B84;
// The physics values (the balance trick component's, 0x004F90A0) and script globals.
constexpr uint32_t kCheese = 0x1231AE3C;
constexpr uint32_t kCheeseFrames = 0xA4362662;
constexpr uint32_t kSameGrindAddTime = 0xFBEC5B6C;
constexpr uint32_t kNewGrindSubTime = 0xC5E1C8EB;
constexpr uint32_t kRobotRailAddTime = 0xF54CA12F;
constexpr uint32_t kRobotRailNudge = 0x30990680;
constexpr uint32_t kRepeatMin = 0x8F4DB5D3;
constexpr uint32_t kRepeatMultiplier = 0x93DD6A10;
constexpr uint32_t kLeanRepeatMultiplier = 0x6CE10EA4;
constexpr uint32_t kLeanBailAngle = 0xF81AB0E9;
constexpr uint32_t kLeanGravityStat = 0x06851748;
constexpr uint32_t kInstableRate = 0xA9FEF5AB;
constexpr uint32_t kInstableBase = 0xB6A634F3;
constexpr uint32_t kLeanAcc = 0x61AD5596;
constexpr uint32_t kLeanMinSpeed = 0xEF27E8FF;
constexpr uint32_t kLeanRndSpeed = 0x32EB3308;
constexpr uint32_t kBalanceSafeButtonPeriod = 0x8CC8F176;
constexpr uint32_t kBalanceIgnoreButtonPeriod = 0x07DA1621;

extern void** const kVtable;

constexpr double kFrames = 60.0; // 0x00645EA0 (frames a second)

uint32_t Clock() { return static_cast<uint32_t>(Tmr::GameClockElapsed() + g_clockOffset); }
// A physics value (0x004F90A0 on the balance trick component).
double Physics(uint8_t* balanceTrick, uint32_t name)
{
    return reinterpret_cast<CSkaterBalanceTrickComponent*>(balanceTrick)->GetPhysicsValue(name);
}
uint32_t& TrickType(uint8_t* balanceTrick) { return *reinterpret_cast<uint32_t*>(balanceTrick + 0x18); }
void* ScoreDisplay(uint8_t* score) { return *reinterpret_cast<void**>(score + 0x18); }
// The score's two balance displays (0x00563370 / 0x00563390): on or off, and the lean shown.
void ShowLean(void* score, int on, float lean) { static_cast<Mdl::Score*>(score)->SetBalanceMeter(on != 0, lean); }
void ShowLean2(void* score, int on, float lean) { static_cast<Mdl::Score*>(score)->SetManualMeter(on != 0, lean); }
// A button of the input component's control data held (its first byte).
bool Held(uint8_t* input, uint32_t button)
{
    return reinterpret_cast<CControlPad*>(input + 0x20)->GetButton(button)->m_pressed != 0;
}

void ResetState(CBalance* b)
{
    b->m_cheese = 0.0f;
    b->m_time = 0.0f;
    b->m_railTime = 0.0f;
    b->m_lean = 0.0f;
    b->m_leanSpeed = 0.0f;
    b->m_pushed = false;
    b->m_showMeter = false;
    b->m_rail = -1;
}

} // namespace

// 0x00579850
void CBalance::DestroyBody() { m_vtable = kVtable; }

// 0x00579860
void CBalance::Init(CCompositeObject* skater)
{
    m_skater = skater;
    m_balanceTrick = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterBalanceTrick));
    m_score = reinterpret_cast<uint8_t*>(m_skater->GetComponent(kSkaterScore));
    m_input = reinterpret_cast<uint8_t*>(m_skater->GetComponent(kInput));
}

// 0x005798A0
void CBalance::Reset()
{
    if (Script::GetInteger(kShowBalanceIndicator))
        Script::RunScript(kResetBalanceIndicator, nullptr, nullptr, false, nullptr);
    ResetState(this);
}

// 0x005798F0
void CBalance::ClearBestTime() { m_bestTime = 0.0f; }
// 0x00579900
void CBalance::HideDisplay() { m_hideDisplay = true; }
// 0x00579910
void CBalance::ShowDisplay() { m_hideDisplay = false; }

// 0x00579920
void CBalance::KeepBestTime()
{
    if (m_time > m_bestTime)
        m_bestTime = m_time;
    m_time = 0.0f;
}

// 0x00579940
void CBalance::ShowMeter() { m_showMeter = true; }

// 0x00579950
CBalance* CBalance::Construct()
{
    m_vtable = kVtable;
    if (Script::GetInteger(kShowBalanceIndicator))
        Script::RunScript(kResetBalanceIndicator, nullptr, nullptr, false, nullptr);
    ResetState(this);
    return this;
}

// 0x005799A0
CBalance* CBalance::Destroy(unsigned int flags)
{
    m_vtable = kVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005799D0
void CBalance::Stop()
{
    if (ScoreDisplay(m_score)) {
        ShowLean(ScoreDisplay(m_score), 0, 0.0f);
        ShowLean2(ScoreDisplay(m_score), 0, 0.0f);
    }
    m_leftButton = 0;
    m_rightButton = 0;
}

// 0x00579A00
void CBalance::Start(uint32_t leftButton, uint32_t rightButton, uint32_t trick, bool flipped, bool flip)
{
    m_leftButton = leftButton;
    m_rightButton = rightButton;
    m_trick = trick;
    m_time = 0.0f;
    m_pushed = false;
    m_flipped = flipped;
    m_flip = flip;
    uint32_t type = TrickType(m_balanceTrick);
    if (type == kGrind || type == kSlide) {
        uint8_t* core = reinterpret_cast<uint8_t*>(m_skater->GetComponent(kSkaterCorePhysics));
        void* rail = *reinterpret_cast<void**>(core + 0x22C);
        if (m_rail != -1) {
            if (static_cast<CRailNode*>(rail)->IsNamed(static_cast<uint32_t>(m_rail))) { // the same rail
                m_railTime = static_cast<float>(Physics(m_balanceTrick, kSameGrindAddTime) + m_railTime);
            } else {
                double time = m_railTime - Physics(m_balanceTrick, kNewGrindSubTime);
                m_railTime = static_cast<float>(time);
                if (time < 0.0)
                    m_railTime = 0.0f;
                m_cheese = 0.0f;
            }
        }
        m_rail = *reinterpret_cast<int*>(static_cast<uint8_t*>(rail) + 0x4C);
        // A robot rail: a short run (under half a second) gets time and a nudge.
        float runTime = *reinterpret_cast<float*>(static_cast<uint8_t*>(ScoreDisplay(m_score)) + 0x154C);
        if (runTime < 0.5f) {
            float inverse = static_cast<float>(1.0 / runTime);
            m_railTime = static_cast<float>(Script::GetFloat(kRobotRailAddTime) * static_cast<double>(inverse) * 0.5 +
                                            m_railTime);
            float side = -1.0f;
            if (!(m_leanSpeed < 0.0f))
                side = 1.0f;
            m_lean = static_cast<float>(Script::GetFloat(kRobotRailNudge) * static_cast<double>(inverse) * side * 0.5 +
                                        m_lean);
        }
    }
    float repeatMin = static_cast<float>(Physics(m_balanceTrick, kRepeatMin));
    if (std::fabs(m_leanSpeed) != 0.0f) {
        double speed = Physics(m_balanceTrick, kRepeatMultiplier) * m_leanSpeed;
        m_leanSpeed = static_cast<float>(speed);
        if (std::fabs(speed) < repeatMin)
            m_leanSpeed = static_cast<float>((speed < 0.0 ? -1.0 : 1.0) * repeatMin);
    } else {
        m_leanSpeed = repeatMin;
        uint32_t balance = TrickType(m_balanceTrick);
        if (balance == kNosemanual)
            m_leanSpeed = -repeatMin;
        else if (balance != kManual && Mth::Rnd(2))
            m_leanSpeed = -m_leanSpeed;
    }
    double lean = Physics(m_balanceTrick, kLeanRepeatMultiplier) * m_lean;
    m_lean = static_cast<float>(lean);
    m_lean = static_cast<float>((lean < 0.0 ? -1.0 : 1.0) * m_cheese + lean);
    double bail = Physics(m_balanceTrick, kLeanBailAngle);
    if (std::fabs(m_lean) > bail) {
        // Kept just inside the bail angle, leaning the way it speeds.
        float limit = static_cast<float>(0.9f * bail);
        m_lean = limit;
        double side = m_leanSpeed < 0.0f ? -1.0 : 1.0;
        m_lean = static_cast<float>(std::fabs(limit) * side);
        side = m_leanSpeed < 0.0f ? -1.0 : 1.0;
        m_leanSpeed = static_cast<float>(side * bail);
    }
    m_cheese = static_cast<float>(Physics(m_balanceTrick, kCheese));
    m_showMeter = false;
    m_startTime = Clock();
}

// 0x00579CC0
void CBalance::Update()
{
    float frame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    if (!m_leftButton || !m_rightButton)
        return;
    auto* net = GameNet::g_gameNetManager;
    if (net->m_server) // (the folded getter 0x005480C0)
        net->PlayerByObjectId(static_cast<uint16_t>(m_skater->m_id)); // (its result unused)
    // The cheese wears off.
    if (m_cheese > 0.01f) {
        float cheese = static_cast<float>(Physics(m_balanceTrick, kCheese));
        double wear = cheese / Physics(m_balanceTrick, kCheeseFrames) * frame * kFrames;
        double left = m_cheese - wear;
        m_cheese = static_cast<float>(left);
        if (left < 0.0)
            m_cheese = 0.0f;
    }
    static_cast<Mdl::Score*>(ScoreDisplay(m_score))->TweakTrick(static_cast<int>(m_trick));
    m_time = static_cast<float>(static_cast<double>(frame) + m_time);
    m_railTime = static_cast<float>(static_cast<double>(frame) + m_railTime);
    CBaseComponent* stats = m_skater->GetComponent(kStatsManager);
    static_cast<CStatsManagerComponent*>(stats)->CheckBalanceTime(TrickType(m_balanceTrick), m_time, 0);
    // The lean speeds up.
    float gravity = static_cast<float>(Physics(m_balanceTrick, kLeanGravityStat));
    float instableRate = static_cast<float>(Physics(m_balanceTrick, kInstableRate));
    double instable = Physics(m_balanceTrick, kInstableBase) + instableRate * static_cast<double>(m_railTime);
    double grown = (gravity * instable * frame * kFrames + 1.0) * m_lean;
    m_lean = static_cast<float>(grown + instable * m_leanSpeed * frame * kFrames);
    bool left = Held(m_input, m_leftButton);
    bool right = Held(m_input, m_rightButton);
    if (!left && !right)
        m_pushed = true;
    uint32_t safePeriod = static_cast<uint32_t>(Script::GetInteger(kBalanceSafeButtonPeriod));
    uint32_t elapsed = Clock() - m_startTime;
    if (!m_pushed && elapsed > static_cast<uint32_t>(Script::GetInteger(kBalanceIgnoreButtonPeriod)))
        m_pushed = true;
    if (left && m_pushed) {
        // Pushing back - not at all before the safe period while leaning the other way (the original's ratio is an
        // integer division: 0 then).
        float scale = 1.0f;
        if (m_lean < 0.0f && elapsed < safePeriod)
            scale = static_cast<float>(elapsed / safePeriod);
        m_leanSpeed = static_cast<float>(m_leanSpeed - Physics(m_balanceTrick, kLeanAcc) * scale * frame * kFrames);
    } else if (right && m_pushed) {
        float scale = 1.0f;
        if (m_lean > 0.0f && elapsed < safePeriod)
            scale = static_cast<float>(elapsed / safePeriod);
        m_leanSpeed = static_cast<float>(Physics(m_balanceTrick, kLeanAcc) * scale * frame * kFrames + m_leanSpeed);
    } else {
        // No button: a random shake.
        float speed = std::fabs(m_leanSpeed);
        if (Physics(m_balanceTrick, kLeanMinSpeed) > speed) {
            int range = static_cast<int>(Physics(m_balanceTrick, kLeanRndSpeed)) | 1;
            float shake = static_cast<float>(Mth::Rnd(range));
            m_leanSpeed = static_cast<float>(Mth::Sign(m_leanSpeed) * shake);
        } else {
            float shake = static_cast<float>(Mth::Rnd(50));
            m_leanSpeed = static_cast<float>(Mth::Sign(m_leanSpeed) * shake * 0.01f + m_leanSpeed);
        }
    }
    if (!m_skater->m_script) {
        void* memory = Script::CScript::operator new(0xF4);
        m_skater->m_script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    // Past the bail angle: the skater falls.
    float lean = std::fabs(m_lean);
    if (Physics(m_balanceTrick, kLeanBailAngle) < lean) {
        m_skater->SelfEvent(m_lean > 0.0f ? kOffMeterTop : kOffMeterBottom, nullptr);
        m_lean = 0.0f;
        TrickType(m_balanceTrick) = 0;
    }
    // The balance meter (the anim blender's).
    if (m_showMeter) {
        bool flip = m_flip;
        if (m_flipped) {
            uint8_t* core = reinterpret_cast<uint8_t*>(m_skater->GetComponent(kSkaterCorePhysics));
            if (*(*reinterpret_cast<uint8_t**>(core + 0x1528) + 0x28))
                flip = !flip;
        }
        if (CBaseComponent* blender = m_skater->GetComponent(kAnimBlender)) {
            double position = flip ? 4096.0 - m_lean : m_lean + 4096.0;
            static_cast<CAnimBlenderComponent*>(blender)->FirstChannel47E280(
                static_cast<float>(position * 0.0001220703125f), 1);
        }
    }
    float shown = static_cast<float>(m_lean * -0.000244140625f);
    if (m_hideDisplay || !TrickType(m_balanceTrick))
        return;
    bool upDown = (m_leftButton == kUp || m_leftButton == kDown) && (m_rightButton == kUp || m_rightButton == kDown);
    void* score = ScoreDisplay(m_score);
    if (!score)
        return;
    if (upDown)
        ShowLean2(score, 1, shown);
    else
        ShowLean(score, 1, shown);
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CBalance::Destroy};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
