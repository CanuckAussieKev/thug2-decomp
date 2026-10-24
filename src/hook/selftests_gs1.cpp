// Self-tests for the skate game's script commands 0x0059F610-0x005A5360 (game_cfuncs.h): each original against
// ours on identical stand-in state; every game function they call is stood in for by recording code (over the
// game's function and our version of it alike), so they're compared call by call.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/file/async.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/node_manager.h"
#include "game/script/preferences.h"
#include "game/script/skate_cfuncs.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/sys/viewer.h"
#include "game/sys/viewer_object.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using CFuncTests::Params;
using Script::CStruct;
using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

// The stand-in objects (one set, used by both runs; Rel makes their addresses position-independent).
struct Fakes {
    uint8_t script[0x100];
    uint8_t results[0x10];
    uint8_t viewer[0x60];
    uint8_t object[0x200];
    uint8_t component[0x40];
    uint8_t camera[0x200];
    uint8_t task[0x20];
    uint8_t devices[2][0xF0];
    uint8_t frontEnd[0x170];
    uint8_t handlers[2][0x2C];
    uint8_t controlData[2][0x40];
    uint8_t profile[0x64];
    uint8_t skaters[8][0x10];
    uint32_t vtable[4];
};
Fakes* g_fakes;

uint32_t Rel(uint32_t value)
{
    auto base = reinterpret_cast<uint32_t>(g_fakes);
    if (g_fakes && value >= base && value < base + sizeof(Fakes))
        return 0x5F000000u + (value - base);
    return value;
}

// What the stand-ins return.
bool g_hasViewer, g_hasViewerObject, g_hasComponent, g_asyncBusy, g_hasObject;
uint32_t g_skaterMask;
Params* g_preference;

void StandInCalls(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00576EA0, AddressOf(&Mdl::GetViewer)}, 0, [](Regs&) {
        Record({0x6EA0});
        return g_hasViewer ? reinterpret_cast<uint32_t>(g_fakes->viewer) : 0u;
    });
    s.At({0x00576E60, AddressOf(&Mdl::Viewer::GetViewerObject)}, 0, [](Regs& r) {
        Record({0x6E60, Rel(r.ecx)});
        return g_hasViewerObject ? reinterpret_cast<uint32_t>(g_fakes->object) : 0u;
    });
    s.At({0x005897D0, AddressOf(&Obj::CViewerObject::PlayAnimation)}, 4, [](Regs& r) {
        Record({0x97D0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00589FD0, AddressOf(&Obj::CViewerObject::ReloadAnimation)}, 8, [](Regs& r) {
        Record({0x9FD0, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        return g_hasComponent ? reinterpret_cast<uint32_t>(g_fakes->component) : 0u;
    });
    s.At({0x00420090, AddressOf(&Obj::CModelComponent::SetColorValue)}, 8, [](Regs& r) {
        Record({0x0090, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0048C8F0}, 0, [](Regs&) { Record({0xC8F0}); return 0u; });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x005B6580, AddressOf(&File::AsyncBusy)}, 0, [](Regs&) { Record({0x6580}); return g_asyncBusy ? 1u : 0u; });
    s.At({0x005B6A50, AddressOf(static_cast<void (*)(bool)>(&File::AsyncWaitForIO))}, 0, [](Regs& r) {
        Record({0x6A50, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x005BE4F0, AddressOf(&Sio::Manager::GetDeviceByIndex)}, 4, [](Regs& r) {
        Record({0xE4F0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_fakes->devices[r.args[0] & 1]);
    });
    using RunFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        return g_hasObject ? reinterpret_cast<uint32_t>(g_fakes->object) : 0u;
    });
    s.At({0x004A09C0}, 0, [](Regs& r) { Record({0x09C0, r.args[0]}); return reinterpret_cast<uint32_t>(g_fakes->camera); });
    s.At({0x004A0970}, 0, [](Regs& r) { Record({0x0970, r.args[0], r.args[1]}); return 0u; });
    s.At({0x00564F80}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_fakes->profile);
    });
    s.At({0x005861C0, AddressOf(&Obj::CSkaterProfile::GetStringF36C1878)}, 0, [](Regs& r) {
        Record({0x61C0, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>("custom.skt");
    });
    s.At({0x005861F0, AddressOf(&Obj::CSkaterProfile::SetStringF36C1878)}, 4, [](Regs& r) {
        Record({0x61F0, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    using AddStringFn = void (CStruct::*)(uint32_t, const char*);
    using AddNamedStringFn = void (CStruct::*)(const char*, const char*);
    using AddFloatFn = void (CStruct::*)(uint32_t, float);
    using AddChecksumFn = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x004779D0, AddressOf(static_cast<AddStringFn>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00477A90, AddressOf(static_cast<AddNamedStringFn>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x7A90, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])),
                Hash(reinterpret_cast<const char*>(r.args[1])), Rel(r.args[1])});
        return 0u;
    });
    s.At({0x00477C60, AddressOf(static_cast<AddFloatFn>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046B940, AddressOf(&Obj::CControlPad::Init)}, 0, [](Regs& r) {
        Record({0xB940});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x238);
        return r.ecx;
    });
    s.At({0x0046BDA0, AddressOf(&Obj::CControlPad::Update)}, 4, [](Regs& r) {
        Record({0xBDA0, Rel(r.args[0])});
        auto* sticks = reinterpret_cast<float*>(r.ecx + 0x210);
        float base = static_cast<float>(*reinterpret_cast<uint8_t*>(r.args[0]));
        for (int i = 0; i < 4; ++i)
            sticks[i] = base + 0.25f * static_cast<float>(i + 1) * (i & 1 ? -1.0f : 1.0f);
        return 0u;
    });
    s.At({0x0059EE80, AddressOf(&CFuncs::ControllerPressed)}, 0, [](Regs& r) {
        Record({0xEE80, Rel(r.args[0]), r.args[1]});
        return (Rel(r.args[0]) ^ r.args[1]) & 0x100 ? 1u : 0u;
    });
    s.At({0x00460930, AddressOf(&Obj::CObject::DestroyIfUnlocked)}, 0, [](Regs& r) {
        Record({0x0930, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00408440, AddressOf(&Ass::CAssMan::LoadAsset)}, 0x24, [](Regs& r) {
        Record({0x8440, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3],
                r.args[4] & 0xFF, r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[7], r.args[8]});
        return 0u;
    });
    s.At({0x0045D670, AddressOf(&SkateScript::CNodeManager::RemoveNodeArrayOfFile)}, 4, [](Regs& r) {
        Record({0xD670, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 1u;
    });
    s.At({0x00565680}, 0, [](Regs& r) { Record({0x5680, Rel(r.args[0]), r.args[1]}); return 0x00FE0000u; });
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Record({0xC4A0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_preference->Struct());
    });
    s.At({0x00535BC0, AddressOf(&GameNet::Manager::ToggleNetMetrics)}, 0, [](Regs& r) { Record({0x5BC0, r.ecx}); return 0u; });
    s.At({0x00576E70, AddressOf(&Mdl::SetViewMode)}, 0, [](Regs& r) { Record({0x6E70, r.args[0]}); return 0u; });
    s.At({0x005665C0}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return (g_skaterMask >> r.args[0]) & 1 ? reinterpret_cast<uint32_t>(g_fakes->skaters[r.args[0] & 7]) : 0u;
    });
    s.At({0x005808A0, AddressOf(&Obj::CSkater::SetPauseState)}, 4, [](Regs& r) {
        Record({0x08A0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00576D40, AddressOf(&Mdl::Viewer::RemoveViewerObject)}, 0, [](Regs& r) { Record({0x6D40, Rel(r.ecx)}); return 0u; });
}

// The globals the commands read and write.
std::vector<Saved> SavedGlobals()
{
    return {{0x007CEBC4, 0x4C, {}}, {0x007D6A2C, 1, {}},  {0x007CC260, 1, {}},  {0x007CCA80, 0x10, {}},
            {0x007CE464, 4, {}},    {0x007CE478, 4, {}},  {0x006F0FB8, 4, {}},  {0x006918DC, 4, {}},
            {0x006F0D84, 4, {}},    {0x007CCE14, 4, {}},  {0x007D1E20, 4, {}},  {0x007CE6D8, 0x40, {}},
            {0x006F3624, 4, {}}};
}

// The vtable stand-ins (slot 1: the camera component's update; slot 2: the task's deleting destructor).
void* g_slot1;
void* g_slot2;

void SetUpFakes()
{
    std::memset(g_fakes, 0, sizeof(Fakes));
    g_fakes->vtable[1] = reinterpret_cast<uint32_t>(g_slot1);
    g_fakes->vtable[2] = reinterpret_cast<uint32_t>(g_slot2);
    *reinterpret_cast<uint32_t*>(g_fakes->component) = reinterpret_cast<uint32_t>(g_fakes->vtable);
    *reinterpret_cast<uint32_t*>(g_fakes->task) = reinterpret_cast<uint32_t>(g_fakes->vtable);
    for (size_t i = 0; i < sizeof(g_fakes->camera); ++i)
        g_fakes->camera[i] = static_cast<uint8_t>(i * 7 + 3);
    for (size_t i = 0; i < sizeof(g_fakes->object); ++i)
        g_fakes->object[i] = static_cast<uint8_t>(i * 5 + 1);
    reinterpret_cast<uint32_t*>(g_fakes->object)[0x14 / 4] = 0x1D0B;          // the object's id
    reinterpret_cast<uint32_t*>(g_fakes->component)[0x18 / 4] = 0x00CA3E7A;   // the component's camera
    reinterpret_cast<uint32_t*>(g_fakes->script)[0x14 / 4] = reinterpret_cast<uint32_t>(g_fakes->results);
    auto* frontEnd = reinterpret_cast<uint32_t*>(g_fakes->frontEnd);
    for (int i = 0; i < 2; ++i) {
        frontEnd[0x20 / 4 + i] = reinterpret_cast<uint32_t>(g_fakes->handlers[i]);
        auto* handler = reinterpret_cast<uint32_t*>(g_fakes->handlers[i]);
        handler[0x18 / 4] = reinterpret_cast<uint32_t>(g_fakes->controlData[i]);
        handler[0x1C / 4] = reinterpret_cast<uint32_t>(g_fakes->devices[i]);
        g_fakes->controlData[i][0] = static_cast<uint8_t>(0x40 + 0x30 * i);
    }
    GameVariable<void*>(0x007CE464) = g_fakes->frontEnd;
    GameVariable<uint32_t>(0x007CE478) = 0x00A7E000; // the skate module
    GameVariable<uint32_t>(0x006F0FB8) = 0x00B0B000; // the object manager
    GameVariable<uint32_t>(0x006918DC) = 0x00A55000; // the asset manager
    GameVariable<uint32_t>(0x006F0D84) = 0x00DE0000; // the node manager
    GameVariable<uint32_t>(0x007CCE14) = 0x00CE7000; // the network manager
    GameVariable<uint32_t>(0x007D1E20) = 0x00DE7000; // the device manager
}


void TestGameCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    g_slot1 = standIns->Thunk(0, [](Regs& r) { Record({0x5107, Rel(r.ecx)}); return 0u; });
    g_slot2 = standIns->Thunk(4, [](Regs& r) { Record({0x5207, Rel(r.ecx), r.args[0]}); return 0u; });
    static Params preference;
    preference = Params();
    preference.String(0x96875C0F, "hunter2!").Checksum(0x21902065, 0xC4C4);
    g_preference = &preference;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            std::memset(g_fakes->script + 0x18, 0, sizeof(g_fakes->script) - 0x18);
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;

        // the viewer: no viewer, no object, everything; ReloadViewerAnim missing either parameter
        for (int state = 0; state < 3; ++state) {
            g_hasViewer = state > 0;
            g_hasViewerObject = state > 1;
            g_hasComponent = true;
            Params a; a.Checksum(0, 0xA417);
            call(0x0059F610, CFuncs::CFunc_SetViewerAnim, a);
            static Script::CPair pair;
            pair.x = 3.5f;
            pair.y = -10.0f;
            Params b; b.Add(Script::ESYMBOLTYPE_PAIR, 0, reinterpret_cast<uint32_t>(&pair));
            call(0x0059F650, CFuncs::CFunc_SetViewerLODDist, b);
            call(0x0059F650, CFuncs::CFunc_SetViewerLODDist, none);
            Params c; c.Checksum(0, 0xA418).String(0, "anims\\walk.ska");
            call(0x0059F6C0, CFuncs::CFunc_ReloadViewerAnim, c);
            Params d; d.Checksum(0, 0xA419);
            call(0x0059F6C0, CFuncs::CFunc_ReloadViewerAnim, d);
            Params e; e.String(0, "anims\\run.ska");
            call(0x0059F6C0, CFuncs::CFunc_ReloadViewerAnim, e);
        }
        call(0x0059F730, CFuncs::CFunc_CheckForHoles, none);
        GameVariable<void*>(0x007CEBC4) = g_fakes->task;
        call(0x0059F740, CFuncs::CFunc_StopSecondControllerCheck, none);
        Record({GameVariable<uint32_t>(0x007CEBC4)});
        call(0x0059F740, CFuncs::CFunc_StopSecondControllerCheck, none);
        call(0x0059F760, CFuncs::CFunc_QuitGame, none);
        Record({GameVariable<uint8_t>(0x007D6A2C)});

        // the network flags: each combination of host / joining / trying / succeeded
        for (uint32_t bits = 0; bits < 16; ++bits) {
            GameVariable<uint8_t>(0x007CC260) = static_cast<uint8_t>(bits & 1);
            GameVariable<uint8_t>(0x007CCA80) = static_cast<uint8_t>((bits >> 1) & 1);
            GameVariable<uint8_t>(0x007CCA81) = static_cast<uint8_t>((bits >> 2) & 1);
            GameVariable<uint8_t>(0x007CCA88) = static_cast<uint8_t>((bits >> 3) & 1);
            call(0x0059F770, CFuncs::CFunc_IsInternetGameHost, none);
            call(0x0059F780, CFuncs::CFunc_IsJoiningInternetGame, none);
            call(0x0059F7A0, CFuncs::CFunc_TryingToJoinGame, none);
            call(0x0059F7B0, CFuncs::CFunc_TryJoinServerIPSuccess, none);
            Record({GameVariable<uint8_t>(0x007CCA88)});
        }
        GameVariable<uint8_t>(0x007CCA82) = 0x55;
        call(0x0059F7E0, CFuncs::CFunc_GSEnableNet, none);
        Record({GameVariable<uint8_t>(0x007CCA82)});
        call(0x0059F7F0, CFuncs::CFunc_GSDisableNet, none);
        Record({GameVariable<uint8_t>(0x007CCA82)});
        for (bool busy : {false, true}) {
            g_asyncBusy = busy;
            call(0x0059F800, CFuncs::CFunc_WaitForAsyncLoadToFinish, none);
        }

        // the second controller check's task: each plugged-in combination
        using TaskFn = void(__cdecl*)(void*);
        for (int plugged = 0; plugged < 4; ++plugged) {
            g_fakes->devices[0][0xE0] = static_cast<uint8_t>(plugged & 1);
            g_fakes->devices[1][0xE0] = static_cast<uint8_t>(plugged >> 1);
            original ? Original<TaskFn>(0x0059F820)(g_fakes->task) : CFuncs::SecondControllerCheckTask(g_fakes->task);
        }

        // MakeCameraActive: no object; no component; the current view taken or not
        using CameraFn = bool(__cdecl*)(uint32_t, int, bool);
        for (int state = 0; state < 4; ++state) {
            g_hasObject = state > 0;
            g_hasComponent = state > 1;
            bool take = state == 3;
            SetUpFakes();
            bool result = original ? Original<CameraFn>(0x0059F890)(0xCA01, state, take)
                                   : CFuncs::MakeCameraActive(0xCA01, state, take);
            Record({0xF890, result ? 1u : 0u, HashBytes(g_fakes->object, sizeof(g_fakes->object))});
        }

        // the custom skater file name
        call(0x0059F930, CFuncs::CFunc_GetCustomSkaterFilename, none);
        call(0x0059F960, CFuncs::CFunc_SetCustomSkaterFilename, none);
        Params name; name.String(0, "my_skater");
        call(0x0059F960, CFuncs::CFunc_SetCustomSkaterFilename, name);

        // ControllerPressed: no button; a controller given (each); either
        Params noButton; noButton.Integer(0, 1);
        call(0x0059F9A0, CFuncs::CFunc_ControllerPressed, noButton);
        for (uint32_t button : {0x7323E97Cu, 0x7323E87Cu, 0x20689278u}) {
            for (int controller = 0; controller < 2; ++controller) {
                Params p; p.Checksum(0, button).Integer(0, controller);
                call(0x0059F9A0, CFuncs::CFunc_ControllerPressed, p);
            }
            Params q; q.Checksum(0, button);
            call(0x0059F9A0, CFuncs::CFunc_ControllerPressed, q);
        }

        // GetAnalogueInfo: unplugged, plugged but not valid, both (controllers 0 and 1)
        for (int state = 0; state < 4; ++state) {
            int controller = state == 3 ? 1 : 0;
            g_fakes->devices[controller][0xE0] = static_cast<uint8_t>(state > 0);
            g_fakes->devices[controller][0x14] = static_cast<uint8_t>(state > 1);
            Params p;
            if (controller)
                p.Integer(0xB30D9965, 1);
            call(0x0059FA40, CFuncs::CFunc_GetAnalogueInfo, p);
        }

        // DestroyObjectWithId: another id, the object's
        using WalkFn = void(__cdecl*)(Obj::CObject*, void*);
        for (uint32_t id : {0x1D0Au, 0x1D0Bu}) {
            auto* object = reinterpret_cast<Obj::CObject*>(g_fakes->object);
            Record({0xFB10, id});
            original ? Original<WalkFn>(0x0059FB10)(object, &id) : CFuncs::DestroyObjectWithId(object, &id);
        }

        // node arrays
        Params file; file.String(0, "levels\\nj\\nj_nodes");
        call(0x0059FB30, CFuncs::CFunc_NodeArrayManLoad, file);
        Record({HashBytes(&GameVariable<uint8_t>(0x007CE6D8), 0x40)});
        call(0x0059FB90, CFuncs::CFunc_NodeArrayManUnload, file);

        // preferences: a field and type; the field's value missing (an empty preference)
        Params pref; pref.Checksum(0, 0xF1E1D).Checksum(0x17DAE143, 0x9F7B7843);
        call(0x0059FBC0, CFuncs::CFunc_GetPreferenceString, pref);
        call(0x0059FC40, CFuncs::CFunc_GetPreferencePassword, pref);
        Record({HashBytes(&GameVariable<uint8_t>(0x007CEBD0), 0x10)});
        call(0x0059FD00, CFuncs::CFunc_GetPreferenceChecksum, pref);
        Params noType; noType.Checksum(0, 0xF1E1E);
        call(0x0059FBC0, CFuncs::CFunc_GetPreferenceString, noType);
        call(0x0059FD00, CFuncs::CFunc_GetPreferenceChecksum, noType);
        Params empty;
        empty.String(0x96875C0F, "");
        g_preference = &empty;
        call(0x0059FC40, CFuncs::CFunc_GetPreferencePassword, noType);
        Record({HashBytes(&GameVariable<uint8_t>(0x007CEBD0), 0x10)});
        g_preference = &preference;

        call(0x0059FD80, CFuncs::CFunc_ToggleNetMetrics, none);

        // SetViewMode: on (the viewer's camera), off with and without the skater camera's object and component;
        // some skaters; no viewer
        for (int state = 0; state < 4; ++state) {
            g_hasObject = state != 2;
            g_hasComponent = state != 3;
            g_hasViewer = state & 1;
            g_skaterMask = 0xA5u >> state;
            Params p; p.Integer(0, state == 0 ? 2 : 0);
            call(0x0059FD90, CFuncs::CFunc_SetViewMode, p);
            Record({GameVariable<uint32_t>(0x007CEBCC)});
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    Check(traces[0].size() > 300, "game script commands: the original reached its calls");
    Compare(traces[0], traces[1], "game script commands (0x0059F610-0x0059FE50)");
}

} // namespace

SELFTEST(Early, TestGameCFuncs, RemoveStandIns);
