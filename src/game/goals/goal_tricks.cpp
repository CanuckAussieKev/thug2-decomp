// Game::CGoal and tricks: key combos picked, the texts' trick tokens, special trick slots as rewards, the trick
// objects coloured (0x00555A60-0x00555DEF, 0x005565C0-0x0055669F, 0x005568B0-0x00556F8F, 0x00557AC0-0x00557E2F;
// see goal.h).

#include <cstdint>

#include "game/goals/goal.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "game/mth/random.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"

namespace Game {

namespace {

using namespace GoalCalls;
using namespace GoalNames;
using Obj::CSkaterProfile;

constexpr uint32_t kKeyCombos = 0x79704516;           // key_combos
constexpr uint32_t kKeyComboString = 0x52E2851E;      // key_combo_string
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

constexpr int kTextSize = 0x200; // the texts' buffer

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
Script::CArray* NewArray()
{
    Script::CArray* array = Script::NewArray();
    return array ? array->Init() : nullptr;
}
// An array a structure has copied, or a temporary one: its contents and itself freed.
void FreeArray(Script::CArray* array)
{
    Script::CleanUpArray(array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}
CSkaterProfile* CurrentProfile() { return Obj::g_profileManager->GetCurrentProfile(); }

} // namespace

// 0x005565C0
void CGoal::NoValidKeyCombos()
{
    Script::CStruct* params = NewStructure();
    uint32_t id = 0;
    m_params->GetChecksum(kGoalIdName, &id, 1);
    params->AddChecksum(kGoalIdChecksum, id);
    Script::RunScript(kGoalNoValidKeyCombos, params, nullptr, false, nullptr);
    DeleteStructure(params);
}

// 0x005568B0
int CGoal::PickKeyCombo(Script::CArray* combos)
{
    Script::CStruct* mapping = SkateCalls::GetProfileFromParams(nullptr)->GetInfoStructure(kTrickMapping);
    uint32_t count = combos->m_size;
    uint32_t combo = 0; // (kept from one look-up to the next, as the original's)
    auto comboAt = [combos, &combo](uint32_t index) {
        if (combos->m_type == Script::ESYMBOLTYPE_NAME)
            combo = combos->GetElement(index);
        else
            reinterpret_cast<Script::CStruct*>(combos->GetElement(index))->GetChecksum(kKeyCombo, &combo, 1);
        return combo;
    };
    uint32_t trick = 0;
    int picked = 0;
    for (int tries = 100; tries; --tries) {
        picked = Mth::Rnd(static_cast<int>(count));
        comboAt(static_cast<uint32_t>(picked));
        mapping->GetChecksum(combo, &trick, 0);
        if (trick)
            return picked;
        int slot;
        if (mapping->GetInteger(combo, &slot, 0))
            return picked;
        trick = 0;
    }
    // Then each in turn (one with a trick returns the last random pick, as the original).
    for (uint32_t i = 0; i < count; ++i) {
        mapping->GetChecksum(comboAt(i), &trick, 0);
        if (trick)
            return picked;
        trick = 0;
    }
    NoValidKeyCombos();
    return -1;
}

// 0x00555B50
bool CGoal::BuildTrickTexts(Script::CArray* keyCombos, Script::CArray* keyTexts, Script::CArray* trickTexts,
                            int premadeCat)
{
    Script::CStruct* comboTexts = Script::GetStructure(kTetrisTrickText);
    void* skate = SkateCalls::Skate();
    CSkaterProfile* profile = SkateCalls::GetProfileFromParams(nullptr);
    Script::CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    int count = static_cast<int>(keyCombos->m_size);
    const char* trickText = nullptr; // (a slot with no local skater keeps the last one, as the original)
    for (int i = 0; i < count; ++i) {
        uint32_t combo = keyCombos->GetElement(static_cast<uint32_t>(i));
        uint32_t trick = 0;
        int slot = 0;
        bool inSlot = false;
        if (mapping->GetInteger(combo, &slot, 0)) {
            inSlot = true; // (a trick slot of the skater's)
        } else if (!mapping->GetChecksum(combo, &trick, 0)) {
            // A special trick on these keys?
            int specials = profile->GetMaxSpecials();
            int k = 0;
            for (; k < specials; ++k) {
                CSkaterProfile::SpecialTrick special;
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
        comboTexts->GetString(combo, &keyText, 1);
        if (inSlot) {
            Obj::CCompositeObject* skater = static_cast<Mdl::Skate*>(skate)->GetLocalSkater();
            if (skater) {
                auto* trickObject = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(skater) + slot * 4 + 0x17C);
                (*reinterpret_cast<Script::CStruct**>(trickObject + 4))->GetString(kName, &trickText, 1);
            }
        } else if (premadeCat > -1) {
            auto* cat = reinterpret_cast<Script::CStruct*>(
                Script::GetArray(kPremadeCats)->GetElement(static_cast<uint32_t>(premadeCat)));
            cat->GetString(kName, &trickText, 1);
        } else {
            Script::CStruct* params;
            Script::GetStructure(trick)->GetStructure(kParams, &params, 1);
            params->GetString(kName, &trickText, 1);
        }
        keyTexts->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(Script::CreateString(keyText)));
        trickTexts->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(Script::CreateString(trickText)));
    }
    return true;
}

// 0x00556A00
bool CGoal::ReplaceTrickText()
{
    Script::CArray* combos = nullptr;
    if (!m_params->GetArray(kKeyCombos, &combos, 0))
        return false;
    // The texts as written, kept the first time.
    const char* text;
    if (!m_params->ContainsComponentNamed(kOriginalGoalDescription)) {
        Script::CArray* lines;
        if (m_params->GetString(kGoalDescription, &text, 0))
            m_params->AddString(kOriginalGoalDescription, text);
        else if (m_params->GetArray(kGoalDescription, &lines, 0))
            m_params->AddArray(kOriginalGoalDescription, lines);
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
        if (!m_params->ContainsComponentNamed(kept.original) && m_params->GetString(kept.text, &text, 0))
            m_params->AddString(kept.original, text);
    }
    int premadeCat = -1;
    if (m_params->ContainsFlag(kCreateATrick))
        m_params->GetInteger(kPremadeCatIndex, &premadeCat, 1);
    Script::CArray* keyTexts = NewArray();
    Script::CArray* trickTexts = NewArray();
    uint32_t count = combos->m_size;
    keyTexts->SetSizeAndType(count, Script::ESYMBOLTYPE_STRING);
    trickTexts->SetSizeAndType(count, Script::ESYMBOLTYPE_STRING);
    if (!BuildTrickTexts(combos, keyTexts, trickTexts, premadeCat)) {
        Script::CleanUpArray(keyTexts);
        Script::CleanUpArray(trickTexts);
        if (keyTexts) {
            keyTexts->Clear();
            Script::DeleteArray(keyTexts);
        }
        if (trickTexts) {
            trickTexts->Clear();
            Script::DeleteArray(trickTexts);
        }
        return false;
    }
    char buffer[kTextSize];
    text = nullptr;
    Script::CArray* lines;
    if (m_params->GetString(kOriginalGoalDescription, &text, 0)) {
        ReplaceTrickTokens(text, buffer, keyTexts, trickTexts, kTextSize);
        // (the original adds the description as written, not the replaced text)
        m_params->AddString(kGoalDescription, text);
    } else if (m_params->GetArray(kOriginalGoalDescription, &lines, 0)) {
        uint32_t size = lines->m_size;
        Script::CArray* replaced = NewArray();
        replaced->SetSizeAndType(size, Script::ESYMBOLTYPE_STRING);
        for (uint32_t i = 0; i < size; ++i) {
            ReplaceTrickTokens(lines->GetLocalString(i), buffer, keyTexts, trickTexts, kTextSize);
            replaced->SetElement(i, reinterpret_cast<uint32_t>(Script::CreateString(buffer)));
        }
        m_params->AddArray(kGoalDescription, replaced);
        FreeArray(replaced);
    }
    for (const Original& kept : kKept) {
        if (m_params->GetString(kept.original, &text, 0)) {
            ReplaceTrickTokens(text, buffer, keyTexts, trickTexts, kTextSize);
            m_params->AddString(kept.text, buffer);
        }
    }
    Script::CleanUpArray(keyTexts);
    Script::CleanUpArray(trickTexts);
    if (keyTexts) {
        keyTexts->Clear();
        Script::DeleteArray(keyTexts);
    }
    if (trickTexts) {
        trickTexts->Clear();
        Script::DeleteArray(trickTexts);
    }
    return true;
}

// 0x00555A60
void CGoal::RemoveRewardTrick()
{
    CSkaterProfile* profile = CurrentProfile();
    int remove;
    m_params->GetInteger(kShouldRemoveTrick, &remove, 1);
    if (remove == 1) {
        uint32_t trick;
        if (m_params->ContainsFlag(kCreateATrick))
            trick = 0;
        else
            m_params->GetChecksum(kTrickName, &trick, 1);
        for (int i = 0; i < 12; ++i) {
            CSkaterProfile::SpecialTrick special;
            profile->GetSpecialTrick(&special, i);
            if (special.trick == trick) {
                CSkaterProfile::SpecialTrick empty;
                empty.Construct();
                empty.trick = kUnassigned;
                empty.keys = kUnassigned;
                profile->SetSpecialTrick(i, &empty, true);
                m_params->AddInteger(kShouldRemoveTrick, 0);
                break;
            }
        }
        profile->AddMaxSpecials(-1);
    }
    m_params->RemoveComponent(kKeyCombos);
    m_params->RemoveComponent(kKeyComboString);
}

// 0x00555D40
void CGoal::ColorTrickObjects(int player, bool clear)
{
    Obj::CTrickObjectManager* objects = Obj::g_trickObjectManager;
    Script::CArray* clusters = nullptr;
    if (!m_params->GetArray(kKillClusters, &clusters, 0))
        return;
    int count = static_cast<int>(clusters->m_size);
    for (int i = 0; i < count; ++i) {
        uint32_t id;
        reinterpret_cast<Script::CStruct*>(clusters->GetElement(static_cast<uint32_t>(i)))->GetChecksum(kId, &id, 1);
        if (!objects->GetCluster(id))
            continue;
        if (clear) {
            if (Obj::CTrickObjectCluster* cluster = objects->GetCluster(id))
                cluster->ClearGraffitiColor(player);
        } else {
            objects->GetCluster(id)->SetGraffitiColor(player);
        }
    }
}

// 0x00557AC0
bool CGoal::AwardSpecialTrick()
{
    bool cat = m_params->ContainsFlag(kCreateATrick);
    uint32_t type;
    m_params->GetChecksum(kTrickType, &type, 1);
    uint32_t trick = 0;
    int premade = 0;
    const char* name;
    if (cat) {
        m_params->GetInteger(kPremadeCatIndex, &premade, 1);
        auto* premadeCat = reinterpret_cast<Script::CStruct*>(
            Script::GetArray(kPremadeCats)->GetElement(static_cast<uint32_t>(premade)));
        premadeCat->GetString(kName, &name, 1);
    } else {
        m_params->GetChecksum(kTrickName, &trick, 1);
        Script::CStruct* params;
        Script::GetStructure(trick)->GetStructure(kParams, &params, 1);
        params->GetString(kName, &name, 1);
    }
    m_params->AddString(kTrickString, name);
    // The key combos free for the trick's type (a create-a-trick: an air trick's).
    Script::CStruct* free = nullptr;
    switch (type) {
    case kCat:
    case kAir: free = Script::GetStructure(kSpecialTricksAir); break;
    case kGrind: free = Script::GetStructure(kSpecialTricksGrind); break;
    case kLip: free = Script::GetStructure(kSpecialTricksLip); break;
    case kManual: free = Script::GetStructure(kSpecialTricksManual); break;
    default: break;
    }
    CSkaterProfile* profile = CurrentProfile();
    int slots = profile->GetMaxSpecials();
    uint32_t keys = 0;
    bool had = false;
    for (int i = 0; i < slots; ++i) {
        CSkaterProfile::SpecialTrick special;
        profile->GetSpecialTrick(&special, i);
        if (!cat && special.trick == trick) {
            keys = special.keys; // (the skater has it already: on its keys)
            had = true;
            break;
        }
        free->RemoveFlag(special.keys);
    }
    if (!had) {
        keys = free->GetNextComponent(nullptr)->raw;
        profile->AddMaxSpecials(1);
        if (cat) {
            Script::CStruct* params = NewStructure();
            params->AddChecksum(kTrickslot, keys);
            params->AddInteger(kIndex, premade);
            params->AddInteger(kSpecialTrickIndex, slots);
            Script::RunScript(kLoadPremadeCat, params, nullptr, false, nullptr);
            DeleteStructure(params);
        } else {
            CSkaterProfile::SpecialTrick special;
            special.Construct();
            special.trick = trick;
            special.keys = keys;
            profile->SetSpecialTrick(slots, &special, true);
        }
        m_params->AddInteger(kShouldRemoveTrick, 1);
    }
    m_params->RemoveComponent(kKeyCombos);
    Script::CArray* combos = NewArray();
    combos->SetSizeAndType(1, Script::ESYMBOLTYPE_NAME);
    combos->SetElement(0, keys);
    m_params->AddArrayPointer(kKeyCombos, combos);
    ReplaceTrickText();
    return !had;
}

} // namespace Game
