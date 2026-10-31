// Script commands (CFunctions) 0x005B0A40-0x005B1B60: configurable tricks, records, soundtracks, controllers (see
// trick_cfuncs.h).

#include "game/script/trick_cfuncs.h"

#include <cstdint>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/front/front_end.h"
#include "game/gfx/texture_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/objman.h"
#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/records.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skater/trick_component.h"
#include "game/skate/score.h"
#include "game/obj/compositemgr.h"
#include "game/core/code_address.h"
#include "game/goals/goal_manager.h"

namespace ScriptCommands {

namespace {

using GameObjects::CRiderIdentity;
using Scripts::CArray;
using Scripts::CStruct;

constexpr uint32_t kProfile = 0x7EA855F0;            // "profile"
constexpr uint32_t kCat = 0x61A1BC57;                // "cat"
constexpr uint32_t kTrickMapping = 0xD544AA2D;       // "trick_mapping"
constexpr uint32_t kTrick = 0x270F56E1;              // "trick"
constexpr uint32_t kCatNum = 0xA75B8581;             // "cat_num"
constexpr uint32_t kSpecial = 0xB394C01C;            // "special"
constexpr uint32_t kSkater = 0x5B8AB877;             // "Skater"
constexpr uint32_t kType = 0x7321A8D6;               // "type"
constexpr uint32_t kConfigurableTricks = 0x5004A967; // "ConfigurableTricks"
constexpr uint32_t kConfigurableCats = 0x325F0B5D;   // "ConfigurableCats"
constexpr uint32_t kTrickType = 0x89FA55CE;          // "trickType"
constexpr uint32_t kScr = 0xA6D2D890;                // "scr"
constexpr uint32_t kGrindTrick = 0x6881FCB4;         // "GrindTrick"
constexpr uint32_t kGrabTrick = 0xB9AE26D2;          // "GrabTrick"
constexpr uint32_t kFlipTrick = 0xBA15FC7F;          // "FlipTrick"
constexpr uint32_t kParams = 0x7031F10C;             // "params"
constexpr uint32_t kIsSpecial = 0x1AEEAA40;          // "IsSpecial"
constexpr uint32_t kFull = 0x1F802B5F;               // "full"
constexpr uint32_t kLockedForOthers = 0x7ABFD5B4;    // (name unknown) TrickIsLocked: locked unless it's the current skater's
constexpr uint32_t kIsHidden = 0x27EB9B9D;           // "is_hidden"
constexpr uint32_t kName = 0xA1DC81F9;               // "name"
constexpr uint32_t kTrickDisplayText = 0x7BFE5BE2;   // "trick_display_text"
constexpr uint32_t kExtraTricks = 0x6E855102;        // "ExtraTricks"
constexpr uint32_t kExtraTrickString = 0x3DAAF58B;   // "extra_trick_string"
constexpr uint32_t kExtraTrickName = 0xD0BCFC62;     // (name unknown) GetTrickDisplayText: the extra trick's name
constexpr uint32_t kIndex = 0x7F8C98FE;              // "index"
constexpr uint32_t kSpecialTrickSlot = 0xC7C1990C;   // "special_trickslot"
constexpr uint32_t kSpecialTrickName = 0x35ECC76D;   // "special_trickname"
constexpr uint32_t kIsCat = 0xB56A8816;              // "isCat"
constexpr uint32_t kTrickTypeOut = 0x3D929B66;       // "trick_type"
constexpr uint32_t kArray = 0x5EF31148;              // "array"
constexpr uint32_t kValue = 0xE288A7CB;              // "value"
constexpr uint32_t kLevel = 0x651533EC;              // "level"
constexpr uint32_t kTrickText = 0x3EAFA520;          // "TrickText"
constexpr uint32_t kNumberOfOccurrences = 0x3F42ADAC; // "Number_Of_Occurrences"
constexpr uint32_t kNumSoundtracks = 0xAA0B4287;     // "numsoundtracks"
constexpr uint32_t kSoundtrackName = 0x869029CA;     // "soundtrackname"
constexpr uint32_t kController = 0xB30D9965;         // "controller"
constexpr uint32_t kSkaterHeapIndex = 0xC52D1A41;    // "skater_heap_index"
constexpr uint32_t kFrontEndPad = 0xCDC3D2FD;        // "front_end_pad"
constexpr uint32_t kTricks = 0x1E26FD3E;             // "tricks"
constexpr uint32_t kKeyCombos = 0x1A4F3519;          // (name unknown) GetKeyComboArrayFromTrickArray: the key combos
constexpr uint32_t kPort = 0xBC6EA233;               // "port"
constexpr uint32_t kActuator = 0x114FC131;           // "Actuator"
constexpr uint32_t kPercent = 0x9E497FC6;            // "Percent"
constexpr uint32_t kSlot = 0x53F1DF98;               // "slot"
constexpr uint32_t kTrickName = 0xEF6FB249;          // "trick_name"
constexpr uint32_t kKeyCombo = 0xACFDB27A;           // "key_combo"

GameModules::Skate* Skate() { return GameModules::Skate::Sole(); }
GameObjects::CProfileRoster* ProfileManager() { return GameObjects::g_profileManager; }
PadInput::Hub* InputManager() { return PadInput::g_inputManager; }

// The skate module's local skater (0x00566520, no source yet; null if there's none).
GameObjects::CGameEntity* LocalSkater()
{
    return Skate()->GetHomeRider();
}
// Whether a skater is a local one (its byte at +0xBC, as 0x00566520 reads it).
bool IsLocalSkater(GameObjects::CGameEntity* skater) { return reinterpret_cast<uint8_t*>(skater)[0xBC] != 0; }
// The skater's score (0x00580480: its score component's object).
void* SkaterScore(GameObjects::CGameEntity* skater) { return static_cast<GameObjects::CRider*>(skater)->GetPointsTotal(); }

// 0x00564F80 (no source yet; a skate module method that ignores the module): the profile of `params`' Skater, or the
// current one (no params or no Skater).
CRiderIdentity* ProfileFor(CStruct* params)
{
    return Skate()->FetchAccount(params);
}

// 0x005A9D90 (ScriptCommands::FindNameOfValue): the key combo in `mapping` bound to `trick` (0 if none).
uint32_t KeyComboForTrick(CStruct* mapping, uint32_t trick) { return ScriptCommands::FindNameOfValue(mapping, trick, 0); }

// 0x005A9E30 (ScriptCommands::FindNameOfInteger; in the original the structure goes in EBX): the key combo in `mapping` bound
// to the created trick `cat` (the name of its last integer component of that value; 0 if none).
uint32_t KeyComboForCat(CStruct* mapping, int cat)
{
    return ScriptCommands::FindNameOfInteger(mapping, static_cast<uint32_t>(cat));
}

// `new Scripts::CArray` (the array pool, constructed).
CArray* NewArray()
{
    CArray* array = Scripts::NewArray();
    return array ? array->PrepareAll() : nullptr;
}

// The profile manager's template table: its count (0x00469490, Singletons::PointerMember::Pull folded) and the template at
// `index` (0x00557FB0, no source yet).
int TemplateCount(GameObjects::CProfileRoster* manager) { return static_cast<int>(manager->m_templates.m_num); }
CRiderIdentity* TemplateAt(GameObjects::CProfileRoster* manager, int index)
{
    return reinterpret_cast<CRiderIdentity*(__thiscall*)(GameObjects::CProfileRoster*, int)>(CodeAddress(&GoalSystem::CGoalManager::GetObjectiveAt))(manager, index);
}

// A trick structure's type: its trickType, else its scr, else GrindTrick.
uint32_t TrickTypeOf(CStruct* trick, uint32_t* type)
{
    if (!trick->ReadHashValue(kTrickType, type, 0) && !trick->ReadHashValue(kScr, type, 0))
        *type = kGrindTrick;
    return *type;
}

} // namespace

// 0x005B0A40: FlushDeadObjects: the object manager's dead objects deleted now. Always true.
bool __cdecl CFunc_FlushDeadObjects(Scripts::CStruct*, Scripts::CScript*)
{
    reinterpret_cast<GameObjects::CMiscHub*>(GameObjects::g_compositeObjectManager)->FlushDeadObjects();
    return true;
}

// 0x005B0A50: BindTrickToKeyCombo key_combo=<keys> trick=<name or CAT number> [update_mappings=0] [profile=<name>]
// [special index=n [cat]]: in the profile (the named template, else the current one) either the special trick slot
// `index` set (the trick, the keys, whether it's a CAT; false without index) or the trick_mapping's key combo bound
// to the trick. True otherwise.
bool __cdecl CFunc_BindTrickToKeyCombo(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t keyCombo;
    params->ReadHashValue("key_combo", &keyCombo, 1);
    uint32_t trickName = 0;
    params->ReadHashValue("trick", &trickName, 0);
    int trickNumber = 0;
    bool isNumber = params->FetchIntValue("trick", &trickNumber, 0);
    bool update = true;
    int updateMappings = 1;
    params->FetchIntValue("update_mappings", &updateMappings, 0);
    if (updateMappings == 0)
        update = false;

    CRiderIdentity* profile;
    if (params->HasEntryNamed(kProfile)) {
        uint32_t name;
        params->ReadHashValue(kProfile, &name, 1);
        profile = ProfileManager()->GetTemplate(name);
    } else {
        profile = ProfileFor(nullptr);
    }

    if (params->HasBareFlag("special")) {
        int index;
        if (!params->FetchIntValue("index", &index, 0))
            return false;
        CRiderIdentity::SpecialTrick trick;
        trick.MakeNew();
        trick.keys = keyCombo;
        trick.trick = isNumber ? static_cast<uint32_t>(trickNumber) : trickName;
        trick.flag = params->HasBareFlag(kCat);
        profile->SetSpecialTrick(index, &trick, update);
        return true;
    }
    CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    mapping->EraseModule(keyCombo);
    if (isNumber)
        mapping->PutInt(keyCombo, trickNumber);
    else
        mapping->PutNameKey(keyCombo, trickName);
    return true;
}

// 0x005B0C00: GetKeyComboBoundToTrick trick=<name> [special] | cat_num=n [profile=<name>]: the key combo bound to
// the trick (the profile's trick_mapping, or with `special` its special trick slots - then also current_index, the
// slot) or to the created trick cat_num, as current_key_combo. False if none.
bool __cdecl CFunc_GetKeyComboBoundToTrick(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t trick = 0;
    int catNumber = 0; // (uninitialised in the original when cat_num is missing too: an asserting get)
    if (!params->ReadHashValue(kTrick, &trick, 0))
        params->FetchIntValue(kCatNum, &catNumber, 1);
    CRiderIdentity* profile;
    if (params->HasEntryNamed(kProfile)) {
        uint32_t name;
        params->ReadHashValue(kProfile, &name, 1);
        profile = ProfileManager()->GetTemplate(name);
    } else {
        profile = ProfileFor(nullptr);
    }

    bool foundSpecial = false;
    int slot = 0;
    uint32_t keyCombo;
    if (trick) {
        if (!params->HasBareFlag(kSpecial)) {
            CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
            keyCombo = KeyComboForTrick(mapping, trick);
            uint32_t bound = 0;
            if (!mapping->ReadHashValue(keyCombo, &bound, 0) || bound != trick)
                return false;
        } else {
            if (!params->HasBareFlag(kSpecial))
                return false;
            int count = profile->GetMaxSpecials();
            if (count <= 0)
                return false;
            CRiderIdentity::SpecialTrick special;
            for (slot = 0;; ++slot) {
                if (slot >= count)
                    return false;
                profile->GetSpecialTrick(&special, slot);
                if (special.trick == trick)
                    break;
            }
            DebugOut::Printf("GetKeyComboBoundToTrick found a special trick\n"); // "GetKeyComboBoundToTrick found a special trick\n"
            keyCombo = special.keys;
            foundSpecial = true;
        }
    } else {
        CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
        keyCombo = KeyComboForCat(mapping, catNumber);
    }
    if (!keyCombo)
        return false;
    script->m_args->PutNameKey("current_key_combo", keyCombo);
    if (foundSpecial) {
        DebugOut::Printf("current_index: %i\n", slot); // "current_index: %i\n"
        script->m_args->PutInt("current_index", slot);
    }
    return true;
}

// 0x005B0DA0: UpdateTrickMappings [Skater=n]: the skater's trick component given its profile's mappings - online the
// local skater and the first profile (not checked for null), otherwise skater n's and profile n's (nothing if either
// is missing). Always true.
bool __cdecl CFunc_UpdateTrickMappings(Scripts::CStruct* params, Scripts::CScript*)
{
    GameModules::Skate* skate = Skate();
    GameObjects::CProfileRoster* manager = ProfileManager();
    GameObjects::CGameEntity* skater;
    CRiderIdentity* profile;
    if (OnlinePlay::g_gameNetManager->InNetGame()) {
        skater = LocalSkater();
        profile = manager->FetchAccount(0);
    } else {
        int index;
        params->FetchIntValue(kSkater, &index, 1);
        skater = skate->GetRiderAt(index);
        profile = manager->FetchAccount(index);
        if (!skater || !profile)
            return true;
    }
    static_cast<GameObjects::CMoveExecutor*>(skater->FetchModule(kTrick))->SetTrickMappings(profile);
    return true;
}

// 0x005B0E30: GetConfigurableTricksFromType [type=<trick type>] [special]: ConfigurableTricks, the names of the global
// ConfigurableTricks whose type (trickType, else scr, else GrindTrick) is `type` (with `special` and no type: any)
// and whose params are IsSpecial or not as `special` asks; for grab and flip tricks also ConfigurableCats, the local
// skater's created trick slots (1-10) that are full. False if no trick matched.
bool __cdecl CFunc_GetConfigurableTricksFromType(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t tricks[128];
    int cats[128];
    uint32_t type = 0;
    params->ReadHashValue(kType, &type, 0);
    bool special = params->HasBareFlag(kSpecial);
    int trickCount = 0;
    int catCount = 0;
    CArray* configurable = Scripts::FetchList(kConfigurableTricks);
    int size = static_cast<int>(configurable->m_extent);
    uint32_t trickType;
    for (int i = 0; i < size; ++i) {
        uint32_t name = configurable->FetchPart(i);
        CStruct* trick = Scripts::ReadSubStruct(name);
        if (!trick)
            continue;
        TrickTypeOf(trick, &trickType);
        if ((!special || type) && trickType != type)
            continue;
        CStruct* trickParams;
        trick->ReadSubStruct(kParams, &trickParams, 1);
        if (trickParams->HasBareFlag(kIsSpecial) != special)
            continue;
        tricks[trickCount++] = name;
    }
    if (type == kGrabTrick || type == kFlipTrick) {
        GameObjects::CGameEntity* skater = LocalSkater();
        if (skater) {
            // the skater's created tricks (+0x180: 10 pointers to objects with their structure at +4)
            auto** created = reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(skater) + 0x180);
            for (int slot = 1; slot < 11; ++slot) {
                int full = 0;
                if (reinterpret_cast<CStruct*>(*reinterpret_cast<uint32_t*>(created[slot - 1] + 4))
                        ->FetchIntValue(kFull, &full, 0) &&
                    full)
                    cats[catCount++] = slot;
            }
        }
    }
    if (trickCount == 0)
        return false;
    CArray* trickArray = NewArray();
    trickArray->ConfigureSizeKind(trickCount, Scripts::QBVAL_NAME);
    for (int i = 0; i < trickCount; ++i)
        trickArray->SetElement(i, tricks[i]);
    script->m_args->AppendArrayRef(kConfigurableTricks, trickArray);
    if (catCount > 0) {
        CArray* catArray = NewArray();
        catArray->ConfigureSizeKind(catCount, Scripts::QBVAL_INTEGER);
        for (int i = 0; i < catCount; ++i)
            catArray->SetElement(i, static_cast<uint32_t>(cats[i]));
        script->m_args->AppendArrayRef(kConfigurableCats, catArray);
    }
    return true;
}

// 0x005B1140: TrickIsLocked trick=<name>: whether the trick (a global structure whose params name a Skater) is
// locked: with the flag 0x7ABFD5B4 unless the current profile is that skater; otherwise if that skater's template is
// hidden (is_hidden). False without the structure, its params or Skater, or with no such template.
bool __cdecl CFunc_TrickIsLocked(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t trick;
    params->ReadHashValue(kTrick, &trick, 1);
    CStruct* structure = Scripts::ReadSubStruct(trick);
    if (!structure)
        return false;
    CStruct* trickParams = nullptr; // (uninitialised in the original: an asserting get)
    structure->ReadSubStruct(kParams, &trickParams, 1);
    if (!trickParams)
        return false;
    uint32_t skater;
    if (!trickParams->ReadHashValue(kSkater, &skater, 0))
        return false;
    GameObjects::CProfileRoster* manager = ProfileManager();
    if (trickParams->HasBareFlag(kLockedForOthers))
        return manager->FetchActiveAccount()->FetchTitle() != skater;
    int count = TemplateCount(manager);
    for (int i = 0; i < count; ++i) {
        CRiderIdentity* profile = TemplateAt(manager, i);
        if (profile->FetchTitle() == skater) {
            int hidden = 0;
            profile->InfoStructure()->FetchIntValue(kIsHidden, &hidden, 0);
            return hidden != 0;
        }
    }
    return false;
}

// 0x005B1250: GetTrickDisplayText trick=<name>: trick_display_text, its params' name; if it has ExtraTricks (a name,
// or an array's first) also extra_trick_string (the first extra trick's params' name) and 0xD0BCFC62 (the extra
// tricks' name). False without the trick structure.
bool __cdecl CFunc_GetTrickDisplayText(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t trick;
    params->ReadHashValue(kTrick, &trick, 1);
    CStruct* structure = Scripts::ReadSubStruct(trick);
    if (!structure)
        return false;
    CStruct* trickParams;
    structure->ReadSubStruct(kParams, &trickParams, 1);
    const char* name;
    trickParams->FetchText(kName, &name, 1);
    script->m_args->AppendWord(kTrickDisplayText, name);
    uint32_t extra = 0;
    if (!trickParams->ReadHashValue(kExtraTricks, &extra, 0)) {
        CArray* extras;
        if (trickParams->FetchList(kExtraTricks, &extras, 0))
            extra = extras->FetchPart(0);
    }
    if (extra) {
        auto* first = reinterpret_cast<CStruct*>(Scripts::FetchList(extra)->FetchPart(0));
        CStruct* extraParams;
        first->ReadSubStruct(kParams, &extraParams, 1);
        const char* extraName;
        extraParams->FetchText(kName, &extraName, 1);
        script->m_args->AppendWord(kExtraTrickString, extraName);
        script->m_args->PutNameKey(kExtraTrickName, extra);
    }
    return true;
}

// 0x005B1380: GetSpecialTrickInfo index=n: the current profile's special trick slot n: special_trickslot (its keys),
// special_trickname (a CAT's number as an integer, else the trick's name) and isCat. Always true.
bool __cdecl CFunc_GetSpecialTrickInfo(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index;
    params->FetchIntValue(kIndex, &index, 1);
    CRiderIdentity::SpecialTrick trick;
    ProfileFor(nullptr)->GetSpecialTrick(&trick, index);
    script->m_args->PutNameKey(kSpecialTrickSlot, trick.keys);
    if (trick.flag)
        script->m_args->PutInt(kSpecialTrickName, static_cast<int>(trick.trick));
    else
        script->m_args->PutNameKey(kSpecialTrickName, trick.trick);
    script->m_args->PutInt(kIsCat, trick.flag);
    return true;
}

// 0x005B1420: GetTrickType trick=<name>: trick_type, the trick structure's type (trickType, else scr, else
// GrindTrick). Always true.
bool __cdecl CFunc_GetTrickType(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t trick;
    params->ReadHashValue(kTrick, &trick, 1);
    uint32_t type;
    TrickTypeOf(Scripts::ReadSubStruct(trick), &type);
    script->m_args->PutNameKey(kTrickTypeOut, type);
    return true;
}

// 0x005B14A0: GetIndexOfItemContaining array=<global array> name=<component> value=<checksum> [index=start]: index,
// the first structure from `start` whose component `name` is `value`. False if none.
bool __cdecl CFunc_GetIndexOfItemContaining(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t arrayName;
    params->ReadHashValue(kArray, &arrayName, 1);
    CArray* array = Scripts::FetchList(arrayName);
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    int index = 0;
    params->FetchIntValue(kIndex, &index, 0);
    uint32_t value;
    params->ReadHashValue(kValue, &value, 1);
    int size = static_cast<int>(array->m_extent);
    for (; index < size; ++index) {
        auto* item = reinterpret_cast<CStruct*>(array->FetchPart(index));
        if (!item->HasEntryNamed(name))
            continue;
        uint32_t found;
        item->ReadHashValue(name, &found, 1);
        if (value == found) {
            script->m_args->PutInt(kIndex, index);
            return true;
        }
    }
    return false;
}

// 0x005B1590: GetLevelRecords [level=n]: the level's records (default: the career's level) written to the script's
// parameters. Always true.
bool __cdecl CFunc_GetLevelRecords(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameModules::Skate* skate = Skate();
    int level;
    if (!params->FetchIntValue(kLevel, &level, 0))
        level = (*reinterpret_cast<GameObjects::CRiderStoryProgress**>(reinterpret_cast<uint8_t*>(skate) + 0x20))->FetchStage();
    Highscores::g_gameRecords->GetLevelRecords(level)->WriteToStructure(script->m_args);
    return true;
}

// 0x005B15F0: ResetComboRecords: the local skater's score's three combo records (0x00561400: +0x2C, +0x30, +0x34)
// zeroed. Always true.
bool __cdecl CFunc_ResetComboRecords(Scripts::CStruct*, Scripts::CScript*)
{
    GameObjects::CGameEntity* skater = LocalSkater();
    if (skater)
        static_cast<GameModules::PointTally*>(SkaterScore(skater))->ResetBests();
    return true;
}

// 0x005B1610: GetNumberOfTrickOccurrences TrickText="text": Number_Of_Occurrences, how often the local skater's score
// has the trick (0x005624C0 with the text's checksum, 0, 0, 1; no local skater is not checked). Always true.
bool __cdecl CFunc_GetNumberOfTrickOccurrences(Scripts::CStruct* params, Scripts::CScript* script)
{
    const char* text;
    params->FetchText(kTrickText, &text, 1);
    uint32_t trick = Checksums::HashFromText(text);
    void* score = SkaterScore(LocalSkater());
    int count = static_cast<GameModules::PointTally*>(score)->CountTrickAfterTaps(trick, 0, false, 1);
    script->m_args->PutInt(kNumberOfOccurrences, count);
    return true;
}

// 0x005B1670: GetNumSoundtracks: numsoundtracks. Always true.
bool __cdecl CFunc_GetNumSoundtracks(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutInt(kNumSoundtracks, AudioStream::GetNumSoundtracks());
    return true;
}

// 0x005B1690: GetSoundtrackName <n>: soundtrackname, user soundtrack n's name. Always true.
bool __cdecl CFunc_GetSoundtrackName(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index;
    params->FetchIntValue(0u, &index, 1);
    script->m_args->AppendWord(kSoundtrackName, AudioStream::GetSoundtrackName(index));
    return true;
}

// 0x005B16D0: BindControllerToSkater controller=c skater_heap_index=n: player n reads controller c; the skate module's
// local skaters' input components told (0x00567090). Always true.
bool __cdecl CFunc_BindControllerToSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    int controller;
    params->FetchIntValue(kController, &controller, 1);
    int skater;
    params->FetchIntValue(kSkaterHeapIndex, &skater, 1);
    GameModules::Skate* skate = Skate();
    InputManager()->SetPlayerController(skater, controller);
    skate->BindControllers();
    return true;
}

// 0x005B1730: BindFrontEndToController controller=c front_end_pad=n: the front end's pad n listens to controller c
// (the pad that had c takes n's old controller); its input mappings updated. Always true.
bool __cdecl CFunc_BindFrontEndToController(Scripts::CStruct* params, Scripts::CScript*)
{
    int controller;
    params->FetchIntValue(kController, &controller, 1);
    int pad;
    params->FetchIntValue(kFrontEndPad, &pad, 1);
    // "attempting to bind front end %i to controller %i\n"
    DebugOut::Printf("attempting to bind front end %i to controller %i\n", pad, controller);
    GameModules::FrontEnd* frontEnd = GameModules::FrontEndInstance();
    int* map = frontEnd->m_deviceServerMap;
    int old = map[pad];
    if (map[0] == controller)
        map[0] = old;
    if (map[1] == controller)
        map[1] = old;
    map[pad] = controller;
    frontEnd->UpdateInputMappings();
    return true;
}

// 0x005B17C0: ControllerBoundToDifferentSkater Skater=n controller=c: whether a local skater other than n (0-7) reads
// controller c.
bool __cdecl CFunc_ControllerBoundToDifferentSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    int skater;
    params->FetchIntValue(kSkater, &skater, 1);
    int controller;
    params->FetchIntValue(kController, &controller, 1);
    GameModules::Skate* skate = Skate();
    for (int i = 0; i < 8; ++i) {
        if (i == skater)
            continue;
        GameObjects::CGameEntity* other = skate->GetRiderAt(i);
        if (!other || !IsLocalSkater(other))
            continue;
        if (InputManager()->GetPlayerController(i) == controller)
            return true;
    }
    return false;
}

// 0x005B1840: ControllerBoundToSkater controller=c Skater=n: whether player n reads controller c.
bool __cdecl CFunc_ControllerBoundToSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    int controller;
    params->FetchIntValue(kController, &controller, 1);
    int skater;
    params->FetchIntValue(kSkater, &skater, 1);
    return InputManager()->GetPlayerController(skater) == controller;
}

// 0x005B1890: GetKeyComboArrayFromTrickArray tricks=[...]: 0x1A4F3519, the key combo of each trick (the current
// profile's trick_mapping, else its special trick slots; 0 if none) - a copy: the array made for it isn't freed (a
// leak in the original). Always true.
bool __cdecl CFunc_GetKeyComboArrayFromTrickArray(Scripts::CStruct* params, Scripts::CScript* script)
{
    CArray* tricks;
    params->FetchList(kTricks, &tricks, 1);
    CRiderIdentity* profile = ProfileFor(nullptr);
    CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    int size = static_cast<int>(tricks->m_extent);
    CArray* keyCombos = NewArray();
    keyCombos->ConfigureSizeKind(size, Scripts::QBVAL_NAME);
    for (int i = 0; i < size; ++i) {
        uint32_t trick = tricks->FetchPart(i);
        uint32_t keyCombo = KeyComboForTrick(mapping, trick);
        if (!keyCombo) {
            int count = profile->GetMaxSpecials();
            for (int slot = 0; slot < count; ++slot) {
                CRiderIdentity::SpecialTrick special;
                profile->GetSpecialTrick(&special, slot);
                if (special.trick == trick) {
                    keyCombo = special.keys;
                    break;
                }
            }
        }
        keyCombos->SetElement(i, keyCombo);
    }
    script->m_args->AppendArrayValue(kKeyCombos, keyCombos);
    return true;
}

// 0x005B1A00: FirstInputReceived: the skate module's flag (+0x3C) set (0x00564DA0). Always true.
bool __cdecl CFunc_FirstInputReceived(Scripts::CStruct*, Scripts::CScript*)
{
    Skate()->CheckControllers();
    return true;
}

// 0x005B1A10: VibrateController port=p Actuator=a Percent=n: the device's actuator a at n percent. False without the
// device.
bool __cdecl CFunc_VibrateController(Scripts::CStruct* params, Scripts::CScript*)
{
    int port;
    params->FetchIntValue(kPort, &port, 1);
    int actuator;
    params->FetchIntValue(kActuator, &actuator, 1);
    int percent;
    params->FetchIntValue(kPercent, &percent, 1);
    Sio::InputPort* device = Sio::ManagerInstance()->FetchGadget(port, 0);
    if (!device)
        return false;
    device->FireActuator(actuator, percent);
    return true;
}

// 0x005B1A90: LockCurrentSkaterProfileIndex [<n> (1)]: the profile manager's lock set (n != 0). Always true.
bool __cdecl CFunc_LockCurrentSkaterProfileIndex(Scripts::CStruct* params, Scripts::CScript*)
{
    int lock = 1;
    params->FetchIntValue(0u, &lock, 1);
    ProfileManager()->SetLocked(lock != 0);
    return true;
}

// 0x005B1AD0: SetSpecialTrickInfo slot=n trick_name=<name> key_combo=<keys>: the current profile's special trick slot
// n set (not a CAT; the skater not told). False without a current profile.
bool __cdecl CFunc_SetSpecialTrickInfo(Scripts::CStruct* params, Scripts::CScript*)
{
    int slot;
    params->FetchIntValue(kSlot, &slot, 1);
    uint32_t name;
    params->ReadHashValue(kTrickName, &name, 1);
    uint32_t keys;
    params->ReadHashValue(kKeyCombo, &keys, 1);
    CRiderIdentity* profile = ProfileManager()->FetchActiveAccount();
    if (!profile)
        return false;
    CRiderIdentity::SpecialTrick trick;
    trick.MakeNew();
    trick.trick = name;
    trick.keys = keys;
    profile->SetSpecialTrick(slot, &trick, false);
    return true;
}

} // namespace ScriptCommands
