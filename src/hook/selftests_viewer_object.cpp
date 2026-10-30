// Self-tests for the model viewer's object (src/game/sys/viewer_object.cpp): the original and ours on the same
// stand-in object (viewer_testkit.h's arena), the components, assets and tasks it calls stood in for by recording code;
// the calls, results and whole arena compared.

#include <bit>
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
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/script/load.h"
#include "game/sio/inpman.h"
#include "game/sys/viewer.h"
#include "game/sys/viewer_object.h"
#include "game/gfx/model.h"
#include "game/obj/movie_cfuncs.h"
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
            object->m_task = Ptr<Obj::ViewerObjectInputHandler>(kTask);
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

// --- The animation controls, the own update, the panel, the wheels, the input handler, the messages, the
// constructor (0x00589840-0x0058AFB7) ---

std::map<uint32_t, uint32_t> g_components; // type -> arena offset (absent: none)
void* g_memberThunk;                         // the animation component's vtable +0x30
constexpr uint32_t kComponentVtable = 0x4380;
constexpr uint32_t kModelComponent = 0x4900;
constexpr uint32_t kSkeletonComponent = 0x4A00;
constexpr uint32_t kNxModel = 0x4B00;
constexpr uint32_t kSkeleton = 0x4C00;
constexpr uint32_t kBones = 0x5000;     // the skeleton's bone matrices (8 x 0x40)
constexpr uint32_t kHierarchy = 0x5400; // the model's hierarchy (8 x 0x50)
int g_boneCount;
float g_maxWheelSpeed;
float g_timeline18;
float g_length;
uint32_t g_timeline30;
Mdl::Viewer* g_viewer;

// The game's sprintf (its C runtime isn't started offline): ours formats from the copied arguments.
void InstallSprintf(StandIns& s)
{
    s.At({0x006234FE}, 0, [](Regs& r) {
        char* out = reinterpret_cast<char*>(r.args[0]);
        int n = std::vsnprintf(out, 128, reinterpret_cast<const char*>(r.args[1]),
                             reinterpret_cast<va_list>(&r.args[2]));
        return static_cast<uint32_t>(n);
    });
}

void InstallAnimation(StandIns& s)
{
    InstallCommon(s);
    InstallSprintf(s);
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        auto it = g_components.find(r.args[0]);
        return it == g_components.end() ? 0u : At(it->second);
    });
    s.At({0x00419AB0, AddressOf(&Obj::CAnimBlenderComponent::GetAnimSet)}, 0, [](Regs& r) {
        Rec(0x9AB0, {r.ecx});
        return 0x1000u;
    });
    s.At({0x00419B90, AddressOf(&Obj::CAnimBlenderComponent::FirstChannel47E850)}, 0x10, [](Regs& r) {
        Rec(0x9B90, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x00419BB0, AddressOf(&Obj::CAnimBlenderComponent::FirstChannel47E810)}, 4, [](Regs& r) {
        Rec(0x9BB0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004199F0, AddressOf(&Obj::CAnimBlenderComponent::PassToEachChannel)}, 4, [](Regs& r) {
        Rec(0x99F0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004199C0, AddressOf(&Obj::CAnimBlenderComponent::VisitChannels)}, 0, [](Regs& r) {
        Rec(0x99C0, {r.ecx});
        return 0u;
    });
    s.At({0x00419730, AddressOf(&Obj::CAnimBlenderComponent::FirstChannel47EFF0)}, 0, [](Regs& r) {
        Rec(0x9730, {r.ecx});
        return 0u;
    });
    s.At({0x0041A0A0, AddressOf(&Obj::CAnimBlenderComponent::GetTimeline30)}, 0, [](Regs& r) {
        Rec(0xA0A0, {r.ecx});
        return g_timeline30;
    });
    s.At({0x00419B10, AddressOf(&Obj::CAnimBlenderComponent::HasValue)}, 4, [](Regs& r) {
        Rec(0x9B10, {r.ecx, r.args[0]});
        return r.args[0] != 0 ? 1u : 0u;
    });
    FeTest::AtFloat(s, {0x00419AF0, AddressOf(&Obj::CAnimBlenderComponent::GetValue)}, 4, [](Regs& r) {
        Rec(0x9AF0, {r.ecx, r.args[0]});
        return g_length;
    });
    FeTest::AtFloat(s, {0x00419F80, AddressOf(&Obj::CAnimBlenderComponent::GetTimeline18)}, 0, [](Regs& r) {
        Rec(0x9F80, {r.ecx});
        return g_timeline18;
    });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Rec(0x7C60, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) {
        Rec(0x7B90, {r.ecx, r.args[0], r.args[1]});
        return (r.args[0] & 3) == 1 ? At(0x4500) : 0u; // (some "loaded")
    });
    s.At({0x00407B20, AddressOf(&Ass::CAssMan::UnloadAsset)}, 4, [](Regs& r) {
        Rec(0x7B20, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00408870, AddressOf(&Ass::CAssMan::LoadAnim)}, 0x14, [](Regs& r) {
        Rec(0x8870, {r.ecx, Text(r.args[0]), r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4] & 0xFF});
        return 1u;
    });
    s.At({0x005897D0, AddressOf(&CViewerObject::PlayAnimation)}, 4, [](Regs& r) {
        Rec(0x97D0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00492FF0, AddressOf(&Nx::CModel::GetAssetBoneCount)}, 0, [](Regs& r) {
        Rec(0x2FF0, {r.ecx});
        return static_cast<uint32_t>(g_boneCount);
    });
    s.At({0x00493040, AddressOf(&Nx::CModel::GetAssetHierarchy)}, 0, [](Regs& r) {
        Rec(0x3040, {r.ecx});
        return At(kHierarchy);
    });
    FeTest::AtFloat(s, {0x00478F00, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}, 0, [](Regs& r) {
        Rec(0x8F00, {r.args[0]});
        return g_maxWheelSpeed;
    });
    g_memberThunk = s.Thunk(0xC, [](Regs& r) {
        Rec(0x0030, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 1u;
    });
}

// A fresh object at kObject (0xCD), its animation component's vtable, the model and skeleton components.
CViewerObject* SetUpObject()
{
    auto* object = Ptr<CViewerObject>(kObject);
    std::memset(object, 0xCD, sizeof(CViewerObject));
    *Ptr<uint32_t>(kComponent) = At(kComponentVtable);
    reinterpret_cast<void**>(At(kComponentVtable))[12] = g_memberThunk;
    *Ptr<uint32_t>(kModelComponent + 0x208) = At(kNxModel);
    *Ptr<uint32_t>(kSkeletonComponent + 0x18) = At(kSkeleton);
    *Ptr<uint32_t>(kSkeleton + 4) = At(kBones);
    GameVariable<uint32_t>(0x006918DC) = At(kAssets);
    return object;
}

// ReloadAnimation, SetRotation, SetAnimSpeed, StepAnimation.
void TestViewerObject4()
{
    SaveGlobals({{0x006918DC, 4, {}}});
    StandIns* s = new StandIns;
    InstallAnimation(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        CViewerObject* object = SetUpObject();
        for (int withComponent = 0; withComponent < 2; ++withComponent) {
            g_components.clear();
            if (withComponent)
                g_components[0x7149B753] = kComponent;
            // An animation reloaded: by its name (the file's checksum), and by checksum (loaded or not).
            for (uint32_t anim : {0u, 0x1235u, 0x1236u}) {
                const char* file = anim ? "anims\\ollie.ska" : "anims\\kick.ska";
                original ? Original<void(__thiscall*)(void*, const char*, uint32_t)>(0x00589FD0)(object, file, anim)
                         : object->ReloadAnimation(file, anim);
            }
            // Turned.
            const float angles[][2] = {{0.0f, 0.0f}, {1.25f, -0.5f}, {-3.0f, 2.75f}};
            for (const auto& a : angles) {
                original ? Original<void(__thiscall*)(void*, float, float)>(0x0058A2E0)(object, a[0], a[1])
                         : object->SetRotation(a[0], a[1]);
                for (int i = 0; i < 16; ++i)
                    t.push_back(Bits(reinterpret_cast<const float*>(&object->m_matrix)[i]));
            }
            // Speeds: in range, above 1, below 0, exactly 1 and 0, NaN.
            const float speeds[] = {0.5f, 2.0f, -1.0f, 1.0f, 0.0f, std::bit_cast<float>(0x7FC00001u), 0.75f};
            for (float speed : speeds) {
                object->m_paused = true;
                original ? Original<void(__thiscall*)(void*, float)>(0x0058A360)(object, speed) : object->SetAnimSpeed(speed);
                t.push_back(Bits(object->m_speed));
                t.push_back(object->m_paused);
            }
            // Steps: forward and back, from playing (the speed saved) and from paused.
            for (int variant = 0; variant < 4; ++variant) {
                object->m_paused = variant >= 2;
                object->m_speed = 0.625f;
                object->m_savedSpeed = 0.125f;
                const bool forward = variant & 1;
                original ? Original<void(__thiscall*)(void*, bool)>(0x0058A4A0)(object, forward)
                         : object->StepAnimation(forward);
                t.push_back(Bits(object->m_speed));
                t.push_back(Bits(object->m_savedSpeed));
                t.push_back(object->m_paused);
            }
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: reload, rotation, speed, steps");
}

// SpinWheels and UpdatePanel.
void TestViewerObject5()
{
    SaveGlobals({{0x006918DC, 4, {}}, {0x007CE590, 4, {}}, {0x0067F384, 4, {}}});
    StandIns* s = new StandIns;
    InstallAnimation(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        CViewerObject* object = SetUpObject();
        GameVariable<uint32_t>(0x0067F384) = 30;
        GameVariable<float>(0x007CE590) = 350.0f;
        // The hierarchy's matrices: distinct values.
        for (int i = 0; i < 8 * 0x50 / 4; ++i)
            Ptr<float>(kHierarchy)[i] = static_cast<float>((i * 7) % 23) * 0.125f - 1.0f;
        // Wheels: no model; no skeleton (twice: the counter wraps); 5 bones (the angle only); 7 bones, with the
        // angle past 360 (several times), below 0, NaN.
        struct Case {
            bool model, skeleton;
            int bones;
            float maxSpeed, speed;
        };
        const Case cases[] = {
            {false, true, 7, 10.0f, 1.0f},   {true, false, 7, 10.0f, 1.0f}, {true, false, 7, 10.0f, 1.0f},
            {true, true, 5, 30.0f, 1.0f},    {true, true, 7, 45.5f, 0.75f}, {true, true, 7, 2000.0f, 1.0f},
            {true, true, 7, -1234.5f, 0.5f}, {true, true, 6, 3.0f, 0.25f},
            {true, true, 7, 1234.567f, 0.3f}, {true, true, 6, 0.0f, 1.0f},
            {true, true, 7, std::bit_cast<float>(0x7FC00000u), 1.0f},
        };
        for (const Case& c : cases) {
            g_components.clear();
            if (c.model)
                g_components[0x286A8D26] = kModelComponent;
            if (c.skeleton)
                g_components[0x222756D5] = kSkeletonComponent;
            if (c.maxSpeed == 0.0f)
                GameVariable<float>(0x007CE590) = 360.0f; // (exactly 360 stays)
            g_boneCount = c.bones;
            g_maxWheelSpeed = c.maxSpeed;
            object->m_speed = c.speed;
            original ? Original<void(__thiscall*)(void*)>(0x0058A720)(object) : object->SpinWheels();
            t.push_back(GameVariable<uint32_t>(0x0067F384));
            t.push_back(GameVariable<uint32_t>(0x007CE590));
            if (c.maxSpeed != c.maxSpeed)
                GameVariable<float>(0x007CE590) = 100.0f;
            for (int i = 0; i < 8 * 16; ++i)
                t.push_back(Bits(Ptr<float>(kBones)[i]));
        }
        // The panel: no animation component; panel off; the animation unknown (0); shown, playing and paused;
        // then without a model, a model with no bones (no wheels), with bones (the wheels for real).
        GameVariable<float>(0x007CE590) = 10.0f;
        for (int variant = 0; variant < 7; ++variant) {
            g_components.clear();
            if (variant != 0)
                g_components[0x7149B753] = kComponent;
            if (variant >= 5)
                g_components[0x286A8D26] = kModelComponent;
            g_components[0x222756D5] = kSkeletonComponent;
            g_boneCount = variant == 5 ? 0 : 7;
            object->m_panelShown = variant != 1;
            object->m_paused = variant == 4;
            object->m_speed = 0.375f;
            g_timeline30 = variant == 2 ? 0 : 0xA11A;
            g_timeline18 = 1.2345f + static_cast<float>(variant);
            g_length = 3.5f + static_cast<float>(variant) * 0.01f;
            g_maxWheelSpeed = 20.0f;
            original ? Original<void(__thiscall*)(void*)>(0x0058A8B0)(object) : object->UpdatePanel();
            t.push_back(GameVariable<uint32_t>(0x007CE590));
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: wheels and panel");
}

// OwnUpdate (its calls stood in).
void TestViewerObject6()
{
    SaveGlobals({{0x007CE4A8, 4, {}}});
    StandIns* s = new StandIns;
    InstallCommon(*s);
    s->At({0x0058A360, AddressOf(&CViewerObject::SetAnimSpeed)}, 4, [](Regs& r) {
        Rec(0xA360, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x0058A4A0, AddressOf(&CViewerObject::StepAnimation)}, 4, [](Regs& r) {
        Rec(0xA4A0, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s->At({0x0058A8B0, AddressOf(&CViewerObject::UpdatePanel)}, 0, [](Regs& r) {
        Rec(0xA8B0, {r.ecx});
        return 0u;
    });
    s->At({0x00576EA0, AddressOf(&Mdl::GetViewer)}, 0, [](Regs&) {
        Rec(0x6EA0, {});
        return reinterpret_cast<uint32_t>(g_viewer);
    });
    s->At({0x00577260, AddressOf(&Mdl::Viewer::AddCommands)}, 4, [](Regs& r) {
        Rec(0x7260, {r.ecx, r.args[0]});
        return 0u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        auto* object = Ptr<CViewerObject>(kObject);
        std::memset(object, 0xCD, sizeof(CViewerObject));
        object->m_speed = 0.5f;
        object->m_savedSpeed = 0.875f;
        const uint32_t commands[] = {0, 0x1000, 0x40, 0x80, 0x100, 0x200, 0x20000, 0x2000, 0x4000, 0x8000, 0x10000,
                                     0x3FFFF, 0x3FFFF, 0x1000 | 0x40 | 0x200 | 0x8000};
        int i = 0;
        for (uint32_t c : commands) {
            object->m_commands = c;
            object->m_paused = (i & 1) != 0;
            g_viewer = (i % 3 == 2) ? nullptr : Ptr<Mdl::Viewer>(0x6000);
            original ? Original<void(__thiscall*)(void*)>(0x0058AB30)(object) : object->OwnUpdate();
            t.push_back(object->m_commands);
            t.push_back(object->m_panelShown);
            ++i;
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: its own update");
}

// The input handler over a run of frames (the auto-repeat static included), then the PC tool's messages.
void TestViewerObject7()
{
    SaveGlobals({{0x007CE588, 8, {}}});
    StandIns* s = new StandIns;
    InstallCommon(*s);
    InstallSprintf(*s);
    s->At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        auto it = g_components.find(r.args[0]);
        return it == g_components.end() ? 0u : At(it->second);
    });
    s->At({0x005897D0, AddressOf(&CViewerObject::PlayAnimation)}, 4, [](Regs& r) {
        Rec(0x97D0, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x0058A360, AddressOf(&CViewerObject::SetAnimSpeed)}, 4, [](Regs& r) {
        Rec(0xA360, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x0058A4A0, AddressOf(&CViewerObject::StepAnimation)}, 4, [](Regs& r) {
        Rec(0xA4A0, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s->At({0x00589FD0, AddressOf(&CViewerObject::ReloadAnimation)}, 8, [](Regs& r) {
        Rec(0x9FD0, {r.ecx, Text(r.args[0]), r.args[1]});
        return 0u;
    });
    s->At({0x005ADEC0, AddressOf(CFuncs::CFunc_ReloadSkaterCamAnim)}, 0, [](Regs& r) {
        Rec(0xDEC0, {r.args[0], r.args[1]});
        return 1u;
    });
    s->At({0x0046EDE0, AddressOf(&Script::LoadQB)}, 0, [](Regs& r) {
        Rec(0xEDE0, {Text(r.args[0]), r.args[1], r.args[2]});
        return 0u;
    });
    s->At({0x00475670, AddressOf(static_cast<Script::CScript* (*)(const char*, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                  bool, bool, bool, bool)>(&Script::SpawnScript))},
          0, [](Regs& r) {
              Rec(0x5670, {Text(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                           r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
              return At(0x7800);
          });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        GameVariable<uint32_t>(0x007CE58C) = 0; // the static not yet set
        GameVariable<uint32_t>(0x007CE588) = 0x77;
        g_integers.clear();
        g_integers[0xE60442D5] = 3; // ViewerAutoRepeatTime
        g_integers[0x2DA212E5] = 2; // ViewerAutoRepeatSpeed
        auto* object = Ptr<CViewerObject>(kObject);
        std::memset(object, 0xCD, sizeof(CViewerObject));
        auto* handler = Ptr<Obj::ViewerObjectInputHandler>(0x4600);
        auto* data = Ptr<Inp::Data>(0x4700);
        handler->m_data = object;
        handler->m_controlData = data;
        struct Frame {
            uint32_t makes, buttons;
            bool animation;
        };
        const Frame frames[] = {
            {0x10 | 0x8000, 0x8000, true}, {0, 0x8000, true}, {0, 0x8000, true}, {0, 0x8000, true}, {0, 0x8000, true},
            {0, 0x8000, true},             {0, 0x8000, true}, {0, 0, true},      {0x2000, 0x2000, true},
            {0, 0x2000, true},             {0, 0x2000, true}, {0, 0x2000, true}, {0, 0x2000, true},
            {0x8000 | 0x2000, 0, true},    {0, 0x8000 | 0x2000, true}, {0x200 | 0x4, 0x8 | 0x2, true},
            {0x1, 0x8, true},              {0x5, 0x2, true},  {0, 0x20 | 0x80, false}, {0, 0x80, false},
            {0, 0x20, true},               {0xFFFFFFFF, 0xFFFFFFFF, false}, {0, 0, false},
        };
        for (const Frame& f : frames) {
            data->m_makes = f.makes;
            data->m_buttons = f.buttons;
            g_components.clear();
            if (f.animation)
                g_components[0x7149B753] = kComponent;
            object->m_commands = 0;
            original ? Original<void(__cdecl*)(void*)>(0x0058A060)(handler) : Obj::HandleViewerObjectInput(handler);
            t.push_back(object->m_commands);
            t.push_back(GameVariable<uint32_t>(0x007CE588));
            t.push_back(GameVariable<uint32_t>(0x007CE58C));
        }
        // The messages.
        auto* context = Ptr<Net::MsgHandlerContext>(0x4800);
        char* message = Ptr<char>(0x4900);
        context->m_Msg = message;
        context->m_data = object;
        strcpy_s(message, 0x80, "ollie_anim");
        *reinterpret_cast<uint32_t*>(message + 0x80) = 0xBEEF0123;
        // (the first word read as a checksum, a float and a byte)
        using MessageCode = int(__cdecl*)(Net::MsgHandlerContext*);
        const struct {
            uint32_t address;
            MessageCode ours;
        } handlers[] = {
            {0x00589840, &Obj::HandlePlayAnimation},      {0x0058ACA0, &Obj::HandleSetAnimSpeed},
            {0x0058ACC0, &Obj::HandleStepAnimation},      {0x0058A1F0, &Obj::HandleReloadAnimation},
            {0x0058ACE0, &Obj::HandleReloadCamAnimation}, {0x0058A210, &Obj::HandleRunAnimViewScript},
        };
        for (const auto& h : handlers) {
            for (int variant = 0; variant < 2; ++variant) {
                if (variant)
                    message[0] = 0; // (a zero first byte: a step back)
                else
                    strcpy_s(message, 0x80, "ollie_anim");
                // The spawned script's object pointer: holding another object the first time.
                auto* script = Ptr<Script::CScript>(0x7800);
                std::memset(script, 0, sizeof(Script::CScript));
                if (!variant) {
                    script->m_object.mp_object = Ptr<Obj::CObject>(0x7600);
                    *Ptr<uint32_t>(0x7600 + 4) = At(0x7800 + 0xBC); // its list: this pointer only
                }
                *Ptr<uint32_t>(kObject + 4) = 0; // the viewer object's smart pointer list: empty
                int r = original ? Original<MessageCode>(h.address)(context) : h.ours(context);
                t.push_back(static_cast<uint32_t>(r));
            }
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: the input handler and the PC tool's messages");
}

// The constructor, with and without a net handler.
void TestViewerObject8()
{
    SaveGlobals({{0x00696D40, 4, {}}});
    StandIns* s = new StandIns;
    InstallCommon(*s);
    Alias(&Obj::HandleViewerObjectInput, 0x0058A060);
    Alias(&Obj::HandlePlayAnimation, 0x00589840);
    Alias(&Obj::HandleSetAnimSpeed, 0x0058ACA0);
    Alias(&Obj::HandleStepAnimation, 0x0058ACC0);
    Alias(&Obj::HandleReloadAnimation, 0x0058A1F0);
    Alias(&Obj::HandleReloadCamAnimation, 0x0058ACE0);
    Alias(&Obj::HandleRunAnimViewScript, 0x0058A210);
    s->At({0x0045E510, AddressOf(&Obj::CCompositeObject::Construct)}, 0, [](Regs& r) {
        Rec(0xE510, {r.ecx});
        return r.ecx;
    });
    s->At({0x0057B250, AddressOf(&Obj::AddBaseComponents)}, 0, [](Regs& r) {
        Rec(0xB250, {r.args[0]});
        return 0u;
    });
    s->At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Rec(0x1970, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, [](Regs& r) {
        Rec(0x6890, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s->At({0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, [](Regs& r) {
        Rec(0x6C90, {r.ecx, r.args[0]});
        return 0u;
    });
    s->At({0x0045A5C0, AddressOf(&Net::Dispatcher::AddHandler)}, 0x14, [](Regs& r) {
        Rec(0xA5C0, {r.ecx, r.args[0] & 0xFF, r.args[1], r.args[2], r.args[3], r.args[4]});
        return At(0x7A00);
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        GameVariable<uint32_t>(0x00696D40) = At(0x8000);
        *Ptr<uint32_t>(0x8000 + 4) = At(0x8800); // controller 0's device
        for (int variant = 0; variant < 2; ++variant) {
            auto* object = Ptr<CViewerObject>(kObject);
            std::memset(object, 0xCD, sizeof(CViewerObject));
            Net::App* app = variant ? Ptr<Net::App>(0x9000) : nullptr;
            void* r = original ? Original<void*(__thiscall*)(void*, void*)>(0x0058ADB0)(object, app)
                               : object->Construct(app);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
            PushArena(t);
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "viewer object: the constructor");
}
} // namespace

SELFTEST(Early, TestViewerObject1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject2, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject3, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject4, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject5, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject6, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject7, ViewerTestKit::Cleanup);
SELFTEST(Early, TestViewerObject8, ViewerTestKit::Cleanup);
