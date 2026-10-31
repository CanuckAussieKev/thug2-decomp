// GameModules::PointTally's queries and displays, and the trick log's table (see point_tally.h).

#include "game/skate/point_tally.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "frontend/text_block.h"
#include "frontend/text_element.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score_internal.h"
#include "engine/gfx/viewport_list.h"

namespace ListKit {

// 0x00561470
GrowableMap* GrowableMap::MakeNew(int capacity, int grow)
{
    m_capacity = capacity;
    m_grow = grow;
    m_items = static_cast<void**>(MemSys::AllocBlock(static_cast<unsigned int>(capacity) * 4));
    for (int i = 0; i < m_capacity; ++i)
        m_items[i] = nullptr;
    m_num = 0;
    return this;
}

// 0x005614C0
void GrowableMap::DestroyBody() { MemSys::FreeMalloc(m_items); }

// 0x005614D0
void GrowableMap::AppendOne(void* item)
{
    if (m_capacity <= m_num) {
        int capacity = m_capacity + m_grow;
        auto** items = static_cast<void**>(MemSys::AllocBlock(static_cast<unsigned int>(capacity) * 4));
        for (int i = 0; i < m_capacity; ++i)
            items[i] = m_items[i];
        for (int i = m_capacity; i < capacity; ++i)
            items[i] = nullptr;
        MemSys::FreeMalloc(m_items);
        m_items = items;
        m_capacity = capacity;
    }
    m_items[m_num++] = item;
}

// 0x00562920
void TableReleaser::EmptyTableItems()
{
    int count = m_lookupTable->m_num;
    for (int i = 0; i < count; ++i)
        MemSys::Dealloc(m_lookupTable->m_items[i]);
    m_lookupTable->m_num = 0;
}

} // namespace Lst

namespace GameModules {

namespace ScoreInternal {

int g_trickReduction[5]{100, 75, 50, 25, 10};
int g_spinMultiplier[6]{2, 3, 4, 5, 6, 7};

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* made = Scripts::SpawnParamBag();
    return made ? made->PrepareAll() : nullptr;
}

void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

} // namespace ScoreInternal

using namespace ScoreInternal;

namespace {

constexpr uint32_t kSpecialBarColors = 0x2E8B9F1C;          // special_bar_colors
constexpr uint32_t kSpecialBarInterpolatorRate = 0x3ABCDA81; // special_bar_interpolator_rate
constexpr uint32_t kParams = 0x7031F10C;                    // params
constexpr uint32_t kExtraTricks = 0x6E855102;               // ExtraTricks
constexpr uint32_t kName = 0xA1DC81F9;                      // Name
constexpr uint32_t kUpdateTrickText = 0xD4491BE1;           // UpdateTrickText
constexpr uint32_t kBalanceMeterInfo = 0x91CB3C33;          // balance_meter_info
constexpr uint32_t kArrowPositions = 0xF716442A;            // arrow_positions
constexpr uint32_t kHorse = 0x9D65D0E7;                     // Horse
constexpr uint32_t kNetHorse = 0xB239668F;                  // nethorse
constexpr uint32_t kBarPositionsMpV = 0x44E39BB5;           // bar_positions_mp_v
constexpr uint32_t kBarPositionsMpH = 0xBEECA6D6;           // bar_positions_mp_h
constexpr uint32_t kBarPositions = 0xF314312A;              // bar_positions
constexpr uint32_t kUpdateScorepot = 0xA611BB15;            // UpdateScorepot
constexpr uint32_t kIsCareer = 0x1DED1EA4;                  // is_career
constexpr uint32_t kNetKing = 0x6EF8FDA0;                   // netking
constexpr uint32_t kKing = 0x5D32129C;                      // king
constexpr uint32_t kTrickMapping = 0xD544AA2D;              // trick_mapping
constexpr uint32_t kTrickSlot = 0xA92A2280;                 // trickslot
constexpr uint32_t kTrickName = 0x5B077CE1;                 // trickname
constexpr uint32_t kIsCat = 0xB56A8816;                     // isCat
constexpr uint32_t kPerfectLandingSlop = 0xFCFACAB8;        // perfect_landing_slop
constexpr uint32_t kRobotKickInCount = 0x6EE127B7;          // robot_kick_in_count
constexpr uint32_t kTrickTextContainerId = 0x33124D2E;      // trick_text_container_id
constexpr uint32_t kTheTrickTextId = 0x6E7C7BA7;            // the_trick_text_id
constexpr uint32_t kTheScorePotTextId = 0x6D02989C;         // the_score_pot_text_id
constexpr uint32_t kTrickTextCountdown = 0xEE9CE723;        // trick_text_countdown
constexpr uint32_t kTrickTextLanded = 0xC890E875;           // trick_text_landed
constexpr uint32_t kTrickTextBail = 0xAA7F404D;             // trick_text_bail
constexpr uint32_t kTrickTextPulse = 0x3BD51FE5;            // trick_text_pulse

// The skate module's score goals (+0x60, 64 of them): a career goal done when the total first reaches `score`.
struct ScoreGoal {
    int score;
    uint32_t script;
    int goal;
};

// The screen mode (0x004A0710: 1 a vertical split).
int ScreenMode() { return Engine3D::GetViewLayout(); }

// The created trick `index`'s name (the local skater's; 0 without one).
uint32_t CreatedTrickName(int index)
{
    auto* skater = static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
    if (!skater)
        return 0;
    const char* name;
    reinterpret_cast<Scripts::CStruct*>(*reinterpret_cast<uint32_t*>(skater->m_trickSlots[index] + 4))
        ->FetchText(kName, &name, 1);
    return Checksums::HashFromText(name);
}

// A trick key combo's name for the counts: the mapped trick, else the created trick in that slot (0: none of
// either; `none` when no created trick either).
bool KeyComboName(uint32_t keyCombo, int taps, uint32_t* name)
{
    *name = PointTally::TrickFromKeyCombo(keyCombo, taps);
    if (*name)
        return true;
    int index = PointTally::CreatedTrickFromKeyCombo(keyCombo);
    if (index == -1)
        return false;
    *name = CreatedTrickName(index);
    return true;
}

// The trick text script `script` with the skater number's ids.
void RunTrickTextScript(uint32_t script, int skater)
{
    Scripts::CStruct* params = SpawnParamTable();
    params->PutNameKey(kTrickTextContainerId, kTrickTextContainer + static_cast<uint32_t>(skater));
    params->PutNameKey(kTheTrickTextId, kTheTrickText + static_cast<uint32_t>(skater));
    params->PutNameKey(kTheScorePotTextId, kTheScorePotText + static_cast<uint32_t>(skater));
    Scripts::LaunchScript(script, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

} // namespace

// 0x00561240
int PointTally::PointGain(int value)
{
    if (value > 1000000)
        return 1000000;
    if (value > 100000)
        return 100000;
    if (value > 10000)
        return 10000;
    if (value > 100)
        return 100; // (above 1000 too: there's no step of 1000)
    return value <= 10 ? 1 : 10;
}

// Update (0x00563580) keeps EDX across the call: the original's leaves it alone.
__declspec(naked) int ScoreIncrementStub()
{
    __asm {
        push edx
        push dword ptr [esp + 8]
        call PointTally::PointGain
        add esp, 4
        pop edx
        ret
    }
}

// 0x00561290
void PointTally::SetRiderNumber(int16_t id) { m_riderNo = id; }

// 0x005612A0
void PointTally::EscapeSpaces(char* out, const char* text)
{
    for (; *text; ++text) {
        if (*text == ' ') {
            *out++ = '\\';
            *out++ = '_';
        } else {
            *out++ = *text;
        }
    }
    *out = 0;
}

// 0x005612D0
void PointTally::SetSpecialBarColors()
{
    Scripts::CArray* colors = Scripts::FetchList(kSpecialBarColors);
    for (uint32_t i = 0; i < 3; ++i) {
        auto* color = reinterpret_cast<Scripts::CArray*>(colors->FetchPart(i));
        for (uint32_t c = 0; c < 4; ++c)
            m_barColors[i][c] = static_cast<uint8_t>(color->FetchPart(c));
    }
    m_specialRate = Scripts::FetchFloat(kSpecialBarInterpolatorRate);
}

// 0x00561360
uint32_t PointTally::TrickNameAfterTaps(uint32_t trick, int taps)
{
    Scripts::CStruct* params;
    Scripts::ReadSubStruct(trick)->ReadSubStruct(kParams, &params, 1);
    if (taps > 1) {
        uint32_t extra = static_cast<uint32_t>(taps); // (the slot the original reuses: kept when ExtraTricks is missing)
        for (int left = taps - 1; left; --left) {
            params->ReadHashValue(kExtraTricks, &extra, 0);
            reinterpret_cast<Scripts::CStruct*>(Scripts::FetchList(extra)->FetchPart(0))->ReadSubStruct(kParams, &params, 1);
        }
    }
    const char* name;
    params->FetchText(kName, &name, 1);
    return Checksums::HashFromText(name);
}

// 0x00561400
void PointTally::ResetBests()
{
    m_bestTricks = 0;
    m_topChain = 0;
    m_bestComboSent = 0;
}

// 0x00561410
void PointTally::AddBonus(int points) { m_bonus += points; }

// 0x00561420
int PointTally::GetBonus() { return m_bonus; }

// 0x00561550
int PointTally::GetScoreOfRange(int first, int last)
{
    int total = 0;
    int spin = 2;
    for (int i = first; i < last; ++i) {
        STrickLog* trick = Trick(i);
        if (trick->m_flagBits & kTrickNoSpin)
            spin = 2;
        else if (i == 0 || (Trick(i - 1)->m_flagBits & kTrickNoSpin))
            spin = SpinMultiplier(trick->m_twistRate);
        int points = trick->m_pointsHeld * Reduction(trick->m_repeats) * spin / 200; // (percent times half the multiplier)
        if (trick->m_kind)
            points = points * 120 / 100;
        total += points;
    }
    return total;
}

// 0x00561640
uint32_t PointTally::FetchNewestTrickKey() { return m_log.m_num ? NewestLoggedTrick()->m_label : 0; }

// 0x00561660
uint32_t PointTally::GetTrickName(int index) { return Trick(index)->m_label; }

// 0x00561670
void PointTally::DispatchTrickSequence(bool clear)
{
    GameObjects::CRider* skater = RiderForSlot(m_riderNo);
    int number = skater ? skater->GetSkaterNumber() : 0;
    auto* text = static_cast<MenuSystem::CParagraphWidget*>(FindElement(kTheTrickText + static_cast<uint32_t>(number)));
    if (!text)
        return;
    const char* lines[0xFD];
    if (!clear) {
        for (int i = 0; i < m_log.m_num; ++i)
            lines[i] = Trick(i)->m_shown;
        text->ChangeCaption(lines, m_log.m_num);
    } else {
        text->ChangeCaption(lines, 0);
    }
    Scripts::LaunchScript(kUpdateTrickText, nullptr, nullptr, false, nullptr);
}

// 0x00561730
void PointTally::SetupBalanceMeter()
{
    Scripts::CStruct* info = Scripts::ReadSubStruct(kBalanceMeterInfo);
    Scripts::CArray* arrows = nullptr;
    info->FetchList(kArrowPositions, &arrows, 0);
    m_arrowCount = static_cast<int>(arrows->m_extent); // (no check: as the original)
    for (int i = 0; i < m_arrowCount; ++i)
        m_arrows[i] = *reinterpret_cast<Scripts::CPair*>(arrows->FetchPart(static_cast<uint32_t>(i)));
    m_arrows[m_arrowCount] = m_arrows[m_arrowCount - 1];
    m_arrowStep = static_cast<float>(1.0 / m_arrowCount);
    Scripts::CArray* bars = nullptr;
    uint32_t name = kBarPositions;
    if (static_cast<uint8_t>(ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr)) && GameMode()->GetNameKey() != kHorse &&
        GameMode()->GetNameKey() != kNetHorse)
        name = ScreenMode() == 1 ? kBarPositionsMpV : kBarPositionsMpH;
    info->FetchList(name, &bars, 0);
    for (uint32_t i = 0; i < 2; ++i)
        m_bars[i] = *reinterpret_cast<Scripts::CPair*>(bars->FetchPart(i));
}

// 0x00561890
void PointTally::DispatchScorePot(int points, float multiplier)
{
    GameObjects::CRider* skater = RiderForSlot(m_riderNo);
    int number = skater ? skater->GetSkaterNumber() : 0;
    auto* text = static_cast<MenuSystem::CLabelWidget*>(FindElement(kTheScorePotText + static_cast<uint32_t>(number)));
    char shown[0x80];
    if (!points) {
        shown[0] = ' ';
        shown[1] = 0;
    } else if (multiplier == 0.0f) {
        std::snprintf(shown, sizeof(shown), "%s", TextUtil::FormatWithSeparators(points)); // "%s"
    } else {
        int whole = Ftol(multiplier);
        if (static_cast<double>(whole) == multiplier)
            std::snprintf(shown, sizeof(shown), "%s X %d", TextUtil::FormatWithSeparators(points), whole); // "%s X %d"
        else
            std::snprintf(shown, sizeof(shown), "%s X %.1f", TextUtil::FormatWithSeparators(points),
                          static_cast<double>(multiplier)); // "%s X %.1f"
    }
    text->ChangeCaption(shown); // (no check: as the original)
    Scripts::LaunchScript(kUpdateScorepot, nullptr, nullptr, false, nullptr);
}

// 0x005619E0
void PointTally::TestRecordScore(int before, int after)
{
    uint8_t* skate = SkateCalls::Skate();
    if (!GameMode()->IsTrue(kIsCareer))
        return;
    auto* goals = reinterpret_cast<ScoreGoal*>(skate + 0x60);
    for (int i = 0; i < 64; ++i) {
        int score = goals[i].score;
        int goal = goals[i].goal;
        uint32_t script = goals[i].script;
        if (!score || before >= score || after < score || SkateCalls::Career()->GetGoal(goal, -1))
            continue;
        SkateCalls::Career()->SetGoal(goal, -1);
        RiderForSlot(m_riderNo)->StartScriptTask(script, 0, false, false, nullptr);
    }
}

// 0x00561A80
void PointTally::SetGrandTotal(int total)
{
    m_sum = total;
    GameObjects::CRider* skater = RiderForSlot(m_riderNo);
    if (!skater->m_unknownBC) // (no check: as the original)
        return;
    char shown[0x40];
    if (GameMode()->GetNameKey() == kNetKing || GameMode()->GetNameKey() == kKing) {
        uint32_t seconds = static_cast<uint32_t>(m_sum) / 1000;
        std::snprintf(shown, sizeof(shown), "%d:%.2d", seconds / 60, seconds % 60); // "%d:%.2d"
    } else {
        std::snprintf(shown, sizeof(shown), "%d", m_sum); // "%d"
    }
    MenuSystem::SetScoreText(shown, skater->GetSkaterNumber());
}

// 0x00561B40
int PointTally::GetTotalScore()
{
    if (NetSessionSlot()->OnServer() && (Scripts::FetchIntValue(kScoreByClusters) || GameMode()->IsTrue(kShouldModulateColor)))
        return TrickObjects()->CountClustersHeldBy(m_riderNo);
    return m_sum;
}

// 0x00562080
uint32_t PointTally::TrickFromKeyCombo(uint32_t keyCombo, int taps)
{
    GameObjects::CRiderIdentity* profile = SkateCalls::GetProfileFromParams(nullptr);
    Scripts::CStruct* mapping = profile->FetchDetailsParams(kTrickMapping);
    uint32_t trick = 0;
    if (!mapping->ReadHashValue(keyCombo, &trick, 0)) {
        Scripts::CArray* specials;
        profile->GetSpecials()->FetchList(0u, &specials, 1);
        int count = static_cast<int>(specials->m_extent);
        for (int i = 0; i < count; ++i) {
            auto* special = reinterpret_cast<Scripts::CStruct*>(specials->FetchPart(static_cast<uint32_t>(i)));
            uint32_t slot;
            special->ReadHashValue(kTrickSlot, &slot, 1);
            if (slot == keyCombo) {
                special->ReadHashValue(kTrickName, &trick, 1);
                break;
            }
        }
    }
    if (!trick)
        return 0;
    return TrickNameAfterTaps(trick, taps);
}

// 0x00562150
int PointTally::CreatedTrickFromKeyCombo(uint32_t keyCombo)
{
    GameObjects::CRiderIdentity* profile = SkateCalls::GetProfileFromParams(nullptr);
    Scripts::CStruct* mapping = profile->FetchDetailsParams(kTrickMapping);
    int index = -1;
    if (mapping->FetchIntValue(keyCombo, &index, 0))
        return index;
    Scripts::CArray* specials;
    profile->GetSpecials()->FetchList(0u, &specials, 1);
    int count = static_cast<int>(specials->m_extent);
    for (int i = 0; i < count; ++i) {
        auto* special = reinterpret_cast<Scripts::CStruct*>(specials->FetchPart(static_cast<uint32_t>(i)));
        int isCat = 0;
        special->FetchIntValue(kIsCat, &isCat, 0);
        if (!isCat)
            continue;
        uint32_t slot;
        special->ReadHashValue(kTrickSlot, &slot, 1);
        if (slot == keyCombo) {
            uint32_t name;
            special->ReadHashValue(kTrickName, &name, 1);
            return static_cast<int>(name);
        }
    }
    return index;
}

// 0x00562250
bool PointTally::IsTrickAt(int index, uint32_t name, int spin, bool perfect)
{
    if (index >= m_tricks)
        return false;
    STrickLog* trick = Trick(index);
    if (trick->m_label != name)
        return false;
    if (!spin)
        return true;
    if (spin != trick->m_twistRate)
        return false;
    if (!perfect)
        return true;
    int degrees = trick->m_rotationDeg < 0 ? -trick->m_rotationDeg : trick->m_rotationDeg;
    int off = degrees - spin * 180;
    if (off < 0)
        off = -off;
    return off < Scripts::FetchIntValue(kPerfectLandingSlop);
}

// 0x005622C0
int PointTally::CountTrick(uint32_t name, int spin, bool perfect)
{
    int count = 0;
    int shared = 1;
    for (int i = 0; i < m_tricks; ++i) {
        STrickLog* trick = Trick(i);
        if (trick->m_flagBits & kTrickNoSpin)
            shared = 1;
        else if (i == 0 || (Trick(i - 1)->m_flagBits & kTrickNoSpin))
            shared = trick->m_twistRate;
        if (trick->m_label != name)
            continue;
        if (spin) {
            if (spin != shared)
                continue;
            if (perfect) {
                int degrees = trick->m_rotationDeg < 0 ? -trick->m_rotationDeg : trick->m_rotationDeg;
                int off = degrees - spin * 180;
                if (off < 0)
                    off = -off;
                if (off >= Scripts::FetchIntValue(kPerfectLandingSlop))
                    continue;
            }
        }
        ++count;
    }
    return count;
}

// 0x00562370
int PointTally::CountTricksNamed(const char* text)
{
    int count = 0;
    for (int i = 0; i < m_tricks; ++i)
        if (TextUtil::FindCaseless(Trick(i)->m_text, text))
            ++count;
    return count;
}

// 0x005623B0
bool PointTally::LastTrickIs(uint32_t name, int spin, bool perfect, int taps)
{
    int index = m_tricks;
    do {
        if (index <= 0)
            return false;
        --index;
    } while (!Trick(index)->m_text[0]);
    uint32_t trick = taps > 1 ? TrickNameAfterTaps(name, taps) : name;
    return IsTrickAt(index, trick, spin, perfect);
}

// 0x00562410
bool PointTally::LastTrickIsKeyCombo(uint32_t keyCombo, int spin, bool perfect, int taps)
{
    int index = m_tricks;
    do {
        if (index <= 0)
            return false;
        --index;
    } while (!Trick(index)->m_text[0]);
    uint32_t trick;
    if (!KeyComboName(keyCombo, taps, &trick))
        return false;
    return IsTrickAt(index, trick, spin, perfect);
}

// 0x005624C0
int PointTally::CountTrickAfterTaps(uint32_t name, int spin, bool perfect, int taps)
{
    if (taps > 1)
        name = TrickNameAfterTaps(name, taps);
    return CountTrick(name, spin, perfect);
}

// 0x00562510
int PointTally::CountKeyCombo(uint32_t keyCombo, int spin, bool perfect, int taps)
{
    uint32_t trick;
    if (!KeyComboName(keyCombo, taps, &trick))
        return 0;
    return CountTrick(trick, spin, perfect);
}

namespace {

// 0x005625A0 / 0x00562620's count.
int CountRepeats(PointTally* score, uint32_t name, int spin)
{
    int count = 0;
    bool latest = false;
    for (int i = 0; i < score->m_tricks; ++i) {
        STrickLog* trick = score->Trick(i);
        if (trick->m_label == name) {
            if (!spin || spin == trick->m_twistRate) {
                ++count;
                latest = true;
            }
        } else if (!(trick->m_flagBits & kTrickGap)) {
            latest = false;
        }
    }
    if (latest)
        --count;
    return count;
}

} // namespace

// 0x005625A0
int PointTally::CountTrickRepeats(uint32_t name, int spin, int taps)
{
    uint32_t trick = taps > 1 ? TrickNameAfterTaps(name, taps) : name;
    return CountRepeats(this, trick, spin);
}

// 0x00562620
int PointTally::CountKeyComboRepeats(uint32_t keyCombo, int spin, int taps)
{
    uint32_t trick;
    if (!KeyComboName(keyCombo, taps, &trick))
        return 0;
    return CountRepeats(this, trick, spin);
}

// 0x005626E0
void PointTally::RefreshBalanceMeter() { SetupBalanceMeter(); }

// 0x00562740
bool PointTally::IsGap(int index) { return (Trick(index)->m_flagBits >> 4) & 1; }

// 0x00562760
void PointTally::SetSpecialActive(bool active)
{
    if (active == m_specialActive)
        return;
    m_specialActive = active;
    if (GameObjects::CRider* skater = RiderForSlot(m_riderNo))
        skater->Announce(m_specialActive ? kSkaterEnterSpecial : kSkaterExitSpecial, nullptr, 0.0f);
}

// 0x005627B0
void PointTally::RefreshBotCheck(int line)
{
    if (line >= 2500) {
        m_robotFactor = 1.0f;
        return;
    }
    ++m_robotLines;
    ++m_robotComboLines;
    uint8_t& count = m_lineCounts[line];
    if (!count) {
        ++m_robotNewLines;
        ++m_robotComboNewLines;
    }
    if (count < 0xFF)
        ++count;
    uint8_t& comboCount = m_comboLineCounts[line];
    if (!comboCount)
        ++m_robotComboFirstLines;
    if (comboCount < 0xFF)
        ++comboCount;
    int kickIn = Scripts::FetchIntValue(kRobotKickInCount);
    if (comboCount <= kickIn) {
        m_robotFactor = 1.0f;
        return;
    }
    double factor = static_cast<double>(21 - count) * static_cast<double>(0.05f) / (comboCount - kickIn + 1);
    m_robotFactor = factor < static_cast<double>(0.1f) ? 0.1f : static_cast<float>(factor);
}

// 0x005629D0
void PointTally::TrimLog()
{
    STrickLog merged = {};
    std::memcpy(merged.m_text, "... + ", 7);  // "... + "
    std::memcpy(merged.m_shown, "...\\_+\\_", 9); // "...\_+\_"
    int end = m_log.m_num;
    for (int i = 0; i < m_log.m_num; ++i) {
        if (i != 0 && !Trick(i)->m_text[0]) {
            end = i;
            break;
        }
    }
    if (end > m_spinTrick || end > m_lastNoSpin)
        return;
    merged.m_pointsHeld = GetScoreOfRange(0, end);
    // (entries 1 to `end` taken out - one more than the points summed, as the original)
    for (int left = end; left > 0; --left) {
        MemSys::Dealloc(m_log.m_items[1]);
        for (int i = 1; i < m_log.m_num - 1; ++i)
            m_log.m_items[i] = m_log.m_items[i + 1];
        m_log.m_items[m_log.m_num - 1] = nullptr;
        --m_log.m_num;
        --m_tricks;
        --m_spinTrick;
        --m_lastNoSpin;
    }
    *Trick(0) = merged;
    Trick(0)->m_flagBits |= kTrickNoSpin;
}

// 0x00562B20
void PointTally::MoveCaptionTimer(int skater) { RunTrickTextScript(kTrickTextCountdown, skater); }

// 0x00562C10
void PointTally::MoveCaptionLanded(int skater) { RunTrickTextScript(kTrickTextLanded, skater); }

// 0x00562D00
void PointTally::MoveCaptionWipeout(int skater) { RunTrickTextScript(kTrickTextBail, skater); }

// 0x005638A0
void PointTally::MoveCaptionPulse(int skater) { RunTrickTextScript(kTrickTextPulse, skater); }

// 0x00562DF0
void PointTally::FillSpecial()
{
    SetSpecialActive(true); // (inlined there)
    m_special = 3000;
}

// 0x005633B0
int PointTally::CountNonGapTricks()
{
    int count = 0;
    for (int i = 0; i < m_tricks; ++i)
        if (!((Trick(i)->m_flagBits >> 4) & 1))
            ++count;
    return count;
}

} // namespace Mdl
