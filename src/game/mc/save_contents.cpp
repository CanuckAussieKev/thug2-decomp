// What a save holds and how it is put back (see save_contents.h).

#include "game/mc/save_contents.h"
#include "game/core/code_address.h"

#include <cstdint>

#include "game/core/dbg.h"
#include "game/ed/goal_editor.h"
#include "game/ed/park_editor.h"
#include "game/goals/goal_manager.h"
#include "game/mc/memcard_cfuncs.h"
#include "game/mc/save_types.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/records.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/sys/game_config.h"
#include "game/goals/goal.h"
#include "game/gfx/skeleton.h"

namespace Mc {

namespace {

using Script::CArray;
using Script::CScript;
using Script::CStruct;

// The names of the save's parts.
constexpr uint32_t kOptions = 0x2FCA0578;                // "Options"
constexpr uint32_t kSplitScreenPreferences = 0xF7720C3F; // "SplitScreenPreferences"
constexpr uint32_t kSoundOptions = 0x89EB9738;           // "SoundOptions"
constexpr uint32_t kMainVolume = 0x6F016DFB;             // "MainVolume"
constexpr uint32_t kMusicVolume = 0xABD4A575;            // "MusicVolume"
constexpr uint32_t kMusicVolOutsideBox = 0xE3C22B01;     // "MusicVolOutsideBox"
constexpr uint32_t kForbiddenTracks1 = 0x595D2C95;       // "PlayListForbiddenTrackFlags1"
constexpr uint32_t kForbiddenTracks2 = 0xC0547D2F;       // "PlayListForbiddenTrackFlags2"
constexpr uint32_t kForbiddenTracks3 = 0xB7534DB9;       // (name unknown) the second set's high half
constexpr uint32_t kForbiddenTracks4 = 0x2937D81A;       // (name unknown) its low half
constexpr uint32_t kCurrentSoundtrack = 0xE1F7C4AE;      // "current_soundtrack"
constexpr uint32_t kRandomMode = 0x31C71B70;             // "RandomMode"
constexpr uint32_t kControllerPreferences = 0x37BDB853;  // "ControllerPreferences"
constexpr uint32_t kVibrationOn = 0x73CEE124;            // "VibrationOn"
constexpr uint32_t kAutokickOn = 0x1EEF7085;             // "AutokickOn"
constexpr uint32_t kSpinTapsOn = 0xA483BA67;             // "SpinTapsOn"
constexpr uint32_t kTaunts = 0xE62B6586;                 // "Taunts"
constexpr uint32_t kPros = 0xC9986BAF;                   // "Pros"
constexpr uint32_t kGlobalInfo = 0xF55CBD13;             // "GlobalInfo"
constexpr uint32_t kStory = 0x14A9FBC7;                  // "Story"
constexpr uint32_t kStorySkater = 0x0DF2F448;            // "StorySkater"
constexpr uint32_t kCustomSkater = 0x12BFAC82;           // "CustomSkater"
constexpr uint32_t kGoalManagerParams = 0xAC7C2B62;      // "GoalManagerParams"
constexpr uint32_t kLayerInfos = 0x9A5FF0A3;             // "layer_infos"
constexpr uint32_t kGoalEditor = 0x81F01058;             // "GoalEditor": the object and its component
constexpr int kControllers = 8;

Mdl::Skate* Skate() { return Mdl::Skate::Instance(); }
Obj::CPlayerProfileManager* Profiles() { return Obj::g_profileManager; }
Records::CGameRecords* GameRecords() { return Records::g_gameRecords; }
GameNet::Manager* GameNetManager() { return GameNet::g_gameNetManager; }
Obj::CSkaterCareer* Career() { return *reinterpret_cast<Obj::CSkaterCareer**>(reinterpret_cast<uint8_t*>(Skate()) + 0x20); }
int Platform() { return GameConfig::g_platform; }

// The skate module's split-screen preferences (0x00564D80, no source yet).
Prefs::Preferences* SplitScreenPreferences()
{
    return reinterpret_cast<Prefs::Preferences*(__thiscall*)(Mdl::Skate*)>(CodeAddress(&Mdl::Skate::GetSplitScreenPreferences))(Skate());
}
// A preferences object's structure (+4).
CStruct* PreferencesStructure(Prefs::Preferences* preferences)
{
    return reinterpret_cast<CStruct*>(reinterpret_cast<uint8_t*>(preferences) + 4);
}

// The goal editor's component (its object's, both named GoalEditor).
Obj::CBaseComponent* GoalEditorComponent()
{
    Obj::CObject* object = Obj::g_tracker->LookUpObject(kGoalEditor);
    return static_cast<Obj::CCompositeObject*>(object)->GetComponent(kGoalEditor);
}

// The skate module's controller settings: each controller's three flags (+0x394 + 8 * n: vibration, autokick,
// spin taps).
uint8_t* ControllerFlags(int controller) { return reinterpret_cast<uint8_t*>(Skate()) + 0x394 + 8 * controller; }

CStruct* NewStructure()
{
    CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

} // namespace

// 0x005A7560
void WriteGlobalInfo(CStruct* global)
{
    CStruct* options = NewStructure();
    Prefs::Preferences* splitScreen = SplitScreenPreferences();
    CStruct* splitScreenCopy = NewStructure();
    splitScreenCopy->AppendStructureIfAny(PreferencesStructure(splitScreen));
    options->AddStructurePointer(kSplitScreenPreferences, splitScreenCopy);

    CStruct* sound = NewStructure();
    sound->AddFloat(kMainVolume, Sfx::GetMainVolume());
    sound->AddFloat(kMusicVolume, Script::GetFloat(kMusicVolOutsideBox));
    uint64_t tracksOff[2];
    Pcm::GetTracksOff(&tracksOff[0], &tracksOff[1]);
    sound->AddInteger(kForbiddenTracks1, static_cast<int>(tracksOff[0] >> 32));
    sound->AddInteger(kForbiddenTracks2, static_cast<int>(tracksOff[0]));
    sound->AddInteger(kForbiddenTracks3, static_cast<int>(tracksOff[1] >> 32));
    sound->AddInteger(kForbiddenTracks4, static_cast<int>(tracksOff[1]));
    if (Platform() == 4)
        sound->AddChecksum(kCurrentSoundtrack, Script::GetSymbolValue("current_soundtrack"));
    if (Pcm::RandomTrackOrder())
        sound->AddChecksum(0u, kRandomMode);
    options->AddStructurePointer(kSoundOptions, sound);

    CArray* controllers = Script::NewArray();
    if (controllers)
        controllers = controllers->Init();
    controllers->SetSizeAndType(kControllers, Script::ESYMBOLTYPE_STRUCTURE);
    for (int i = 0; i < kControllers; ++i) {
        CStruct* controller = NewStructure();
        const uint8_t* flags = ControllerFlags(i);
        if (flags[1])
            controller->AddChecksum(0u, kAutokickOn);
        if (flags[2])
            controller->AddChecksum(0u, kSpinTapsOn);
        if (flags[0])
            controller->AddChecksum(0u, kVibrationOn);
        controllers->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(controller));
    }
    options->AddArrayPointer(kControllerPreferences, controllers);
    options->AddStructure(kTaunts, PreferencesStructure(GameNetManager()->GetTauntPreferences()));
    global->AddStructurePointer(kOptions, options);

    Career()->WriteToStructure(global);
    GameRecords()->WriteToStructure(global);
    CStruct* pros = NewStructure();
    Profiles()->WritePros(pros);
    global->AddStructurePointer(kPros, pros);
}

// 0x005A7CF0
void ReadGlobalInfo(CStruct* global, CScript* script)
{
    CStruct* options = nullptr;
    global->GetStructure(kOptions, &options, 0);
    CStruct* splitScreen = nullptr;
    options->GetStructure(kSplitScreenPreferences, &splitScreen, 0);
    SplitScreenPreferences()->SetPreferences(splitScreen);
    CStruct* taunts = nullptr;
    options->GetStructure(kTaunts, &taunts, 0);
    GameNetManager()->GetTauntPreferences()->SetPreferences(taunts);

    CStruct* sound = nullptr;
    options->GetStructure(kSoundOptions, &sound, 0);
    auto* sfx = Sfx::g_sfxManager;
    float mainVolume = 0.0f;
    if (sound->GetFloat(kMainVolume, &mainVolume, 0))
        sfx->SetMainVolume(mainVolume);
    float musicVolume = 0.0f;
    if (sound->GetFloat(kMusicVolume, &musicVolume, 0)) {
        Pcm::SetMusicVolume(musicVolume);
        CStruct* change = NewStructure();
        change->AddFloat(kMusicVolOutsideBox, musicVolume);
        Script::RunScript("Change", change, nullptr, false);
        DeleteStructure(change);
    }
    int tracks[4] = {};
    if (sound->GetInteger(kForbiddenTracks1, &tracks[0], 0)) {
        sound->GetInteger(kForbiddenTracks2, &tracks[1], 0);
        sound->GetInteger(kForbiddenTracks3, &tracks[2], 0);
        sound->GetInteger(kForbiddenTracks4, &tracks[3], 0);
        uint64_t low = (static_cast<uint64_t>(static_cast<uint32_t>(tracks[0])) << 32) | static_cast<uint32_t>(tracks[1]);
        uint64_t high = (static_cast<uint64_t>(static_cast<uint32_t>(tracks[2])) << 32) | static_cast<uint32_t>(tracks[3]);
        Pcm::SetTracksOff(low, high);
    }
    Pcm::SetRandomMode(sound->ContainsFlag(kRandomMode) ? 1 : 0);
    if (Platform() == 4) {
        uint32_t soundtrack = 0xFFFFFFFF;
        sound->GetChecksum(kCurrentSoundtrack, &soundtrack, 0);
        if (Script::CSymbolTableEntry* symbol = Script::FindSymbol(kCurrentSoundtrack))
            symbol->checksum = soundtrack;
        CStruct* change = NewStructure();
        change->AddChecksum(0x8C465905u, soundtrack); // trackchecksum
        Script::RunScript(0xD44400C2u, change, nullptr, false, nullptr); // set_loaded_soundtrack
        DeleteStructure(change);
    }

    CArray* controllers = nullptr;
    if (options->GetArray(kControllerPreferences, &controllers, 0)) {
        Mdl::Skate* skate = Skate();
        for (int i = 0; i < kControllers; ++i) {
            auto* controller = reinterpret_cast<CStruct*>(controllers->GetElement(static_cast<uint32_t>(i)));
            // the skate module's setters (no source yet): vibration, autokick, spin taps
            skate->SetVibration(i, controller->ContainsFlag(kVibrationOn));
            skate->SetAutoKick(i, controller->ContainsFlag(kAutokickOn));
            skate->SetSpinTaps(i, controller->ContainsFlag(kSpinTapsOn));
        }
    }
    GameRecords()->ReadFromStructure(global);
    CStruct* pros = nullptr;
    global->GetStructure(kPros, &pros, 1);
    Profiles()->ReadPros(pros);
    Career()->ReadFromStructure(global);

    constexpr uint32_t kLastLevelLoadScript = 0xE3335D2F, kLastGameMode = 0x2CC06F5E;
    CStruct* career = nullptr;
    global->GetStructure(0x4DA4937Bu, &career, 0); // career
    uint32_t lastLevel = 0;
    uint32_t lastMode = 0;
    if (career) {
        career->GetChecksum(kLastLevelLoadScript, &lastLevel, 0);
        career->GetChecksum(kLastGameMode, &lastMode, 0);
    }
    CStruct* results = *reinterpret_cast<CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14);
    if (lastLevel)
        results->AddChecksum(kLastLevelLoadScript, lastLevel);
    if (lastMode)
        results->AddChecksum(kLastGameMode, lastMode);

    // the first profile: a custom skater keeps its CAS file name
    // (0x00564F80, the overload taking the params - none: not GetProfile(int))
    auto* profile = Skate()->GetProfile(static_cast<CStruct*>(nullptr));
    if (!profile->IsPro()) {
        const char* casFileName = "Unimplemented"; // "Unimplemented"
        global->GetString(0xF36C1878u, &casFileName, 0); // CASFileName
        profile->SetStringF36C1878(casFileName);
    }
}

// 0x005A79A0
void WriteSaveContents(CStruct* contents, uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals:
        // the goal editor's goals written (0x004EB850, no source yet)
        static_cast<Ed::CGoalEditorComponent*>(GoalEditorComponent())->WriteGoals(contents);
        return;
    case kSaveCat: {
        auto* skater = static_cast<Obj::CSkater*>(Skate()->GetSkater(0));
        uint8_t* trickInfo = skater->m_trickInfo[0];
        contents->AddStructure("other_params", *reinterpret_cast<CStruct**>(trickInfo + 4));
        contents->AddArray("rotation_info", *reinterpret_cast<CArray**>(trickInfo + 8));
        contents->AddArray("animation_info", *reinterpret_cast<CArray**>(trickInfo + 0xC));
        return;
    }
    case kSavePark:
        Ed::CParkEditor::Instance()->WriteIntoStructure(contents);
        return;
    case kSaveNetworkSettings:
        contents->AppendStructureIfAny(PreferencesStructure(GameNetManager()->GetNetworkPreferences()));
        return;
    case kSaveOptionsAndPros: {
        CStruct* global = NewStructure();
        WriteGlobalInfo(global);
        contents->AddStructurePointer(kGlobalInfo, global);
        CStruct* story = NewStructure();
        void* goalManager = Game::GetGoalManager();
        // the goal manager's parameters (a folded getter, +0x14)
        story->AddStructure(kGoalManagerParams, reinterpret_cast<CStruct*(__thiscall*)(void*)>(CodeAddress(&Gfx::CSkeleton::GetNumBones))(goalManager));
        contents->AddStructurePointer(kStory, story);
        CStruct* storySkater = NewStructure();
        auto* skater = static_cast<Obj::CSkater*>(Skate()->GetSkater(0));
        skater->WriteCATInfo(storySkater);
        skater->WriteList44B66A9F(storySkater);
        skater->WriteChapterInfo(storySkater);
        contents->AddStructurePointer(kStorySkater, storySkater);
        CStruct* customSkater = NewStructure();
        Profiles()->WriteCustomSkaters(customSkater);
        contents->AddStructurePointer(kCustomSkater, customSkater);
        // (on into CreatedGraphic's layer_infos, as the original does)
        contents->AddArray(kLayerInfos, Script::GetArray(0x8EAD7D43u)); // edit_graphic_layer_infos
        return;
    }
    case kSaveCreatedGraphic:
        contents->AddArray(kLayerInfos, Script::GetArray(0x8EAD7D43u)); // edit_graphic_layer_infos
        return;
    default:
        return;
    }
}

// 0x005A81B0
void ApplyLoadedContents(uint32_t type, CStruct* contents, CScript* script, CStruct* params)
{
    bool& loadedCustomSkater = g_loadedCustomSkater;
    loadedCustomSkater = false;
    switch (type) {
    case kSaveCreatedGoals:
        // the goal editor's goals read (0x004EA470, no source yet)
        static_cast<Ed::CGoalEditorComponent*>(GoalEditorComponent())->ReadGoals(contents, 0);
        return;
    case kSaveCat: {
        // the local skater (0x00566520, no source yet)
        Obj::CSkater* skater = Skate()->GetLocalSkater();
        if (!skater)
            return;
        CStruct* other = nullptr;
        contents->GetStructure("other_params", &other, 1);
        reinterpret_cast<CStruct*>(*reinterpret_cast<CStruct**>(skater->m_trickInfo[0] + 4))->AppendStructureIfAny(other);
        CArray* rotation = nullptr;
        contents->GetArray("rotation_info", &rotation, 1);
        Script::CopyArray(*reinterpret_cast<CArray**>(skater->m_trickInfo[0] + 8), rotation);
        CArray* animation = nullptr;
        contents->GetArray("animation_info", &animation, 1);
        Script::CopyArray(*reinterpret_cast<CArray**>(skater->m_trickInfo[0] + 0xC), animation);
        return;
    }
    case kSavePark:
        Ed::CParkEditor::Instance()->ReadFromStructure(contents, true);
        return;
    case kSaveCreatedGraphic: {
        CArray* layers = nullptr;
        contents->GetArray(kLayerInfos, &layers, 1);
        params->AddArray(kLayerInfos, layers);
        return;
    }
    case kSaveNetworkSettings:
        GameNetManager()->GetNetworkPreferences()->SetPreferences(contents);
        return;
    case kSaveOptionsAndPros: {
        uint32_t& sections = SectionsToApplyWhenLoading();
        if (sections & 1) {
            Dbg::Printf("APPLY_GLOBAL_INFO\n"); // "APPLY_GLOBAL_INFO\n"
            CStruct* global = nullptr;
            contents->GetStructure(kGlobalInfo, &global, 1);
            ReadGlobalInfo(global, script);
        }
        if (sections & 2) {
            Dbg::Printf("APPLY_STORY\n"); // "APPLY_STORY\n"
            CStruct* story = nullptr;
            contents->GetStructure(kStory, &story, 1);
            ApplyGoalManagerParams(story);
        }
        if (sections & 8) {
            Dbg::Printf("APPLY_CUSTOM_SKATER\n"); // "APPLY_CUSTOM_SKATER\n"
            CStruct* customSkater = nullptr;
            contents->GetStructure(kCustomSkater, &customSkater, 1);
            Obj::CPlayerProfileManager* profiles = Profiles();
            // the stats kept unless the current skater is the first (the manager's +0x1C, 0x00553E90)
            bool keep = reinterpret_cast<int(__thiscall*)(void*)>(CodeAddress(&Game::CGoal::GetParams))(profiles) != 0;
            profiles->ReadCustomSkater(customSkater, keep);
            loadedCustomSkater = true;
        }
        if (sections & 4) {
            Dbg::Printf("APPLY_STORY_SKATER\n"); // "APPLY_STORY_SKATER\n"
            CStruct* storySkater = nullptr;
            contents->GetStructure(kStorySkater, &storySkater, 1);
            CStruct* customSkater = nullptr;
            contents->GetStructure(kCustomSkater, &customSkater, 1);
            ApplyCatAndCustomSkater(storySkater, customSkater);
        }
        CArray* layers = nullptr;
        contents->GetArray(kLayerInfos, &layers, 1);
        params->AddArray(kLayerInfos, layers);
        sections = 0;
        return;
    }
    default:
        return;
    }
}

// --- the register entry points ---

namespace {

void __cdecl WriteSaveContentsRegisters(CStruct* contents, uint32_t type) { WriteSaveContents(contents, type); }
void __cdecl ApplyLoadedContentsRegisters(uint32_t type, CStruct* contents, CScript* script, CStruct* params)
{
    ApplyLoadedContents(type, contents, script, params);
}

} // namespace

// 0x005A79A0: the structure in EBX, the type on the stack (the caller pops it).
__declspec(naked) void WriteSaveContentsStub()
{
    __asm {
        push dword ptr [esp + 4]
        push ebx
        call WriteSaveContentsRegisters
        add esp, 8
        ret
    }
}

// 0x005A81B0: the type in EAX, the contents in ESI, the script and params on the stack (the caller pops them).
__declspec(naked) void ApplyLoadedContentsStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push esi
        push eax
        call ApplyLoadedContentsRegisters
        add esp, 16
        ret
    }
}

} // namespace Mc
