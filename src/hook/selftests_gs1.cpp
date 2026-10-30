// Self-tests for the skate game's script commands 0x0059F610-0x005A5360 (game_cfuncs.h): each original against
// ours on identical stand-in state; every game function they call is stood in for by recording code (over the
// game's function and our version of it alike), so they're compared call by call.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
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
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_manager.h"
#include "game/net/player_info.h"
#include "game/script/qb.h"
#include "game/net/net_conn.h"
#include "game/skate/career.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/obj/input_component.h"
#include "game/skater/trick_component.h"
#include "game/sys/timer.h"
#include "game/file/file.h"
#include "game/obj/rail_manager.h"
#include "game/script/nodes.h"
#include <cstdlib>
#include "game/lst/hashtable.h"
#include "game/mth/vector.h"
#include "game/obj/emitter.h"
#include "game/obj/game_objects.h"
#include "game/obj/node_objects.h"
#include "game/obj/proxim.h"
#include "game/script/names.h"
#include "game/obj/trick_objects.h"
#include "game/skate/gap_checklist.h"
#include "game/core/pool.h"
#include "game/obj/smtptr.h"
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
#include "game/gfx/face_texture.h"
#include "game/gfx/viewport.h"
#include "game/gfx/viewport_list.h"
#include "game/skate/game_mode.h"
#include "game/goals/goal_manager.h"
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
        Record({0x1C30, Hash(format)});
        int arguments = 0;
        for (const char* c = format; *c && arguments < 4; ++c)
            arguments += *c == '%';
        for (int i = 1; i <= arguments; ++i)
            Record({r.args[i]});
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
    s.At({0x004A09C0, AddressOf(Nx::GetActiveCamera)}, 0, [](Regs& r) { Record({0x09C0, r.args[0]}); return reinterpret_cast<uint32_t>(g_fakes->camera); });
    s.At({0x004A0970, AddressOf(Nx::SetCamera)}, 0, [](Regs& r) { Record({0x0970, r.args[0], r.args[1]}); return 0u; });
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
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
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
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
    simple({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, 0x91B0, 0, 0x00517F00);
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
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
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
    s.At({0x00564D50, AddressOf(&Mdl::Skate::SetUnknown370)}, 4, [](Regs& r) { Record({0x4D50, r.ecx, r.args[0]}); return 0u; });
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

// --- 0x005A0F00-0x005A1320 (net_state_cfuncs.cpp, game_flow_cfuncs.cpp) ----------------------------------------

uint32_t g_clients[2];
int g_connIndex, g_serviceState, g_modeMatch;
uint8_t (*g_conns)[0x3E00];

void StandInServerListCalls(StandIns& s)
{
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    s.At({0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 4, [](Regs& r) {
        Record({0x2EA0, r.args[0]});
        return g_clients[r.args[0] & 1];
    });
    s.At({0x00458BE0, AddressOf(&Net::App::FirstConnection)}, 4, [](Regs& r) {
        Record({0x8BE0, r.ecx, *reinterpret_cast<uint32_t*>(r.args[0])});
        g_connIndex = 0;
        return g_conns ? reinterpret_cast<uint32_t>(g_conns[0]) : 0u;
    });
    s.At({0x004573D0, AddressOf(&Net::App::NextConnection)}, 4, [](Regs& r) {
        Record({0x73D0, r.ecx});
        return ++g_connIndex < 2 ? reinterpret_cast<uint32_t>(g_conns[g_connIndex]) : 0u;
    });
    simple({0x00533F50, AddressOf(&GameNet::Manager::SetServerMode)}, 4, 0x3F50, 5);
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        Record({0x9E50, r.ecx, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 0x12)});
        return 0u;
    });
    simple({0x0053AA70, AddressOf(&GameNet::Manager::SendObserveRequest)}, 0, 0xAA70, 1);
    simple({0x005651D0}, 0, 0x51D0, 1);
    simple({0x00547E80, AddressOf(&GameNet::Manager::RefreshServerList)}, 4, 0x7E80, 5);
    simple({0x00545300, AddressOf(&GameNet::Manager::StartServerList)}, 0, 0x5300, 1);
    s.At({0x005451C0, AddressOf(&GameNet::Manager::GetServiceState)}, 0, [](Regs&) { Record({0x51C0}); return static_cast<uint32_t>(g_serviceState); });
    simple({0x005451B0, AddressOf(&GameNet::Manager::SetNextServiceState)}, 4, 0x51B0, 3);
    simple({0x005453E0, AddressOf(&GameNet::Manager::SetServiceState)}, 4, 0x53E0, 3);
    simple({0x00545370, AddressOf(&GameNet::Manager::StopServerList)}, 0, 0x5370, 1);
    simple({0x005462F0, AddressOf(&GameNet::Manager::ClearServerList)}, 0, 0x62F0, 1);
    s.At({0x005651F0, AddressOf(Mdl::Skate::IsMultiplayer)}, 0, [](Regs& r) { Record({0x51F0, r.ecx}); return static_cast<uint32_t>(g_modeMatch & 1); });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs&) { Record({0xE4F0}); return g_modeMatch & 2 ? 1u : 0u; });
    simple({0x005536D0}, 0, 0x36D0, 1);
    s.At({0x00553220, AddressOf(&Game::CGameMode::IsTrue)}, 4, [](Regs& r) { Record({0x3220, r.ecx, r.args[0]}); return r.args[0] == static_cast<uint32_t>(g_modeMatch) ? 1u : 0u; });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 0xC, [](Regs& r) {
        Record({0x83C0, Rel(r.ecx), r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    simple({0x005668A0}, 0, 0x68A0, 1);
    simple({0x00535BF0, AddressOf(&GameNet::Manager::ToggleScores)}, 0, 0x5BF0, 1);
    simple({0x00552E30}, 4, 0x2E30, 3);
}

void TestServerListCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> flowSaved = SavedFlowGlobals();
    SaveAll(saved);
    SaveAll(flowSaved);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInServerListCalls(*standIns);
    static uint8_t gameNet[0x10];
    static uint8_t conns[2][0x3E00];
    static uint8_t skate[0x400];
    static uint8_t gameFlow[0x10];

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        std::memset(skate, 0, sizeof(skate));
        reinterpret_cast<uint32_t*>(skate)[0x374 / 4] = 0x1E7E1;
        reinterpret_cast<uint32_t*>(skate)[0x380 / 4] = 0x57C7;
        reinterpret_cast<void**>(skate)[0x388 / 4] = gameFlow;
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007CCE14) = gameNet;
        GameVariable<uint32_t>(0x007CE470) = 0x00700470;
        auto call = [&](uint32_t address, CFunc ours, Params& params, bool withScript = true) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = withScript ? reinterpret_cast<Script::CScript*>(g_fakes->script) : nullptr;
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;

        // DumpNetMessageStats: the server; no server (client 0; none)
        for (size_t i = 0; i < sizeof(conns); ++i)
            conns[0][i] = static_cast<uint8_t>(i * 13);
        g_conns = conns;
        reinterpret_cast<uint32_t*>(gameNet)[1] = 0x005E7E00;
        call(0x005A0F00, CFuncs::CFunc_DumpNetMessageStats, none);
        reinterpret_cast<uint32_t*>(gameNet)[1] = 0;
        g_clients[0] = 0x00C11E00;
        call(0x005A0F00, CFuncs::CFunc_DumpNetMessageStats, none);
        g_clients[0] = 0;
        call(0x005A0F00, CFuncs::CFunc_DumpNetMessageStats, none);
        Params off; off.Flag(CrcOf("off"));
        call(0x005A1020, CFuncs::CFunc_SetServerMode, none);
        call(0x005A1020, CFuncs::CFunc_SetServerMode, off);
        // NotifyBailDone: no script, no object, not a skater, not local, no client, sent
        auto* skater = g_fakes->skater;
        reinterpret_cast<uint32_t*>(skater)[0x18 / 4] = 0xBAF9B341;
        reinterpret_cast<int*>(skater)[0x2BC / 4] = 1;
        auto setObject = [&](void* object) { reinterpret_cast<void**>(g_fakes->script)[0xBC / 4] = object; };
        call(0x005A1050, CFuncs::CFunc_NotifyBailDone, none, false);
        setObject(nullptr);
        call(0x005A1050, CFuncs::CFunc_NotifyBailDone, none);
        setObject(g_fakes->object);
        call(0x005A1050, CFuncs::CFunc_NotifyBailDone, none);
        setObject(skater);
        skater[0xBC] = 0;
        call(0x005A1050, CFuncs::CFunc_NotifyBailDone, none);
        skater[0xBC] = 1;
        g_clients[1] = 0;
        call(0x005A1050, CFuncs::CFunc_NotifyBailDone, none);
        g_clients[1] = 0x00C11E01;
        call(0x005A1050, CFuncs::CFunc_NotifyBailDone, none);
        call(0x005A10D0, CFuncs::CFunc_EnterObserverMode, none);
        call(0x005A10E0, CFuncs::CFunc_ObserveNextSkater, none);
        Params force; force.Flag(CrcOf("force_refresh"));
        call(0x005A10F0, CFuncs::CFunc_RefreshServerList, none);
        call(0x005A10F0, CFuncs::CFunc_RefreshServerList, force);
        call(0x005A1120, CFuncs::CFunc_StartServerList, none);
        for (int state : {0, 2, 3}) {
            g_serviceState = state;
            call(0x005A1130, CFuncs::CFunc_StartLobbyList, none);
        }
        call(0x005A1160, CFuncs::CFunc_StopServerList, none);
        call(0x005A1170, CFuncs::CFunc_FreeServerList, none);
        using IntFn = int(__cdecl*)(CStruct*, Script::CScript*);
        for (int match = 0; match < 4; ++match) {
            g_modeMatch = match;
            int split = original ? Original<IntFn>(0x005A1180)(nullptr, nullptr) : CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr);
            int multi = original ? Original<IntFn>(0x005A11B0)(nullptr, nullptr) : CFuncs::CFunc_InMultiplayerGame(nullptr, nullptr);
            Record({0x1180, static_cast<uint32_t>(split), static_cast<uint32_t>(multi)});
        }
        for (uint8_t flags : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
            gameNet[0] = flags;
            call(0x005A1220, CFuncs::CFunc_OnServer, none);
        }
        call(0x005A12D0, CFuncs::CFunc_ToggleScores, none);
        call(0x005A11C0, CFuncs::CFunc_RoundIncrement, none);
        // GameModeEquals: none; named components and flags, the mode last; the mode first
        g_modeMatch = 0x0B0B;
        call(0x005A11D0, CFuncs::CFunc_GameModeEquals, none);
        Params modes; modes.Checksum(0x99, 0x0B0B).Flag(0x0A0A).Integer(0, 0x0B0B).Flag(0x0B0B);
        call(0x005A11D0, CFuncs::CFunc_GameModeEquals, modes);
        Params first; first.Flag(0x0B0B).Flag(0x0C0C);
        call(0x005A11D0, CFuncs::CFunc_GameModeEquals, first);
        Params integerOnly; integerOnly.Integer(0, 0x0B0B);
        call(0x005A11D0, CFuncs::CFunc_GameModeEquals, integerOnly);
        Params other; other.Flag(0x0C0C);
        call(0x005A11D0, CFuncs::CFunc_GameModeEquals, other);
        call(0x005A1230, CFuncs::CFunc_GetCurrentLevel, none);
        Params structure; structure.Checksum(0xA1DC81F9, 0x5C5C);
        call(0x005A1270, CFuncs::CFunc_SetLevelStructureName, structure);
        Record({reinterpret_cast<uint32_t*>(skate)[0x380 / 4]});
        call(0x005A12A0, CFuncs::CFunc_GetLevelStructureName, none);
        call(0x005A12C0, CFuncs::CFunc_RestartLevel, none);
        call(0x005A12E0, CFuncs::CFunc_GameFlow, none);
        Params flow; flow.Checksum(0, 0xF10E);
        call(0x005A12E0, CFuncs::CFunc_GameFlow, flow);
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(flowSaved);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    g_conns = nullptr;
    Check(traces[0].size() > 150, "server list commands: the original reached its calls");
    Compare(traces[0], traces[1], "server list, game mode commands (0x005A0F00-0x005A1320)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A1320-0x005A1C80 (game_flow_cfuncs.cpp, net_state_cfuncs.cpp) ----------------------------------------

uint32_t g_screenMode, g_currentScreenMode;
CStruct* g_cheatEntries[3];
uint32_t g_capturedCheat[2];
bool g_gameOver;

void StandInLobbyCalls(StandIns& s)
{
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    simple({0x005843D0, AddressOf(&Obj::CSkaterCareer::UnSetGlobalFlag)}, 4, 0x43D0, 3);
    using IntFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<IntFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return r.args[0] >> 27;
    });
    s.At({0x00455A30, AddressOf(&Net::Conn::IsRemote)}, 0, [](Regs& r) {
        Record({0x5A30, Rel(r.ecx)});
        return reinterpret_cast<uint32_t*>(r.ecx)[0x3C / 4] & 1;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[1]);
        Record({0x7EB0, r.ecx, r.args[0], desc->m_id, desc->m_length, static_cast<uint32_t>(desc->m_priority),
                static_cast<uint32_t>(desc->m_queue), desc->m_groupId, HashBytes(static_cast<uint8_t*>(desc->m_data), 5),
                GameVariable<uint32_t>(0x007CEBC8)});
        return 0u;
    });
    simple({0x004A0C20}, 0, 0x0C20, 2);
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, Rel(r.ecx)});
        return g_skaterMask & 0x200 ? 0x005C0E00u : 0u;
    });
    simple({0x005626E0}, 0, 0x26E0, 1);
    s.At({0x00553630}, 0, [](Regs&) { Record({0x3630}); return g_screenMode; });
    s.At({0x004A0710, AddressOf(Nx::GetScreenMode)}, 0, [](Regs&) { Record({0x0710}); return g_currentScreenMode; });
    simple({0x004A0AD0}, 0, 0x0AD0, 2);
    s.At({0x006234FE}, 0, StandInSprintf);
    using ArrayFn = Script::CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<ArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        static Script::CArray array;
        array.m_type = Script::ESYMBOLTYPE_STRUCTURE;
        array.m_size = g_cheatEntries[2] ? 3 : g_cheatEntries[0] ? 1 : 0;
        array.m_elements = reinterpret_cast<uint32_t*>(g_cheatEntries);
        if (array.m_size == 1)
            array.m_single = reinterpret_cast<uint32_t>(g_cheatEntries[0]);
        return reinterpret_cast<uint32_t>(&array);
    });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0x1C30, Hash(format), std::strchr(format, '%') ? r.args[1] : 0u,
                std::strstr(format, "c2") ? r.args[2] : 0u});
        if (std::strstr(format, "c2")) { // (the cheat's numbers, for the next call's matching entry)
            g_capturedCheat[0] = r.args[1];
            g_capturedCheat[1] = r.args[2];
        }
        return 0u;
    });
    simple({0x00534BD0, AddressOf(&GameNet::Manager::ReattemptJoinWithPassword)}, 4, 0x4BD0, 1);
    simple({0x0053D120, AddressOf(&GameNet::Manager::ReAddPendingPlayers)}, 0, 0xD120, 1);
    simple({0x00537D90, AddressOf(&GameNet::Manager::LaunchQueuedScripts)}, 0, 0x7D90, 1);
    simple({0x0053AF90, AddressOf(&GameNet::Manager::ResetToLobby)}, 0, 0xAF90, 1);
    simple({0x00532F70, AddressOf(&GameNet::Manager::GetNetworkPreferences)}, 0, 0x2F70, 1, 0x00FE0001);
    s.At({0x005332B0, AddressOf(&GameNet::Manager::IsGameOver)}, 0, [](Regs&) { Record({0x32B0}); return g_gameOver ? 1u : 0u; });
}

void TestLobbyCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> flowSaved = SavedFlowGlobals();
    std::vector<Saved> netSaved = {{0x006EFDD8, 4, {}}};
    SaveAll(saved);
    SaveAll(flowSaved);
    SaveAll(netSaved);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInServerListCalls(*standIns);
    StandInLobbyCalls(*standIns);
    static uint8_t gameNet[0x10];
    static uint8_t net[0x380];
    static uint8_t conns[2][0x3E00];
    static uint8_t skate[0x400];

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        std::memset(skate, 0, sizeof(skate));
        reinterpret_cast<uint32_t*>(skate)[0x20 / 4] = 0x00CA4EE4; // the career
        reinterpret_cast<uint32_t*>(skate)[0x378 / 4] = 0x1E7E1;
        reinterpret_cast<uint32_t*>(skate)[0x37C / 4] = 0xE92ECAFE;
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007CCE14) = gameNet;
        GameVariable<void*>(0x006EFDD8) = net;
        GameVariable<uint32_t>(0x007CE470) = 0x00700470;
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;

        call(0x005A1320, CFuncs::CFunc_ClearOmnigons, none);
        // BroadcastOmnigon: two connections (one remote), with and without the flag; no connections
        std::memset(conns, 0, sizeof(conns));
        reinterpret_cast<uint32_t*>(conns[0])[0x3C / 4] = 0x10;
        reinterpret_cast<uint32_t*>(conns[1])[0x3C / 4] = 0x21;
        g_conns = conns;
        reinterpret_cast<uint32_t*>(gameNet)[1] = 0x005E7E00;
        Params omnigon; omnigon.Checksum(0xAE94C183, 0x0A61).Integer(0xF649D637, 0);
        call(0x005A1540, CFuncs::CFunc_BroadcastOmnigon, omnigon);
        Record({GameVariable<uint32_t>(0x007CEBC8)});
        Params omnigon2; omnigon2.Checksum(0xAE94C183, 0x0A62);
        reinterpret_cast<uint32_t*>(conns[0])[0x3C / 4] = 0x11;
        call(0x005A1540, CFuncs::CFunc_BroadcastOmnigon, omnigon2);
        g_conns = nullptr;
        call(0x005A1540, CFuncs::CFunc_BroadcastOmnigon, omnigon);
        Record({GameVariable<uint32_t>(0x007CEBC8)});
        // the screen mode: skaters with and without scores, local or not; the mode pairs
        for (int state = 0; state < 3; ++state) {
            g_skaterMask = state == 0 ? 0 : state == 1 ? 0x2A5 : 0x05A;
            g_fakes->skater[0xBC] = static_cast<uint8_t>(state == 1);
            call(0x005A1720, CFuncs::CFunc_SetScreenMode, none);
            for (uint32_t mode : {0u, 1u, 2u, 3u}) {
                for (uint32_t current : {1u, 2u}) {
                    g_screenMode = mode;
                    g_currentScreenMode = current;
                    call(0x005A1770, CFuncs::CFunc_SetScreenModeFromGameMode, none);
                }
            }
        }
        // TryCheatString: too short; no cheats; a list without the code; with it (the numbers the first run logged)
        static Params cheat1, cheat2, cheat3;
        cheat1 = Params();
        cheat1.Integer(CrcOf("c1"), 1).Integer(CrcOf("c2"), 2).Checksum(CrcOf("cheatscript"), 0xC4EA7);
        Params shortText; shortText.String(0x61414D56, "abcde");
        call(0x005A1840, CFuncs::CFunc_TryCheatString, shortText);
        Params text; text.String(0x61414D56, "SKATELIKEAPRO");
        g_cheatEntries[0] = g_cheatEntries[1] = g_cheatEntries[2] = nullptr;
        call(0x005A1840, CFuncs::CFunc_TryCheatString, text);
        g_cheatEntries[0] = cheat1.Struct();
        call(0x005A1840, CFuncs::CFunc_TryCheatString, text);
        cheat2 = Params();
        cheat2.Integer(CrcOf("c1"), static_cast<int>(g_capturedCheat[0])).Integer(CrcOf("c2"), static_cast<int>(g_capturedCheat[1]))
            .Checksum(CrcOf("cheatscript"), 0xC4EA8);
        cheat3 = Params();
        cheat3.Integer(CrcOf("c1"), static_cast<int>(g_capturedCheat[0])).Integer(CrcOf("c2"), 5)
            .Checksum(CrcOf("cheatscript"), 0xC4EA9);
        g_cheatEntries[1] = cheat2.Struct();
        g_cheatEntries[2] = cheat3.Struct();
        call(0x005A1840, CFuncs::CFunc_TryCheatString, text);
        Params text2; text2.String(0x61414D56, "a much longer cheat text, 34 long");
        call(0x005A1840, CFuncs::CFunc_TryCheatString, text2);
        Params text3; text3.String(0x61414D56, "ABCDEFG"); // (shorter than the numbers: the first fill's bytes are read)
        call(0x005A1840, CFuncs::CFunc_TryCheatString, text3);
        Params level; level.Checksum(0, 0x1E7E1);
        call(0x005A1A90, CFuncs::CFunc_LevelIs, level);
        Params otherLevel; otherLevel.Checksum(0, 0x1E7E2);
        call(0x005A1A90, CFuncs::CFunc_LevelIs, otherLevel);

        // the lobby and the network options
        Params password; password.String(0x61414D56, "hunter2");
        call(0x005A1680, CFuncs::CFunc_JoinWithPassword, password);
        call(0x005A1820, CFuncs::CFunc_LoadPendingPlayers, none);
        call(0x005A1830, CFuncs::CFunc_LaunchQueuedScripts, none);
        call(0x005A1AC0, CFuncs::CFunc_StartNetworkLobby, none);
        for (int type : {0, 1, 2}) {
            reinterpret_cast<int*>(net)[0x128 / 4] = type;
            call(0x005A1AD0, CFuncs::CFunc_ObserversAllowed, none);
            Params two; two.Checksum(0, 0x1FA67C3D);
            call(0x005A1AF0, CFuncs::CFunc_NumPlayersAllowed, two);
            Params four; four.Checksum(0, 0x1FA67C3E);
            call(0x005A1AF0, CFuncs::CFunc_NumPlayersAllowed, four);
        }
        static Params preference;
        for (uint32_t value : {0xF81BC89Bu, 0x2189CFF8u, 0x09562A0Au, 0x12345678u}) {
            preference = Params();
            preference.Checksum(0x21902065, value);
            g_preference = &preference;
            call(0x005A1B30, CFuncs::CFunc_AutoDNS, none);
            call(0x005A1B70, CFuncs::CFunc_UsingDefaultMasterServers, none);
            call(0x005A1BB0, CFuncs::CFunc_UsingDHCP, none);
            call(0x005A1C30, CFuncs::CFunc_DeviceChosen, none);
        }
        for (uint8_t flags : {uint8_t(0), uint8_t(4), uint8_t(8)}) {
            gameNet[0] = flags;
            call(0x005A1BF0, CFuncs::CFunc_InInternetMode, none);
        }
        for (int state = 0; state < 4; ++state) {
            g_modeMatch = state & 2;
            reinterpret_cast<uint32_t*>(skate)[0x37C / 4] = state & 1 ? 0xE92ECAFE : 0xE92ECAFF;
            call(0x005A1C00, CFuncs::CFunc_EnteringNetGame, none);
        }
        for (bool over : {false, true}) {
            g_gameOver = over;
            call(0x005A1C70, CFuncs::CFunc_GameIsOver, none);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(netSaved);
    RestoreAll(flowSaved);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    g_conns = nullptr;
    Check(traces[0].size() > 300, "lobby commands: the original reached its calls");
    Compare(traces[0], traces[1], "omnigons, cheats, lobby commands (0x005A1320-0x005A1C80)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A1C80-0x005A21C0 (game_cfuncs.cpp, game_flow_cfuncs.cpp) ----------------------------------------------

bool g_hasAsset, g_callocFails;
uint32_t g_clock;
uint8_t g_components[2][3][0x20];
int g_componentCounts[2];
uint8_t g_task[0x20];

void StandInViewerModeCalls(StandIns& s)
{
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    simple({0x00577740, AddressOf(&Mdl::Viewer::PlaceCamera)}, 4, 0x7740, 3, 1);
    simple({0x00577170, AddressOf(&Mdl::Viewer::CreateViewerObject)}, 4, 0x7170, 3);
    s.At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) {
        Record({0x7B90, r.ecx, r.args[0], r.args[1]});
        return g_hasAsset ? 0x00A55E70u : 0u;
    });
    simple({0x00407B20, AddressOf(&Ass::CAssMan::UnloadAsset)}, 4, 0x7B20, 3);
    s.At({0x00408870, AddressOf(&Ass::CAssMan::LoadAnim)}, 0x14, [](Regs& r) {
        Record({0x8870, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3] & 0xFF,
                r.args[4] & 0xFF});
        return 1u;
    });
    simple({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 0xB0E0, 3);
    simple({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0xB110, 1);
    s.At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Record({0xF2D0, r.ecx, r.args[0]});
        int list = r.args[0] == 0x27D7CD28 ? 0 : 1;
        for (int i = 0; i < 3; ++i)
            reinterpret_cast<uint32_t*>(g_components[list][i])[0x14 / 4] =
                i + 1 < g_componentCounts[list] ? reinterpret_cast<uint32_t>(g_components[list][i + 1]) : 0u;
        return g_componentCounts[list] ? reinterpret_cast<uint32_t>(g_components[list][0]) : 0u;
    });
    s.At({0x0041E7E0, AddressOf(&Obj::CInputComponent::LockButton)}, 0xC, [](Regs& r) {
        Record({0xE7E0, r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x0051F290, AddressOf(&Obj::CTrickComponent::HoldOffButton)}, 8, [](Regs& r) {
        Record({0xF290, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0044E5A0, AddressOf(&Inp::ButtonIndex)}, 0, [](Regs& r) { Record({0xE5A0, r.args[0]}); return r.args[0] & 0xF; });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) { Record({0xE970}); return g_clock; });
    simple({0x0054EF60, AddressOf(&Mdl::FrontEnd::AddKeyboardHandler)}, 4, 0xEF60, 3);
    simple({0x0054ECD0, AddressOf(&Mdl::FrontEnd::RemoveKeyboardHandler)}, 0, 0xECD0, 1);
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        std::memset(g_task, 0xCD, sizeof(g_task));
        return g_callocFails ? 0u : reinterpret_cast<uint32_t>(g_task);
    });
    s.At({0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, [](Regs& r) {
        Record({0x6890, r.ecx == reinterpret_cast<uint32_t>(g_task) ? 1u : 0u, r.args[0]});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x18);
        return r.ecx;
    });
    s.At({0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, [](Regs& r) {
        Record({0x6C90, r.ecx, r.args[0] == reinterpret_cast<uint32_t>(g_task) ? 1u : r.args[0]});
        return 0u;
    });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00553170}, 0, [](Regs& r) { Record({0x3170, r.ecx}); return 120000u; });
    simple({0x005532D0}, 0, 0x32D0, 1, 0x60A15);
    simple({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, 1, 0x60DE);
    s.At({0x00565ED0}, 8, [](Regs& r) { Record({0x5ED0, r.ecx, r.args[0], Rel(r.args[1])}); return 0u; });
    simple({0x00565F80}, 0, 0x5F80, 1, 0x100);
}

void TestViewerModeCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> more = {{0x007CEFAC, 1, {}}, {0x007D1800, 4, {}}, {0x00696D68, 4, {}}, {0x007CE470, 4, {}}};
    SaveAll(saved);
    SaveAll(more);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInViewerModeCalls(*standIns);
    g_slot1 = standIns->Thunk(0, [](Regs& r) { Record({0x5107, Rel(r.ecx)}); return 0u; });
    g_slot2 = standIns->Thunk(4, [](Regs& r) { Record({0x5207, Rel(r.ecx), r.args[0]}); return 0u; });
    static uint8_t skate[0x400];
    static uint32_t memoryManager[0x5E8 / 4];
    for (uint32_t i = 0; i < 0x5E8 / 4; ++i)
        memoryManager[i] = 0x00E30000 + i;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        std::memset(skate, 0, sizeof(skate));
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007D1800) = memoryManager;
        GameVariable<uint32_t>(0x00696D68) = 0x00696D00;
        GameVariable<uint32_t>(0x007CE470) = 0x00700470;
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;

        // SetViewerModel: with and without a viewer; GetViewerObjectID
        g_hasObject = true;
        g_hasComponent = true;
        g_skaterMask = 0x0C;
        Params model; model.String(0, "models\\skater.mdl").Integer(0x77, 3);
        for (bool viewer : {false, true}) {
            g_hasViewer = viewer;
            call(0x005A1C80, CFuncs::CFunc_SetViewerModel, model);
            Record({GameVariable<uint32_t>(0x007CEBCC)});
        }
        g_hasViewerObject = true;
        call(0x005A1D40, CFuncs::CFunc_GetViewerObjectID, none);
        g_hasViewer = false;
        call(0x005A1D40, CFuncs::CFunc_GetViewerObjectID, none);
        // ReloadAnim: not loaded; by file name; by base + anim; in the debug heap
        Params byFile; byFile.Checksum(0x52A864B0, 0xA417).String(0xC3F4169A, "anims\\ollie.ska");
        Params byBase; byBase.Checksum(0x52A864B0, 0xA418).Checksum(0x98549BA4, 0xBA5E).String(0xC3F4169A, "anims\\kick.ska");
        g_hasAsset = false;
        call(0x005A1D80, CFuncs::CFunc_ReloadAnim, byFile);
        g_hasAsset = true;
        GameVariable<uint8_t>(0x007CEFAC) = 0;
        call(0x005A1D80, CFuncs::CFunc_ReloadAnim, byFile);
        call(0x005A1D80, CFuncs::CFunc_ReloadAnim, byBase);
        GameVariable<uint8_t>(0x007CEFAC) = 1;
        call(0x005A1D80, CFuncs::CFunc_ReloadAnim, byBase);
        call(0x005A1D80, CFuncs::CFunc_ReloadAnim, none);
        // Debounce: no button; components of each kind (0 - 3); the clock's top bit; a release; a time
        call(0x005A1E70, CFuncs::CFunc_Debounce, none);
        for (int state = 0; state < 4; ++state) {
            g_componentCounts[0] = state;
            g_componentCounts[1] = 3 - state;
            g_clock = state & 1 ? 0x80001234u : 0x00031234u;
            Params p; p.Checksum(0, 0x7323E97C + state);
            if (state & 1)
                p.Float(0x906B67BA, 0.3337f * state);
            if (state & 2)
                p.Integer(0x1A4E0EF9, state);
            call(0x005A1E70, CFuncs::CFunc_Debounce, p);
        }
        Params device; device.Integer(0x69E3A1C4, 1);
        call(0x005A2050, CFuncs::CFunc_StartKeyboardHandler, device);
        call(0x005A2080, CFuncs::CFunc_StopKeyboardHandler, none);
        // SpawnSecondControllerCheck: on already; made; out of memory
        GameVariable<uint32_t>(0x007CEBC4) = 0x00AAAA00;
        call(0x005A2100, CFuncs::CFunc_SpawnSecondControllerCheck, none);
        GameVariable<uint32_t>(0x007CEBC4) = 0;
        g_callocFails = false;
        call(0x005A2100, CFuncs::CFunc_SpawnSecondControllerCheck, none);
        {
            auto* task = reinterpret_cast<uint32_t*>(g_task);
            uint32_t code = task[0x18 / 4];
            bool ours = code == 0x0059F820 || code == AddressOf(&CFuncs::SecondControllerCheckTask) ||
                        code == reinterpret_cast<uint32_t>(&CFuncs::SecondControllerCheckTask);
            Record({GameVariable<uint32_t>(0x007CEBC4) == reinterpret_cast<uint32_t>(g_task) ? 1u : 0u, task[0],
                    ours ? 1u : 0u, HashBytes(g_task + 4, 0x14)});
        }
        GameVariable<uint32_t>(0x007CEBC4) = 0;
        g_callocFails = true;
        call(0x005A2100, CFuncs::CFunc_SpawnSecondControllerCheck, none);
        Record({GameVariable<uint32_t>(0x007CEBC4)});
        g_callocFails = false;
        GameVariable<uint32_t>(0x007CEBC4) = 0;

        // the skate module's and game mode's values
        Params stat; stat.Float(0, 7.5f);
        call(0x005A1F80, CFuncs::CFunc_SetStatOverride, stat);
        Record({reinterpret_cast<uint32_t*>(skate)[0x3D0 / 4]});
        call(0x005A1F80, CFuncs::CFunc_SetStatOverride, none);
        Record({reinterpret_cast<uint32_t*>(skate)[0x3D0 / 4]});
        for (uint8_t rails : {uint8_t(0), uint8_t(1), uint8_t(5)}) {
            skate[0x3D4] = rails;
            call(0x005A1FB0, CFuncs::CFunc_ToggleRails, none);
            Record({skate[0x3D4]});
        }
        call(0x005A1FD0, CFuncs::CFunc_GetTimeLimit, none);
        call(0x005A1FF0, CFuncs::CFunc_GetGoalsMode, none);
        call(0x005A2010, CFuncs::CFunc_GetTeamGraffitiMode, none);
        call(0x005A2030, CFuncs::CFunc_GetGameMode, none);
        Params inner; inner.Integer(5, 6);
        Params tester; tester.Checksum(0, 0x7E57).Structure(0x7031F10C, inner.Struct());
        call(0x005A2090, CFuncs::CFunc_SetTesterScript, tester);
        call(0x005A2090, CFuncs::CFunc_SetTesterScript, none);
        call(0x005A20F0, CFuncs::CFunc_KillTesterScript, none);
        call(0x005A21A0, CFuncs::CFunc_AutoRail, tester);
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(more);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    Check(traces[0].size() > 200, "viewer and mode commands: the original reached its calls");
    Compare(traces[0], traces[1], "viewer model, debounce, game mode commands (0x005A1C80-0x005A21C0)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A21C0-0x005A2A00 (game_cfuncs.cpp, net_state_cfuncs.cpp) ----------------------------------------------

Params* g_node;
bool g_exists, g_fileOpens, g_hasHost;
const char* g_trackingText;
uint8_t g_trackingData[0x200];

double __cdecl TestAtof(const char* text) { return std::strtod(text, nullptr); }

void StandInNodeCalls(StandIns& s)
{
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
        return g_node ? reinterpret_cast<uint32_t>(g_node->Struct()) : 0u;
    });
    using AddVectorFn = void (CStruct::*)(uint32_t, float, float, float);
    s.At({0x00477E20, AddressOf(static_cast<AddVectorFn>(&CStruct::AddVector))}, 0x10, [](Regs& r) {
        Record({0x7E20, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x0057C3F0, AddressOf(&Obj::CRailManager::MoveNode)}, 8, [](Regs& r) {
        Record({0xC3F0, r.ecx, r.args[0], HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 16)});
        return 0u;
    });
    simple({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 0xB0E0, 3);
    simple({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0xB110, 1);
    s.At({0x005BA010, AddressOf(&File::Exist)}, 0, [](Regs& r) {
        Record({0xA010, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_exists ? 1u : 0u;
    });
    s.At({0x005B7B00, AddressOf(&Pip::LoadFileInto)}, 0, [](Regs& r) {
        Record({0x7B00, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[2]});
        size_t length = std::strlen(g_trackingText);
        std::memset(g_trackingData, 0, sizeof(g_trackingData));
        std::memcpy(g_trackingData, g_trackingText, length);
        *reinterpret_cast<int*>(r.args[1]) = static_cast<int>(length);
        return reinterpret_cast<uint32_t>(g_trackingData);
    });
    s.Bytes(0x006253C9, Branch(0xE9, 0x006253C9, reinterpret_cast<const void*>(&TestAtof))); // atof
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Record({0xBB60, r.args[0] == reinterpret_cast<uint32_t>(g_trackingData) ? 1u : r.args[0]});
        return 0u;
    });
    // the C runtime's file reading
    s.At({0x006241E8}, 0, [](Regs& r) {
        Record({0x41E8, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return g_fileOpens ? 0x00F11E00u : 0u;
    });
    s.At({0x006240D2}, 0, [](Regs& r) { Record({0x40D2, r.args[0], r.args[1], r.args[2]}); return 0u; });
    s.At({0x00625388}, 0, [](Regs& r) { Record({0x5388, r.args[0]}); return 0x1234u; });
    s.At({0x00624031}, 0, [](Regs& r) { Record({0x4031, r.args[0]}); return 0x00DA7A00u; });
    s.At({0x00623F3E}, 0, [](Regs& r) { Record({0x3F3E, r.args[0], r.args[1], r.args[2], r.args[3]}); return r.args[2]; });
    s.At({0x00623E04}, 0, [](Regs& r) { Record({0x3E04, r.args[0]}); return 0u; });
    s.At({0x0062411B}, 0, [](Regs& r) { Record({0x411B, r.args[0]}); return 0u; });
    s.At({0x00487420}, 8, [](Regs& r) { Record({0x7420, Rel(r.ecx), r.args[0], r.args[1]}); return 0u; });
    s.At({0x00487520, AddressOf(&Gfx::CFaceTexture::SetValid)}, 4, [](Regs& r) { Record({0x7520, Rel(r.ecx), r.args[0]}); return 0u; });
    s.At({0x00564FC0, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4FC0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_fakes->profile);
    });
    simple({0x005732D0, AddressOf(&Gfx::CTODManager::SetSceneColor)}, 4, 0x32D0, 3);
    // joining by address
    simple({0x00536490, AddressOf(&GameNet::Manager::FindServerAt)}, 4, 0x6490, 2);
    s.At({0x005BEAAE}, 4, [](Regs& r) {
        const char* address = reinterpret_cast<const char*>(r.args[0]);
        Record({0xEAAE, Hash(address)});
        return address[0] >= '0' && address[0] <= '9' ? 0x0700000Au : 0xFFFFFFFFu;
    });
    s.At({0x005BEABA}, 4, [](Regs& r) {
        Record({0xEABA, Hash(reinterpret_cast<const char*>(r.args[0]))});
        static uint8_t address[4] = {192, 168, 1, 250};
        static uint8_t* list[2] = {address, nullptr};
        static uint32_t host[4] = {0, 0, 0x00040002, reinterpret_cast<uint32_t>(list)};
        return g_hasHost ? reinterpret_cast<uint32_t>(host) : 0u;
    });
    simple({0x00547320, AddressOf(&GameNet::Manager::ShowFoundServers)}, 0, 0x7320, 1);
}

void TestNodeTrackingCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> more = {{0x007D1800, 4, {}}, {0x007CE490, 4, {}}, {0x0067EFB8, 4, {}}};
    SaveAll(saved);
    SaveAll(more);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInNodeCalls(*standIns);
    g_slot1 = standIns->Thunk(0, [](Regs& r) { Record({0x5107, Rel(r.ecx)}); return 0u; });
    g_slot2 = standIns->Thunk(4, [](Regs& r) { Record({0x5207, Rel(r.ecx), r.args[0]}); return 0u; });
    static uint8_t skate[0x400];
    static uint8_t gameNet[0x10];
    static uint32_t memoryManager[0x5E8 / 4];
    for (uint32_t i = 0; i < 0x5E8 / 4; ++i)
        memoryManager[i] = 0x00E30000 + i;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        std::memset(skate, 0, sizeof(skate));
        reinterpret_cast<uint32_t*>(skate)[0x24 / 4] = 0x00A11000; // the rail manager
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007D1800) = memoryManager;
        GameVariable<void*>(0x007CCE14) = gameNet;
        GameVariable<uint32_t>(0x007CE490) = 0x00700490;
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;

        // MoveNode: no name; no node; no position (either name); the node has none; each class; an object or not
        static Script::CVector to = {0, 1.5f, -2.0f, 300.25f};
        static Script::CVector old = {0, 9.0f, 9.0f, 9.0f};
        call(0x005A21C0, CFuncs::CFunc_MoveNode, none);
        Params named; named.Checksum(0xA1DC81F9, 0x40DE);
        g_node = nullptr;
        call(0x005A21C0, CFuncs::CFunc_MoveNode, named);
        static Params node;
        node = Params();
        g_node = &node;
        call(0x005A21C0, CFuncs::CFunc_MoveNode, named);
        Params move; move.Checksum(0xA1DC81F9, 0x40DE).Vector(0x7F261953, &to);
        call(0x005A21C0, CFuncs::CFunc_MoveNode, move);
        Params move2; move2.Checksum(0xA1DC81F9, 0x40DF).Vector(0xB9D31B0A, &to);
        const uint32_t classes[] = {0x30C19600, 0x8E6B02AD, 0xA0DFAC98, 0x061A741E, 0x08470F2E, 0xE47F1B79,
                                    0xB7B3BD86, 0xBF4D3536, 0xEF59C100, 0x12345678, 0};
        for (int i = 0; i < 11; ++i) {
            node = Params();
            node.Vector(i & 1 ? 0xB9D31B0A : 0x7F261953, &old);
            if (classes[i])
                node.Checksum(0x12B4E660, classes[i]);
            g_hasObject = i % 3 != 0;
            call(0x005A21C0, CFuncs::CFunc_MoveNode, i & 2 ? move2 : move);
            Record({HashBytes(g_fakes->object + 0x4C, 16)});
        }
        // SetActiveCamera
        g_hasObject = true;
        g_hasComponent = true;
        Params camera; camera.Checksum(0x40C698AF, 0xCA3).Integer(0x9FD29151, 1).Flag(0x10C1C887);
        call(0x005A2370, CFuncs::CFunc_SetActiveCamera, camera);
        g_hasObject = false;
        call(0x005A2370, CFuncs::CFunc_SetActiveCamera, none);
        // ShowTracking: no file; a drive name; each flag set; none (all)
        // (right after a line's sixth number the parser stands on the next line's first character: a line straight
        // after another is missed - kept - so most lines here end with a space)
        g_trackingText = "junk\nTracking1 1.5 -2 3.25 4 5 6 \nTracking2 1 2 3 4 5 6 \nTracking2 1 2 3 4 5 6 \nTrackin"
                         "\nTracking3 -1.5 2 3 0 0 0 \nTracking4 9 8 7 6 5 4 \nTracking4 9 8 7 6 5 4 \nTracking4 9 8 7 6 5 4 "
                         "\nTracking7 1 1 1 2 2 2\nTracking1 0 0 0 1 1 1 \nTracking3 1 1 1 2 2 2";
        g_exists = false;
        Params missing; missing.String(0, "c:tracking.txt");
        call(0x005A23E0, CFuncs::CFunc_ShowTracking, missing);
        g_exists = true;
        call(0x005A23E0, CFuncs::CFunc_ShowTracking, missing);
        for (uint32_t flag : {0xB426BF5Au, 0x14240297u, 0x969D3AF6u, 0xB0FE7369u}) {
            Params p; p.String(0, "tracking.txt").Flag(flag);
            call(0x005A23E0, CFuncs::CFunc_ShowTracking, p);
        }
        // ChooseFaceForFacemap: the file can't be opened; read
        g_hasViewerObject = true;
        Params face; face.String(CrcOf("string"), "face01.png");
        g_fileOpens = false;
        call(0x005A2790, CFuncs::CFunc_ChooseFaceForFacemap, face);
        g_fileOpens = true;
        call(0x005A2790, CFuncs::CFunc_ChooseFaceForFacemap, face);
        for (bool custom : {false, true}) {
            g_hasViewerObject = custom;
            Params p; p.Integer(0x67E6859A, 2);
            call(0x005A29A0, CFuncs::CFunc_SkaterIsCustom, p);
        }
        call(0x005A29E0, CFuncs::CFunc_SetSceneColor, face);
        // joining
        for (uint32_t server : {0xFFFFFFFFu, 3u}) {
            GameVariable<uint32_t>(0x0067EFB8) = server;
            GameVariable<uint8_t>(0x007CCA81) = 0;
            call(0x005A28A0, CFuncs::CFunc_TryJoinInternetGame, none);
            Record({GameVariable<uint8_t>(0x007CCA81)});
        }
        for (int state = 0; state < 3; ++state) {
            GameVariable<uint8_t>(0x007CCA81) = 0;
            g_hasHost = state == 2;
            Params p; p.String(CrcOf("string"), state == 0 ? "10.0.0.7" : "server.example");
            call(0x005A28D0, CFuncs::CFunc_TryJoinServerIP, p);
            Record({GameVariable<uint8_t>(0x007CCA81)});
        }
        call(0x005A2960, CFuncs::CFunc_TryJoinServerIPCancel, none);
        Record({GameVariable<uint8_t>(0x007CCA81)});
        call(0x005A2990, CFuncs::CFunc_ForceServerListRefresh, none);
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(more);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    g_node = nullptr;
    Check(traces[0].size() > 200, "node and tracking commands: the original reached its calls");
    Compare(traces[0], traces[1], "move node, tracking, face, join commands (0x005A21C0-0x005A2A00)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A2A00-0x005A34E0 (node_object_cfuncs.cpp) --------------------------------------------------------------

// Stand-in scenes: a list of two (the third node ends it), each with a sector table of three sectors.
struct FakeSector {
    uint32_t unknown0;
    uint32_t name;   // +4
    uint32_t flags;  // +8
    uint32_t active;
    int vertexCount;
    float base;
};
struct FakeScene {
    uint8_t unknown[0xD0];
    Lst::HashTable* sectors; // +0xD0
};
struct FakeSceneNode {
    uint32_t unknown0;
    void* scene;
    uint32_t unknown8;
    FakeSceneNode* next;
};
FakeSector g_sectors[2][3];
FakeScene g_scenes[2];
Lst::HashTable::Item g_sectorItems[2][2];
uint8_t g_tables[2][0x18];
FakeSceneNode g_sceneNodes[3];
int g_sceneCount;
bool g_skipNode;
uint32_t g_cloneMissing;
Mth::Vector g_vertexBlock[16];

void SetUpScenes()
{
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < 3; ++i) {
            FakeSector& sector = g_sectors[s][i];
            sector = FakeSector{};
            sector.name = 0x5EC0 + 0x10 * s + i;
            sector.flags = 0x1100 + i;
            sector.active = (s + i) % 3 != 1;
            sector.vertexCount = i == 2 ? 0 : 2 + i + s;
            sector.base = 10.0f * (s + 1) + i;
        }
        // two buckets: the first holds sector 0 with sector 1 chained, the second sector 2
        g_sectorItems[s][0] = {g_sectors[s][1].name, &g_sectors[s][1], nullptr};
        auto* table = reinterpret_cast<Lst::HashTable*>(g_tables[s]);
        std::memset(g_tables[s], 0, sizeof(g_tables[s]));
        static Lst::HashTable::Item buckets[2][2];
        buckets[s][0] = {g_sectors[s][0].name, &g_sectors[s][0], &g_sectorItems[s][0]};
        buckets[s][1] = {g_sectors[s][2].name, &g_sectors[s][2], nullptr};
        table->m_bits = 1;
        table->m_buckets = buckets[s];
        g_scenes[s].sectors = table;
        g_sceneNodes[s] = {0, &g_scenes[s], 0, &g_sceneNodes[s + 1]};
    }
    g_sceneNodes[2] = {0, reinterpret_cast<void*>(-1), 0, nullptr};
}

void StandInNodeObjectCalls(StandIns& s)
{
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    s.At({0x0048C7F0}, 0, [](Regs&) {
        Record({0xC7F0});
        if (!g_sceneCount)
            return 0u;
        g_sceneNodes[g_sceneCount].scene = reinterpret_cast<void*>(-1);
        g_sceneNodes[g_sceneCount].next = nullptr;
        return reinterpret_cast<uint32_t>(&g_sceneNodes[0]);
    });
    s.At({0x0049E9E0}, 0, [](Regs& r) { Record({0xE9E0, reinterpret_cast<FakeSector*>(r.ecx)->name}); return reinterpret_cast<FakeSector*>(r.ecx)->active; });
    s.At({0x004A2BA0}, 0, [](Regs& r) { Record({0x2BA0, reinterpret_cast<FakeSector*>(r.ecx)->name}); return r.ecx; });
    s.At({0x0048DF70}, 0, [](Regs& r) { Record({0xDF70}); return static_cast<uint32_t>(reinterpret_cast<FakeSector*>(r.ecx)->vertexCount); });
    s.At({0x0048DF80}, 4, [](Regs& r) {
        auto* sector = reinterpret_cast<FakeSector*>(r.ecx);
        Record({0xDF80, sector->name, r.args[0] == reinterpret_cast<uint32_t>(g_vertexBlock) ? 1u : r.args[0]});
        auto* out = reinterpret_cast<Mth::Vector*>(r.args[0]);
        for (int i = 0; i < sector->vertexCount; ++i)
            out[i] = {sector->base * (i + 1), -sector->base + 3.5f * i, sector->base * 0.25f - i, 1.0f};
        return 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 0xC, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return reinterpret_cast<uint32_t>(g_vertexBlock);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, r.args[0] == reinterpret_cast<uint32_t>(g_vertexBlock) ? 1u : r.args[0]});
        return 0u;
    });
    simple({0x004E1950, AddressOf(&Mem::EnsureManager)}, 0, 0x1950, 0);
    s.At({0x005BFA8C}, 0xC, [](Regs& r) { // D3DXMatrixMultiply (row vectors: out = a * b)
        auto* a = reinterpret_cast<const float*>(r.args[1]);
        auto* b = reinterpret_cast<const float*>(r.args[2]);
        float out[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
        Record({0xFA8C, HashBytes(reinterpret_cast<const uint8_t*>(b), 64)});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    // the node objects
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
        return g_node ? reinterpret_cast<uint32_t>(g_node->Struct()) : 0u;
    });
    s.At({0x00565BB0, AddressOf(Mdl::Skate::ShouldBeAbsent)}, 4, [](Regs& r) { Record({0x5BB0, Rel(r.args[0])}); return g_skipNode ? 1u : 0u; });
    simple({0x0057B4E0, AddressOf(&Obj::CreatePed)}, 0, 0xB4E0, 2);
    s.At({0x0057D7C0, AddressOf(&Obj::CRailManager::SetActive)}, 0xC, [](Regs& r) {
        Record({0xD7C0, r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF});
        return 0u;
    });
    s.At({0x0046ACB0, AddressOf(&Obj::Proxim_SetActive)}, 0, [](Regs& r) { Record({0xACB0, r.args[0], r.args[1]}); return 0u; });
    simple({0x00468B00, AddressOf(&Obj::CreateParticleObject)}, 0, 0x8B00, 2);
    s.At({0x00468340, AddressOf(&Obj::Emitter_SetActive)}, 0, [](Regs& r) { Record({0x8340, r.args[0], r.args[1]}); return 0u; });
    s.At({0x00571140, AddressOf(&Gfx::CTODManager::GetLevelColor)}, 4, [](Regs& r) {
        Record({0x1140, r.ecx});
        *reinterpret_cast<uint32_t*>(r.args[0]) = 0x80C0FF40;
        return 0u;
    });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    simple({0x00468EC0, AddressOf(&Obj::CreateNodeObject)}, 0, 0x8EC0, 2);
    simple({0x00578170, AddressOf(&Obj::CreateCar)}, 0, 0x8170, 2);
    simple({0x00468EB0, AddressOf(&Obj::CreateGameObjectFromNode)}, 0, 0x8EB0, 2, 0x0B1EC7);
    s.At({0x0049CB00}, 4, [](Regs& r) {
        Record({0xCB00, Rel(r.ecx), r.args[0]});
        if (r.args[0] == g_cloneMissing)
            return 0u;
        for (int s = 0; s < 2; ++s)
            if (r.ecx == reinterpret_cast<uint32_t>(&g_scenes[s]))
                for (auto& sector : g_sectors[s])
                    if (sector.name == r.args[0])
                        return reinterpret_cast<uint32_t>(&sector);
        return 0u;
    });
    s.At({0x0046D110, AddressOf(&Script::FindChecksumNameOrUnknown)}, 0, [](Regs& r) {
        Record({0xD110, r.args[0]});
        return reinterpret_cast<uint32_t>("<name>");
    });
    s.At({0x0049E680}, 4, [](Regs& r) { Record({0xE680, reinterpret_cast<FakeSector*>(r.ecx)->name, r.args[0]}); return 0u; });
    s.At({0x0049E9B0}, 4, [](Regs& r) { Record({0xE9B0, reinterpret_cast<FakeSector*>(r.ecx)->name, r.args[0] & 0xFF}); return 0u; });
    s.At({0x0049EA30}, 4, [](Regs& r) { Record({0xEA30, reinterpret_cast<FakeSector*>(r.ecx)->name, r.args[0] & 0xFF}); return 0u; });
    s.At({0x0048BF90}, 0, [](Regs& r) { Record({0xBF90, r.args[0], r.args[1]}); return 0u; });
    s.At({0x0049D510}, 0x14, [](Regs& r) {
        Record({0xD510, Rel(r.ecx), reinterpret_cast<FakeSector*>(r.args[0])->name, Rel(r.args[1]), r.args[2], r.args[3], r.args[4]});
        g_cloneMissing = 0;
        return 0u;
    });
    s.At({0x0049E700}, 4, [](Regs& r) {
        Record({0xE700, reinterpret_cast<FakeSector*>(r.ecx)->name, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 64)});
        return 0u;
    });
    s.At({0x0049E7C0}, 4, [](Regs& r) {
        Record({0xE7C0, reinterpret_cast<FakeSector*>(r.ecx)->name, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 16)});
        return 0u;
    });
    s.At({0x0049E6C0}, 4, [](Regs& r) {
        Record({0xE6C0, reinterpret_cast<FakeSector*>(r.ecx)->name, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 16)});
        return 0u;
    });
    simple({0x0048FCA0}, 0, 0xFCA0, 2);
    s.At({0x00461CC0, AddressOf(&Obj::CBaseManager::ForEach)}, 8, [](Regs& r) {
        Record({0x1CC0, r.ecx, *reinterpret_cast<uint32_t*>(r.args[1])});
        for (auto& object : g_fakes->flagObjects)
            reinterpret_cast<Obj::ObjectCallback>(r.args[0])(reinterpret_cast<Obj::CObject*>(object),
                                                              reinterpret_cast<void*>(r.args[1]));
        return 0u;
    });
    s.At({0x00460930, AddressOf(&Obj::CObject::DestroyIfUnlocked)}, 0, [](Regs& r) { Record({0x0930, Rel(r.ecx)}); return 0u; });
}

void TestNodeObjectCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> more = {{0x007CE4A4, 4, {}}, {0x007CC979, 1, {}}, {0x007D1800, 4, {}}, {0x007CE490, 4, {}}};
    SaveAll(saved);
    SaveAll(more);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInNodeObjectCalls(*standIns);
    static uint8_t skate[0x400];
    static uint8_t tod[0x340];

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        SetUpScenes();
        std::memset(skate, 0, sizeof(skate));
        reinterpret_cast<uint32_t*>(skate)[0x24 / 4] = 0x00A11000; // the rail manager
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<uint32_t>(0x007D1800) = 0x00701800;
        GameVariable<void*>(0x007CE490) = tod;
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;
        auto recordCamera = [&] { Record({HashBytes(g_fakes->object + 0x4C, 0x60)}); };

        // CenterCamera: no viewer module; no scenes; two scenes (all sectors / one name); no camera object; a
        // memory manager not set up yet; a small box (the 1000 minimum)
        GameVariable<uint32_t>(0x007CE4A4) = 0;
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, none);
        GameVariable<uint32_t>(0x007CE4A4) = 0x00A1E000;
        GameVariable<uint8_t>(0x007CC979) = 1;
        g_hasObject = true;
        g_sceneCount = 0;
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, none);
        recordCamera();
        g_sceneCount = 2;
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, none);
        recordCamera();
        g_sectors[1][1].base = 700.0f; // (a box over the 1000 minimum)
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, none);
        recordCamera();
        Params angled;
        angled.Float(CrcOf("scale"), 1.75f).Float(CrcOf("x"), 30.0f).Float(CrcOf("y"), -20.0f).Float(CrcOf("z"), 12.5f)
            .Checksum(CrcOf("id"), 0x5ED1);
        GameVariable<uint8_t>(0x007CC979) = 0;
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, angled);
        recordCamera();
        g_sceneCount = 1;
        for (auto& sector : g_sectors[0])
            sector.base = 0.5f;
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, angled);
        Params all; all.Float(CrcOf("scale"), 0.5f);
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, all);
        recordCamera();
        g_hasObject = false;
        call(0x005A2D60, CFuncs::CFunc_CenterCamera, none);
        g_sceneCount = 2;
        SetUpScenes();

        // CreateFromNode, by each class; a node the skate module skips; a trigger
        using NodeFn = bool(__cdecl*)(CStruct*);
        auto create = [&](Params& node) {
            bool result = original ? Original<NodeFn>(0x005A2E50)(node.Struct()) : CFuncs::CreateFromNode(node.Struct());
            Record({0x2E50, result ? 1u : 0u});
        };
        const uint32_t classes[] = {0xA0DFAC98, 0x061A741E, 0x30C19600, 0x8E6B02AD, 0x08470F2E, 0x9E7D469E,
                                    0xD64DC7C2, 0xB7B3BD86, 0xE47F1B79, 0xEF59C100, 0xE594F0A2, 0x12345678};
        for (int i = 0; i < 12; ++i) {
            Params node;
            node.Checksum(0xA1DC81F9, 0x40DE + i).Checksum(0x12B4E660, classes[i]);
            if (i == 3)
                node.Flag(CrcOf("FLAG_WHOLE_RAIL"));
            tod[0x33C] = static_cast<uint8_t>(i & 1);
            create(node);
            if (i == 7) { // the level object again, with the time of day's colour
                tod[0x33C] = 1;
                create(node);
            }
        }
        g_skipNode = true;
        Params skipped; skipped.Checksum(0x12B4E660, 0xA0DFAC98);
        create(skipped);
        g_skipNode = false;
        // level geometry: no name; a sector in the second scene; one in none; with the occluder flag; copies (made
        // or there already; with angles, scale)
        Params noName; noName.Checksum(0x12B4E660, 0xBF4D3536);
        create(noName);
        Params geometry; geometry.Checksum(0x12B4E660, 0xBF4D3536).Checksum(0xA1DC81F9, 0x5ED2).Flag(0x4E549FE1);
        create(geometry);
        Params lost; lost.Checksum(0x12B4E660, 0xBF4D3536).Checksum(0xA1DC81F9, 0x5EFF);
        create(lost);
        Params plain; plain.Checksum(0x12B4E660, 0xBF4D3536).Checksum(0xA1DC81F9, 0x5EC1);
        create(plain);
        static Script::CVector angles = {0, 0.5f, 1.25f, -0.75f};
        static Script::CVector scaled = {0, 2.0f, 3.0f, 4.0f};
        static Script::CVector placed = {0, 100.0f, -50.0f, 25.0f};
        Params copy;
        copy.Checksum(0x12B4E660, 0xBF4D3536).Checksum(0xA1DC81F9, 0x5ED0).Checksum(0xEDC063B9, 0x5ED1)
            .Vector(CrcOf("angles"), &angles).Vector(CrcOf("scale"), &scaled).Vector(CrcOf("pos"), &placed);
        g_cloneMissing = 0x5ED0;
        create(copy);
        Record({g_sectors[1][1].flags, g_sectors[1][0].flags});
        Params copyThere; copyThere.Checksum(0x12B4E660, 0xBF4D3536).Checksum(0xA1DC81F9, 0x5ED2).Checksum(0xEDC063B9, 0x5ED0);
        create(copyThere);
        Record({g_sectors[1][0].flags, g_sectors[1][2].flags});
        // CreateFromStructure, CreateFromNodeName
        Params structure; structure.Checksum(0x12B4E660, 0xE47F1B79).Checksum(0xA1DC81F9, 0x7E5);
        call(0x005A34D0, CFuncs::CFunc_CreateFromStructure, structure);
        using NameFn = bool(__cdecl*)(uint32_t);
        static Params named;
        named = Params();
        named.Checksum(0x12B4E660, 0x9E7D469E).Checksum(0xA1DC81F9, 0x9A3);
        g_node = &named;
        Record({(original ? Original<NameFn>(0x005A32A0)(0x9A3) : CFuncs::CreateFromNodeName(0x9A3)) ? 1u : 0u});

        // KillFromNodeName: no node; each class; level geometry (no name, found with and without the flag, lost)
        using KillFn = bool(__cdecl*)(uint32_t, Script::CScript*);
        auto kill = [&](uint32_t name) {
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<KillFn>(0x005A32C0)(name, script) : CFuncs::KillFromNodeName(name, script);
            Record({0x32C0, result ? 1u : 0u});
        };
        auto* objects = g_fakes->flagObjects;
        for (int i = 0; i < 3; ++i)
            reinterpret_cast<uint32_t*>(objects[i])[0x14 / 4] = 0x0B10 + i;
        reinterpret_cast<void**>(g_fakes->script)[0xBC / 4] = g_fakes->object;
        g_node = nullptr;
        kill(0x0B11);
        const uint32_t killClasses[] = {0xA0DFAC98, 0x5B8AB877, 0x061A741E, 0xB7B3BD86, 0xE47F1B79, 0xEF59C100,
                                        0x9E7D469E, 0x30C19600, 0x8E6B02AD, 0x08470F2E, 0xA0E52802, 0xD64DC7C2,
                                        0xE594F0A2};
        static Params killNode;
        g_node = &killNode;
        for (int i = 0; i < 13; ++i) {
            killNode = Params();
            killNode.Checksum(0x12B4E660, killClasses[i]);
            if (i == 8)
                killNode.Flag(CrcOf("FLAG_WHOLE_RAIL"));
            kill(0x0B10 + i % 3);
        }
        killNode = Params();
        killNode.Checksum(0x12B4E660, 0xBF4D3536);
        kill(0x0B10);
        for (uint32_t sectorName : {0x5ED1u, 0x5EC2u, 0x5EFFu}) {
            killNode = Params();
            killNode.Checksum(0x12B4E660, 0xBF4D3536).Checksum(0xA1DC81F9, sectorName);
            if (sectorName == 0x5EC2)
                killNode.Flag(0x4E549FE1);
            kill(0x0B10);
        }
        g_sceneCount = 0;
        kill(0x0B10);
        g_node = nullptr;
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(more);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    Check(traces[0].size() > 300, "node object commands: the original reached its calls");
    Compare(traces[0], traces[1], "camera on the scenes, node objects (0x005A2A00-0x005A34E0)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A34E0-0x005A3AF7 (node_object_cfuncs.cpp) --------------------------------------------------------------

Script::CArray g_nodeArray;
bool g_multiplayer, g_allTricks, g_noChecklist;
uint32_t g_skipMask; // nodes (by their name's low bits) the skate module skips

void StandInParseCalls(StandIns& s)
{
    auto simple = [&s](std::initializer_list<uint32_t> at, uint16_t pop, uint32_t tag, int mode, uint32_t result = 0) {
        s.At(at, pop, [tag, mode, result](Regs& r) {
            Record({tag, mode & 1 ? Rel(r.ecx) : 0u, mode & 2 ? Rel(r.args[0]) : mode & 4 ? r.args[0] & 0xFF : 0u});
            return result;
        });
    };
    s.At({0x0045C220, AddressOf(&SkateScript::GetNodeOwnerValue58)}, 0, [](Regs&) { Record({0xC220}); return reinterpret_cast<uint32_t>(&g_nodeArray); });
    simple({0x0046AB30, AddressOf(&Obj::Proxim_Init)}, 0, 0xAB30, 0);
    simple({0x004683B0, AddressOf(&Obj::Emitter_Init)}, 0, 0x83B0, 0);
    simple({0x0057C330, AddressOf(&Obj::CRailManager::Cleanup)}, 0, 0xC330, 1);
    simple({0x005889F0, AddressOf(&Obj::CTrickObjectManager::RemoveAllClusters)}, 0, 0x89F0, 1, 1);
    simple({0x0048EF00}, 0, 0xEF00, 0);
    simple({0x0048FDA0}, 0, 0xFDA0, 0);
    simple({0x0057E960, AddressOf(&Obj::CRailManager::AddFromNodeArray)}, 4, 0xE960, 3);
    s.At({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x0048C510}, 0, [](Regs& r) {
        Record({0xC510, r.args[0]});
        for (auto& scene : g_sectors)
            for (auto& sector : scene)
                if (sector.name == r.args[0])
                    return reinterpret_cast<uint32_t>(&sector);
        return 0u;
    });
    s.At({0x0049EA60}, 4, [](Regs& r) { Record({0xEA60, reinterpret_cast<FakeSector*>(r.ecx)->name, r.args[0]}); return 0u; });
    s.At({0x00565BB0, AddressOf(Mdl::Skate::ShouldBeAbsent)}, 4, [](Regs& r) {
        uint32_t name = 0;
        reinterpret_cast<CStruct*>(r.args[0])->GetChecksum(0xA1DC81F9u, &name, 0);
        Record({0x5BB0, name});
        return (g_skipMask >> (name & 31)) & 1;
    });
    s.At({0x005651F0, AddressOf(Mdl::Skate::IsMultiplayer)}, 0, [](Regs&) { Record({0x51F0}); return g_multiplayer ? 1u : 0u; });
    using IntFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<IntFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return r.args[0] == 0xB675F8F6 ? (g_allTricks ? 1u : 0u) : (g_noChecklist ? 1u : 0u);
    });
    s.At({0x005896D0, AddressOf(&Obj::CTrickObjectManager::AddCluster)}, 4, [](Regs& r) { Record({0x96D0, r.ecx, r.args[0]}); return 1u; });
    s.At({0x00589760, AddressOf(static_cast<bool (Obj::CTrickObjectManager::*)(uint32_t, uint32_t)>(&Obj::CTrickObjectManager::AddObject))}, 8,
         [](Regs& r) { Record({0x9760, r.ecx, r.args[0], r.args[1]}); return 1u; });
    s.At({0x0046B1F0, AddressOf(&Obj::Proxim_AddNode)}, 0, [](Regs& r) { Record({0xB1F0, r.args[0], r.args[1] & 0xFF}); return 0u; });
    s.At({0x00468450, AddressOf(&Obj::Emitter_AddNode)}, 0, [](Regs& r) { Record({0x8450, r.args[0], r.args[1] & 0xFF}); return 0u; });
    s.At({0x005A2E50, AddressOf(&CFuncs::CreateFromNode)}, 0, [](Regs& r) { Record({0x2E50, Rel(r.args[0])}); return 1u; });
    simple({0x0057D7A0, AddressOf(&Obj::CRailManager::RemoveOverlapsIfNotPark)}, 0, 0xD7A0, 1);
    simple({0x005656C0, AddressOf(&Mdl::Skate::GetGapChecklist)}, 0, 0x56C0, 1, 0x00CEC000);
    simple({0x00578AC0, AddressOf(&Game::CGapChecklist::Build)}, 0, 0x8AC0, 1);
    s.At({0x0049E1B0}, 4, [](Regs& r) { Record({0xE1B0, Rel(r.ecx), r.args[0]}); return 0u; });
    simple({0x00490570}, 0, 0x0570, 2);
    // ReLoadNodeArray
    simple({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 0xB0E0, 3);
    simple({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0xB110, 1);
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        static Script::CComponent component;
        component = Script::CComponent{};
        component.typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_STRING << 1);
        component.name = r.args[0];
        component.raw = r.args[1];
        reinterpret_cast<CStruct*>(r.ecx)->m_head = &component;
        return 0u;
    });
    s.At({0x00477130, 0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7130, reinterpret_cast<CStruct*>(r.ecx)->m_head ? 1u : 0u});
        reinterpret_cast<CStruct*>(r.ecx)->m_head = nullptr;
        return 0u;
    });
    s.At({0x0059FB30, AddressOf(&CFuncs::CFunc_NodeArrayManLoad)}, 0, [](Regs& r) { Record({0xFB30, Rel(r.args[1])}); return 1u; });
    s.At({0x005A5960}, 0, [](Regs& r) { Record({0x5960, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]}); return 0u; });
    simple({0x0045C210, AddressOf(&SkateScript::GetLastNodeArray)}, 0, 0xC210, 0);
    simple({0x0045D150, AddressOf(&SkateScript::CNodeManager::GetLastNodeArrayName)}, 0, 0xD150, 1, 0x1A57);
    simple({0x0045D1C0, AddressOf(&SkateScript::CNodeManager::RemoveNodeArray)}, 4, 0xD1C0, 3, 1);
    s.At({0x0045D520, AddressOf(&SkateScript::CNodeManager::AddNodeArray)}, 8, [](Regs& r) {
        Record({0xD520, r.ecx, r.args[0], r.args[1] & 0xFF});
        return 1u;
    });
    s.At({0x0045D670, AddressOf(&SkateScript::CNodeManager::RemoveNodeArrayOfFile)}, 4, [](Regs& r) {
        Record({0xD670, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 1u;
    });
    s.At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) { Record({0x7B90, r.ecx, r.args[0]}); return 0x00A55E70u; });
    simple({0x00407B20, AddressOf(&Ass::CAssMan::UnloadAsset)}, 4, 0x7B20, 3);
}

void TestParseNodeArrayCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> more = {{0x007D1800, 4, {}}, {0x007CE5D8, 4, {}}, {0x007CE500, 4, {}}, {0x00684FA8, 1, {}}};
    SaveAll(saved);
    SaveAll(more);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInNodeObjectCalls(*standIns);
    StandInParseCalls(*standIns);
    static uint8_t skate[0x400];
    static uint8_t parkEditor[0x80];
    static uint32_t memoryManager[0x5E8 / 4];
    for (uint32_t i = 0; i < 0x5E8 / 4; ++i)
        memoryManager[i] = 0x00E30000 + i;
    // the node array: each class, with and without the flags that matter
    static Params nodes[16];
    static CStruct* elements[16];
    const uint32_t kName = 0xA1DC81F9, kClass = 0x12B4E660;
    const uint32_t kCreated = 0x7C2552B9, kOccluder = 0x4E549FE1;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        SetUpScenes();
        g_sceneCount = 2;
        std::memset(skate, 0, sizeof(skate));
        reinterpret_cast<uint32_t*>(skate)[0x24 / 4] = 0x00A11000;
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007D1800) = memoryManager;
        GameVariable<void*>(0x007CE5D8) = parkEditor;
        GameVariable<uint32_t>(0x007CE500) = 0x007CE5FF;
        for (int i = 0; i < 16; ++i)
            nodes[i] = Params();
        nodes[0].Checksum(kName, 0x401).Checksum(kClass, 0x8E6B02AD).Integer(0x9D2D0915, 1);
        nodes[1].Checksum(kName, 0x5EC0).Checksum(kClass, 0xB7B3BD86).Checksum(0x7E4813A7, 0x7E1);
        nodes[2].Checksum(kName, 0x5EC1).Checksum(kClass, 0xB7B3BD86);
        nodes[3].Checksum(kName, 0x5999).Checksum(kClass, 0xB7B3BD86);
        nodes[4].Checksum(kName, 0x5ED0).Checksum(kClass, 0xBF4D3536).Flag(kCreated).Flag(0x1645B830);
        nodes[5].Checksum(kName, 0x5ED1).Checksum(kClass, 0xBF4D3536).Flag(kOccluder).Checksum(0x7E4813A7, 0x7E2);
        nodes[6].Checksum(kName, 0x5ED2).Checksum(kClass, 0xBF4D3536).Flag(kOccluder).Flag(kCreated).Checksum(0x1A3A966B, 0xC105);
        nodes[7].Checksum(kClass, 0xBF4D3536);
        nodes[8].Checksum(kName, 0x5EFF).Checksum(kClass, 0xBF4D3536);
        nodes[9].Checksum(kName, 0x5EFE).Checksum(kClass, 0xBF4D3536).Checksum(0xEDC063B9, 0x5ED0);
        nodes[10].Checksum(kName, 0x40A).Checksum(kClass, 0x08470F2E).Flag(kCreated);
        nodes[11].Checksum(kName, 0x40B).Checksum(kClass, 0xD64DC7C2);
        nodes[12].Checksum(kName, 0x40C).Checksum(kClass, 0xA0E52802).Flag(kCreated);
        nodes[13].Checksum(kName, 0x40D).Checksum(kClass, 0xEF59C100).Flag(kCreated);
        nodes[14].Checksum(kName, 0x5EC2).Checksum(kClass, 0xBF4D3536).Flag(kCreated);
        nodes[15].Checksum(kName, 0x40F).Checksum(kClass, 0x08470F2E);
        for (int i = 0; i < 16; ++i)
            elements[i] = nodes[i].Struct();
        g_nodeArray.m_type = Script::ESYMBOLTYPE_STRUCTURE;
        g_nodeArray.m_size = 16;
        g_nodeArray.m_elements = reinterpret_cast<uint32_t*>(elements);
        auto call = [&](uint32_t address, CFunc ours, Params& params, bool withParams = true) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            CStruct* p = withParams ? params.Struct() : nullptr;
            bool result = original ? Original<CFunc>(address)(p, script) : ours(p, script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;
        Params noCleanup; noCleanup.Flag(CrcOf("no_cleanup"));
        // the park editor off / on; the skate module skipping some; multiplayer; every node a trick object; no checklist
        for (int state = 0; state < 5; ++state) {
            *reinterpret_cast<uint32_t*>(parkEditor + 0x40) = state == 1 ? 0x12345678u : 0xD443A2BCu;
            g_skipMask = state == 2 ? 0x00004005u : 0;
            g_multiplayer = state == 3;
            g_allTricks = state == 4;
            g_noChecklist = state == 2;
            GameVariable<uint8_t>(0x00684FA8) = static_cast<uint8_t>(state == 3);
            call(0x005A34E0, CFuncs::CFunc_ParseNodeArray, state == 1 ? noCleanup : none, state != 4);
            for (auto& scene : g_sectors)
                for (auto& sector : scene)
                    Record({HashBytes(reinterpret_cast<const uint8_t*>(&sector), sizeof(sector))});
        }
        GameVariable<uint8_t>(0x00684FA8) = 0;
        // ReLoadNodeArray
        std::memcpy(&GameVariable<char>(0x007CE6D8), "levels\\nj\\nj.qb", 16);
        call(0x005A39F0, CFuncs::CFunc_ReLoadNodeArray, none);
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(more);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    Check(traces[0].size() > 400, "node array commands: the original reached its calls");
    Compare(traces[0], traces[1], "parse / reload node array (0x005A34E0-0x005A3AF7)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A3B80-0x005A4170, 0x005A4DA0-0x005A5360 (node_object_cfuncs.cpp) -------------------------------------

bool g_netMode, g_railActive;
uint32_t g_prefixed[3];
uint16_t g_prefixedCount;
uint8_t g_geometry[0x10];
uint32_t g_geometryVtable[8];

void StandInNodeCommandCalls(StandIns& s)
{
    s.At({0x0049E690}, 0, [](Regs& r) {
        Record({0xE690, reinterpret_cast<FakeSector*>(r.ecx)->name});
        return 0x00F0u | reinterpret_cast<FakeSector*>(r.ecx)->name & 3;
    });
    s.At({0x0057D840, AddressOf(&Obj::CRailManager::IsActive)}, 4, [](Regs& r) {
        Record({0xD840, r.ecx, r.args[0]});
        return g_railActive ? 1u : 0u;
    });
    s.At({0x00420700, AddressOf(&Obj::CModelComponent::SetColor)}, 4, [](Regs& r) { Record({0x0700, Rel(r.ecx), r.args[0]}); return 0u; });
    s.At({0x004A1090}, 4, [](Regs& r) { Record({0x1090, reinterpret_cast<FakeSector*>(r.ecx)->name, r.args[0]}); return 0u; });
    s.At({0x0044E4E0, AddressOf(&GameNet::Manager::InNetMode)}, 0, [](Regs&) { Record({0xE4E0}); return g_netMode ? 1u : 0u; });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs&) { Record({0xE4F0}); return g_inNetGame ? 1u : 0u; });
    s.At({0x005A32A0, AddressOf(&CFuncs::CreateFromNodeName)}, 0, [](Regs& r) { Record({0x32A0, r.args[0]}); return 0x101u; });
    s.At({0x005A32C0, AddressOf(&CFuncs::KillFromNodeName)}, 0, [](Regs& r) { Record({0x32C1, r.args[0], Rel(r.args[1])}); return 0x100u; });
    s.At({0x0045BF90, AddressOf(static_cast<uint32_t* (*)(uint32_t, uint16_t*)>(&SkateScript::GetPrefixedNodes))}, 0, [](Regs& r) {
        Record({0xBF90, r.args[0]});
        *reinterpret_cast<uint16_t*>(r.args[1]) = g_prefixedCount;
        return reinterpret_cast<uint32_t>(g_prefixed);
    });
    s.At({0x005B0A40}, 0, [](Regs& r) { Record({0x0A40, r.args[0], r.args[1]}); return 1u; });
    s.At({0x00491110}, 0, [](Regs& r) {
        Record({0x1110, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 16), HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 16)});
        for (int i = 2; i < 13; ++i)
            Record({r.args[i]});
        return 0u;
    });
    s.At({0x005651F0, AddressOf(Mdl::Skate::IsMultiplayer)}, 0, [](Regs&) { Record({0x51F0}); return g_multiplayer ? 1u : 0u; });
}

void TestNodeCommandCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> more = {{0x00701378, 4, {}}};
    SaveAll(saved);
    SaveAll(more);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInNodeObjectCalls(*standIns);
    StandInNodeCommandCalls(*standIns);
    void* colourSlot = standIns->Thunk(4, [](Regs& r) { Record({0xC010, Rel(r.ecx), r.args[0]}); return 0u; });
    static uint8_t skate[0x400];
    static uint8_t gameNet[0x10];
    const uint32_t kName = 0xA1DC81F9, kClass = 0x12B4E660;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        SetUpScenes();
        g_sceneCount = 2;
        std::memset(skate, 0, sizeof(skate));
        reinterpret_cast<uint32_t*>(skate)[0x24 / 4] = 0x00A11000;
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007CCE14) = gameNet;
        g_geometryVtable[6] = reinterpret_cast<uint32_t>(colourSlot);
        *reinterpret_cast<uint32_t**>(g_geometry) = g_geometryVtable;
        void* geometry = g_geometry;
        std::memcpy(&g_sectors[1][0].base, &geometry, 4); // (sector 0x5ED0's geometry, +0x14)
        auto* objects = g_fakes->flagObjects;
        for (int i = 0; i < 3; ++i) {
            reinterpret_cast<uint32_t*>(objects[i])[0x14 / 4] = 0x0B10 + i;
            reinterpret_cast<uint32_t*>(objects[i])[0x08 / 4] = 0x101u << i;
        }
        auto recordObjects = [&] {
            for (int k = 0; k < 3; ++k)
                Record({reinterpret_cast<uint32_t*>(objects[k])[0x08 / 4]});
        };
        static Params node;
        g_node = &node;
        using NodeFn = bool(__cdecl*)(uint32_t, int, Script::CScript*, CStruct*);
        auto command = [&](uint32_t name, int action, bool withScript, Params& params) {
            auto* script = withScript ? reinterpret_cast<Script::CScript*>(g_fakes->script) : nullptr;
            bool result = original ? Original<NodeFn>(0x005A4DA0)(name, action, script, params.Struct())
                                   : CFuncs::NodeCommand(name, action, script, params.Struct());
            Record({0x4DA0, static_cast<uint32_t>(action), result ? 1u : 0u,
                    HashBytes(reinterpret_cast<const uint8_t*>(g_sectors), sizeof(g_sectors))});
            recordObjects();
        };
        Params none;
        Params colour; colour.Integer(0x99A9B716, 0x123456).Checksum(0x9655CFF7, 0x7E77);
        // every action on objects, rails, level objects and geometry; skipped nodes; the second viewport
        const uint32_t classes[] = {0xA0DFAC98, 0x9E7D469E, 0x5B8AB877, 0x8E6B02AD, 0xB7B3BD86, 0xBF4D3536, 0x12345678,
                                    0xEF59C100, 0x061A741E, 0xE47F1B79, 0x30C19600};
        GameVariable<int>(0x00701378) = 1;
        for (int c = 0; c < 11; ++c) {
            for (int action = 0; action < 9; ++action) {
                node = Params();
                node.Checksum(kClass, classes[c]);
                if (c >= 4)
                    node.Checksum(kName, c == 4 ? 0x5EC1 : 0x5ED0);
                g_railActive = action & 1;
                g_skipNode = false;
                command(c == 4 && action == 6 ? 0x0B11 : 0x0B10 + action % 3, action, true, colour);
            }
        }
        // the colour's default
        node = Params();
        node.Checksum(kClass, 0xBF4D3536).Checksum(kName, 0x5ED0);
        command(0x0B10, 6, true, none);
        // one viewport, then the second viewport: two viewports, the script on skater 1, not a network game
        g_skaterMask = 0x2;
        reinterpret_cast<void**>(g_fakes->script)[0xBC / 4] = g_fakes->skater;
        node = Params();
        node.Checksum(kClass, 0xBF4D3536).Checksum(kName, 0x5ED2);
        command(0x0B10, 3, true, none);
        GameVariable<int>(0x00701378) = 2;
        g_skaterMask = 0x2;
        reinterpret_cast<void**>(g_fakes->script)[0xBC / 4] = g_fakes->skater;
        for (int state = 0; state < 3; ++state) {
            g_inNetGame = state == 1;
            node = Params();
            node.Checksum(kClass, 0xBF4D3536).Checksum(kName, 0x5ED2);
            command(0x0B10, state == 2 ? 2 : 3, true, none);
        }
        command(0x0B10, 3, false, none);
        reinterpret_cast<void**>(g_fakes->script)[0xBC / 4] = nullptr;
        command(0x0B10, 2, true, none);
        g_inNetGame = false;
        // a network game: skipped nodes refused; the skate module's +0x44 stopping a shatter; level geometry without
        // a name
        g_netMode = true;
        g_skipNode = true;
        command(0x0B10, 0, true, none);
        g_skipNode = false;
        command(0x0B10, 0, true, none);
        g_netMode = false;
        skate[0x44] = 1;
        command(0x0B10, 4, true, none);
        skate[0x44] = 0;
        g_skipNode = true;
        command(0x0B10, 4, true, none);
        command(0x0B10, 2, true, none);
        g_skipNode = false;
        for (int action : {2, 4, 5, 6, 7}) {
            node = Params();
            node.Checksum(kClass, 0xBF4D3536);
            command(0x0B10, action, true, colour);
        }

        // NodeCommandByName: the skip flags; by name (a string); by prefix (stopping at the first alive for 5; an exact
        // name); neither
        using ByNameFn = bool(__cdecl*)(CStruct*, Script::CScript*, int);
        auto byName = [&](Params& params, int action) {
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<ByNameFn>(0x005A4EF0)(params.Struct(), script, action)
                                   : CFuncs::NodeCommandByName(params.Struct(), script, action);
            Record({0x4EF0, result ? 1u : 0u});
        };
        node = Params();
        node.Checksum(kClass, 0xA0DFAC98);
        Params skipA; skipA.Flag(0x2E7D5EE7).Checksum(kName, 0x0B10); byName(skipA, 1);
        Params skipB; skipB.Flag(0xBCEB479A).Checksum(kName, 0x0B10); byName(skipB, 1);
        Params named; named.String(kName, "TRG_Ped01"); byName(named, 1);
        Params prefixed; prefixed.String(0x6C4E7971, "TRG_Ped").Flag(0x80804991);
        g_prefixed[0] = 0x0B12;
        g_prefixed[1] = 0x0B10;
        g_prefixed[2] = 0x0B11;
        g_prefixedCount = 3;
        byName(prefixed, 5);
        byName(prefixed, 3);
        g_prefixedCount = 0;
        byName(prefixed, 2);
        byName(none, 1);
        // the CFunctions
        Params target; target.Checksum(kName, 0x0B11);
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
            recordObjects();
        };
        call(0x005A4FF0, CFuncs::CFunc_Create, target);
        call(0x005A5010, CFuncs::CFunc_SetObjectColor, target);
        call(0x005A5030, CFuncs::CFunc_Kill, target);
        call(0x005A5050, CFuncs::CFunc_Visible, target);
        call(0x005A5070, CFuncs::CFunc_Invisible, target);
        // Shatter: the calling object's velocity (scaled); given; the area's minimum (multiplayer or not); every
        // setting
        reinterpret_cast<void**>(g_fakes->script)[0xBC / 4] = g_fakes->object;
        float* velocity = reinterpret_cast<float*>(g_fakes->object + 0xAC);
        velocity[0] = 3.5f;
        velocity[1] = -1.25f;
        velocity[2] = 7.0f;
        velocity[3] = 0.5f;
        static Script::CVector shatterVector = {0, 4.0f, 5.0f, 6.0f};
        for (int state = 0; state < 4; ++state) {
            g_multiplayer = state & 1;
            Params p;
            p.Checksum(kName, 0x0B10);
            if (state < 2)
                p.Flag(0x9F98989E).Float(0x13B9DA7B, 0.75f * (state + 1));
            else
                p.Float(0x6187906D, 1.5f).Float(0x1680A0FB, 2.5f).Float(0x8F89F141, -3.5f);
            if (state == 3)
                p.Float(0x286BC297, 9000.0f).Float(0xBD309DD9, 1.1f).Float(0x834A5F87, 2.2f).Float(0xE632C101, 3.3f)
                    .Float(0x7DC30BA6, 4.4f).Float(0xF01BFA01, 5.5f).Checksum(0xB3DE2CAC, 0xB3D).Checksum(0xAF1A25F2, 0xAF1)
                    .Float(0x2E1C4A64, 6.6f).Vector(0x210EA9E1, &shatterVector).Float(0xC48391A5, 7.7f).Integer(0x33DF5E27, 8);
            else if (state == 2)
                p.Float(0x286BC297, 1500.0f);
            call(0x005A5090, CFuncs::CFunc_Shatter, p);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(more);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    g_node = nullptr;
    Check(traces[0].size() > 500, "node commands: the original reached its calls");
    Compare(traces[0], traces[1], "node commands (0x005A3B80-0x005A4170, 0x005A4DA0-0x005A5360)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

// --- 0x005A4170-0x005A4DA0 (game_cfuncs.cpp, net_state_cfuncs.cpp, game_flow_cfuncs.cpp) ----------------------

uint8_t g_structPool[16][8];
int g_structsUsed;
uint32_t g_restarts[4];
int g_restartCount;
const char* g_faceFiles[14];
int g_faceIndex;
uint8_t g_spawnedScript[0x100];

// A structure from the stand-in pool: its index (or the value itself when it isn't one).
uint32_t StructIndex(uint32_t value)
{
    for (int i = 0; i < 16; ++i)
        if (value == reinterpret_cast<uint32_t>(g_structPool[i]))
            return 0x57C00 + i;
    return Rel(value);
}

void StandInMenuCalls(StandIns& s)
{
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return g_structsUsed < 16 ? reinterpret_cast<uint32_t>(g_structPool[g_structsUsed++]) : 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0x9070, StructIndex(r.args[0])}); return 0u; });
    s.At({0x00477130, 0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0x7130, StructIndex(r.ecx)}); return 0u; });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, StructIndex(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00478390, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t, const char*)>(&CStruct::AddString))}, 0xC, [](Regs& r) {
        Record({0x8390, StructIndex(r.ecx), r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
        return 0u;
    });
    s.At({0x00477A90, AddressOf(static_cast<void (CStruct::*)(const char*, const char*)>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x7A90, StructIndex(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, StructIndex(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477DF0, AddressOf(static_cast<void (CStruct::*)(const char*, uint32_t)>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7DF0, StructIndex(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8670, StructIndex(r.ecx), r.args[0], StructIndex(r.args[1])});
        return 0u;
    });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, StructIndex(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046B790, AddressOf(&Obj::GetRestartNode)}, 0, [](Regs& r) {
        Record({0xB790, r.args[0], r.args[1], r.args[2]});
        for (int i = 0; i < g_restartCount; ++i)
            if (r.args[2] == (i ? g_restarts[i - 1] : 0u))
                return g_restarts[i];
        return 0u;
    });
    using RunFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], StructIndex(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    using RunNamedFn = void (*)(const char*, CStruct*, Obj::CObject*, bool);
    s.At({0x004758F0, AddressOf(static_cast<RunNamedFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x58F0, Hash(reinterpret_cast<const char*>(r.args[0])), StructIndex(r.args[1]), r.args[2], r.args[3] & 0xFF});
        return 0u;
    });
    StandInApi(s, 0x006450F8, reinterpret_cast<const void*>(&FindFirstFileA), 8, [](Regs& r) {
        Record({0x50F8, Hash(reinterpret_cast<const char*>(r.args[0]))});
        g_faceIndex = 0;
        if (!g_faceFiles[0])
            return 0xFFFFFFFFu;
        auto* data = reinterpret_cast<WIN32_FIND_DATAA*>(r.args[1]);
        std::memset(data, 0, sizeof(*data));
        std::memcpy(data->cFileName, g_faceFiles[0], std::strlen(g_faceFiles[0]) + 1);
        data->dwFileAttributes = g_faceFiles[0][0] == 'D' ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        return 0x00F1D000u;
    });
    StandInApi(s, 0x006450F4, reinterpret_cast<const void*>(&FindNextFileA), 8, [](Regs& r) {
        Record({0x50F4, r.args[0]});
        const char* next = g_faceFiles[++g_faceIndex];
        if (!next)
            return 0u;
        auto* data = reinterpret_cast<WIN32_FIND_DATAA*>(r.args[1]);
        std::memset(data, 0, sizeof(*data));
        std::memcpy(data->cFileName, next, std::strlen(next) + 1);
        data->dwFileAttributes = next[0] == 'D' ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        return 1u;
    });
    StandInApi(s, 0x006450F0, reinterpret_cast<const void*>(&FindClose), 4, [](Regs& r) { Record({0x50F0, r.args[0]}); return 1u; });
    s.At({0x00532F70, AddressOf(&GameNet::Manager::GetNetworkPreferences)}, 0, [](Regs&) { Record({0x2F70}); return 0x00FE0002u; });
    s.At({0x0046C5D0, AddressOf(&Prefs::Preferences::SetPreference)}, 8, [](Regs& r) {
        Record({0xC5D0, r.ecx, r.args[0], StructIndex(r.args[1])});
        return 1u;
    });
    s.At({0x00534090, AddressOf(&GameNet::Manager::SetMaxPlayersPreference)}, 4, [](Regs& r) { Record({0x4090, r.args[0]}); return 0u; });
    // SpawnSkaterScript
    using SpawnFn = Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool, bool);
    s.At({0x00475420, AddressOf(static_cast<SpawnFn>(&Script::SpawnScript))}, 0, [](Regs& r) {
        Record({0x5420, r.args[0], Rel(r.args[1]), r.args[2], Rel(r.args[3]), r.args[4], r.args[5], r.args[6] & 0xFF,
                r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        return reinterpret_cast<uint32_t>(g_spawnedScript);
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return (g_skaterMask >> 8) & 1 ? reinterpret_cast<uint32_t>(g_fakes->skater) : 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Rel(r.ecx), r.args[0] == reinterpret_cast<uint32_t>(g_spawnedScript + 0xBC) ? 1u : r.args[0]});
        return 0u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, Rel(r.ecx), r.args[0] == reinterpret_cast<uint32_t>(g_spawnedScript + 0xBC) ? 1u : r.args[0]});
        return 0u;
    });
}

void TestMenuPrefsCFuncs()
{
    std::vector<Saved> saved = SavedGlobals();
    std::vector<Saved> more = {{0x006F3614, 0x10, {}}, {0x007CCBC4, 0x20, {}}, {0x0067EFD0, 8, {}}, {0x007CCBE0, 4, {}}};
    SaveAll(saved);
    SaveAll(more);
    CFuncTests::UseEmptySymbolTable();
    g_fakes = new Fakes;
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    StandInNodeObjectCalls(*standIns);
    StandInMenuCalls(*standIns);
    static uint8_t skate[0x400];
    static uint8_t gameNet[0x10];

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        SetUpFakes();
        std::memset(skate, 0, sizeof(skate));
        GameVariable<void*>(0x007CE478) = skate;
        GameVariable<void*>(0x007CCE14) = gameNet;
        GameVariable<int>(0x006F3614) = 0;
        GameVariable<uint32_t>(0x006F361C) = 0x00B001F0; // the structures' pool
        auto call = [&](uint32_t address, CFunc ours, Params& params) {
            std::memset(g_fakes->script + 0x18, 0, 0xB4 - 0x18);
            reinterpret_cast<int*>(g_fakes->script)[0xB8 / 4] = 77; // m_node
            g_structsUsed = 0;
            auto* script = reinterpret_cast<Script::CScript*>(g_fakes->script);
            bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
            Record({0xC0DE, address, result ? 1u : 0u});
        };
        Params none;

        // AddRestartsToMenu: none; three (one without a RestartName)
        static Params restartNode;
        g_node = &restartNode;
        Params scale; scale.Float(0x1B9E8272, 1.25f);
        g_restartCount = 0;
        call(0x005A4260, CFuncs::CFunc_AddRestartsToMenu, scale);
        g_restarts[0] = 0x2E51;
        g_restarts[1] = 0x2E52;
        g_restarts[2] = 0x2E53;
        g_restartCount = 3;
        restartNode = Params();
        restartNode.String(CrcOf("RestartName"), "Player 1");
        call(0x005A4260, CFuncs::CFunc_AddRestartsToMenu, scale);
        restartNode = Params();
        call(0x005A4260, CFuncs::CFunc_AddRestartsToMenu, scale);
        // AddWarpPointsToMenu: an array of three (named or not); empty
        static uint32_t warps[3] = {0x3A01, 0x3A02, 0x3A03};
        static Script::CArray warpArray;
        warpArray.m_type = Script::ESYMBOLTYPE_NAME;
        warpArray.m_size = 3;
        warpArray.m_elements = warps;
        Params warpParams; warpParams.Array(0xE2C2FA03, &warpArray);
        restartNode = Params();
        restartNode.String(0xFCD206E1, "Warp A");
        call(0x005A4560, CFuncs::CFunc_AddWarpPointsToMenu, warpParams);
        restartNode = Params();
        call(0x005A4560, CFuncs::CFunc_AddWarpPointsToMenu, warpParams);
        static Script::CArray emptyArray;
        emptyArray.m_type = Script::ESYMBOLTYPE_NAME;
        emptyArray.m_size = 0;
        Params emptyWarps; emptyWarps.Array(0xE2C2FA03, &emptyArray);
        call(0x005A4560, CFuncs::CFunc_AddWarpPointsToMenu, emptyWarps);
        g_node = nullptr;
        // AddFacesToMenu: none; a directory, short names, a long one, more than 11
        std::memset(g_faceFiles, 0, sizeof(g_faceFiles));
        call(0x005A4730, CFuncs::CFunc_AddFacesToMenu, none);
        static const char* faces[] = {"Dir.img", "a.img", "abcd", "face01.img",
                                      "a_very_long_face_name_that_goes_on_and_on_and_on_and_on_and_on_and_on_and_on_and_on_and_on.img",
                                      "f2.img", "f3.img", "f4.img", "f5.img", "f6.img", "f7.img", "f8.img", "f9.img"};
        for (int i = 0; i < 13; ++i)
            g_faceFiles[i] = faces[i];
        call(0x005A4730, CFuncs::CFunc_AddFacesToMenu, none);
        // InitPrefsBeenox: the team modes and skill levels; a network id or not
        std::memcpy(&GameVariable<char>(0x007CCBD4), "secret", 7);
        for (int state = 0; state < 7; ++state) {
            std::memcpy(&GameVariable<char>(0x007CCBC4), state & 1 ? "net01" : "", state & 1 ? 6 : 1);
            GameVariable<uint32_t>(0x007CCBE0) = static_cast<uint32_t>(state - 1);
            GameVariable<int>(0x0067EFD4) = state;
            GameVariable<int>(0x0067EFD0) = 2 + state;
            call(0x005A4D90, CFuncs::CFunc_InitPrefsBeenox, none);
        }
        // SpawnSkaterScript: everything; nothing; a local skater or none, an object there before
        static Params inner;
        inner = Params();
        inner.Integer(0x10, 2);
        Params spawn;
        spawn.Checksum(0, 0x5CA7).Structure(0x7031F10C, inner.Struct()).Checksum(CrcOf("Id"), 0x1D)
            .Checksum(CrcOf("Callback"), 0xCA11).Structure(CrcOf("CallbackParams"), inner.Struct());
        for (int state = 0; state < 3; ++state) {
            std::memset(g_spawnedScript, 0, sizeof(g_spawnedScript));
            if (state == 2)
                reinterpret_cast<void**>(g_spawnedScript)[0xBC / 4] = g_fakes->object;
            g_skaterMask = state ? 0x100 : 0;
            call(0x005A4170, CFuncs::CFunc_SpawnSkaterScript, state == 1 ? none : spawn);
            Record({Rel(reinterpret_cast<uint32_t*>(g_spawnedScript)[0xBC / 4])});
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(more);
    RestoreAll(saved);
    delete g_fakes;
    g_fakes = nullptr;
    Check(traces[0].size() > 300, "menu and preference commands: the original reached its calls");
    Compare(traces[0], traces[1], "menus, Beenox preferences, skater script (0x005A4170-0x005A4DA0)");
    LogAroundFirstDifference(traces[0], traces[1]);
}

} // namespace

SELFTEST(Early, TestMenuPrefsCFuncs, RemoveStandIns);
SELFTEST(Early, TestNodeCommandCFuncs, RemoveStandIns);
SELFTEST(Early, TestParseNodeArrayCFuncs, RemoveStandIns);
SELFTEST(Early, TestNodeObjectCFuncs, RemoveStandIns);
SELFTEST(Early, TestNodeTrackingCFuncs, RemoveStandIns);
SELFTEST(Early, TestViewerModeCFuncs, RemoveStandIns);
SELFTEST(Early, TestLobbyCFuncs, RemoveStandIns);
SELFTEST(Early, TestServerListCFuncs, RemoveStandIns);
SELFTEST(Early, TestGameCFuncs, RemoveStandIns);
SELFTEST(Early, TestGameFlowCFuncs, RemoveStandIns);
SELFTEST(Early, TestNetStateCFuncs, RemoveStandIns);
