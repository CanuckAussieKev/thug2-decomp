// Self-tests for the objsys agent's batch OS-11: the cutscene's objects, assets, lights, camera and poses
// (src/game/obj/cutscene_data.cpp, 0x00463AA0-0x0046449B). The original and ours on the same random cutscene data,
// with every game function they call stood in for (the game's and ours): structures and arrays answering from a
// hash of what's asked, objects, components, models and skeletons as recording memory, the asset and memory
// managers, scripts, the camera's animations; every call, result and the cutscene data compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/obj/cutscene.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/objsys_testkit.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Script::CStruct;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
uint32_t FB(float f)
{
    return std::isnan(f) ? 0x7FC00000u : Bits(f);
}
uint32_t Rel(uint32_t word)
{
    return OnStack(word) ? 0x57AC0000u : word;
}

uint32_t g_seed;
bool g_componentsAlways; // (DestroyObjects uses the objects and components it finds unchecked)
Lcg g_answers(1);
// A deterministic answer for a question (what, who, which) in this seed.
uint32_t Hash(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = g_seed * 0x9E3779B1u ^ a * 0x85EBCA77u ^ b * 0xC2B2AE3Du ^ c * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

// (A value asked for with the assert flag is always there: the game would stop otherwise, and the original
// leaves its variable uninitialised.)
// Fake script values: structures are numbers (never dereferenced: every structure call is stood in for); arrays
// are real CArray-shaped memory.
constexpr uint32_t kFakeStruct = 0x5C000000;
alignas(16) uint32_t g_arrays[4][3];
// Objects, components, models, skeletons.
alignas(16) uint8_t g_objects[6][0x120];
alignas(16) uint8_t g_components[6][0x220];
alignas(16) uint8_t g_skeletonData[2][0x600];
alignas(16) uint8_t g_skeletons[2][0x20];
alignas(16) uint8_t g_basePose[0x400];
alignas(16) uint8_t g_animData[4][0x220];
alignas(16) uint8_t g_animEntries[4][0x10];
alignas(16) uint8_t g_manager[0x200];
alignas(16) uint8_t g_camera[0x100];
alignas(16) uint8_t g_objectMemory[0x110];
void* g_objectVtable[16];
void* g_componentVtable[16];
void* g_managerVtable[4];
alignas(16) uint8_t g_objectManager[8];
const char* const kModelNames[] = {"model_a", "model_b", "skater_c"};

constexpr uint32_t kDataSize = 0x34D0;
alignas(16) uint8_t g_data[kDataSize];

uint32_t Object(int i)
{
    return reinterpret_cast<uint32_t>(g_objects[i]);
}

// A checksum value: for the kinds and models the cutscene loader knows, those names too.
uint32_t ChecksumAnswer(uint32_t name, uint32_t h)
{
    static const uint32_t kKinds[] = {0x01815FB3, 0x6218C3C9, 0x9F000001, 0x6218C3C9};
    static const uint32_t kModels[] = {0x5B8AB877, 0x67E6859A, 0x9F000002, 0x9F000003};
    if (name == 0x7321A8D6)
        return kKinds[h % 4];
    if (name == 0xB974F2FC)
        return kModels[h % 4];
    return 0x9F000000 + h % 4;
}

void StandInScript(StandIns& s)
{
    using GetChecksumFn = bool (CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&CStruct::GetChecksum))}, 12, [](Regs& r) {
        uint32_t h = Hash(1, r.ecx, r.args[0]);
        uint32_t found = h % 5 != 0 || r.args[2] != 0;
        if (found)
            *reinterpret_cast<uint32_t*>(r.args[1]) = ChecksumAnswer(r.args[0], h >> 8);
        Record({0x47695000, r.ecx, r.args[0], r.args[2], found});
        return found;
    });
    using GetStringFn = bool (CStruct::*)(uint32_t, const char**, int);
    s.At({0x00476590, AddressOf(static_cast<GetStringFn>(&CStruct::GetString))}, 12, [](Regs& r) {
        uint32_t h = Hash(2, r.ecx, r.args[0]);
        uint32_t found = h % 4 != 0 || r.args[2] != 0;
        if (found)
            *reinterpret_cast<const char**>(r.args[1]) = kModelNames[(h >> 8) % 3];
        Record({0x47659000, r.ecx, r.args[0], r.args[2], found});
        return found;
    });
    using GetIntegerFn = bool (CStruct::*)(uint32_t, int*, int);
    s.At({0x00476630, AddressOf(static_cast<GetIntegerFn>(&CStruct::GetInteger))}, 12, [](Regs& r) {
        uint32_t h = Hash(3, r.ecx, r.args[0]);
        uint32_t found = h % 5 != 0 || r.args[2] != 0;
        static const int kValues[] = {0, 1, 0, 2, 37, 100, -5, 1, 255, 73, 51, 99, 0, 1, 7, 33};
        if (found)
            *reinterpret_cast<int*>(r.args[1]) = kValues[(h >> 8) % 16];
        Record({0x47663000, r.ecx, r.args[0], r.args[2], found});
        return found;
    });
    using GetStructureFn = bool (CStruct::*)(uint32_t, CStruct**, int);
    s.At({0x004767C0, AddressOf(static_cast<GetStructureFn>(&CStruct::GetStructure))}, 12, [](Regs& r) {
        uint32_t h = Hash(4, r.ecx, r.args[0]);
        uint32_t found = h % 4 != 0;
        if (found)
            *reinterpret_cast<uint32_t*>(r.args[1]) = kFakeStruct + 0x100 + (h >> 8) % 8;
        Record({0x4767C000, r.ecx, r.args[0], r.args[2], found});
        return found;
    });
    using AddIntegerFn = void (CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x477B8000, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    using GetArrayFn = Script::CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        uint32_t h = Hash(5, r.args[0], 0);
        // (the profiles array is never missing: SkaterModelName reads it unchecked)
        bool missing = h % 5 == 0 && r.args[0] != 0xCA1DB456;
        uint32_t array = missing ? 0 : reinterpret_cast<uint32_t>(g_arrays[(h >> 8) % 4]);
        Record({0x47907000, r.args[0], array}); // (not the assert flag: ours has none)
        return array;
    });
    using GetGlobalStructureFn = CStruct* (*)(uint32_t);
    s.At({0x00479020, AddressOf(static_cast<GetGlobalStructureFn>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x47902000, r.args[0]});
        return kFakeStruct + 0x200 + (r.args[0] & 0xFF);
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        uint32_t value = (Hash(6, r.ecx, r.args[0]) % 3 == 0) ? Object(static_cast<int>(r.args[0] % 6))
                                                              : kFakeStruct + 0x300 + r.args[0];
        Record({0x46CC3000, r.ecx, r.args[0], value});
        return value;
    });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        Record({0x45BE4000, r.args[0]});
        return 0x40DE0000 + r.args[0];
    });
    using RunScriptFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunScriptFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x47579000, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
}

void StandInObjects(StandIns& s)
{
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        uint32_t h = Hash(7, r.args[0], 0);
        uint32_t object = h % 5 == 0 && !g_componentsAlways ? 0 : Object(static_cast<int>((h >> 8) % 6));
        Record({0x460AD000, r.args[0], object});
        return object;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        uint32_t h = Hash(8, r.ecx, r.args[0]);
        uint32_t component =
            h % 6 == 0 && !g_componentsAlways ? 0 : reinterpret_cast<uint32_t>(g_components[(h >> 8) % 6]);
        Record({0x45DB9000, r.ecx, r.args[0], component});
        return component;
    });
    s.At({0x00420100}, 0, [](Regs& r) {
        Record({0x42010000, r.ecx});
        return 0x1D000000 + (r.ecx & 0xFFF);
    });
    for (uint32_t address : {0x00492E80u, 0x00494B70u, 0x00492E60u, 0x00493460u, 0x00493C60u, 0x004A3090u}) {
        s.At({address}, 4, [address](Regs& r) {
            Record({address, r.ecx, Rel(r.args[0])});
            if (address == 0x00492E80) { // (an offset vector)
                const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
                Record({v[0], v[1], v[2], v[3]});
            }
            return 0u;
        });
    }
    s.At({0x00492E50}, 0, [](Regs& r) {
        Record({0x492E5000, r.ecx});
        return 0x3D000000 + (r.ecx & 0xFFF);
    });
    s.At({0x004A2BA0}, 0, [](Regs& r) {
        uint32_t count = Hash(9, r.ecx, 0) % 65;
        Record({0x4A2BA000, r.ecx, count});
        return count;
    });
    s.At({0x00485730}, 0x18, [](Regs& r) {
        Record({0x48573000, r.ecx, r.args[0], r.args[1], r.args[2], FB(*reinterpret_cast<float*>(&r.args[3])),
                r.args[4], r.args[5], HashBytes(&GameVariable<uint8_t>(0x006F1960), 0x400)});
        return 0u;
    });
    s.At({0x004A4240}, 8, [](Regs& r) {
        Record({0x4A424000, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    // the virtuals: an object's destructor (slot 0) and slot 8, a component's slot 1, the object manager's slot 1
    for (int i = 0; i < 16; ++i) {
        g_objectVtable[i] = s.Thunk(i == 0 ? 4 : 0, [i](Regs& r) {
            Record({0x0B1E0000u + static_cast<uint32_t>(i), r.ecx, i == 0 ? r.args[0] : 0});
            return 0u;
        });
    }
    // (a component's slot 7 takes one argument, slot 13 four)
    for (int i = 0; i < 16; ++i) {
        int count = i == 7 ? 1 : i == 13 ? 4 : 0;
        g_componentVtable[i] = s.Thunk(static_cast<uint16_t>(4 * count), [i, count](Regs& r) {
            Record({0xC0390000u + static_cast<uint32_t>(i), r.ecx});
            for (int k = 0; k < count; ++k)
                Record({r.args[k]});
            return 0u;
        });
    }
    for (int i = 0; i < 4; ++i) {
        g_managerVtable[i] = s.Thunk(4, [i](Regs& r) {
            Record({0x0B3A0000u + static_cast<uint32_t>(i), r.ecx, r.args[0]});
            return 0u;
        });
    }
    // making an object
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        std::memset(g_objectMemory, 0, sizeof(g_objectMemory));
        Record({0x401CC000, r.args[0]});
        return reinterpret_cast<uint32_t>(g_objectMemory);
    });
    s.At({0x0045E510, AddressOf(&Obj::CCompositeObject::Construct)}, 0, [](Regs& r) {
        *reinterpret_cast<void***>(r.ecx) = g_objectVtable;
        Record({0x45E51000, r.ecx});
        return r.ecx;
    });
    s.At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Record({0x46197000, r.ecx, r.args[0]});
        return 0u;
    });
    // assets
    s.At({0x00408060}, 0x24, [](Regs& r) {
        Record({0x40806000, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6],
                r.args[7], r.args[8]});
        return 0xA55E0000u;
    });
    s.At({0x0048CF50}, 0, [](Regs& r) {
        uint32_t value = Hash(10, r.args[0], 0) % 3 == 0 ? 0 : 0xA41D0000 + (r.args[0] & 0xFF);
        Record({0x48CF5000, r.args[0], r.args[1], value});
        return value;
    });
    // lights
    s.At({0x00490570}, 0, [](Regs& r) {
        Record({0x49057000, r.args[0]});
        return 0u;
    });
    s.At({0x0048FCA0}, 0, [](Regs& r) {
        Record({0x48FCA000, r.args[0]});
        return 0u;
    });
    // memory
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0x5BB0E000, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0x5BB11000, r.ecx});
        return 0u;
    });
    s.At({0x005BB050, AddressOf(&Mem::Manager::Available)}, 0, [](Regs& r) {
        Record({0x5BB05000, r.ecx});
        return 0x00400000u + (g_seed << 12);
    });
    s.At({0x005BB780, AddressOf(&Mem::Manager::InitCutsceneHeaps)}, 4, [](Regs& r) {
        Record({0x5BB78000, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0049AA80}, 4, [](Regs& r) {
        Record({0x49AA8000, r.ecx, r.args[0]});
        return 0u;
    });
    // the camera
    s.At({0x00485A50, AddressOf(&Gfx::Camera::StoreOldPos)}, 0, [](Regs& r) {
        Record({0x485A5000, r.ecx});
        return 0u;
    });
    s.At({0x004844C0}, 0x14, [](Regs& r) {
        auto* rotation = reinterpret_cast<float*>(r.args[0]);
        auto* translation = reinterpret_cast<float*>(r.args[1]);
        Record({0x4844C000, r.ecx, FB(rotation[0]), FB(rotation[3]), Rel(r.args[2]),
                FB(*reinterpret_cast<float*>(&r.args[3])), r.args[4]});
        for (int i = 0; i < 4; ++i) {
            rotation[i] = static_cast<float>(static_cast<int>(g_answers.Below(2001)) - 1000) / 1000.0f;
            translation[i] = static_cast<float>(g_answers.Below(1000));
        }
        *reinterpret_cast<uint8_t*>(r.args[2]) = static_cast<uint8_t>(g_answers.Below(2));
        return 0u;
    });
    s.At({0x00482F40}, 0x10, [](Regs& r) {
        Record({0x482F4000, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
}

void RandomWorld(Lcg& random)
{
    for (auto& a : g_arrays) {
        a[0] = 0x0A00;
        a[1] = random.Below(6);
        a[2] = 0xBAD0BAD0;
    }
    for (auto& o : g_objects) {
        for (auto& b : o)
            b = static_cast<uint8_t>(random.Next());
        *reinterpret_cast<void***>(o) = g_objectVtable;
    }
    for (int i = 0; i < 6; ++i) {
        uint8_t* c = g_components[i];
        for (int k = 0; k < 0x220; ++k)
            c[k] = static_cast<uint8_t>(random.Next());
        *reinterpret_cast<void***>(c) = g_componentVtable;
        *reinterpret_cast<uint32_t*>(c + 0x18) = reinterpret_cast<uint32_t>(g_skeletons[i % 2]);
        *reinterpret_cast<uint32_t*>(c + 0x208) = random.Below(4) == 0 ? 0 : 0x30DE1000 + i;
    }
    for (int i = 0; i < 2; ++i) {
        uint8_t* sk = g_skeletons[i];
        *reinterpret_cast<uint32_t*>(sk + 8) = reinterpret_cast<uint32_t>(g_skeletonData[i]);
        *reinterpret_cast<uint32_t*>(sk + 0xC) = random.Next();
        *reinterpret_cast<uint32_t*>(g_skeletonData[i] + 0x590) = reinterpret_cast<uint32_t>(g_basePose);
    }
    for (auto& b : g_basePose)
        b = static_cast<uint8_t>(random.Next());
    for (int i = 0; i < 4; ++i) {
        std::memset(g_animData[i], 0xAB, sizeof(g_animData[i]));
        *reinterpret_cast<uint32_t*>(g_animEntries[i] + 4) = reinterpret_cast<uint32_t>(g_animData[i]);
        *reinterpret_cast<uint32_t*>(g_animEntries[i] + 8) = 0xA417A000 + i;
    }
    std::memset(&GameVariable<uint8_t>(0x006F1160), 0x77, 0x1000); // (the pose buffers)
    // the cutscene data
    for (auto& b : g_data)
        b = static_cast<uint8_t>(random.Next());
    auto& data = *reinterpret_cast<Obj::CCutsceneData*>(g_data);
    data.m_objectCount = static_cast<int>(random.Below(6));
    for (int i = 0; i < 8; ++i) {
        auto& e = data.m_objects[i];
        e.active = random.Below(2) != 0;
        e.animation = random.Below(3)
                          ? reinterpret_cast<Obj::CCutsceneData::AnimationEntry*>(g_animEntries[random.Below(4)])
                          : nullptr;
        e.object = random.Below(5) ? reinterpret_cast<Obj::CCompositeObject*>(Object(static_cast<int>(random.Below(6))))
                                   : nullptr;
        if (e.active || e.animation)
            e.object = reinterpret_cast<Obj::CCompositeObject*>(Object(static_cast<int>(random.Below(6))));
    }
    static const int kCounts[] = {0, 3, 100};
    data.m_animationCount = random.Pick(kCounts);
    data.m_assetCount = random.Pick(kCounts);
    for (int i = 0; i < 256; ++i)
        data.m_animations[i].data = random.Below(2) ? nullptr : reinterpret_cast<void*>(0xDA7A0000 + i);
    static const float kTimes[] = {0.0f, 0.05f, 1.5f, 10.0f, 0.0833f, 0.083333336f, 0.0f};
    data.m_time = random.Pick(kTimes);
    data.m_unknown3480 = random.Pick(kTimes);
    data.m_cameraStart = random.Pick(kTimes);
    data.m_flag34B0 = random.Below(4) != 0;
    data.m_skipped = random.Below(3) == 0;
    // the managers
    std::memset(g_manager, 0, sizeof(g_manager));
    *reinterpret_cast<uint32_t*>(g_manager + 0x110) = 0x4EA90110;
    *reinterpret_cast<uint32_t*>(g_manager + 0x14C) = 0x4EA9014C;
    *reinterpret_cast<void***>(g_objectManager) = g_managerVtable;
    for (auto& b : g_camera)
        b = static_cast<uint8_t>(random.Next());
}

void PushData()
{
    for (uint32_t i = 0; i < kDataSize; i += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_data + i));
    for (uint32_t i = 0; i < 0x120 * 6; i += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(&g_objects[0][0] + i));
    for (uint32_t i = 0; i < 0x100; i += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_camera + i));
    for (int i = 0; i < 4; ++i)
        g_trace.push_back(HashBytes(g_animData[i], sizeof(g_animData[i])));
    g_trace.push_back(GameVariable<uint8_t>(0x006F1158));
    for (uint32_t i = 0; i < sizeof(g_objectMemory); i += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_objectMemory + i));
    g_trace.push_back(HashBytes(&GameVariable<uint8_t>(0x006F1960), 0x400));
}

void TestCutsceneObjects()
{
    ObjsysTests::SaveGlobals({{0x006F1158, 1}, {0x006F1160, 0x1000}, {0x007D1800, 4}, {0x006F0FB8, 4},
                              {0x006918DC, 4}, {0x006FA7C0, 4}});
    StandIns& s = *new StandIns();
    StandInScript(s);
    StandInObjects(s);
    GameVariable<uint32_t>(0x007D1800) = reinterpret_cast<uint32_t>(g_manager);
    GameVariable<uint32_t>(0x006F0FB8) = reinterpret_cast<uint32_t>(g_objectManager);
    GameVariable<uint32_t>(0x006918DC) = 0xA55E7000;
    GameVariable<uint32_t>(0x006FA7C0) = 0x0B1A7C00;
    for (uint32_t seed = 1; seed <= 120; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_seed = seed;
            Lcg random(0x5FE00000 + seed);
            g_answers = Lcg(0x5FEA0000 + seed);
            RandomWorld(random);
            GameVariable<uint8_t>(0x006F1158) = 1;
            void* data = g_data;
            for (int step = 0; step < 10; ++step) {
                switch (random.Below(13)) {
                case 0: {
                    alignas(16) float quat[4], translation[4], matrix[16];
                    for (int i = 0; i < 4; ++i) {
                        quat[i] = static_cast<float>(static_cast<int>(random.Below(2001)) - 1000) / 997.0f;
                        translation[i] = static_cast<float>(random.Below(500));
                    }
                    std::memset(matrix, 0xCC, sizeof(matrix));
                    Fn<void(__cdecl*)(float*, const float*, float*)>(original, 0x00463AA0)(quat, translation, matrix);
                    for (float f : quat)
                        Record({FB(f)});
                    for (float f : matrix)
                        Record({FB(f)});
                    break;
                }
                case 1: {
                    const char* name = Fn<const char*(__cdecl*)(uint32_t)>(original, 0x00463BA0)(
                        kFakeStruct + random.Below(16));
                    Record({0xA3E00000, name ? TestStandIns::Hash(name) : 0});
                    break;
                }
                case 2: {
                    alignas(16) float colour[4] = {9, 9, 9, 9};
                    float* result = Fn<float*(__cdecl*)(float*, uint32_t, uint32_t)>(original, 0x00463DA0)(
                        colour, kFakeStruct + random.Below(16), 0xC0100000 + random.Below(8));
                    Record({result == colour, FB(colour[0]), FB(colour[1]), FB(colour[2]), FB(colour[3])});
                    break;
                }
                case 3:
                    Fn<void(__cdecl*)()>(original, random.Below(2) ? 0x00463E80 : 0x00463ED0)();
                    break;
                case 4:
                    g_componentsAlways = true;
                    Fn<void(__thiscall*)(void*)>(original, 0x00463C70)(data);
                    g_componentsAlways = false;
                    break;
                case 5:
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x00463F20)(
                        data, 0xA5000000 + random.Below(50), random.Next(), random.Next());
                    break;
                case 6:
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x00463F70)(
                        data, 0xA5000000 + random.Below(50), random.Next(), random.Next());
                    break;
                case 7:
                    Record({Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00464000)(
                        data, kFakeStruct + random.Below(16))});
                    break;
                case 8:
                    Fn<void(__cdecl*)()>(original, 0x004640D0)();
                    break;
                case 9:
                    Fn<void(__thiscall*)(void*)>(original, 0x004641A0)(data);
                    break;
                case 10:
                    Fn<void(__thiscall*)(void*, void*)>(original, 0x00464220)(data, g_camera);
                    break;
                default:
                    Fn<void(__thiscall*)(void*)>(original, 0x00464360)(data);
                    break;
                }
            }
            PushData();
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "cutscene data: objects, assets, camera, poses");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    // the unreachable identity rotation: ours alone
    alignas(16) float q[4] = {5, 6, 7, 8};
    reinterpret_cast<Obj::IdentityQuat*>(q)->Set();
    Check(q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && q[3] == 1.0f, "cutscene: identity rotation");
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestCutsceneObjects, ObjsysTests::CleanUp);

// --- OS-12: the data's constructor and adding objects ----------------------------------------------------------

// The memory manager: blocks from one arena (the same addresses in both runs).
alignas(16) uint8_t g_arena[0x8000];
uint32_t g_arenaUsed;
alignas(16) uint8_t g_symbol[0x20];
alignas(16) uint8_t g_animList[0x10];
alignas(16) uint8_t g_models[6][0x50];

void PushState(uint32_t from, uint32_t to)
{
    for (uint32_t a = from; a < to; a += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(a));
}

void StandInLoading(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        uint32_t block = reinterpret_cast<uint32_t>(g_arena) + g_arenaUsed;
        g_arenaUsed += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xA5, r.args[0]);
        Record({0x3E3A0000, r.args[0], r.args[1], r.args[2], block});
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0x3E3F0000, r.args[0]});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x47641000, r.ecx});
        return r.ecx;
    });
    s.At({0x00477130, 0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x47713000, r.ecx});
        return 0u;
    });
    using GetArrayFn = bool (CStruct::*)(uint32_t, Script::CArray**, int);
    s.At({0x00476860, AddressOf(static_cast<GetArrayFn>(&CStruct::GetArray))}, 12, [](Regs& r) {
        uint32_t array = reinterpret_cast<uint32_t>(g_arrays[Hash(11, r.ecx, r.args[0]) % 4]);
        *reinterpret_cast<uint32_t*>(r.args[1]) = array;
        Record({0x47686000, r.ecx, r.args[0], r.args[2], array});
        return 1u;
    });
    using ContainsFlagFn = bool (CStruct::*)(uint32_t);
    s.At({0x00476B40, AddressOf(static_cast<ContainsFlagFn>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        // (the skater's parts are mostly not hidden: 0xC4C5B2CC)
        uint32_t h = Hash(12, r.ecx, r.args[0]);
        uint32_t yes = r.args[0] == 0xC4C5B2CC ? h % 4 == 0 : h % 3 != 0;
        Record({0x476B4000, r.ecx, r.args[0], yes});
        return yes;
    });
    using AddChecksumFn = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x477D4000, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    using GlobalIntegerFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GlobalIntegerFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        uint32_t value = Hash(13, r.args[0], 0) % 3;
        Record({0x478F6000, r.args[0], value});
        return value;
    });
    s.At({0x00478CC0, AddressOf(&Script::FindSymbol)}, 0, [](Regs& r) {
        Record({0x478CC000, r.args[0]});
        return reinterpret_cast<uint32_t>(g_symbol);
    });
    s.At({0x00461930, AddressOf(&Obj::CObject::SetID)}, 4, [](Regs& r) {
        Record({0x46193000, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00566520}, 0, [](Regs& r) {
        uint32_t skater = Hash(14, 0, 0) % 3 ? Object(5) : 0;
        Record({0x56652000, r.ecx, skater});
        return skater;
    });
    s.At({0x00407C00}, 8, [](Regs& r) {
        uint32_t asset = Hash(15, r.args[0], 0) % 2 ? 0xA55E0001 : 0;
        Record({0x407C0000, r.ecx, r.args[0], r.args[1], asset});
        return asset;
    });
    s.At({0x00576E60}, 0, [](Regs& r) {
        uint32_t face = Hash(16, r.ecx, 0) % 3 ? 0xFACE0000 : 0;
        Record({0x576E6000, r.ecx, face});
        return face;
    });
    s.At({0x00487510}, 0, [](Regs& r) {
        uint32_t yes = Hash(17, r.ecx, 0) % 2;
        Record({0x48751000, r.ecx, yes});
        return yes;
    });
    for (uint32_t address : {0x00489050u, 0x004890B0u}) {
        s.At({address}, 4, [address](Regs& r) {
            uint32_t part = Hash(address, r.args[0], 0) % 3 ? kFakeStruct + 0x400 + (r.args[0] & 0xF) : 0;
            Record({address, r.ecx, r.args[0], part});
            return part;
        });
    }
    s.At({0x00492F30}, 4, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        for (int i = 0; i < 4; ++i)
            out[i] = Hash(18, r.ecx, i);
        Record({0x492F3000, r.ecx});
        return r.args[0];
    });
    s.At({0x004200C0}, 0, [](Regs& r) {
        Record({0x4200C000, r.ecx});
        return 0u;
    });
    s.At({0x0048CF70}, 0, [](Regs& r) {
        Record({0x48CF7000, r.ecx});
        *reinterpret_cast<void***>(r.ecx) = g_objectVtable; // (a source can be deleted later)
        return r.ecx;
    });
    s.At({0x0048CFD0}, 4, [](Regs& r) {
        Record({0x48CFD000, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_animList); // (the animation: its length at +0, its count at +8)
    });
    for (uint32_t address : {0x0048CFA0u, 0x00493B90u}) {
        s.At({address}, 4, [address](Regs& r) {
            Record({address, r.ecx, r.args[0]});
            return 0u;
        });
    }
    s.At({0x004A2E20}, 8, [](Regs& r) {
        Record({0x4A2E2000, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00493B30}, 0, [](Regs& r) {
        uint32_t flags = 1u << (Hash(19, r.ecx, 0) % 8);
        Record({0x493B3000, r.ecx, flags});
        return flags;
    });
    s.At({0x00494200}, 0x10, [](Regs& r) {
        Record({0x49420000, r.ecx, r.args[0], FB(*reinterpret_cast<float*>(&r.args[1])),
                FB(*reinterpret_cast<float*>(&r.args[2])), FB(*reinterpret_cast<float*>(&r.args[3]))});
        return 0u;
    });
    s.At({0x00482F00}, 4, [](Regs& r) {
        uint32_t name = 0x9F000000 + (Hash(20, r.args[0], 0) % 5);
        Record({0x482F0000, r.ecx, r.args[0], name});
        return name;
    });
    // the bool-assert integer getter: as the plain one
    s.At({0x00476CB0}, 12, [](Regs& r) {
        uint32_t h = Hash(3, r.ecx, r.args[0]);
        uint32_t found = h % 5 != 0 || r.args[2] != 0;
        static const int kValues[] = {0, 1, 0, 2, 37, 100, -5, 1, 255, 73, 51, 99, 0, 1, 7, 33};
        if (found)
            *reinterpret_cast<int*>(r.args[1]) = kValues[(h >> 8) % 16];
        Record({0x47663000, r.ecx, r.args[0], r.args[2] & 0xFF, found});
        return found;
    });
}

void TestCutsceneLoading()
{
    ObjsysTests::SaveGlobals({{0x006F1158, 1}, {0x006F1160, 0x1000}, {0x007D1800, 4}, {0x006F0FB8, 4},
                              {0x006918DC, 4}, {0x006FA7C0, 4}, {0x006F3614, 0x10}, {0x006FA7F4, 0x10},
                              {0x007CC979, 1}, {0x007CE478, 4}});
    StandIns& s = *new StandIns();
    StandInScript(s);
    StandInObjects(s);
    StandInLoading(s);
    GameVariable<uint32_t>(0x007D1800) = reinterpret_cast<uint32_t>(g_manager);
    GameVariable<uint32_t>(0x006F0FB8) = reinterpret_cast<uint32_t>(g_objectManager);
    GameVariable<uint32_t>(0x006918DC) = 0xA55E7000;
    GameVariable<uint32_t>(0x007CE478) = 0x5CA7E000;
    GameVariable<uint8_t>(0x007CC979) = 1;
    std::memset(&GameVariable<uint8_t>(0x006F3614), 0, 0x10); // (no pools: memory from the manager)
    std::memset(&GameVariable<uint8_t>(0x006FA7F4), 0, 0x10);
    for (uint32_t seed = 1; seed <= 150; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_seed = seed;
            Lcg random(0x5FF00000 + seed);
            g_answers = Lcg(0x5FFA0000 + seed);
            g_arenaUsed = 0;
            RandomWorld(random);
            auto& data = *reinterpret_cast<Obj::CCutsceneData*>(g_data);
            // objects named in families (a child is its parent's name + 1), some of them stand-ins
            data.m_objectCount = static_cast<int>(random.Below(4));
            for (auto& entry : data.m_objects)
                entry.name = 0x9F000000 + random.Below(5);
            for (int i = 0; i < data.m_objectCount; ++i)
                data.m_objects[i].object =
                    reinterpret_cast<Obj::CCompositeObject*>(Object(static_cast<int>(random.Below(6))));
            for (int i = 0; i < 256; ++i)
                data.m_animations[i].name = 0x9F000000 + random.Below(5);
            data.m_animationCount = static_cast<int>(random.Below(6));
            data.m_colourSaved = random.Below(3) == 0;
            data.m_unknown34A8 = reinterpret_cast<void*>(0xA99EA000);
            data.m_unknown34AC = reinterpret_cast<void*>(kFakeStruct + 0x500);
            data.mp_animList = g_animList;
            *reinterpret_cast<int*>(g_animList + 8) = static_cast<int>(random.Below(5));
            for (auto& a : g_arrays)
                a[1] = random.Below(5);
            std::memset(g_symbol, 0x33, sizeof(g_symbol));
            g_componentsAlways = true; // (the original uses the objects and components it finds unchecked)
            // (models as memory: AddObjects reads their flags, unchecked)
            for (int i = 0; i < 6; ++i) {
                for (auto& byte : g_models[i])
                    byte = static_cast<uint8_t>(random.Next());
                *reinterpret_cast<uint32_t*>(g_components[i] + 0x208) = reinterpret_cast<uint32_t>(g_models[i]);
            }
            void* p = g_data;
            switch (random.Below(4)) {
            case 0:
                Record({Fn<uint32_t(__thiscall*)(void*)>(original, 0x00464640)(p) == reinterpret_cast<uint32_t>(p)});
                break;
            case 1:
                Fn<void(__thiscall*)(void*)>(original, 0x00464780)(p);
                break;
            default:
                Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004648D0)(p, kFakeStruct + random.Below(16));
                break;
            }
            g_componentsAlways = false;
            // the source pools' new and delete
            Record({Fn<uint32_t(__cdecl*)(unsigned int)>(original, 0x004645A0)(0x214)});
            Fn<void(__cdecl*)(void*)>(original, 0x004645E0)(g_arena + 0x10);
            PushData();
            PushState(reinterpret_cast<uint32_t>(g_symbol), reinterpret_cast<uint32_t>(g_symbol) + 0x20);
            g_trace.push_back(HashBytes(g_arena, sizeof(g_arena)));
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "cutscene data: constructor, adding objects");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestCutsceneLoading, ObjsysTests::CleanUp);

// --- OS-13: the objects table, the clock, stand-ins, transforms, the details ----------------------------------

alignas(16) uint8_t g_details[0x70];
alignas(16) uint32_t g_table[2 + 5 * 6];

void StandInUpdating(StandIns& s)
{
    s.At({0x0046CAA0, AddressOf(&Script::CArray::Init)}, 0, [](Regs& r) {
        Record({0x46CAA000, r.ecx});
        return r.ecx;
    });
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0x46CC5000, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&Script::CArray::SetElement)}, 8, [](Regs& r) {
        Record({0x46CB1000, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    using AddArrayFn = void (CStruct::*)(uint32_t, Script::CArray*);
    s.At({0x004780D0, AddressOf(static_cast<AddArrayFn>(&CStruct::AddArrayPointer))}, 8, [](Regs& r) {
        Record({0x4780D000, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005BE8E0, AddressOf(&Tmr::GetVblanks)}, 0, [](Regs& r) {
        static const uint32_t kHigh[] = {0, 0, 1, 0x80000000, 0xFFFFFFFF};
        uint32_t low = g_answers.Next(), high = g_answers.Pick(kHigh);
        r.edx = high;
        Record({0x5BE8E000, low, high});
        return low;
    });
    s.At({0x00463910, AddressOf(&Obj::CCutsceneData::NameChecksum)}, 8, [](Regs& r) {
        Record({0x46391000, r.ecx, TestStandIns::Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0xA1000000 + r.args[1];
    });
    s.At({0x00482F10}, 0, [](Regs& r) {
        Record({0x482F1000, r.ecx});
        return 0u;
    });
    s.At({0x00532EB0}, 0, [](Regs& r) {
        uint32_t fader = Hash(21, r.ecx, 0) % 3 ? 0xFADE0000 + (r.ecx & 0xFFF) : 0;
        Record({0x532EB000, r.ecx, fader});
        return fader;
    });
    s.At({0x0048E830}, 8, [](Regs& r) {
        Record({0x48E83000, r.ecx, r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x00404090}, 4, [](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<const void*>(r.args[0]), 64);
        Record({0x40409000, r.args[0]});
        return r.ecx;
    });
    s.At({0x00493090}, 12, [](Regs& r) {
        Record({0x49309000, r.ecx, HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 64), r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0041CAC0}, 4, [](Regs& r) {
        Record({0x41CAC000, r.ecx, r.args[0]});
        return 0u;
    });
    for (uint32_t address : {0x004A3870u, 0x004A36E0u}) {
        s.At({address}, 8, [address](Regs& r) {
            auto* out = reinterpret_cast<float*>(r.args[1]);
            for (int i = 0; i < (address == 0x004A3870 ? 4 : 16); ++i)
                out[i] = static_cast<float>(static_cast<int>(g_answers.Below(2001)) - 1000) / 64.0f;
            Record({address, r.ecx, r.args[0]});
            return 0u;
        });
    }
    // a vector by a matrix, a matrix by a matrix (the out's contents from the inputs)
    s.At({0x0041F470}, 0, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* v = reinterpret_cast<const float*>(r.args[1]);
        const auto* m = reinterpret_cast<const float*>(r.args[2]);
        uint32_t h = HashBytes(reinterpret_cast<const uint8_t*>(m), 64);
        for (int i = 0; i < 4; ++i)
            out[i] = v[i] * 0.5f + static_cast<float>(h >> (8 * i) & 0xFF);
        Record({0x41F47000, h});
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // (D3DXMatrixMultiply: stdcall)
        uint32_t a = HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 64);
        uint32_t b = HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 64);
        auto* out = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            out[i] = static_cast<float>((a >> i & 0xFF) + (b >> (i + 8) & 0xFF));
        Record({0x5BFA8C00, a, b});
        return r.args[0];
    });
    // the movie details
    s.At({0x00468FB0, AddressOf(&Obj::CMovieDetails::Construct)}, 0, [](Regs& r) {
        Record({0x468FB000, r.ecx});
        return r.ecx;
    });
    s.At({0x00469230, AddressOf(&Obj::CMovieDetails::DestroyBody)}, 0, [](Regs& r) {
        Record({0x46923000, r.ecx});
        return 0u;
    });
    s.At({0x00407B90}, 8, [](Regs& r) {
        Record({0x407B9000, r.ecx, r.args[0], r.args[1]});
        return 0xA55E0B90u;
    });
    s.At({0x00407B20}, 4, [](Regs& r) {
        Record({0x407B2000, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0048E970}, 0, [](Regs& r) {
        Record({0x48E97000, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0048E990}, 0, [](Regs& r) {
        Record({0x48E99000, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
}

void TestCutsceneUpdate()
{
    ObjsysTests::SaveGlobals({{0x006F1158, 1}, {0x006F1160, 0x1000}, {0x007D1800, 4}, {0x006F0FB8, 4},
                              {0x006918DC, 4}, {0x006FA7C0, 4}, {0x006F3614, 0x10}, {0x006FA7F4, 0x10},
                              {0x006F22B4, 0x10}, {0x007CC979, 1}, {0x007CE478, 4}, {0x006F115C, 4},
                              {0x00684FB4, 4}, {0x006F0FD8, 0x40}});
    StandIns& s = *new StandIns();
    StandInScript(s);
    StandInObjects(s);
    StandInLoading(s);
    StandInUpdating(s);
    GameVariable<uint32_t>(0x007D1800) = reinterpret_cast<uint32_t>(g_manager);
    GameVariable<uint32_t>(0x006F0FB8) = reinterpret_cast<uint32_t>(g_objectManager);
    GameVariable<uint32_t>(0x006918DC) = 0xA55E7000;
    GameVariable<uint32_t>(0x007CE478) = 0x5CA7E000;
    GameVariable<uint8_t>(0x007CC979) = 1;
    std::memset(&GameVariable<uint8_t>(0x006F3614), 0, 0x10); // (no pools: memory from the manager)
    std::memset(&GameVariable<uint8_t>(0x006FA7F4), 0, 0x10);
    std::memset(&GameVariable<uint8_t>(0x006F22B4), 0, 0x10);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_seed = seed;
            Lcg random(0x60000000 + seed);
            g_answers = Lcg(0x600A0000 + seed);
            g_arenaUsed = 0;
            RandomWorld(random);
            for (int i = 0; i < 6; ++i) {
                for (auto& byte : g_models[i])
                    byte = static_cast<uint8_t>(random.Next());
                *reinterpret_cast<uint32_t*>(g_components[i] + 0x208) = reinterpret_cast<uint32_t>(g_models[i]);
            }
            auto& data = *reinterpret_cast<Obj::CCutsceneData*>(g_data);
            data.m_objectCount = static_cast<int>(random.Below(5));
            for (int i = 0; i < 256; ++i) {
                auto& entry = data.m_objects[i];
                entry.name = 0x9F000000 + random.Below(5);
                entry.part = static_cast<int>(random.Below(3));
                entry.object = reinterpret_cast<Obj::CCompositeObject*>(Object(static_cast<int>(random.Below(6))));
                entry.parent = random.Below(3) == 0
                                   ? reinterpret_cast<Obj::CCompositeObject*>(Object(static_cast<int>(random.Below(6))))
                                   : nullptr;
                entry.animIndex = random.Below(3) == 0 ? -1 : static_cast<int>(random.Below(8));
                entry.animation = random.Below(3) ? &data.m_animations[random.Below(6)] : nullptr;
            }
            data.m_animationCount = static_cast<int>(random.Below(8));
            for (int i = 0; i < 256; ++i) {
                data.m_animations[i].name = 0x9F000000 + random.Below(25);
                data.m_animations[i].source =
                    random.Below(2) ? reinterpret_cast<void*>(Object(static_cast<int>(random.Below(6)))) : nullptr;
            }
            data.m_unknown34A8 = reinterpret_cast<void*>(0xA99EA000);
            data.m_unknown34AC = reinterpret_cast<void*>(kFakeStruct + 0x500);
            data.mp_params = reinterpret_cast<Script::CStruct*>(kFakeStruct + 0x600);
            data.mp_animList = g_animList;
            data.mp_cameraAnim = g_animList;
            static const float kLengths[] = {0.5f, 1.0f, 10.0f, 0.0f};
            *reinterpret_cast<float*>(g_animList) = random.Below(8) ? random.Pick(kLengths) : std::nanf("");
            *reinterpret_cast<int*>(g_animList + 8) = static_cast<int>(random.Below(6));
            data.m_cameraSource = random.Below(2) ? reinterpret_cast<void*>(Object(0)) : nullptr;
            data.m_objectSource = random.Below(4) ? reinterpret_cast<void*>(Object(1)) : nullptr;
            data.m_cameraParts = static_cast<int>(random.Below(3));
            data.m_objectParts = static_cast<int>(random.Below(3));
            data.m_cameraPart = static_cast<int>(random.Below(3));
            data.m_objectPart = static_cast<int>(random.Below(3));
            data.m_unknown3474 = static_cast<uint8_t>(random.Below(2));
            static const float kRates[] = {1.0f, 2.0f, 0.5f, -1.0f, 0.0f};
            data.m_unknown3470 = random.Pick(kRates);
            static const float kEnds[] = {0.0f, 0.5f, 10.0f, -1.0f, 0.51666665f};
            data.m_unknown348C = random.Pick(kEnds);
            data.m_unknown3488 = Bits(0.125f);
            data.m_time = random.Below(2) ? 0.5f : static_cast<float>(random.Below(2000)) / 100.0f;
            if (random.Below(4) == 0) // (a loop point the clock lands on exactly)
                data.m_unknown348C = data.m_time;
            data.m_vblanks = random.Next();
            data.m_pausedVblanks = random.Below(3) == 0 ? 0xFFFFFFFFFFFFFF00ull : random.Below(1000);
            GameVariable<int>(0x006F115C) = static_cast<int>(random.Below(1000));
            static const int kVblankRates[] = {60, 50, 1};
            GameVariable<int>(0x00684FB4) = random.Pick(kVblankRates);
            for (auto& word : g_table)
                word = random.Next() ^ random.Next() << 16;
            g_table[1] = random.Below(6);
            for (auto& byte : g_details)
                byte = static_cast<uint8_t>(random.Next());
            g_componentsAlways = true; // (the original uses the objects and components it finds unchecked)
            void* p = g_data;
            switch (random.Below(8)) {
            case 0:
                Record({Fn<uint32_t(__stdcall*)(uint32_t, const uint32_t*)>(original, 0x00465510)(kFakeStruct + 7,
                                                                                                  g_table) & 0xFF});
                break;
            case 1:
            case 2:
                Fn<void(__thiscall*)(void*, int)>(original, 0x004656E0)(p, 0x7E57);
                break;
            case 3:
                Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00465AB0)(p, Object(static_cast<int>(random.Below(6))));
                break;
            case 4:
            case 5:
                Fn<void(__thiscall*)(void*)>(original, 0x00465C00)(p);
                break;
            case 6:
                Fn<void(__thiscall*)(void*)>(original, 0x00466450)(p);
                break;
            default: {
                uint32_t settings[3] = {random.Next(), random.Next(), random.Next()};
                if (random.Below(2))
                    settings[2] &= ~0xFFu; // (doesn't own its data)
                Record({Fn<uint32_t(__thiscall*)(void*, const uint32_t*)>(original, 0x00466490)(g_details, settings) ==
                        reinterpret_cast<uint32_t>(g_details)});
                if (random.Below(2))
                    reinterpret_cast<uint32_t*>(g_details)[0x2C / 4] = random.Below(2) ? 0xDA7A0000 : 0;
                Fn<void(__thiscall*)(void*)>(original, 0x00466560)(g_details);
                PushState(reinterpret_cast<uint32_t>(g_details), reinterpret_cast<uint32_t>(g_details) + 0x70);
                break;
            }
            }
            g_componentsAlways = false;
            PushData();
            for (int i = 0; i < 6; ++i)
                PushState(reinterpret_cast<uint32_t>(g_objects[i]) + 0x4C, reinterpret_cast<uint32_t>(g_objects[i]) + 0x100);
            g_trace.push_back(HashBytes(g_arena, sizeof(g_arena)));
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "cutscene data: table, clock, stand-ins, transforms, details");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestCutsceneUpdate, ObjsysTests::CleanUp);

} // namespace
