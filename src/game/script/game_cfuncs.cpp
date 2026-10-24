// Script commands (CFunctions) of the skate game, 0x0059F610-0x0059FE50: the viewer, controllers, node arrays,
// preferences, some network flags (see game_cfuncs.h).

#include "game/script/game_cfuncs.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/ass/assman.h"
#include "game/ass/asset.h"
#include "game/core/crc.h"
#include "game/file/file.h"
#include "game/file/pip.h"
#include "game/gfx/tod_manager.h"
#include "game/mth/vector.h"
#include "game/obj/rail_manager.h"
#include "game/script/nodes.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/obj/compositemgr.h"
#include "game/obj/input_component.h"
#include "game/sio/inpman.h"
#include "game/skater/trick_component.h"
#include "game/sys/mlp.h"
#include "game/sys/timer.h"
#include "game/file/async.h"
#include "game/front/front_end.h"
#include "game/gfx/camera.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/preferences.h"
#include "game/script/skate_cfuncs.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/sys/viewer.h"
#include "game/sys/viewer_object.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

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

// The skate module (Mdl::Skate, 0x007CE478) and its code we call through the game.
uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }
Obj::CSkaterProfile* GetProfile(int index)
{
    return GameFunction<Obj::CSkaterProfile*(__thiscall*)(uint8_t*, int)>(0x00564F80)(Skate(), index);
}
Obj::CSkater* GetSkater(int index)
{
    return GameFunction<Obj::CSkater*(__thiscall*)(uint8_t*, int)>(0x005665C0)(Skate(), index);
}

// The object manager (0x006F0FB8).
Obj::CBaseManager* Objects() { return GameVariable<Obj::CBaseManager*>(0x006F0FB8); }

// A viewport's camera (0x004A09C0) and making `camera` a viewport's (0x004A0970).
Gfx::Camera* ViewportCamera(int viewport) { return GameFunction<Gfx::Camera*(__cdecl*)(int)>(0x004A09C0)(viewport); }
void SetViewportCamera(int viewport, void* camera)
{
    GameFunction<void(__cdecl*)(int, void*)>(0x004A0970)(viewport, camera);
}

// The preferences of a type (0x00565680: the network's, the taunts', the skate module's; null for others). The
// original passes a second argument (1) that isn't read.
Prefs::Preferences* GetPreferencesOfType(uint32_t type)
{
    return GameFunction<Prefs::Preferences*(__cdecl*)(uint32_t, int)>(0x00565680)(type, 1);
}

// The network game flags (GameNet).
bool& InternetGameHost() { return GameVariable<bool>(0x007CC260); }
bool& JoiningInternetGame() { return GameVariable<bool>(0x007CCA80); }
bool& TryingToJoin() { return GameVariable<bool>(0x007CCA81); }
bool& NetEnabled() { return GameVariable<bool>(0x007CCA82); }
bool& JoinServerIPSucceeded() { return GameVariable<bool>(0x007CCA88); }

// The second controller check's task (0x007CEBC4) and the view mode (0x007CEBCC).
void*& SecondControllerCheck() { return GameVariable<void*>(0x007CEBC4); }
int& ViewMode() { return GameVariable<int>(0x007CEBCC); }

// GetPreferencePassword's row of stars (0x007CEBD0).
char* PasswordStars() { return &GameVariable<char>(0x007CEBD0); }

Obj::CViewerObject* ViewerObject(Mdl::Viewer* viewer) { return static_cast<Obj::CViewerObject*>(viewer->GetViewerObject()); }

} // namespace

// 0x0059F610: SetViewerAnim <anim>: the viewer's object plays the animation (if there's a viewer and an object).
bool __cdecl CFunc_SetViewerAnim(Script::CStruct* params, Script::CScript*)
{
    if (Mdl::Viewer* viewer = Mdl::GetViewer()) {
        uint32_t anim = 0; // (unset in the original when missing - an asserted parameter)
        params->GetChecksum(0u, &anim, 1);
        if (Obj::CViewerObject* object = ViewerObject(viewer))
            object->PlayAnimation(anim);
    }
    return true;
}

// 0x0059F650: SetViewerLODDist (x, y): the viewer's object's model component gets colour value `x` (an index,
// truncated) set to `y`. False without the pair (with a viewer); true otherwise.
bool __cdecl CFunc_SetViewerLODDist(Script::CStruct* params, Script::CScript*)
{
    Mdl::Viewer* viewer = Mdl::GetViewer();
    if (!viewer)
        return true;
    Script::CPair pair;
    pair.Init();
    if (!params->GetPair(0u, &pair, false))
        return false;
    if (Obj::CViewerObject* object = ViewerObject(viewer)) {
        // (the model component isn't checked for null, as in the original)
        auto* model = reinterpret_cast<Obj::CModelComponent*>(object->GetComponent(kModel));
        model->SetColorValue(static_cast<int>(pair.x), pair.y);
    }
    return true;
}

// 0x0059F6C0: ReloadViewerAnim <anim> "<file>": the viewer's object reloads the animation from the file.
bool __cdecl CFunc_ReloadViewerAnim(Script::CStruct* params, Script::CScript*)
{
    Mdl::Viewer* viewer = Mdl::GetViewer();
    if (!viewer)
        return true;
    Obj::CViewerObject* object = ViewerObject(viewer);
    if (!object)
        return true;
    uint32_t anim;
    const char* file;
    if (params->GetChecksum(0u, &anim, 0) && params->GetString(0u, &file, 0))
        object->ReloadAnimation(file, anim);
    return true;
}

// 0x0059F730: CheckForHoles (0x0048C8F0).
bool __cdecl CFunc_CheckForHoles(Script::CStruct*, Script::CScript*)
{
    GameFunction<void(__cdecl*)()>(0x0048C8F0)();
    return true;
}

// 0x0059F740: StopSecondControllerCheck: the check's task (if any) deleted.
bool __cdecl CFunc_StopSecondControllerCheck(Script::CStruct*, Script::CScript*)
{
    if (void* task = SecondControllerCheck()) {
        auto destroy = (*static_cast<void* (__thiscall***)(void*, unsigned int)>(task))[2];
        destroy(task, 1); // the scalar deleting destructor (vtable slot 2)
    }
    SecondControllerCheck() = nullptr;
    return true;
}

// 0x0059F760: QuitGame: the game ends (0x007D6A2C).
bool __cdecl CFunc_QuitGame(Script::CStruct*, Script::CScript*)
{
    GameVariable<bool>(0x007D6A2C) = true;
    return true;
}

// 0x0059F770: IsInternetGameHost.
bool __cdecl CFunc_IsInternetGameHost(Script::CStruct*, Script::CScript*) { return InternetGameHost(); }

// 0x0059F780: IsJoiningInternetGame (logged).
bool __cdecl CFunc_IsJoiningInternetGame(Script::CStruct*, Script::CScript*)
{
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FB04), JoiningInternetGame()); // "ScriptIsJoiningInternetGame %d\n"
    return JoiningInternetGame();
}

// 0x0059F7A0: TryingToJoinGame.
bool __cdecl CFunc_TryingToJoinGame(Script::CStruct*, Script::CScript*) { return TryingToJoin(); }

// 0x0059F7B0: TryJoinServerIPSuccess: whether joining by IP worked (forgotten once no join is being tried; logged).
bool __cdecl CFunc_TryJoinServerIPSuccess(Script::CStruct*, Script::CScript*)
{
    if (!TryingToJoin())
        JoinServerIPSucceeded() = false;
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FB24), JoinServerIPSucceeded()); // "ScriptTryJoinServerIPSuccess %d\n"
    return JoinServerIPSucceeded();
}

// 0x0059F7E0 / 0x0059F7F0: GSEnableNet / GSDisableNet.
bool __cdecl CFunc_GSEnableNet(Script::CStruct*, Script::CScript*)
{
    NetEnabled() = true;
    return true;
}

bool __cdecl CFunc_GSDisableNet(Script::CStruct*, Script::CScript*)
{
    NetEnabled() = false;
    return true;
}

// 0x0059F800: WaitForAsyncLoadToFinish: waits for every asynchronous file read.
bool __cdecl CFunc_WaitForAsyncLoadToFinish(Script::CStruct*, Script::CScript*)
{
    if (File::AsyncBusy())
        File::AsyncWaitForIO(true);
    return true;
}

// 0x0059F820
void __cdecl SecondControllerCheckTask(void*)
{
    Sio::Manager* devices = Sio::ManagerInstance();
    Sio::Device* first = devices->GetDeviceByIndex(0);
    Sio::Device* second = devices->GetDeviceByIndex(1);
    if (second->IsPluggedIn() && first->IsPluggedIn())
        Script::RunScript(kBothControllersIn, nullptr, nullptr, false, nullptr);
    else
        Script::RunScript(kControllerMissing, nullptr, nullptr, false, nullptr);
}

// 0x0059F890
bool __cdecl MakeCameraActive(uint32_t id, int viewport, bool takeCurrentView)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Objects()->GetObjectByID(id));
    if (!object)
        return false;
    Obj::CBaseComponent* camera = object->GetComponent(kCamera);
    if (!camera)
        return true;
    if (takeCurrentView) {
        object->m_matrix = *ViewportCamera(viewport)->GetMatrix();
        object->m_pos = *ViewportCamera(viewport)->GetPos();
        (*reinterpret_cast<void(__thiscall***)(void*)>(camera))[1](camera); // (vtable slot 1)
    }
    SetViewportCamera(viewport, reinterpret_cast<void**>(camera)[0x18 / 4]); // the component's camera (+0x18)
    return true;
}

// 0x0059F930: GetCustomSkaterFilename: the first profile's custom skater file name ("Unimplemented" if none) as
// the script's 0xF36C1878.
bool __cdecl CFunc_GetCustomSkaterFilename(Script::CStruct*, Script::CScript* script)
{
    Obj::CSkaterProfile* profile = GetProfile(0);
    script->m_params->AddString(kCustomSkaterFile, profile->GetStringF36C1878());
    return true;
}

// 0x0059F960: SetCustomSkaterFilename ["<name>" ("Unimplemented")]: the first profile's custom skater file name.
bool __cdecl CFunc_SetCustomSkaterFilename(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = GetProfile(0);
    const char* name = reinterpret_cast<const char*>(0x0064EFD8); // "Unimplemented"
    params->GetString(0u, &name, 0);
    profile->SetStringF36C1878(name);
    return true;
}

// 0x0059F9A0: ControllerPressed <button> [controller]: whether the button is held on that controller (the front
// end's handler) - without a number, on either. False without a button.
bool __cdecl CFunc_ControllerPressed(Script::CStruct* params, Script::CScript*)
{
    Mdl::FrontEnd* frontEnd = Mdl::FrontEndInstance();
    uint32_t button = 0;
    if (!params->GetChecksum(0u, &button, 0))
        return false;
    int controller;
    if (params->GetInteger(0u, &controller, 0))
        return ControllerPressed(frontEnd->m_inputHandlers[controller], button);
    for (int i = 0; i < 2; ++i) {
        if (ControllerPressed(frontEnd->m_inputHandlers[i], button))
            return true;
    }
    return false;
}

// 0x0059FA40: GetAnalogueInfo [controller=n (0)]: the controller's sticks as leftx / lefty / rightx / righty in
// the script's parameters - false (and none) if it isn't plugged in and valid.
bool __cdecl CFunc_GetAnalogueInfo(Script::CStruct* params, Script::CScript* script)
{
    Mdl::FrontEnd* frontEnd = Mdl::FrontEndInstance();
    int controller = 0;
    params->GetInteger(kController, &controller, 0);
    Mdl::FrontEndInputHandler* handler = frontEnd->m_inputHandlers[controller];
    Obj::CControlPad pad;
    pad.Init();
    pad.Update(reinterpret_cast<const uint8_t*>(handler->m_controlData));
    auto* device = reinterpret_cast<Sio::Device*>(handler->m_device);
    if (!device->IsPluggedIn() || !device->m_valid)
        return false;
    script->m_params->AddFloat(kLeftX, pad.m_scaledStick[2]);
    script->m_params->AddFloat(kLeftY, pad.m_scaledStick[3]);
    script->m_params->AddFloat(kRightX, pad.m_scaledStick[0]);
    script->m_params->AddFloat(kRightY, pad.m_scaledStick[1]);
    return true;
}

// 0x0059FB10
void __cdecl DestroyObjectWithId(Obj::CObject* object, void* id)
{
    if (object->m_id == *static_cast<uint32_t*>(id))
        object->DestroyIfUnlocked();
}

// 0x0059FB30: NodeArrayManLoad "<file>": the node array's name noted (0x007CE6D8, copied unchecked) and the file
// loaded as an asset of type 11 (the node array manager's).
bool __cdecl CFunc_NodeArrayManLoad(Script::CStruct* params, Script::CScript*)
{
    const char* file = nullptr;
    params->GetString(0u, &file, 1);
    char* out = &GameVariable<char>(0x007CE6D8);
    for (const char* in = file; (*out++ = *in++) != '\0';) {
    }
    Ass::AssetManager()->LoadAsset(file, 11, 0, 0, false, false, false, nullptr, 0);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FB48), file); // "p_file = %s\n"
    return true;
}

// 0x0059FB90: NodeArrayManUnload "<file>": the node manager drops the file's node array.
bool __cdecl CFunc_NodeArrayManUnload(Script::CStruct* params, Script::CScript*)
{
    const char* file = nullptr;
    params->GetString(0u, &file, 1);
    SkateScript::NodeManagerInstance()->RemoveNodeArrayOfFile(file);
    return true;
}

// 0x0059FBC0: GetPreferenceString <field> pref_type=<type>: the preference's ui_string, as the script's ui_string.
// Without pref_type the original reads the type from the slot it would have been written to - the `script`
// argument's (kept: a type no set has, so a null set; the call on it crashes, as does a missing field's
// preference).
bool __cdecl CFunc_GetPreferenceString(Script::CStruct* params, Script::CScript* script)
{
    Script::CStruct* results = script->m_params;
    uint32_t field = 0;
    params->GetChecksum(0u, &field, 0);
    uint32_t type = reinterpret_cast<uint32_t>(script);
    params->GetChecksum(kPrefType, &type, 0);
    Script::CStruct* preference = GetPreferencesOfType(type)->GetPreference(field);
    const char* text = nullptr; // (unset in the original when missing)
    preference->GetString(kUiString, &text, 0);
    results->AddString(kUiString, text);
    return true;
}

// 0x0059FC40: GetPreferencePassword <field> pref_type=<type>: as many stars as the preference's ui_string has
// characters (in a static buffer, 0x007CEBD0, unchecked), as the script's password_string.
bool __cdecl CFunc_GetPreferencePassword(Script::CStruct* params, Script::CScript* script)
{
    Script::CStruct* results = script->m_params;
    uint32_t field = 0;
    params->GetChecksum(0u, &field, 0);
    uint32_t type = reinterpret_cast<uint32_t>(script); // (the slot the original reads back: the argument's)
    params->GetChecksum(reinterpret_cast<const char*>(0x0064FB68), &type, 0); // "pref_type"
    Script::CStruct* preference = GetPreferencesOfType(type)->GetPreference(field);
    const char* password = nullptr; // (unset in the original when missing)
    preference->GetString(reinterpret_cast<const char*>(0x0064C830), &password, 0); // "ui_string"
    char* stars = PasswordStars();
    size_t length = std::strlen(password);
    for (size_t i = 0; i < length; ++i)
        stars[i] = '*';
    stars[length] = '\0';
    results->AddString(reinterpret_cast<const char*>(0x0064FB58), stars); // "password_string"
    return true;
}

// 0x0059FD00: GetPreferenceChecksum <field> pref_type=<type>: the preference's checksum, as the script's checksum.
bool __cdecl CFunc_GetPreferenceChecksum(Script::CStruct* params, Script::CScript* script)
{
    Script::CStruct* results = script->m_params;
    uint32_t field = 0;
    params->GetChecksum(0u, &field, 0);
    uint32_t type = reinterpret_cast<uint32_t>(script); // (the slot the original reads back: the argument's)
    params->GetChecksum(reinterpret_cast<const char*>(0x0064FB68), &type, 0); // "pref_type"
    Script::CStruct* preference = GetPreferencesOfType(type)->GetPreference(field);
    uint32_t value = 0; // (unset in the original when missing)
    preference->GetChecksum(kChecksum, &value, 0);
    results->AddChecksum(kChecksum, value);
    return true;
}

// 0x0059FD80: ToggleNetMetrics.
bool __cdecl CFunc_ToggleNetMetrics(Script::CStruct*, Script::CScript*)
{
    GameVariable<GameNet::Manager*>(0x007CCE14)->ToggleNetMetrics();
    return true;
}

// 0x0059FD90: SetViewMode <mode>: the viewer's view mode (0 = off); in one, the camera of the object 0xEB17151B,
// else viewport 0 gets the camera of 0x967C138C (if there is one) back. Every skater's pause state is set to the
// mode, and the viewer lets go of its object.
namespace {

// The view mode `mode` (0x007CEBCC and the viewer's): in one, the camera of the object 0xEB17151B; else viewport 0
// gets the camera of 0x967C138C (if there is one) back. Every skater's pause state is set to the mode.
void ApplyViewMode(int mode)
{
    ViewMode() = mode;
    Mdl::SetViewMode(mode);
    if (ViewMode()) {
        MakeCameraActive(kViewerCameraObject, 0, true);
    } else if (Obj::CObject* object = Objects()->GetObjectByID(kSkaterCamObject)) {
        if (Obj::CBaseComponent* camera = static_cast<Obj::CCompositeObject*>(object)->GetComponent(kCamera))
            SetViewportCamera(0, reinterpret_cast<void**>(camera)[0x18 / 4]);
    }
    for (int i = 0; i < 8; ++i) {
        if (Obj::CSkater* skater = GetSkater(i))
            skater->SetPauseState(ViewMode());
    }
}

} // namespace

// 0x0059FD90: SetViewMode <mode>: the view mode (ApplyViewMode), and the viewer lets go of its object.
bool __cdecl CFunc_SetViewMode(Script::CStruct* params, Script::CScript*)
{
    int mode = 0; // (unset in the original when missing - an asserted parameter)
    params->GetInteger(0u, &mode, 1);
    ApplyViewMode(mode);
    if (Mdl::Viewer* viewer = Mdl::GetViewer())
        viewer->RemoveViewerObject();
    return true;
}

// 0x005A1C80: SetViewerModel <...>: view mode 1; the viewer (if any) places its camera and makes a viewer object
// from the parameters.
bool __cdecl CFunc_SetViewerModel(Script::CStruct* params, Script::CScript*)
{
    Mdl::Viewer* viewer = Mdl::GetViewer();
    ApplyViewMode(1);
    if (viewer) {
        viewer->PlaceCamera(params);
        Script::PrintContents(params, 0);
        viewer->CreateViewerObject(params);
    }
    return true;
}

// 0x005A1D40: GetViewerObjectID: the viewer object's id as the script's 0x0830ECAF and 0x52280066 (the object isn't
// checked for null).
bool __cdecl CFunc_GetViewerObjectID(Script::CStruct*, Script::CScript* script)
{
    if (Mdl::Viewer* viewer = Mdl::GetViewer()) {
        Obj::CViewerObject* object = ViewerObject(viewer);
        script->m_params->AddChecksum(0x0830ECAF, object->m_id);
        script->m_params->AddChecksum(0x52280066, object->m_id);
    }
    return true;
}

// 0x005A1D80: ReloadAnim 0x52A864B0=<anim> 0x98549BA4=<base> 0xC3F4169A="<file>": the animation asset (by base +
// anim, or without a base the file name's checksum) unloaded and loaded again from the file (in the debug heap while
// 0x007CEFAC is set). False if it isn't loaded.
bool __cdecl CFunc_ReloadAnim(Script::CStruct* params, Script::CScript*)
{
    uint32_t anim = 0;
    params->GetChecksum(0x52A864B0u, &anim, 0);
    uint32_t base = 0;
    params->GetChecksum(0x98549BA4u, &base, 0);
    const char* file = reinterpret_cast<const char*>(0x006469B4); // ""
    params->GetString(0xC3F4169Au, &file, 0);
    Ass::CAssMan* assets = Ass::AssetManager();
    uint32_t name = Crc::GenerateCRCFromString(file);
    if (base)
        name = base + anim;
    Ass::CAsset* asset = assets->GetAsset(name, 0);
    if (!asset)
        return false;
    assets->UnloadAsset(asset);
    bool debugHeap = GameVariable<bool>(0x007CEFAC);
    if (debugHeap) {
        Mem::Manager* memory = GameVariable<Mem::Manager*>(0x007D1800);
        memory->PushContext(reinterpret_cast<Mem::Allocator*>(memory->m_debugHeap));
    }
    assets->LoadAnim(file, base, anim, false, false);
    if (GameVariable<bool>(0x007CEFAC))
        GameVariable<Mem::Manager*>(0x007D1800)->PopContext();
    return true;
}

// 0x005A1E70: Debounce <button> [time=seconds (1)] [0x1A4E0EF9=n]: every input component locks the button for that
// long (released if n), and every trick component holds it off until the clock (ms) + time * 1000. Nothing without
// a button.
bool __cdecl CFunc_Debounce(Script::CStruct* params, Script::CScript*)
{
    uint32_t button = 0;
    params->GetChecksum(0u, &button, 0);
    if (!button)
        return true;
    float time = 1.0f;
    params->GetFloat(0x906B67BAu, &time, 0); // "time"
    int release = 0;
    params->GetInteger(0x1A4E0EF9u, &release, 0);
    auto* objects = GameVariable<Obj::CCompositeObjectManager*>(0x006F0FB8);
    for (Obj::CBaseComponent* input = objects->GetFirstComponentByType(0x27D7CD28); input; input = input->mp_nextOfType)
        static_cast<Obj::CInputComponent*>(input)->LockButton(button, time, release != 0);
    // x87: the unsigned clock + time * 1000.0 (0x006453C0), stored as a float
    float until = static_cast<float>(static_cast<double>(Tmr::GetTime()) +
                                     static_cast<double>(time) * static_cast<double>(GameVariable<float>(0x006453C0)));
    for (Obj::CBaseComponent* trick = objects->GetFirstComponentByType(0x270F56E1); trick; trick = trick->mp_nextOfType)
        static_cast<Obj::CTrickComponent*>(trick)->HoldOffButton(Inp::ButtonIndex(button), until);
    return true;
}

// 0x005A2050: StartKeyboardHandler 0x69E3A1C4=<device>: typed keys reported for it.
bool __cdecl CFunc_StartKeyboardHandler(Script::CStruct* params, Script::CScript*)
{
    Mdl::FrontEnd* frontEnd = Mdl::FrontEndInstance();
    int device = 0; // (unset in the original when missing - an asserted parameter)
    params->GetInteger(0x69E3A1C4u, &device, true);
    frontEnd->AddKeyboardHandler(device);
    return true;
}

// 0x005A2080: StopKeyboardHandler.
bool __cdecl CFunc_StopKeyboardHandler(Script::CStruct*, Script::CScript*)
{
    Mdl::FrontEndInstance()->RemoveKeyboardHandler();
    return true;
}

// 0x005A2100: SpawnSecondControllerCheck: unless it's on, the check's task (Tsk::Task, SecondControllerCheckTask,
// priority 0) made and added to the main loop's logic tasks. (The original gives the task a pointer to one of its
// own locals as its data - the task never reads it; null here.)
bool __cdecl CFunc_SpawnSecondControllerCheck(Script::CStruct*, Script::CScript*)
{
    auto* mainLoop = GameVariable<Mlp::Manager*>(0x00696D68);
    if (SecondControllerCheck())
        return true;
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (task) {
        reinterpret_cast<Tsk::BaseTask*>(task)->Construct(0);
        task[0] = 0x006476F0; // Tsk::Task vtable
        task[0x18 / 4] = reinterpret_cast<uint32_t>(&SecondControllerCheckTask);
        task[0x1C / 4] = 0;
    }
    SecondControllerCheck() = task;
    mainLoop->m_logicTasks.AddTask(reinterpret_cast<Tsk::BaseTask*>(task));
    return true;
}

namespace {

constexpr uint32_t kPos = 0x7F261953;      // "pos"
constexpr uint32_t kPosition = 0xB9D31B0A; // "position"

// The node classes MoveNode knows.
constexpr uint32_t kClassClimbingNode = 0x30C19600; // "ClimbingNode"
constexpr uint32_t kClassRailNode = 0x8E6B02AD;     // "RailNode"

bool GetPosition(Script::CStruct* structure, Script::VectorValue* out)
{
    return structure->GetVector(kPosition, out, 0) || structure->GetVector(kPos, out, 0);
}

// The getter folded at 0x00576E60 (the word at +0x54: the viewer's object, a profile's face texture...).
void* Word54(void* object) { return reinterpret_cast<Mdl::Viewer*>(object)->GetViewerObject(); }

// The game's C runtime (library code, called through the game).
void* GameFopen(const char* name, const char* mode)
{
    return GameFunction<void*(__cdecl*)(const char*, const char*)>(0x006241E8)(name, mode);
}
int GameFseek(void* file, long offset, int origin)
{
    return GameFunction<int(__cdecl*)(void*, long, int)>(0x006240D2)(file, offset, origin);
}
long GameFtell(void* file) { return GameFunction<long(__cdecl*)(void*)>(0x00625388)(file); }
void* GameMalloc(size_t size) { return GameFunction<void*(__cdecl*)(size_t)>(0x00624031)(size); }
size_t GameFread(void* buffer, size_t size, size_t count, void* file)
{
    return GameFunction<size_t(__cdecl*)(void*, size_t, size_t, void*)>(0x00623F3E)(buffer, size, count, file);
}
int GameFclose(void* file) { return GameFunction<int(__cdecl*)(void*)>(0x00623E04)(file); }
void GameFree(void* memory) { GameFunction<void(__cdecl*)(void*)>(0x0062411B)(memory); }
double GameAtof(const char* text) { return GameFunction<double(__cdecl*)(const char*)>(0x006253C9)(text); }

// ShowTracking's debug line drawing - compiled out of this build (an empty call, 0x005480A0).
inline void DrawDebugLine(const Mth::Vector* /*from*/, const Mth::Vector* /*to*/, uint32_t /*colour1*/,
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
bool __cdecl CFunc_MoveNode(Script::CStruct* params, Script::CScript*)
{
    uint32_t name;
    if (!params->GetChecksum(0xA1DC81F9u, &name, 0)) { // "name"
        Dbg::Printf(reinterpret_cast<const char*>(0x0064FF04)); // "Missing Name\n"
        return false;
    }
    Script::CStruct* node = SkateScript::GetNode(name, false);
    if (!node)
        return false;
    Script::VectorValue position;
    if (!GetPosition(params, &position)) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064FEDC)); // "Missing position parameter in MoveNode\n"
        return false;
    }
    Script::VectorValue old;
    if (!GetPosition(node, &old)) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064FEC0)); // "node is missing position\n"
        return false;
    }
    node->AddVector(kPos, position.x, position.y, position.z);
    if (Obj::CObject* object = Objects()->GetObjectByID(name))
        std::memcpy(&static_cast<Obj::CCompositeObject*>(object)->m_pos, &position, sizeof(position));
    uint32_t nodeClass = 0;
    node->GetChecksum(0x12B4E660u, &nodeClass, 0); // "class"
    switch (nodeClass) {
    case kClassClimbingNode:
    case kClassRailNode:
        (*reinterpret_cast<Obj::CRailManager**>(Skate() + 0x24))
            ->MoveNode(name, reinterpret_cast<const Mth::Vector*>(&position));
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
bool __cdecl CFunc_SetActiveCamera(Script::CStruct* params, Script::CScript*)
{
    uint32_t id = 0;
    params->GetChecksum(0x40C698AFu, &id, 0); // "id"
    int viewport = 0;
    params->GetInteger(0x9FD29151u, &viewport, 0);
    bool takeCurrentView = params->ContainsFlag(0x10C1C887u);
    return MakeCameraActive(id, viewport, takeCurrentView);
}

// 0x005A23E0: ShowTracking "<file>" [0xB426BF5A] [0x14240297] [0x969D3AF6] [0xB0FE7369] (none: all four): the
// tracking log (its drive name stripped) read in the debug heap; each "\nTracking<type>" line's six numbers make a
// line from one point to another - types 1 to 4 counted, each shown (compiled out) if its flag is on, the line
// lengthened 10 / 1 / 2 / 4 times for 1, 3, 4; other types shown always. The counts are logged. False if the file
// isn't there.
bool __cdecl CFunc_ShowTracking(Script::CStruct* params, Script::CScript*)
{
    CompiledOutDebugCall();
    Mem::Manager* memory = GameVariable<Mem::Manager*>(0x007D1800);
    memory->PushContext(reinterpret_cast<Mem::Allocator*>(memory->m_debugHeap));
    bool show1 = params->ContainsFlag(0xB426BF5Au);
    bool show2 = params->ContainsFlag(0x14240297u);
    bool show3 = params->ContainsFlag(0x969D3AF6u);
    bool show4 = params->ContainsFlag(0xB0FE7369u);
    if (!show1 && !show2 && !show3 && !show4)
        show1 = show2 = show3 = show4 = true;
    const char* file = reinterpret_cast<const char*>(0x006469B4); // ""
    params->GetString(0u, &file, 0);
    if (file[1] == ':') {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064FF80)); // "WARNING: stripped drive name off file\n"
        file += 2;
    }
    int size = 0;
    if (!File::Exist(file)) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064FF14), file); // "Can't find file: %s\n"
        return false; // (as in the original, the debug heap stays pushed)
    }
    auto* data = static_cast<uint8_t*>(Pip::LoadFileInto(file, &size, nullptr));
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FF60), file, size, data); // "Loaded %s, size = %d, at %p\n"
    GameVariable<Mem::Manager*>(0x007D1800)->PopContext();
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
        Mth::Vector from = {values[0], values[1], values[2], 1.0f};
        Mth::Vector to = {values[3], values[4], values[5], 1.0f};
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
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FF2C), count1, count2, count3, count4); // "Bonks = %d, ..."
    Mem::FreeMalloc(data);
    return true;
}

// 0x005A2790: ChooseFaceForFacemap string="<file>": the first profile's face texture (+0x54) loaded from
// faces\<file> (read with the C runtime; the script 0x808008EA then runs). False if the file can't be opened.
bool __cdecl CFunc_ChooseFaceForFacemap(Script::CStruct* params, Script::CScript*)
{
    void* face = Word54(GetProfile(0));
    const char* name = nullptr; // (unset in the original when missing - an asserted parameter)
    params->GetString(reinterpret_cast<const char*>(0x0064D2B0), &name, 1); // "string"
    char path[0x80];
#pragma warning(suppress : 4996) // (unbounded: as in the original)
    std::sprintf(path, reinterpret_cast<const char*>(0x0064D2A4), name); // "faces\%s"
    void* file = GameFopen(path, reinterpret_cast<const char*>(0x00647344)); // "rb"
    if (!file)
        return false;
    GameFseek(file, 0, SEEK_END);
    long size = GameFtell(file);
    GameFseek(file, 0, SEEK_SET);
    void* data = GameMalloc(static_cast<size_t>(size));
    GameFread(data, 1, static_cast<size_t>(size), file);
    GameFclose(file);
    GameFunction<void(__thiscall*)(void*, void*, int)>(0x00487420)(face, data, 0);
    GameFree(data);
    GameFunction<void(__thiscall*)(void*, int)>(0x00487520)(face, 1);
    Script::RunScript(0x808008EA, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005A29A0: SkaterIsCustom 0x67E6859A=<n>: whether the skate module's profile n (0x00564FC0) has a face texture.
bool __cdecl CFunc_SkaterIsCustom(Script::CStruct* params, Script::CScript*)
{
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->GetInteger(0x67E6859Au, &index, 1);
    void* profile = GameFunction<void*(__thiscall*)(uint8_t*, int)>(0x00564FC0)(Skate(), index);
    return Word54(profile) != nullptr;
}

// 0x005A29E0: SetSceneColor <...>: the time of day manager's.
bool __cdecl CFunc_SetSceneColor(Script::CStruct* params, Script::CScript*)
{
    GameVariable<Gfx::CTODManager*>(0x007CE490)->SetSceneColor(params);
    return true;
}

namespace {

constexpr uint32_t kMenuText = 0xC4745838;    // (names unknown) a menu item's text,
constexpr uint32_t kMenuId = 0x40C698AF;      // "id"
constexpr uint32_t kMenuParams = 0xB8437568;  // its pad_choose parameters
constexpr uint32_t kMenuScript = 0x2B0CFCA1;  // its pad_choose script
constexpr uint32_t kFirstRestartId = 123456;  // the restart items' ids, from here

// A new, empty script structure (from the structures' pool) and its deletion.
Script::CStruct* NewStructure()
{
    auto* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

// A restart menu item's fixed parts.
void AddRestartItemFlags(Script::CStruct* item)
{
    item->AddChecksum(0x9F67B2C8, 0x9F67B2C8); // (flags, names unknown)
    item->AddChecksum(0x2A434D05, 0x2A434D05);
    item->AddChecksum(kMenuScript, 0x506562D9);
}

} // namespace

// 0x005A4260: AddRestartsToMenu 0x1B9E8272=<scale>: an item (script 0xC16BF7BD) per restart of the level (its
// RestartName, else "Unnamed restart"; ids from 123456; the restart's node in its parameters), or "No restarts in
// level".
bool __cdecl CFunc_AddRestartsToMenu(Script::CStruct* params, Script::CScript*)
{
    int count = 0;
    uint32_t restart = 0;
    while ((restart = Obj::GetRestartNode(0, 0, restart)) != 0) {
        Script::CStruct* node = SkateScript::GetNode(restart, true);
        const char* name;
        if (!node->GetString(reinterpret_cast<const char*>(0x00650230), &name, 0)) // "RestartName"
            name = reinterpret_cast<const char*>(0x00650220);                       // "Unnamed restart"
        Script::CStruct* item = NewStructure();
        item->AddString(kMenuText, name);
        item->AddChecksum(kMenuId, kFirstRestartId + count);
        AddRestartItemFlags(item);
        Script::CStruct* choose = NewStructure();
        choose->AddChecksum(0x66780C6F, restart);
        item->AddStructure(kMenuParams, choose);
        float scale = 0.0f; // (unset in the original when missing - an asserted parameter)
        params->GetFloat(0x1B9E8272u, &scale, true);
        item->AddFloat(0x13B9DA7B, scale); // "scale"
        Script::RunScript(0xC16BF7BD, item, nullptr, false, nullptr);
        DeleteStructure(item);
        DeleteStructure(choose);
        ++count;
    }
    if (!count) {
        Script::CStruct* item = NewStructure();
        item->AddString(kMenuText, reinterpret_cast<const char*>(0x00650208)); // "No restarts in level"
        item->AddChecksum(kMenuId, kFirstRestartId);
        AddRestartItemFlags(item);
        Script::RunScript(0xC16BF7BD, item, nullptr, false, nullptr);
        DeleteStructure(item);
    }
    return true;
}

// 0x005A4560: AddWarpPointsToMenu 0xE2C2FA03=[nodes]: an item (script 0xBEED50B6) per node (its 0xFCD206E1 name,
// else "Unnamed restart"; the node in its parameters). The parameters' own structure isn't freed (a leak, kept). The
// array isn't checked for null.
bool __cdecl CFunc_AddWarpPointsToMenu(Script::CStruct* params, Script::CScript*)
{
    Script::CArray* nodes = nullptr;
    params->GetArray(0xE2C2FA03u, &nodes, 0);
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        Script::CStruct* item = NewStructure();
        Script::CStruct* node = SkateScript::GetNode(nodes->GetElement(i), true);
        const char* name;
        if (!node->GetString(0xFCD206E1u, &name, 0))
            name = reinterpret_cast<const char*>(0x00650220); // "Unnamed restart"
        item->AddString(kMenuText, name);
        item->AddChecksum(kMenuScript, 0x54523C62);
        Script::CStruct* choose = NewStructure();
        choose->AddChecksum(0x9F92BA78, nodes->GetElement(i));
        item->AddStructure(kMenuParams, choose);
        Script::RunScript(0xBEED50B6, item, nullptr, false, nullptr);
        DeleteStructure(item);
    }
    return true;
}

// 0x005A4730: AddFacesToMenu: an add_item_to_face_menu (name= without its extension, file=) for each of the first 11
// Faces\*.img files with names up to 90 characters. False if there are none.
bool __cdecl CFunc_AddFacesToMenu(Script::CStruct*, Script::CScript*)
{
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(reinterpret_cast<const char*>(0x00650254), &found); // "Faces\*.img"
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
            Script::CStruct* item = NewStructure();
            item->AddString(reinterpret_cast<const char*>(0x0064D0C4), name); // "name"
            item->AddString(reinterpret_cast<const char*>(0x0064D2B8), file); // "file"
            Script::RunScript(reinterpret_cast<const char*>(0x0065023C), item, nullptr, false); // "add_item_to_face_menu"
            ++count;
            DeleteStructure(item);
        }
    } while (count < 11 && FindNextFileA(search, &found));
    FindClose(search);
    return true;
}

} // namespace CFuncs

REPLACE(0x0059F610, CFuncs::CFunc_SetViewerAnim);
REPLACE(0x0059F650, CFuncs::CFunc_SetViewerLODDist);
REPLACE(0x0059F6C0, CFuncs::CFunc_ReloadViewerAnim);
REPLACE(0x0059F730, CFuncs::CFunc_CheckForHoles);
REPLACE(0x0059F740, CFuncs::CFunc_StopSecondControllerCheck);
REPLACE(0x0059F760, CFuncs::CFunc_QuitGame);
REPLACE(0x0059F770, CFuncs::CFunc_IsInternetGameHost);
REPLACE(0x0059F780, CFuncs::CFunc_IsJoiningInternetGame);
REPLACE(0x0059F7A0, CFuncs::CFunc_TryingToJoinGame);
REPLACE(0x0059F7B0, CFuncs::CFunc_TryJoinServerIPSuccess);
REPLACE(0x0059F7E0, CFuncs::CFunc_GSEnableNet);
REPLACE(0x0059F7F0, CFuncs::CFunc_GSDisableNet);
REPLACE(0x0059F800, CFuncs::CFunc_WaitForAsyncLoadToFinish);
REPLACE(0x0059F820, CFuncs::SecondControllerCheckTask);
REPLACE(0x0059F890, CFuncs::MakeCameraActive);
REPLACE(0x0059F930, CFuncs::CFunc_GetCustomSkaterFilename);
REPLACE(0x0059F960, CFuncs::CFunc_SetCustomSkaterFilename);
REPLACE(0x0059F9A0, CFuncs::CFunc_ControllerPressed);
REPLACE(0x0059FA40, CFuncs::CFunc_GetAnalogueInfo);
REPLACE(0x0059FB10, CFuncs::DestroyObjectWithId);
REPLACE(0x0059FB30, CFuncs::CFunc_NodeArrayManLoad);
REPLACE(0x0059FB90, CFuncs::CFunc_NodeArrayManUnload);
REPLACE(0x0059FBC0, CFuncs::CFunc_GetPreferenceString);
REPLACE(0x0059FC40, CFuncs::CFunc_GetPreferencePassword);
REPLACE(0x0059FD00, CFuncs::CFunc_GetPreferenceChecksum);
REPLACE(0x0059FD80, CFuncs::CFunc_ToggleNetMetrics);
REPLACE(0x0059FD90, CFuncs::CFunc_SetViewMode);
REPLACE(0x005A1C80, CFuncs::CFunc_SetViewerModel);
REPLACE(0x005A1D40, CFuncs::CFunc_GetViewerObjectID);
REPLACE(0x005A1D80, CFuncs::CFunc_ReloadAnim);
REPLACE(0x005A1E70, CFuncs::CFunc_Debounce);
REPLACE(0x005A2050, CFuncs::CFunc_StartKeyboardHandler);
REPLACE(0x005A2080, CFuncs::CFunc_StopKeyboardHandler);
REPLACE(0x005A2100, CFuncs::CFunc_SpawnSecondControllerCheck);
REPLACE(0x005A21C0, CFuncs::CFunc_MoveNode);
REPLACE(0x005A2370, CFuncs::CFunc_SetActiveCamera);
REPLACE(0x005A23E0, CFuncs::CFunc_ShowTracking);
REPLACE(0x005A2790, CFuncs::CFunc_ChooseFaceForFacemap);
REPLACE(0x005A29A0, CFuncs::CFunc_SkaterIsCustom);
REPLACE(0x005A29E0, CFuncs::CFunc_SetSceneColor);
REPLACE(0x005A4260, CFuncs::CFunc_AddRestartsToMenu);
REPLACE(0x005A4560, CFuncs::CFunc_AddWarpPointsToMenu);
REPLACE(0x005A4730, CFuncs::CFunc_AddFacesToMenu);
