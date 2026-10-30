// Self-tests for the skater's non-local network logic component (src/game/skater/nonlocal_net_logic.cpp): the
// original and ours on the same stand-in skater, every call they make recorded - its life cycle, its ground lighting
// and shadow, the ground snap, the wall bounce and its turn, the frame's update.

#include <windows.h>

#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>

#include "game/mth/matrix.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/nx/feeler.h"
#include "game/obj/shadow_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/looping_sound.h"
#include "game/skater/nonlocal_net_logic.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "game/skate/game_mode.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;
using Obj::CSkaterNonLocalNetLogicComponent;

// Arena layout
constexpr uint32_t kComponent = 0x0000, kObject = 0x0400, kState = 0x0600, kHistory = 0x0800, kModel = 0x2000,
                   kShadow = 0x2400, kSound = 0x2600, kLight = 0x2800, kPlayer = 0x2A00, kOthers = 0x2C00,
                   kNxModel = 0x2300, kHeap = 0x4000;

struct Answers {
    uint32_t calloc, heap;
    uint32_t hits, triangles, lights; // bit patterns, used up one bit a call
    uint32_t hitCount;
    uint16_t faceFlags;
    float normalY;
    float hitT; // (above 0: where along the line the hit is, its normal the object's up row)
    float snapUp, snapDown, stickAngle, collisionHeight, collisionLength, nonSkatableAngle, dontSlowAngle,
        angleMultiplier, vertexValue, frameSeconds;
    bool observing, waiting, extrapolates;
    uint32_t gameMode;
    int afterState; // what the playback leaves the state in (-1: as it was)
};
Answers g_a;
void* g_vtable[16];

bool NextBit(uint32_t& pattern)
{
    bool bit = (pattern & 1) != 0;
    pattern = (pattern >> 1) | (pattern << 31);
    return bit;
}

void RecordVector(const void* v)
{
    const auto* w = static_cast<const uint32_t*>(v);
    Record({w[0], w[1], w[2], w[3]});
}

uint32_t Allocate(uint32_t size)
{
    uint32_t at = g_a.heap;
    g_a.heap += (size + 15) & ~15u;
    std::memset(At(at), 0xCD, size);
    return Addr(at);
}

float PhysicsAnswer(uint32_t name)
{
    switch (name) {
    case 0xE4D79235: return g_a.snapUp;
    case 0x899BA3D0: return g_a.snapDown;
    case 0x4138283E: return g_a.stickAngle;
    case 0xD4205C9B: return g_a.collisionHeight;
    case 0x20102726: return g_a.collisionLength;
    default: return 1.5f;
    }
}
float ScriptAnswer(uint32_t name)
{
    switch (name) {
    case 0x3EEDE4D3: return g_a.nonSkatableAngle;
    case 0x1483FD01: return g_a.dontSlowAngle;
    case 0xE07EE1A9: return g_a.angleMultiplier;
    default: return 2.5f;
    }
}

// The C runtime's _CIacos (0x00623630: argument and result in ST0) - its x87 path, as ours computes it (its error
// handling reaches C runtime state that isn't set up offline).
__declspec(naked) void CIacosStandIn()
{
    __asm {
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        fxch st(1)
        fpatan
        ret
    }
}

// (`update`: the update's test - the component's own lighting and snap stood in for too)
void StandIn(StandIns& s, bool update)
{
    StandInComponentBase(s, &g_a.calloc);
    s.Bytes(0x00623630, Branch(0xE9, 0x00623630, reinterpret_cast<void*>(&CIacosStandIn)));
    // (the matrix's re-orthonormalisation: ours, tested on its own, isn't bit-identical to the x87 original)
    s.Bytes(0x00405170, Branch(0xE9, 0x00405170, reinterpret_cast<void*>(AddressOf(&Mth::Matrix::OrthoNormalizeAbout))));
    Rec(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 1, 0xDB90, [](Regs& r) {
        switch (r.args[0]) {
        case 0x18223FD6: return Addr(kHistory);
        case 0x43686585: return Addr(kState);
        case 0xB5F1E9D2: return Addr(kSound);
        case 0x286A8D26: return Addr(kModel);
        case 0x8A897DD2: return Addr(kShadow);
        default: return Addr(kOthers) + (r.args[0] & 0xF0);
        }
    });
    // The feeler's query: the line and the filters recorded, a hit made up (the point a quarter or so along it).
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(r.ecx);
        Record({0x4172, r.args[0] & 0xFF, r.args[1] & 0xFF, feeler->m_hit.ignoreFlags, feeler->m_hit.requireFlags});
        RecordVector(&feeler->m_line.start);
        RecordVector(&feeler->m_line.end);
        bool hit = NextBit(g_a.hits);
        if (hit) {
            const Mth::Vector& a = feeler->m_line.start;
            const Mth::Vector& b = feeler->m_line.end;
            float t = 0.0625f * static_cast<float>(1 + g_a.hitCount++ % 7);
            if (g_a.hitT > 0.0f)
                t = g_a.hitT;
            feeler->m_point = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f};
            float nx = 0.13f * static_cast<float>(g_a.hitCount % 5) - 0.3f;
            feeler->m_normal = {nx, g_a.normalY, 0.11f * static_cast<float>(g_a.hitCount % 3) - 0.1f, 0.0f};
            if (g_a.hitT > 0.0f) // (the ground the skater stands on: its up row, tilted a little)
                feeler->m_normal = {F<float>(kObject + 0x7C) + nx * 0.1f, F<float>(kObject + 0x80),
                                    F<float>(kObject + 0x84), 0.25f};
            if (g_a.hitT == 1.0f) // (exactly at the skater)
                feeler->m_point = F<Mth::Vector>(kObject + 0x4C);
            feeler->m_hit.faceFlags = g_a.faceFlags;
        }
        return static_cast<uint32_t>(hit);
    });
    Rec(s, {0x00417120, AddressOf(&Nx::CFeeler::IsHitTriangleCollision)}, 0, 0x7120, [](Regs&) {
        // (the model's light object, its +0x2C, read next - the original through the folded getter 0x00532EB0, ours
        // directly)
        F<uint32_t>(kNxModel + 0x2C) = NextBit(g_a.lights) ? Addr(kLight) : 0u;
        return NextBit(g_a.triangles) ? 1u : 0u;
    }, false);
    RecFloat(s, {0x004174A0, AddressOf(&Nx::CFeeler::GetHitVertexValue)}, 0, 0x74A0,
             [](Regs&) { return g_a.vertexValue; }, false);
    Rec(s, {0x0048E830}, 2, 0xE830);
    Rec(s, {0x0048EDB0}, 1, 0xEDB0, nullptr, false, true);
    s.At({0x004317C0, AddressOf(&Obj::CShadowComponent::SetPosition)}, 4, [](Regs& r) {
        Record({0x17C0, Rel(r.ecx)});
        RecordVector(reinterpret_cast<const void*>(r.args[0]));
        return 0u;
    });
    s.At({0x004317E0, AddressOf(&Obj::CShadowComponent::SetNormal)}, 4, [](Regs& r) {
        Record({0x17E0, Rel(r.ecx)});
        RecordVector(reinterpret_cast<const void*>(r.args[0]));
        return 0u;
    });
    // The values.
    RecFloat(s, {0x00580400, AddressOf(&Obj::GetPhysicsFloat)}, 3, 0x0400,
             [](Regs& r) { return PhysicsAnswer(r.args[0]); }, false, true);
    RecFloat(s, {0x00478F00, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}, 1, 0x8F00,
             [](Regs& r) { return ScriptAnswer(r.args[0]); }, false, true);
    RecFloat(s, {0x005BE910, AddressOf(&Tmr::GetFrameSeconds)}, 0, 0xE910, [](Regs&) { return g_a.frameSeconds; },
             false);
    // The turn (the matrix product as D3DX makes it).
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, Rel(r.args[0]), Rel(r.args[2])});
        for (int i = 0; i < 16; ++i)
            Record({reinterpret_cast<const uint32_t*>(a)[i]});
        float out[16];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                out[i * 4 + j] = static_cast<float>(sum);
            }
        }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    // The update's calls: the component's other functions, the network, the scripts, the looping sound.
    if (update) {
        Rec(s, {0x00514610, AddressOf(&CSkaterNonLocalNetLogicComponent::UpdateGroundLighting)}, 0, 0x4610);
        Rec(s, {0x00514850, AddressOf(&CSkaterNonLocalNetLogicComponent::SnapToGround)}, 0, 0x4850);
    }
    Rec(s, {0x00515940, AddressOf(&CSkaterNonLocalNetLogicComponent::Interpolate)}, 0, 0x5940, [](Regs&) {
        if (g_a.afterState >= 0)
            F<int>(kState + 0x18) = g_a.afterState;
        return 0u;
    });
    Rec(s, {0x00515360, AddressOf(&CSkaterNonLocalNetLogicComponent::Extrapolate)}, 0, 0x5360);
    Rec(s, {0x00516E00, AddressOf(&CSkaterNonLocalNetLogicComponent::ExtrapolateOnRail)}, 0, 0x6E00);
    Rec(s, {0x00516180, AddressOf(&CSkaterNonLocalNetLogicComponent::PlayHistoryEvents)}, 0, 0x6180);
    Rec(s, {0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, 0x7320, [](Regs&) { return Addr(kPlayer); });
    Rec(s, {0x00543320, AddressOf(&GameNet::PlayerInfo::IsObserver)}, 0, 0x3320, [](Regs&) { return g_a.observing ? 1u : 0u; });
    Rec(s, {0x00543330, AddressOf(&GameNet::PlayerInfo::IsSurveying)}, 0, 0x3330, [](Regs&) { return g_a.waiting ? 1u : 0u; });
    Rec(s, {0x00534340, AddressOf(&GameNet::Manager::PlayerCollisionEnabled)}, 0, 0x4340, [](Regs&) { return g_a.extrapolates ? 1u : 0u; });
    Rec(s, {0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, [](Regs&) { return g_a.gameMode; });
    // (a new structure: the original's own pool allocation inlined in one place, NewStruct in the other)
    Rec(s, {0x00409030, AddressOf(&Script::NewStruct)}, 0, 0x9030, [](Regs&) { return Allocate(0x20); }, false, true);
    Rec(s, {0x005BA3D0}, 0, 0x9030, [](Regs&) { return Allocate(0x20); }, false);
    Rec(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0, 0x6410, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(
                            &Script::CStruct::AddChecksum))},
        2, 0x7D40);
    Rec(s, {0x00477090, 0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, 0x7130);
    Rec(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1, 0x9070, nullptr, false, true);
    Rec(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                            &Script::RunScript))},
        5, 0x5790, nullptr, false, true);
    Rec(s, {0x005138B0, AddressOf(&Obj::CSkaterLoopingSoundComponent::SetSpeedFraction)}, 1, 0x38B0);
    for (int i = 0; i < 16; ++i)
        g_vtable[i] = RecThunk(s, 1, 0x7AB00 + i);
}

void ResetWorld(uint32_t seed)
{
    FillArena(seed);
    g_a = {};
    g_a.heap = kHeap;
    g_structs.clear();
    F<uint32_t>(kComponent + 0x0C) = Addr(kObject);
    F<uint32_t>(kComponent + 0x2A4) = Addr(kHistory);
    F<uint32_t>(kComponent + 0x2AC) = Addr(kModel);
    F<uint32_t>(kComponent + 0x2B0) = Addr(kState);
    F<uint32_t>(kComponent + 0x2BC) = Addr(kShadow);
    F<uint32_t>(kModel + 0x208) = Addr(kNxModel); // (the Nx model: +0x2C its light object)
    F<uint32_t>(kSound) = reinterpret_cast<uint32_t>(g_vtable);
    F<uint32_t>(kObject + 0x14) = 0x0B1E0000 + seed;
}

// A unit vector (roughly) from `random`, w 0; now and then a longer one.
void RandomDirection(Lcg& random, float* v)
{
    float x = static_cast<float>(random.Below(201)) / 100.0f - 1.0f;
    float y = static_cast<float>(random.Below(201)) / 100.0f - 1.0f;
    float z = static_cast<float>(random.Below(201)) / 100.0f - 1.0f;
    float length = std::sqrt(x * x + y * y + z * z);
    if (length < 0.01f) {
        x = 1.0f;
        length = 1.0f;
    }
    float scale = random.Below(6) == 0 ? 1.25f : 1.0f;
    v[0] = x / length * scale;
    v[1] = y / length * scale;
    v[2] = z / length * scale;
    v[3] = 0.0f;
}

// An orthonormal-ish matrix for the object: up mostly up, the at row across it.
void RandomMatrix(Lcg& random)
{
    float* m = &F<float>(kObject + 0x6C);
    RandomDirection(random, m + 4);
    if (random.Below(3) != 0) {
        m[4] = m[4] * 0.3f;
        m[5] = 0.9f;
        m[6] = m[6] * 0.3f;
    }
    RandomDirection(random, m + 8);
    // right = up x at
    m[0] = m[5] * m[10] - m[6] * m[9];
    m[1] = m[6] * m[8] - m[4] * m[10];
    m[2] = m[4] * m[9] - m[5] * m[8];
    m[3] = 0.0f;
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

void SetValues(Lcg& random)
{
    g_a.snapUp = static_cast<float>(1 + random.Below(12));
    g_a.snapDown = static_cast<float>(1 + random.Below(30));
    g_a.stickAngle = static_cast<float>(10 + random.Below(80));
    g_a.collisionHeight = static_cast<float>(random.Below(40)) / 2.0f;
    g_a.collisionLength = static_cast<float>(4 + random.Below(60));
    g_a.nonSkatableAngle = static_cast<float>(random.Below(90));
    g_a.dontSlowAngle = static_cast<float>(random.Below(80));
    g_a.angleMultiplier = static_cast<float>(random.Below(9)) / 4.0f;
    g_a.vertexValue = static_cast<float>(random.Below(256)) / 8.0f;
    g_a.normalY = static_cast<float>(random.Below(201)) / 100.0f - 1.0f;
    const uint16_t flags[] = {0x09, 0x01, 0x0B, 0x0D, 0x08, 0x19, 0x109, 0x00};
    g_a.faceFlags = flags[random.Below(8)];
    g_a.hits = random.Next();
    g_a.triangles = random.Next();
    g_a.lights = random.Next();
}

void SetPositions(Lcg& random)
{
    for (uint32_t at : {kObject + 0x4Cu, kComponent + 0x48u, kObject + 0xACu}) {
        for (int i = 0; i < 3; ++i)
            F<float>(at + 4 * i) = static_cast<float>(random.Below(4001)) / 8.0f - 250.0f;
        F<float>(at + 12) = at == kObject + 0x4C ? 1.0f : 0.0f;
    }
    if (random.Below(3) == 0) // (moved little)
        for (int i = 0; i < 3; ++i)
            F<float>(kComponent + 0x48 + 4 * i) = F<float>(kObject + 0x4C + 4 * i) + static_cast<float>(i) * 0.5f;
    RandomDirection(random, &F<float>(kComponent + 0x58));
}

// What the movement functions change (later rounds overwrite it): the object's position, matrix and velocity, the
// last ground's normal.
void RecordSkater(std::vector<uint32_t>& trace)
{
    for (uint32_t i = 0x4C; i < 0xBC; i += 4)
        trace.push_back(F<uint32_t>(kObject + i));
    for (uint32_t i = 0x58; i < 0x68; i += 4)
        trace.push_back(F<uint32_t>(kComponent + i));
}

// The life cycle, Finalize, ResetPlayback; the ground lighting, the snap, the turn and the wall bounce in every case.
void TestSkaterNonLocalNetLogic()
{
    StandIns& s = *new StandIns();
    StandIn(s, false);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            Originals guard(original, {0x005147F0, 0x005144E0, 0x005144B0, 0x00514550, 0x00514580, 0x005144D0,
                                       0x00514610, 0x00514850, 0x00514D10, 0x00514E20});
            std::vector<uint32_t> trace;
            TestLifecycle(original, 0x005147F0, 0x005144B0, 0x00514550, kComponent, 0x2C0, g_a.calloc, trace);
            ResetWorld(seed);
            void* c = At(kComponent);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00514580))(c);
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x005144D0))(c);
            for (uint32_t i = 0; i < 0x2C0; i += 4)
                trace.push_back(Rel(F<uint32_t>(kComponent + i)));
            ResetWorld(seed);
            Lcg random(seed * 7919);
            for (int k = 0; k < 48; ++k) {
                SetValues(random);
                RandomMatrix(random);
                SetPositions(random);
                F<int>(kState + 0x18) = static_cast<int>(random.Below(6));
                F<uint8_t>(kObject + 0xBC) = static_cast<uint8_t>(random.Below(2));
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00514610))(c);
                SetValues(random);
                SetPositions(random);
                if (k % 4 == 1) { // (a ground hit: skatable, its normal up)
                    g_a.hits |= 1;
                    g_a.faceFlags = 0x09;
                    g_a.normalY = 0.95f;
                    g_a.nonSkatableAngle = 20.0f;
                }
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00514850))(c);
                RecordSkater(trace);
                // Standing on the ground (or just above or below it): the last ground about the same, a hit near.
                SetValues(random);
                RandomMatrix(random);
                SetPositions(random);
                g_a.hits |= 1;
                g_a.faceFlags = k % 5 == 2 ? 0x0B : 0x09;
                g_a.nonSkatableAngle = 20.0f;
                g_a.snapUp = static_cast<float>(4 + random.Below(9));
                const float ts[] = {0.0625f, 0.05f, 0.07f, 0.055f, 0.06f};
                g_a.hitT = k % 3 == 0 ? 1.0f : ts[random.Below(5)];
                for (int i = 0; i < 4; ++i)
                    F<float>(kComponent + 0x58 + 4 * i) = F<float>(kObject + 0x7C + 4 * i) * (k % 7 == 1 ? -1.0f : 1.0f);
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00514850))(c);
                RecordSkater(trace);
                g_a.hitT = 0.0f;
                // The turn towards a direction.
                SetValues(random);
                RandomMatrix(random);
                Mth::Vector dir;
                RandomDirection(random, &dir.x);
                if (k % 6 == 2) // (straight along or against the right row: the cosine clamped)
                    for (int i = 0; i < 4; ++i)
                        (&dir.x)[i] = F<float>(kObject + 0x6C + 4 * i) * (k % 12 == 2 ? 1.0001f : -1.0001f);
                float turn = 0.0f;
                double angle = reinterpret_cast<double(__thiscall*)(void*, Mth::Vector, float*)>(
                    Code(original, 0x00514D10))(c, dir, &turn);
                trace.push_back(Bits(static_cast<float>(angle)));
                trace.push_back(Bits(turn));
                // The wall bounce.
                SetValues(random);
                RandomMatrix(random);
                SetPositions(random);
                Mth::Vector normal, point;
                RandomDirection(random, &normal.x);
                for (int i = 0; i < 3; ++i)
                    (&point.x)[i] = F<float>(kObject + 0x4C + 4 * i) + static_cast<float>(random.Below(17)) - 8.0f;
                point.w = 1.0f;
                if (k % 3 == 0) // (moved little: the feeler's within reach)
                    for (int i = 0; i < 3; ++i)
                        F<float>(kComponent + 0x48 + 4 * i) = F<float>(kObject + 0x4C + 4 * i) - 2.0f;
                if (k % 5 == 0) // (no velocity)
                    std::memset(At(kObject + 0xAC), 0, 16);
                F<uint8_t>(kObject + 0xBC) = static_cast<uint8_t>(k % 7 != 3);
                reinterpret_cast<void(__thiscall*)(void*, const void*, const void*)>(Code(original, 0x00514E20))(
                    c, &normal, &point);
                RecordSkater(trace);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater non-local net logic: made, destroyed, finalized, lighting, ground snap, "
                                  "turn, wall bounce");
    }
    delete &s;
}

// The frame's update: each state, observing / waiting or not, a game that extrapolates or not, the history full or
// not, on a rail or not, out of a lip, driving in and out, the looping sound on and off.
void TestSkaterNonLocalNetLogicUpdate()
{
    StandIns& s = *new StandIns();
    StandIn(s, true);
    GameMemory globals({{0x007CCE14, 4}, {0x007CE470, 4}, {0x006F3614, 4}, {0x006F361C, 4}});
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            GameVariable<uint32_t>(0x007CCE14) = 0xAE7000 + seed;
            GameVariable<uint32_t>(0x007CE470) = 0x6A3E000 + seed;
            GameVariable<uint32_t>(0x006F3614) = 0;
            GameVariable<uint32_t>(0x006F361C) = 0x9001000;
            Originals guard(original, {0x00517270});
            std::vector<uint32_t> trace;
            void* c = At(kComponent);
            Lcg random(seed * 104729);
            for (int k = 0; k < 64; ++k) {
                g_a.frameSeconds = 1.0f / 60.0f * static_cast<float>(1 + random.Below(3));
                g_a.observing = random.Below(5) == 0;
                g_a.waiting = random.Below(5) == 0;
                g_a.extrapolates = random.Below(3) == 0;
                const uint32_t modes[] = {0xBFF33600, 0x3D6D444F, 0x12345678, 0x1C471C60};
                g_a.gameMode = modes[random.Below(4)];
                F<int>(kHistory + 0x18) = static_cast<int>(random.Below(24));
                F<uint32_t>(kHistory + 0x1104) = random.Below(2) ? 0u : 0xC0470000 + static_cast<uint32_t>(k);
                F<int>(kState + 0x18) = static_cast<int>(random.Below(6));
                if (k % 4 == 1)
                    F<int>(kState + 0x18) = 3;
                F<uint8_t>(kState + 0x118) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kComponent + 0x2A0) = static_cast<uint8_t>(random.Below(2));
                F<int>(kState + 0x1C) = random.Below(4) == 0 ? 1 : 0;
                F<uint8_t>(kSound + 0x10C) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kSound + 0xFD) = static_cast<uint8_t>(random.Below(2));
                F<int>(kSound + 0x74) = static_cast<int>(random.Below(6));
                F<uint8_t>(kSound + 0x60) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kState + 0xE1) = static_cast<uint8_t>(random.Below(3));
                F<uint8_t>(kSound + 0x100) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kState + 0x58) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kState + 0x70) = static_cast<uint8_t>(random.Below(2));
                F<uint8_t>(kState + 0xF4) = static_cast<uint8_t>(random.Below(2));
                for (int i = 0; i < 3; ++i)
                    F<float>(kObject + 0xAC + 4 * i) =
                        random.Below(3) == 0 ? static_cast<float>(random.Below(15)) / 8.0f - 0.875f
                                             : static_cast<float>(random.Below(800)) - 400.0f;
                // (the playback may change the state: out of a lip into the air, or anything)
                g_a.afterState = random.Below(2) ? -1 : static_cast<int>(random.Below(6));
                if (k % 8 == 5)
                    g_a.afterState = 1;
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x00517270))(c);
                trace.push_back(F<uint32_t>(kComponent + 0x29C));
                trace.push_back(F<uint8_t>(kComponent + 0x2A0));
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "skater non-local net logic: the frame's update");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestSkaterNonLocalNetLogic, RemoveStandIns);
SELFTEST(Early, TestSkaterNonLocalNetLogicUpdate, RemoveStandIns);
