// Script commands (CFunctions) of the skate game, 0x0059F610-0x0059FE50: the viewer, controllers, node arrays,
// preferences, some network flags (see game_cfuncs.h).

#include "game/script/game_cfuncs.h"
#include "game/net/task_vtable.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/assets/resource_registry.h"
#include "game/assets/resource_entry.h"
#include "game/core/checksums.h"
#include "game/file/file_load.h"
#include "game/file/packs.h"
#include "game/gfx/tod_manager.h"
#include "game/maths/vec4.h"
#include "game/obj/rail_manager.h"
#include "game/script/nodes.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/obj/compositemgr.h"
#include "game/obj/input_component.h"
#include "game/pad_input/input_hub.h"
#include "game/skater/trick_component.h"
#include "game/sys/mlp.h"
#include "game/sys/stopwatch.h"
#include "game/file/async.h"
#include "game/front/front_end.h"
#include "game/gfx/view_cam.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/entity.h"
#include "game/obj/proximity.h"
#include "game/obj/entity_hub.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/preferences.h"
#include "game/script/skate_cfuncs.h"
#include "game/script/param_struct.h"
#include "game/pad_input/device.h"
#include "game/skate/profile_manager.h"
#include "game/skater/rider.h"
#include "game/sys/model_viewer.h"
#include "game/sys/viewer_object.h"
#include "game/skate/skate_module.h"
#include "game/gfx/viewport.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/face_texture.h"
#include "game/sys/winmain.h"
#include "game/gfx/movie.h"
#include "game/sys/crt.h"

namespace ScriptCommands {

bool g_tryingToJoin;
char g_nodeArrayName[128];
void* g_secondControllerCheck;
int g_viewModeCfunc;
char g_passwordStars[32];
bool g_gotExtraMemory;

namespace {

constexpr uint32_t kModel = 0x286A8D26;           // "model" (the component)
constexpr uint32_t kCamera = 0xC4E311FA;          // "Camera" (an object's camera component)
constexpr uint32_t kController = 0xB30D9965;      // "controller"
constexpr uint32_t kLeftX = 0x303067F1;           // "leftx"
constexpr uint32_t kLeftY = 0x47375767;           // "lefty"
constexpr uint32_t kRightX = 0x694DF774;          // "rightx"
constexpr uint32_t kRightY = 0x1E4AC7E2;          // "righty"
constexpr uint32_t kPrefType = 0x17DAE143;        // "pref_type"
constexpr uint32_t kUiString = 0x96875C0F;        // "ui_string"
constexpr uint32_t kChecksum = 0x21902065;        // "checksum"
constexpr uint32_t kCustomSkaterFile = 0xF36C1878; // (name unknown) the profile's custom skater file name
constexpr uint32_t kBothControllersIn = 0x07C28B45;  // (name unknown) the script run while both are plugged in
constexpr uint32_t kControllerMissing = 0x9C9DFCF8;  // (name unknown) ... and while one isn't
constexpr uint32_t kViewerCameraObject = 0xEB17151B; // (name unknown) the object whose camera a view mode uses
constexpr uint32_t kSkaterCamObject = 0x967C138C;    // (name unknown) the object whose camera is used otherwise

// The skate module (GameModules::Skate, 0x007CE478) and its code we call through the game.
uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
GameObjects::CRiderIdentity* FetchAccount(int index)
{
    // (0x00564F80 is the overload taking the params: the original passes the index in its place - 0, no params, the
    // current profile)
    return reinterpret_cast<GameModules::Skate*>(Skate())->FetchAccount(reinterpret_cast<Scripts::CStruct*>(index));
}
GameObjects::CRider* GetRiderAt(int index)
{
    return static_cast<GameObjects::CRider*>(reinterpret_cast<GameModules::Skate*>(Skate())->GetRiderAt(index));
}

// The object manager (0x006F0FB8).
GameObjects::CObjectHubBase* Objects() { return reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager); }

// A viewport's camera (0x004A09C0) and making `camera` a viewport's (0x004A0970).
Gfx2D::ViewCam* ViewportCamera(int viewport) { return Engine3D::FetchLiveCamera(viewport); }
void SetViewportCamera(int viewport, void* camera)
{
    Engine3D::ChooseCamera(viewport, static_cast<Gfx2D::ViewCam*>(camera));
}

// The preferences of a type (0x00565680: the network's, the taunts', the skate module's; null for others). The
// original passes a second argument (1) that isn't read.
OptionStore::Preferences* GetPreferencesOfType(uint32_t type)
{
    return GameModules::Skate::GetPlayerOptions(type);
}

// The network game flags (OnlinePlay).
bool& InternetGameHost() { return SystemCore::g_internetGameHost; }
bool& JoiningInternetGame() { return SystemCore::g_joiningInternetGame; }
bool& TryingToJoin() { return g_tryingToJoin; }
bool& NetEnabled() { return OnlinePlay::g_netEnabled; }
bool& JoinServerIPSucceeded() { return OnlinePlay::g_joinServerIPSucceeded; }

// The second controller check's task (0x007CEBC4) and the view mode (0x007CEBCC).
void*& SecondControllerCheck() { return g_secondControllerCheck; }
int& ViewMode() { return g_viewModeCfunc; }

// GetPreferencePassword's row of stars (0x007CEBD0).
char* PasswordStars() { return g_passwordStars; }

GameObjects::CPreviewEntity* ViewerObject(GameModules::Viewer* viewer) { return static_cast<GameObjects::CPreviewEntity*>(viewer->GetViewerEntity()); }

} // namespace

// 0x0059F610: SetViewerAnim <anim>: the viewer's object plays the animation (if there's a viewer and an object).
bool __cdecl CFunc_SetViewerAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    if (GameModules::Viewer* viewer = GameModules::GetLiveViewer()) {
        uint32_t anim = 0; // (unset in the original when missing - an asserted parameter)
        params->ReadHashValue(0u, &anim, 1);
        if (GameObjects::CPreviewEntity* object = ViewerObject(viewer))
            object->PlayAnimation(anim);
    }
    return true;
}

// 0x0059F650: SetViewerLODDist (x, y): the viewer's object's model component gets colour value `x` (an index,
// truncated) set to `y`. False without the pair (with a viewer); true otherwise.
bool __cdecl CFunc_SetViewerLODDist(Scripts::CStruct* params, Scripts::CScript*)
{
    GameModules::Viewer* viewer = GameModules::GetLiveViewer();
    if (!viewer)
        return true;
    Scripts::CPair pair;
    pair.PrepareAll();
    if (!params->FetchPair(0u, &pair, false))
        return false;
    if (GameObjects::CPreviewEntity* object = ViewerObject(viewer)) {
        // (the model component isn't checked for null, as in the original)
        auto* model = reinterpret_cast<GameObjects::CMeshInstanceModule*>(object->FetchModule(kModel));
        model->SetColorValue(static_cast<int>(pair.x), pair.y);
    }
    return true;
}

// 0x0059F6C0: ReloadViewerAnim <anim> "<file>": the viewer's object reloads the animation from the file.
bool __cdecl CFunc_ReloadViewerAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    GameModules::Viewer* viewer = GameModules::GetLiveViewer();
    if (!viewer)
        return true;
    GameObjects::CPreviewEntity* object = ViewerObject(viewer);
    if (!object)
        return true;
    uint32_t anim;
    const char* file;
    if (params->ReadHashValue(0u, &anim, 0) && params->FetchText(0u, &file, 0))
        object->ReloadAnimation(file, anim);
    return true;
}

// 0x0059F730: CheckForHoles (0x0048C8F0).
bool __cdecl CFunc_CheckForHoles(Scripts::CStruct*, Scripts::CScript*)
{
    Engine3D::PrintCollStatics();
    return true;
}

// 0x0059F740: StopSecondControllerCheck: the check's task (if any) deleted.
bool __cdecl CFunc_StopSecondControllerCheck(Scripts::CStruct*, Scripts::CScript*)
{
    if (void* task = SecondControllerCheck()) {
        auto destroy = (*static_cast<void* (__thiscall***)(void*, unsigned int)>(task))[2];
        destroy(task, 1); // the scalar deleting destructor (vtable slot 2)
    }
    SecondControllerCheck() = nullptr;
    return true;
}

// 0x0059F760: QuitGame: the game ends (0x007D6A2C).
bool __cdecl CFunc_QuitGame(Scripts::CStruct*, Scripts::CScript*)
{
    Engine3D::g_quitRequested = true;
    return true;
}

// 0x0059F770: IsInternetGameHost.
bool __cdecl CFunc_IsInternetGameHost(Scripts::CStruct*, Scripts::CScript*) { return InternetGameHost(); }

// 0x0059F780: IsJoiningInternetGame (logged).
bool __cdecl CFunc_IsJoiningInternetGame(Scripts::CStruct*, Scripts::CScript*)
{
    DebugOut::Printf("ScriptIsJoiningInternetGame %d\n", JoiningInternetGame()); // "ScriptIsJoiningInternetGame %d\n"
    return JoiningInternetGame();
}

// 0x0059F7A0: TryingToJoinGame.
bool __cdecl CFunc_TryingToJoinGame(Scripts::CStruct*, Scripts::CScript*) { return TryingToJoin(); }

// 0x0059F7B0: TryJoinServerIPSuccess: whether joining by IP worked (forgotten once no join is being tried; logged).
bool __cdecl CFunc_TryJoinServerIPSuccess(Scripts::CStruct*, Scripts::CScript*)
{
    if (!TryingToJoin())
        JoinServerIPSucceeded() = false;
    DebugOut::Printf("ScriptTryJoinServerIPSuccess %d\n", JoinServerIPSucceeded()); // "ScriptTryJoinServerIPSuccess %d\n"
    return JoinServerIPSucceeded();
}

// 0x0059F7E0 / 0x0059F7F0: GSEnableNet / GSDisableNet.
bool __cdecl CFunc_GSEnableNet(Scripts::CStruct*, Scripts::CScript*)
{
    NetEnabled() = true;
    return true;
}

bool __cdecl CFunc_GSDisableNet(Scripts::CStruct*, Scripts::CScript*)
{
    NetEnabled() = false;
    return true;
}

// 0x0059F800: WaitForAsyncLoadToFinish: waits for every asynchronous file read.
bool __cdecl CFunc_WaitForAsyncLoadToFinish(Scripts::CStruct*, Scripts::CScript*)
{
    if (FileLoad::AsyncBusy())
        FileLoad::AsyncWaitForIO(true);
    return true;
}

// 0x0059F820
void __cdecl SecondControllerCheckTask(void*)
{
    Sio::Hub* devices = Sio::ManagerInstance();
    Sio::InputPort* first = devices->GetPadAtSlot(0);
    Sio::InputPort* second = devices->GetPadAtSlot(1);
    if (second->IsAttached() && first->IsAttached())
        Scripts::LaunchScript(kBothControllersIn, nullptr, nullptr, false, nullptr);
    else
        Scripts::LaunchScript(kControllerMissing, nullptr, nullptr, false, nullptr);
}

// 0x0059F890
bool __cdecl MakeCameraActive(uint32_t id, int viewport, bool takeCurrentView)
{
    auto* object = static_cast<GameObjects::CGameEntity*>(Objects()->LookUpEntity(id));
    if (!object)
        return false;
    GameObjects::CObjectModule* camera = object->FetchModule(kCamera);
    if (!camera)
        return true;
    if (takeCurrentView) {
        object->m_basisFrame = *ViewportCamera(viewport)->FetchMat();
        object->m_atPoint = *ViewportCamera(viewport)->FetchLocation();
        (*reinterpret_cast<void(__thiscall***)(void*)>(camera))[1](camera); // (vtable slot 1)
    }
    SetViewportCamera(viewport, reinterpret_cast<void**>(camera)[0x18 / 4]); // the component's camera (+0x18)
    return true;
}

// 0x0059F930: GetCustomSkaterFilename: the first profile's custom skater file name ("Unimplemented" if none) as
// the script's 0xF36C1878.
bool __cdecl CFunc_GetCustomSkaterFilename(Scripts::CStruct*, Scripts::CScript* script)
{
    GameObjects::CRiderIdentity* profile = FetchAccount(0);
    script->m_args->AppendWord(kCustomSkaterFile, profile->GetStringF36C1878());
    return true;
}

// 0x0059F960: SetCustomSkaterFilename ["<name>" ("Unimplemented")]: the first profile's custom skater file name.
bool __cdecl CFunc_SetCustomSkaterFilename(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = FetchAccount(0);
    const char* name = "Unimplemented"; // "Unimplemented"
    params->FetchText(0u, &name, 0);
    profile->SetStringF36C1878(name);
    return true;
}

// 0x0059F9A0: ControllerPressed <button> [controller]: whether the button is held on that controller (the front
// end's handler) - without a number, on either. False without a button.
bool __cdecl CFunc_ControllerPressed(Scripts::CStruct* params, Scripts::CScript*)
{
    GameModules::FrontEnd* frontEnd = GameModules::FrontEndInstance();
    uint32_t button = 0;
    if (!params->ReadHashValue(0u, &button, 0))
        return false;
    int controller;
    if (params->FetchIntValue(0u, &controller, 0))
        return ControllerPressed(frontEnd->m_inputHandlers[controller], button);
    for (int i = 0; i < 2; ++i) {
        if (ControllerPressed(frontEnd->m_inputHandlers[i], button))
            return true;
    }
    return false;
}

// 0x0059FA40: GetAnalogueInfo [controller=n (0)]: the controller's sticks as leftx / lefty / rightx / righty in
// the script's parameters - false (and none) if it isn't plugged in and valid.
bool __cdecl CFunc_GetAnalogueInfo(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameModules::FrontEnd* frontEnd = GameModules::FrontEndInstance();
    int controller = 0;
    params->FetchIntValue(kController, &controller, 0);
    GameModules::FrontEndInputHandler* handler = frontEnd->m_inputHandlers[controller];
    GameObjects::CPadState pad;
    pad.PrepareAll();
    pad.TickFrame(reinterpret_cast<const uint8_t*>(handler->m_controlData));
    auto* device = reinterpret_cast<Sio::InputPort*>(handler->m_dev);
    if (!device->IsAttached() || !device->m_isUsable)
        return false;
    script->m_args->PushFloat(kLeftX, pad.m_scaledStick[2]);
    script->m_args->PushFloat(kLeftY, pad.m_scaledStick[3]);
    script->m_args->PushFloat(kRightX, pad.m_scaledStick[0]);
    script->m_args->PushFloat(kRightY, pad.m_scaledStick[1]);
    return true;
}

// 0x0059FB10
void __cdecl DestroyObjectWithId(GameObjects::CEntity* object, void* id)
{
    if (object->m_ident == *static_cast<uint32_t*>(id))
        object->DisposeWhenFree();
}

// 0x0059FB30: NodeArrayManLoad "<file>": the node array's name noted (0x007CE6D8, copied unchecked) and the file
// loaded as an asset of type 11 (the node array manager's).
bool __cdecl CFunc_NodeArrayManLoad(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* file = nullptr;
    params->FetchText(0u, &file, 1);
    char* out = g_nodeArrayName;
    for (const char* in = file; (*out++ = *in++) != '\0';) {
    }
    AssetStore::AssetManager()->LoadAsset(file, 11, 0, 0, false, false, false, nullptr, 0);
    DebugOut::Printf("p_file = %s\n", file); // "p_file = %s\n"
    return true;
}

// 0x0059FB90: NodeArrayManUnload "<file>": the node manager drops the file's node array.
bool __cdecl CFunc_NodeArrayManUnload(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* file = nullptr;
    params->FetchText(0u, &file, 1);
    LevelNodes::NodeManagerInstance()->RemoveNodeArrayOfFile(file);
    return true;
}

// 0x0059FBC0: GetPreferenceString <field> pref_type=<type>: the preference's ui_string, as the script's ui_string.
// Without pref_type the original reads the type from the slot it would have been written to - the `script`
// argument's (kept: a type no set has, so a null set; the call on it crashes, as does a missing field's
// preference).
bool __cdecl CFunc_GetPreferenceString(Scripts::CStruct* params, Scripts::CScript* script)
{
    Scripts::CStruct* results = script->m_args;
    uint32_t field = 0;
    params->ReadHashValue(0u, &field, 0);
    uint32_t type = reinterpret_cast<uint32_t>(script);
    params->ReadHashValue(kPrefType, &type, 0);
    Scripts::CStruct* preference = GetPreferencesOfType(type)->FetchSetting(field);
    const char* text = nullptr; // (unset in the original when missing)
    preference->FetchText(kUiString, &text, 0);
    results->AppendWord(kUiString, text);
    return true;
}

// 0x0059FC40: GetPreferencePassword <field> pref_type=<type>: as many stars as the preference's ui_string has
// characters (in a static buffer, 0x007CEBD0, unchecked), as the script's password_string.
bool __cdecl CFunc_GetPreferencePassword(Scripts::CStruct* params, Scripts::CScript* script)
{
    Scripts::CStruct* results = script->m_args;
    uint32_t field = 0;
    params->ReadHashValue(0u, &field, 0);
    uint32_t type = reinterpret_cast<uint32_t>(script); // (the slot the original reads back: the argument's)
    params->ReadHashValue("pref_type", &type, 0); // "pref_type"
    Scripts::CStruct* preference = GetPreferencesOfType(type)->FetchSetting(field);
    const char* password = nullptr; // (unset in the original when missing)
    preference->FetchText("ui_string", &password, 0); // "ui_string"
    char* stars = PasswordStars();
    size_t length = std::strlen(password);
    for (size_t i = 0; i < length; ++i)
        stars[i] = '*';
    stars[length] = '\0';
    results->AppendWord("password_string", stars); // "password_string"
    return true;
}

// 0x0059FD00: GetPreferenceChecksum <field> pref_type=<type>: the preference's checksum, as the script's checksum.
bool __cdecl CFunc_GetPreferenceChecksum(Scripts::CStruct* params, Scripts::CScript* script)
{
    Scripts::CStruct* results = script->m_args;
    uint32_t field = 0;
    params->ReadHashValue(0u, &field, 0);
    uint32_t type = reinterpret_cast<uint32_t>(script); // (the slot the original reads back: the argument's)
    params->ReadHashValue("pref_type", &type, 0); // "pref_type"
    Scripts::CStruct* preference = GetPreferencesOfType(type)->FetchSetting(field);
    uint32_t value = 0; // (unset in the original when missing)
    preference->ReadHashValue(kChecksum, &value, 0);
    results->PutNameKey(kChecksum, value);
    return true;
}

// 0x0059FD80: ToggleNetMetrics.
bool __cdecl CFunc_ToggleNetMetrics(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::g_gameNetManager->ToggleNetMetrics();
    return true;
}

// 0x0059FD90: ChangeViewMode <mode>: the viewer's view mode (0 = off); in one, the camera of the object 0xEB17151B,
// else viewport 0 gets the camera of 0x967C138C (if there is one) back. Every skater's pause state is set to the
// mode, and the viewer lets go of its object.
namespace {

// The view mode `mode` (0x007CEBCC and the viewer's): in one, the camera of the object 0xEB17151B; else viewport 0
// gets the camera of 0x967C138C (if there is one) back. Every skater's pause state is set to the mode.
void ApplyViewMode(int mode)
{
    ViewMode() = mode;
    GameModules::ChangeViewMode(mode);
    if (ViewMode()) {
        MakeCameraActive(kViewerCameraObject, 0, true);
    } else if (GameObjects::CEntity* object = Objects()->LookUpEntity(kSkaterCamObject)) {
        if (GameObjects::CObjectModule* camera = static_cast<GameObjects::CGameEntity*>(object)->FetchModule(kCamera))
            SetViewportCamera(0, reinterpret_cast<void**>(camera)[0x18 / 4]);
    }
    for (int i = 0; i < 8; ++i) {
        if (GameObjects::CRider* skater = GetRiderAt(i))
            skater->SetPauseState(ViewMode());
    }
}

} // namespace

// 0x0059FD90: ChangeViewMode <mode>: the view mode (ApplyViewMode), and the viewer lets go of its object.
bool __cdecl CFunc_SetViewMode(Scripts::CStruct* params, Scripts::CScript*)
{
    int mode = 0; // (unset in the original when missing - an asserted parameter)
    params->FetchIntValue(0u, &mode, 1);
    ApplyViewMode(mode);
    if (GameModules::Viewer* viewer = GameModules::GetLiveViewer())
        viewer->DropViewerEntity();
    return true;
}

// 0x005A1C80: SetViewerModel <...>: view mode 1; the viewer (if any) places its camera and makes a viewer object
// from the parameters.
bool __cdecl CFunc_SetViewerModel(Scripts::CStruct* params, Scripts::CScript*)
{
    GameModules::Viewer* viewer = GameModules::GetLiveViewer();
    ApplyViewMode(1);
    if (viewer) {
        viewer->PlaceCamera(params);
        Scripts::DumpToLog(params, 0);
        viewer->CreateViewerObject(params);
    }
    return true;
}

// 0x005A1D40: GetViewerObjectID: the viewer object's id as the script's 0x0830ECAF and 0x52280066 (the object isn't
// checked for null).
bool __cdecl CFunc_GetViewerObjectID(Scripts::CStruct*, Scripts::CScript* script)
{
    if (GameModules::Viewer* viewer = GameModules::GetLiveViewer()) {
        GameObjects::CPreviewEntity* object = ViewerObject(viewer);
        script->m_args->PutNameKey(0x0830ECAF, object->m_ident);
        script->m_args->PutNameKey(0x52280066, object->m_ident);
    }
    return true;
}

// 0x005A1D80: ReloadAnim 0x52A864B0=<anim> 0x98549BA4=<base> 0xC3F4169A="<file>": the animation asset (by base +
// anim, or without a base the file name's checksum) unloaded and loaded again from the file (in the debug heap while
// 0x007CEFAC is set). False if it isn't loaded.
bool __cdecl CFunc_ReloadAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t anim = 0;
    params->ReadHashValue(0x52A864B0u, &anim, 0);
    uint32_t base = 0;
    params->ReadHashValue(0x98549BA4u, &base, 0);
    const char* file = ""; // ""
    params->FetchText(0xC3F4169Au, &file, 0);
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    uint32_t name = Checksums::HashFromText(file);
    if (base)
        name = base + anim;
    AssetStore::CResource* asset = assets->ObtainLoadedItem(name, 0);
    if (!asset)
        return false;
    assets->DiscardLoadedItem(asset);
    bool debugHeap = g_gotExtraMemory;
    if (debugHeap) {
        MemSys::Hub* memory = MemSys::Hub::Pull();
        memory->EnterHeapScope(reinterpret_cast<MemSys::MemSource*>(memory->m_debugHeap));
    }
    assets->LoadAnim(file, base, anim, false, false);
    if (g_gotExtraMemory)
        MemSys::Hub::Pull()->LeaveHeapScope();
    return true;
}

// 0x005A1E70: Debounce <button> [time=seconds (1)] [0x1A4E0EF9=n]: every input component locks the button for that
// long (released if n), and every trick component holds it off until the clock (ms) + time * 1000. Nothing without
// a button.
bool __cdecl CFunc_Debounce(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t button = 0;
    params->ReadHashValue(0u, &button, 0);
    if (!button)
        return true;
    float time = 1.0f;
    params->FetchFloat(0x906B67BAu, &time, 0); // "time"
    int release = 0;
    params->FetchIntValue(0x1A4E0EF9u, &release, 0);
    auto* objects = GameObjects::g_compositeObjectManager;
    for (GameObjects::CObjectModule* input = objects->FindFirstModuleOfKind(0x27D7CD28); input; input = input->mp_nextOfType)
        static_cast<GameObjects::CPadInputModule*>(input)->LockButton(button, time, release != 0);
    // x87: the unsigned clock + time * 1000.0 (0x006453C0), stored as a float
    float until = static_cast<float>(static_cast<double>(FrameClock::GetTime()) +
                                     static_cast<double>(time) * static_cast<double>(Gfx2D::g_thousand));
    for (GameObjects::CObjectModule* trick = objects->FindFirstModuleOfKind(0x270F56E1); trick; trick = trick->mp_nextOfType)
        static_cast<GameObjects::CMoveExecutor*>(trick)->HoldOffButton(PadInput::ButtonIndex(button), until);
    return true;
}

// 0x005A2050: StartKeyboardHandler 0x69E3A1C4=<device>: typed keys reported for it.
bool __cdecl CFunc_StartKeyboardHandler(Scripts::CStruct* params, Scripts::CScript*)
{
    GameModules::FrontEnd* frontEnd = GameModules::FrontEndInstance();
    int device = 0; // (unset in the original when missing - an asserted parameter)
    params->FetchIntValue(0x69E3A1C4u, &device, true);
    frontEnd->RegisterKeyListener(device);
    return true;
}

// 0x005A2080: StopKeyboardHandler.
bool __cdecl CFunc_StopKeyboardHandler(Scripts::CStruct*, Scripts::CScript*)
{
    GameModules::FrontEndInstance()->UnregisterKeyListener();
    return true;
}

// 0x005A2100: SpawnSecondControllerCheck: unless it's on, the check's task (TaskList::Task, SecondControllerCheckTask,
// priority 0) made and added to the main loop's logic tasks. (The original gives the task a pointer to one of its
// own locals as its data - the task never reads it; null here.)
bool __cdecl CFunc_SpawnSecondControllerCheck(Scripts::CStruct*, Scripts::CScript*)
{
    auto* mainLoop = AssetStore::g_mainLoopManager;
    if (SecondControllerCheck())
        return true;
    auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
    if (task) {
        reinterpret_cast<TaskList::SchedulerTask*>(task)->MakeNew(0);
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable); // TaskList::Task vtable
        task[0x18 / 4] = reinterpret_cast<uint32_t>(&SecondControllerCheckTask);
        task[0x1C / 4] = 0;
    }
    SecondControllerCheck() = task;
    mainLoop->m_logicTasks.QueueJob(reinterpret_cast<TaskList::SchedulerTask*>(task));
    return true;
}

namespace {

constexpr uint32_t kPos = 0x7F261953;      // "pos"
constexpr uint32_t kPosition = 0xB9D31B0A; // "position"

// The node classes MoveNode knows.
constexpr uint32_t kClassClimbingNode = 0x30C19600; // "ClimbingNode"
constexpr uint32_t kClassRailNode = 0x8E6B02AD;     // "RailNode"

bool FetchPlace(Scripts::CStruct* structure, Scripts::VectorValue* out)
{
    return structure->FetchVec(kPosition, out, 0) || structure->FetchVec(kPos, out, 0);
}

// The getter folded at 0x00576E60 (the word at +0x54: the viewer's object, a profile's face texture...).
void* Word54(void* object) { return reinterpret_cast<GameModules::Viewer*>(object)->GetViewerEntity(); }

// The game's C runtime (library code, called through the game).
void* GameFopen(const char* name, const char* mode) { return Crt::Fopen(name, mode); }
int GameFseek(void* file, long offset, int origin) { return Crt::Fseek(static_cast<FILE*>(file), offset, origin); }
long GameFtell(void* file) { return Crt::Ftell(static_cast<FILE*>(file)); }
void* GameMalloc(size_t size) { return Crt::AllocBlock(size); }
size_t GameFread(void* buffer, size_t size, size_t count, void* file)
{
    return Crt::Fread(buffer, size, count, static_cast<FILE*>(file));
}
int GameFclose(void* file) { return Crt::Fclose(static_cast<FILE*>(file)); }
void GameFree(void* memory) { Crt::Dealloc(memory); }
double GameAtof(const char* text) { return Crt::Atof(text); }

// ShowTracking's debug line drawing - compiled out of this build (an empty call, 0x005480A0).
inline void DrawDebugLine(const Maths::Vec4* /*from*/, const Maths::Vec4* /*to*/, uint32_t /*colour1*/,
                          uint32_t /*colour2*/, int /*unused*/)
{
}
// ...and the empty call at its start (no arguments).
inline void CompiledOutDebugCall() {}

} // namespace

// 0x005A21C0: MoveNode name=<node> position= / pos=<vector>: the node's pos (the node needs a position already) and
// its object's position (if there is one) set. Rail and climbing nodes are moved in the rail manager too; the other
// known classes (Pedestrian, ProximNode, Vehicle, LevelObject, LevelGeometry, GameObject, 0x061A741E) need nothing
// more; any other class: false. Also false (with a message) without the name or a position.
bool __cdecl CFunc_MoveNode(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name;
    if (!params->ReadHashValue(0xA1DC81F9u, &name, 0)) { // "name"
        DebugOut::Printf("Missing Name\n"); // "Missing Name\n"
        return false;
    }
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, false);
    if (!node)
        return false;
    Scripts::VectorValue position;
    if (!FetchPlace(params, &position)) {
        DebugOut::Printf("Missing position parameter in MoveNode\n"); // "Missing position parameter in MoveNode\n"
        return false;
    }
    Scripts::VectorValue old;
    if (!FetchPlace(node, &old)) {
        DebugOut::Printf("node is missing position\n"); // "node is missing position\n"
        return false;
    }
    node->PutVec3(kPos, position.x, position.y, position.z);
    if (GameObjects::CEntity* object = Objects()->LookUpEntity(name))
        std::memcpy(&static_cast<GameObjects::CGameEntity*>(object)->m_atPoint, &position, sizeof(position));
    uint32_t nodeClass = 0;
    node->ReadHashValue(0x12B4E660u, &nodeClass, 0); // "class"
    switch (nodeClass) {
    case kClassClimbingNode:
    case kClassRailNode:
        (*reinterpret_cast<GameObjects::CGrindRailHub**>(Skate() + 0x24))
            ->MoveNode(name, reinterpret_cast<const Maths::Vec4*>(&position));
        return true;
    case 0xA0DFAC98: // Pedestrian
    case 0x061A741E: // (name unknown)
    case 0x08470F2E: // ProximNode
    case 0xE47F1B79: // Vehicle
    case 0xB7B3BD86: // LevelObject
    case 0xBF4D3536: // LevelGeometry
    case 0xEF59C100: // GameObject
        return true;
    default:
        return false;
    }
}

// 0x005A2370: SetActiveCamera id=<object> [0x9FD29151=viewport (0)] [0x10C1C887]: MakeCameraActive (taking the
// current view with the flag); its answer.
bool __cdecl CFunc_SetActiveCamera(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t id = 0;
    params->ReadHashValue(0x40C698AFu, &id, 0); // "id"
    int viewport = 0;
    params->FetchIntValue(0x9FD29151u, &viewport, 0);
    bool takeCurrentView = params->HasBareFlag(0x10C1C887u);
    return MakeCameraActive(id, viewport, takeCurrentView);
}

// 0x005A23E0: ShowTracking "<file>" [0xB426BF5A] [0x14240297] [0x969D3AF6] [0xB0FE7369] (none: all four): the
// tracking log (its drive name stripped) read in the debug heap; each "\nTracking<type>" line's six numbers make a
// line from one point to another - types 1 to 4 counted, each shown (compiled out) if its flag is on, the line
// lengthened 10 / 1 / 2 / 4 times for 1, 3, 4; other types shown always. The counts are logged. False if the file
// isn't there.
bool __cdecl CFunc_ShowTracking(Scripts::CStruct* params, Scripts::CScript*)
{
    CompiledOutDebugCall();
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(reinterpret_cast<MemSys::MemSource*>(memory->m_debugHeap));
    bool show1 = params->HasBareFlag(0xB426BF5Au);
    bool show2 = params->HasBareFlag(0x14240297u);
    bool show3 = params->HasBareFlag(0x969D3AF6u);
    bool show4 = params->HasBareFlag(0xB0FE7369u);
    if (!show1 && !show2 && !show3 && !show4)
        show1 = show2 = show3 = show4 = true;
    const char* file = ""; // ""
    params->FetchText(0u, &file, 0);
    if (file[1] == ':') {
        DebugOut::Printf("WARNING: stripped drive name off file\n"); // "WARNING: stripped drive name off file\n"
        file += 2;
    }
    int size = 0;
    if (!FileLoad::IsPresent(file)) {
        DebugOut::Printf("Can't find file: %s\n", file); // "Can't find file: %s\n"
        return false; // (as in the original, the debug heap stays pushed)
    }
    auto* data = static_cast<uint8_t*>(Packs::LoadFileInto(file, &size, nullptr));
    DebugOut::Printf("Loaded %s, size = %d, at %p\n", file, size, data); // "Loaded %s, size = %d, at %p\n"
    MemSys::Hub::Pull()->LeaveHeapScope();
    int count1 = 0;
    int count2 = 0;
    int count3 = 0;
    int count4 = 0;
    for (const uint8_t* at = data; at < data + size; ++at) {
        static const char kTracking[] = "\nTracking";
        int matched = 0; // (compared a byte at a time: never past the first difference)
        while (matched < 9 && at[matched] == static_cast<uint8_t>(kTracking[matched]))
            ++matched;
        if (matched < 9)
            continue;
        int type = at[9] - '0';
        at += 11;
        float values[6];
        for (float& value : values) {
            value = static_cast<float>(GameAtof(reinterpret_cast<const char*>(at)));
            while (*at == '-' || *at == '.' || (*at >= '0' && *at <= '9'))
                ++at;
            ++at;
        }
        Maths::Vec4 from = {values[0], values[1], values[2], 1.0f};
        Maths::Vec4 to = {values[3], values[4], values[5], 1.0f};
        --at;
        uint32_t colour;
        switch (type) {
        case 1:
            ++count1;
            if (!show1)
                continue;
            colour = 0xFFFFFF;
            from -= (to - from) * 10.0f;
            break;
        case 2:
            ++count2;
            if (!show2)
                continue;
            colour = 0xFF;
            break;
        case 3:
            ++count3;
            if (!show3)
                continue;
            colour = 0xFFFF00;
            from -= (to - from) * 2.0f;
            break;
        case 4:
            ++count4;
            if (!show4)
                continue;
            colour = 0x8000;
            from -= (to - from) * 4.0f;
            break;
        default:
            colour = 0xFFFF;
            break;
        }
        DrawDebugLine(&from, &to, colour, colour, 0);
    }
    DebugOut::Printf("Bonks = %d,  Grinds = %d, verts= %d, lines = %d\n", count1, count2, count3, count4); // "Bonks = %d, ..."
    MemSys::FreeMalloc(data);
    return true;
}

// 0x005A2790: ChooseFaceForFacemap string="<file>": the first profile's face texture (+0x54) loaded from
// faces\<file> (read with the C runtime; the script 0x808008EA then runs). False if the file can't be opened.
bool __cdecl CFunc_ChooseFaceForFacemap(Scripts::CStruct* params, Scripts::CScript*)
{
    void* face = Word54(FetchAccount(0));
    const char* name = nullptr; // (unset in the original when missing - an asserted parameter)
    params->FetchText("string", &name, 1); // "string"
    char path[0x80];
#pragma warning(suppress : 4996) // (unbounded: as in the original)
    std::sprintf(path, "faces\\%s", name); // "faces\%s"
    void* file = GameFopen(path, "rb"); // "rb"
    if (!file)
        return false;
    GameFseek(file, 0, SEEK_END);
    long size = GameFtell(file);
    GameFseek(file, 0, SEEK_SET);
    void* data = GameMalloc(static_cast<size_t>(size));
    GameFread(data, 1, static_cast<size_t>(size), file);
    GameFclose(file);
    static_cast<Gfx2D::CFaceSkin*>(face)->SetImage(static_cast<const uint8_t*>(data), 0);
    GameFree(data);
    static_cast<Gfx2D::CFaceSkin*>(face)->MarkUsable(true);
    Scripts::LaunchScript(0x808008EA, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005A29A0: SkaterIsCustom 0x67E6859A=<n>: whether the skate module's profile n (0x00564FC0) has a face texture.
bool __cdecl CFunc_SkaterIsCustom(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->FetchIntValue(0x67E6859Au, &index, 1);
    void* profile = reinterpret_cast<GameModules::Skate*>(Skate())->FetchAccount(index);
    return Word54(profile) != nullptr;
}

// 0x005A29E0: SetSceneColor <...>: the time of day manager's.
bool __cdecl CFunc_SetSceneColor(Scripts::CStruct* params, Scripts::CScript*)
{
    g_todManager->SetSceneColor(params);
    return true;
}

namespace {

constexpr uint32_t kMenuText = 0xC4745838;    // (names unknown) a menu item's text,
constexpr uint32_t kMenuId = 0x40C698AF;      // "id"
constexpr uint32_t kMenuParams = 0xB8437568;  // its pad_choose parameters
constexpr uint32_t kMenuScript = 0x2B0CFCA1;  // its pad_choose script
constexpr uint32_t kFirstRestartId = 123456;  // the restart items' ids, from here

// A new, empty script structure (from the structures' pool) and its deletion.
Scripts::CStruct* NewStructure()
{
    auto* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// A restart menu item's fixed parts.
void AddRestartItemFlags(Scripts::CStruct* item)
{
    item->PutNameKey(0x9F67B2C8, 0x9F67B2C8); // (flags, names unknown)
    item->PutNameKey(0x2A434D05, 0x2A434D05);
    item->PutNameKey(kMenuScript, 0x506562D9);
}

} // namespace

// 0x005A4260: AddRestartsToMenu 0x1B9E8272=<scale>: an item (script 0xC16BF7BD) per restart of the level (its
// RestartName, else "Unnamed restart"; ids from 123456; the restart's node in its parameters), or "No restarts in
// level".
bool __cdecl CFunc_AddRestartsToMenu(Scripts::CStruct* params, Scripts::CScript*)
{
    int count = 0;
    uint32_t restart = 0;
    while ((restart = GameObjects::GetSpawnNode(0, 0, restart)) != 0) {
        Scripts::CStruct* node = LevelNodes::FetchVertex(restart, true);
        const char* name;
        if (!node->FetchText("RestartName", &name, 0)) // "RestartName"
            name = "Unnamed restart";                       // "Unnamed restart"
        Scripts::CStruct* item = NewStructure();
        item->AppendWord(kMenuText, name);
        item->PutNameKey(kMenuId, kFirstRestartId + count);
        AddRestartItemFlags(item);
        Scripts::CStruct* choose = NewStructure();
        choose->PutNameKey(0x66780C6F, restart);
        item->PutSubStruct(kMenuParams, choose);
        float scale = 0.0f; // (unset in the original when missing - an asserted parameter)
        params->FetchFloat(0x1B9E8272u, &scale, true);
        item->PushFloat(0x13B9DA7B, scale); // "scale"
        Scripts::LaunchScript(0xC16BF7BD, item, nullptr, false, nullptr);
        DeleteStructure(item);
        DeleteStructure(choose);
        ++count;
    }
    if (!count) {
        Scripts::CStruct* item = NewStructure();
        item->AppendWord(kMenuText, "No restarts in level"); // "No restarts in level"
        item->PutNameKey(kMenuId, kFirstRestartId);
        AddRestartItemFlags(item);
        Scripts::LaunchScript(0xC16BF7BD, item, nullptr, false, nullptr);
        DeleteStructure(item);
    }
    return true;
}

// 0x005A4560: AddWarpPointsToMenu 0xE2C2FA03=[nodes]: an item (script 0xBEED50B6) per node (its 0xFCD206E1 name,
// else "Unnamed restart"; the node in its parameters). The parameters' own structure isn't freed (a leak, kept). The
// array isn't checked for null.
bool __cdecl CFunc_AddWarpPointsToMenu(Scripts::CStruct* params, Scripts::CScript*)
{
    Scripts::CArray* nodes = nullptr;
    params->FetchList(0xE2C2FA03u, &nodes, 0);
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        Scripts::CStruct* item = NewStructure();
        Scripts::CStruct* node = LevelNodes::FetchVertex(nodes->FetchPart(i), true);
        const char* name;
        if (!node->FetchText(0xFCD206E1u, &name, 0))
            name = "Unnamed restart"; // "Unnamed restart"
        item->AppendWord(kMenuText, name);
        item->PutNameKey(kMenuScript, 0x54523C62);
        Scripts::CStruct* choose = NewStructure();
        choose->PutNameKey(0x9F92BA78, nodes->FetchPart(i));
        item->PutSubStruct(kMenuParams, choose);
        Scripts::LaunchScript(0xBEED50B6, item, nullptr, false, nullptr);
        DeleteStructure(item);
    }
    return true;
}

// 0x005A4730: AddFacesToMenu: an add_item_to_face_menu (name= without its extension, file=) for each of the first 11
// Faces\*.img files with names up to 90 characters. False if there are none.
bool __cdecl CFunc_AddFacesToMenu(Scripts::CStruct*, Scripts::CScript*)
{
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA("Faces\\*.img", &found); // "Faces\*.img"
    if (search == INVALID_HANDLE_VALUE)
        return false;
    int count = 0;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && std::strlen(found.cFileName) <= 90) {
            char file[0x110];
            char name[0xC8];
            std::memcpy(file, found.cFileName, std::strlen(found.cFileName) + 1);
            std::memcpy(name, found.cFileName, std::strlen(found.cFileName) + 1);
            if (std::strlen(name) > 4)
                name[std::strlen(name) - 4] = '\0';
            Scripts::CStruct* item = NewStructure();
            item->AppendWord("name", name); // "name"
            item->AppendWord("file", file); // "file"
            Scripts::LaunchScript("add_item_to_face_menu", item, nullptr, false); // "add_item_to_face_menu"
            ++count;
            DeleteStructure(item);
        }
    } while (count < 11 && FindNextFileA(search, &found));
    FindClose(search);
    return true;
}

} // namespace ScriptCommands
