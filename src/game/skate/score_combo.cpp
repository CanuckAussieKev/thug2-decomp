// Mdl::Score's combo: tricks logged and scored, the multiplier, the special meter, landing and bailing, the balance
// meter (see score.h).

#include "game/skate/score.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/sprite_element.h"
#include "game/goals/goal_manager.h"
#include "game/mth/x87_math.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/anim_blender.h"
#include "game/script/cscript.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score_internal.h"
#include "game/skater/run_timer.h"
#include "game/skater/stats_manager.h"
#include "game/skater/trick_component.h"
#include "game/sys/timer.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace Mdl {

using namespace ScoreInternal;

namespace {

constexpr uint32_t kTagTurnedOn = 0xC22EB9A0;        // tag_turned_on
constexpr uint32_t kId = 0x40C698AF;                 // id
constexpr uint32_t kShowBalanceMeter = 0xBA95DA16;   // show_balance_meter
constexpr uint32_t kHideBalanceMeter = 0x58783B40;   // hide_balance_meter
constexpr uint32_t kTagMode = 0x863A07E2;            // tag_mode
constexpr uint32_t kManual = 0xEF24413B;             // Manual
constexpr uint32_t kBalance = 0x530BE001;            // balance
constexpr uint32_t kDebugScore = 0x1EDC33F1;         // (name unknown: the points printed)
constexpr uint32_t kFocusTimeLimit = 0x59B9A85E;     // focus_time_limit
constexpr uint32_t kFocusMaxTimesInCombo = 0xB33D71C0; // focus_max_times_in_combo
constexpr uint32_t kTrickMultDegradeStart = 0xFD4625CD; // trick_mult_degrade_start
constexpr uint32_t kPhysicsRunTimerDuration = 0x0B84F532; // Physics_RunTimer_Duration
constexpr uint32_t kSkaterEnterCombo = 0x670FDA8C;   // SkaterEnterCombo
constexpr uint32_t kSpinCountSlop = 0x50C5CC2F;      // spin_count_slop
constexpr uint32_t kOllie = 0x9B65D7B8;              // ollie
constexpr uint32_t kSkaterSpinDisplayed = 0x68B887BB; // SkaterSpinDisplayed
constexpr uint32_t kNetLobby = 0x1C471C60;           // NetLobby
constexpr uint32_t kNetFirefight = 0xBFF33600;       // netfirefight
constexpr uint32_t kFirefight = 0x3D6D444F;          // firefight
// The trick names a spin turns into a Cab: Fakie\_Ollie and two more (names unknown).
constexpr uint32_t kFakieOllie = 0x28C6399C;
constexpr uint32_t kCabTrick2 = 0x8BF1B0CB;
constexpr uint32_t kCabTrick3 = 0x428485BB;

constexpr uint8_t kMsgBestCombo = 0x86;  // the best combo to the server
constexpr uint8_t kMsgLandedScore = 0x52; // a landed combo's points to the server
constexpr uint8_t kGroupGame = 8;

// The message both send: the game id and the points.
struct PointsMessage {
    uint8_t gameId;
    uint8_t pad01[3];
    int points;
};

// The tricks since the last no-spin one (index 0 not looked at).
int TricksSinceNoSpin(Score* score)
{
    int since = 0;
    for (int i = score->m_tricks - 1; i > 0; --i) {
        if (score->Trick(i)->m_flags & kTrickNoSpin)
            break;
        ++since;
    }
    return since;
}

// The skater's adjust-physics component holding the tricks' points (its +0x1C).
bool PointsHeld(Obj::CSkater* skater)
{
    auto* adjust = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterAdjustPhysics));
    return adjust && adjust[0x1C];
}

// `points` to the server (message `id`, the client of the skater's viewport).
void SendPoints(GameNet::Manager* net, Obj::CSkater* skater, uint8_t id, int points)
{
    Net::MsgDesc desc;
    desc.Construct();
    PointsMessage message;
    message.points = points;
    message.gameId = net->GetGameId();
    desc.m_id = id;
    desc.m_data = &message;
    desc.m_length = sizeof(message);
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    static_cast<Net::Client*>(net->GetClient(skater->m_viewport))->EnqueueMessageToServer(&desc);
}

// The robot counts: of the run (`run`) and of the combo; the factor back to 1.
void ResetRobot(Score* score, bool run)
{
    if (run) {
        score->m_robotLines = 0;
        score->m_robotNewLines = 0;
        std::memset(score->m_lineCounts, 0, sizeof(score->m_lineCounts));
    }
    score->m_robotComboLines = 0;
    score->m_robotComboFirstLines = 0;
    score->m_robotComboNewLines = 0;
    std::memset(score->m_comboLineCounts, 0, sizeof(score->m_comboLineCounts));
    score->m_robotFactor = 1.0f;
}

// The log's tricks freed and the combo emptied (Land and Bail; the pot itself is the caller's).
void ClearCombo(Score* score)
{
    int count = score->m_log.m_count;
    for (int i = 0; i < count; ++i)
        Mem::Free(score->m_log.m_items[i]);
    score->m_log.m_count = 0;
    score->m_lastNoSpin = -1;
    score->m_spinTrick = -1;
    score->m_tricks = 0;
    score->m_multiplier = 0.0f;
    ResetRobot(score, false);
    score->m_bonus = 0;
}

} // namespace

// 0x00562E40
void Score::CaptureScore()
{
    if (!m_tricks) {
        m_potSpecial = 0;
        m_potScore = 0;
        return;
    }
    m_pot = 0;
    int spin = 2;
    for (int i = 0; i < m_tricks; ++i) {
        STrickLog* trick = Trick(i);
        uint32_t flags = trick->m_flags;
        if (flags & kTrickNoSpin)
            spin = 2;
        else if (i == 0 || (Trick(i - 1)->m_flags & kTrickNoSpin))
            spin = ((flags >> 8) & 1) ? trick->m_spin + 1 : SpinMultiplier(trick->m_spin);
        int reduction = Reduction(trick->m_repeats);
        int points = reduction * spin * trick->m_score / 200; // (percent times half the multiplier)
        if (trick->m_kind)
            points = points * 120 / 100;
        if (m_debug)
            Dbg::Printf(GAME_TEXT(0x0064E2F0, "base %d reduction mult %.2f spin mult %.1f score = %i %s\n"), trick->m_score, static_cast<double>(reduction) * static_cast<double>(0.01f),
                        static_cast<double>(spin) * 0.5, points,
                        trick->m_text); // "base %d reduction mult %.2f spin mult %.1f score = %i %s\n"
        m_pot += points;
    }
    if (m_debug)
        Dbg::Printf(GAME_TEXT(0x0064E2E0, "   total %d\n"), m_pot); // "   total %d\n"
    if (Script::GetInteger(kNewSpecial)) {
        double points = static_cast<double>(m_pot) * m_multiplier;
        double counted = static_cast<double>(m_potSpecial);
        if (points > counted) {
            m_special += Ftol(points - counted);
            if (m_special < 0) {
                SetSpecialActive(false);
                m_special = 0;
            }
            if (m_special >= 3000) {
                SetSpecialActive(true);
                m_special = 3000;
            }
        }
    }
    m_potScore = Ftol(static_cast<double>(m_pot) * m_multiplier);
    m_potSpecial = m_potScore;
}

// 0x00563020
void Score::ResetScorePot()
{
    m_pot = 0;
    Lst::DynamicTableDestroyer destroyer{&m_log};
    destroyer.DeleteTableContents();
    m_lastNoSpin = -1;
    m_spinTrick = -1;
    m_tricks = 0;
    m_multiplier = 0.0f;
    m_robotComboLines = 0;
    m_robotComboFirstLines = 0;
    m_robotComboNewLines = 0;
    std::memset(m_comboLineCounts, 0, sizeof(m_comboLineCounts));
    m_bonus = 0;
    m_robotFactor = 1.0f;
}

// 0x00563080
void Score::PositionBalanceMeter(bool on, float lean, bool manual)
{
    Obj::CSkater* skater = SkaterById(m_skaterId);
    auto* meter = static_cast<Front::CSpriteElement*>(
        FindElement(kTheBalanceMeter + static_cast<uint32_t>(skater->GetSkaterNumber())));
    int turnedOn = 0;
    meter->GetIntegerTag(kTagTurnedOn, &turnedOn);
    if (on != (turnedOn != 0)) {
        Script::CStruct* params = NewStructure();
        params->AddChecksum(kId, kTheBalanceMeter + static_cast<uint32_t>(skater->GetSkaterNumber()));
        Script::RunScript(on ? kShowBalanceMeter : kHideBalanceMeter, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    meter->SetChecksumTag(kTagMode, manual ? kManual : kBalance);
    // The arrow between the two arrow positions the lean falls between.
    double size = std::fabs(static_cast<double>(lean));
    double steps = 1.0 / m_arrowStep;
    int index = Ftol(steps * size);
    double part = size - index * static_cast<double>(m_arrowStep);
    const Script::CPair& from = m_arrows[index];
    const Script::CPair& to = m_arrows[index + 1];
    m_meterOffset.x = static_cast<float>((static_cast<double>(to.x) - from.x) * steps * part + from.x);
    m_meterOffset.y = static_cast<float>((static_cast<double>(to.y) - from.y) * steps * part + from.y);
    m_meterRotation = static_cast<float>(static_cast<double>(lean) * -45.0);
    auto* arrow = static_cast<Front::CSpriteElement*>(meter->mp_firstChild.mp_object);
    bool left = !(lean >= 0.0f); // (as the FPU compares: a NaN counts as below)
    double x;
    double y;
    if (manual) {
        meter->SetPos(m_bars[1].x, m_bars[1].y, 1);
        meter->SetSpriteRotation(-90.0f, 1);
        y = static_cast<double>(meter->m_height) * 0.5 + m_meterOffset.y + 50.0;
        x = static_cast<double>(meter->m_width) * 0.5 - 80.0;
    } else {
        meter->SetPos(m_bars[0].x, m_bars[0].y, 1);
        meter->SetSpriteRotation(0.0f, 1);
        y = static_cast<double>(meter->m_height) * 0.5 + m_meterOffset.y;
        x = static_cast<double>(meter->m_width) * 0.5;
    }
    x = left ? x - m_meterOffset.x : x + m_meterOffset.x;
    arrow->SetPos(static_cast<float>(x), static_cast<float>(y), 1);
    arrow->SetSpriteRotation(-m_meterRotation, 1);
}

// 0x00563370
void Score::SetBalanceMeter(bool on, float lean) { PositionBalanceMeter(on, lean, false); }

// 0x00563390
void Score::SetManualMeter(bool on, float lean) { PositionBalanceMeter(on, lean, true); }

// 0x00563440
Score* Score::Construct()
{
    m_seen.m_count = 0;
    m_seen.m_first = nullptr;
    m_seen.m_cursor = nullptr;
    m_log.Construct(8, 8);
    for (Script::CPair& arrow : m_arrows)
        arrow.Init();
    m_meterOffset.Init();
    for (Script::CPair& bar : m_bars)
        bar.Init();
    m_lastNoSpin = -1;
    m_spinTrick = -1;
    m_skaterId = 0;
    m_total = 0;
    m_pot = 0;
    m_potScore = 0;
    m_potSpecial = 0;
    m_tricks = 0;
    m_multiplier = 0.0f;
    m_special = 0;
    SetSpecialActive(false); // (inlined there; m_specialActive isn't set before: as the original)
    m_specialPhase = 0.0f;
    m_bestTricks = 0;
    m_bestCombo = 0;
    m_bestComboSent = 0;
    m_debug = Script::GetInteger(kDebugScore) != 0;
    m_focusTimeLimit = Script::GetFloat(kFocusTimeLimit);
    m_focusMaxTimes = Script::GetInteger(kFocusMaxTimesInCombo);
    SetupBalanceMeter();
    return this;
}

// 0x00564B80
void Score::DestroyBody()
{
    Reset();
    Mem::FreeMalloc(m_log.m_items);
    reinterpret_cast<Obj::ProfileTable*>(&m_seen)->Clear(); // (the lookup table's shared destructor)
}

// 0x00563580
void Score::Update()
{
    if (m_special) {
        float special = static_cast<float>(m_special);
        double rate = m_specialActive ? static_cast<double>(0.2f) : static_cast<double>(0.05f);
        m_special = Ftol(special - Tmr::GetSlomoFrameSeconds() * rate);
        if (m_special <= 0) {
            SetSpecialActive(false);
            m_special = 0;
        }
    }
    if (m_potState == 1) {
        if (!--m_potCountdown) {
            Obj::CSkater* skater = SkaterById(m_skaterId);
            TrickTextCountdown(skater ? skater->GetSkaterNumber() : 0);
            m_potState = 2;
        }
    } else if (m_potState == 2) {
        DispatchScorePot(m_potShown, 0.0f);
        m_potShown -= ScoreIncrement(m_potShown);
        if (m_potShown < 0) {
            m_potShown = 0;
            m_potState = 0;
        }
    }
    Front::ElementPtr bar;
    {
        Front::ElementPtr found;
        Obj::CSkater* skater = SkaterById(m_skaterId);
        uint32_t id = skater ? kTheSpecialBarSprite + static_cast<uint32_t>(skater->GetSkaterNumber()) : kTheSpecialBarSprite;
        bar = *Front::CScreenElementManager::Instance()->GetElement(&found, id, 0);
    }
    double full = static_cast<double>(m_special) * static_cast<double>(1.0f / 3000.0f); // (0x0064E334)
    float size = static_cast<float>(full);
    if (full > 1.0)
        size = 1.0f;
    else if (size < 0.0f)
        size = 0.0f;
    Front::CScreenElement* element = bar.mp_object; // (no check: as the original)
    element->SetScale(size, element->m_shown.scaleY, false, 0);
    uint8_t rgba[4];
    std::memcpy(rgba, m_barColors[0], 4);
    if (m_specialActive) {
        double flash = (X87::Cos(m_specialPhase) + 1.0) * 0.5;
        m_specialPhase = static_cast<float>(static_cast<double>(m_specialRate) + m_specialPhase);
        for (int c = 0; c < 4; ++c) {
            int from = m_barColors[1][c];
            rgba[c] = static_cast<uint8_t>(Ftol((m_barColors[2][c] - from) * flash + from));
        }
    }
    uint32_t color;
    std::memcpy(&color, rgba, 4);
    element->SetRGBA(color, 1);
}

// 0x00563990
void Score::Trigger(const char* text, int score, uint32_t flags)
{
    bool unnamed = !text[0];
    Obj::CSkater* skater = SkaterById(m_skaterId);
    if (!skater)
        return;
    auto* trickComponent = reinterpret_cast<uint8_t*>(skater->GetComponent(kTrick));
    auto* stats = static_cast<Obj::CStatsManagerComponent*>(skater->GetComponent(kStatsManager));
    if (text[0])
        trickComponent[0x13A8] = 1;
    auto* physics = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterCorePhysics));
    auto* physicsSkater = *reinterpret_cast<Obj::CSkater**>(physics + 0xC);
    uint8_t flipped = (*reinterpret_cast<uint8_t**>(physics + 0x1528))[0x28];
    int stance = physicsSkater->m_goofy ? flipped : !flipped;
    stats->Set10(static_cast<int>(reinterpret_cast<uintptr_t>(text)), score, stance);
    if (m_tricks > 249) {
        TrimLog();
        if (m_tricks > 249) {
            if (unnamed)
                return;
            Dbg::Printf(GAME_TEXT(0x0064E368, "Trick Limit (%d) Reached\n"), 250); // "Trick Limit (%d) Reached\n"
            LastTrick()->m_score += score;
            if (!(flags & kTrickNoMultiplier))
                m_multiplier = static_cast<float>(static_cast<double>(m_multiplier) + 1.0);
            CaptureScore();
            return;
        }
    }
    Mem::PushContext(Mem::Manager::Get()->m_frontEndHeap);
    uint32_t name = Crc::GenerateCRCFromString(text);
    int kind = (flags & kTrickKindHigh) ? (flags >> 1) & 4 : (flags >> 1) & 6;
    if (m_log.m_count < 250) {
        m_log.Add(Mem::New(sizeof(STrickLog)));
        LastTrick()->m_score = 0;
    }
    STrickLog* trick = LastTrick();
    trick->m_score += score;
    trick->m_name = name;
    trick->m_kind = kind;
    trick->m_flags = flags;
    trick->m_spin = 0;
    trick->m_text[0] = 0;
    trick->m_shown[0] = 0;
    trick->m_spinDegrees = 0;
    if (!unnamed) {
        EscapeSpaces(trick->m_text, text);
        const char* format;
        if (flags & kTrickColour3)
            format = GAME_TEXT(0x0064E35C, "\\c3%s%s\\c0 "); // "\c3%s%s\c0 "
        else if (flags & kTrickSpecial)
            format = GAME_TEXT(0x0064E350, "\\c2%s%s\\c0 "); // "\c2%s%s\c0 "
        else
            format = GAME_TEXT(0x0064E348, "%s%s "); // "%s%s "
        std::snprintf(trick->m_shown, sizeof(trick->m_shown), format, GAME_TEXT(0x006469B4, ""), trick->m_text);
        // The trick before it (the last one named, not "...") gets the " + " joining them.
        for (int i = m_log.m_count - 2; i >= 0; --i) {
            char* shown = Trick(i)->m_shown;
            size_t length = std::strlen(shown);
            if (shown[0] == '.' || !length)
                continue;
            if (shown[length - 1] == ' ')
                shown[length - 1] = 0;
            std::memcpy(shown + std::strlen(shown), GAME_TEXT(0x0064E340, "\\_+ "), 5); // "\_+ "
            break;
        }
    }
    if (m_lastNoSpin == m_tricks - 1)
        m_spinTrick = m_tricks;
    if (flags & kTrickNoSpin)
        m_lastNoSpin = m_tricks;
    auto* seen = static_cast<STrickSeen*>(m_seen.GetItem(&name));
    bool notCounted = false;
    if (flags & kTrickNotCounted) {
        LastTrick()->m_repeats = 0;
        notCounted = true;
    } else {
        if (!seen) {
            seen = static_cast<STrickSeen*>(Mem::New(sizeof(STrickSeen)));
            m_seen.PutItem(&name, seen);
            for (int k = 0; k < 4; ++k) {
                seen->m_landed[k] = 0;
                seen->m_current[k] = 0;
            }
        }
        int* counts = seen->m_landed; // (m_landed then m_current; the kind can reach past them, as the original)
        LastTrick()->m_repeats = counts[kind] + counts[kind + 4];
        ++counts[kind + 4];
    }
    ++m_tricks;
    if (!unnamed) {
        float oldMultiplier = m_multiplier;
        int degradeStart = Script::GetInteger(kTrickMultDegradeStart);
        ++m_comboTricks;
        bool addOne = true;
        if (m_multiplier != 0.0f && !notCounted) {
            int times = seen->m_landed[kind + 4];
            if (times >= degradeStart) {
                addOne = false;
                double added = 1.0 - (times - degradeStart + 1) * 0.5;
                float addedF = static_cast<float>(added);
                if (added > 0.0 && !(flags & kTrickNoMultiplier))
                    m_multiplier = static_cast<float>(static_cast<double>(addedF) + m_multiplier);
            } else if (flags & kTrickNoMultiplier) {
                addOne = false;
            }
        }
        if (addOne)
            m_multiplier = static_cast<float>(static_cast<double>(m_multiplier) + 1.0);
        if (m_multiplier == 1.0f) {
            auto* timer = static_cast<Obj::CSkaterRunTimerComponent*>(skater->GetComponent(kSkaterRunTimer));
            timer->m_timeLeft = static_cast<Obj::CSkater*>(timer->m_object)->GetScaledPhysics(kPhysicsRunTimerDuration);
            timer->m_resumes = 0;
            timer->m_state = 2;
            m_focusTimeLimit = Script::GetFloat(kFocusTimeLimit);
            m_focusMaxTimes = Script::GetInteger(kFocusMaxTimesInCombo);
            skater->BroadcastEvent(kSkaterEnterCombo, nullptr, 0.0f);
        }
        if (TricksSinceNoSpin(this) > 10) {
            LastTrick()->m_score = 0;
            if (m_multiplier > 1.0f)
                m_multiplier = static_cast<float>(static_cast<double>(m_multiplier) - 1.0);
        }
        if (PointsHeld(skater)) {
            LastTrick()->m_score = 0;
            m_multiplier = oldMultiplier;
        }
    }
    CaptureScore();
    DispatchTrickSequence(false);
    TrickTextPulse(skater->GetSkaterNumber());
    m_potState = 0;
    DispatchScorePot(m_pot, m_multiplier);
    Mem::PopContext();
}

// 0x00563F30
void Score::SetSpin(int degrees)
{
    if (!m_tricks || m_spinTrick < 0)
        return;
    Obj::CSkater* skater = SkaterById(m_skaterId);
    bool flipped = static_cast<Obj::CAnimBlenderComponent*>(skater->GetComponent(kAnimBlender))->AllChannelsFlag29();
    int size = degrees;
    const char* side;
    if (degrees < 0) {
        size = -degrees;
        side = flipped ? GAME_TEXT(0x0064E490, "BS") : GAME_TEXT(0x0064E494, "FS"); // "BS" / "FS"
    } else {
        side = flipped ? GAME_TEXT(0x0064E494, "FS") : GAME_TEXT(0x0064E490, "BS");
    }
    int halfTurns = (Ftol(Obj::GetPhysicsFloat(kSpinCountSlop, 0, nullptr)) + size) / 180;
    Trick(m_spinTrick)->m_spinDegrees = degrees;
    uint32_t crc = Crc::GenerateCRCFromString(Trick(m_spinTrick)->m_text);
    bool renamed = false;
    if (crc == kFakieOllie || crc == kCabTrick2 || crc == kCabTrick3) {
        if (halfTurns == 1) {
            std::memcpy(Trick(m_spinTrick)->m_text, GAME_TEXT(0x0064E484, "Half\\_Cab"), 10); // "Half\_Cab"
            renamed = true;
        } else if (halfTurns == 2) {
            std::memcpy(Trick(m_spinTrick)->m_text, GAME_TEXT(0x0064E478, "Cabalerial"), 11); // "Cabalerial"
            renamed = true;
        } else if (halfTurns > 2) {
            Trick(m_spinTrick)->m_text[0] = 0;
            if (side == GAME_TEXT(0x0064E494, "FS"))
                side = GAME_TEXT(0x0064E470, "FS Cab"); // "FS Cab"
            if (side == GAME_TEXT(0x0064E490, "BS"))
                side = GAME_TEXT(0x0064E468, "BS Cab"); // "BS Cab"
        }
    }
    if (Trick(m_spinTrick)->m_spin >= halfTurns)
        return;
    if (m_debug)
        Dbg::Printf(GAME_TEXT(0x0064E454, "New spin value %d:\n"), halfTurns * 180); // "New spin value %d:\n"
    STrickLog* trick = Trick(m_spinTrick);
    if (trick->m_name == kOllie && halfTurns % 2 == 1) { // (an odd number of half turns: the other side)
        if (side == GAME_TEXT(0x0064E494, "FS"))
            side = GAME_TEXT(0x0064E490, "BS");
        else if (side == GAME_TEXT(0x0064E490, "BS"))
            side = GAME_TEXT(0x0064E494, "FS");
        if (side == GAME_TEXT(0x0064E470, "FS Cab"))
            side = GAME_TEXT(0x0064E468, "BS Cab");
        else if (side == GAME_TEXT(0x0064E468, "BS Cab"))
            side = GAME_TEXT(0x0064E470, "FS Cab");
    }
    int shownDegrees = halfTurns * 180;
    bool joined = m_log.m_count - 1 > m_spinTrick;
    uint8_t flags = static_cast<uint8_t>(trick->m_flags);
    if (!renamed) {
        const char* format;
        if (flags & kTrickColour3)
            format = (joined ? GAME_TEXT(0x0064E43C, "\\c3%s\\_%d\\_%s\\c0\\_+ ") : GAME_TEXT(0x0064E400, "\\c3%s\\_%d\\_%s\\c0")); // "\c3%s\_%d\_%s\c0[\_+ ]"
        else if (flags & kTrickSpecial)
            format = (joined ? GAME_TEXT(0x0064E424, "\\c2%s\\_%d\\_%s\\c0\\_+ ") : GAME_TEXT(0x0064E3EC, "\\c2%s\\_%d\\_%s\\c0")); // "\c2%s\_%d\_%s\c0[\_+ ]"
        else
            format = (joined ? GAME_TEXT(0x0064E414, "%s\\_%d\\_%s\\_+ ") : GAME_TEXT(0x0064E3E0, "%s\\_%d\\_%s")); // "%s\_%d\_%s[\_+ ]"
        std::snprintf(trick->m_shown, sizeof(trick->m_shown), format, side, shownDegrees, trick->m_text);
    } else {
        const char* format;
        if (flags & kTrickColour3)
            format = (joined ? GAME_TEXT(0x0064E3CC, "\\c3%s\\_%s\\c0\\_+ ") : GAME_TEXT(0x0064E39C, "\\c3%s\\_%s\\c0")); // "\c3%s\_%s\c0[\_+ ]"
        else if (flags & kTrickSpecial)
            format = (joined ? GAME_TEXT(0x0064E3B8, "\\c2%s\\_%s\\c0\\_+ ") : GAME_TEXT(0x0064E38C, "\\c2%s\\_%s\\c0")); // "\c2%s\_%s\c0[\_+ ]"
        else
            format = (joined ? GAME_TEXT(0x0064E3AC, "%s\\_%s\\_+ ") : GAME_TEXT(0x0064E384, "%s\\_%s")); // "%s\_%s[\_+ ]"
        std::snprintf(trick->m_shown, sizeof(trick->m_shown), format, side, trick->m_text);
    }
    DispatchTrickSequence(false);
    Trick(m_spinTrick)->m_spin = halfTurns;
    CaptureScore();
    static_cast<Obj::CStatsManagerComponent*>(skater->GetComponent(kStatsManager))
        ->SetValue58(static_cast<float>(shownDegrees));
    m_potState = 0;
    DispatchScorePot(m_pot, m_multiplier);
    skater->BroadcastEvent(kSkaterSpinDisplayed, nullptr, 0.0f);
}

// 0x00564270
void Score::TweakTrick(int points)
{
    if (m_tricks <= 0)
        return;
    STrickLog* trick = Trick(m_tricks - 1);
    if (!trick->m_text[0])
        return;
    if (TricksSinceNoSpin(this) > 10)
        return;
    if (!(trick->m_flags & kTrickNoSpin) && trick->m_score > 20000)
        return;
    if (PointsHeld(SkaterById(m_skaterId)))
        return;
    Trick(m_tricks - 1)->m_score += points;
    CaptureScore();
    DispatchTrickSequence(false);
    m_potState = 0;
    DispatchScorePot(m_pot, m_multiplier);
}

// 0x00564330
void Score::Land()
{
    GameNet::Manager* net = NetManager();
    Game::CGameMode* mode = GameMode();
    CaptureScore();
    int points = Ftol(static_cast<double>(m_pot) * m_multiplier);
    Obj::CSkater* skater = SkaterById(m_skaterId);
    bool byClusters = Script::GetInteger(kScoreByClusters) != 0;
    Game::GetGoalManager()->Land();
    if (byClusters || (mode->IsTrue(kShouldModulateColor) && points > 0))
        LogTrickObjectRequest(points);
    static_cast<Obj::CTrickComponent*>(skater->GetComponent(kTrick))->SetTrickObjects(false);
    if (!Script::GetInteger(kNewSpecial)) {
        m_special += Ftol(static_cast<double>(m_pot) * m_multiplier);
        if (m_special >= 3000) {
            SetSpecialActive(true);
            m_special = 3000;
        }
        if (m_special < 0)
            m_special = 0;
    } else {
        m_potSpecial = 0;
    }
    if (m_comboTricks > m_bestTricks)
        m_bestTricks = m_comboTricks;
    int combo = m_potScore;
    if (combo > m_bestCombo)
        m_bestCombo = combo;
    m_comboTricks = 0;
    if (combo > m_bestComboSent) {
        if (net->InNetGame() && mode->GetNameChecksum() != kNetLobby)
            SendPoints(net, skater, kMsgBestCombo, m_potScore);
        m_bestComboSent = m_potScore;
    }
    if (!mode->ShouldDegradeScore()) {
        for (int i = 0; i < m_seen.m_count; ++i) {
            auto* seen = static_cast<STrickSeen*>(m_seen.GetItemByIndex(&i, nullptr)); // (inlined there)
            for (int k = 0; k < 4; ++k) {
                seen->m_landed[k] = 0;
                seen->m_current[k] = 0;
            }
        }
        ResetRobot(this, true);
    } else {
        for (int i = 0; i < m_seen.m_count; ++i) {
            auto* seen = static_cast<STrickSeen*>(m_seen.GetItemByIndex(&i, nullptr));
            for (int k = 0; k < 4; ++k) {
                seen->m_landed[k] += seen->m_current[k];
                seen->m_current[k] = 0;
            }
        }
    }
    if (m_tricks) {
        m_potState = 1;
        m_potShown = Ftol(static_cast<double>(m_pot) * m_multiplier);
        m_potCountdown = 150;
        DispatchScorePot(m_potShown, 0.0f);
        TrickTextLanded(skater->GetSkaterNumber());
    }
    m_pot = 0;
    ClearCombo(this);
    int before = GetTotalScore(); // (inlined there)
    if (!mode->IsTrue(kShouldModulateColor) && mode->ShouldTrackTrickScore()) {
        bool send = mode->ShouldAccumulateScore();
        if (!send && mode->ShouldTrackBestCombo() && GetTotalScore() < points)
            send = true;
        if (send) {
            if (!net->OnServer())
                SendPoints(net, skater, kMsgLandedScore, points);
            SetTotalScore(mode->ShouldTrackBestCombo() ? points : m_total + points);
        } else if (!mode->ShouldTrackBestCombo() && !static_cast<uint8_t>(mode->IsScoreFrozen())) {
            SetTotalScore(points);
        }
    }
    CheckHighScore(before, GetTotalScore());
    static_cast<Obj::CSkaterRunTimerComponent*>(skater->GetComponent(kSkaterRunTimer))->Stop();
}

// 0x00564860
void Score::Bail(bool clear)
{
    for (int i = 0; i < m_seen.m_count; ++i) {
        auto* seen = static_cast<STrickSeen*>(m_seen.GetItemByIndex(&i, nullptr)); // (inlined there)
        for (int k = 0; k < 4; ++k)
            seen->m_current[k] = 0;
    }
    if (m_tricks) {
        m_potState = 0;
        if (!clear) {
            Obj::CSkater* skater = SkaterById(m_skaterId);
            DispatchScorePot(Ftol(static_cast<double>(m_pot) * m_multiplier), 0.0f);
            TrickTextBail(skater->GetSkaterNumber());
        } else {
            DispatchTrickSequence(true);
            DispatchScorePot(0, 0.0f);
        }
    }
    m_pot = 0;
    ClearCombo(this);
    m_special = 0;
    SetSpecialActive(false); // (inlined there)
    static_cast<Obj::CSkaterRunTimerComponent*>(SkaterById(m_skaterId)->GetComponent(kSkaterRunTimer))->Stop();
    Game::GetGoalManager()->Bail();
}

// 0x00564A10
void Score::Reset()
{
    if (!SkateCalls::Skate())
        return;
    Obj::CSkater* skater = SkaterById(m_skaterId);
    Dbg::Printf(GAME_TEXT(0x0064E498, "Score::RESET ...............\n")); // "Score::RESET ...............\n"
    ResetRobot(this, true);
    ResetScorePot();
    Lst::LookupTableDestroyer destroyer{&m_seen};
    destroyer.FreeTableContents();
    if (skater && skater->m_unknownBC) {
        DispatchTrickSequence(false);
        DispatchScorePot(0, 0.0f);
    }
    m_total = GameMode()->GetNameChecksum() == kNetFirefight || GameMode()->GetNameChecksum() == kFirefight ? 100 : 0;
    m_pot = 0;
    m_potScore = 0;
    m_potSpecial = 0;
    m_special = 0;
    m_bestComboSent = 0;
    SetSpecialActive(false); // (inlined there)
    m_potShown = 0;
    m_potState = 0;
    SetSpecialBarColors();
    skater = SkaterById(m_skaterId);
    if (skater && skater->m_unknownBC)
        static_cast<Obj::CSkaterRunTimerComponent*>(skater->GetComponent(kSkaterRunTimer))->Stop();
}

// 0x00564BE0
void Score::UpdateSpin(int degrees)
{
    if (m_tricks && m_spinTrick >= 0 && !(Trick(m_spinTrick)->m_flags & kTrickNoSpin))
        SetSpin(degrees);
}

} // namespace Mdl

namespace Lst {

// 0x005633E0
void LookupTableDestroyer::FreeTableContents()
{
    int count = m_table->m_count;
    int index = 0;
    for (; count > 0; --count) {
        uint32_t key;
        void* item = m_table->GetItemByIndex(&index, &key);
        m_table->FlushItem(&key);
        if (item)
            Mem::Free(item);
    }
}

} // namespace Lst

REPLACE(0x00562E40, &Mdl::Score::CaptureScore);
REPLACE(0x00563020, &Mdl::Score::ResetScorePot);
REPLACE(0x00563080, &Mdl::Score::PositionBalanceMeter);
REPLACE(0x00563370, &Mdl::Score::SetBalanceMeter);
REPLACE(0x00563390, &Mdl::Score::SetManualMeter);
REPLACE(0x00563440, &Mdl::Score::Construct);
REPLACE(0x00564B80, &Mdl::Score::DestroyBody);
REPLACE(0x00563580, &Mdl::Score::Update);
REPLACE(0x00563990, &Mdl::Score::Trigger);
REPLACE(0x00563F30, &Mdl::Score::SetSpin);
REPLACE(0x00564270, &Mdl::Score::TweakTrick);
REPLACE(0x00564330, &Mdl::Score::Land);
REPLACE(0x00564860, &Mdl::Score::Bail);
REPLACE(0x00564A10, &Mdl::Score::Reset);
REPLACE(0x00564BE0, &Mdl::Score::UpdateSpin);
REPLACE(0x005633E0, &Lst::LookupTableDestroyer::FreeTableContents);
