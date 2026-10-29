// Self-tests for the bounding box component (src/game/obj/bbox_component.cpp, 0x0041A810-0x0041B274), the object
// search's destructors (objman.cpp: 0x0041A850, 0x0041AA90, 0x0041AAA0), the skate module's skater count
// (skate_module.cpp: 0x0041AD90) and two Mth helpers (0x0041A730 Matrix(angles), 0x0041A820 DistanceSquared): the
// original and ours, every call they make stood in for and recorded (the allocation, the base component, the script
// structure's getters, the node position, the matrix helpers, the object search, the skaters, PointInBox).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/bbox_component.h"
#include "game/obj/objman.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Obj::CBBoxComponent;

constexpr int kOthers = 6;

struct World {
    alignas(16) uint8_t component[0x60];
    alignas(16) uint8_t self[0x120];
    alignas(16) uint8_t others[kOthers][0x120];
    alignas(16) uint8_t otherQuery[0x60];
    alignas(16) uint8_t rect[0x90];
    alignas(16) uint8_t skate[0x40];
    alignas(16) uint8_t nodes[8][0x14];
    alignas(16) uint8_t manager[0x40];
    alignas(16) uint8_t array[0x10];
    uint32_t elements[4];
    void* managerVtable[8];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
void RecordDouble(double d)
{
    uint32_t bits[2];
    std::memcpy(bits, &d, 8);
    Record({bits[0], bits[1]});
}
void RecordVector(const float* v) { Record({Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(v[3])}); }

constexpr uint32_t kOffset = 0xA6F5352F, kAngle = 0xFF7EBAF6, kHeight = 0x0AB21AF0, kDist = 0x7E832F08,
                   kWidth = 0x73E5BAD0, kRadius = 0xC48391A5, kType = 0x7321A8D6, kNode = 0x7A8017BA;

// What the stand-ins answer this run.
struct Answers {
    uint32_t seed;
    uint32_t params;    // P_* bits: which parameters the structure has
    uint32_t typeValue; // type's checksum (when P_TYPE)
    bool callocFails;
    int searchAt;       // the object search's cursor (its sequence: the order of `others`, self among them)
    uint32_t searchType;
};
Answers g_a;
enum : uint32_t {
    P_OFFSET = 1,
    P_ANGLE = 2,
    P_HEIGHT = 4,
    P_DIST = 8,
    P_WIDTH = 0x10,
    P_RADIUS = 0x20,
    P_TYPE = 0x40,
    P_TYPES = 0x80,
    P_NODE = 0x100,
};
bool Has(uint32_t p) { return (g_a.params & p) != 0; }

// The object search's sequence for a type: others 0..5 with self in place of one, depending on the type and seed.
uint8_t* SearchItem(int index)
{
    int length = 3 + static_cast<int>((g_a.searchType ^ g_a.seed) % 4);
    if (index >= length)
        return nullptr;
    if (index == static_cast<int>(g_a.seed % 3))
        return g_world.self;
    return g_world.others[(index + g_a.searchType) % kOthers];
}

void StandIn(StandIns& s)
{
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        std::memset(g_world.component, 0, sizeof(g_world.component));
        return reinterpret_cast<uint32_t>(g_world.component);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    using GetFloatFn = bool (Script::CStruct::*)(uint32_t, float*, int);
    s.At({0x004766D0, AddressOf(static_cast<GetFloatFn>(&Script::CStruct::GetFloat))}, 12, [](Regs& r) {
        uint32_t name = r.args[0];
        auto* out = reinterpret_cast<float*>(r.args[1]);
        Record({0x66D0, r.ecx, name, Rel(r.args[1]), Bits(*out), r.args[2]});
        bool found = name == kAngle    ? Has(P_ANGLE)
                     : name == kHeight ? Has(P_HEIGHT)
                     : name == kDist   ? Has(P_DIST)
                     : name == kWidth  ? Has(P_WIDTH)
                     : name == kRadius ? Has(P_RADIUS)
                                       : false;
        if (!found)
            return 0u;
        float values[] = {1.5f, -2.25f, 7.0f, 0.0f, 33.3f, 5.75f, -0.0f, 12.5f};
        *out = values[(name ^ g_a.seed) % 8] + static_cast<float>(g_a.seed % 5) * 0.37f;
        return 1u;
    });
    using GetVectorFn = bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int);
    s.At({0x00476FB0, AddressOf(static_cast<GetVectorFn>(&Script::CStruct::GetVector))}, 12, [](Regs& r) {
        uint32_t name = r.args[0];
        auto* out = reinterpret_cast<float*>(r.args[1]);
        // (x, y, z uninitialised before: only w recorded)
        Record({0x6FB0, r.ecx, name, Rel(r.args[1]), Bits(out[3]), r.args[2]});
        if (name != kOffset || !Has(P_OFFSET))
            return 0u;
        out[0] = 0.5f * static_cast<float>(g_a.seed) - 2.0f;
        out[1] = 1.25f;
        out[2] = -0.75f * static_cast<float>(g_a.seed % 7);
        out[3] = 1.0f;
        return 1u;
    });
    using GetChecksumFn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&Script::CStruct::GetChecksum))}, 12, [](Regs& r) {
        uint32_t name = r.args[0];
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x6950, r.ecx, name, Rel(r.args[1]), r.args[2]});
        if (name == kType && Has(P_TYPE)) {
            *out = g_a.typeValue;
            return 1u;
        }
        if (name == kNode && Has(P_NODE)) {
            *out = 0x40DE0000 + g_a.seed;
            return 1u;
        }
        return 0u;
    });
    using GetArrayFn = bool (Script::CStruct::*)(uint32_t, Script::CArray**, int);
    s.At({0x00476860, AddressOf(static_cast<GetArrayFn>(&Script::CStruct::GetArray))}, 12, [](Regs& r) {
        uint32_t name = r.args[0];
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x6860, r.ecx, name, Rel(r.args[1]), Rel(*out), r.args[2]});
        if (name != kType || !Has(P_TYPES))
            return 0u;
        *out = reinterpret_cast<uint32_t>(g_world.array);
        return 1u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Rel(r.ecx), r.args[0]});
        return g_world.elements[r.args[0] % 4];
    });
    s.At({0x0045C560, AddressOf(static_cast<bool (*)(uint32_t, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             Record({0xC560, r.args[0], Rel(r.args[1])});
             auto* out = reinterpret_cast<float*>(r.args[1]);
             out[0] = 3.0f * static_cast<float>(g_a.seed);
             out[1] = -4.5f;
             out[2] = 20.0f - static_cast<float>(g_a.seed);
             out[3] = 1.0f;
             return g_a.seed & 1;
         });
    s.At({0x00405600, AddressOf(&Mth::Matrix::Invert)}, 0, [](Regs& r) {
        auto* m = reinterpret_cast<float*>(r.ecx);
        Record({0x5600, Rel(r.ecx), HashBytes(reinterpret_cast<const uint8_t*>(m), 64)});
        for (int i = 0; i < 16; ++i)
            m[i] = m[i] * 0.5f + static_cast<float>(i) - 3.0f;
        return r.ecx;
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Rel(r.args[0]), Rel(r.args[1]), Rel(r.args[2]), HashBytes(reinterpret_cast<const uint8_t*>(a), 64),
                HashBytes(reinterpret_cast<const uint8_t*>(b), 64)});
        float result[16];
        for (int i = 0; i < 16; ++i)
            result[i] = a[i] * 2.0f - b[15 - i];
        std::memcpy(out, result, sizeof(result));
        return r.args[0];
    });
    for (uint32_t address : {0x00404DD0u, 0x00404E20u, 0x00404E70u}) {
        auto ours = address == 0x00404DD0u   ? AddressOf(Mth::CreateRotateXMatrix)
                    : address == 0x00404E20u ? AddressOf(Mth::CreateRotateYMatrix)
                                             : AddressOf(Mth::CreateRotateZMatrix);
        s.At({address, ours}, 0, [address](Regs& r) {
            auto* m = reinterpret_cast<float*>(r.args[0]);
            float angle;
            std::memcpy(&angle, &r.args[1], 4);
            Record({address, Rel(r.args[0]), r.args[1]});
            for (int i = 0; i < 16; ++i)
                m[i] = angle * static_cast<float>(i + 1) + static_cast<float>(address & 0xFF);
            return r.args[0];
        });
    }
    s.At({0x0040AF60, AddressOf(&Mth::Matrix::Transform)}, 8, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        const auto* in = reinterpret_cast<const float*>(r.args[1]);
        const auto* m = reinterpret_cast<const float*>(r.ecx);
        Record({0xAF60, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1])});
        RecordVector(in);
        float result[4];
        for (int i = 0; i < 4; ++i)
            result[i] = in[i] * 0.25f + m[i * 5 % 16];
        std::memcpy(out, result, sizeof(result));
        return r.args[0];
    });
    s.At({0x004817D0, AddressOf(&Mth::PointInBox)}, 0, [](Regs& r) {
        const auto* p = reinterpret_cast<const float*>(r.args[0]);
        Record({0x17D0, Rel(r.args[0]), Rel(r.args[1]), Rel(r.args[2])});
        RecordVector(p);
        RecordVector(reinterpret_cast<const float*>(r.args[1]));
        RecordVector(reinterpret_cast<const float*>(r.args[2]));
        return (Bits(p[0]) ^ Bits(p[1]) ^ g_a.seed) % 3 == 0 ? 1u : 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.otherQuery);
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, Rel(r.ecx), r.args[0]});
        int index = static_cast<int>(r.args[0]);
        if (index == static_cast<int>(g_a.seed % 4))
            return reinterpret_cast<uint32_t>(g_world.self);
        return reinterpret_cast<uint32_t>(g_world.others[(index + 1) % kOthers]);
    });
    s.At({0x00461F30, AddressOf(&Obj::CObjectSearch::FirstItemOfType)}, 8, [](Regs& r) {
        auto* search = reinterpret_cast<uint32_t*>(r.ecx);
        Record({0x1F30, Rel(r.ecx), Rel(search[0]), Rel(search[1]), Rel(r.args[0]), r.args[1]});
        g_a.searchType = r.args[1];
        g_a.searchAt = 0;
        return reinterpret_cast<uint32_t>(SearchItem(0));
    });
    s.At({0x00461ED0, AddressOf(&Obj::CObjectSearch::NextItemOfType)}, 0, [](Regs& r) {
        Record({0x1ED0, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(SearchItem(++g_a.searchAt));
    });
    g_world.managerVtable[6] = s.Thunk(0, [](Regs& r) {
        Record({0x4006, Rel(r.ecx)});
        return 0x11570000u;
    });
}

void FillFloats(Lcg& random, uint8_t* p, size_t size, int spread)
{
    for (size_t i = 0; i < size; i += 4) {
        // (sevenths: values whose sums and squares need more than a float's precision)
        float f = static_cast<float>(static_cast<int>(random.Below(2 * spread + 1)) - spread) / 7.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0x0B1E0000 + seed);
    FillFloats(random, g_world.component, sizeof(g_world.component), 400);
    FillFloats(random, g_world.self, sizeof(g_world.self), 400);
    for (auto& other : g_world.others)
        FillFloats(random, other, sizeof(other), 400);
    FillFloats(random, g_world.otherQuery, sizeof(g_world.otherQuery), 400);
    FillFloats(random, g_world.rect, sizeof(g_world.rect), 400);
    auto* self = reinterpret_cast<Obj::CCompositeObject*>(g_world.self);
    self->mp_manager = reinterpret_cast<Obj::CBaseManager*>(g_world.manager);
    std::memset(g_world.manager, 0, sizeof(g_world.manager));
    *reinterpret_cast<void***>(g_world.manager) = g_world.managerVtable;
    auto* c = reinterpret_cast<CBBoxComponent*>(g_world.component);
    c->m_object = self;
    // The skaters' list: `count` nodes after the head (data -1).
    std::memset(g_world.skate, 0, sizeof(g_world.skate));
    std::memset(g_world.nodes, 0, sizeof(g_world.nodes));
    auto* skate = reinterpret_cast<Mdl::Skate*>(g_world.skate);
    int count = static_cast<int>(seed % 6);
    Mem::ListNode* previous = &skate->m_skaters.m_node;
    skate->m_skaters.m_node.data = reinterpret_cast<void*>(-1);
    for (int i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<Mem::ListNode*>(g_world.nodes[i]);
        node->data = g_world.others[i];
        previous->next = node;
        previous = node;
    }
    previous->next = &skate->m_skaters.m_node;
    // A types array: ped, car, an unknown type, skater.
    std::memset(g_world.array, 0, sizeof(g_world.array));
    auto* array = reinterpret_cast<Script::CArray*>(g_world.array);
    array->m_size = 1 + seed % 4;
    static const uint32_t types[] = {0x061A741E, 0x88C21962, 0x12345678, 0x5B8AB877, 0x3B5737A6};
    for (int i = 0; i < 4; ++i)
        g_world.elements[i] = types[(i + seed) % 5];
}

void DumpWorld(std::vector<uint32_t>& trace)
{
    for (size_t i = 0; i < sizeof(g_world.component) / 4; ++i)
        trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
    for (size_t i = 0; i < sizeof(g_world.rect) / 4; ++i)
        trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.rect)[i]));
    for (size_t i = 0; i < sizeof(g_world.self) / 4; ++i)
        trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.self)[i]));
}

#define BBOX_ADDRESSES                                                                                             {0x0041ADC0, 0x0041AAD0, 0x0041A810, 0x0041A880, 0x0041B1A0, 0x0041A950, 0x0041AB20, 0x0041AC40, 0x0041AE20,           0x0041AF10, 0x0041B0A0, 0x0041A8B0, 0x0041AD90, 0x0041A850, 0x0041AA90, 0x0041AAA0, 0x0041A730, 0x0041A820}

using Rect = CBBoxComponent::Rect;

// The rect's state before a call: not computed (or computed), its sizes and offset from the seed.
void PrepareRect(Lcg& random, int variant)
{
    auto* rect = reinterpret_cast<Rect*>(g_world.rect);
    FillFloats(random, g_world.rect, sizeof(g_world.rect), 800);
    rect->m_computed = variant % 3 == 2;
    rect->m_radiusComputed = variant % 4 == 3;
    if (variant % 5 == 1)
        rect->m_angle = 0.0f;
    if (variant % 7 == 4)
        rect->m_angle = -0.0f;
    if (variant % 6 == 5)
        rect->m_height = std::numeric_limits<float>::quiet_NaN();
    rect->m_radiusSqr = 1234.5f + static_cast<float>(variant);
}

// The component made / constructed / destroyed; the rect's parameters, matrix and radius; whether objects are in it
// or near; the searches for each type; the three members and another (every parameter combination the seeds pick).
void TestBBoxComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x007CE478, 4}});
    for (uint32_t seed = 1; seed <= 48; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            GameVariable<void*>(0x007CE478) = g_world.skate;
            Originals guard(original, BBOX_ADDRESSES);
            std::vector<uint32_t> trace;
            Lcg random(0x9E000000 + seed);
            auto* c = reinterpret_cast<CBBoxComponent*>(g_world.component);
            auto* rect = reinterpret_cast<Rect*>(g_world.rect);
            void* script = reinterpret_cast<void*>(0x5C900000 + seed);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            // Create (and out of memory), the destructors.
            g_a.callocFails = seed == 3;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041ADC0))()));
            g_a.callocFails = false;
            DumpWorld(trace);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041A810))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041A880))(
                c, seed % 3)));
            trace.push_back(Rel(reinterpret_cast<uint32_t*>(c)[0]));
            ResetWorld(seed);
            // InitRect from each parameter combination.
            g_a.params = random.Next() & 0x1FF;
            reinterpret_cast<void(__thiscall*)(void*, void*, void*)>(Code(original, 0x0041A950))(c, rect, params);
            DumpWorld(trace);
            // ComputeRect / RectRadiusSqr from several states.
            for (int variant = 0; variant < 6; ++variant) {
                PrepareRect(random, variant + static_cast<int>(seed));
                reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041AB20))(c, rect);
                double radius = reinterpret_cast<double(__cdecl*)(void*)>(Code(original, 0x0041A8B0))(rect);
                RecordDouble(radius);
                DumpWorld(trace);
            }
            // IsInRect for each other object (sizes that put some inside the bounding distance).
            for (int i = 0; i < kOthers; ++i) {
                PrepareRect(random, i + static_cast<int>(seed));
                if (i & 1) {
                    rect->m_width = 2000.0f;
                    rect->m_offset.x = static_cast<float>(i) * 10.0f;
                }
                trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*)>(Code(original, 0x0041AC40))(
                    c, g_world.others[i], rect));
                DumpWorld(trace);
            }
            // At exactly the bounding distance: inside it (not computed again; the radius is the distance the original
            // computes, rounded as it rounds it).
            for (int i = 0; i < 2; ++i) {
                PrepareRect(random, i + 7 * static_cast<int>(seed));
                rect->m_computed = true;
                rect->m_radiusComputed = true;
                auto* other = reinterpret_cast<Obj::CCompositeObject*>(g_world.others[i + 2]);
                auto* self = reinterpret_cast<Obj::CCompositeObject*>(g_world.self);
                float dx = static_cast<float>((static_cast<double>(rect->m_offset.x) + other->m_pos.x) - self->m_pos.x);
                double dy = (static_cast<double>(rect->m_offset.y) + other->m_pos.y) - self->m_pos.y;
                float middleZ = static_cast<float>(static_cast<double>(rect->m_offset.z) + other->m_pos.z);
                double dz = static_cast<double>(middleZ) - self->m_pos.z;
                rect->m_radiusSqr = static_cast<float>(dz * dz + dy * dy + static_cast<double>(dx) * dx);
                trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*)>(Code(original, 0x0041AC40))(
                    c, other, rect));
            }
            // An object at exactly the radius (integer distances: 3, 4, 12 - 13 from the point): not near.
            for (int i = 0; i < kOthers; ++i) {
                auto* other = reinterpret_cast<Obj::CCompositeObject*>(g_world.others[i]);
                if (i % 2 == static_cast<int>(seed % 2))
                    other->m_pos = {3.0f, -4.0f, 12.0f, 1.0f};
            }
            {
                Mth::Vector origin = {0.0f, 0.0f, 0.0f, 1.0f};
                for (float radiusSqr : {169.0f, 169.00002f}) {
                    trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, float, void*)>(
                        Code(original, 0x0041AE20))(c, nullptr, radiusSqr, &origin));
                    trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*, float, void*, uint32_t, void*)>(
                        Code(original, 0x0041AF10))(c, params, script, radiusSqr, nullptr, 0x88C21962, &origin));
                }
            }
            ResetWorld(seed);
            // The skaters near / in the rect; the other types; unknown types.
            static const uint32_t types[] = {0x5B8AB877, 0x061A741E, 0x3B5737A6, 0x88C21962, 0xD2DB7F97, 0};
            for (int k = 0; k < 8; ++k) {
                PrepareRect(random, k * 3 + static_cast<int>(seed));
                if (k & 2)
                    rect->m_width = 4000.0f;
                Mth::Vector point = {static_cast<float>(k) * 5.0f, 2.0f, -7.0f, 1.0f};
                float radiusSqr = k % 3 == 0 ? 2500.0f : k % 3 == 1 ? 40000.0f : 0.0f;
                Rect* withRect = k & 1 ? rect : nullptr;
                Mth::Vector* withPoint = k & 4 ? &point : nullptr;
                trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, float, void*)>(
                    Code(original, 0x0041AE20))(c, withRect, radiusSqr, withPoint));
                for (uint32_t type : types) {
                    trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*, float, void*, uint32_t, void*)>(
                        Code(original, 0x0041AF10))(c, params, script, radiusSqr, withRect, type, withPoint));
                }
                DumpWorld(trace);
            }
            // QueryObjects and the members, from parameter combinations.
            for (int k = 0; k < 6; ++k) {
                g_a.params = random.Next() & 0x1FF;
                if (k == 0)
                    g_a.params |= P_RADIUS;
                g_a.typeValue = types[random.Below(6)];
                PrepareRect(random, k + 11 * static_cast<int>(seed));
                Mth::Vector point = {static_cast<float>(seed), 0.0f, 1.0f, 1.0f};
                trace.push_back(reinterpret_cast<bool(__thiscall*)(void*, void*, void*, void*, void*)>(
                    Code(original, 0x0041B0A0))(c, params, script, k & 1 ? rect : nullptr, k & 2 ? &point : nullptr));
                for (uint32_t name : {0x6520B902u, 0x67660FE5u, 0xDCE760A1u, 0x6520B903u}) {
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                        Code(original, 0x0041B1A0))(c, name, params, script));
                }
                DumpWorld(trace);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "bbox component: made, destroyed, its rect, objects in the rect / near, "
                                  "the members");
    }
    delete &s;
}

// The object search's destructors, the skater count, Matrix(angles) and DistanceSquared.
void TestBBoxHelpers()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            Originals guard(original, BBOX_ADDRESSES);
            std::vector<uint32_t> trace;
            // the search's destructors
            uint32_t search[3] = {0x5EA00000, 0x5EA00004, seed};
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041AA90))(search);
            trace.push_back(search[0]);
            for (uint32_t address : {0x0041AAA0u, 0x0041A850u}) {
                search[0] = 0x5EA00000;
                trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, address))(
                    search, seed & 3)));
                trace.push_back(search[0]);
            }
            // the skater count (0 to 5 skaters)
            trace.push_back(reinterpret_cast<int(__thiscall*)(void*)>(Code(original, 0x0041AD90))(g_world.skate));
            // Matrix(angles): zeros (-0 too), one angle, NaN
            float nan = std::numeric_limits<float>::quiet_NaN();
            float angleSets[][4] = {{0.0f, 0.0f, 0.0f, 1.0f},  {-0.0f, 0.0f, -0.0f, 1.0f}, {0.5f, 0.0f, 0.0f, 1.0f},
                                    {0.0f, -1.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 1.0f},   {nan, 0.0f, 0.0f, 1.0f},
                                    {0.0f, 0.0f, nan, 1.0f},   {static_cast<float>(seed), 0.25f, -3.0f, 1.0f}};
            for (auto& angles : angleSets) {
                alignas(16) float m[16];
                std::memset(m, 0xCD, sizeof(m));
                trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, const void*)>(
                                        Code(original, 0x0041A730))(m, angles)) == reinterpret_cast<uint32_t>(m));
                for (float f : m)
                    trace.push_back(Bits(f));
            }
            // DistanceSquared (unrounded)
            Lcg random(0xD1500000 + seed);
            for (int k = 0; k < 8; ++k) {
                float a[4], b[4];
                for (int i = 0; i < 4; ++i) {
                    a[i] = static_cast<float>(static_cast<int>(random.Below(200001)) - 100000) / 7.0f;
                    b[i] = static_cast<float>(static_cast<int>(random.Below(200001)) - 100000) / 3.0f;
                }
                RecordDouble(reinterpret_cast<double(__cdecl*)(const void*, const void*)>(Code(original, 0x0041A820))(a, b));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "object search destructors, skater count, Matrix(angles), DistanceSquared");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestBBoxComponent, RemoveStandIns);
SELFTEST(Early, TestBBoxHelpers, RemoveStandIns);
