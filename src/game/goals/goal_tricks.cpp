// GoalSystem::CObjective and tricks: key combos picked, the texts' trick tokens, special trick slots as rewards, the trick
// objects coloured (0x00555A60-0x00555DEF, 0x005565C0-0x0055669F, 0x005568B0-0x00556F8F, 0x00557AC0-0x00557E2F;
// see objective.h).

#include <cstdint>

#include "game/goals/objective.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "engine/maths/random.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"

namespace GoalSystem {

namespace {

using namespace GoalCalls;
using namespace GoalNames;
using GameObjects::CRiderIdentity;

constexpr uint32_t kButtonSequences = 0x79704516;           // key_combos
constexpr uint32_t kButtonSeqText = 0x52E2851E;      // key_combo_string
constexpr uint32_t kKeyCombo = 0xACFDB27A;            // key_combo
constexpr uint32_t kTrickMapping = 0xD544AA2D;        // trick_mapping
constexpr uint32_t kGoalNoValidKeyCombos = 0xF3C20AF5; // goal_no_valid_key_combos
constexpr uint32_t kTetrisTrickText = 0x8856C817;     // goal_tetris_trick_text
constexpr uint32_t kName = 0xA1DC81F9;                // Name
constexpr uint32_t kPremadeCats = 0xFFD7913F;         // Premade_CATS
constexpr uint32_t kParams = 0x7031F10C;              // params
constexpr uint32_t kShouldRemoveTrick = 0xBC678826;   // should_remove_trick
constexpr uint32_t kTrickName = 0xEF6FB249;           // trick_name
constexpr uint32_t kUnassigned = 0xF60C9090;          // Unassigned
constexpr uint32_t kId = 0x40C698AF;                  // id
constexpr uint32_t kPremadeCatIndex = 0x0B502200;     // premade_cat_index
constexpr uint32_t kGoalDescription = 0x0C5D7E6B;     // goal_deScription
constexpr uint32_t kViewGoalsText = 0x4BC5229D;       // view_goals_text
constexpr uint32_t kViewGoalsTip = 0x63981D6F;        // view_goals_tip
constexpr uint32_t kGoalText = 0xDA441D9A;            // goal_text
constexpr uint32_t kKeyComboText = 0x1B3B4DD8;        // key_combo_text
constexpr uint32_t kOriginalGoalDescription = 0x01F238B1; // original_goal_description
constexpr uint32_t kOriginalViewGoalsText = 0x89E9C617;   // original_view_goals_text
constexpr uint32_t kOriginalViewGoalsTip = 0x988330D3;    // original_view_goals_tip
constexpr uint32_t kOriginalGoalText = 0xB0D7628F;        // original_goal_text
constexpr uint32_t kOriginalKeyComboText = 0xE0206064;    // original_key_combo_text
constexpr uint32_t kCat = 0x61A1BC57;                 // cat
constexpr uint32_t kAir = 0x439F4704;                 // AIR
constexpr uint32_t kLip = 0xA549B57B;                 // lip
constexpr uint32_t kManual = 0xEF24413B;              // Manual
constexpr uint32_t kSpecialTricksAir = 0x4764231D;    // goal_special_tricks_air
constexpr uint32_t kSpecialTricksGrind = 0xF481D0CD;  // goal_special_tricks_grind
constexpr uint32_t kSpecialTricksManual = 0xD72D5CF7; // goal_special_tricks_manual
constexpr uint32_t kSpecialTricksLip = 0xA1B2D162;    // goal_special_tricks_lip
constexpr uint32_t kTrickslot = 0xA92A2280;           // trickslot
constexpr uint32_t kIndex = 0x7F8C98FE;               // index
constexpr uint32_t kSpecialTrickIndex = 0x7F264BE9;   // special_trick_index
constexpr uint32_t kLoadPremadeCat = 0x466576C8;      // load_premade_cat

constexpr int kLabelPoints = 0x200; // the texts' buffer

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* structure = Scripts::SpawnParamBag();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
Scripts::CArray* AllocScriptArray()
{
    Scripts::CArray* array = Scripts::AllocScriptArray();
    return array ? array->PrepareAll() : nullptr;
}
// An array a structure has copied, or a temporary one: its contents and itself freed.
void FreeArray(Scripts::CArray* array)
{
    Scripts::ReleaseArrayStorage(array);
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
}
CRiderIdentity* CurrentProfile() { return GameObjects::g_profileRoster->FetchActiveAccount(); }

} // namespace

// 0x005565C0
void CObjective::NoComboSequenceLeft()
{
    Scripts::CStruct* params = SpawnParamTable();
    uint32_t id = 0;
    m_args->ReadHashValue(kGoalIdName, &id, 1);
    params->PutNameKey(kGoalIdChecksum, id);
    Scripts::LaunchScript(kGoalNoValidKeyCombos, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// 0x005568B0
int CObjective::PickKeyCombo(Scripts::CArray* combos)
{
    Scripts::CStruct* mapping = SkateCalls::GetProfileFromParams(nullptr)->FetchDetailsParams(kTrickMapping);
    uint32_t count = combos->m_extent;
    uint32_t combo = 0; // (kept from one look-up to the next, as the original's)
    auto comboAt = [combos, &combo](uint32_t index) {
        if (combos->m_kindTag == Scripts::QBVAL_NAME)
            combo = combos->FetchPart(index);
        else
            reinterpret_cast<Scripts::CStruct*>(combos->FetchPart(index))->ReadHashValue(kKeyCombo, &combo, 1);
        return combo;
    };
    uint32_t trick = 0;
    int picked = 0;
    for (int tries = 100; tries; --tries) {
        picked = Maths::Rnd(static_cast<int>(count));
        comboAt(static_cast<uint32_t>(picked));
        mapping->ReadHashValue(combo, &trick, 0);
        if (trick)
            return picked;
        int slot;
        if (mapping->FetchIntValue(combo, &slot, 0))
            return picked;
        trick = 0;
    }
    // Then each in turn (one with a trick returns the last random pick, as the original).
    for (uint32_t i = 0; i < count; ++i) {
        mapping->ReadHashValue(comboAt(i), &trick, 0);
        if (trick)
            return picked;
        trick = 0;
    }
    NoComboSequenceLeft();
    return -1;
}

// 0x00555B50
bool CObjective::BuildTrickTexts(Scripts::CArray* keyCombos, Scripts::CArray* keyTexts, Scripts::CArray* trickTexts,
                            int premadeCat)
{
    Scripts::CStruct* comboTexts = Scripts::ReadSubStruct(kTetrisTrickText);
    void* skate = SkateCalls::Skate();
    CRiderIdentity* profile = SkateCalls::GetProfileFromParams(nullptr);
    Scripts::CStruct* mapping = profile->FetchDetailsParams(kTrickMapping);
    int count = static_cast<int>(keyCombos->m_extent);
    const char* trickText = nullptr; // (a slot with no local skater keeps the last one, as the original)
    for (int i = 0; i < count; ++i) {
        uint32_t combo = keyCombos->FetchPart(static_cast<uint32_t>(i));
        uint32_t trick = 0;
        int slot = 0;
        bool inSlot = false;
        if (mapping->FetchIntValue(combo, &slot, 0)) {
            inSlot = true; // (a trick slot of the skater's)
        } else if (!mapping->ReadHashValue(combo, &trick, 0)) {
            // A special trick on these keys?
            int specials = profile->GetMaxSpecials();
            int k = 0;
            for (; k < specials; ++k) {
                CRiderIdentity::SpecialTrick special;
                profile->GetSpecialTrick(&special, k);
                if (special.keys == combo) {
                    trick = special.trick;
                    break;
                }
            }
            if (k == specials)
                return false;
        }
        const char* keyText;
        comboTexts->FetchText(combo, &keyText, 1);
        if (inSlot) {
            GameObjects::CGameEntity* skater = static_cast<GameModules::Skate*>(skate)->GetHomeRider();
            if (skater) {
                auto* trickObject = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(skater) + slot * 4 + 0x17C);
                (*reinterpret_cast<Scripts::CStruct**>(trickObject + 4))->FetchText(kName, &trickText, 1);
            }
        } else if (premadeCat > -1) {
            auto* cat = reinterpret_cast<Scripts::CStruct*>(
                Scripts::FetchList(kPremadeCats)->FetchPart(static_cast<uint32_t>(premadeCat)));
            cat->FetchText(kName, &trickText, 1);
        } else {
            Scripts::CStruct* params;
            Scripts::ReadSubStruct(trick)->ReadSubStruct(kParams, &params, 1);
            params->FetchText(kName, &trickText, 1);
        }
        keyTexts->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(Scripts::SpawnText(keyText)));
        trickTexts->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(Scripts::SpawnText(trickText)));
    }
    return true;
}

// 0x00556A00
bool CObjective::SwapMoveCaption()
{
    Scripts::CArray* combos = nullptr;
    if (!m_args->FetchList(kButtonSequences, &combos, 0))
        return false;
    // The texts as written, kept the first time.
    const char* text;
    if (!m_args->HasEntryNamed(kOriginalGoalDescription)) {
        Scripts::CArray* lines;
        if (m_args->FetchText(kGoalDescription, &text, 0))
            m_args->AppendWord(kOriginalGoalDescription, text);
        else if (m_args->FetchList(kGoalDescription, &lines, 0))
            m_args->AppendArrayValue(kOriginalGoalDescription, lines);
    }
    struct Original {
        uint32_t original;
        uint32_t text;
    };
    static constexpr Original kKept[] = {{kOriginalViewGoalsText, kViewGoalsText},
                                         {kOriginalViewGoalsTip, kViewGoalsTip},
                                         {kOriginalGoalText, kGoalText},
                                         {kOriginalKeyComboText, kKeyComboText}};
    for (const Original& kept : kKept) {
        if (!m_args->HasEntryNamed(kept.original) && m_args->FetchText(kept.text, &text, 0))
            m_args->AppendWord(kept.original, text);
    }
    int premadeCat = -1;
    if (m_args->HasBareFlag(kCreateATrick))
        m_args->FetchIntValue(kPremadeCatIndex, &premadeCat, 1);
    Scripts::CArray* keyTexts = AllocScriptArray();
    Scripts::CArray* trickTexts = AllocScriptArray();
    uint32_t count = combos->m_extent;
    keyTexts->ConfigureSizeKind(count, Scripts::QBVAL_STRING);
    trickTexts->ConfigureSizeKind(count, Scripts::QBVAL_STRING);
    if (!BuildTrickTexts(combos, keyTexts, trickTexts, premadeCat)) {
        Scripts::ReleaseArrayStorage(keyTexts);
        Scripts::ReleaseArrayStorage(trickTexts);
        if (keyTexts) {
            keyTexts->Clear();
            Scripts::DeleteArray(keyTexts);
        }
        if (trickTexts) {
            trickTexts->Clear();
            Scripts::DeleteArray(trickTexts);
        }
        return false;
    }
    char buffer[kLabelPoints];
    text = nullptr;
    Scripts::CArray* lines;
    if (m_args->FetchText(kOriginalGoalDescription, &text, 0)) {
        ReplaceTrickTokens(text, buffer, keyTexts, trickTexts, kLabelPoints);
        // (the original adds the description as written, not the replaced text)
        m_args->AppendWord(kGoalDescription, text);
    } else if (m_args->FetchList(kOriginalGoalDescription, &lines, 0)) {
        uint32_t size = lines->m_extent;
        Scripts::CArray* replaced = AllocScriptArray();
        replaced->ConfigureSizeKind(size, Scripts::QBVAL_STRING);
        for (uint32_t i = 0; i < size; ++i) {
            ReplaceTrickTokens(lines->FindInlineText(i), buffer, keyTexts, trickTexts, kLabelPoints);
            replaced->SetElement(i, reinterpret_cast<uint32_t>(Scripts::SpawnText(buffer)));
        }
        m_args->AppendArrayValue(kGoalDescription, replaced);
        FreeArray(replaced);
    }
    for (const Original& kept : kKept) {
        if (m_args->FetchText(kept.original, &text, 0)) {
            ReplaceTrickTokens(text, buffer, keyTexts, trickTexts, kLabelPoints);
            m_args->AppendWord(kept.text, buffer);
        }
    }
    Scripts::ReleaseArrayStorage(keyTexts);
    Scripts::ReleaseArrayStorage(trickTexts);
    if (keyTexts) {
        keyTexts->Clear();
        Scripts::DeleteArray(keyTexts);
    }
    if (trickTexts) {
        trickTexts->Clear();
        Scripts::DeleteArray(trickTexts);
    }
    return true;
}

// 0x00555A60
void CObjective::RemoveRewardTrick()
{
    CRiderIdentity* profile = CurrentProfile();
    int remove;
    m_args->FetchIntValue(kShouldRemoveTrick, &remove, 1);
    if (remove == 1) {
        uint32_t trick;
        if (m_args->HasBareFlag(kCreateATrick))
            trick = 0;
        else
            m_args->ReadHashValue(kTrickName, &trick, 1);
        for (int i = 0; i < 12; ++i) {
            CRiderIdentity::SpecialTrick special;
            profile->GetSpecialTrick(&special, i);
            if (special.trick == trick) {
                CRiderIdentity::SpecialTrick empty;
                empty.MakeNew();
                empty.trick = kUnassigned;
                empty.keys = kUnassigned;
                profile->SetSpecialTrick(i, &empty, true);
                m_args->PutInt(kShouldRemoveTrick, 0);
                break;
            }
        }
        profile->AddMaxSpecials(-1);
    }
    m_args->EraseModule(kButtonSequences);
    m_args->EraseModule(kButtonSeqText);
}

// 0x00555D40
void CObjective::TintTaggedProps(int player, bool clear)
{
    GameObjects::CTrickSpotHub* objects = GameObjects::g_trickSpotHub;
    Scripts::CArray* clusters = nullptr;
    if (!m_args->FetchList(kKillClusters, &clusters, 0))
        return;
    int count = static_cast<int>(clusters->m_extent);
    for (int i = 0; i < count; ++i) {
        uint32_t id;
        reinterpret_cast<Scripts::CStruct*>(clusters->FetchPart(static_cast<uint32_t>(i)))->ReadHashValue(kId, &id, 1);
        if (!objects->GetCluster(id))
            continue;
        if (clear) {
            if (GameObjects::CTrickObjectCluster* cluster = objects->GetCluster(id))
                cluster->ClearGraffitiColor(player);
        } else {
            objects->GetCluster(id)->SetGraffitiColor(player);
        }
    }
}

// 0x00557AC0
bool CObjective::AwardSpecialTrick()
{
    bool cat = m_args->HasBareFlag(kCreateATrick);
    uint32_t type;
    m_args->ReadHashValue(kTrickType, &type, 1);
    uint32_t trick = 0;
    int premade = 0;
    const char* name;
    if (cat) {
        m_args->FetchIntValue(kPremadeCatIndex, &premade, 1);
        auto* premadeCat = reinterpret_cast<Scripts::CStruct*>(
            Scripts::FetchList(kPremadeCats)->FetchPart(static_cast<uint32_t>(premade)));
        premadeCat->FetchText(kName, &name, 1);
    } else {
        m_args->ReadHashValue(kTrickName, &trick, 1);
        Scripts::CStruct* params;
        Scripts::ReadSubStruct(trick)->ReadSubStruct(kParams, &params, 1);
        params->FetchText(kName, &name, 1);
    }
    m_args->AppendWord(kTrickString, name);
    // The key combos free for the trick's type (a create-a-trick: an air trick's).
    Scripts::CStruct* free = nullptr;
    switch (type) {
    case kCat:
    case kAir: free = Scripts::ReadSubStruct(kSpecialTricksAir); break;
    case kGrind: free = Scripts::ReadSubStruct(kSpecialTricksGrind); break;
    case kLip: free = Scripts::ReadSubStruct(kSpecialTricksLip); break;
    case kManual: free = Scripts::ReadSubStruct(kSpecialTricksManual); break;
    default: break;
    }
    CRiderIdentity* profile = CurrentProfile();
    int slots = profile->GetMaxSpecials();
    uint32_t keys = 0;
    bool had = false;
    for (int i = 0; i < slots; ++i) {
        CRiderIdentity::SpecialTrick special;
        profile->GetSpecialTrick(&special, i);
        if (!cat && special.trick == trick) {
            keys = special.keys; // (the skater has it already: on its keys)
            had = true;
            break;
        }
        free->RemoveFlag(special.keys);
    }
    if (!had) {
        keys = free->StepToEntry(nullptr)->raw;
        profile->AddMaxSpecials(1);
        if (cat) {
            Scripts::CStruct* params = SpawnParamTable();
            params->PutNameKey(kTrickslot, keys);
            params->PutInt(kIndex, premade);
            params->PutInt(kSpecialTrickIndex, slots);
            Scripts::LaunchScript(kLoadPremadeCat, params, nullptr, false, nullptr);
            DeleteStructure(params);
        } else {
            CRiderIdentity::SpecialTrick special;
            special.MakeNew();
            special.trick = trick;
            special.keys = keys;
            profile->SetSpecialTrick(slots, &special, true);
        }
        m_args->PutInt(kShouldRemoveTrick, 1);
    }
    m_args->EraseModule(kButtonSequences);
    Scripts::CArray* combos = AllocScriptArray();
    combos->ConfigureSizeKind(1, Scripts::QBVAL_NAME);
    combos->SetElement(0, keys);
    m_args->AppendArrayRef(kButtonSequences, combos);
    SwapMoveCaption();
    return !had;
}

} // namespace Game
