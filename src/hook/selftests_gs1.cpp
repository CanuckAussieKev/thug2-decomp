// Self-tests for the skate game's script commands 0x0059F610-0x005A5360 (game_cfuncs.h): each original against
// ours on identical stand-in state; every game function they call is stood in for by recording code (over the
// game's function and our version of it alike), so they're compared call by call.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/stubs.h"
#include "game/ed/park_editor.h"
#include "game/file/pip.h"
#include "game/front/front_end.h"
#include "game/gfx/tod_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/path_manager.h"
#include "game/script/cfuncs_scene.h"
#include "game/script/node_prefixes.h"
#include "game/sfx/sfx.h"
#include "game/net/net_manager.h"
#include "game/net/player_info.h"
#include "game/script/qb.h"
#include "game/sio/inpman.h"
#include "game/skate/skate_net.h"
#include "game/sys/game_clock.h"
#include "game/sys/mlp.h"
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
    uint8_t component[0x300];
    uint8_t camera[0x200];
    uint8_t task[0x20];
    uint8_t devices[2][0xF0];
    uint8_t frontEnd[0x170];
    uint8_t handlers[2][0x2C];
    uint8_t controlData[2][0x40];
    uint8_t profile[0x64];
    uint8_t skater[0x330];
    uint8_t flagObjects[3][0x4C];
    uint32_t skaterVtable[16];
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
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0x1C30, Hash(format), std::strchr(format, '%') ? r.args[1] : 0u});
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
        Record({0x5790, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4], g_fakes ? g_fakes->script[0xCC] : 0u});
        if (g_fakes && r.args[0] == 0xDCA9A9A0)
            g_fakes->script[0xCC] |= 1; // (Cleanup's second script: something setting the flag meanwhile)
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
        return (g_skaterMask >> r.args[0]) & 1 ? reinterpret_cast<uint32_t>(g_fakes->skater) : 0u;
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

// --- 0x0059FE50-0x005A0880, 0x005A3B00-0x005A3B75 (game_flow_cfuncs.cpp) ------------------------------------

bool g_hasSymbol, g_netHeaps, g_internetHeaps;
const char* g_pres[4];
uint32_t g_prefixCount;
uint32_t g_prefixNodes[3];

void StandInFlowCalls(StandIns& s)
{
    // mode: 1 records ECX (a member function), 2 the first argument, 4 its low byte (a bool)
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    simple({0x0048C1B0, AddressOf(&Stubs::DoNothing)}, 0, 0xC1B0, 0);
    simple({0x005591B0}, 0, 0x91B0, 0, 0x00517F00);
    simple({0x00558250}, 4, 0x8250, 3);
    simple({0x0055BEF0}, 0, 0xBEF0, 1);
    simple({0x00572BD0, AddressOf(&Gfx::CTODManager::ClearDeadObjects)}, 0, 0x2BD0, 1);
    simple({0x0058DB30, AddressOf(&Ed::CParkEditor::Leave)}, 0, 0xDB30, 1);
    simple({0x005666A0}, 0, 0x66A0, 1);
    simple({0x0048E260}, 0, 0xE260, 1);
    simple({0x00492340}, 0, 0x2340, 0);
    simple({0x00480660}, 0, 0x0660, 0, 0x00FA2700);
    simple({0x00480DB0}, 4, 0x0DB0, 3);
    simple({0x00490DE0}, 0, 0x0DE0, 4);
    simple({0x00475700, AddressOf(&Script::DeleteFinishedScripts)}, 0, 0x5700, 0);
    simple({0x0048FB60}, 0, 0xFB60, 0);
    simple({0x0047B1B0, AddressOf(&Sfx::CSfxManager::CleanUp)}, 0, 0xB1B0, 1);
    s.At({0x00407F50, AddressOf(&Ass::CAssMan::UnloadAllAssetsOfType)}, 8, [](Regs& r) {
        Record({0x7F50, r.ecx, r.args[0] & 0xFF, r.args[1]});
        return 0u;
    });
    s.At({0x00407F20, AddressOf(&Ass::CAssMan::UnloadAllAssets)}, 4, [](Regs& r) {
        Record({0x7F20, r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    simple({0x005A59B0}, 0, 0x59B0, 2);
    s.At({0x00445120, AddressOf(&CFuncs::CFunc_UnloadAllLevelGeometry)}, 0, [](Regs& r) {
        Record({0x5120, r.args[0], r.args[1]});
        return 1u;
    });
    simple({0x005BACF0, AddressOf(&Mem::Manager::DeleteExtraSkaterHeaps)}, 0, 0xACF0, 1);
    s.At({0x005B7300, AddressOf(&Pip::GetNextLoadedPre)}, 0, [](Regs& r) {
        Record({0x7300, Hash(reinterpret_cast<const char*>(r.args[0]))});
        for (int i = 0; i < 3; ++i) {
            if (reinterpret_cast<const char*>(r.args[0]) == (i ? g_pres[i - 1] : nullptr))
                return reinterpret_cast<uint32_t>(g_pres[i]);
        }
        return 0u;
    });
    s.At({0x005B7370, AddressOf(&Pip::PreFileIsInUse)}, 0, [](Regs& r) {
        Record({0x7370, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return reinterpret_cast<const char*>(r.args[0])[0] == 'u' && reinterpret_cast<const char*>(r.args[0])[1] == 's'
                   ? 1u
                   : 0u;
    });
    s.At({0x005B7130, AddressOf(&Pip::UnloadPre)}, 0, [](Regs& r) {
        Record({0x7130, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 1u;
    });
    simple({0x004A0760}, 0, 0x0760, 0);
    simple({0x0048FDE0}, 0, 0xFDE0, 0);
    simple({0x0049EFB0}, 0, 0xEFB0, 2);
    simple({0x004694E0, AddressOf(&Obj::CPathManager::Instance)}, 0, 0x94E0, 0, 0x00BA7000);
    simple({0x00469820, AddressOf(&Obj::CPathManager::Free)}, 0, 0x9820, 1);
    s.At({0x005656D0}, 0, [](Regs& r) { Record({0x56D0, r.ecx}); return g_netHeaps ? 1u : 0u; });
    s.At({0x00565700}, 0, [](Regs& r) { Record({0x5701, r.ecx}); return g_internetHeaps ? 1u : 0u; });
    simple({0x0045EFD0, AddressOf(&Obj::DestroyCoimHeap)}, 0, 0xEFD0, 0);
    simple({0x005BB560, AddressOf(&Mem::Manager::InitNetMiscHeap)}, 0, 0xB560, 1);
    simple({0x005BB660, AddressOf(&Mem::Manager::InitInternetHeaps)}, 0, 0xB660, 1);
    simple({0x005BAC50, AddressOf(&Mem::Manager::DeleteNetMiscHeap)}, 0, 0xAC50, 1);
    StandInApi(s, 0x006450EC, reinterpret_cast<const void*>(&GlobalMemoryStatus), 4, [](Regs& r) {
        auto* status = reinterpret_cast<MEMORYSTATUS*>(r.args[0]);
        Record({0x50EC});
        status->dwTotalPhys = 0x7FF12345;
        status->dwAvailPhys = 0x12345678;
        return 0u;
    });
    StandInApi(s, 0x00645218, reinterpret_cast<const void*>(&wsprintfA), 0, [](Regs& r) {
        Record({0x5218, Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2]});
        return static_cast<uint32_t>(std::snprintf(reinterpret_cast<char*>(r.args[0]), 64, "<%u>", r.args[2]));
    });
    StandInApi(s, 0x006450A4, reinterpret_cast<const void*>(&OutputDebugStringA), 4, [](Regs& r) {
        Record({0x50A4, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    // the object flag commands
    s.At({0x00461CC0, AddressOf(&Obj::CBaseManager::ForEach)}, 8, [](Regs& r) {
        Record({0x1CC0, r.ecx});
        for (auto& object : g_fakes->flagObjects)
            reinterpret_cast<Obj::ObjectCallback>(r.args[0])(reinterpret_cast<Obj::CObject*>(object),
                                                              reinterpret_cast<void*>(r.args[1]));
        return 0u;
    });
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Record({0x09F0, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using PrefixedFn = uint32_t* (*)(const char*, uint16_t*);
    s.At({0x0045BFB0, AddressOf(static_cast<PrefixedFn>(&SkateScript::GetPrefixedNodes))}, 0, [](Regs& r) {
        Record({0xBFB0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        *reinterpret_cast<uint16_t*>(r.args[1]) = static_cast<uint16_t>(g_prefixCount);
        return reinterpret_cast<uint32_t>(g_prefixNodes);
    });
    // skater scripts
    s.At({0x00566520}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return (g_skaterMask >> 8) & 1 ? reinterpret_cast<uint32_t>(g_fakes->skater) : 0u;
    });
    s.At({0x00582510, AddressOf(&Obj::CSkater::SwitchToScript)}, 8, [](Regs& r) {
        Record({0x2510, Rel(r.ecx), r.args[0], Rel(r.args[1])});
        return 0u;
    });
    s.At({0x00461500, AddressOf(&Obj::CObject::CallScript)}, 8, [](Regs& r) {
        Record({0x1500, Rel(r.ecx), r.args[0], Rel(r.args[1])});
        return 0u;
    });
    s.At({0x00472E00, AddressOf(&Script::GetSpawnId)}, 0, [](Regs& r) { Record({0x2E00, Rel(r.args[0])}); return 0x5150u; });
    s.At({0x00461410, AddressOf(&Obj::CObject::SpawnScript)}, 0x10, [](Regs& r) {
        Record({0x1410, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2], r.args[3] & 0xFF});
        return 0x00599000u;
    });
    s.At({0x00472F90, AddressOf(&Script::CScript::SetSpawnerFile)}, 4, [](Regs& r) {
        Record({0x2F90, r.ecx, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0059F330, AddressOf(&CFuncs::CFunc_SpawnScript)}, 0, [](Regs& r) {
        Record({0xF330, Rel(r.args[0]), Rel(r.args[1])});
        return 0x100u; // (its result passed on: only AL is read)
    });
    s.At({0x0045D990, AddressOf(&Obj::CCompositeObject::Hide)}, 4, [](Regs& r) {
        Record({0xD990, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    // pausing, the clock, the flash
    s.At({0x0054F960, AddressOf(&Mdl::FrontEnd::PauseGame)}, 4, [](Regs& r) {
        Record({0xF960, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00472DC0, AddressOf(&Script::UnpauseSpawnedScript)}, 0, [](Regs& r) { Record({0x2DC0, Rel(r.args[0])}); return 0u; });
    s.At({0x0045F5D0, AddressOf(&Obj::CCompositeObjectManager::Pause)}, 4, [](Regs& r) {
        Record({0xF5D0, r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00553170}, 0, [](Regs& r) { Record({0x3170, r.ecx}); return 120000u; });
    s.At({0x00564D50}, 4, [](Regs& r) { Record({0x4D50, r.ecx, r.args[0]}); return 0u; });
    s.At({0x0054ECA0, AddressOf(&Mdl::FrontEnd::PadsPluggedIn)}, 0, [](Regs& r) { Record({0xECA0, Rel(r.ecx)}); return 0x100u; });
    s.At({0x004923E0}, 0, [](Regs& r) {
        Record({0x23E0, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5],
                Hash(reinterpret_cast<const char*>(r.args[6]))});
        return 0u;
    });
}

std::vector<Saved> SavedFlowGlobals()
{
    return {{0x007CE490, 4, {}}, {0x007CE5D8, 4, {}}, {0x006FA7BC, 4, {}}, {0x006F85A8, 4, {}},
            {0x007D1800, 4, {}}, {0x00696900, 0x3C, {}}, {0x007CE470, 4, {}}, {0x006FA7D4, 1, {}}};
}

uint32_t CrcOf(const char* name) { return Crc::GenerateCRCFromString(name); }

// On a difference, the traces around the first one (to find which call it is).
void LogAroundFirstDifference(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i])
        ++i;
    if (i == a.size() && i == b.size())
        return;
    for (size_t k = i > 12 ? i - 12 : 0; k < i + 12; ++k)
        Log("  [%u] original %08X ours %08X", static_cast<unsigned>(k), k < a.size() ? a[k] : 0xEEEEEEEEu,
            k < b.size() ? b[k] : 0xEEEEEEEEu);
}

void TestGameFlowCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> flowSaved = SavedFlowGlobals();
    SaveAll(saved);
    SaveAll(flowSaved);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInFlowCalls(*standIns);
    g_slot1 = standIns->Thunk(0, [](Regs& r) { Record({0x5107, Rel(r.ecx)}); return 0u; });
    g_slot2 = standIns->Thunk(4, [](Regs& r) { Record({0x5207, Rel(r.ecx), r.args[0]}); return 0u; });
    void* pauseSlot = standIns->Thunk(0, [](Regs& r) { Record({0x2C2C, Rel(r.ecx)}); return 0u; });
    void* unpauseSlot = standIns->Thunk(0, [](Regs& r) { Record({0x3030, Rel(r.ecx)}); return 0u; });
    static uint8_t gameNet[0x10];
    const uint32_t kName = 0xA1DC81F9, kPrefix = 0x6C4E7971, kSkater = 0x5B8AB877;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        auto* skater = g_fakes->skater;
        reinterpret_cast<uint32_t*>(skater)[0] = reinterpret_cast<uint32_t>(g_fakes->skaterVtable);
        g_fakes->skaterVtable[0x2C / 4] = reinterpret_cast<uint32_t>(pauseSlot);
        g_fakes->skaterVtable[0x30 / 4] = reinterpret_cast<uint32_t>(unpauseSlot);
        reinterpret_cast<uint32_t*>(skater)[0x18 / 4] = 0xBAF9B341; // a skater
        reinterpret_cast<int*>(skater)[0x2BC / 4] = 1;               // its viewport
        GameVariable<uint32_t>(0x007CE490) = 0x00700490;
        GameVariable<uint32_t>(0x007CE5D8) = 0x007005D8;
        GameVariable<uint32_t>(0x006FA7BC) = 0x007007BC;
        GameVariable<uint32_t>(0x006F85A8) = 0x007005A8;
        GameVariable<uint32_t>(0x007D1800) = 0x00701800;
        GameVariable<uint32_t>(0x007CE470) = 0x00700470;
        GameVariable<void*>(0x007CCE14) = gameNet;
        auto call = [&](uint32_t address, CFunc ours, Params& params, bool withScript = true) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = withScript ? reinterpret_cast<Script::CScript*>(g_fakes->script) : nullptr;
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        auto setObject = [&](void* object) { reinterpret_cast<void**>(g_fakes->script)[0xBC / 4] = object; };
        Params none;

        // Cleanup: no params; an unrelated flag; preserve_skaters; no script; the symbol; the network heaps; the
        // .pre files (in use or not)
        static const char* pres[] = {"used.pre", "unused.pre", "used2.pre"};
        for (int state = 0; state < 5; ++state) {
            g_hasSymbol = state & 1;
            {
                // the global 0xC472ECC5 (its value at +8: a .qb file) in the stand-in symbol table, or not
                static uint32_t entry[5] = {1, 0xC472ECC5, 0x0F11E5, 0, 0};
                GameVariable<void**>(0x006F3624)[0xC472ECC5 & 0xFFF] = g_hasSymbol ? entry : nullptr;
            }
            g_netHeaps = state == 1 || state == 3;
            g_internetHeaps = state >= 2;
            for (int i = 0; i < 4; ++i)
                g_pres[i] = i < 3 && state != 4 ? pres[i] : nullptr;
            for (int i = 0; i < 15; ++i)
                GameVariable<uint32_t>(0x00696900 + 4 * i) = (i + state) % 4 ? 0xF00 + i : 0;
            g_fakes->script[0xCC] = static_cast<uint8_t>(state & 1 ? 0x0B : 0x0E);
            Params p;
            if (state == 1)
                p.Flag(0xC2E5C1B5);
            if (state == 3)
                p.Flag(0x7F07D3D4); // preserve_skaters
            CStruct* params = state == 2 ? nullptr : p.Struct();
            auto* script = state == 4 ? nullptr : reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(0x0059FE50)(params, script) : CFuncs::CFunc_Cleanup(params, script);
            Record({0xC1EA, result ? 1u : 0u, g_fakes->script[0xCC]});
            for (int i = 0; i < 15; ++i)
                Record({GameVariable<uint32_t>(0x00696900 + 4 * i)});
        }

        // the object flag commands: by name (set, clear, test set / unset, an unknown id, an exception), by prefix
        // (no nodes, three), neither; actions 3, 4 and an unknown one; the 0x005A0120 stub alone
        auto* objects = g_fakes->flagObjects;
        for (int i = 0; i < 3; ++i) {
            reinterpret_cast<uint32_t*>(objects[i])[0x14 / 4] = 0x0B10 + i;
            reinterpret_cast<uint32_t*>(objects[i])[0x24 / 4] = 0x80000010u >> i;
            reinterpret_cast<uint32_t*>(objects[i])[0x08 / 4] = 0x100u << i;
        }
        auto recordObjects = [&] {
            for (int k = 0; k < 3; ++k)
                Record({reinterpret_cast<uint32_t*>(objects[k])[0x24 / 4], reinterpret_cast<uint32_t*>(objects[k])[0x08 / 4]});
        };
        {
            Params a; a.Integer(0, 52).Checksum(kName, 0x0B11); call(0x005A3B20, CFuncs::CFunc_SendFlag, a);
            Params a2; a2.Integer(0, 5).Checksum(kName, 0x0B10); call(0x005A3B00, CFuncs::CFunc_ClearFlag, a2);
            Params b; b.Integer(0, 4).Checksum(kName, 0x0B10).Flag(0x2E7D5EE7); call(0x005A3B00, CFuncs::CFunc_ClearFlag, b);
            Params c; c.Integer(0, 31).Checksum(kName, 0x0B10); call(0x005A3B40, CFuncs::CFunc_QueryFlag, c);
            Params d; d.Integer(0, 30).Checksum(kName, 0x0B10); call(0x005A3B40, CFuncs::CFunc_QueryFlag, d);
            Params e; e.Integer(0, 2).Checksum(kName, 0x0B99); call(0x005A3B40, CFuncs::CFunc_QueryFlag, e);
            Params f; f.Checksum(0, 0xE8C3).Integer(0, 9).Checksum(kName, 0x0B12); call(0x005A3B60, CFuncs::CFunc_SendException, f);
            recordObjects();
            g_prefixCount = 0;
            Params g; g.Integer(0, 3).String(kPrefix, "TRG_Door"); call(0x005A3B20, CFuncs::CFunc_SendFlag, g);
            g_prefixCount = 3;
            g_prefixNodes[0] = 0x0B12;
            g_prefixNodes[1] = 0x0B77;
            g_prefixNodes[2] = 0x0B10;
            call(0x005A3B20, CFuncs::CFunc_SendFlag, g);
            g_prefixNodes[0] = 0x0B12;
            g_prefixNodes[2] = 0x0B11;
            Params h; h.Integer(0, 2).String(kPrefix, "TRG_Door"); call(0x005A3B40, CFuncs::CFunc_QueryFlag, h);
            Params i; i.Integer(0, 4); call(0x005A3B40, CFuncs::CFunc_QueryFlag, i);
            recordObjects();
            using CommandFn = bool(__cdecl*)(CStruct*, Script::CScript*, int);
            for (int action : {3, 4, 6}) {
                Params j; j.Integer(0, 0x00F0F00F).Checksum(kName, 0x0B11);
                bool result = original ? Original<CommandFn>(0x005A01D0)(j.Struct(), nullptr, action)
                                       : CFuncs::FlagCommandByName(j.Struct(), nullptr, action);
                Record({0x01D0, result ? 1u : 0u});
                recordObjects();
            }
            uint32_t data[3] = {0x00F0, 4, 0x0000AAAA};
            void* stub = original ? reinterpret_cast<void*>(0x005A0120) : reinterpret_cast<void*>(&CFuncs::ApplyFlagCommandStub);
            void* object = objects[2];
            __asm {
                push esi
                lea esi, data
                mov eax, object
                call stub
                pop esi
            }
            Record({data[0], data[1], data[2]});
            recordObjects();
        }

        // MakeSkaterGoto / Gosub: the calling script's skater; another object; skater=n; the local skater; none;
        // not local; no script
        for (int state = 0; state < 6; ++state) {
            setObject(state == 0 ? static_cast<void*>(g_fakes->skater) : state == 1 ? g_fakes->object : nullptr);
            g_skaterMask = state == 4 ? 0 : 0x1FF;
            g_fakes->skater[0xBC] = static_cast<uint8_t>(state != 5);
            Params inner; inner.Integer(0x1234, 5);
            Params p; p.Checksum(0, 0x5C417).Structure(0x7031F10C, inner.Struct());
            if (state == 2)
                p.Integer(kSkater, 1);
            call(0x005A0300, CFuncs::CFunc_MakeSkaterGoto, p);
            call(0x005A03C0, CFuncs::CFunc_MakeSkaterGosub, p);
        }
        g_fakes->skater[0xBC] = 1;
        Params noScript; noScript.Checksum(0, 0x5C418).Integer(kSkater, 0);
        call(0x005A0300, CFuncs::CFunc_MakeSkaterGoto, noScript, false);
        call(0x005A03C0, CFuncs::CFunc_MakeSkaterGosub, noScript, false);

        // SpawnSound: no object (SpawnScript); no name; a name; everything with the keep-spawner flag
        setObject(nullptr);
        Params sound; sound.Checksum(0, 0x50D);
        call(0x005A0460, CFuncs::CFunc_SpawnSound, sound);
        setObject(g_fakes->object);
        Params soundId; soundId.Integer(0, 2);
        call(0x005A0460, CFuncs::CFunc_SpawnSound, soundId);
        call(0x005A0460, CFuncs::CFunc_SpawnSound, sound);
        Params inner; inner.Integer(0x99, 1);
        Params soundAll;
        soundAll.Checksum(0, 0x50E).Checksum(CrcOf("Id"), 0x1D).Structure(CrcOf("Params"), inner.Struct()).Flag(0x01917DF2);
        call(0x005A0460, CFuncs::CFunc_SpawnSound, soundAll);
        Params soundSome; soundSome.Checksum(0, 0x50F).Structure(CrcOf("Params"), inner.Struct());
        call(0x005A0460, CFuncs::CFunc_SpawnSound, soundSome);

        // PauseSkater / UnPauseSkater: hidden or not; no skater
        for (int state = 0; state < 3; ++state) {
            g_skaterMask = state == 2 ? 0 : 0x0F;
            Params p; p.Integer(0, 3);
            if (state == 1)
                p.Flag(CrcOf("hide"));
            call(0x005A0520, CFuncs::CFunc_PauseSkater, p);
            call(0x005A0580, CFuncs::CFunc_UnPauseSkater, p);
        }
        call(0x005A05C0, CFuncs::CFunc_PauseGame, none);
        call(0x005A05E0, CFuncs::CFunc_UnPauseGame, none);
        for (uint8_t paused : {uint8_t(0), uint8_t(1)}) {
            GameVariable<uint8_t>(0x006FA7D4) = paused;
            call(0x005A05F0, CFuncs::CFunc_GameIsPaused, none);
        }
        call(0x005A0620, CFuncs::CFunc_PauseObjects, none);
        call(0x005A0630, CFuncs::CFunc_UnPauseObjects, none);
        for (uint8_t flags : {uint8_t(0), uint8_t(1), uint8_t(4)}) {
            gameNet[0] = flags;
            call(0x005A0640, CFuncs::CFunc_ResetClock, none);
        }
        call(0x005A0670, CFuncs::CFunc_PadsPluggedIn, none);

        // DoFlash: no duration; zero; everything; the viewport from the object; the colours partly missing; the
        // flag combinations
        setObject(g_fakes->skater);
        call(0x005A0680, CFuncs::CFunc_DoFlash, none);
        Params zero; zero.Float(CrcOf("duration"), 0.0f).Integer(kSkater, 0);
        call(0x005A0680, CFuncs::CFunc_DoFlash, zero);
        Params all;
        all.Float(CrcOf("duration"), 1.5f).String(CrcOf("texture"), "white").Float(CrcOf("z"), -3.25f).Integer(kSkater, 0)
            .Integer(CrcOf("start_r"), 0x111).Integer(CrcOf("start_g"), 0x22).Integer(CrcOf("start_b"), 0x33)
            .Integer(CrcOf("start_a"), 0x44).Flag(0x70FFC649).Flag(0x19C43CF6).Flag(0xDF6436D2);
        call(0x005A0680, CFuncs::CFunc_DoFlash, all);
        Params ends;
        ends.Float(CrcOf("duration"), 2.0f).Integer(CrcOf("end_r"), 0x55).Integer(CrcOf("end_g"), 0x66)
            .Integer(CrcOf("end_b"), 0x77).Integer(CrcOf("end_a"), 0x88).Integer(kSkater, 3);
        call(0x005A0680, CFuncs::CFunc_DoFlash, ends);
        for (int flags = 0; flags < 8; ++flags) {
            Params p;
            p.Float(CrcOf("duration"), 0.25f).Integer(CrcOf("start_g"), 0x20 + flags).Integer(CrcOf("end_b"), 0x90);
            if (flags & 1)
                p.Flag(0x70FFC649);
            if (flags & 2)
                p.Flag(0x387C9ED6);
            if (flags & 4)
                p.Flag(flags & 1 ? 0xDF6436D2 : 0x19C43CF6);
            call(0x005A0680, CFuncs::CFunc_DoFlash, p);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(flowSaved);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    Check(traces[0].size() > 400, "game flow commands: the original reached its calls");
    Compare(traces[0], traces[1], "game flow commands (0x0059FE50-0x005A0880, 0x005A3B00)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A0880-0x005A0F00 (net_state_cfuncs.cpp) ---------------------------------------------------------------

bool g_setupOk, g_drawNames, g_inNetGame, g_hasLocalPlayer;
uint32_t g_players, g_newPlayers[3];
int g_newPlayerIndex;

void StandInNetCalls(StandIns& s)
{
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    simple({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 0xB0E0, 3);
    simple({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0xB110, 1);
    simple({0x0056DEE0, AddressOf(&Mdl::SkateNet::StartServer)}, 0, 0xDEE0, 1);
    simple({0x00533F70, AddressOf(&GameNet::Manager::InLanMode)}, 0, 0x3F70, 1, 0x55);
    simple({0x0056B890}, 0, 0xB890, 1);
    simple({0x00472830, AddressOf(&Script::ProcessPendingSymbols)}, 0, 0x2830, 0);
    s.At({0x00553190}, 0, [](Regs& r) { Record({0x3190, r.ecx}); return g_players; });
    s.At({0x00537220, AddressOf(&GameNet::Manager::SpawnClient)}, 0x10, [](Regs& r) {
        Record({0x7220, Rel(r.ecx), r.args[0] & 0xFF, r.args[1] & 0xFF, r.args[2] & 0xFF, r.args[3]});
        return 0x00C11E00u + r.args[3];
    });
    s.At({0x00534480, AddressOf(&GameNet::Manager::JoinServer)}, 0xC, [](Regs& r) {
        Record({0x4480, Rel(r.ecx), r.args[0], r.args[1] & 0xFFFF, r.args[2]});
        return 0u;
    });
    s.At({0x0056EB50, AddressOf(&Mdl::SkateNet::AddClientHandlers)}, 8, [](Regs& r) {
        Record({0xEB50, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) { Record({0xE4F0, Rel(r.ecx)}); return g_inNetGame ? 1u : 0u; });
    s.At({0x005BEAAE}, 4, [](Regs& r) { Record({0xEAAE, Hash(reinterpret_cast<const char*>(r.args[0]))}); return 0x0100007Fu; });
    simple({0x00533F20, AddressOf(&GameNet::Manager::SetNetworkMode)}, 4, 0x3F20, 3);
    simple({0x0044F220, AddressOf(&Mlp::Manager::ProfileFrames)}, 4, 0xF220, 3);
    simple({0x00535040, AddressOf(&GameNet::Manager::UseNetworkPreferences)}, 0, 0x5040, 1);
    s.At({0x005B9760, AddressOf(&Stubs::ReturnTrue)}, 0, [](Regs&) { Record({0x9760}); return g_setupOk ? 1u : 0u; });
    s.At({0x005B9E50, AddressOf(&Stubs::ReturnFalse)}, 0, [](Regs&) { Record({0x9E50}); return 0x100u; });
    using GetStringFn = const char* (*)(const char*);
    s.At({0x00478FF0, AddressOf(static_cast<GetStringFn>(&Script::GetString))}, 0, [](Regs& r) {
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0x8FF0, Hash(name)});
        if (std::strcmp(name, "use_deci2") == 0)
            return reinterpret_cast<uint32_t>(g_newPlayerIndex == 0 ? "" : g_newPlayerIndex == 1 ? nullptr : "1");
        return reinterpret_cast<uint32_t>(name + 1);
    });
    using AddTypedFn = void (CStruct::*)(uint32_t, uint32_t, const char*);
    s.At({0x00478390, AddressOf(static_cast<AddTypedFn>(&CStruct::AddString))}, 0xC, [](Regs& r) {
        Record({0x8390, Rel(r.ecx), r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
        return 0u;
    });
    s.At({0x004549C0, AddressOf(&Net::Manager::LoadNetworkDrivers)}, 0xC, [](Regs& r) {
        Record({0x49C0, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])),
                Hash(reinterpret_cast<const char*>(r.args[1])), Hash(reinterpret_cast<const char*>(r.args[2]))});
        return r.args[0] ? 1u : 0x100u;
    });
    s.At({0x00535C10, AddressOf(&GameNet::Manager::ConnectToInternet)}, 8, [](Regs& r) {
        Record({0x5C10, Rel(r.ecx), r.args[0], r.args[1]});
        return r.args[0] & 1;
    });
    simple({0x00536C10, AddressOf(&GameNet::Manager::CancelConnectToInternet)}, 0, 0x6C10, 0);
    simple({0x00535CB0, AddressOf(&GameNet::Manager::DisconnectFromInternet)}, 4, 0x5CB0, 3);
    simple({0x0044E5F0, AddressOf(&Inp::Manager::EnableActuators)}, 4, 0xE5F0, 3);
    simple({0x0044E5D0, AddressOf(&Inp::Manager::DisableActuators)}, 4, 0xE5D0, 3);
    s.At({0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, [](Regs& r) {
        Record({0x7320, Rel(r.ecx)});
        return g_hasLocalPlayer ? 0x00A1A000u : 0u;
    });
    s.At({0x00543320, AddressOf(&GameNet::PlayerInfo::IsObserver)}, 0, [](Regs& r) { Record({0x3320, r.ecx}); return r.ecx & 0x100 ? 1u : 0u; });
    s.At({0x00537070, AddressOf(&GameNet::Manager::FirstNewPlayerInfo)}, 4, [](Regs& r) {
        Record({0x7070, Rel(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0])});
        g_newPlayerIndex = 0;
        return g_newPlayers[0];
    });
    s.At({0x004573D0, AddressOf(&GameNet::Manager::NextNewPlayerInfo)}, 4, [](Regs& r) {
        Record({0x73D0, Rel(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0])});
        return ++g_newPlayerIndex < 3 ? g_newPlayers[g_newPlayerIndex] : 0u;
    });
    s.At({0x00532AC0, AddressOf(&GameNet::Manager::GetJoinMode)}, 0, [](Regs& r) { Record({0x2AC0, Rel(r.ecx)}); return g_players; });
    simple({0x0044ED70, AddressOf(&Tmr::SetGameClockSpeed)}, 0, 0xED70, 2);
    simple({0x00533130, AddressOf(&GameNet::Manager::TogglePlayerNames)}, 0, 0x3130, 1);
    s.At({0x00533140, AddressOf(&GameNet::Manager::ShouldDrawPlayerNames)}, 0, [](Regs& r) { Record({0x3140, Rel(r.ecx)}); return g_drawNames ? 1u : 0u; });
    simple({0x00564FD0}, 0, 0x4FD0, 1);
}

void TestNetStateCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> flowSaved = SavedFlowGlobals();
    std::vector<Saved> netSaved = {{0x006EFDD8, 4, {}}, {0x00696D68, 4, {}}, {0x00696D40, 4, {}}};
    SaveAll(saved);
    SaveAll(flowSaved);
    SaveAll(netSaved);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInFlowCalls(*standIns);
    StandInNetCalls(*standIns);
    g_slot1 = standIns->Thunk(0, [](Regs& r) { Record({0x5107, Rel(r.ecx)}); return 0u; });
    g_slot2 = standIns->Thunk(4, [](Regs& r) { Record({0x5207, Rel(r.ecx), r.args[0]}); return 0u; });
    static uint8_t gameNet[0x10];
    static uint8_t net[0x380];
    static uint8_t newPlayers[3][0x30];
    static uint8_t skate[0x400];

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        std::memset(skate, 0, sizeof(skate));
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007CCE14) = gameNet;
        GameVariable<void*>(0x006EFDD8) = net;
        GameVariable<uint32_t>(0x00696D68) = 0x00696D00;
        GameVariable<uint32_t>(0x00696D40) = 0x00696C00;
        static uint32_t memoryManager[0x5E8 / 4];
        for (uint32_t i = 0; i < 0x5E8 / 4; ++i)
            memoryManager[i] = 0x00E30000 + i;
        GameVariable<void*>(0x007D1800) = memoryManager;
        GameVariable<uint32_t>(0x007CE470) = 0x00700470;
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;

        for (uint8_t flags : {uint8_t(0), uint8_t(4), uint8_t(8)}) {
            gameNet[0] = flags;
            call(0x005A0880, CFuncs::CFunc_StartServer, none);
        }
        call(0x005A08D0, CFuncs::CFunc_LeaveServer, none);
        // JoinServer: on the server (0 / 3 players, in a network game or not); a client with an address and port,
        // either missing
        for (int state = 0; state < 3; ++state) {
            gameNet[0] = 1;
            g_players = state ? 3 : 0;
            g_inNetGame = state == 2;
            call(0x005A08E0, CFuncs::CFunc_JoinServer, none);
        }
        gameNet[0] = 0;
        Params join; join.String(0, "10.0.0.7").Integer(0, 0x12345);
        call(0x005A08E0, CFuncs::CFunc_JoinServer, join);
        Params joinNoPort; joinNoPort.String(0, "10.0.0.8");
        call(0x005A08E0, CFuncs::CFunc_JoinServer, joinNoPort);
        Params joinNoIp; joinNoIp.Integer(0, 99);
        call(0x005A08E0, CFuncs::CFunc_JoinServer, joinNoIp);
        for (int mode : {-1, 0, 1, 2, 3}) {
            Params p; p.Integer(0, mode);
            call(0x005A0A00, CFuncs::CFunc_SetNetworkMode, p);
        }
        call(0x005A0A00, CFuncs::CFunc_SetNetworkMode, none);
        call(0x005A0A60, CFuncs::CFunc_ProfileTasks, none);
        Params frames; frames.Integer(0, 60);
        call(0x005A0A60, CFuncs::CFunc_ProfileTasks, frames);
        call(0x005A0AB0, CFuncs::CFunc_UseNetworkPreferences, none);
        // TestNetSetup: set up; each error
        g_setupOk = true;
        Params success; success.Checksum(CrcOf("success_script"), 0x5CC);
        call(0x005A0AC0, CFuncs::CFunc_TestNetSetup, success);
        g_setupOk = false;
        for (int error : {5, 4, 2, 6, 7, 3, 0, 1, 8}) {
            reinterpret_cast<int*>(net)[0x2E0 / 4] = error;
            call(0x005A0AC0, CFuncs::CFunc_TestNetSetup, success);
        }
        call(0x005A0BD0, CFuncs::CFunc_NeedToTestNetSetup, none);
        call(0x005A0BE0, CFuncs::CFunc_CanChangeDevices, none);
        for (int deci = 0; deci < 3; ++deci) {
            g_newPlayerIndex = deci; // (the use_deci2 string: empty, none, set)
            call(0x005A0BF0, CFuncs::CFunc_LoadNetworkDrivers, none);
        }
        call(0x005A0C60, CFuncs::CFunc_ConnectToInternet, none);
        Params connect; connect.Checksum(0x90FF204D, 0xC01).Checksum(0xDE64FC3E, 0xFA1);
        call(0x005A0C60, CFuncs::CFunc_ConnectToInternet, connect);
        call(0x005A0CC0, CFuncs::CFunc_CancelConnectToInternet, none);
        for (uint8_t online : {uint8_t(0), uint8_t(1)}) {
            net[0x2E4] = online;
            call(0x005A0CD0, CFuncs::CFunc_IsOnline, none);
        }
        call(0x005A0CE0, CFuncs::CFunc_DisconnectFromInternet, none);
        Params callback; callback.Checksum(0x86068BD9, 0xCA11);
        call(0x005A0CE0, CFuncs::CFunc_DisconnectFromInternet, callback);
        // EnableActuators: each controller used or not; on, off, the default
        for (int used = 0; used < 4; ++used) {
            skate[0x394] = static_cast<uint8_t>(used & 1);
            skate[0x39C] = static_cast<uint8_t>(used >> 1);
            Params off; off.Integer(0, used == 3 ? 7 : 0);
            call(0x005A0D20, CFuncs::CFunc_EnableActuators, off);
            call(0x005A0D20, CFuncs::CFunc_EnableActuators, none);
        }
        for (int state = 0; state < 2; ++state) {
            g_inNetGame = state;
            call(0x005A0D80, CFuncs::CFunc_InNetGame, none);
        }
        // IsObserving: the local player (observer or not); joining players (none matching, one); the join mode
        g_hasLocalPlayer = true;
        call(0x005A0D90, CFuncs::CFunc_IsObserving, none);
        g_hasLocalPlayer = false;
        for (int i = 0; i < 3; ++i) {
            std::memset(newPlayers[i], 0, sizeof(newPlayers[i]));
            g_newPlayers[i] = reinterpret_cast<uint32_t>(newPlayers[i]);
        }
        reinterpret_cast<uint32_t*>(newPlayers[0])[0x20 / 4] = 1;
        reinterpret_cast<uint32_t*>(newPlayers[1])[0x20 / 4] = 4;
        reinterpret_cast<uint32_t*>(newPlayers[2])[0x20 / 4] = 0x10;
        for (uint32_t mode : {0u, 1u, 2u}) {
            g_players = mode;
            call(0x005A0D90, CFuncs::CFunc_IsObserving, none);
        }
        reinterpret_cast<uint32_t*>(newPlayers[2])[0x20 / 4] = 5;
        call(0x005A0D90, CFuncs::CFunc_IsObserving, none);
        g_newPlayers[0] = 0;
        call(0x005A0D90, CFuncs::CFunc_IsObserving, none);
        // SetSlomo: no local skater; one without / with the component
        for (int state = 0; state < 3; ++state) {
            g_skaterMask = state ? 0x100 : 0;
            g_hasComponent = state == 2;
            Params p;
            if (state)
                p.Float(0, 0.25f * state);
            call(0x005A0E50, CFuncs::CFunc_SetSlomo, p);
            Record({reinterpret_cast<uint32_t*>(g_fakes->component)[0x2E8 / 4]});
        }
        for (bool draw : {false, true}) {
            g_drawNames = draw;
            call(0x005A0EB0, CFuncs::CFunc_TogglePlayerNames, none);
        }
        call(0x005A0EF0, CFuncs::CFunc_SetCurrentGameType, none);
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(netSaved);
    RestoreAll(flowSaved);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    Check(traces[0].size() > 300, "network state commands: the original reached its calls");
    Compare(traces[0], traces[1], "network state commands (0x005A0880-0x005A0F00)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

} // namespace

SELFTEST(Early, TestGameCFuncs, RemoveStandIns);
SELFTEST(Early, TestGameFlowCFuncs, RemoveStandIns);
SELFTEST(Early, TestNetStateCFuncs, RemoveStandIns);
