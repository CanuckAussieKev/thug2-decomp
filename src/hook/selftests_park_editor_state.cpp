// Self-tests for the park editor module's state commands, constructor, singleton and logic task
// (src/game/ed/park_editor_state.cpp): the original and ours on the same stand-in editor, park manager, cursor and
// menus (viewer_testkit.h's arena), every function they call stood in for by recording code; calls, results and
// the whole arena compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/task.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/piece_menu.h"
#include "game/obj/event.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"
#include "game/sys/module.h"
#include "game/sys/timer.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Ed::CCursor;
using Ed::CParkGenerator;
using Ed::CParkManager;
using Ed::CUpperMenuManager;

constexpr uint32_t kEditor = 0x1000;   // 0x7C
constexpr uint32_t kManager = 0x1100;  // only +0x00 (its generator)
constexpr uint32_t kGenerator = 0x1200;
constexpr uint32_t kCursor = 0x1300;   // 200 bytes
constexpr uint32_t kMenu = 0x1400;
constexpr uint32_t kParams = 0x1500;
constexpr uint32_t kTask = 0x1600;
constexpr uint32_t kGaps = 0x1700;

uint32_t g_time;

// A recording stand-in for a member function (thiscall) returning 0 (or `ecx` with `returnThis`).
void Stub(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag, int args, bool returnThis = false)
{
    s.At(addresses, pop, [tag, args, returnThis](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < args; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return returnThis ? r.ecx : 0u;
    });
}
// ... and for a cdecl function (ECX not recorded).
void StubC(StandIns& s, std::initializer_list<uint32_t> addresses, uint32_t tag, int args)
{
    s.At(addresses, 0, [tag, args](Regs& r) {
        g_trace.push_back(tag);
        for (int i = 0; i < args; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return 0u;
    });
}

void Install(StandIns& s)
{
    InstallCommon(s);
    using Script::CStruct;
    s.At({0x00476A00, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             uint32_t name = Text(r.args[0]);
             Rec(0x6A00, {r.ecx, name, r.args[2] & 0xFF, *reinterpret_cast<uint32_t*>(r.args[1])});
             auto it = g_params.find({r.ecx, name});
             if (it == g_params.end())
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476BA0, AddressOf(static_cast<bool (CStruct::*)(const char*)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        uint32_t name = Text(r.args[0]);
        Rec(0x6BA0, {r.ecx, name});
        return g_params.count({r.ecx, name}) ? 1u : 0u;
    });
    StubC(s, {0x0059E390, AddressOf(&Ed::Marker)}, 0xE390, 1);
    Stub(s, {0x0059DCD0, AddressOf(&CParkManager::Initialize)}, 4, 0xDCD0, 1);
    s.At({0x0059CC70, AddressOf(&CParkManager::RebuildArea)}, 8, [](Regs& r) {
        Rec(0xCC70, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 1u;
    });
    StubC(s, {0x0059C900, AddressOf(&CCursor::Destroy)}, 0xC900, 0);
    Stub(s, {0x00592090, AddressOf(&CParkGenerator::RebuildCollision)}, 0, 0x2090, 0);
    Stub(s, {0x005993E0, AddressOf(&CParkManager::GenerateNodeArray)}, 0, 0x93E0, 0);
    s.At({0x0058CBB0, AddressOf(&Ed::CGapManager::sInstance)}, 0, [](Regs&) {
        Rec(0xCBB0, {});
        return At(kGaps);
    });
    Stub(s, {0x0058CD40, AddressOf(&Ed::CGapManager::UpdateChecklist)}, 0, 0xCD40, 0);
    Stub(s, {0x0058FA50, AddressOf(&Ed::CParkEditor::SetState)}, 4, 0xFA50, 1);
    Stub(s, {0x0058F640, AddressOf(&Ed::CParkEditor::UpdateMenuStick)}, 0, 0xF640, 0);
    Stub(s, {0x0058DDE0, AddressOf(&Ed::CParkEditor::RefreshPieceMenu)}, 0, 0xDDE0, 0);
    Stub(s, {0x0058F8A0, AddressOf(&Ed::CParkEditor::LogicUpdate)}, 0, 0xF8A0, 0);
    StubC(s, {0x00598DA0, AddressOf(&CUpperMenuManager::DeleteInstance)}, 0x8DA0, 0);
    Stub(s, {0x0059BD40, AddressOf(&CParkManager::Destroy)}, 4, 0xBD40, 1);
    Stub(s, {0x00595C80, AddressOf(&CParkManager::DestroyGapManagerCopy)}, 0, 0x5C80, 0);
    StubC(s, {0x00592C90, AddressOf(&CParkGenerator::RemoveNodeArray)}, 0x2C90, 0);
    Stub(s, {0x00597490, AddressOf(&CUpperMenuManager::Enable)}, 0, 0x7490, 0);
    Stub(s, {0x00595580, AddressOf(&CUpperMenuManager::Disable)}, 0, 0x5580, 0);
    StubC(s, {0x00597400, AddressOf(&CUpperMenuManager::Create)}, 0x7400, 0);
    s.At({0x005954A0, AddressOf(&CCursor::SetVisibility)}, 4, [](Regs& r) {
        Rec(0x54A0, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    Stub(s, {0x005954D0, AddressOf(&CCursor::OnModeChanged)}, 0, 0x54D0, 0);
    Stub(s, {0x005971E0, AddressOf(&CCursor::ClampToPark)}, 0, 0x71E0, 0);
    StubC(s, {0x0059C860, AddressOf(&CCursor::CreateInstance)}, 0xC860, 0);
    Stub(s, {0x0059CB10, AddressOf(&CParkManager::RebuildPark)}, 4, 0xCB10, 1);
    Stub(s, {0x0059BE20, AddressOf(&CParkManager::ResetPark)}, 0, 0xBE20, 0);
    Stub(s, {0x00591D20, AddressOf(&CParkGenerator::SetMaxPlayers)}, 4, 0x1D20, 1);
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Rec(0xE970, {});
        return g_time;
    });
    // The constructor's.
    Stub(s, {0x0044F7E0, AddressOf(&Mdl::Module::Construct)}, 0, 0xF7E0, 0, true);
    Stub(s, {0x0045FAA0, AddressOf(&Obj::CEventListener::Construct)}, 0, 0xFAA0, 0, true);
    Stub(s, {0x0045FB30, AddressOf(&Obj::CEventListener::RegisterWithTracker)}, 4, 0xFB30, 1);
    Stub(s, {0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, 0x6890, 1, true);
    StubC(s, {0x00599BF0, AddressOf(&CParkManager::CreateInstance)}, 0x9BF0, 0);
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1D40, {r.args[0], r.args[1], r.args[2]});
        if (g_integers[0xCA11] == 0)
            return 0u; // (out of memory)
        uint32_t block = Allocate(r.args[0]);
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
}

void SetUp(int run)
{
    SetUpArena(run);
    Alias(&Ed::CParkEditor::LogicTask, 0x0058FFC0);
    Alias(&Ed::HandleInput, 0x0058DB70);
    Alias(&Stubs::DoNothing, 0x005480A0); // (the display task's code)
    GameVariable<uint32_t>(0x007CE5D8) = At(kEditor);
    GameVariable<uint32_t>(0x007CE5DC) = 3;
    GameVariable<uint32_t>(0x007CE604) = At(kManager);
    GameVariable<uint32_t>(0x007CE608) = At(kCursor);
    GameVariable<uint32_t>(0x007CE60C) = At(kMenu);
    GameVariable<uint32_t>(0x007CE5EC) = 1000;
    *Ptr<uint32_t>(kManager) = At(kGenerator);
    for (uint32_t i = 0; i < 0x7C; i += 4)
        *Ptr<uint32_t>(kEditor + i) = 0x22220000 + i;
    Ptr<uint32_t>(kEditor)[0x48 / 4] = 0xFF; // (paused, among others)
}

void TestParkEditorState()
{
    SaveGlobals({{0x007CE5D8, 8, {}}, {0x007CE604, 16, {}}, {0x007CE5EC, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    const uint32_t states[] = {0xCD3682C1, 0xA40F0B8A, 0xD443A2BC, 0x7ECA21E5, 0x12345678, 0};
    const uint32_t commands[] = {0x27F2117D, 0xE76F5250, 0x1A4E0EF9, 0x2A907CBC, 0x01625EDA, 0x99, 0};
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        auto* params = Ptr<Script::CStruct>(kParams);
        const uint32_t state = Hash("state");
        const uint32_t command = Hash("command");
        // Every state (the cursor placing rails or not, there or not; the menu there or not), every edit command.
        for (uint32_t st : states) {
            for (int variant = 0; variant < (st == 0x7ECA21E5 ? 7 : 4); ++variant) {
                g_params.clear();
                if (st)
                    g_params[{reinterpret_cast<uint32_t>(params), state}] = st;
                if (st == 0x7ECA21E5 && commands[variant])
                    g_params[{reinterpret_cast<uint32_t>(params), command}] = commands[variant];
                GameVariable<uint32_t>(0x007CE608) = variant == 3 ? 0u : At(kCursor);
                GameVariable<uint32_t>(0x007CE60C) = variant == 2 ? 0u : At(kMenu);
                Ptr<CCursor>(kCursor)->m_mode = variant == 1 ? CCursor::kModeRailPlacement : CCursor::kModeRegular;
                bool r = original ? Original<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x0058FBD0)(params, nullptr)
                                  : Ed::CFunc_SetParkEditorState(params, nullptr);
                t.push_back(r);
                PushArena(t);
            }
        }
        // Paused, unpaused, neither (the menu and the cursor there or not, the cursor placing rails).
        const uint32_t pause = Hash("pause");
        const uint32_t unpause = Hash("unpause");
        for (int which = 0; which < 4; ++which) {
            for (int variant = 0; variant < 4; ++variant) {
                g_params.clear();
                if (which == 0 || which == 3)
                    g_params[{reinterpret_cast<uint32_t>(params), pause}] = 1;
                if (which == 1 || which == 3)
                    g_params[{reinterpret_cast<uint32_t>(params), unpause}] = 1;
                GameVariable<uint32_t>(0x007CE608) = variant == 3 ? 0u : At(kCursor);
                GameVariable<uint32_t>(0x007CE60C) = variant == 2 ? 0u : At(kMenu);
                Ptr<CCursor>(kCursor)->m_mode = variant == 1 ? CCursor::kModeRailPlacement : CCursor::kModeGap;
                bool r = original ? Original<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x0058FED0)(params, nullptr)
                                  : Ed::CFunc_SetParkEditorPauseMode(params, nullptr);
                t.push_back(r);
                PushArena(t);
            }
        }
        // The logic task (the clock moved on by the time since the last frame).
        Ptr<uint32_t>(kTask)[7] = At(kEditor);
        for (uint32_t now : {1500u, 1000u, 200u}) {
            g_time = now;
            original ? Original<void(__cdecl*)(void*)>(0x0058FFC0)(Ptr<void>(kTask)) : Ed::CParkEditor::LogicTask(Ptr<Ed::ParkEditorTask>(kTask));
            t.push_back(GameVariable<uint32_t>(0x007CE5EC));
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park editor: SetParkEditorState, SetParkEditorPauseMode, the logic task");
}

void* CallConstruct(void* code, void* memory)
{
    void* result;
    __asm {
        mov ecx, memory
        call code
        mov result, eax
    }
    return result;
}

void TestParkEditorCreate()
{
    SaveGlobals({{0x007CE5D8, 8, {}}, {0x007CE604, 16, {}}, {0x007CE5EC, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        // Constructed over dirty memory.
        {
            std::memset(Ptr<void>(kEditor), 0xCD, 0x7C);
            void* code = original ? reinterpret_cast<void*>(0x0058FFF0) : reinterpret_cast<void*>(AddressOf(&Ed::CParkEditor::Construct));
            t.push_back(Rel(reinterpret_cast<uint32_t>(CallConstruct(code, Ptr<void>(kEditor)))));
            PushArena(t);
        }
        // The singleton: there already (with and without `create`), not there (no `create`; made; out of memory).
        g_integers[0xCA11] = 1;
        for (int c = 0; c < 5; ++c) {
            GameVariable<uint32_t>(0x007CE5D8) = c < 2 ? At(kEditor) : 0u;
            g_integers[0xCA11] = c == 4 ? 0 : 1;
            bool create = c != 1 && c != 2;
            void* r = original ? Original<void*(__cdecl*)(bool)>(0x00590160)(create) : Ed::CParkEditor::Create(create);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
            t.push_back(Rel(GameVariable<uint32_t>(0x007CE5D8)));
            t.push_back(GameVariable<uint32_t>(0x007CE5DC));
            PushArena(t);
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park editor: the constructor and the singleton");
}

} // namespace

SELFTEST(Early, TestParkEditorState, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkEditorCreate, ViewerTestKit::Cleanup);
