// Self-tests for the walk component's hanging, ladder and rail-climbing code (obj/walk_hang.cpp): original vs ours on
// one stand-in walk component (an arena holding it and everything it points at: rail nodes, a rail manager, the
// skater core, the movable contact...), every callee into game code - and our rewrite where ours calls that directly
// - stood in for by recording code (skater_core_testkit.h); the walk parameters a hand-built structure.

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/obj/walk_hang.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/sys/game_clock.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkaterCoreKit;
using Mth::Vector;
using Walk::Component;
using CFuncTests::Params;

// --- the arena ---
constexpr uint32_t kWalk = 0x0000;      // the walk component (0x1960 bytes)
constexpr uint32_t kWalkObject = 0x1A00;  // its object (+0x0C)
constexpr uint32_t kNodes = 0x1C00;     // 6 rail nodes (0x54 each)
constexpr uint32_t kRails = 0x1E00;     // the rail manager (+0x176C)
constexpr uint32_t kPhysics = 0x1E80;   // the skater core (+0x178C; +0x22C / +0x230 / +0x1528 used)
constexpr uint32_t kModel = 0x2200;     // +0x1778 (+0x208: kModelNode)
constexpr uint32_t kModelNode = 0x2480; // (+0x20 a fraction, +0x44 flags)
constexpr uint32_t kTriggerComp = 0x2500; // +0x177C
constexpr uint32_t kContact = 0x2600;   // +0x1784 (+0x18: a ride tracker)
constexpr uint32_t kTracker = 0x2680;   // (0xB0)
constexpr uint32_t kTurnTarget = 0x2780; // +0x1760
constexpr uint32_t kWalkInput = 0x2800; // +0x1770 (+0x248)
constexpr uint32_t kTagging = 0x2B00;   // the tagging component (GetComponent's answer)
constexpr uint32_t kRideObject = 0x2B80; // an object a rail component rides with
constexpr uint32_t kTestFeeler = 0x2C00; // a feeler for SideClear (0xB0)
constexpr uint32_t kFlagsBlock = 0x3400; // the skater core's timed flags (+0x1528 there)
constexpr uint32_t kRailComps = 0x2D00;  // two rail manager components (0x40 each: vtable, +0x0C object, +0x14 next)
constexpr uint32_t kSpare = 0x3600;      // allocations (to 0x3F00)

uint8_t* g_savedSkate;
alignas(16) uint8_t g_skate[0x40]; // the skate module (+0x24: the level's rail manager)

// The walk parameters (walk_parameters): every name these functions read, each a different value.
Params* g_walkParams;
void BuildWalkParams()
{
    static Params params;
    new (&params) Params;
    const uint32_t names[] = {0x7213EF07, 0xCC461B87, 0xA8C13E74, 0xDE86132E, 0x7201AD48, 0xC3263E6E,
                              0xAFBF7687, 0x8F35F29F, 0xA20C43B7, 0x6DA7F696, 0x9B2818CF, 0x21DFBE77,
                              0x01D1E6AF, 0xFDC8AEC0, 0x3344A6D0, 0x84FF931C, 0xCBEB3D89, 0xC774DD6D};
    const float values[] = {9.5f, 6.25f, 18.0f, 40.5f, 1.125f, 7.75f, 13.0f, 22.5f,
                            30.0f, 4.5f, 36.0f, 2.75f, 0.875f, 0.3f, 1.25f, 3.5f, 2.25f, 15.5f};
    // (a Params holds 16 components: the rest in a second one, chained on)
    static Params more;
    new (&more) Params;
    for (int i = 0; i < 18; ++i)
        (i < 16 ? params : more).Float(names[i], values[i]);
    params.components[15].next = &more.components[0];
    g_walkParams = &params;
}

uint32_t g_walkMode; // what some stand-ins answer (per variant)
uint32_t g_hitPercent = 50; // how often a feeler hits

std::vector<StandIn> WalkStandIns()
{
    using Obj::CRailManager;
    return {
        {0x00479020, 0, 1, Ret::Int, false, 0, {Ours(static_cast<Script::CStruct* (*)(uint32_t)>(&Script::GetStructure))},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(g_walkParams->Struct()); }},
        {0x005260E0, 4, 1, Ret::None, true},
        {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}},
        {0x005092E0, 4, 1, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::TriggerRailScript)}},
        {0x0052AB90, 8, 2, Ret::None, true},
        {0x00527720, 8, 2, Ret::None, true},
        {0x00434BF0, 8, 2, Ret::None, true},
        {0x00427CD0, 4, 1, Ret::None, true},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kTagging)); }},
        {0x0057CB60, 8, 2, Ret::Int, true, 0, {Ours(&CRailManager::GetWorldPos)}, [](Frame& f, Result& r) {
             std::memcpy(reinterpret_cast<void*>(f.args[0]), reinterpret_cast<void*>(f.args[1] + 0x20), 16);
             r.eax = f.args[0];
         }},
        {0x0057C600, 0, 8, Ret::Bool, false, 0, {Ours(&Obj::RailClear)}},
        {0x0057C5A0, 8, 2, Ret::Bool, false, 0, {Ours(&Obj::NodesTogether)}},
        {0x0057E170, 12, 3, Ret::Bool, true, 0, {Ours(&CRailManager::FindJoiningRail)}, [](Frame& f, Result& r) {
             // a node with a next (as the rail manager finds them)
             uint32_t first = Next() % 6;
             for (uint32_t i = 0; i < 6; ++i) {
                 uint8_t* node = ArenaPtr(kNodes + ((first + i) % 6) * 0x54);
                 if (*reinterpret_cast<uint8_t**>(node + 0x40)) {
                     *reinterpret_cast<uint8_t**>(f.args[2]) = node;
                     return;
                 }
             }
             r.eax = 0;
         }},
        {0x005250A0, 0, 0, Ret::None, true},
        {0x00401C30, 0, 0, Ret::None, false, 0, {Ours(&Dbg::Printf)}, [](Frame& f, Result&) {
             // (the strings by content: the original's and ours are at different addresses)
             for (int i = 0; i < 4; ++i)
                 Record(i == 2 ? f.args[i] : TestStandIns::Hash(reinterpret_cast<const char*>(f.args[i])));
         }},
        {0x004172F0, 8, 2, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::GetCollision)}, [](Frame& f, Result& r) {
             const float* line = reinterpret_cast<const float*>(f.ecx);
             for (int i = 0; i < 8; ++i)
                 RecordFloat(line[i]);
             r.eax = Next() % 100 < g_hitPercent;
         }},
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kSpare)) : 0u; }},
        {0x00416E70, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CRideTracker::Construct)},
         [](Frame& f, Result& r) { r.eax = f.ecx; }},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(&Mem::EnsureManager)}},
        {0x00401CC0, 0, 1, Ret::Int, false, 0, {Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))},
         [](Frame&, Result& r) {
             r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kSpare)) : 0u;
             if (r.eax)
                 std::memset(ArenaPtr(kSpare), 0, 0x28);
         }},
        {0x00528280, 12, 1, Ret::Bool, true, 0, {}, [](Frame& f, Result&) {
             for (int i = 0; i < 4; ++i)
                 RecordFloat(reinterpret_cast<float*>(f.args[0])[i]);
             for (int i = 0; i < 4; ++i)
                 reinterpret_cast<float*>(f.args[1])[i] = RandomFloat(3.0f);
             float* feeler = reinterpret_cast<float*>(f.args[2]);
             for (int i = 0; i < 4; ++i)
                 feeler[0x88 / 4 + i] = RandomFloat(1.0f);
         }},
        {0x0043A930, 4, 0, Ret::Int, true, 0, {}, [](Frame& f, Result& r) {
             // (the normal the stand-in set; the rest of a stack feeler is unset)
             std::memcpy(reinterpret_cast<uint8_t*>(f.ecx) + 0x88, reinterpret_cast<uint8_t*>(f.args[0]) + 0x88, 0x10);
             r.eax = f.ecx;
         }},
        {0x00427C70, 0, 0, Ret::None, true},
        {0x0045F2D0, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObjectManager::GetFirstComponentByType)},
         [](Frame&, Result& r) { r.eax = g_walkMode % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kRailComps)) : 0u; }},
        {0x0042BA10, 4, 1, Ret::None, true, 0, {}, [](Frame& f, Result&) {
             float* m = reinterpret_cast<float*>(f.args[0]);
             for (int i = 0; i < 16; ++i)
                 m[i] = (i % 5 == 0) ? 1.0f + RandomFloat(0.25f) : RandomFloat(0.5f);
         }},
        {0x0057DDA0, 0x18, 6, Ret::Bool, true, 0, {Ours(&CRailManager::FindRail)}, [](Frame& f, Result& r) {
             for (int i = 0; i < 4; ++i) {
                 RecordFloat(reinterpret_cast<float*>(f.args[0])[i]);
                 RecordFloat(reinterpret_cast<float*>(f.args[1])[i]);
                 RecordFloat(reinterpret_cast<float*>(f.args[2])[i]);
             }
             float* record = reinterpret_cast<float*>(f.args[4]);
             for (int i = 0; i < 12; ++i)
                 record[i] = RandomFloat(2.0f);
             reinterpret_cast<uint8_t**>(record)[12] = ArenaPtr(kNodes + 0x54 * (Next() % 3));
             if (g_walkMode % 3 == 0)
                 r.eax = 0;
         }},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}},
    };
}

// The rail manager components' vtable slot 0x34 (their rail manager).
struct FakeRailComponent {
    Obj::CRailManager* Rails() { return reinterpret_cast<Obj::CRailManager*>(ArenaPtr(kRails)); }
};
void* g_railComponentVtable[16];

// The walk component's arena: random floats, then its pointers and the nodes' links.
void FillWalkArena()
{
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        ArenaAt<float>(i) = RandomFloat(3.0f);
    auto pointer = [](uint32_t at, uint32_t to) { ArenaAt<uint8_t*>(at) = to == ~0u ? nullptr : ArenaPtr(to); };
    pointer(kWalk + 0x0C, kWalkObject);
    pointer(kWalk + 0x1760, kTurnTarget);
    pointer(kWalk + 0x1764, kNodes + 0x54);
    pointer(kWalk + 0x1768, kNodes + 0xA8);
    pointer(kWalk + 0x176C, kRails);
    pointer(kWalk + 0x1770, kWalkInput);
    pointer(kWalk + 0x1778, kModel);
    pointer(kWalk + 0x177C, kTriggerComp);
    pointer(kWalk + 0x1784, kContact);
    pointer(kWalk + 0x178C, kPhysics);
    pointer(kModel + 0x208, kModelNode);
    pointer(kPhysics + 0x1528, kFlagsBlock);
    *reinterpret_cast<uint8_t**>(g_skate + 0x24) = ArenaPtr(kRails);
    ArenaAt<uint8_t>(kModelNode + 0x44) = static_cast<uint8_t>(Next());
    // the pull-up feeler: no object, no cache
    ArenaAt<uint32_t>(kWalk + 0x17B4 + 0x98) = 0;
    ArenaAt<uint32_t>(kWalk + 0x17B4 + 0xAC) = 0;
    ArenaAt<uint32_t>(kWalk + 0x1864) = Next() % 2;
    ArenaAt<uint32_t>(kTestFeeler + 0x98) = 0;
    ArenaAt<uint32_t>(kTestFeeler + 0xAC) = 0;
    // rail nodes: a chain 0-1-2-3, 4 and 5 alone; flags varied
    for (int i = 0; i < 6; ++i) {
        uint32_t node = kNodes + i * 0x54;
        pointer(node + 0x40, i < 3 && Next() % 5 ? node + 0x54 : (Next() % 2 ? kNodes + 0x54 * 4 : ~0u));
        pointer(node + 0x44, i > 0 && i < 4 && Next() % 5 ? node - 0x54 : (Next() % 2 ? kNodes + 0x54 * 5 : ~0u));
        ArenaAt<uint32_t>(node + 0x48) = Next() & 0x3F;
    }
    // the ride tracker (or none)
    ArenaAt<uint32_t>(kContact + 0x18) = Next() % 3 ? reinterpret_cast<uint32_t>(ArenaPtr(kTracker)) : 0u;
    ArenaAt<uint32_t>(kTracker) = Next() % 2 ? reinterpret_cast<uint32_t>(ArenaPtr(kRideObject)) : 0u;
    ArenaAt<uint8_t>(kTagging + 0x1C) = static_cast<uint8_t>(Next() % 4 == 0);
    // two rail manager components
    for (int i = 0; i < 2; ++i) {
        uint32_t component = kRailComps + i * 0x40;
        ArenaAt<void**>(component) = g_railComponentVtable;
        pointer(component + 0x0C, kRideObject);
        pointer(component + 0x14, i == 0 ? kRailComps + 0x40 : ~0u);
    }
    ArenaAt<uint8_t>(kFlagsBlock + 0x70) = static_cast<uint8_t>(Next() % 4 == 0);
    ArenaAt<uint8_t>(kTagging + 0x1D) = 7;
}

// Run `ours` / the original on a filled arena for `variants` seeds; Check each.
template <typename Call>
void Compare(const char* what, int variants, Call call)
{
    int failures = 0;
    for (int variant = 0; variant < variants && failures < 3; ++variant) {
        g_rng = 0x5EED0000u + variant * 7919u;
        g_walkMode = variant;
        FillWalkArena();
        SaveStart();
        uint32_t seed = Next();
        std::vector<uint32_t> expected = RunOnArena(seed, [&](uint8_t* walk) { return call(walk, true); });
        std::vector<uint32_t> actual = RunOnArena(seed, [&](uint8_t* walk) { return call(walk, false); });
        if (!SkaterCoreKit::Compare(expected, actual, what, variant))
            ++failures;
    }
    Check(failures == 0, what);
}

uint32_t DoubleBits(double value, bool high)
{
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    return static_cast<uint32_t>(high ? bits >> 32 : bits);
}

StandIns* g_walkStandIns;
UseOurs* g_walkMath;
uint8_t g_savedManagerReady;

// (run even if a test crashes)
void RemoveWalkStandIns()
{
    delete g_walkMath;
    delete g_walkStandIns;
    g_walkMath = nullptr;
    g_walkStandIns = nullptr;
    if (g_savedSkate)
        GameVariable<uint8_t*>(0x007CE478) = g_savedSkate;
}

void TestWalkHang()
{
    g_savedManagerReady = GameVariable<uint8_t>(0x007CC979);
    GameVariable<uint8_t>(0x007CC979) = 1;
    g_savedSkate = GameVariable<uint8_t*>(0x007CE478);
    GameVariable<uint8_t*>(0x007CE478) = g_skate;
    BuildWalkParams();
    g_walkStandIns = new StandIns(WalkStandIns());
    g_walkMath = new UseOurs({{0x004023F0, Ours(&Vector::Normalize)},
                              {0x00405600, Ours(&Mth::Matrix::Invert)},
                              {0x0040AF60, Ours(&Mth::Matrix::Transform)}});
    g_railComponentVtable[0x34 / 4] = reinterpret_cast<void*>(Ours(&FakeRailComponent::Rails));

    // the small ones
    Compare("walk: Param, RunSpeed, HangHeightOffset, CriticalPointUp", 12, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        float p = original ? Original<float(__cdecl*)(uint32_t)>(0x0043DA70)(0xCC461B87) : Component::Param(0xCC461B87);
        RecordFloat(p);
        double values[3];
        if (original) {
            values[0] = Original<double(__thiscall*)(Component*)>(0x0043DAA0)(c);
            values[1] = Original<double(__thiscall*)(Component*)>(0x0043DC00)(c);
            values[2] = Original<double(__thiscall*)(Component*)>(0x0043DC60)(c);
        } else {
            values[0] = c->RunSpeed();
            values[1] = c->HangHeightOffset();
            values[2] = c->CriticalPointUp();
        }
        for (double v : values) {
            Record(DoubleBits(v, false));
            Record(DoubleBits(v, true));
        }
        return 0u;
    });
    Compare("walk: Lerp, SkaterTurnedRound", 30, [](uint8_t* walk, bool original) {
        Vector out;
        auto* a = reinterpret_cast<const Vector*>(walk + 0x100);
        auto* b = reinterpret_cast<const Vector*>(walk + 0x110);
        float t = ArenaAt<float>(0x120) * 0.5f;
        if (original)
            Original<Vector*(__cdecl*)(Vector*, const Vector*, const Vector*, float)>(0x0043D990)(&out, a, b, t);
        else
            Walk::Lerp(&out, a, b, t);
        for (int i = 0; i < 4; ++i)
            RecordFloat((&out.x)[i]);
        // the skater core's +0x228 near a half turn, +0x22C 0 or not, +0x234 0 or not
        uint8_t* skater = walk + 0x200;
        ArenaAt<float>(0x200 + 0x228) = 3.1415927f - 0.785f + RandomFloat(0.05f);
        ArenaAt<uint32_t>(0x200 + 0x234) = g_walkMode % 3 == 0;
        if (g_walkMode % 5 == 0)
            ArenaAt<float>(0x200 + 0x22C) = 0.0f;
        if (g_walkMode % 7 == 0)
            ArenaAt<float>(0x200 + 0x228) = -ArenaAt<float>(0x200 + 0x228);
        int turned = original ? Original<int(__thiscall*)(const void*)>(0x0043DA20)(skater)
                              : reinterpret_cast<const Walk::SkaterTurn*>(skater)->TurnedRound();
        return static_cast<uint32_t>(turned);
    });
    Compare("walk: the animation-end callbacks", 12, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        if (original) {
            Original<void(__thiscall*)(Component*)>(0x0043DB00)(c);
            RecordFloat(ArenaAt<float>(kWalk + 0x728)); // (the hang offset, before the drop to hang sets it)
            Original<void(__thiscall*)(Component*)>(0x0043DB90)(c);
            Original<void(__thiscall*)(Component*)>(0x0043DCF0)(c);
        } else {
            c->EndPullUp();
            RecordFloat(ArenaAt<float>(kWalk + 0x728));
            c->EndDropFromHang();
            c->EndDropToHang();
        }
        return 0u;
    });
    Compare("walk: SideClear", 30, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(ArenaPtr(kTestFeeler));
        auto* hang = reinterpret_cast<const Vector*>(walk + 0x300);
        auto* side = reinterpret_cast<const Vector*>(walk + 0x320);
        bool clear = original ? Original<bool(__thiscall*)(Component*, Nx::CFeeler*, const Vector*, const Vector*)>(
                                    0x0043DDD0)(c, feeler, hang, side)
                              : c->SideClear(feeler, hang, side);
        return static_cast<uint32_t>(clear);
    });
    Compare("walk: FaceHangRail", 240, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        if (g_walkMode % 3 == 0) {
            // a short rail (less than 1 long)
            for (int i = 0; i < 6; ++i)
                for (int k = 0; k < 3; ++k)
                    ArenaAt<float>(kNodes + i * 0x54 + 0x20 + k * 4) *= 0.125f;
        }
        float distance = g_walkMode % 4 == 0 ? 100.0f : ArenaAt<float>(0x340) + 1.0f;
        bool fromEnd = g_walkMode % 2 != 0;
        auto* direction = reinterpret_cast<const Vector*>(walk + 0x350);
        if (original)
            Original<void(__thiscall*)(Component*, float, uint32_t, const Vector*)>(0x0043DFC0)(c, distance, fromEnd,
                                                                                               direction);
        else
            c->FaceHangRail(distance, fromEnd, direction);
        return 0u;
    });
    Compare("walk: NextHangRail", 120, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        static const float ts[] = {0.5f, -0.25f, 0.0f, -0.0f, 0.25f, 1.5f};
        float t = ts[g_walkMode % 6];
        auto* start = reinterpret_cast<Obj::CRailNode*>(ArenaPtr(kNodes + 0x54 * (1 + (g_walkMode / 6) % 2)));
        auto* end = start->m_next ? start->m_next : reinterpret_cast<Obj::CRailNode*>(ArenaPtr(kNodes + 0xFC));
        Obj::CRailNode* newStart = nullptr;
        Obj::CRailNode* newEnd = nullptr;
        bool found = original ? Original<bool(__thiscall*)(Component*, float*, Obj::CRailNode*, Obj::CRailNode*,
                                                           Obj::CRailNode**, Obj::CRailNode**)>(0x0043E230)(
                                    c, &t, start, end, &newStart, &newEnd)
                              : c->NextHangRail(&t, start, end, &newStart, &newEnd);
        RecordFloat(t);
        Record(reinterpret_cast<uint32_t>(newStart));
        Record(reinterpret_cast<uint32_t>(newEnd));
        return static_cast<uint32_t>(found);
    });
    Compare("walk: StartRiding, CreateWaitAndDieComponent", 20, [](uint8_t*, bool original) {
        auto* contact = reinterpret_cast<Walk::MovableContact*>(ArenaPtr(kContact));
        auto* object = reinterpret_cast<Obj::CCompositeObject*>(ArenaPtr(kRideObject));
        if (original)
            Original<void(__thiscall*)(Walk::MovableContact*, Obj::CCompositeObject*)>(0x0043EB80)(contact, object);
        else
            contact->StartRiding(object);
        Obj::CBaseComponent* component = original ? Original<Obj::CBaseComponent*(__cdecl*)()>(0x0043D930)()
                                                  : Obj::CreateWaitAndDieComponent();
        return reinterpret_cast<uint32_t>(component);
    });

    g_hitPercent = 4;
    Compare("walk: PullUp", 120, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        auto* target = reinterpret_cast<Vector*>(walk + 0x400);
        bool started = original ? Original<bool(__thiscall*)(Component*, Vector*)>(0x0043E530)(c, target)
                                : c->PullUp(target);
        return static_cast<uint32_t>(started);
    });
    Compare("walk: DropToHang", 60, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        if (g_walkMode % 2)
            for (int i = 0; i < 3; ++i)
                ArenaAt<float>(kWalk + 0x34 + i * 4) *= 0.25f;
        bool started = original ? Original<bool(__thiscall*)(Component*)>(0x0043EC40)(c) : c->DropToHang();
        return static_cast<uint32_t>(started);
    });
    Compare("walk: GrabRail", 40, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        ArenaAt<uint8_t>(kFlagsBlock + 0xA0) = g_walkMode % 2 ? 1 : 0;
        Vector start = *reinterpret_cast<Vector*>(walk + 0x420);
        Vector end = *reinterpret_cast<Vector*>(walk + 0x430);
        ArenaAt<float>(kWalk + 0x44) = g_walkMode % 5 == 0 ? 50.5f : g_walkMode % 7 == 0 ? 50.0f : 10.0f;
        bool grabbed = original ? Original<bool(__thiscall*)(Component*, Vector, Vector)>(0x0043F030)(c, start, end)
                                : c->GrabRail(start, end);
        return static_cast<uint32_t>(grabbed);
    });
    g_hitPercent = 10;
    Compare("walk: ShimmyBlocked", 360, [](uint8_t* walk, bool original) {
        auto* c = reinterpret_cast<Component*>(walk);
        auto* out = reinterpret_cast<const Vector*>(walk + 0x440);
        auto* point = reinterpret_cast<const Vector*>(walk + 0x450);
        auto* move = reinterpret_cast<const Vector*>(walk + 0x460);
        Vector segment = *reinterpret_cast<Vector*>(walk + 0x470);
        if (g_walkMode % 3 == 0)
            segment = {segment.x * 0.25f, segment.y * 0.25f, segment.z * 0.25f, segment.w};
        static const float alongs[] = {0.5f, 0.05f, 0.95f, 0.0f, 1.0f, 0.3f};
        float along = alongs[g_walkMode % 6];
        bool left = (g_walkMode / 6) % 2 != 0;
        auto* start = reinterpret_cast<Obj::CRailNode*>(ArenaPtr(kNodes + 0x54));
        auto* end = reinterpret_cast<Obj::CRailNode*>(ArenaPtr(kNodes + 0xA8));
        bool blocked = original ? Original<bool(__thiscall*)(Component*, const Vector*, float, const Vector*, uint32_t,
                                                             const Vector*, Vector, Obj::CRailNode*, Obj::CRailNode*)>(
                                      0x0043F3A0)(c, out, along, point, left, move, segment, start, end)
                                : c->ShimmyBlocked(out, along, point, left, move, segment, start, end);
        return static_cast<uint32_t>(blocked);
    });

    g_hitPercent = 50;
    RemoveWalkStandIns();
    GameVariable<uint8_t>(0x007CC979) = g_savedManagerReady;
    GameVariable<uint8_t*>(0x007CE478) = g_savedSkate;
}

} // namespace

SELFTEST(Early, TestWalkHang, RemoveWalkStandIns);
