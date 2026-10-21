// Self-tests for the sun and its lens flare (gfx/lens_flare.cpp, 0x00442670-0x00443870): the original against ours
// on the same stand-in camera, textures, screen elements and script structures; the renderer (the viewport camera,
// the primitive drawer, occlusion queries), the clock, the scripts and the structures' Add* methods are stood in for
// by recording code. The math (vectors, matrices) runs for real in both.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#include "game/core/mem.h"
#include "game/front/screen_element_cfuncs.h"
#include "game/front/screen_element_manager.h"
#include "game/gfx/camera.h"
#include "game/gfx/lens_flare.h"
#include "game/mth/matrix.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using CFuncTests::Bits;
using CFuncTests::Params;
using Script::CArray;
using Script::CStruct;

// --- per-run memory: allocations and structures, recorded relative to the run's blocks ---
alignas(16) uint8_t g_arena[2][0x400];
alignas(16) uint8_t g_structs[2][64][8];
int g_run;
uint32_t g_arenaUsed;
int g_structsUsed;
uint32_t Relative(uint32_t p)
{
    uint32_t arena = reinterpret_cast<uint32_t>(g_arena[g_run]);
    uint32_t structs = reinterpret_cast<uint32_t>(g_structs[g_run]);
    if (p >= arena && p < arena + sizeof(g_arena[0]))
        return 0xA0000000 + (p - arena);
    if (p >= structs && p < structs + sizeof(g_structs[0]))
        return 0xB0000000 + (p - structs);
    return p;
}

// --- stand-in objects (the same in both runs) ---
alignas(16) uint8_t g_camera[0xC4];
alignas(16) uint8_t g_viewport[0x10];
alignas(16) uint8_t g_manager[0x6A4];
alignas(16) uint8_t g_container[0x40];
alignas(16) uint8_t g_dictionary[0x10];
struct FakeTexture {
    void** vtable;
    uint32_t number;
};
FakeTexture g_textures[4];
void* g_textureVtable[2];
struct FakeTextureMethods {
    void* Delete(uint32_t flags)
    {
        auto* texture = reinterpret_cast<FakeTexture*>(this);
        Record({0xDE1E, texture->number, flags});
        return this;
    }
};

// --- what the stand-ins answer ---
bool g_containerThere;
int g_doUpdate;
float g_projX, g_projY;
uint8_t g_queryNumber;
uint32_t g_visible;
uint32_t g_now;
uint32_t g_since;
CArray* g_flareData;

struct FakeViewport {
    float Project(const Mth::Vector* point, float* x, float* y, float* z)
    {
        Record({0x1090, Bits(point->x), Bits(point->y), Bits(point->z), Bits(point->w), Bits(*z)});
        *x = g_projX;
        *y = g_projY;
        *z = 77.0f;
        return 0.5f;
    }
    uint8_t Query(float left, float top, float right, float bottom, int unknown, uint32_t query)
    {
        Record({0x10A0, Bits(left), Bits(top), Bits(right), Bits(bottom), static_cast<uint32_t>(unknown), query & 0xFF});
        return g_queryNumber;
    }
};
// A primitive drawer's memory as far as the code sets it.
void RecordPrim(const uint8_t* prim)
{
    Record({HashBytes(prim, 0x20)});
}
struct FakePrim {
    void SetTexture(void* texture, int a, int b)
    {
        Record({0x1091, reinterpret_cast<uint32_t>(texture), static_cast<uint32_t>(a), static_cast<uint32_t>(b)});
    }
    void SetUnknown(int a, int b, int c)
    {
        Record({0x10A1, static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c)});
    }
};
void* g_project;
void* g_query;
void* g_primTexture;
void* g_primUnknown;
uint32_t g_viewportAddress;
// The two virtual call helpers (vtable slots 1 and 3): the viewport camera's or the primitive drawer's.
__declspec(naked) void VirtualSlot1()
{
    __asm {
        cmp ecx, g_viewportAddress
        jne prim
        jmp dword ptr [g_project]
    prim:
        jmp dword ptr [g_primTexture]
    }
}
__declspec(naked) void VirtualSlot3()
{
    __asm {
        cmp ecx, g_viewportAddress
        jne prim
        jmp dword ptr [g_query]
    prim:
        jmp dword ptr [g_primUnknown]
    }
}
// Script::GetFloat (a float in ST0): a cdecl stand-in of its own.
float __cdecl FakeGetFloat(uint32_t name)
{
    Record({0x8F00, name});
    return name == 0x8BDA33DC ? 0.75f : name == 0x753D46C1 ? 1.5f : 0.25f;
}

uint32_t QueryCallbackKind(uint32_t callback)
{
    return callback == 0x00442810 || Resolve(reinterpret_cast<const void*>(callback)) == AddressOf(&Sun::QuerySunVisibility)
               ? 1u
               : callback;
}

void StandInCalls(StandIns& s)
{
    // The math the original calls goes to ours, as in the game (the hooks): its x87 sine and cosine aren't ours.
    s.Bytes(0x00405950, Branch(0xE9, 0x00405950, reinterpret_cast<void*>(AddressOf(&Mth::Matrix::Rotate))));
    s.At({0x0049F540}, 4, [](Regs& r) {
        Record({0xF540, r.ecx == reinterpret_cast<uint32_t>(g_dictionary), r.args[0]});
        return reinterpret_cast<uint32_t>(&g_textures[r.args[0] & 3]);
    });
    using GetArrayFn = CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return reinterpret_cast<uint32_t>(g_flareData);
    });
    using GetIntegerFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GetIntegerFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return static_cast<uint32_t>(g_doUpdate);
    });
    using GetFloatFn = float (*)(uint32_t);
    s.Bytes(0x00478F00, Branch(0xE9, 0x00478F00, reinterpret_cast<void*>(&FakeGetFloat)));
    s.Bytes(AddressOf(static_cast<GetFloatFn>(&Script::GetFloat)),
            Branch(0xE9, AddressOf(static_cast<GetFloatFn>(&Script::GetFloat)), reinterpret_cast<void*>(&FakeGetFloat)));
    s.At({0x004015D0, AddressOf(&Mem::New)}, 0, [](Regs& r) {
        Record({0x15D0, r.args[0]});
        uint8_t* p = g_arena[g_run] + g_arenaUsed;
        g_arenaUsed += (r.args[0] + 15) & ~15u;
        return reinterpret_cast<uint32_t>(p);
    });
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, [](Regs&) {
        Record({0x9030});
        return reinterpret_cast<uint32_t>(g_structs[g_run][g_structsUsed++]);
    });
    using AddChecksumFn = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Relative(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddFloatFn = void (CStruct::*)(uint32_t, float);
    s.At({0x00477C60, AddressOf(static_cast<AddFloatFn>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, Relative(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddVectorFn = void (CStruct::*)(uint32_t, Script::VectorValue);
    s.At({0x00477F70, AddressOf(static_cast<AddVectorFn>(&CStruct::AddVector))}, 20, [](Regs& r) {
        Record({0x7F70, Relative(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
    using AddArrayFn = void (CStruct::*)(uint32_t, CArray*);
    s.At({0x00478B00, AddressOf(static_cast<AddArrayFn>(&CStruct::AddArray))}, 8, [](Regs& r) {
        Record({0x8B00, Relative(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0x7090, Relative(r.ecx)}); return 0u; });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9071, Relative(r.args[0])});
        return 0u;
    });
    using RunScriptFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunScriptFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], Relative(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x004AD540, AddressOf(&Front::CFunc_DoScreenElementMorph)}, 0, [](Regs& r) {
        Record({0xD540, Relative(r.args[0]), r.args[1]});
        return 1u;
    });
    s.At({0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 12, [](Regs& r) {
        Record({0xAE20, r.ecx == reinterpret_cast<uint32_t>(g_manager), r.args[1], r.args[2]});
        auto* out = reinterpret_cast<Front::ElementPtr*>(r.args[0]);
        out->ConstructFrom(g_containerThere ? reinterpret_cast<Front::CScreenElement*>(g_container) : nullptr);
        return r.args[0];
    });
    s.At({0x004A06D0}, 0, [](Regs& r) { Record({0x06D0, r.args[0]}); return reinterpret_cast<uint32_t>(g_viewport); });
    s.Bytes(0x004A1090, Branch(0xE9, 0x004A1090, reinterpret_cast<void*>(&VirtualSlot1)));
    s.Bytes(0x004A10A0, Branch(0xE9, 0x004A10A0, reinterpret_cast<void*>(&VirtualSlot3)));
    s.At({0x004C5230}, 4, [](Regs& r) {
        Record({0x5230, r.args[0]});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x150); // (the original's frame keeps 0x150-0x15C bytes)
        return r.ecx;
    });
    s.At({0x004C5A20}, 0, [](Regs& r) {
        Record({0x5A20});
        RecordPrim(reinterpret_cast<const uint8_t*>(r.ecx));
        return 0u;
    });
    s.At({0x0049C6C0}, 0, [](Regs& r) {
        Record({0xC6C0});
        reinterpret_cast<uint8_t*>(r.ecx)[0x15] = 1;
        return 0u;
    });
    s.At({0x0049C6E0}, 16, [](Regs& r) {
        Record({0xC6E0, r.args[3]});
        RecordPrim(reinterpret_cast<const uint8_t*>(r.ecx));
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 48),
                HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 16),
                HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 32)});
        return 0u;
    });
    s.At({0x0049C700}, 0, [](Regs& r) { Record({0xC700, r.args[0], r.args[1]}); return 0u; });
    s.At({0x004A0EB0}, 0, [](Regs& r) {
        auto* request = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x0EB0, QueryCallbackKind(request[1]), request[2], request[3] & 0xFF});
        return 0u;
    });
    s.At({0x004A0F20}, 0, [](Regs& r) { Record({0x0F20, r.args[0] & 0xFF}); return g_visible; });
    s.At({0x0041C200}, 0, [](Regs&) { Record({0xC200}); return g_now++; });
    s.At({0x00420250}, 0, [](Regs& r) { Record({0x0250, r.args[0]}); return g_since; });
}

// The sun's state, recorded (pointers made run-relative).
void RecordState()
{
    const uint32_t* words = reinterpret_cast<const uint32_t*>(0x00696878);
    for (int i = 0; i < (0x006968D0 - 0x00696878) / 4; ++i)
        Record({i == (0x00696894 - 0x00696878) / 4 ? words[i] & 0xFFFFFF : Relative(words[i])});
}

void SetCamera(float x, float y, float z, float yaw, float factor)
{
    auto* camera = reinterpret_cast<Gfx::Camera*>(g_camera);
    camera->m_pos = {x, y, z, 1.0f};
    Mth::Matrix& m = camera->m_matrix;
    m.Ident();
    m.RotateY(yaw);
    camera->m_unknownB4 = factor;
}

template <typename F>
F Pick(bool original, uint32_t address, F ours)
{
    return original ? Original<F>(address) : ours;
}

void TestSunAndLensFlare()
{
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x00696878, 0x58, {}}, {0x00700F20, 4, {}},
                                {0x00701440, 4, {}}};
    SaveAll(saved);
    CFuncTests::UseEmptySymbolTable();
    // the lens flare data: three elements
    static Params items[3];
    static uint32_t rgbaValues[3][4] = {{255, 200, 100, 50}, {1, 2, 3, 4}, {0x80, 0xFF, 0x7F, 0x10}};
    static CArray rgbas[3];
    static uint32_t itemElements[3];
    static CArray flareData;
    const float posScales[3] = {0.5f, -1.25f, 2.0f};
    const float scales[3] = {1.0f, 0.75f, 3.5f};
    for (int i = 0; i < 3; ++i) {
        new (&items[i]) Params;
        std::memset(&rgbas[i], 0, sizeof(CArray));
        rgbas[i].m_type = Script::ESYMBOLTYPE_INTEGER;
        rgbas[i].m_size = 4;
        rgbas[i].m_elements = rgbaValues[i];
        items[i].Float(0xEAA2389E, posScales[i]).Float(0x13B9DA7B, scales[i]).Array(0x3F6BCDBA, &rgbas[i])
            .Checksum(0x40C698AF, 0x1D000 + i);
        itemElements[i] = reinterpret_cast<uint32_t>(items[i].Struct());
    }
    std::memset(&flareData, 0, sizeof(flareData));
    flareData.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    flareData.m_size = 3;
    flareData.m_elements = itemElements;
    g_flareData = &flareData;
    for (int i = 0; i < 4; ++i)
        g_textures[i] = {g_textureVtable, 0x7E0 + static_cast<uint32_t>(i)};
    g_textureVtable[0] = reinterpret_cast<void*>(AddressOf(&FakeTextureMethods::Delete));
    g_viewportAddress = reinterpret_cast<uint32_t>(g_viewport);
    g_project = reinterpret_cast<void*>(AddressOf(&FakeViewport::Project));
    g_query = reinterpret_cast<void*>(AddressOf(&FakeViewport::Query));
    g_primTexture = reinterpret_cast<void*>(AddressOf(&FakePrim::SetTexture));
    g_primUnknown = reinterpret_cast<void*>(AddressOf(&FakePrim::SetUnknown));
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    using namespace Sun;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_run = run;
        g_trace.clear();
        g_arenaUsed = 0;
        g_structsUsed = 0;
        std::memset(g_arena[run], 0xCD, sizeof(g_arena[0]));
        std::memset(g_structs[run], 0xCD, sizeof(g_structs[0]));
        std::memset(reinterpret_cast<void*>(0x00696878), 0, 0x58);
        std::memset(g_manager, 0, sizeof(g_manager));
        std::memset(g_container, 0, sizeof(g_container));
        std::memset(g_camera, 0, sizeof(g_camera));
        GameVariable<void*>(0x00700F20) = g_dictionary;
        GameVariable<void*>(0x00701440) = g_manager;
        g_now = 1000;
        g_since = 0;
        g_containerThere = true;
        g_doUpdate = 1;
        g_queryNumber = 0;
        g_visible = 0;
        g_projX = 100.5f;
        g_projY = 200.25f;

        // clamping
        for (float value : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN()}) {
            float clamped = Pick(original, 0x00442680, &ClampToRange)(value, 0.0f, 1.0f);
            Record({Bits(clamped)});
            clamped = Pick(original, 0x00442680, &ClampToRange)(value, 1.0f, 0.0f);
            Record({Bits(clamped)});
        }
        // setting up
        Pick(original, 0x004429D0, &Init)(0.5f, 0.25f, 5000.0f, 300.0f, 0x7E1, 0x7E2, 0x1F1F);
        RecordState();
        // SetSun: every part, the textures replaced (the corona's replacement deleting the sun's)
        {
            Settings settings;
            std::memset(&settings, 0, sizeof(settings));
            settings.theta = 1.25f;
            settings.phi = -0.5f;
            settings.radius = 1000.0f;
            settings.size = 64.0f;
            settings.texture = 0x7E3;
            settings.coronaTexture = 0x7E0;
            settings.color = {10.0f, 20.0f, 30.0f, 40.0f};
            settings.shown = true;
            for (uint32_t mask : {0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u, 0xFFu, 0x60u}) {
                settings.theta += 0.125f;
                Pick(original, 0x004426D0, &SetSun)(&settings, mask);
                RecordState();
            }
            GameVariable<void*>(0x00696898) = nullptr;
            Pick(original, 0x004426D0, &SetSun)(&settings, 0x40);
            GameVariable<void*>(0x0069689C) = nullptr;
            Pick(original, 0x004426D0, &SetSun)(&settings, 0x40);
            settings.shown = false;
            Pick(original, 0x004426D0, &SetSun)(&settings, 0x80);
            RecordState();
            settings.shown = true;
            Pick(original, 0x004426D0, &SetSun)(&settings, 0x9F);
        }
        // the query's callback
        GameVariable<Mth::Vector>(0x006968C0) = {1.5f, -2.5f, 3.25f, 1.0f};
        for (uint8_t number : std::initializer_list<uint8_t>{0, 3}) {
            g_queryNumber = number;
            Record({static_cast<uint32_t>(Pick(original, 0x00442810, &QuerySunVisibility)(nullptr, 9))});
            RecordState();
        }
        // placing, updating, hiding and showing
        {
            Mth::Vector sun = {500.0f, 100.0f, 0.0f, 1.0f};
            Mth::Vector centre = {320.0f, 240.0f, 0.0f, 1.0f};
            GameVariable<bool>(0x00696896) = true;
            Pick(original, 0x004428F0, &PlaceFlareElements)(0.375f, &sun, &centre);
            RecordState();
            Params a;
            Script::VectorValue sunPos = {12.0f, 34.0f, 0.0f, 1.0f};
            Script::VectorValue centrePos = {300.0f, 200.0f, 0.0f, 1.0f};
            a.Vector(0x2E089F23, &sunPos).Vector(0xBF0F14DB, &centrePos).Float(0xA99F828E, 0.5f);
            Pick(original, 0x00442D70, &UpdateFlare)(a.Struct(), nullptr);
            Params b;
            b.Vector(0x2E089F23, &sunPos);
            Pick(original, 0x00442D70, &UpdateFlare)(b.Struct(), nullptr);
            auto* script = reinterpret_cast<Script::CScript*>(0x5C000);
            for (bool there : {false, true}) {
                g_containerThere = there;
                Pick(original, 0x00442DF0, &HideFlare)(a.Struct(), script);
                RecordState();
                Pick(original, 0x00442DF0, &HideFlare)(nullptr, script);
                Params c;
                c.Float(0xA99F828E, 2.0f);
                Pick(original, 0x00442DF0, &HideFlare)(c.Struct(), script);
                RecordState();
                Pick(original, 0x00443030, &UnhideFlare)(nullptr, script);
                RecordState();
            }
            // ShowFlare: no container (made), DoUpdateLensFlare off, hidden or not
            for (int i = 0; i < 4; ++i) {
                g_containerThere = i != 0;
                g_doUpdate = i == 1 ? 0 : 1;
                GameVariable<bool>(0x00696896) = i == 2;
                Pick(original, 0x00443780, &ShowFlare)(&sun, 0.125f * static_cast<float>(i));
                RecordState();
            }
            g_containerThere = true;
            g_doUpdate = 1;
        }
        // the quad
        {
            SetCamera(10.0f, 20.0f, 30.0f, 0.75f, 1.25f);
            alignas(16) uint8_t prim[0x200];
            std::memset(prim, 0, sizeof(prim));
            float corners[12];
            Mth::Vector at = {100.0f, -50.0f, 2000.0f, 1.0f};
            Pick(original, 0x00443180, &BuildSunQuad)(reinterpret_cast<Gfx::Camera*>(g_camera), prim, &at, corners);
            RecordPrim(prim);
            Record({HashBytes(reinterpret_cast<const uint8_t*>(corners), sizeof(corners))});
            GameVariable<void*>(0x00696898) = nullptr;
            Pick(original, 0x00443180, &BuildSunQuad)(reinterpret_cast<Gfx::Camera*>(g_camera), prim, &at, corners);
            Record({HashBytes(reinterpret_cast<const uint8_t*>(corners), sizeof(corners))});
        }
        // drawing the sun
        {
            struct Case {
                bool shown;
                float yaw, theta, phi;
                float projX, projY;
                uint8_t query;
                uint32_t visible, since;
                float burst;
                bool hidden, sunTexture, corona;
            };
            const Case cases[] = {
                {false, 0, 0, 0, 1, 1, 0, 0, 0, 1.0f, false, true, true},          // not shown
                {true, 0.0f, 0.0f, 0.3f, 1, 1, 0, 0, 0, 1.0f, false, true, true},  // behind the camera
                {true, 0.0f, 1.4f, 0.1f, 1, 1, 0, 0, 0, 1.0f, false, true, true},  // just behind (a small dot product)
                {true, 3.0f, 0.1f, 0.3f, 5, 7, 0, 0, 250, 0.25f, false, true, true},   // exactly faded out
                {true, 3.0f, 0.0f, 0.3f, 0, 0, 0, 0, 0, 1.0f, false, true, true},  // off the projection
                {true, 3.0f, 0.1f, 0.3f, 5, 7, 0, 0, 250, 1.0f, false, true, true},     // no query: fading
                {true, 3.0f, 0.1f, 0.3f, 5, 0, 2, 0, 1500, 1.0f, true, true, false},    // nothing seen, faded
                {true, 3.0f, 0.1f, 0.3f, 0, 7, 2, 0, 300, 0.0f, false, false, true},    // a zero fade time
                {true, 3.0f, 0.2f, 0.3f, 5, 7, 2, 900, 100, 1.0f, false, true, true},   // seen, recently hidden
                {true, 3.0f, 0.2f, 0.3f, 5, 7, 2, 70000, 5000, 1.0f, true, true, false}, // seen after long
                {true, 3.0f, 0.2f, 0.3f, 5, 7, 2, 1, 750, 1.0f, false, false, true},    // 750: the morph-in boundary
                {true, 3.0f, 0.1f, 0.3f, 5, 7, 0, 0, 1500, 0.4f, false, true, true},    // 1500: the morph-out boundary
            };
            for (const Case& c : cases) {
                GameVariable<bool>(0x00696895) = c.shown;
                GameVariable<float>(0x00696878) = c.theta;
                GameVariable<float>(0x0069687C) = c.phi;
                GameVariable<float>(0x00696890) = c.burst;
                GameVariable<bool>(0x00696896) = c.hidden;
                GameVariable<void*>(0x00696898) = c.sunTexture ? &g_textures[1] : nullptr;
                GameVariable<void*>(0x0069689C) = c.corona ? &g_textures[2] : nullptr;
                GameVariable<uint8_t>(0x00696894) = c.query;
                SetCamera(5.0f, 6.0f, 7.0f, c.yaw, 0.9f);
                g_projX = c.projX;
                g_projY = c.projY;
                g_visible = c.visible;
                g_since = c.since;
                Pick(original, 0x00443870, &Render)(reinterpret_cast<Gfx::Camera*>(g_camera));
                Record({0xCA5E});
                RecordState();
            }
        }
        for (uint32_t& word : g_trace)
            word = Relative(word);
        for (const auto* block : {g_arena[run], &g_structs[run][0][0]}) {
            size_t size = block == g_arena[run] ? sizeof(g_arena[0]) : sizeof(g_structs[0]);
            for (size_t i = 0; i < size; i += 4) {
                uint32_t word;
                std::memcpy(&word, block + i, 4);
                g_trace.push_back(Relative(word));
            }
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 1200, "sun and lens flare: the original reached its calls");
    Compare(traces[0], traces[1], "sun and lens flare (0x00442670-0x00443870)");
}

} // namespace

SELFTEST(Early, TestSunAndLensFlare, RemoveStandIns);
