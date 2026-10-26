// Mdl::Score's queries and displays, and the trick log's table (see score.h).

#include "game/skate/score.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/front/text_block.h"
#include "game/front/text_element.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score_internal.h"
#include "hook/hook.h"

namespace Lst {

// 0x00561470
DynamicTable* DynamicTable::Construct(int capacity, int grow)
{
    m_capacity = capacity;
    m_grow = grow;
    m_items = static_cast<void**>(Mem::Malloc(static_cast<unsigned int>(capacity) * 4));
    for (int i = 0; i < m_capacity; ++i)
        m_items[i] = nullptr;
    m_count = 0;
    return this;
}

// 0x005614C0
void DynamicTable::DestroyBody() { Mem::FreeMalloc(m_items); }

// 0x005614D0
void DynamicTable::Add(void* item)
{
    if (m_capacity <= m_count) {
        int capacity = m_capacity + m_grow;
        auto** items = static_cast<void**>(Mem::Malloc(static_cast<unsigned int>(capacity) * 4));
        for (int i = 0; i < m_capacity; ++i)
            items[i] = m_items[i];
        for (int i = m_capacity; i < capacity; ++i)
            items[i] = nullptr;
        Mem::FreeMalloc(m_items);
        m_items = items;
        m_capacity = capacity;
    }
    m_items[m_count++] = item;
}

// 0x00562920
void DynamicTableDestroyer::DeleteTableContents()
{
    int count = m_table->m_count;
    for (int i = 0; i < count; ++i)
        Mem::Free(m_table->m_items[i]);
    m_table->m_count = 0;
}

} // namespace Lst

namespace Mdl {

namespace ScoreInternal {

Script::CStruct* NewStructure()
{
    Script::CStruct* made = Script::NewStruct();
    return made ? made->Init() : nullptr;
}

void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
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
int ScreenMode() { return GameFunction<int(__cdecl*)()>(0x004A0710)(); }

// The created trick `index`'s name (the local skater's; 0 without one).
uint32_t CreatedTrickName(int index)
{
    auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
    if (!skater)
        return 0;
    const char* name;
    reinterpret_cast<Script::CStruct*>(*reinterpret_cast<uint32_t*>(skater->m_trickInfo[index] + 4))
        ->GetString(kName, &name, 1);
    return Crc::GenerateCRCFromString(name);
}

// A trick key combo's name for the counts: the mapped trick, else the created trick in that slot (0: none of
// either; `none` when no created trick either).
bool KeyComboName(uint32_t keyCombo, int taps, uint32_t* name)
{
    *name = Score::TrickFromKeyCombo(keyCombo, taps);
    if (*name)
        return true;
    int index = Score::CreatedTrickFromKeyCombo(keyCombo);
    if (index == -1)
        return false;
    *name = CreatedTrickName(index);
    return true;
}

// The trick text script `script` with the skater number's ids.
void RunTrickTextScript(uint32_t script, int skater)
{
    Script::CStruct* params = NewStructure();
    params->AddChecksum(kTrickTextContainerId, kTrickTextContainer + static_cast<uint32_t>(skater));
    params->AddChecksum(kTheTrickTextId, kTheTrickText + static_cast<uint32_t>(skater));
    params->AddChecksum(kTheScorePotTextId, kTheScorePotText + static_cast<uint32_t>(skater));
    Script::RunScript(script, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

} // namespace

// 0x00561240
int Score::ScoreIncrement(int value)
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
        call Score::ScoreIncrement
        add esp, 4
        pop edx
        ret
    }
}

// 0x00561290
void Score::SetSkaterId(int16_t id) { m_skaterId = id; }

// 0x005612A0
void Score::EscapeSpaces(char* out, const char* text)
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
void Score::SetSpecialBarColors()
{
    Script::CArray* colors = Script::GetArray(kSpecialBarColors);
    for (uint32_t i = 0; i < 3; ++i) {
        auto* color = reinterpret_cast<Script::CArray*>(colors->GetElement(i));
        for (uint32_t c = 0; c < 4; ++c)
            m_barColors[i][c] = static_cast<uint8_t>(color->GetElement(c));
    }
    m_specialRate = Script::GetFloat(kSpecialBarInterpolatorRate);
}

// 0x00561360
uint32_t Score::TrickNameAfterTaps(uint32_t trick, int taps)
{
    Script::CStruct* params;
    Script::GetStructure(trick)->GetStructure(kParams, &params, 1);
    if (taps > 1) {
        uint32_t extra = static_cast<uint32_t>(taps); // (the slot the original reuses: kept when ExtraTricks is missing)
        for (int left = taps - 1; left; --left) {
            params->GetChecksum(kExtraTricks, &extra, 0);
            reinterpret_cast<Script::CStruct*>(Script::GetArray(extra)->GetElement(0))->GetStructure(kParams, &params, 1);
        }
    }
    const char* name;
    params->GetString(kName, &name, 1);
    return Crc::GenerateCRCFromString(name);
}

// 0x00561400
void Score::ResetBests()
{
    m_bestTricks = 0;
    m_bestCombo = 0;
    m_bestComboSent = 0;
}

// 0x00561410
void Score::AddBonus(int points) { m_bonus += points; }

// 0x00561420
int Score::GetBonus() { return m_bonus; }

// 0x00561550
int Score::GetScoreOfRange(int first, int last)
{
    int total = 0;
    int spin = 2;
    for (int i = first; i < last; ++i) {
        STrickLog* trick = Trick(i);
        if (trick->m_flags & kTrickNoSpin)
            spin = 2;
        else if (i == 0 || (Trick(i - 1)->m_flags & kTrickNoSpin))
            spin = SpinMultiplier(trick->m_spin);
        int points = trick->m_score * Reduction(trick->m_repeats) * spin / 200; // (percent times half the multiplier)
        if (trick->m_kind)
            points = points * 120 / 100;
        total += points;
    }
    return total;
}

// 0x00561640
uint32_t Score::GetLastTrickName() { return m_log.m_count ? LastTrick()->m_name : 0; }

// 0x00561660
uint32_t Score::GetTrickName(int index) { return Trick(index)->m_name; }

// 0x00561670
void Score::DispatchTrickSequence(bool clear)
{
    Obj::CSkater* skater = SkaterById(m_skaterId);
    int number = skater ? skater->GetSkaterNumber() : 0;
    auto* text = static_cast<Front::CTextBlockElement*>(FindElement(kTheTrickText + static_cast<uint32_t>(number)));
    if (!text)
        return;
    const char* lines[0xFD];
    if (!clear) {
        for (int i = 0; i < m_log.m_count; ++i)
            lines[i] = Trick(i)->m_shown;
        text->SetText(lines, m_log.m_count);
    } else {
        text->SetText(lines, 0);
    }
    Script::RunScript(kUpdateTrickText, nullptr, nullptr, false, nullptr);
}

// 0x00561730
void Score::SetupBalanceMeter()
{
    Script::CStruct* info = Script::GetStructure(kBalanceMeterInfo);
    Script::CArray* arrows = nullptr;
    info->GetArray(kArrowPositions, &arrows, 0);
    m_arrowCount = static_cast<int>(arrows->m_size); // (no check: as the original)
    for (int i = 0; i < m_arrowCount; ++i)
        m_arrows[i] = *reinterpret_cast<Script::CPair*>(arrows->GetElement(static_cast<uint32_t>(i)));
    m_arrows[m_arrowCount] = m_arrows[m_arrowCount - 1];
    m_arrowStep = static_cast<float>(1.0 / m_arrowCount);
    Script::CArray* bars = nullptr;
    uint32_t name = kBarPositions;
    if (static_cast<uint8_t>(CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr)) && GameMode()->GetNameChecksum() != kHorse &&
        GameMode()->GetNameChecksum() != kNetHorse)
        name = ScreenMode() == 1 ? kBarPositionsMpV : kBarPositionsMpH;
    info->GetArray(name, &bars, 0);
    for (uint32_t i = 0; i < 2; ++i)
        m_bars[i] = *reinterpret_cast<Script::CPair*>(bars->GetElement(i));
}

// 0x00561890
void Score::DispatchScorePot(int points, float multiplier)
{
    Obj::CSkater* skater = SkaterById(m_skaterId);
    int number = skater ? skater->GetSkaterNumber() : 0;
    auto* text = static_cast<Front::CTextElement*>(FindElement(kTheScorePotText + static_cast<uint32_t>(number)));
    char shown[0x80];
    if (!points) {
        shown[0] = ' ';
        shown[1] = 0;
    } else if (multiplier == 0.0f) {
        std::snprintf(shown, sizeof(shown), Text(0x00646B8C), Str::FormatWithSeparators(points)); // "%s"
    } else {
        int whole = Ftol(multiplier);
        if (static_cast<double>(whole) == multiplier)
            std::snprintf(shown, sizeof(shown), Text(0x0064E2A0), Str::FormatWithSeparators(points), whole); // "%s X %d"
        else
            std::snprintf(shown, sizeof(shown), Text(0x0064E294), Str::FormatWithSeparators(points),
                          static_cast<double>(multiplier)); // "%s X %.1f"
    }
    text->SetText(shown); // (no check: as the original)
    Script::RunScript(kUpdateScorepot, nullptr, nullptr, false, nullptr);
}

// 0x005619E0
void Score::CheckHighScore(int before, int after)
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
        SkaterById(m_skaterId)->SpawnAndRunScript(script, 0, false, false, nullptr);
    }
}

// 0x00561A80
void Score::SetTotalScore(int total)
{
    m_total = total;
    Obj::CSkater* skater = SkaterById(m_skaterId);
    if (!skater->m_unknownBC) // (no check: as the original)
        return;
    char shown[0x40];
    if (GameMode()->GetNameChecksum() == kNetKing || GameMode()->GetNameChecksum() == kKing) {
        uint32_t seconds = static_cast<uint32_t>(m_total) / 1000;
        std::snprintf(shown, sizeof(shown), Text(0x0064E2A8), seconds / 60, seconds % 60); // "%d:%.2d"
    } else {
        std::snprintf(shown, sizeof(shown), Text(0x00645394), m_total); // "%d"
    }
    Front::SetScoreText(shown, skater->GetSkaterNumber());
}

// 0x00561B40
int Score::GetTotalScore()
{
    if (NetManager()->OnServer() && (Script::GetInteger(kScoreByClusters) || GameMode()->IsTrue(kShouldModulateColor)))
        return TrickObjects()->CountClustersHeldBy(m_skaterId);
    return m_total;
}

// 0x00562080
uint32_t Score::TrickFromKeyCombo(uint32_t keyCombo, int taps)
{
    Obj::CSkaterProfile* profile = SkateCalls::GetProfileFromParams(nullptr);
    Script::CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    uint32_t trick = 0;
    if (!mapping->GetChecksum(keyCombo, &trick, 0)) {
        Script::CArray* specials;
        profile->GetSpecials()->GetArray(0u, &specials, 1);
        int count = static_cast<int>(specials->m_size);
        for (int i = 0; i < count; ++i) {
            auto* special = reinterpret_cast<Script::CStruct*>(specials->GetElement(static_cast<uint32_t>(i)));
            uint32_t slot;
            special->GetChecksum(kTrickSlot, &slot, 1);
            if (slot == keyCombo) {
                special->GetChecksum(kTrickName, &trick, 1);
                break;
            }
        }
    }
    if (!trick)
        return 0;
    return TrickNameAfterTaps(trick, taps);
}

// 0x00562150
int Score::CreatedTrickFromKeyCombo(uint32_t keyCombo)
{
    Obj::CSkaterProfile* profile = SkateCalls::GetProfileFromParams(nullptr);
    Script::CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    int index = -1;
    if (mapping->GetInteger(keyCombo, &index, 0))
        return index;
    Script::CArray* specials;
    profile->GetSpecials()->GetArray(0u, &specials, 1);
    int count = static_cast<int>(specials->m_size);
    for (int i = 0; i < count; ++i) {
        auto* special = reinterpret_cast<Script::CStruct*>(specials->GetElement(static_cast<uint32_t>(i)));
        int isCat = 0;
        special->GetInteger(kIsCat, &isCat, 0);
        if (!isCat)
            continue;
        uint32_t slot;
        special->GetChecksum(kTrickSlot, &slot, 1);
        if (slot == keyCombo) {
            uint32_t name;
            special->GetChecksum(kTrickName, &name, 1);
            return static_cast<int>(name);
        }
    }
    return index;
}

// 0x00562250
bool Score::IsTrickAt(int index, uint32_t name, int spin, bool perfect)
{
    if (index >= m_tricks)
        return false;
    STrickLog* trick = Trick(index);
    if (trick->m_name != name)
        return false;
    if (!spin)
        return true;
    if (spin != trick->m_spin)
        return false;
    if (!perfect)
        return true;
    int degrees = trick->m_spinDegrees < 0 ? -trick->m_spinDegrees : trick->m_spinDegrees;
    int off = degrees - spin * 180;
    if (off < 0)
        off = -off;
    return off < Script::GetInteger(kPerfectLandingSlop);
}

// 0x005622C0
int Score::CountTrick(uint32_t name, int spin, bool perfect)
{
    int count = 0;
    int shared = 1;
    for (int i = 0; i < m_tricks; ++i) {
        STrickLog* trick = Trick(i);
        if (trick->m_flags & kTrickNoSpin)
            shared = 1;
        else if (i == 0 || (Trick(i - 1)->m_flags & kTrickNoSpin))
            shared = trick->m_spin;
        if (trick->m_name != name)
            continue;
        if (spin) {
            if (spin != shared)
                continue;
            if (perfect) {
                int degrees = trick->m_spinDegrees < 0 ? -trick->m_spinDegrees : trick->m_spinDegrees;
                int off = degrees - spin * 180;
                if (off < 0)
                    off = -off;
                if (off >= Script::GetInteger(kPerfectLandingSlop))
                    continue;
            }
        }
        ++count;
    }
    return count;
}

// 0x00562370
int Score::CountTricksNamed(const char* text)
{
    int count = 0;
    for (int i = 0; i < m_tricks; ++i)
        if (Str::FindCaseless(Trick(i)->m_text, text))
            ++count;
    return count;
}

// 0x005623B0
bool Score::LastTrickIs(uint32_t name, int spin, bool perfect, int taps)
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
bool Score::LastTrickIsKeyCombo(uint32_t keyCombo, int spin, bool perfect, int taps)
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
int Score::CountTrickAfterTaps(uint32_t name, int spin, bool perfect, int taps)
{
    if (taps > 1)
        name = TrickNameAfterTaps(name, taps);
    return CountTrick(name, spin, perfect);
}

// 0x00562510
int Score::CountKeyCombo(uint32_t keyCombo, int spin, bool perfect, int taps)
{
    uint32_t trick;
    if (!KeyComboName(keyCombo, taps, &trick))
        return 0;
    return CountTrick(trick, spin, perfect);
}

namespace {

// 0x005625A0 / 0x00562620's count.
int CountRepeats(Score* score, uint32_t name, int spin)
{
    int count = 0;
    bool latest = false;
    for (int i = 0; i < score->m_tricks; ++i) {
        STrickLog* trick = score->Trick(i);
        if (trick->m_name == name) {
            if (!spin || spin == trick->m_spin) {
                ++count;
                latest = true;
            }
        } else if (!(trick->m_flags & kTrickGap)) {
            latest = false;
        }
    }
    if (latest)
        --count;
    return count;
}

} // namespace

// 0x005625A0
int Score::CountTrickRepeats(uint32_t name, int spin, int taps)
{
    uint32_t trick = taps > 1 ? TrickNameAfterTaps(name, taps) : name;
    return CountRepeats(this, trick, spin);
}

// 0x00562620
int Score::CountKeyComboRepeats(uint32_t keyCombo, int spin, int taps)
{
    uint32_t trick;
    if (!KeyComboName(keyCombo, taps, &trick))
        return 0;
    return CountRepeats(this, trick, spin);
}

// 0x005626E0
void Score::RefreshBalanceMeter() { SetupBalanceMeter(); }

// 0x00562740
bool Score::IsGap(int index) { return (Trick(index)->m_flags >> 4) & 1; }

// 0x00562760
void Score::SetSpecialActive(bool active)
{
    if (active == m_specialActive)
        return;
    m_specialActive = active;
    if (Obj::CSkater* skater = SkaterById(m_skaterId))
        skater->BroadcastEvent(m_specialActive ? kSkaterEnterSpecial : kSkaterExitSpecial, nullptr, 0.0f);
}

// 0x005627B0
void Score::UpdateRobotDetection(int line)
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
    int kickIn = Script::GetInteger(kRobotKickInCount);
    if (comboCount <= kickIn) {
        m_robotFactor = 1.0f;
        return;
    }
    double factor = static_cast<double>(21 - count) * static_cast<double>(0.05f) / (comboCount - kickIn + 1);
    m_robotFactor = factor < static_cast<double>(0.1f) ? 0.1f : static_cast<float>(factor);
}

// 0x005629D0
void Score::TrimLog()
{
    STrickLog merged = {};
    std::memcpy(merged.m_text, Text(0x0064E2D8), 7);  // "... + "
    std::memcpy(merged.m_shown, Text(0x0064E2CC), 9); // "...\_+\_"
    int end = m_log.m_count;
    for (int i = 0; i < m_log.m_count; ++i) {
        if (i != 0 && !Trick(i)->m_text[0]) {
            end = i;
            break;
        }
    }
    if (end > m_spinTrick || end > m_lastNoSpin)
        return;
    merged.m_score = GetScoreOfRange(0, end);
    // (entries 1 to `end` taken out - one more than the points summed, as the original)
    for (int left = end; left > 0; --left) {
        Mem::Free(m_log.m_items[1]);
        for (int i = 1; i < m_log.m_count - 1; ++i)
            m_log.m_items[i] = m_log.m_items[i + 1];
        m_log.m_items[m_log.m_count - 1] = nullptr;
        --m_log.m_count;
        --m_tricks;
        --m_spinTrick;
        --m_lastNoSpin;
    }
    *Trick(0) = merged;
    Trick(0)->m_flags |= kTrickNoSpin;
}

// 0x00562B20
void Score::TrickTextCountdown(int skater) { RunTrickTextScript(kTrickTextCountdown, skater); }

// 0x00562C10
void Score::TrickTextLanded(int skater) { RunTrickTextScript(kTrickTextLanded, skater); }

// 0x00562D00
void Score::TrickTextBail(int skater) { RunTrickTextScript(kTrickTextBail, skater); }

// 0x005638A0
void Score::TrickTextPulse(int skater) { RunTrickTextScript(kTrickTextPulse, skater); }

// 0x00562DF0
void Score::FillSpecial()
{
    SetSpecialActive(true); // (inlined there)
    m_special = 3000;
}

// 0x005633B0
int Score::CountNonGapTricks()
{
    int count = 0;
    for (int i = 0; i < m_tricks; ++i)
        if (!((Trick(i)->m_flags >> 4) & 1))
            ++count;
    return count;
}

} // namespace Mdl

REPLACE(0x00561470, &Lst::DynamicTable::Construct);
REPLACE(0x005614C0, &Lst::DynamicTable::DestroyBody);
REPLACE(0x005614D0, &Lst::DynamicTable::Add);
REPLACE(0x00562920, &Lst::DynamicTableDestroyer::DeleteTableContents);
REPLACE_UNCHECKED(0x00561240, Mdl::ScoreIncrementStub); // EDX kept
REPLACE(0x00561290, &Mdl::Score::SetSkaterId);
REPLACE(0x005612A0, &Mdl::Score::EscapeSpaces);
REPLACE(0x005612D0, &Mdl::Score::SetSpecialBarColors);
REPLACE(0x00561360, Mdl::Score::TrickNameAfterTaps);
REPLACE(0x00561400, &Mdl::Score::ResetBests);
REPLACE(0x00561410, &Mdl::Score::AddBonus);
REPLACE_SOURCE_ONLY(0x00561420, &Mdl::Score::GetBonus); // 4 bytes
REPLACE(0x00561550, &Mdl::Score::GetScoreOfRange);
REPLACE(0x00561640, &Mdl::Score::GetLastTrickName);
REPLACE(0x00561660, &Mdl::Score::GetTrickName);
REPLACE(0x00561670, &Mdl::Score::DispatchTrickSequence);
REPLACE(0x00561730, &Mdl::Score::SetupBalanceMeter);
REPLACE(0x00561890, &Mdl::Score::DispatchScorePot);
REPLACE(0x005619E0, &Mdl::Score::CheckHighScore);
REPLACE(0x00561A80, &Mdl::Score::SetTotalScore);
REPLACE(0x00561B40, &Mdl::Score::GetTotalScore);
REPLACE(0x00562080, Mdl::Score::TrickFromKeyCombo);
REPLACE(0x00562150, Mdl::Score::CreatedTrickFromKeyCombo);
REPLACE(0x00562250, &Mdl::Score::IsTrickAt);
REPLACE(0x005622C0, &Mdl::Score::CountTrick);
REPLACE(0x00562370, &Mdl::Score::CountTricksNamed);
REPLACE(0x005623B0, &Mdl::Score::LastTrickIs);
REPLACE(0x00562410, &Mdl::Score::LastTrickIsKeyCombo);
REPLACE(0x005624C0, &Mdl::Score::CountTrickAfterTaps);
REPLACE(0x00562510, &Mdl::Score::CountKeyCombo);
REPLACE(0x005625A0, &Mdl::Score::CountTrickRepeats);
REPLACE(0x00562620, &Mdl::Score::CountKeyComboRepeats);
REPLACE_SOURCE_ONLY(0x005626E0, &Mdl::Score::RefreshBalanceMeter); // a jump to 0x00561730
REPLACE(0x00562740, &Mdl::Score::IsGap);
REPLACE(0x00562760, &Mdl::Score::SetSpecialActive);
REPLACE(0x005627B0, &Mdl::Score::UpdateRobotDetection);
REPLACE(0x005629D0, &Mdl::Score::TrimLog);
REPLACE(0x00562B20, &Mdl::Score::TrickTextCountdown);
REPLACE(0x00562C10, &Mdl::Score::TrickTextLanded);
REPLACE(0x00562D00, &Mdl::Score::TrickTextBail);
REPLACE(0x005638A0, &Mdl::Score::TrickTextPulse);
REPLACE(0x00562DF0, &Mdl::Score::FillSpecial);
REPLACE(0x005633B0, &Mdl::Score::CountNonGapTricks);
