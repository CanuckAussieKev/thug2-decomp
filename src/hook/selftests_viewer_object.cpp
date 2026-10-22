// Self-tests for the model viewer's object (src/game/sys/viewer_object.cpp): the original and ours on the same
// stand-in object (viewer_testkit.h's arena), the components, assets and tasks it calls stood in for by recording code;
// the calls, results and whole arena compared.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/task.h"
#include "game/obj/game_objects.h"
#include "game/script/struct.h"
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

// The loader: model0 and model2 named, then modelname / model / neither (profile looked up); the CAS artist flag on and
// off; skeletonFileName with the skeleton loaded or not; ViewerObjPos given or not; the debug heap or not. The struct
// getters, assets, components, scripts, the object manager and Finalize stood in for; the path helpers (0x004879D0,
// 0x0048BFE0) and the strings run for real.
std::map<uint32_t, const char*> g_strings; // by key text hash or checksum
int g_artist;
uint32_t g_loadedSkeleton;
bool g_hasPosition;
void* g_registerThunk;
void* g_finalizeThunk;

void TestViewerObject3()
{
    SaveGlobals({{0x006918DC, 4, {}}, {0x006F0FB8, 4, {}}, {0x007CEFAC, 1, {}}});
    StandIns* s = new StandIns;
    InstallCommon(*s);
    s->At({0x006234FE}, 0, [](Regs& r) {
        return static_cast<uint32_t>(std::snprintf(reinterpret_cast<char*>(r.args[0]), 260,
                                                   reinterpret_cast<const char*>(r.args[1]), r.args[2], r.args[3], r.args[4]));
    });
    auto getString = [](Regs& r, uint32_t key) {
        Rec(0x0C60, {r.ecx, key, r.args[2] & 0xFF});
        auto it = g_strings.find(key);
        *reinterpret_cast<const char**>(r.args[1]) = it == g_strings.end() ? nullptr : it->second;
        return it == g_strings.end() ? 0u : 1u;
    };
    s->At({0x00476C80, AddressOf(static_cast<bool (CStruct::*)(const char*, const char**, int)>(&CStruct::GetString))}, 12,
          [getString](Regs& r) { return getString(r, Hash(reinterpret_cast<const char*>(r.args[0]))); });
    s->At({0x00476C60, 0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
          12, [getString](Regs& r) { return getString(r, r.args[0]); });
    s->At({0x00476E70, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
          [](Regs& r) {
              Rec(0x6E70, {r.ecx, r.args[0], r.args[2] & 0xFF});
              return 0u;
          });
    s->At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
          [](Regs& r) {
              Rec(0x6950, {r.ecx, r.args[0], r.args[2] & 0xFF});
              *reinterpret_cast<uint32_t*>(r.args[1]) = 0x5E1E;
              return 1u;
          });
    s->At({0x00477040, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
          12, [](Regs& r) {
              Rec(0x7040, {r.ecx, r.args[0], r.args[2] & 0xFF});
              if (g_hasPosition)
                  *reinterpret_cast<Script::VectorValue*>(r.args[1]) = {10.0f, 20.0f, 30.0f, 0.5f};
              return g_hasPosition ? 1u : 0u;
          });
    s->At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x8F60, {r.args[0]});
        return static_cast<uint32_t>(g_artist);
    });
    s->At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) {
        Rec(0x7B90, {r.ecx, r.args[0], r.args[1]});
        return r.args[0] == g_loadedSkeleton || (r.args[0] & 1) ? At(0x4500) : 0u; // (some files "cached")
    });
    s->At({0x00407B20, AddressOf(&Ass::CAssMan::UnloadAsset)}, 4, [](Regs& r) {
        Rec(0x7B20, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x00408440, AddressOf(&Ass::CAssMan::LoadAsset)}, 0x24, [](Regs& r) {
        Rec(0x8440, {r.ecx, Text(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4] & 0xFF, r.args[5] & 0xFF,
                     r.args[6] & 0xFF, r.args[7], r.args[8]});
        return 0u;
    });
    s->At({0x0057B2F0, AddressOf(&Obj::AddMotionComponents)}, 0, [](Regs& r) {
        Rec(0xB2F0, {r.args[0]});
        return 0u;
    });
    s->At({0x0057B420, AddressOf(&Obj::AddNodeComponents)}, 0, [](Regs& r) {
        Rec(0xB420, {r.args[0], r.args[1]});
        return 0u;
    });
    s->At({0x005897D0, AddressOf(&CViewerObject::PlayAnimation)}, 4, [](Regs& r) {
        Rec(0x97D0, {r.ecx, r.args[0]});
        return 0u;
    });
    g_registerThunk = s->Thunk(4, [](Regs& r) {
        Rec(0x0EE1, {r.ecx, r.args[0]});
        return 0u;
    });
    g_finalizeThunk = s->Thunk(0, [](Regs& r) {
        Rec(0x0F17, {r.ecx});
        return 0u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        GameVariable<uint32_t>(0x006918DC) = At(kAssets);
        GameVariable<uint32_t>(0x006F0FB8) = At(0x4600);
        *Ptr<uint32_t>(0x4600) = At(0x4680);
        reinterpret_cast<void**>(At(0x4680))[1] = g_registerThunk;
        reinterpret_cast<void**>(At(0x4700))[8] = g_finalizeThunk;
        *Ptr<uint32_t>(0x7000 + 0x114) = At(0x7180); // the bottom-up heap
        *Ptr<uint32_t>(0x7000 + 0x154) = At(0x71C0); // the debug heap
        auto* object = Ptr<CViewerObject>(kObject);
        auto* params = Ptr<CStruct>(0x4800);
        for (int variant = 0; variant < 5; ++variant) {
            std::memset(object, 0xCD, sizeof(CViewerObject));
            object->m_vtable = Ptr<void*>(0x4700);
            g_strings.clear();
            g_strings[Hash("model0")] = "board";
            g_strings[Hash("model2")] = "skins\\deck"; // (a path: "models\<name>" only)
            if (variant == 0)
                g_strings[0xB974F2FC] = "ped_a";
            if (variant == 1)
                g_strings[0x286A8D26] = "ped_b";
            if (variant != 4)
                g_strings[0xF679DB13] = "skel.ske";
            g_artist = variant != 2;
            g_loadedSkeleton = variant == 3 ? 0x5E1E : 0;
            g_hasPosition = variant & 1;
            GameVariable<bool>(0x007CEFAC) = variant == 1;
            original ? Original<void(__thiscall*)(void*, void*)>(0x00589910)(object, params) : object->Load(params);
            PushArena(t);
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: loaded from its parameters");
}
} // namespace

SELFTEST(Early, TestViewerObject1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject2, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject3, ViewerTestKit::Cleanup);
