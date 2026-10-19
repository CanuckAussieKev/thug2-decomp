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
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
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
void* g_componentVtable[4];
void* g_managerVtable[4];
alignas(16) uint8_t g_objectManager[8];
const char* const kModelNames[] = {"model_a", "model_b", "skater_c"};

constexpr uint32_t kDataSize = 0x34D0;
alignas(16) uint8_t g_data[kDataSize];

uint32_t Object(int i)
{
    return reinterpret_cast<uint32_t>(g_objects[i]);
}

void StandInScript(StandIns& s)
{
    using GetChecksumFn = bool (CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&CStruct::GetChecksum))}, 12, [](Regs& r) {
        uint32_t h = Hash(1, r.ecx, r.args[0]);
        uint32_t found = h % 5 != 0 || r.args[2] != 0;
        if (found)
            *reinterpret_cast<uint32_t*>(r.args[1]) = 0x9F000000 + (h >> 8) % 4;
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
    for (int i = 0; i < 4; ++i) {
        g_componentVtable[i] = s.Thunk(0, [i](Regs& r) {
            Record({0xC0390000u + static_cast<uint32_t>(i), r.ecx});
            return 0u;
        });
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
        e.animation = random.Below(3) ? g_animEntries[random.Below(4)] : nullptr;
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

} // namespace
