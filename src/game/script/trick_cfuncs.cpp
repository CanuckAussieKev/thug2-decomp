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
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Obj::CSkaterProfile;
using Script::CArray;
using Script::CStruct;

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

Mdl::Skate* Skate() { return Mdl::Skate::Instance(); }
Obj::CPlayerProfileManager* ProfileManager() { return Obj::g_profileManager; }
Inp::Manager* InputManager() { return GameVariable<Inp::Manager*>(0x00696D40); }

// The skate module's local skater (0x00566520, no source yet; null if there's none).
Obj::CCompositeObject* LocalSkater()
{
    return Skate()->GetLocalSkater();
}
// Whether a skater is a local one (its byte at +0xBC, as 0x00566520 reads it).
bool IsLocalSkater(Obj::CCompositeObject* skater) { return reinterpret_cast<uint8_t*>(skater)[0xBC] != 0; }
// The skater's score (0x00580480: its score component's object).
void* SkaterScore(Obj::CCompositeObject* skater) { return static_cast<Obj::CSkater*>(skater)->GetScore(); }

// 0x00564F80 (no source yet; a skate module method that ignores the module): the profile of `params`' Skater, or the
// current one (no params or no Skater).
CSkaterProfile* ProfileFor(CStruct* params)
{
    return Skate()->GetProfile(params);
}

// 0x005A9D90 (CFuncs::FindNameOfValue): the key combo in `mapping` bound to `trick` (0 if none).
uint32_t KeyComboForTrick(CStruct* mapping, uint32_t trick) { return CFuncs::FindNameOfValue(mapping, trick, 0); }

// 0x005A9E30 (CFuncs::FindNameOfInteger; in the original the structure goes in EBX): the key combo in `mapping` bound
// to the created trick `cat` (the name of its last integer component of that value; 0 if none).
uint32_t KeyComboForCat(CStruct* mapping, int cat)
{
    return CFuncs::FindNameOfInteger(mapping, static_cast<uint32_t>(cat));
}

// `new Script::CArray` (the array pool, constructed).
CArray* NewArray()
{
    CArray* array = Script::NewArray();
    return array ? array->Init() : nullptr;
}

// The profile manager's template table: its count (0x00469490, Spt::PointerMember::Get folded) and the template at
// `index` (0x00557FB0, no source yet).
int TemplateCount(Obj::CPlayerProfileManager* manager) { return static_cast<int>(manager->m_templates.m_count); }
CSkaterProfile* TemplateAt(Obj::CPlayerProfileManager* manager, int index)
{
    return GameFunction<CSkaterProfile*(__thiscall*)(Obj::CPlayerProfileManager*, int)>(0x00557FB0)(manager, index);
}

// A trick structure's type: its trickType, else its scr, else GrindTrick.
uint32_t TrickTypeOf(CStruct* trick, uint32_t* type)
{
    if (!trick->GetChecksum(kTrickType, type, 0) && !trick->GetChecksum(kScr, type, 0))
        *type = kGrindTrick;
    return *type;
}

} // namespace

// 0x005B0A40: FlushDeadObjects: the object manager's dead objects deleted now. Always true.
bool __cdecl CFunc_FlushDeadObjects(Script::CStruct*, Script::CScript*)
{
    reinterpret_cast<Obj::CGeneralManager*>(Obj::g_compositeObjectManager)->FlushDeadObjects();
    return true;
}

// 0x005B0A50: BindTrickToKeyCombo key_combo=<keys> trick=<name or CAT number> [update_mappings=0] [profile=<name>]
// [special index=n [cat]]: in the profile (the named template, else the current one) either the special trick slot
// `index` set (the trick, the keys, whether it's a CAT; false without index) or the trick_mapping's key combo bound
// to the trick. True otherwise.
bool __cdecl CFunc_BindTrickToKeyCombo(Script::CStruct* params, Script::CScript*)
{
    uint32_t keyCombo;
    params->GetChecksum("key_combo", &keyCombo, 1);
    uint32_t trickName = 0;
    params->GetChecksum("trick", &trickName, 0);
    int trickNumber = 0;
    bool isNumber = params->GetInteger("trick", &trickNumber, 0);
    bool update = true;
    int updateMappings = 1;
    params->GetInteger("update_mappings", &updateMappings, 0);
    if (updateMappings == 0)
        update = false;

    CSkaterProfile* profile;
    if (params->ContainsComponentNamed(kProfile)) {
        uint32_t name;
        params->GetChecksum(kProfile, &name, 1);
        profile = ProfileManager()->GetTemplate(name);
    } else {
        profile = ProfileFor(nullptr);
    }

    if (params->ContainsFlag("special")) {
        int index;
        if (!params->GetInteger("index", &index, 0))
            return false;
        CSkaterProfile::SpecialTrick trick;
        trick.Construct();
        trick.keys = keyCombo;
        trick.trick = isNumber ? static_cast<uint32_t>(trickNumber) : trickName;
        trick.flag = params->ContainsFlag(kCat);
        profile->SetSpecialTrick(index, &trick, update);
        return true;
    }
    CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    mapping->RemoveComponent(keyCombo);
    if (isNumber)
        mapping->AddInteger(keyCombo, trickNumber);
    else
        mapping->AddChecksum(keyCombo, trickName);
    return true;
}

// 0x005B0C00: GetKeyComboBoundToTrick trick=<name> [special] | cat_num=n [profile=<name>]: the key combo bound to
// the trick (the profile's trick_mapping, or with `special` its special trick slots - then also current_index, the
// slot) or to the created trick cat_num, as current_key_combo. False if none.
bool __cdecl CFunc_GetKeyComboBoundToTrick(Script::CStruct* params, Script::CScript* script)
{
    uint32_t trick = 0;
    int catNumber = 0; // (uninitialised in the original when cat_num is missing too: an asserting get)
    if (!params->GetChecksum(kTrick, &trick, 0))
        params->GetInteger(kCatNum, &catNumber, 1);
    CSkaterProfile* profile;
    if (params->ContainsComponentNamed(kProfile)) {
        uint32_t name;
        params->GetChecksum(kProfile, &name, 1);
        profile = ProfileManager()->GetTemplate(name);
    } else {
        profile = ProfileFor(nullptr);
    }

    bool foundSpecial = false;
    int slot = 0;
    uint32_t keyCombo;
    if (trick) {
        if (!params->ContainsFlag(kSpecial)) {
            CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
            keyCombo = KeyComboForTrick(mapping, trick);
            uint32_t bound = 0;
            if (!mapping->GetChecksum(keyCombo, &bound, 0) || bound != trick)
                return false;
        } else {
            if (!params->ContainsFlag(kSpecial))
                return false;
            int count = profile->GetMaxSpecials();
            if (count <= 0)
                return false;
            CSkaterProfile::SpecialTrick special;
            for (slot = 0;; ++slot) {
                if (slot >= count)
                    return false;
                profile->GetSpecialTrick(&special, slot);
                if (special.trick == trick)
                    break;
            }
            Dbg::Printf(reinterpret_cast<const char*>(0x00657C60)); // "GetKeyComboBoundToTrick found a special trick\n"
            keyCombo = special.keys;
            foundSpecial = true;
        }
    } else {
        CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
        keyCombo = KeyComboForCat(mapping, catNumber);
    }
    if (!keyCombo)
        return false;
    script->m_params->AddChecksum("current_key_combo", keyCombo);
    if (foundSpecial) {
        Dbg::Printf(reinterpret_cast<const char*>(0x00657C38), slot); // "current_index: %i\n"
        script->m_params->AddInteger("current_index", slot);
    }
    return true;
}

// 0x005B0DA0: UpdateTrickMappings [Skater=n]: the skater's trick component given its profile's mappings - online the
// local skater and the first profile (not checked for null), otherwise skater n's and profile n's (nothing if either
// is missing). Always true.
bool __cdecl CFunc_UpdateTrickMappings(Script::CStruct* params, Script::CScript*)
{
    Mdl::Skate* skate = Skate();
    Obj::CPlayerProfileManager* manager = ProfileManager();
    Obj::CCompositeObject* skater;
    CSkaterProfile* profile;
    if (GameVariable<GameNet::Manager*>(0x007CCE14)->InNetGame()) {
        skater = LocalSkater();
        profile = manager->GetProfile(0);
    } else {
        int index;
        params->GetInteger(kSkater, &index, 1);
        skater = skate->GetSkater(index);
        profile = manager->GetProfile(index);
        if (!skater || !profile)
            return true;
    }
    static_cast<Obj::CTrickComponent*>(skater->GetComponent(kTrick))->SetTrickMappings(profile);
    return true;
}

// 0x005B0E30: GetConfigurableTricksFromType [type=<trick type>] [special]: ConfigurableTricks, the names of the global
// ConfigurableTricks whose type (trickType, else scr, else GrindTrick) is `type` (with `special` and no type: any)
// and whose params are IsSpecial or not as `special` asks; for grab and flip tricks also ConfigurableCats, the local
// skater's created trick slots (1-10) that are full. False if no trick matched.
bool __cdecl CFunc_GetConfigurableTricksFromType(Script::CStruct* params, Script::CScript* script)
{
    uint32_t tricks[128];
    int cats[128];
    uint32_t type = 0;
    params->GetChecksum(kType, &type, 0);
    bool special = params->ContainsFlag(kSpecial);
    int trickCount = 0;
    int catCount = 0;
    CArray* configurable = Script::GetArray(kConfigurableTricks);
    int size = static_cast<int>(configurable->m_size);
    uint32_t trickType;
    for (int i = 0; i < size; ++i) {
        uint32_t name = configurable->GetElement(i);
        CStruct* trick = Script::GetStructure(name);
        if (!trick)
            continue;
        TrickTypeOf(trick, &trickType);
        if ((!special || type) && trickType != type)
            continue;
        CStruct* trickParams;
        trick->GetStructure(kParams, &trickParams, 1);
        if (trickParams->ContainsFlag(kIsSpecial) != special)
            continue;
        tricks[trickCount++] = name;
    }
    if (type == kGrabTrick || type == kFlipTrick) {
        Obj::CCompositeObject* skater = LocalSkater();
        if (skater) {
            // the skater's created tricks (+0x180: 10 pointers to objects with their structure at +4)
            auto** created = reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(skater) + 0x180);
            for (int slot = 1; slot < 11; ++slot) {
                int full = 0;
                if (reinterpret_cast<CStruct*>(*reinterpret_cast<uint32_t*>(created[slot - 1] + 4))
                        ->GetInteger(kFull, &full, 0) &&
                    full)
                    cats[catCount++] = slot;
            }
        }
    }
    if (trickCount == 0)
        return false;
    CArray* trickArray = NewArray();
    trickArray->SetSizeAndType(trickCount, Script::ESYMBOLTYPE_NAME);
    for (int i = 0; i < trickCount; ++i)
        trickArray->SetElement(i, tricks[i]);
    script->m_params->AddArrayPointer(kConfigurableTricks, trickArray);
    if (catCount > 0) {
        CArray* catArray = NewArray();
        catArray->SetSizeAndType(catCount, Script::ESYMBOLTYPE_INTEGER);
        for (int i = 0; i < catCount; ++i)
            catArray->SetElement(i, static_cast<uint32_t>(cats[i]));
        script->m_params->AddArrayPointer(kConfigurableCats, catArray);
    }
    return true;
}

// 0x005B1140: TrickIsLocked trick=<name>: whether the trick (a global structure whose params name a Skater) is
// locked: with the flag 0x7ABFD5B4 unless the current profile is that skater; otherwise if that skater's template is
// hidden (is_hidden). False without the structure, its params or Skater, or with no such template.
bool __cdecl CFunc_TrickIsLocked(Script::CStruct* params, Script::CScript*)
{
    uint32_t trick;
    params->GetChecksum(kTrick, &trick, 1);
    CStruct* structure = Script::GetStructure(trick);
    if (!structure)
        return false;
    CStruct* trickParams = nullptr; // (uninitialised in the original: an asserting get)
    structure->GetStructure(kParams, &trickParams, 1);
    if (!trickParams)
        return false;
    uint32_t skater;
    if (!trickParams->GetChecksum(kSkater, &skater, 0))
        return false;
    Obj::CPlayerProfileManager* manager = ProfileManager();
    if (trickParams->ContainsFlag(kLockedForOthers))
        return manager->GetCurrentProfile()->GetName() != skater;
    int count = TemplateCount(manager);
    for (int i = 0; i < count; ++i) {
        CSkaterProfile* profile = TemplateAt(manager, i);
        if (profile->GetName() == skater) {
            int hidden = 0;
            profile->InfoStructure()->GetInteger(kIsHidden, &hidden, 0);
            return hidden != 0;
        }
    }
    return false;
}

// 0x005B1250: GetTrickDisplayText trick=<name>: trick_display_text, its params' name; if it has ExtraTricks (a name,
// or an array's first) also extra_trick_string (the first extra trick's params' name) and 0xD0BCFC62 (the extra
// tricks' name). False without the trick structure.
bool __cdecl CFunc_GetTrickDisplayText(Script::CStruct* params, Script::CScript* script)
{
    uint32_t trick;
    params->GetChecksum(kTrick, &trick, 1);
    CStruct* structure = Script::GetStructure(trick);
    if (!structure)
        return false;
    CStruct* trickParams;
    structure->GetStructure(kParams, &trickParams, 1);
    const char* name;
    trickParams->GetString(kName, &name, 1);
    script->m_params->AddString(kTrickDisplayText, name);
    uint32_t extra = 0;
    if (!trickParams->GetChecksum(kExtraTricks, &extra, 0)) {
        CArray* extras;
        if (trickParams->GetArray(kExtraTricks, &extras, 0))
            extra = extras->GetElement(0);
    }
    if (extra) {
        auto* first = reinterpret_cast<CStruct*>(Script::GetArray(extra)->GetElement(0));
        CStruct* extraParams;
        first->GetStructure(kParams, &extraParams, 1);
        const char* extraName;
        extraParams->GetString(kName, &extraName, 1);
        script->m_params->AddString(kExtraTrickString, extraName);
        script->m_params->AddChecksum(kExtraTrickName, extra);
    }
    return true;
}

// 0x005B1380: GetSpecialTrickInfo index=n: the current profile's special trick slot n: special_trickslot (its keys),
// special_trickname (a CAT's number as an integer, else the trick's name) and isCat. Always true.
bool __cdecl CFunc_GetSpecialTrickInfo(Script::CStruct* params, Script::CScript* script)
{
    int index;
    params->GetInteger(kIndex, &index, 1);
    CSkaterProfile::SpecialTrick trick;
    ProfileFor(nullptr)->GetSpecialTrick(&trick, index);
    script->m_params->AddChecksum(kSpecialTrickSlot, trick.keys);
    if (trick.flag)
        script->m_params->AddInteger(kSpecialTrickName, static_cast<int>(trick.trick));
    else
        script->m_params->AddChecksum(kSpecialTrickName, trick.trick);
    script->m_params->AddInteger(kIsCat, trick.flag);
    return true;
}

// 0x005B1420: GetTrickType trick=<name>: trick_type, the trick structure's type (trickType, else scr, else
// GrindTrick). Always true.
bool __cdecl CFunc_GetTrickType(Script::CStruct* params, Script::CScript* script)
{
    uint32_t trick;
    params->GetChecksum(kTrick, &trick, 1);
    uint32_t type;
    TrickTypeOf(Script::GetStructure(trick), &type);
    script->m_params->AddChecksum(kTrickTypeOut, type);
    return true;
}

// 0x005B14A0: GetIndexOfItemContaining array=<global array> name=<component> value=<checksum> [index=start]: index,
// the first structure from `start` whose component `name` is `value`. False if none.
bool __cdecl CFunc_GetIndexOfItemContaining(Script::CStruct* params, Script::CScript* script)
{
    uint32_t arrayName;
    params->GetChecksum(kArray, &arrayName, 1);
    CArray* array = Script::GetArray(arrayName);
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    int index = 0;
    params->GetInteger(kIndex, &index, 0);
    uint32_t value;
    params->GetChecksum(kValue, &value, 1);
    int size = static_cast<int>(array->m_size);
    for (; index < size; ++index) {
        auto* item = reinterpret_cast<CStruct*>(array->GetElement(index));
        if (!item->ContainsComponentNamed(name))
            continue;
        uint32_t found;
        item->GetChecksum(name, &found, 1);
        if (value == found) {
            script->m_params->AddInteger(kIndex, index);
            return true;
        }
    }
    return false;
}

// 0x005B1590: GetLevelRecords [level=n]: the level's records (default: the career's level) written to the script's
// parameters. Always true.
bool __cdecl CFunc_GetLevelRecords(Script::CStruct* params, Script::CScript* script)
{
    Mdl::Skate* skate = Skate();
    int level;
    if (!params->GetInteger(kLevel, &level, 0))
        level = (*reinterpret_cast<Obj::CSkaterCareer**>(reinterpret_cast<uint8_t*>(skate) + 0x20))->GetLevel();
    Records::g_gameRecords->GetLevelRecords(level)->WriteToStructure(script->m_params);
    return true;
}

// 0x005B15F0: ResetComboRecords: the local skater's score's three combo records (0x00561400: +0x2C, +0x30, +0x34)
// zeroed. Always true.
bool __cdecl CFunc_ResetComboRecords(Script::CStruct*, Script::CScript*)
{
    Obj::CCompositeObject* skater = LocalSkater();
    if (skater)
        static_cast<Mdl::Score*>(SkaterScore(skater))->ResetBests();
    return true;
}

// 0x005B1610: GetNumberOfTrickOccurrences TrickText="text": Number_Of_Occurrences, how often the local skater's score
// has the trick (0x005624C0 with the text's checksum, 0, 0, 1; no local skater is not checked). Always true.
bool __cdecl CFunc_GetNumberOfTrickOccurrences(Script::CStruct* params, Script::CScript* script)
{
    const char* text;
    params->GetString(kTrickText, &text, 1);
    uint32_t trick = Crc::GenerateCRCFromString(text);
    void* score = SkaterScore(LocalSkater());
    int count = static_cast<Mdl::Score*>(score)->CountTrickAfterTaps(trick, 0, false, 1);
    script->m_params->AddInteger(kNumberOfOccurrences, count);
    return true;
}

// 0x005B1670: GetNumSoundtracks: numsoundtracks. Always true.
bool __cdecl CFunc_GetNumSoundtracks(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(kNumSoundtracks, Pcm::GetNumSoundtracks());
    return true;
}

// 0x005B1690: GetSoundtrackName <n>: soundtrackname, user soundtrack n's name. Always true.
bool __cdecl CFunc_GetSoundtrackName(Script::CStruct* params, Script::CScript* script)
{
    int index;
    params->GetInteger(0u, &index, 1);
    script->m_params->AddString(kSoundtrackName, Pcm::GetSoundtrackName(index));
    return true;
}

// 0x005B16D0: BindControllerToSkater controller=c skater_heap_index=n: player n reads controller c; the skate module's
// local skaters' input components told (0x00567090). Always true.
bool __cdecl CFunc_BindControllerToSkater(Script::CStruct* params, Script::CScript*)
{
    int controller;
    params->GetInteger(kController, &controller, 1);
    int skater;
    params->GetInteger(kSkaterHeapIndex, &skater, 1);
    Mdl::Skate* skate = Skate();
    InputManager()->SetPlayerController(skater, controller);
    skate->BindControllers();
    return true;
}

// 0x005B1730: BindFrontEndToController controller=c front_end_pad=n: the front end's pad n listens to controller c
// (the pad that had c takes n's old controller); its input mappings updated. Always true.
bool __cdecl CFunc_BindFrontEndToController(Script::CStruct* params, Script::CScript*)
{
    int controller;
    params->GetInteger(kController, &controller, 1);
    int pad;
    params->GetInteger(kFrontEndPad, &pad, 1);
    // "attempting to bind front end %i to controller %i\n"
    Dbg::Printf(reinterpret_cast<const char*>(0x00657C90), pad, controller);
    Mdl::FrontEnd* frontEnd = Mdl::FrontEndInstance();
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
bool __cdecl CFunc_ControllerBoundToDifferentSkater(Script::CStruct* params, Script::CScript*)
{
    int skater;
    params->GetInteger(kSkater, &skater, 1);
    int controller;
    params->GetInteger(kController, &controller, 1);
    Mdl::Skate* skate = Skate();
    for (int i = 0; i < 8; ++i) {
        if (i == skater)
            continue;
        Obj::CCompositeObject* other = skate->GetSkater(i);
        if (!other || !IsLocalSkater(other))
            continue;
        if (InputManager()->GetPlayerController(i) == controller)
            return true;
    }
    return false;
}

// 0x005B1840: ControllerBoundToSkater controller=c Skater=n: whether player n reads controller c.
bool __cdecl CFunc_ControllerBoundToSkater(Script::CStruct* params, Script::CScript*)
{
    int controller;
    params->GetInteger(kController, &controller, 1);
    int skater;
    params->GetInteger(kSkater, &skater, 1);
    return InputManager()->GetPlayerController(skater) == controller;
}

// 0x005B1890: GetKeyComboArrayFromTrickArray tricks=[...]: 0x1A4F3519, the key combo of each trick (the current
// profile's trick_mapping, else its special trick slots; 0 if none) - a copy: the array made for it isn't freed (a
// leak in the original). Always true.
bool __cdecl CFunc_GetKeyComboArrayFromTrickArray(Script::CStruct* params, Script::CScript* script)
{
    CArray* tricks;
    params->GetArray(kTricks, &tricks, 1);
    CSkaterProfile* profile = ProfileFor(nullptr);
    CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    int size = static_cast<int>(tricks->m_size);
    CArray* keyCombos = NewArray();
    keyCombos->SetSizeAndType(size, Script::ESYMBOLTYPE_NAME);
    for (int i = 0; i < size; ++i) {
        uint32_t trick = tricks->GetElement(i);
        uint32_t keyCombo = KeyComboForTrick(mapping, trick);
        if (!keyCombo) {
            int count = profile->GetMaxSpecials();
            for (int slot = 0; slot < count; ++slot) {
                CSkaterProfile::SpecialTrick special;
                profile->GetSpecialTrick(&special, slot);
                if (special.trick == trick) {
                    keyCombo = special.keys;
                    break;
                }
            }
        }
        keyCombos->SetElement(i, keyCombo);
    }
    script->m_params->AddArray(kKeyCombos, keyCombos);
    return true;
}

// 0x005B1A00: FirstInputReceived: the skate module's flag (+0x3C) set (0x00564DA0). Always true.
bool __cdecl CFunc_FirstInputReceived(Script::CStruct*, Script::CScript*)
{
    Skate()->CheckControllers();
    return true;
}

// 0x005B1A10: VibrateController port=p Actuator=a Percent=n: the device's actuator a at n percent. False without the
// device.
bool __cdecl CFunc_VibrateController(Script::CStruct* params, Script::CScript*)
{
    int port;
    params->GetInteger(kPort, &port, 1);
    int actuator;
    params->GetInteger(kActuator, &actuator, 1);
    int percent;
    params->GetInteger(kPercent, &percent, 1);
    Sio::Device* device = Sio::ManagerInstance()->GetDevice(port, 0);
    if (!device)
        return false;
    device->ActivateActuator(actuator, percent);
    return true;
}

// 0x005B1A90: LockCurrentSkaterProfileIndex [<n> (1)]: the profile manager's lock set (n != 0). Always true.
bool __cdecl CFunc_LockCurrentSkaterProfileIndex(Script::CStruct* params, Script::CScript*)
{
    int lock = 1;
    params->GetInteger(0u, &lock, 1);
    ProfileManager()->SetLocked(lock != 0);
    return true;
}

// 0x005B1AD0: SetSpecialTrickInfo slot=n trick_name=<name> key_combo=<keys>: the current profile's special trick slot
// n set (not a CAT; the skater not told). False without a current profile.
bool __cdecl CFunc_SetSpecialTrickInfo(Script::CStruct* params, Script::CScript*)
{
    int slot;
    params->GetInteger(kSlot, &slot, 1);
    uint32_t name;
    params->GetChecksum(kTrickName, &name, 1);
    uint32_t keys;
    params->GetChecksum(kKeyCombo, &keys, 1);
    CSkaterProfile* profile = ProfileManager()->GetCurrentProfile();
    if (!profile)
        return false;
    CSkaterProfile::SpecialTrick trick;
    trick.Construct();
    trick.trick = name;
    trick.keys = keys;
    profile->SetSpecialTrick(slot, &trick, false);
    return true;
}

} // namespace CFuncs

REPLACE(0x005B0A40, CFuncs::CFunc_FlushDeadObjects);
REPLACE(0x005B0A50, CFuncs::CFunc_BindTrickToKeyCombo);
REPLACE(0x005B0C00, CFuncs::CFunc_GetKeyComboBoundToTrick);
REPLACE(0x005B0DA0, CFuncs::CFunc_UpdateTrickMappings);
REPLACE(0x005B0E30, CFuncs::CFunc_GetConfigurableTricksFromType);
REPLACE(0x005B1140, CFuncs::CFunc_TrickIsLocked);
REPLACE(0x005B1250, CFuncs::CFunc_GetTrickDisplayText);
REPLACE(0x005B1380, CFuncs::CFunc_GetSpecialTrickInfo);
REPLACE(0x005B1420, CFuncs::CFunc_GetTrickType);
REPLACE(0x005B14A0, CFuncs::CFunc_GetIndexOfItemContaining);
REPLACE(0x005B1590, CFuncs::CFunc_GetLevelRecords);
REPLACE(0x005B15F0, CFuncs::CFunc_ResetComboRecords);
REPLACE(0x005B1610, CFuncs::CFunc_GetNumberOfTrickOccurrences);
REPLACE(0x005B1670, CFuncs::CFunc_GetNumSoundtracks);
REPLACE(0x005B1690, CFuncs::CFunc_GetSoundtrackName);
REPLACE(0x005B16D0, CFuncs::CFunc_BindControllerToSkater);
REPLACE(0x005B1730, CFuncs::CFunc_BindFrontEndToController);
REPLACE(0x005B17C0, CFuncs::CFunc_ControllerBoundToDifferentSkater);
REPLACE(0x005B1840, CFuncs::CFunc_ControllerBoundToSkater);
REPLACE(0x005B1890, CFuncs::CFunc_GetKeyComboArrayFromTrickArray);
REPLACE(0x005B1A00, CFuncs::CFunc_FirstInputReceived);
REPLACE(0x005B1A10, CFuncs::CFunc_VibrateController);
REPLACE(0x005B1A90, CFuncs::CFunc_LockCurrentSkaterProfileIndex);
REPLACE(0x005B1AD0, CFuncs::CFunc_SetSpecialTrickInfo);
