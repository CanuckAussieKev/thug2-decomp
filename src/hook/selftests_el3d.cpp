// Self-tests for the Element3d screen element (src/game/front/element3d.cpp) and the matrix helpers it uses
// (src/game/mth/matrix.cpp): the original and ours run the same calls on identical stand-in elements, objects,
// components, cameras and parameter structures in an arena. Everything they call outside the batch (the structure
// functions, CScreenElement's pieces, the composite objects and their manager, the scripts, the node and object
// lookups, the viewport's camera, the models' bounding boxes and viewport masks, the timer, the memory manager) is
// stood in for by recording code (written over the game's function and ours alike), so the two are compared call
// by call, then the whole arena word by word. The math the original reaches through D3DX and the rewritten Mth
// helpers is redirected to the same code in both runs. The widescreen front end's rule is checked against
// worked-out values, and through Update (front end on: stretched; off / in a level: as the original).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/front/element3d.h"
#include "game/gfx/camera.h"
#include "game/gfx/screen.h"
#include "game/mth/matrix.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/sdl_window.h"
#include "game/sys/timer.h"
#include "game/gfx/viewport.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Front::CElement3d;
using Script::CStruct;

uint32_t FloatBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

Mth::Matrix* __stdcall SameMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    return &Mth::Multiply(*out, *a, *b);
}
template <typename Method>
void* CodeOf(Method method)
{
    void* address;
    static_assert(sizeof(method) == sizeof(address));
    std::memcpy(&address, &method, sizeof(address));
    return address;
}
// The math both runs use: D3DXMatrixMultiply, the rotation builders (x87 vs the C runtime's sin/cos), and the
// rewritten helpers that call them (RotateXLocal, Normalize, OrthoNormalizeAbout) - the same code for both.
struct SameMath {
    Patch multiply{0x005BFA8C, Branch(0xE9, 0x005BFA8C, reinterpret_cast<void*>(&SameMultiply))};
    Patch rotateX{0x00404DD0, Branch(0xE9, 0x00404DD0, reinterpret_cast<void*>(&Mth::CreateRotateXMatrix))};
    Patch rotateY{0x00404E20, Branch(0xE9, 0x00404E20, reinterpret_cast<void*>(&Mth::CreateRotateYMatrix))};
    Patch rotateZ{0x00404E70, Branch(0xE9, 0x00404E70, reinterpret_cast<void*>(&Mth::CreateRotateZMatrix))};
    Patch rotateXLocal{0x00405670, Branch(0xE9, 0x00405670, CodeOf(&Mth::Matrix::RotateXLocal))};
    Patch normalize{0x004023F0, Branch(0xE9, 0x004023F0, CodeOf(&Mth::Vector::Normalize))};
    Patch ortho{0x00405170, Branch(0xE9, 0x00405170, CodeOf(&Mth::Matrix::OrthoNormalizeAbout))};
};

// --- the matrix helpers, the layout getters, GetScreenAspect, ApplyAngles and ScreenToWorld -------------------

Mth::Matrix TestMatrix(int n)
{
    Mth::Matrix m;
    float* f = &m.row[0].x;
    for (int i = 0; i < 16; ++i)
        f[i] = std::sin(static_cast<float>(i * 7 + n * 13) * 0.37f) * (1.0f + static_cast<float>(i)) + 0.001f * n;
    return m;
}
void Push(std::vector<uint32_t>& trace, const void* p, size_t words)
{
    const auto* w = static_cast<const uint32_t*>(p);
    trace.insert(trace.end(), w, w + words);
}

alignas(16) uint8_t g_el3d_camera[0x100];

void CallScreenToWorld(void* code, const Mth::Vector* centre, Mth::Matrix* result, int viewport, float x, float y,
                       float z)
{
    __asm {
        push esi
        push z
        push y
        push x
        push viewport
        mov eax, centre
        mov esi, result
        call code
        add esp, 0x10
        pop esi
    }
}

void TestElement3dMath()
{
    SameMath math;
    std::vector<Saved> saved = {{0x00701340, 4, {}}, {0x00786D6C, 4, {}}, {0x00701348, 4, {}}};
    SaveAll(saved);
    StandIns s;
    void* camera = nullptr;
    s.At({0x004A09C0, AddressOf(&Nx::GetActiveCamera)}, 0, [&](Regs& r) {
        Record({0xC0, r.args[0]});
        return reinterpret_cast<uint32_t>(camera);
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        g_trace.clear();
        for (int n = 0; n < 6; ++n) {
            Mth::Matrix a = TestMatrix(n), b = TestMatrix(n + 9);
            Mth::Vector v = {1.5f + n, -2.25f * n, 0.125f - n, n % 2 ? 1.0f : 0.75f};
            Mth::Matrix m = a;
            Mth::Matrix* r = original ? Original<Mth::Matrix*(__thiscall*)(Mth::Matrix*)>(0x0041A640)(&m) : &m.Ident();
            t.push_back(r == &m);
            Push(t, &m, 16);
            m = a;
            r = original ? Original<Mth::Matrix*(__thiscall*)(Mth::Matrix*, const Mth::Vector*)>(0x0041BA30)(&m, &v)
                         : &m.Scale(v);
            t.push_back(r == &m);
            Push(t, &m, 16);
            m = a;
            r = original ? Original<Mth::Matrix*(__thiscall*)(Mth::Matrix*, const Mth::Vector*)>(0x0048A230)(&m, &v)
                         : &m.Translate(v);
            t.push_back(r == &m);
            Push(t, &m, 16);
            Mth::Vector out = {};
            if (original)
                Original<Mth::Vector*(__cdecl*)(Mth::Vector*, const Mth::Vector*, const Mth::Matrix*)>(0x0041F470)(
                    &out, &v, &b);
            else
                out = v * b;
            Push(t, &out, 4);
            for (int axis = 0; axis < 3; ++axis) {
                m = a;
                float angle = 0.3f * n - 0.7f + axis;
                if (original) {
                    r = Original<Mth::Matrix*(__thiscall*)(Mth::Matrix*, float)>(0x0041A6A0 + 0x30 * axis)(&m, angle);
                } else {
                    r = axis == 0 ? &m.RotateX(angle) : axis == 1 ? &m.RotateY(angle) : &m.RotateZ(angle);
                }
                t.push_back(r == &m);
                Push(t, &m, 16);
            }
            for (int order = -1; order <= 6; ++order) {
                m = a;
                float x = 0.5f + n, y = -1.25f + 0.1f * n, z = 2.0f - n;
                if (original)
                    Original<void(__cdecl*)(Mth::Matrix*, float, float, float, int)>(0x004A6BD0)(&m, x, y, z, order);
                else
                    Front::ApplyAngles(&m, x, y, z, order);
                Push(t, &m, 16);
            }
        }
        // Vector * Matrix with magnitudes far apart (the summation order shows).
        {
            Mth::Matrix wide;
            float* f = &wide.row[0].x;
            for (int i = 0; i < 16; ++i)
                f[i] = (i % 3 == 0 ? 1.0e7f : i % 3 == 1 ? 3.3e-3f : -77.7f) * (1.0f + 0.37f * i);
            const Mth::Vector vs[3] = {{1.0e7f, 3.3f, -2.7e-3f, 12345.678f}, {-1.0f, 1.0e8f, 1.0f, -1.0e-8f},
                                       {0.1f, 0.2f, 1.0e6f, 1.0f}};
            for (const Mth::Vector& v : vs) {
                Mth::Vector out = {};
                if (original)
                    Original<Mth::Vector*(__cdecl*)(Mth::Vector*, const Mth::Vector*, const Mth::Matrix*)>(
                        0x0041F470)(&out, &v, &wide);
                else
                    out = v * wide;
                Push(t, &out, 4);
            }
            // terms cancelling: x = ((1e16 - 1e16) + 0.5) + 0.5 in the original's order
            Mth::Matrix cancel = TestMatrix(1);
            cancel.row[3].x = 1.0e16f;
            cancel.row[2].x = -1.0e16f;
            cancel.row[1].x = 0.5f;
            cancel.row[0].x = 0.5f;
            const Mth::Vector ones = {1.0f, 1.0f, 1.0f, 1.0f};
            Mth::Vector out = {};
            if (original)
                Original<Mth::Vector*(__cdecl*)(Mth::Vector*, const Mth::Vector*, const Mth::Matrix*)>(0x0041F470)(
                    &out, &ones, &cancel);
            else
                out = ones * cancel;
            Push(t, &out, 4);
        }
        // The cameras' default angle.
        if (original)
            Original<void(__cdecl*)(float)>(0x004A07A0)(61.5f);
        else
            Nx::SetScreenAngle(61.5f);
        float angle = original ? Original<float(__cdecl*)()>(0x004A07B0)() : Nx::GetScreenAngle();
        t.push_back(FloatBits(angle));
        t.push_back(GameVariable<uint32_t>(0x00701348));
        // The layout getters and the screen's aspect.
        alignas(16) uint8_t element[0x178] = {};
        auto* e = reinterpret_cast<Front::CScreenElement*>(element);
        e->m_summed.baseScaleX = 1.37f;
        e->m_summed.baseScaleY = 0.61f;
        e->m_width = 333.3f;
        e->m_height = 77.7f;
        double w, h;
        if (original) {
            w = Original<double(__thiscall*)(void*)>(0x004A87A0)(e);
            h = Original<double(__thiscall*)(void*)>(0x004A87B0)(e);
        } else {
            w = e->GetLayoutWidth();
            h = e->GetLayoutHeight();
        }
        Push(t, &w, 2);
        Push(t, &h, 2);
        GameVariable<float>(0x00701340) = 1.7777778f;
        float aspect = original ? Original<float(__cdecl*)()>(0x004A0790)() : Nx::GetScreenAspect();
        t.push_back(FloatBits(aspect));

        // ScreenToWorld: with and without a camera, back buffers wider than 640 and not, three aspects.
        auto* camMatrix = reinterpret_cast<Mth::Matrix*>(g_el3d_camera + 0x6C);
        *camMatrix = TestMatrix(4);
        camMatrix->row[3] = {5.0f, 6.0f, 7.0f, 0.5f};
        auto* camPos = reinterpret_cast<Mth::Vector*>(g_el3d_camera + 0x5C);
        *camPos = {100.25f, -40.5f, 3000.125f, 1.0f};
        const float aspects[3] = {1.3333334f, 1.7777778f, 2.3703704f};
        const float places[4][3] = {{320.0f, 225.0f, -500.0f}, {12.5f, 400.25f, -6.0f}, {640.0f, -3.0f, 100.0f},
                                    {577.7f, 31.3f, -123.4f}};
        for (int c = 0; c < 2; ++c) {
            camera = c ? g_el3d_camera : nullptr;
            for (int width : {1920, 640})
                for (float a : aspects)
                    for (const auto& p : places) {
                        GameVariable<int32_t>(0x00786D6C) = width;
                        GameVariable<float>(0x00701340) = a;
                        Mth::Vector centre = {1.5f, -2.5f, 0.25f, 1.0f};
                        Mth::Matrix result = TestMatrix(7);
                        CallScreenToWorld(original ? reinterpret_cast<void*>(0x004A5EE0)
                                                   : reinterpret_cast<void*>(&Front::ScreenToWorldStub),
                                          &centre, &result, 2, p[0], p[1], p[2]);
                        Push(t, &result, 16);
                        Push(t, &centre, 4);
                    }
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "element3d: matrix helpers, ApplyAngles, layout size, ScreenToWorld");
}

// --- the class ------------------------------------------------------------------------------------------------

constexpr uint32_t kArenaSize = 0x10000;
alignas(16) uint8_t g_el3d_arenas[2][kArenaSize];
uint32_t g_el3d_base;
// (both arenas, the same for both runs: a value that only happens to fall inside one of them maps the same way in
// each - ArenaWord, selftest.h)
bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_el3d_arenas) <= 2 * kArenaSize; }
uint32_t Rel(uint32_t word) { return ArenaWord(g_el3d_arenas, kArenaSize, word); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
uint32_t At(uint32_t offset) { return g_el3d_base + offset; }

// Arena layout.
constexpr uint32_t kElement = 0x100;    // the element (0x510 bytes)
constexpr uint32_t kObjects = 0x1000;   // stand-in composite objects, 0x400 bytes each (component at +0x140,
                                        // its model pointer +0x208 -> +0x360, bounding box +0x380)
constexpr uint32_t kCamera = 0x6000;    // a stand-in camera (position +0x5C, matrix +0x6C)
constexpr uint32_t kStructs = 0x6200;   // stand-in parameter structures, 0x40 apart
constexpr uint32_t kArray = 0x6800;     // a stand-in array (size +4; elements at +0x10)
constexpr uint32_t kScript = 0x6900;    // a stand-in spawned script (its object pointer at +0xBC)
constexpr uint32_t kTargets = 0x6A00;   // stand-in objects found by id (m_pos at +0x4C)
constexpr uint32_t kNode = 0x6C00;
constexpr uint32_t kManager = 0x6D00;

std::map<std::tuple<char, uint32_t, uint32_t>, std::vector<uint32_t>> g_el3d_answers;
int g_el3d_nextObject;
bool g_el3d_noObject;
uint32_t g_el3d_camera_on;
uint32_t g_el3d_found;  // what FindObject / GetObjectByID return (arena offset or 0)
uint32_t g_el3d_time;
float g_el3d_angle;     // the stand-in camera's angle (degrees)
void* g_el3d_objectVtable[11];
void* g_el3d_componentVtable[13];
std::vector<Saved> g_el3d_saved;

void Answer(char kind, uint32_t structure, uint32_t name, std::vector<uint32_t> value)
{
    g_el3d_answers[{kind, Rel(structure), name}] = std::move(value);
}
const std::vector<uint32_t>* Lookup(char kind, uint32_t structure, uint32_t name)
{
    auto it = g_el3d_answers.find({kind, Rel(structure), name});
    return it == g_el3d_answers.end() ? nullptr : &it->second;
}
uint32_t Words(float f) { return FloatBits(f); }

float __fastcall StandInCameraAngle(void* camera)
{
    Rec(0x5B10, {reinterpret_cast<uint32_t>(camera)});
    return g_el3d_angle;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_el3d_saved);
    g_el3d_saved.clear();
}

void InstallStandIns(StandIns& s)
{
    using Script::VectorValue;
    auto getter = [&](std::initializer_list<uint32_t> addresses, char kind, uint32_t tag, bool byString) {
        s.At(addresses, 12, [kind, tag, byString](Regs& r) {
            uint32_t name = byString ? Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]))
                                     : r.args[0];
            Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
            const std::vector<uint32_t>* value = Lookup(kind, r.ecx, name);
            if (!value)
                return 0u;
            std::memcpy(reinterpret_cast<void*>(r.args[1]), value->data(), value->size() * 4);
            return 1u;
        });
    };
    getter({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
           'K', 0x6E50, false);
    getter({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 'F',
           0x6E51, false);
    getter({0x00476FB0,
            AddressOf(static_cast<bool (CStruct::*)(uint32_t, VectorValue*, int)>(&CStruct::GetVector))},
           'V', 0x6E52, false);
    getter({0x00477010,
            AddressOf(static_cast<bool (CStruct::*)(const char*, VectorValue*, int)>(&CStruct::GetVector))},
           'V', 0x6E53, true);
    getter({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 'I',
           0x6E54, false);
    getter({0x004767C0,
            AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
           'S', 0x6E55, false);
    getter({0x00476860,
            AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
           'S', 0x6E56, false);
    auto flag = [&](std::initializer_list<uint32_t> addresses, uint32_t tag, char kind, bool byString) {
        s.At(addresses, 4, [tag, kind, byString](Regs& r) {
            uint32_t name = byString ? Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]))
                                     : r.args[0];
            Rec(tag, {r.ecx, name});
            return Lookup(kind, r.ecx, name) ? 1u : 0u;
        });
    };
    flag({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))},
         0x6AF0, 'C', false);
    flag({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 0x6B40, 'L',
         false);
    flag({0x00476BA0, AddressOf(static_cast<bool (CStruct::*)(const char*)>(&CStruct::ContainsFlag))}, 0x6BA0, 'L',
         true);
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(r.ecx + 0x10)[r.args[0]];
    });

    // CScreenElement's pieces, the object's type, the memory manager.
    s.At({0x004A8F90, AddressOf(&Front::CScreenElement::Construct)}, 0, [](Regs& r) {
        Rec(0x8F90, {r.ecx});
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0; // (CRefCounted's: no smart pointers)
        return r.ecx;
    });
    s.At({0x004AAB30, AddressOf(&Front::CScreenElement::DestroyBody)}, 0, [](Regs& r) {
        Rec(0xAB30, {r.ecx});
        return 0u;
    });
    s.At({0x004A96B0, AddressOf(&Front::CScreenElement::SetProperties)}, 4, [](Regs& r) {
        Rec(0x96B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004A9AA0, AddressOf(&Front::CScreenElement::SetMorph)}, 4, [](Regs& r) {
        Rec(0x9AA0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Rec(0x1970, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.ecx, r.args[0]});
        return 0u;
    });

    // The composite objects, their manager, the scripts.
    s.At({0x0045F330, AddressOf(&Obj::CCompositeObjectManager::CreateCompositeObject)}, 0, [](Regs& r) {
        Rec(0xF330, {r.ecx});
        if (g_el3d_noObject)
            return 0u;
        uint32_t object = At(kObjects + 0x400 * g_el3d_nextObject++);
        *reinterpret_cast<void***>(object) = g_el3d_objectVtable;
        *reinterpret_cast<uint32_t*>(object + 4) = 0; // no smart pointers yet
        *reinterpret_cast<void***>(object + 0x140) = g_el3d_componentVtable;
        *reinterpret_cast<uint32_t*>(object + 0x140 + 0x208) = object + 0x360;
        float box[8] = {-200.0f + g_el3d_nextObject, -100.5f, -1.0f - g_el3d_nextObject, 1.0f,
                        210.25f, 99.0f + 3 * g_el3d_nextObject, 1.5f, 1.0f};
        std::memcpy(reinterpret_cast<void*>(object + 0x380), box, sizeof(box));
        return object;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x5790, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                  bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                          r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             return At(kScript);
         });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return r.ecx + 0x140;
    });
    s.At({0x0045D990, AddressOf(&Obj::CCompositeObject::Hide)}, 4, [](Regs& r) {
        Rec(0xD990, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00494970}, 4, [](Regs& r) {
        Rec(0x4970, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00493CD0}, 4, [](Regs& r) {
        Rec(0x3CD0, {r.ecx, r.args[0]});
        return r.ecx + 0x10;
    });
    s.At({0x0048DE80}, 0, [](Regs& r) {
        Rec(0xDE80, {r.ecx});
        return r.ecx + 0x10; // (the model at +0x360: its geometry +0x370, the box +0x380)
    });
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Rec(0xC1B0, {r.args[0], r.args[1] & 0xFF});
        return At(kNode);
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0, [](Regs& r) {
        Rec(0xC050, {r.args[0], InArena(r.args[1]) ? r.args[1] : 0x57ACu}); // (a local: on the stack)
        const float pos[4] = {17.5f, -3.25f, 400.0625f, 1.0f};
        std::memcpy(reinterpret_cast<void*>(r.args[1]), pos, sizeof(pos));
        return 0u;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Rec(0x0AD0, {r.args[0]});
        return g_el3d_found ? At(g_el3d_found) : 0u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        return g_el3d_found ? At(g_el3d_found) : 0u;
    });
    s.At({0x004A09C0, AddressOf(&Nx::GetActiveCamera)}, 0, [](Regs& r) {
        Rec(0x09C0, {r.args[0]});
        return g_el3d_camera_on ? At(kCamera) : 0u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        g_el3d_time += 1237;
        return g_el3d_time;
    });
    s.Bytes(0x00485B10, Branch(0xE9, 0x00485B10, reinterpret_cast<void*>(&StandInCameraAngle)));
    const uint32_t angle = AddressOf(&Gfx::Camera::GetAdjustedHFOV); // (ours calls ours)
    s.Bytes(angle, Branch(0xE9, angle, reinterpret_cast<void*>(&StandInCameraAngle)));

    for (void*& slot : g_el3d_objectVtable)
        slot = nullptr;
    g_el3d_objectVtable[6] = s.Thunk(0, [](Regs& r) {
        Rec(0x0D6, {r.ecx});
        return 0u;
    });
    g_el3d_objectVtable[8] = s.Thunk(0, [](Regs& r) {
        Rec(0x0D8, {r.ecx});
        return 0u;
    });
    for (void*& slot : g_el3d_componentVtable)
        slot = nullptr;
    g_el3d_componentVtable[1] = s.Thunk(0, [](Regs& r) {
        Rec(0x0C1, {r.ecx});
        return 0u;
    });
}

// The parameter structures of the test (arena offsets).
uint32_t Params(int n) { return At(kStructs + 0x40 * n); }

void SetUpAnswers()
{
    g_el3d_answers.clear();
    using F = float;
    // 0: a model, with most of the properties.
    uint32_t p = Params(0);
    Answer('C', p, 0x286A8D26, {1});
    Answer('F', p, 0xED7C6031, {Words(-500.0f)});
    Answer('F', p, 0x0AFFD09D, {Words(0.25f)});
    Answer('F', p, 0x7DF8E00B, {Words(-7.5f)});
    Answer('F', p, 0xE4F1B1B1, {Words(12.75f)});
    Answer('F', p, 0xC1608989, {Words(0.1f)});
    Answer('F', p, 0xB667B91F, {Words(-0.3f)});
    Answer('F', p, 0x2F6EE8A5, {Words(1.7f)});
    Answer('L', p, 0x233F6B18, {1}); // AngleOrderXYZ then ZYX: the last one wins
    Answer('L', p, 0xCEB5DE5A, {1});
    Answer('K', p, 0xB2A86EB5, {0x1234});
    Answer('F', p, 0xE3C07609, {Words(15.0f)});
    Answer('S', p, 0x00CA1C41, {Params(1)});
    Answer('F', Params(1), 0xC9FDE32C, {Words(4.5f)});
    Answer('F', Params(1), 0xA80BEA4A, {Words(3.0f)});
    Answer('V', p, Crc::GenerateCRCFromString("PivotPoint"), {Words(1.0f), Words(-2.0f), Words(0.5f), Words(9.0f)});
    Answer('L', p, 0x4564C8F2, {1});
    Answer('F', p, 0x571F3F3A, {Words(0.004f)});
    Answer('F', p, 0x31E78F98, {Words(2.5f)});
    Answer('I', p, 0x655EE08E, {3});
    // 2: two sectors (the second without its own offset), pointed at an object, a hover without a frequency,
    // scale_to_screen_dims.
    p = Params(2);
    Answer('C', p, 0x4A6BF967, {1});
    Answer('S', p, 0x4A6BF967, {At(kArray)});
    auto* array = reinterpret_cast<uint32_t*>(At(kArray));
    array[1] = 2;
    array[4] = Params(3);
    array[5] = Params(4);
    Answer('K', Params(3), 0, {0xAAAA0001});
    Answer('V', Params(3), 0, {Words(5.0f), Words(-6.0f), Words(7.0f), Words(1.0f)});
    Answer('K', Params(4), 0, {0xAAAA0002});
    Answer('K', p, 0xDAD39B88, {0x777});
    Answer('L', p, 0xF01FDCA4, {1});
    Answer('S', p, 0x00CA1C41, {Params(5)});
    Answer('F', Params(5), 0xC9FDE32C, {Words(2.0f)});
    Answer('L', p, 0x1B399065, {1});
    Answer('L', p, 0x3BE6306E, {1});
    // 6: one sector, ParentParams with a name, an offset, the skater integer and a node.
    p = Params(6);
    Answer('C', p, 0xB45C2617, {1});
    Answer('K', p, 0xB45C2617, {0xBBBB0001});
    Answer('S', p, 0x36388933, {Params(7)});
    Answer('K', Params(7), 0xA1DC81F9, {0x4242});
    Answer('V', Params(7), 0, {Words(0.5f), Words(1.5f), Words(-2.5f), Words(1.0f)});
    Answer('I', Params(7), 0x5B8AB877, {1});
    Answer('K', Params(7), 0x7A8017BA, {0x9999});
    // 8: nothing but CloneModel and the point-at-nothing flag; 9: SetMorph's flag.
    Answer('C', Params(8), 0x073EC4C8, {1});
    Answer('L', Params(8), 0xF01FDCA4, {1});
    Answer('L', Params(9), Crc::GenerateCRCFromString("scale_to_screen_dims"), {1});
    (void)sizeof(F);
}

std::vector<uint32_t> RunClass(bool original)
{
    g_el3d_base = reinterpret_cast<uint32_t>(g_el3d_arenas[original ? 0 : 1]);
    uint8_t* arena = g_el3d_arenas[original ? 0 : 1];
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + kElement, 0xCD, 0x510);
    g_trace.clear();
    g_el3d_nextObject = 0;
    g_el3d_noObject = false;
    g_el3d_camera_on = 1;
    g_el3d_found = 0;
    g_el3d_time = 5000;
    g_el3d_angle = 72.0f;
    SetUpAnswers();
    GameVariable<uint32_t>(0x006F0FB8) = At(kManager);
    GameVariable<uint32_t>(0x007D1800) = At(kManager + 0x80);
    GameVariable<int32_t>(0x00786D6C) = 1920;
    GameVariable<float>(0x00701340) = 1.7777778f;
    auto* camMatrix = reinterpret_cast<Mth::Matrix*>(At(kCamera + 0x6C));
    *camMatrix = TestMatrix(3);
    *reinterpret_cast<Mth::Vector*>(At(kCamera + 0x5C)) = {10.0f, 20.0f, -30.0f, 1.0f};
    // Objects found by id / FindObject: positions at +0x4C.
    *reinterpret_cast<Mth::Vector*>(At(kTargets + 0x4C)) = {-55.5f, 12.0f, 900.0f, 1.0f};

    auto* e = reinterpret_cast<CElement3d*>(At(kElement));
    auto call = [&](uint32_t address, auto ours, auto... args) {
        if (original)
            Original<void(__thiscall*)(void*, decltype(args)...)>(address)(e, args...);
        else
            (e->*ours)(args...);
    };
    // (the element's words, marked so a difference can be placed: 0x5AA50000 + the snapshot's number)
    uint32_t snapshots = 0;
    auto snapshot = [&]() {
        g_trace.push_back(0x5AA50000u + snapshots++);
        for (uint32_t i = 0; i < 0x510 / 4; ++i)
            g_trace.push_back(Rel(reinterpret_cast<uint32_t*>(e)[i]));
        // (and where the models' objects were put: position, display matrix)
        for (int o = 0; o < 6; ++o) {
            const auto* words = reinterpret_cast<const uint32_t*>(At(kObjects + 0x400 * o));
            g_trace.insert(g_trace.end(), words + 0x4C / 4, words + 0x5C / 4);
            g_trace.insert(g_trace.end(), words + 0xC0 / 4, words + 0x100 / 4);
        }
    };

    // The constructor (returns this).
    uint32_t r = original ? reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*)>(0x004A6060)(e))
                          : reinterpret_cast<uint32_t>(e->Construct());
    Rec(0xC70, {r});
    snapshot();
    // SetProperties: a model and most properties; sectors with scale_to_screen_dims; a sector with ParentParams;
    // CloneModel; nothing at all.
    call(0x004A6630, &CElement3d::SetProperties, reinterpret_cast<CStruct*>(Params(0)));
    snapshot();
    call(0x004A6630, &CElement3d::SetProperties, reinterpret_cast<CStruct*>(Params(2)));
    snapshot();
    // SetMorph without and with scale_to_screen_dims (the element's size changed first).
    e->m_width = 300.0f;
    e->m_height = 150.0f;
    call(0x004A6BA0, &CElement3d::SetMorph, reinterpret_cast<CStruct*>(Params(8)));
    call(0x004A6BA0, &CElement3d::SetMorph, reinterpret_cast<CStruct*>(Params(9)));
    snapshot();
    // ScaleToScreenDims on its own: deeper than wide, a tall box in a short element (the vertical fit wins), a NaN
    // in the second model's box (not taken as the minimum).
    {
        auto box = [&](int object, float x0, float y0, float z0, float x1, float y1, float z1) {
            const float b[8] = {x0, y0, z0, 1.0f, x1, y1, z1, 1.0f};
            std::memcpy(reinterpret_cast<void*>(At(kObjects + 0x400 * object + 0x380)), b, sizeof(b));
        };
        box(1, -10.0f, -5.0f, -50.0f, 10.0f, 5.0f, 60.0f);
        box(2, -12.0f, -4.0f, -40.0f, 9.0f, 6.0f, 45.0f);
        call(0x004A62B0, &CElement3d::ScaleToScreenDims);
        snapshot();
        e->m_width = 100.0f;
        e->m_height = 80.0f;
        box(1, -10.0f, -500.0f, -1.0f, 10.0f, 450.0f, 1.0f);
        box(2, -12.0f, -400.0f, -2.0f, 9.0f, 600.0f, 2.0f);
        call(0x004A62B0, &CElement3d::ScaleToScreenDims);
        snapshot();
        box(2, -12.0f, std::nanf(""), -2.0f, 9.0f, 600.0f, 2.0f);
        call(0x004A62B0, &CElement3d::ScaleToScreenDims);
        snapshot();
    }

    // Updates on the layout: pointing at nothing / a node / an object, hover, distance scaling, angle orders.
    e->m_summed.alpha = 1.0f;
    e->m_summed.left = 100.25f;
    e->m_summed.top = 50.5f;
    e->m_summed.scaleX = 2.1f;
    e->m_summed.scaleY = 2.2f;
    e->m_summed.baseScaleX = 1.25f;
    e->m_summed.baseScaleY = 0.75f;
    e->m_parentObject = 0;
    e->m_parentNode = 0;
    struct UpdateCase {
        int pointAt, order, width;
        bool camera, hover, distance;
        float alpha, distanceScale, maxScale;
        float angle0, angle1, angle2;
        bool clearSlot1;
    };
    const UpdateCase cases[] = {
        {0, 2, 1920, true, true, true, 1.0f, 0.004f, 2.5f, 0.3f, -0.4f, 0.5f, false},
        {1, 0, 1920, true, false, true, 1.0f, 0.00001f, 2.5f, 7.0f, -9.5f, 13.0f, false},   // (clamped to 1)
        {2, 1, 640, true, true, true, 0.00005f, 1.0f, 2.5f, -20.0f, 3.0f, 0.0f, false},     // (clamped to max)
        {0, 3, 640, false, true, true, 1.0f, 0.004f, 2.5f, 0.0f, 0.0f, 6.3f, false},        // no camera
        {1, 4, 1920, true, true, false, 0.0001f, 0.004f, 2.5f, 1.0f, 2.0f, 3.0f, false},
        {0, 5, 1920, true, true, true, 1.0f, 0.004f, 1e30f, 1.0f, 2.0f, 3.0f, true},       // an empty slot
        {0, 7, 1920, true, false, false, 1.0f, 0.004f, 2.5f, -1.0f, -2.0f, -3.0f, false},  // no such order
    };
    g_el3d_found = kTargets;
    for (const UpdateCase& c : cases) {
        e->m_pointAt = c.pointAt;
        e->m_angleOrder = c.order;
        GameVariable<int32_t>(0x00786D6C) = c.width;
        g_el3d_camera_on = c.camera;
        if (!c.hover)
            e->m_hoverAmplitude = 0.0f;
        else if (e->m_hoverAmplitude == 0.0f)
            e->m_hoverAmplitude = 3.0f;
        e->m_scaleWithDistance = c.distance;
        e->m_tilt = 15.0f + c.order;       // (pointing at something: tilted)
        e->m_pivot.w = 0.25f * c.order;   // (a pivot's w isn't always 1)
        e->m_summed.alpha = c.alpha;
        e->m_distanceScale = c.distanceScale;
        e->m_maxScale = c.maxScale;
        e->m_angle[0] = c.angle0;
        e->m_angle[1] = c.angle1;
        e->m_angle[2] = c.angle2;
        if (c.clearSlot1)
            e->m_models[1].object.Release();
        call(0x004A6D30, &CElement3d::Update);
        snapshot();
    }
    // Following a node, an object, then the object gone (KillElement3d; nothing more after).
    g_el3d_camera_on = 1;
    e->m_parentNode = 0x9999;
    call(0x004A6D30, &CElement3d::Update);
    snapshot();
    e->m_parentObject = 0x4242;
    call(0x004A6D30, &CElement3d::Update);
    snapshot();
    g_el3d_found = 0;
    call(0x004A6D30, &CElement3d::Update);
    snapshot();
    call(0x004A6D30, &CElement3d::Update);
    snapshot();
    // A sector with ParentParams; CloneModel; no models: no Update.
    e->m_dead = false;
    call(0x004A6630, &CElement3d::SetProperties, reinterpret_cast<CStruct*>(Params(6)));
    snapshot();
    call(0x004A6630, &CElement3d::SetProperties, reinterpret_cast<CStruct*>(Params(8)));
    snapshot();
    call(0x004A5E80, &CElement3d::ApplyViewport);
    call(0x004A5E30, &CElement3d::ClearModels);
    call(0x004A6D30, &CElement3d::Update);
    snapshot();
    // Models again, then the destructors (the scalar deleting one frees with bit 0 only).
    call(0x004A6630, &CElement3d::SetProperties, reinterpret_cast<CStruct*>(Params(0)));
    call(0x004A6630, &CElement3d::SetProperties, reinterpret_cast<CStruct*>(Params(0)));
    r = original ? reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, int)>(0x004A6D00)(e, 2))
                 : reinterpret_cast<uint32_t>(e->Destroy(2));
    Rec(0xD00, {r});
    call(0x004A6630, &CElement3d::SetProperties, reinterpret_cast<CStruct*>(Params(0)));
    r = original ? reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, int)>(0x004A6D00)(e, 3))
                 : reinterpret_cast<uint32_t>(e->Destroy(3));
    Rec(0xD00, {r});
    call(0x004A6240, &CElement3d::DestroyBody);

    std::vector<uint32_t> trace = g_trace;
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    for (uint32_t& word : image)
        word = Rel(word);
    trace.insert(trace.end(), image.begin(), image.end());
    return trace;
}

void TestElement3dClass()
{
    g_el3d_saved = {{0x006F0FB8, 4, {}}, {0x007D1800, 4, {}}, {0x00786D6C, 4, {}}, {0x00701340, 4, {}}};
    SaveAll(g_el3d_saved);
    bool widescreenMenus = Nx::WidescreenMenus();
    Nx::WidescreenMenus() = false;
    SameMath math;
    auto* s = new StandIns;
    InstallStandIns(*s);
    std::vector<uint32_t> expected = RunClass(true);
    std::vector<uint32_t> actual = RunClass(false);
    int updates = 0;
    for (uint32_t word : expected)
        updates += word == 0x0C1; // (component updates: the main path ran)
    Nx::WidescreenMenus() = widescreenMenus;
    Cleanup();
    Compare(expected, actual, "element3d: construct, properties, morph, updates, models, destructors");
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 12; ++i) {
        if (expected[i] == actual[i])
            continue;
        size_t mark = i;
        while (mark > 0 && (expected[mark] & 0xFFFF0000u) != 0x5AA50000u)
            --mark;
        Log("  element3d: snapshot %u, element +0x%X (entry %u): original %08X ours %08X", expected[mark] & 0xFFFF,
            static_cast<unsigned>((i - mark - 1) * 4), static_cast<unsigned>(i), expected[i], actual[i]);
        ++shown;
    }
    Check(updates >= 10, "element3d: the updates placed models");
}

// --- the widescreen front end's rule ---------------------------------------------------------------------------

struct RuleState {
    std::vector<Saved> saved{{0x007CE478, 4, {}}};
    bool setting = Nx::WidescreenMenus();
    int width = Sys::WindowWidth(), height = Sys::WindowHeight();
    RuleState() { SaveAll(saved); }
    ~RuleState()
    {
        RestoreAll(saved);
        Nx::WidescreenMenus() = setting;
        Sys::WindowWidth() = width;
        Sys::WindowHeight() = height;
    }
};

// Worked out (1920x1080: the 4:3 area is 0.75 of the half-width; tan(half angle) 1.2): a flat panel 500 in front
// reaching x -460 / 470 is seen at -0.767 / 0.783 of the half-width (both past 0.95 * 0.75): the screen's edge
// there is 600 away, so the left needs 600 / 460 = 1.3043478, the right 600 / 470; the larger, plus 1%.
void TestElement3dWidescreenRule()
{
    RuleState state;
    Sys::WindowWidth() = 1920;
    Sys::WindowHeight() = 1080;
    alignas(16) static uint8_t career[0x640];
    alignas(16) static uint8_t skater[0x40];
    *reinterpret_cast<void**>(skater + 0x20) = career;
    career[0x630] = 3;
    struct Case {
        bool setting, inLevel;
        float left, right, leftZ, rightZ;
        Mth::Vector place;
        float expected;
    };
    const Mth::Vector centre = {0.0f, 0.0f, -500.0f, 2.0f};
    const Case cases[] = {
        {true, false, -460.0f, 470.0f, 0.0f, 0.0f, centre, static_cast<float>(600.0 / 460.0 * 1.01)},
        {false, false, -460.0f, 470.0f, 0.0f, 0.0f, centre, 1.0f},
        {true, true, -460.0f, 470.0f, 0.0f, 0.0f, centre, 1.0f},
        // off the centre by 10: the left needs (600 + 10) / 460
        {true, false, -460.0f, 470.0f, 0.0f, 0.0f, {10.0f, 5.0f, -500.0f, 2.0f},
         static_cast<float>(610.0 / 460.0 * 1.01)},
        // nearer / further corners: the left at 450 (edge 540), the right at 550 (edge 660): 471 is seen at
        // 0.7136 (stretched by 660 / 471), 470 at 0.7121 (short of 0.7125: unchanged)
        {true, false, -460.0f, 471.0f, 50.0f, -50.0f, centre, static_cast<float>(660.0 / 471.0 * 1.01)},
        {true, false, -460.0f, 470.0f, 50.0f, -50.0f, centre, 1.0f},
        // small (a sign, an icon), one side short, already past both edges, behind the camera
        {true, false, -50.0f, 60.0f, 0.0f, 0.0f, centre, 1.0f},
        {true, false, -300.0f, 470.0f, 0.0f, 0.0f, centre, 1.0f},
        {true, false, -700.0f, 700.0f, 0.0f, 0.0f, centre, 1.0f},
        {true, false, -460.0f, 470.0f, 0.0f, 0.0f, {0.0f, 0.0f, 100.0f, 2.0f}, 1.0f},
    };
    bool ok = true;
    for (const Case& c : cases) {
        Nx::WidescreenMenus() = c.setting;
        GameVariable<void*>(0x007CE478) = c.inLevel ? skater : nullptr;
        Mth::Vector corners[4] = {{c.left, -100.0f, c.leftZ, 1.0f},
                                  {c.left, 100.0f, c.leftZ, 1.0f},
                                  {c.right, -100.0f, c.rightZ, 1.0f},
                                  {c.right, 100.0f, c.rightZ, 1.0f}};
        float k = Front::WidescreenStretch(corners, 4, c.place, 1.2);
        if (FloatBits(k) != FloatBits(c.expected)) {
            Log("  element3d rule (setting %d, level %d, %g..%g): %.9g, expected %.9g", c.setting, c.inLevel, c.left,
                c.right, k, c.expected);
            ok = false;
        }
    }
    // A corner behind the camera (a left one, 600 behind the panel's plane): unchanged, though the others would
    // be stretched.
    {
        Nx::WidescreenMenus() = true;
        GameVariable<void*>(0x007CE478) = nullptr;
        Mth::Vector corners[3] = {{-460.0f, 0.0f, 0.0f, 1.0f}, {470.0f, 0.0f, 0.0f, 1.0f}, {-460.0f, 0.0f, 600.0f, 1.0f}};
        float k = Front::WidescreenStretch(corners, 3, centre, 1.2);
        if (k != 1.0f) {
            Log("  element3d rule (a corner behind the camera): %.9g", k);
            ok = false;
        }
    }
    Check(ok, "element3d: the widescreen front end's stretch (spanning the 4:3 layout only; off and levels unchanged)");
}

// Through Update: a backdrop at the layout's centre, 500 in front of a camera looking down -z with a 90 degree
// angle (tan 1): its box -200..210 by 2.1 reaches x -420..441 (z +-2.1), seen at -0.844 / 0.886 - stretched by
// 497.9 / 420 * 1.01 = 1.1973369 (its x row, compared with the setting off); in a level and off, the same as the
// original's (the class test).
void TestElement3dWidescreenUpdate()
{
    RuleState state;
    g_el3d_saved = {{0x006F0FB8, 4, {}}, {0x007D1800, 4, {}}, {0x00786D6C, 4, {}}, {0x00701340, 4, {}}};
    SaveAll(g_el3d_saved);
    Sys::WindowWidth() = 1920;
    Sys::WindowHeight() = 1080;
    SameMath math;
    auto* s = new StandIns;
    InstallStandIns(*s);
    alignas(16) static uint8_t career[0x640];
    alignas(16) static uint8_t skater[0x40];
    *reinterpret_cast<void**>(skater + 0x20) = career;
    career[0x630] = 3;
    float rows[4][8] = {};
    for (int run = 0; run < 4; ++run) {
        // off; on in the front end; on in a level; on, but the model has no geometry yet
        Nx::WidescreenMenus() = run != 0;
        GameVariable<void*>(0x007CE478) = run == 2 ? skater : nullptr;
        g_el3d_base = reinterpret_cast<uint32_t>(g_el3d_arenas[0]);
        std::memset(g_el3d_arenas[0], 0, kArenaSize);
        g_trace.clear();
        g_el3d_nextObject = 0;
        g_el3d_noObject = false;
        g_el3d_camera_on = 1;
        g_el3d_found = 0;
        g_el3d_angle = 90.0f;
        g_el3d_answers.clear();
        Answer('C', Params(0), 0x286A8D26, {1});
        GameVariable<uint32_t>(0x006F0FB8) = At(kManager);
        GameVariable<int32_t>(0x00786D6C) = 1920;
        GameVariable<float>(0x00701340) = 1.7777778f;
        reinterpret_cast<Mth::Matrix*>(At(kCamera + 0x6C))->Ident();
        *reinterpret_cast<Mth::Vector*>(At(kCamera + 0x5C)) = {0.0f, 0.0f, 0.0f, 1.0f};
        auto* e = reinterpret_cast<CElement3d*>(At(kElement));
        e->Construct();
        e->SetProperties(reinterpret_cast<CStruct*>(Params(0)));
        auto* box = reinterpret_cast<float*>(At(kObjects + 0x380));
        const float panel[8] = {-200.0f, -100.0f, -1.0f, 1.0f, 210.0f, 100.0f, 1.0f, 1.0f};
        std::memcpy(box, panel, sizeof(panel));
        if (run == 3) { // (a second model, without geometry)
            e->AddModel(reinterpret_cast<CStruct*>(Params(0)), {0.0f, 0.0f, 0.0f, 1.0f});
            *reinterpret_cast<uint32_t*>(At(kObjects + 0x400 + 0x140 + 0x208)) = 0;
        }
        e->m_cameraZ = -500.0f;
        e->m_summed.alpha = 1.0f;
        e->m_summed.left = 0.0f;
        e->m_summed.top = 0.0f;
        e->m_width = 640.0f;
        e->m_summed.baseScaleX = 1.0f;
        e->m_height = 448.0f;
        e->m_summed.baseScaleY = 1.0f;
        e->m_summed.scaleX = 2.1f;
        e->m_summed.scaleY = 2.2f;
        e->m_angleOrder = 2;
        e->Update();
        auto* object = reinterpret_cast<Obj::CCompositeObject*>(At(kObjects));
        std::memcpy(rows[run], &object->m_displayMatrix.row[0].x, 32);
    }
    Cleanup();
    double ratio = static_cast<double>(rows[1][0]) / rows[0][0];
    // (the x row stretched, the y row as it was; in a level as with the setting off)
    bool ok = std::fabs(ratio - 497.9 / 420.0 * 1.01) < 2e-5 && rows[1][1] == rows[0][1] &&
              rows[1][5] == rows[0][5] && rows[0][5] != 0.0f && std::memcmp(rows[0], rows[2], sizeof(rows[0])) == 0 &&
              std::memcmp(rows[0], rows[3], sizeof(rows[0])) == 0;
    if (!ok)
        Log("  element3d widescreen update: x row %g %g (off) %g %g (on) %g (level), y %g / %g, ratio %.7f", rows[0][0],
            rows[0][1], rows[1][0], rows[1][1], rows[2][0], rows[0][5], rows[1][5], ratio);
    Check(ok, "element3d: Update stretches a backdrop spanning the layout in the widescreen front end");
}

} // namespace

SELFTEST(Early, TestElement3dMath);
SELFTEST(Early, TestElement3dClass, Cleanup);
SELFTEST(Early, TestElement3dWidescreenRule);
SELFTEST(Early, TestElement3dWidescreenUpdate, Cleanup);
