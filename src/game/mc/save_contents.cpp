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
#include "game/obj/entity.h"
#include "game/obj/tracker.h"
#include "game/pcm/music_player.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/param_struct.h"
#include "game/sfx/sfx.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/highscores.h"
#include "game/skate/skate_module.h"
#include "game/skater/rider.h"
#include "game/sys/game_config.h"
#include "game/goals/objective.h"
#include "game/gfx/rig.h"

namespace MemCard {

namespace {

using Scripts::CArray;
using Scripts::CScript;
using Scripts::CStruct;

// The names of the save's parts.
constexpr uint32_t kOptions = 0x2FCA0578;                // "Options"
constexpr uint32_t kSplitScreenPreferences = 0xF7720C3F; // "SplitScreenPreferences"
constexpr uint32_t kSoundOptions = 0x89EB9738;           // "SoundOptions"
constexpr uint32_t kMainVolume = 0x6F016DFB;             // "MasterLoudness"
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

GameModules::Skate* Skate() { return GameModules::Skate::Sole(); }
GameObjects::CProfileRoster* Profiles() { return GameObjects::g_profileManager; }
Highscores::CBestResults* GameRecords() { return Highscores::g_gameRecords; }
OnlinePlay::Hub* GameNetManager() { return OnlinePlay::g_gameNetManager; }
GameObjects::CRiderStoryProgress* Career() { return *reinterpret_cast<GameObjects::CRiderStoryProgress**>(reinterpret_cast<uint8_t*>(Skate()) + 0x20); }
int OsLayer() { return GameConfig::g_platform; }

// The skate module's split-screen preferences (0x00564D80, no source yet).
OptionStore::Preferences* SplitScreenPreferences()
{
    return reinterpret_cast<OptionStore::Preferences*(__thiscall*)(GameModules::Skate*)>(CodeAddress(&GameModules::Skate::GetSplitViewSettings))(Skate());
}
// A preferences object's structure (+4).
CStruct* PreferencesStructure(OptionStore::Preferences* preferences)
{
    return reinterpret_cast<CStruct*>(reinterpret_cast<uint8_t*>(preferences) + 4);
}

// The goal editor's component (its object's, both named GoalEditor).
GameObjects::CObjectModule* GoalEditorComponent()
{
    GameObjects::CEntity* object = GameObjects::g_tracker->LookUpObject(kGoalEditor);
    return static_cast<GameObjects::CGameEntity*>(object)->FetchModule(kGoalEditor);
}

// The skate module's controller settings: each controller's three flags (+0x394 + 8 * n: vibration, autokick,
// spin taps).
uint8_t* ControllerFlags(int controller) { return reinterpret_cast<uint8_t*>(Skate()) + 0x394 + 8 * controller; }

CStruct* NewStructure()
{
    CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

} // namespace

// 0x005A7560
void WriteGlobalInfo(CStruct* global)
{
    CStruct* options = NewStructure();
    OptionStore::Preferences* splitScreen = SplitScreenPreferences();
    CStruct* splitScreenCopy = NewStructure();
    splitScreenCopy->AppendStructureIfAny(PreferencesStructure(splitScreen));
    options->AppendStructRef(kSplitScreenPreferences, splitScreenCopy);

    CStruct* sound = NewStructure();
    sound->PushFloat(kMainVolume, SoundFx::FetchMasterLoudness());
    sound->PushFloat(kMusicVolume, Scripts::FetchFloat(kMusicVolOutsideBox));
    uint64_t tracksOff[2];
    AudioStream::GetTracksOff(&tracksOff[0], &tracksOff[1]);
    sound->PutInt(kForbiddenTracks1, static_cast<int>(tracksOff[0] >> 32));
    sound->PutInt(kForbiddenTracks2, static_cast<int>(tracksOff[0]));
    sound->PutInt(kForbiddenTracks3, static_cast<int>(tracksOff[1] >> 32));
    sound->PutInt(kForbiddenTracks4, static_cast<int>(tracksOff[1]));
    if (OsLayer() == 4)
        sound->PutNameKey(kCurrentSoundtrack, Scripts::GetSymbolValue("current_soundtrack"));
    if (AudioStream::RandomTrackOrder())
        sound->PutNameKey(0u, kRandomMode);
    options->AppendStructRef(kSoundOptions, sound);

    CArray* controllers = Scripts::NewArray();
    if (controllers)
        controllers = controllers->PrepareAll();
    controllers->ConfigureSizeKind(kControllers, Scripts::QBVAL_STRUCT);
    for (int i = 0; i < kControllers; ++i) {
        CStruct* controller = NewStructure();
        const uint8_t* flags = ControllerFlags(i);
        if (flags[1])
            controller->PutNameKey(0u, kAutokickOn);
        if (flags[2])
            controller->PutNameKey(0u, kSpinTapsOn);
        if (flags[0])
            controller->PutNameKey(0u, kVibrationOn);
        controllers->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(controller));
    }
    options->AppendArrayRef(kControllerPreferences, controllers);
    options->PutSubStruct(kTaunts, PreferencesStructure(GameNetManager()->GetTauntSettings()));
    global->AppendStructRef(kOptions, options);

    Career()->WriteToStructure(global);
    GameRecords()->WriteToStructure(global);
    CStruct* pros = NewStructure();
    Profiles()->WritePros(pros);
    global->AppendStructRef(kPros, pros);
}

// 0x005A7CF0
void ReadGlobalInfo(CStruct* global, CScript* script)
{
    CStruct* options = nullptr;
    global->ReadSubStruct(kOptions, &options, 0);
    CStruct* splitScreen = nullptr;
    options->ReadSubStruct(kSplitScreenPreferences, &splitScreen, 0);
    SplitScreenPreferences()->SetPreferences(splitScreen);
    CStruct* taunts = nullptr;
    options->ReadSubStruct(kTaunts, &taunts, 0);
    GameNetManager()->GetTauntSettings()->SetPreferences(taunts);

    CStruct* sound = nullptr;
    options->ReadSubStruct(kSoundOptions, &sound, 0);
    auto* sfx = SoundFx::g_sfxManager;
    float mainVolume = 0.0f;
    if (sound->FetchFloat(kMainVolume, &mainVolume, 0))
        sfx->ChangeMasterLoudness(mainVolume);
    float musicVolume = 0.0f;
    if (sound->FetchFloat(kMusicVolume, &musicVolume, 0)) {
        AudioStream::SetMusicVolume(musicVolume);
        CStruct* change = NewStructure();
        change->PushFloat(kMusicVolOutsideBox, musicVolume);
        Scripts::LaunchScript("Change", change, nullptr, false);
        DeleteStructure(change);
    }
    int tracks[4] = {};
    if (sound->FetchIntValue(kForbiddenTracks1, &tracks[0], 0)) {
        sound->FetchIntValue(kForbiddenTracks2, &tracks[1], 0);
        sound->FetchIntValue(kForbiddenTracks3, &tracks[2], 0);
        sound->FetchIntValue(kForbiddenTracks4, &tracks[3], 0);
        uint64_t low = (static_cast<uint64_t>(static_cast<uint32_t>(tracks[0])) << 32) | static_cast<uint32_t>(tracks[1]);
        uint64_t high = (static_cast<uint64_t>(static_cast<uint32_t>(tracks[2])) << 32) | static_cast<uint32_t>(tracks[3]);
        AudioStream::SetTracksOff(low, high);
    }
    AudioStream::SetRandomMode(sound->HasBareFlag(kRandomMode) ? 1 : 0);
    if (OsLayer() == 4) {
        uint32_t soundtrack = 0xFFFFFFFF;
        sound->ReadHashValue(kCurrentSoundtrack, &soundtrack, 0);
        if (Scripts::CSymbolTableEntry* symbol = Scripts::FindSymbol(kCurrentSoundtrack))
            symbol->checksum = soundtrack;
        CStruct* change = NewStructure();
        change->PutNameKey(0x8C465905u, soundtrack); // trackchecksum
        Scripts::LaunchScript(0xD44400C2u, change, nullptr, false, nullptr); // set_loaded_soundtrack
        DeleteStructure(change);
    }

    CArray* controllers = nullptr;
    if (options->FetchList(kControllerPreferences, &controllers, 0)) {
        GameModules::Skate* skate = Skate();
        for (int i = 0; i < kControllers; ++i) {
            auto* controller = reinterpret_cast<CStruct*>(controllers->FetchPart(static_cast<uint32_t>(i)));
            // the skate module's setters (no source yet): vibration, autokick, spin taps
            skate->ChangeRumble(i, controller->HasBareFlag(kVibrationOn));
            skate->SetAutoEject(i, controller->HasBareFlag(kAutokickOn));
            skate->SetTurnTapCount(i, controller->HasBareFlag(kSpinTapsOn));
        }
    }
    GameRecords()->DecodeFromStruct(global);
    CStruct* pros = nullptr;
    global->ReadSubStruct(kPros, &pros, 1);
    Profiles()->ReadPros(pros);
    Career()->DecodeFromStruct(global);

    constexpr uint32_t kLastLevelLoadScript = 0xE3335D2F, kLastGameMode = 0x2CC06F5E;
    CStruct* career = nullptr;
    global->ReadSubStruct(0x4DA4937Bu, &career, 0); // career
    uint32_t lastLevel = 0;
    uint32_t lastMode = 0;
    if (career) {
        career->ReadHashValue(kLastLevelLoadScript, &lastLevel, 0);
        career->ReadHashValue(kLastGameMode, &lastMode, 0);
    }
    CStruct* results = *reinterpret_cast<CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14);
    if (lastLevel)
        results->PutNameKey(kLastLevelLoadScript, lastLevel);
    if (lastMode)
        results->PutNameKey(kLastGameMode, lastMode);

    // the first profile: a custom skater keeps its CAS file name
    // (0x00564F80, the overload taking the params - none: not FetchAccount(int))
    auto* profile = Skate()->FetchAccount(static_cast<CStruct*>(nullptr));
    if (!profile->IsPro()) {
        const char* casFileName = "Unimplemented"; // "Unimplemented"
        global->FetchText(0xF36C1878u, &casFileName, 0); // CASFileName
        profile->SetStringF36C1878(casFileName);
    }
}

// 0x005A79A0
void WriteSaveContents(CStruct* contents, uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals:
        // the goal editor's goals written (0x004EB850, no source yet)
        static_cast<ParkEditing::CObjectiveAuthoring*>(GoalEditorComponent())->WriteGoals(contents);
        return;
    case kSaveCat: {
        auto* skater = static_cast<GameObjects::CRider*>(Skate()->GetRiderAt(0));
        uint8_t* trickRecord = skater->m_trickInfo[0];
        contents->PutSubStruct("other_params", *reinterpret_cast<CStruct**>(trickRecord + 4));
        contents->AppendArrayValue("rotation_info", *reinterpret_cast<CArray**>(trickRecord + 8));
        contents->AppendArrayValue("animation_info", *reinterpret_cast<CArray**>(trickRecord + 0xC));
        return;
    }
    case kSavePark:
        ParkEditing::CParkBuilderMode::Sole()->ExportToStruct(contents);
        return;
    case kSaveNetworkSettings:
        contents->AppendStructureIfAny(PreferencesStructure(GameNetManager()->GetOnlineSettings()));
        return;
    case kSaveOptionsAndPros: {
        CStruct* global = NewStructure();
        WriteGlobalInfo(global);
        contents->AppendStructRef(kGlobalInfo, global);
        CStruct* story = NewStructure();
        void* goalManager = GoalSystem::GetObjectiveBoard();
        // the goal manager's parameters (a folded getter, +0x14)
        story->PutSubStruct(kGoalManagerParams, reinterpret_cast<CStruct*(__thiscall*)(void*)>(CodeAddress(&Gfx2D::CRig::GetNumBones))(goalManager));
        contents->AppendStructRef(kStory, story);
        CStruct* storySkater = NewStructure();
        auto* skater = static_cast<GameObjects::CRider*>(Skate()->GetRiderAt(0));
        skater->WriteCATInfo(storySkater);
        skater->WriteList44B66A9F(storySkater);
        skater->WriteChapterInfo(storySkater);
        contents->AppendStructRef(kStorySkater, storySkater);
        CStruct* customSkater = NewStructure();
        Profiles()->WriteCustomSkaters(customSkater);
        contents->AppendStructRef(kCustomSkater, customSkater);
        // (on into CreatedGraphic's layer_infos, as the original does)
        contents->AppendArrayValue(kLayerInfos, Scripts::FetchList(0x8EAD7D43u)); // edit_graphic_layer_infos
        return;
    }
    case kSaveCreatedGraphic:
        contents->AppendArrayValue(kLayerInfos, Scripts::FetchList(0x8EAD7D43u)); // edit_graphic_layer_infos
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
        static_cast<ParkEditing::CObjectiveAuthoring*>(GoalEditorComponent())->ReadGoals(contents, 0);
        return;
    case kSaveCat: {
        // the local skater (0x00566520, no source yet)
        GameObjects::CRider* skater = Skate()->GetHomeRider();
        if (!skater)
            return;
        CStruct* other = nullptr;
        contents->ReadSubStruct("other_params", &other, 1);
        reinterpret_cast<CStruct*>(*reinterpret_cast<CStruct**>(skater->m_trickInfo[0] + 4))->AppendStructureIfAny(other);
        CArray* rotation = nullptr;
        contents->FetchList("rotation_info", &rotation, 1);
        Scripts::DuplicateArray(*reinterpret_cast<CArray**>(skater->m_trickInfo[0] + 8), rotation);
        CArray* animation = nullptr;
        contents->FetchList("animation_info", &animation, 1);
        Scripts::DuplicateArray(*reinterpret_cast<CArray**>(skater->m_trickInfo[0] + 0xC), animation);
        return;
    }
    case kSavePark:
        ParkEditing::CParkBuilderMode::Sole()->DecodeFromStruct(contents, true);
        return;
    case kSaveCreatedGraphic: {
        CArray* layers = nullptr;
        contents->FetchList(kLayerInfos, &layers, 1);
        params->AppendArrayValue(kLayerInfos, layers);
        return;
    }
    case kSaveNetworkSettings:
        GameNetManager()->GetOnlineSettings()->SetPreferences(contents);
        return;
    case kSaveOptionsAndPros: {
        uint32_t& sections = SectionsToApplyWhenLoading();
        if (sections & 1) {
            DebugOut::Printf("APPLY_GLOBAL_INFO\n"); // "APPLY_GLOBAL_INFO\n"
            CStruct* global = nullptr;
            contents->ReadSubStruct(kGlobalInfo, &global, 1);
            ReadGlobalInfo(global, script);
        }
        if (sections & 2) {
            DebugOut::Printf("APPLY_STORY\n"); // "APPLY_STORY\n"
            CStruct* story = nullptr;
            contents->ReadSubStruct(kStory, &story, 1);
            ApplyGoalManagerParams(story);
        }
        if (sections & 8) {
            DebugOut::Printf("APPLY_CUSTOM_SKATER\n"); // "APPLY_CUSTOM_SKATER\n"
            CStruct* customSkater = nullptr;
            contents->ReadSubStruct(kCustomSkater, &customSkater, 1);
            GameObjects::CProfileRoster* profiles = Profiles();
            // the stats kept unless the current skater is the first (the manager's +0x1C, 0x00553E90)
            bool keep = reinterpret_cast<int(__thiscall*)(void*)>(CodeAddress(&GoalSystem::CObjective::FetchArgs))(profiles) != 0;
            profiles->ReadCustomSkater(customSkater, keep);
            loadedCustomSkater = true;
        }
        if (sections & 4) {
            DebugOut::Printf("APPLY_STORY_SKATER\n"); // "APPLY_STORY_SKATER\n"
            CStruct* storySkater = nullptr;
            contents->ReadSubStruct(kStorySkater, &storySkater, 1);
            CStruct* customSkater = nullptr;
            contents->ReadSubStruct(kCustomSkater, &customSkater, 1);
            ApplyCatAndCustomSkater(storySkater, customSkater);
        }
        CArray* layers = nullptr;
        contents->FetchList(kLayerInfos, &layers, 1);
        params->AppendArrayValue(kLayerInfos, layers);
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
