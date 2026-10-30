// Self-tests for the objsys agent's batch OS-5: objects made for nodes, the movie details base class and the class
// node list (src/game/obj/node_objects.cpp). The original and ours against recording stand-ins for everything they
// call (node parameters, the object manager, components, scripts, memory, the camera), every call and the memory
// they leave compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/obj/bbox_component.h"
#include "game/obj/compositemgr.h"
#include "game/obj/lock_obj.h"
#include "game/obj/model_component.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/gfx/viewport_list.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/objsys_testkit.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

constexpr uint32_t kArenaSize = 0x10000;
uint8_t* const g_noArena =
    static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
uint32_t g_noBump;
uint32_t NoBase() { return reinterpret_cast<uint32_t>(g_noArena); }
uint32_t NoReloc(uint32_t word)
{
    if (word - NoBase() < kArenaSize)
        return 0xA0000000u + (word - NoBase());
    return OnStack(word) ? 0x57AC0000u : word;
}
uint32_t Bump(uint32_t size)
{
    uint32_t block = NoBase() + g_noBump;
    g_noBump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), 0xCD, size);
    return block;
}

// Node parameters (stand-in structures 0x5A000000 + n): their values and flags.
struct Params {
    std::map<uint32_t, uint32_t> values;
    std::set<uint32_t> flags;
    std::set<std::string> named;
};
std::map<uint32_t, Params> g_noParams;
Lcg g_noAnswers(1);
constexpr uint32_t kVtable = 0x0100;         // the made objects' / components' vtables (slots 0-15)
constexpr uint32_t kCameraVtable = 0x0200;

uint32_t g_markers[3];

void StandInNodeWorld(StandIns& s)
{
    auto get = [](Regs& r) {
        Params& p = g_noParams[r.ecx];
        auto it = p.values.find(r.args[0]);
        Record({0x6E7C0000, r.ecx, r.args[0], r.args[2], it != p.values.end()});
        if (it == p.values.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(
                          &Script::CStruct::GetChecksum))},
         12, get);
    s.At({0x00476630, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(
                          &Script::CStruct::GetInteger))},
         12, get);
    s.At({0x004767C0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int)>(
                          &Script::CStruct::GetStructure))},
         12, get);
    s.At({0x00476B40, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))},
         4, [](Regs& r) {
             bool has = g_noParams[r.ecx].flags.count(r.args[0]) != 0;
             Record({0xF1A60000, r.ecx, r.args[0], has});
             return static_cast<uint32_t>(has);
         });
    s.At({0x00476B10, AddressOf(static_cast<bool (Script::CStruct::*)(const char*)>(
                          &Script::CStruct::ContainsComponentNamed))},
         4, [](Regs& r) {
             const char* name = reinterpret_cast<const char*>(r.args[0]);
             bool has = g_noParams[r.ecx].named.count(name) != 0;
             Record({0xC0A70000, r.ecx, Hash(name), has});
             return static_cast<uint32_t>(has);
         });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        Record({0x3A1C0003, r.args[0]});
        return 0x0DE00000u + (r.args[0] & 0xFF);
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))},
         0, [](Regs& r) {
             auto* out = reinterpret_cast<float*>(r.args[1]);
             out[0] = static_cast<float>(r.args[0] & 0xFF);
             out[1] = 2.0f;
             out[2] = -3.5f;
             out[3] = 1.0f;
             Record({0x9050000, r.args[0], NoReloc(r.args[1])});
             return 1u;
         });
    // structures (tokens: nothing reads them)
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1A170000, NoReloc(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477130, 0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA0000, NoReloc(r.ecx)});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(
                          &Script::CStruct::AddChecksum))},
         8, [](Regs& r) {
             Record({0xADDC0000, NoReloc(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))},
         8, [](Regs& r) {
             Record({0xADD10000, NoReloc(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478540, AddressOf(&Script::CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0xA99E0000, NoReloc(r.ecx), NoReloc(r.args[0])});
        return 0u;
    });
    s.At({0x00479090, AddressOf(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, 0,
         [](Regs& r) {
             Record({0x6A770000, Hash(reinterpret_cast<const char*>(r.args[0]))});
             return 0x0A77A000u;
         });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x6A770001, r.args[0]});
        return 0x0A77B000u;
    });
    // memory
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        uint32_t block = Bump(r.args[0]);
        Record({0xA110C000, r.args[0], r.args[1], NoReloc(block)});
        return block;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        uint32_t block = Bump(r.args[0]);
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        Record({0xCA110000, r.args[0], NoReloc(block)});
        return g_noAnswers.Below(12) == 0 ? 0u : block; // (out of memory now and then)
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000, NoReloc(r.args[0])});
        return 0u;
    });
    // objects, components, scripts
    s.At({0x0045F3A0, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObjectFromNode)}, 12, [](Regs& r) {
        uint32_t object = Bump(0x108);
        *reinterpret_cast<uint32_t*>(object) = NoBase() + kVtable;
        Record({0xC0C10000, r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF, NoReloc(object)});
        return object;
    });
    s.At({0x0045F330, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObject)}, 0, [](Regs& r) {
        uint32_t object = Bump(0x108);
        *reinterpret_cast<uint32_t*>(object) = NoBase() + kVtable;
        Record({0xC0C00000, r.ecx, NoReloc(object)});
        return object;
    });
    s.At({0x0045DAD0, AddressOf(&Obj::CCompositeObject::InitFromStructure)}, 4, [](Regs& r) {
        Record({0x1F50000, NoReloc(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x0045DA30, AddressOf(&Obj::CCompositeObject::AddComponent)}, 4, [](Regs& r) {
        Record({0xADDC0300, NoReloc(r.ecx), NoReloc(r.args[0])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        uint32_t component = g_noAnswers.Below(2) ? 0x0C0C0000u + r.args[0] % 0x100 : 0u;
        Record({0xC0390000, NoReloc(r.ecx), r.args[0], component});
        return component;
    });
    s.At({0x00420480, AddressOf(&Obj::CModelComponent::Teleport)}, 0, [](Regs& r) {
        Record({0x42048000, r.ecx});
        return 0u;
    });
    s.At({0x0041F500, AddressOf(&Obj::CLockObjComponent::Construct)}, 0, [](Regs& r) {
        Record({0x41F50000, NoReloc(r.ecx)});
        return r.ecx;
    });
    s.At({0x0041AAD0, AddressOf(&Obj::CBBoxComponent::Construct)}, 0, [](Regs& r) {
        Record({0x41AAD000, NoReloc(r.ecx)});
        return r.ecx;
    });
    s.At({0x00461350, AddressOf(&Obj::CObject::SwitchScript)}, 8, [](Regs& r) {
        Record({0x5C110000, NoReloc(r.ecx), r.args[0], NoReloc(r.args[1])});
        return 0u;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x52700000, r.args[0], NoReloc(r.args[1]), NoReloc(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    // the camera
    s.At({0x00485BA0, AddressOf(&Gfx::Camera::Construct)}, 0, [](Regs& r) {
        *reinterpret_cast<uint32_t*>(r.ecx) = NoBase() + kCameraVtable;
        Record({0xCA3E0000, NoReloc(r.ecx)});
        return r.ecx;
    });
    s.At({0x004A0720, AddressOf(Nx::MarkCameraForDeletion)}, 0, [](Regs& r) {
        uint32_t used = g_noAnswers.Below(2);
        Record({0xCA3E0001, NoReloc(r.args[0]), used});
        return used;
    });
    // the transform (game code, the same for both)
    void* finalize = s.Thunk(0, [](Regs& r) {
        Record({0xF1A10000, NoReloc(r.ecx)});
        return 0u;
    });
    void* destroyCamera = s.Thunk(4, [](Regs& r) {
        Record({0xCA3ED000, NoReloc(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_markers[0] = reinterpret_cast<uint32_t>(finalize);
    g_markers[1] = reinterpret_cast<uint32_t>(destroyCamera);
}

void ResetWorld()
{
    std::memset(g_noArena, 0, kArenaSize);
    g_noBump = 0x1000;
    auto* vtable = reinterpret_cast<uint32_t*>(g_noArena + kVtable);
    vtable[0x20 / 4] = g_markers[0];
    reinterpret_cast<uint32_t*>(g_noArena + kCameraVtable)[0] = g_markers[1];
}

void PushArena()
{
    for (uint32_t i = 0x1000; i < g_noBump; i += 4)
        g_trace.push_back(NoReloc(*reinterpret_cast<uint32_t*>(g_noArena + i)));
}

// Random node parameters for structure `structure`.
void RandomParams(uint32_t structure, Lcg& random)
{
    Params& p = g_noParams[structure];
    p = Params();
    if (random.Below(2))
        p.values[0x2CA8A299] = 0x5C000000 + random.Below(16);
    if (random.Below(2))
        p.values[0x7031F10C] = 0x5B000000 + random.Below(16);
    if (random.Below(2))
        p.values[0x99A9B716] = random.Below(5);
    if (random.Below(2))
        p.flags.insert(0x69C257E9);
    if (random.Below(2))
        p.named.insert("HasMotion");
    // the movie settings
    if (random.Below(3))
        p.values[0x1A005041] = 0x0F000000 + random.Below(16);
    if (random.Below(2))
        p.values[0x894FD988] = 0x5B100000 + random.Below(16);
    if (random.Below(2))
        p.values[0x8DDE72C0] = random.Below(3);
    if (random.Below(2))
        p.values[0xB8CA12C5] = random.Below(3);
    if (random.Below(2))
        p.flags.insert(0xEB300C29);
    // the class node list
    if (random.Below(2))
        p.values[0x12B4E660] = random.Below(3) ? 0xD18719D5 : 0x11111111;
    if (random.Below(2))
        p.flags.insert(0x7C2552B9);
}

void TestNodeObjects()
{
    ObjsysTests::SaveGlobals({{0x007CC979, 1}, {0x006F0FB8, 4}, {0x006F3614, 16}});
    StandIns& s = *new StandIns();
    StandInNodeWorld(s);
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ObjsysTests::ZeroGlobals();
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<uint32_t>(0x006F0FB8) = 0x0B0B0000;
            ResetWorld();
            Lcg random(0x40DE0000 + seed);
            g_noAnswers = Lcg(0x40DA0000 + seed);
            for (uint32_t k = 0; k < 4; ++k)
                RandomParams(0x5A000000 + k, random);
            for (int step = 0; step < 12; ++step) {
                uint32_t params = 0x5A000000 + random.Below(4);
                switch (random.Below(5)) {
                case 0:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x00468B00)(params);
                    break;
                case 1: {
                    uint32_t object = Bump(0x108);
                    *reinterpret_cast<uint32_t*>(object) = NoBase() + kVtable;
                    Fn<void(__cdecl*)(uint32_t, uint32_t)>(original, 0x00468B60)(object, params);
                    break;
                }
                case 2:
                    Record({NoReloc(Fn<uint32_t(__cdecl*)(uint32_t, int)>(original, 0x00468CB0)(params, 5))});
                    break;
                case 3:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x00468EB0)(params);
                    break;
                default:
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x00468EC0)(params);
                    break;
                }
            }
            PushArena();
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "node objects: particles, game objects, plain objects, scripts");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestNodeObjects, ObjsysTests::CleanUp);

// The movie details (constructed on dirty memory, settings read from parameters of every kind - again, with params
// there already -, the settings structure, the setters, done or not, destroyed with the camera in use or not, by
// each destructor and flags) and the class node list (made, nodes of the class added from arrays of 0-5 nodes, a
// transform set or not, points through it, cleared).
void TestMovieDetails()
{
    ObjsysTests::SaveGlobals({{0x007CC979, 1}, {0x006F3614, 16}});
    StandIns& s = *new StandIns();
    StandInNodeWorld(s);
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ObjsysTests::ZeroGlobals();
            GameVariable<bool>(0x007CC979) = true;
            ResetWorld();
            Lcg random(0x30DE0000 + seed);
            g_noAnswers = Lcg(0x30DA0000 + seed);
            for (uint32_t k = 0; k < 4; ++k)
                RandomParams(0x5A000000 + k, random);
            uint32_t details = Bump(0x40);
            Record({NoReloc(Fn<uint32_t(__thiscall*)(uint32_t)>(original, 0x00468FB0)(details))});
            for (uint32_t i = 0; i < 0x28; i += 4) // (the list node as constructed)
                Record({NoReloc(*reinterpret_cast<uint32_t*>(details + i))});
            for (int step = 0; step < 10; ++step) {
                switch (random.Below(7)) {
                case 0:
                case 1:
                    Fn<void(__thiscall*)(uint32_t, uint32_t)>(original, 0x00469050)(details,
                                                                                 0x5A000000 + random.Below(4));
                    break;
                case 2:
                    Record({NoReloc(Fn<uint32_t(__thiscall*)(uint32_t)>(original, 0x00469180)(details))});
                    break;
                case 3:
                    Record({Fn<uint32_t(__thiscall*)(uint32_t, uint32_t)>(original, 0x00468F20)(details,
                                                                                            random.Below(2)) &
                            0xFF});
                    break;
                case 4:
                    Record({Fn<uint32_t(__thiscall*)(uint32_t, uint32_t)>(original, 0x00468F40)(details,
                                                                                            random.Below(2)) &
                            0xFF});
                    break;
                case 5:
                    Record({Fn<uint32_t(__thiscall*)(uint32_t, uint32_t)>(original, 0x00468F50)(details,
                                                                                            random.Below(2)) &
                            0xFF});
                    break;
                default:
                    Record({Fn<uint32_t(__thiscall*)(uint32_t, uint32_t)>(original, 0x00468F60)(details,
                                                                                            random.Next()) &
                            0xFF});
                    break;
                }
            }
            // linked into a list of two, then destroyed
            uint32_t other = Bump(0x20);
            auto* node = reinterpret_cast<uint32_t*>(details);
            auto* otherNode = reinterpret_cast<uint32_t*>(other);
            node[3] = other;
            node[4] = other;
            otherNode[3] = details;
            otherNode[4] = details;
            switch (seed % 4) {
            case 0:
                Fn<void(__thiscall*)(uint32_t)>(original, 0x00469230)(details);
                break;
            case 1:
                Record({NoReloc(Fn<uint32_t(__thiscall*)(uint32_t, uint32_t)>(original, 0x004692D0)(details, 1))});
                break;
            case 2:
                Record({NoReloc(Fn<uint32_t(__thiscall*)(uint32_t, uint32_t)>(original, 0x004692D0)(details, 2))});
                break;
            default:
                Record({NoReloc(Fn<uint32_t(__thiscall*)(uint32_t, uint32_t)>(original, 0x00468F70)(
                    details, random.Below(4)))});
                break;
            }

            // the class node list
            uint32_t list = Bump(0x50);
            Record({NoReloc(Fn<uint32_t(__thiscall*)(uint32_t)>(original, 0x004693C0)(list))});
            uint32_t count = random.Below(6);
            uint32_t array = Bump(0x40);
            auto* a = reinterpret_cast<uint32_t*>(array);
            a[0] = 0x0A00;
            a[1] = count;
            auto* elements = reinterpret_cast<uint32_t*>(Bump(0x20));
            for (uint32_t i = 0; i < count; ++i)
                elements[i] = 0x5A000000 + random.Below(4);
            a[2] = count == 1 ? elements[0] : reinterpret_cast<uint32_t>(elements);
            Fn<void(__thiscall*)(uint32_t, uint32_t)>(original, 0x00469430)(list, array);
            if (random.Below(2)) {
                float matrix[16];
                for (float& m : matrix)
                    m = (static_cast<float>(random.Below(201)) - 100.0f) / 50.0f;
                Fn<void(__thiscall*)(uint32_t, const float*)>(original, 0x00469340)(list, matrix);
            }
            for (int i = 0; i < 3; ++i) {
                float in[4] = {static_cast<float>(random.Below(100)), -2.0f, 3.25f, 1.0f};
                float out[4] = {9, 9, 9, 9};
                Record({NoReloc(Fn<uint32_t(__thiscall*)(uint32_t, float*, const float*)>(original, 0x00469350)(
                            list, out, in)) == NoReloc(reinterpret_cast<uint32_t>(out)),
                        *reinterpret_cast<uint32_t*>(&out[0]), *reinterpret_cast<uint32_t*>(&out[1]),
                        *reinterpret_cast<uint32_t*>(&out[2]), *reinterpret_cast<uint32_t*>(&out[3])});
            }
            Record({*reinterpret_cast<uint32_t*>(list)});
            if (random.Below(2))
                Fn<void(__thiscall*)(uint32_t)>(original, 0x00469300)(list);
            else
                Fn<void(__thiscall*)(uint32_t)>(original, 0x004693D0)(list); // (the jump to it)
            PushArena();
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "movie details and class node list");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestMovieDetails, ObjsysTests::CleanUp);

} // namespace
