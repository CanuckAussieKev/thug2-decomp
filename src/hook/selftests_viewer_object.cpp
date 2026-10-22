// Self-tests for the model viewer's object (src/game/sys/viewer_object.cpp): the original and ours on the same
// stand-in object (viewer_testkit.h's arena), the components, assets and tasks it calls stood in for by recording code;
// the calls, results and whole arena compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/task.h"
#include "game/obj/anim_blender.h"
#include "game/sys/viewer_object.h"
#include "hook/fe_testkit.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CViewerObject;

constexpr uint32_t kObject = 0x4000;
constexpr uint32_t kTask = 0x4200;
constexpr uint32_t kTaskVtable = 0x4280;
constexpr uint32_t kComponent = 0x4300;
constexpr uint32_t kAssets = 0x4400;

uint32_t g_component;
bool g_hasValue;
uint32_t g_asset;
void* g_taskDestroy;

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return g_component;
    });
    s.At({0x00419B10, AddressOf(&Obj::CAnimBlenderComponent::HasValue)}, 4, [](Regs& r) {
        Rec(0x9B10, {r.ecx, r.args[0]});
        return g_hasValue ? 1u : 0u;
    });
    FeTest::AtFloat(s, {0x00419AF0, AddressOf(&Obj::CAnimBlenderComponent::GetValue)}, 4, [](Regs& r) {
        Rec(0x9AF0, {r.ecx, r.args[0]});
        return 12.5f;
    });
    s.At({0x00419B50, AddressOf(&Obj::CAnimBlenderComponent::PlayAnimationEx)}, 0x1C, [](Regs& r) {
        Rec(0x9B50, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6]});
        return 0u;
    });
    s.At({0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, [](Regs& r) {
        Rec(0x68F0, {r.ecx});
        return 0u;
    });
    s.At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) {
        Rec(0x7B90, {r.ecx, r.args[0], r.args[1]});
        return g_asset;
    });
    s.At({0x00407B20, AddressOf(&Ass::CAssMan::UnloadAsset)}, 4, [](Regs& r) {
        Rec(0x7B20, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045D890, AddressOf(&Obj::CCompositeObject::DestroyBody)}, 0, [](Regs& r) {
        Rec(0xD890, {r.ecx});
        return 0u;
    });
    g_taskDestroy = s.Thunk(4, [](Regs& r) {
        Rec(0x0DE5, {r.ecx, r.args[0]});
        return r.ecx;
    });
}

void TestViewerObject1()
{
    SaveGlobals({{0x006918DC, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        auto* object = Ptr<CViewerObject>(kObject);
        GameVariable<uint32_t>(0x006918DC) = At(kAssets);
        reinterpret_cast<void**>(At(kTaskVtable))[2] = g_taskDestroy;

        original ? Original<void(__thiscall*)(void*)>(0x005897B0)(object) : object->ShutDown();

        // An animation: no component; a component that doesn't know it; one that does.
        for (int variant = 0; variant < 3; ++variant) {
            g_component = variant == 0 ? 0 : At(kComponent);
            g_hasValue = variant == 2;
            original ? Original<void(__thiscall*)(void*, uint32_t)>(0x005897D0)(object, 0xABCD1234)
                     : object->PlayAnimation(0xABCD1234);
        }

        // Destroyed: with a loaded asset; an asset name that isn't loaded; none. (With no task both would fault in
        // its Remove, so the later null check can't be reached here.)
        for (int variant = 0; variant < 3; ++variant) {
            std::memset(object, 0xCD, sizeof(CViewerObject));
            object->m_task = Ptr<Tsk::BaseTask>(kTask);
            *Ptr<uint32_t>(kTask) = At(kTaskVtable);
            object->m_asset = variant == 2 ? 0 : 0x5151;
            g_asset = variant == 0 ? At(0x4500) : 0;
            original ? Original<void(__thiscall*)(void*)>(0x00589860)(object) : object->DestroyBody();
            PushArena(t);
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: shut down, an animation played, the destructor body");
}

void TestViewerObject2()
{
    SaveGlobals({});
    StandIns* s = new StandIns;
    InstallCommon(*s);
    s->At({0x00589860, AddressOf(&CViewerObject::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x9860, {r.ecx});
        return 0u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        auto* object = Ptr<CViewerObject>(kObject);
        for (unsigned int flags : {0u, 1u, 2u}) {
            void* r = original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x0058A2B0)(object, flags)
                               : object->Destroy(flags);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: the scalar deleting destructor");
}

} // namespace

SELFTEST(Early, TestViewerObject1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject2, ViewerTestKit::Cleanup);
