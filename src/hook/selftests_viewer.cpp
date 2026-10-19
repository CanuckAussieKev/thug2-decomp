// Self-tests for the viewer module's constructor, start callback, tasks and PC tool messages
// (src/game/sys/viewer.cpp, 0x00577380-0x00577F20): the original and ours run the same calls on an identical stand-in
// viewer, camera object, input and main loop managers and messages in an arena; what they call outside (scripts,
// structures, memory, the object manager, the matrix helpers, the net manager and dispatcher, the task lists, the
// timer) is stood in for by recording code (viewer_testkit.h), and the calls and the whole arena are compared.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr: what the game uses
#include <winsock2.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/stubs.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_manager.h"
#include "game/net/net_messages.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/sys/module.h"
#include "game/sys/timer.h"
#include "game/sys/viewer.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;

// Arena layout.
constexpr uint32_t kViewer = 0x0000;    // the viewer (0x58)
constexpr uint32_t kTasks = 0x0100;     // its tasks and handlers, 0x40 apart
constexpr uint32_t kCamera = 0x0200;    // viewer_cam (0x108)
constexpr uint32_t kMessage = 0x0400;   // a PC tool message
constexpr uint32_t kContext = 0x0500;   // its MsgHandlerContext
constexpr uint32_t kParams = 0x0600;    // a parameter structure
constexpr uint32_t kApp = 0x0700;       // the viewer's app server
constexpr uint32_t kInput = 0x0800;     // the input manager (0x118)
constexpr uint32_t kMainLoop = 0x0A00;  // the main loop manager
constexpr uint32_t kObject = 0x0B00;    // a viewer object

bool g_cameraThere;
uint32_t g_time;
int g_setting;

Mth::Matrix* __stdcall SameMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    return &Mth::Multiply(*out, *a, *b);
}

void FillRotation(uint32_t matrix, uint32_t angle, uint32_t salt)
{
    float a;
    std::memcpy(&a, &angle, 4);
    auto* m = reinterpret_cast<float*>(matrix);
    for (int i = 0; i < 16; ++i)
        m[i] = static_cast<float>(std::sin(a * (i + 1) + salt) * 0.5 + (i % 5 == 0 ? 1.0 : 0.0));
}

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        return g_cameraThere ? At(kCamera) : 0u;
    });
    s.At({0x005772A0, AddressOf(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Vector&, float)>(&Mth::Matrix::RotateLocal))},
         8, [](Regs& r) {
             const auto* axis = reinterpret_cast<const uint32_t*>(r.args[0]);
             Rec(0x72A0, {r.ecx, axis[0], axis[1], axis[2], axis[3], r.args[1]});
             float angle;
             std::memcpy(&angle, &r.args[1], 4);
             reinterpret_cast<float*>(r.ecx)[0] += angle;
             reinterpret_cast<float*>(r.ecx)[6] -= angle * 0.5f;
             return r.ecx;
         });
    s.At({0x00404E20, AddressOf(&Mth::CreateRotateYMatrix)}, 0, [](Regs& r) {
        Rec(0x4E20, {r.args[1]});
        FillRotation(r.args[0], r.args[1], 1);
        return r.args[0];
    });
    s.At({0x00404C60, AddressOf(static_cast<Mth::Matrix& (*)(Mth::Matrix&, const Mth::Vector&, float)>(&Mth::CreateRotateMatrix))},
         0, [](Regs& r) {
             const auto* axis = reinterpret_cast<const uint32_t*>(r.args[1]);
             Rec(0x4C60, {axis[0], axis[1], axis[2], axis[3], r.args[2]});
             FillRotation(r.args[0], r.args[2], 2);
             return r.args[0];
         });
    s.At({0x005772E0, AddressOf(&Mdl::MoveObjectLocal)}, 0, [](Regs& r) {
        const auto* offset = reinterpret_cast<const uint32_t*>(r.args[1]);
        Rec(0x72E0, {r.args[0], offset[0], offset[1], offset[2], offset[3]});
        return 0u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Rec(0xE970, {});
        return g_time;
    });
    s.At({0x0058A2E0}, 8, [](Regs& r) {
        Rec(0xA2E0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004A0710}, 0, [](Regs&) {
        Rec(0x0710, {});
        return static_cast<uint32_t>(g_setting);
    });
    s.At({0x004A0AD0}, 0, [](Regs& r) {
        Rec(0x0AD0, {r.args[0]});
        return 0u;
    });
    s.At({0x00577170, AddressOf(&Mdl::Viewer::CreateViewerObject)}, 4, [](Regs& r) {
        Rec(0x7170, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0047A0C0}, 0, [](Regs& r) {
        Rec(0xA0C0, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0045A5C0, AddressOf(&Net::Dispatcher::AddHandler)}, 20, [](Regs& r) {
        Rec(0xA5C0, {r.ecx, r.args[0] & 0xFF, r.args[1], r.args[2], r.args[3], r.args[4]});
        return At(0x7E00);
    });
    s.At({0x00406C90}, 4, [](Regs& r) {
        Rec(0x6C90, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00406890}, 4, [](Regs& r) {
        Rec(0x6890, {r.ecx, r.args[0]});
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0x40DE0000u + r.args[0];
        return r.ecx;
    });
    s.At({0x0044F7E0, AddressOf(&Mdl::Module::Construct)}, 0, [](Regs& r) {
        Rec(0xF7E0, {r.ecx});
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0x3D0;
        return r.ecx;
    });
    static const char kIp[] = "10.0.0.7";
    s.At({0x00454910, AddressOf(&Net::Manager::GetLocalIP)}, 0, [](Regs& r) {
        Rec(0x4910, {r.ecx});
        return reinterpret_cast<uint32_t>(kIp);
    });
    StandInApi(s, 0x006452A0, reinterpret_cast<const void*>(&inet_addr), 4, [](Regs& r) {
        Rec(0xEAAE, {Text(r.args[0])});
        return 0x0700000Au;
    });
    s.At({0x005B9760, AddressOf(&Stubs::ReturnTrue)}, 0, [](Regs&) {
        Rec(0x9760, {});
        return 1u;
    });
    s.At({0x00455270, AddressOf(&Net::Manager::CreateNewAppServer)}, 24, [](Regs& r) {
        Rec(0x5270, {r.ecx, r.args[0], Text(r.args[1]), r.args[2], r.args[3] & 0xFFFF, r.args[4], r.args[5]});
        return At(kApp);
    });
    // (the same matrix product for both: the original's statically linked one and ours)
    s.Bytes(0x005BFA8C, Branch(0xE9, 0x005BFA8C, reinterpret_cast<void*>(&SameMultiply)));
}

void Aliases()
{
    Alias(&Mdl::FreeCameraTask, 0x00577870);
    Alias(&Mdl::SelectComboTask, 0x00577380);
    Alias(&Mdl::HandleViewerInput, 0x00576F00);
    Alias(&Mdl::HandleViewerShift, 0x00576EB0);
    Alias(&Mdl::HandleReloadScene, 0x00577CD0);
    Alias(&Mdl::ReloadScriptFile, 0x00576CF0);
    Alias(&Mdl::HandleAddToScene, 0x005774C0);
    Alias(&Mdl::HandleCreateViewerObject, 0x005775A0);
    Alias(&Mdl::HandleRemoveViewerObject, 0x00577140);
    Alias(&Mdl::HandleRunScript, 0x00577670);
}

void PushCamera(std::vector<uint32_t>& t)
{
    const auto* words = Ptr<uint32_t>(kCamera);
    for (uint32_t i = 0x4C / 4; i < 0x108 / 4; ++i)
        t.push_back(words[i]);
}

void SaveViewerGlobals()
{
    SaveGlobals({{0x007CE498, 0x18, {}}, {0x00684FA8, 1, {}}, {0x00696D40, 4, {}}, {0x00696D68, 4, {}},
                 {0x006EFDD8, 4, {}}, {0x006F0FB8, 4, {}}});
}

// The free camera (every dead zone edge, NaN, both modes and a third, the mode toggle with and without a viewer
// object, the shoulder moves, both rolls with and without the roll lock, the setting command) and PlaceCamera (off,
// no camera, with and without ViewerObjPos).
void TestViewerFreeCamera()
{
    SaveViewerGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    Aliases();
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_integers.clear();
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x006F0FB8) = At(0x7D00);
        auto* viewer = Ptr<Mdl::Viewer>(kViewer);
        auto* task = Ptr<Mdl::ViewerTask>(kTasks);
        task->m_data = viewer;
        auto* camera = Ptr<Obj::CCompositeObject>(kCamera);
        for (int i = 0; i < 16; ++i)
            (&camera->m_matrix.row[0].x)[i] = static_cast<float>(std::cos(i * 0.9) * 2.0);
        camera->m_compositeFlags = 0x37;
        g_cameraThere = true;
        GameVariable<float>(0x007CE49C) = 400.0f;
        GameVariable<float>(0x007CE498) = 120.0f;
        GameVariable<uint32_t>(0x007CE4AC) = 1000;
        g_time = 1000;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float sticks[] = {0.0f, 30.0f, 50.0f, 50.5f, 51.0f, 127.0f, -50.0f, -50.5f, -51.0f, -128.0f, 77.25f, nan};
        const uint32_t commands[] = {0, 0x2, 0x4, 0x6, 0x4000, 0x10000, 0x20000, 0x30000, 0x1000, 0x4000 | 0x1000 | 0x2,
                                     0xFFFFFFFF};
        int step = 0;
        for (int mode : {0, 1, 2}) {
            for (uint32_t command : commands) {
                for (int k = 0; k < 4; ++k) {
                    ++step;
                    viewer->m_mode = mode;
                    viewer->m_commands = command;
                    viewer->m_object = step % 3 == 0 ? nullptr : Ptr<void>(kObject);
                    viewer->m_rightX = sticks[(step * 5) % 12];
                    viewer->m_rightY = sticks[(step * 7 + 1) % 12];
                    viewer->m_leftX = sticks[(step * 3 + 2) % 12];
                    viewer->m_leftY = sticks[(step * 11 + 3) % 12];
                    if (k == 3)
                        viewer->m_rightX = viewer->m_rightY = viewer->m_leftX = viewer->m_leftY = 0.0f;
                    g_integers[0xD8C91578] = step % 4 == 1;
                    g_setting = step % 7;
                    g_time += 7 + step * 13;
                    if (original)
                        Original<void(__cdecl*)(Mdl::ViewerTask*)>(0x00577870)(task);
                    else
                        Mdl::FreeCameraTask(task);
                    t.insert(t.end(), {Bits(viewer->m_rightX), Bits(viewer->m_rightY), Bits(viewer->m_leftX),
                                       Bits(viewer->m_leftY), static_cast<uint32_t>(viewer->m_mode), viewer->m_commands,
                                       GameVariable<uint32_t>(0x007CE4AC)});
                    PushCamera(t);
                }
            }
        }
        // PlaceCamera.
        auto* params = Ptr<CStruct>(kParams);
        for (int c = 0; c < 6; ++c) {
            g_integers[0xC965E06F] = c != 0;
            g_cameraThere = c != 1;
            g_vectors.clear();
            if (c >= 3)
                g_vectors[{At(kParams), Hash("ViewerObjPos")}] = {12.5f + c, -40.25f * c, 7.0f - c, 3.0f};
            camera->m_pos = {9.0f, 9.0f, 9.0f, 9.0f};
            bool result = original ? Original<bool(__thiscall*)(Mdl::Viewer*, CStruct*)>(0x00577740)(viewer, params)
                                   : viewer->PlaceCamera(params);
            t.push_back(result);
            PushCamera(t);
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer free camera, PlaceCamera");
}

// The Select combos (select_shift off; every button alone, several at once - the first in the order wins; the
// script there or not), the PC tool's messages, the start callback (with and without the debug checks) and the
// constructor (both ways).
void TestViewerModule()
{
    SaveViewerGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    Aliases();
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_integers.clear();
        g_scripts.clear();
        std::vector<uint32_t>& t = traces[run];
        auto* viewer = Ptr<Mdl::Viewer>(kViewer);
        auto* task = Ptr<Mdl::ViewerTask>(kTasks);
        task->m_data = viewer;
        // The Select combos.
        const uint32_t makes[] = {0,      0x10,   0x80,   0x20,   0x800, 0x40,       0x4,        0x1,
                                  0x8,    0x2,    0x1000, 0x4000, 0x8000, 0x2000,    0x100,      0xFFFF,
                                  0x6000, 0x3000, 0xA,    0x5,    0x60,   0x880,     0x10000,    0x48};
        int step = 0;
        for (uint32_t m : makes) {
            for (int k = 0; k < 3; ++k) {
                ++step;
                g_integers[0xF3E055E1] = k != 0;
                for (uint32_t name : {0xCBB91022u, 0xE69691FAu, 0xFFC29C2Au, 0x51B0E413u, 0x746D2598u, 0x81D0A13Cu,
                                      0x18D9F086u, 0x55919EE3u, 0xCC98CF59u, 0x1B0B7922u, 0xBBC0E36Au, 0xDD589439u,
                                      0x7A03C488u})
                    g_scripts[name] = ((name >> 3) + step) % 3 != 0;
                viewer->m_shiftMakes = m;
                if (original)
                    Original<void(__cdecl*)(Mdl::ViewerTask*)>(0x00577380)(task);
                else
                    Mdl::SelectComboTask(task);
            }
        }
        // The PC tool's messages.
        auto* context = Ptr<Net::MsgHandlerContext>(kContext);
        context->m_Msg = Ptr<char>(kMessage);
        std::memcpy(Ptr<char>(kMessage), "scene_name", 11);
        std::memcpy(Ptr<char>(kMessage + 0x80), "addition", 9);
        *Ptr<uint32_t>(kMessage + 0x84) = 0x5E1E7011;
        context->m_data = Ptr<void>(kViewer + 0x40);
        using MessageHandler = int(__cdecl*)(Net::MsgHandlerContext*);
        const struct {
            uint32_t address;
            MessageHandler ours;
        } handlers[] = {{0x005774C0, &Mdl::HandleAddToScene},
                        {0x005775A0, &Mdl::HandleCreateViewerObject},
                        {0x00577670, &Mdl::HandleRunScript},
                        {0x00577CD0, &Mdl::HandleReloadScene}};
        for (const auto& h : handlers)
            t.push_back(original ? Original<MessageHandler>(h.address)(context) : h.ours(context));
        // The start callback.
        GameVariable<uint32_t>(0x00696D40) = At(kInput);
        GameVariable<uint32_t>(0x00696D68) = At(kMainLoop);
        *Ptr<uint32_t>(0x7000 + 0x11C) = 0xFE0EA900; // the front-end heap
        *Ptr<uint32_t>(kInput + 4) = 0xDE000004;      // the servers' devices
        *Ptr<uint32_t>(kInput + 4 + 0x84) = 0xDE000088;
        viewer->m_netHandler = Ptr<void>(kApp);
        viewer->m_task1C = Ptr<void>(kTasks + 0x40);
        viewer->m_task20 = Ptr<void>(kTasks + 0x80);
        viewer->m_inputHandler = Ptr<Mdl::ViewerInputHandler>(kTasks + 0xC0);
        viewer->m_shiftHandler = Ptr<Mdl::ViewerInputHandler>(kTasks + 0x100);
        viewer->m_inputHandler->m_index = 1;
        viewer->m_shiftHandler->m_index = 0;
        for (bool debug : {false, true}) {
            GameVariable<bool>(0x00684FA8) = debug;
            if (original)
                Original<void(__thiscall*)(Mdl::Viewer*)>(0x00577DA0)(viewer);
            else
                viewer->StartCallback();
            viewer->m_inputHandler->m_index = 0;
            viewer->m_shiftHandler->m_index = 1;
        }
        // The constructor.
        GameVariable<uint32_t>(0x006EFDD8) = At(0x7C00);
        for (bool debug : {false, true}) {
            GameVariable<bool>(0x00684FA8) = debug;
            g_integers[0x702247A5] = debug ? 1 : 0;
            auto* made = Ptr<Mdl::Viewer>(0x0C00 + 0x80 * debug);
            t.push_back(Rel(reinterpret_cast<uint32_t>(
                original ? Original<Mdl::Viewer*(__thiscall*)(Mdl::Viewer*)>(0x00577F20)(made) : made->Construct())));
            t.insert(t.end(), {GameVariable<uint32_t>(0x007CE498), GameVariable<uint32_t>(0x007CE49C),
                               Rel(GameVariable<uint32_t>(0x007CE4A8))});
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer select combos, messages, start callback, constructor");
}

} // namespace

SELFTEST(Early, TestViewerFreeCamera, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerModule, ViewerTestKit::Cleanup);
