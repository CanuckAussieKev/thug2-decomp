// Self-tests for the particle, proximity and chain components (particle_component.cpp, proximity_component.cpp,
// chain_component.cpp): original vs ours on stand-in components in skater_core_testkit.h's arena, every callee stood
// in for - but the feeler's constructor, the capsule maths and the matrix and vector maths, which run as they are
// (the original's and ours).

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/net/buddy_status.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/feeler.h"
#include "game/obj/chain_component.h"
#include "game/obj/particle_component.h"
#include "game/obj/proximity_component.h"
#include "game/obj/skeleton_component.h"
#include "game/obj/smtptr.h"
#include "game/obj/suspend_component.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/motion_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace MotionKit;
using Obj::CChain;
using Obj::CChainComponent;
using Obj::CChainLink;
using Obj::CParticleComponent;
using Obj::CProximityComponent;

// Where things are (the component at kMotion, its object at kObj; the rest as motion_testkit.h's).
constexpr uint32_t kComp = kMotion;
constexpr uint32_t kOther = kOtherObj;          // another object (the chain's end, the skater)
constexpr uint32_t kParticle = kMine;           // the particle system (0x200)
constexpr uint32_t kParticleVtable = kMine + 0x200;
constexpr uint32_t kParticleParams = kMine + 0x300; // its parameters (0x200)
constexpr uint32_t kObjVtable = kMine + 0x500;
constexpr uint32_t kCompVtable = kMine + 0x540;
constexpr uint32_t kArray = kMine + 0x580;      // a script array
constexpr uint32_t kChainA = kMine + 0x600;     // a chain (0x684)
constexpr uint32_t kNodes = kMine + 0xD00;      // list nodes (0x10 apart)
constexpr uint32_t kLinesA = kMine + 0xE00;     // the first chain's lines (0x20 apart)
constexpr uint32_t kSkeleton = kMine + 0x1100;  // the skeleton component's data
constexpr uint32_t kChainB = kAlloc;            // a second chain (0x684)
constexpr uint32_t kLinesB = kAlloc + 0x700;    // its lines

constexpr uint32_t kLineVtable = 0x0064A628; // the Nx line's (slots 0, 7 and 15 stood in)

CParticleComponent* Particle() { return reinterpret_cast<CParticleComponent*>(ArenaPtr(kComp)); }
CProximityComponent* Proximity() { return reinterpret_cast<CProximityComponent*>(ArenaPtr(kComp)); }
CChainComponent* Chains() { return reinterpret_cast<CChainComponent*>(ArenaPtr(kComp)); }

float Pick(std::initializer_list<float> values) { return values.begin()[Next() % values.size()]; }

StandIn CallocCall() { return Calloc(Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))); }
StandIn ManagerFree() { return {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}}; }
StandIn BaseConstruct()
{
    return {0x0045D730, 0, 0, Ret::Int, true, 0, {Ours(&Obj::CBaseComponent::Construct)},
            [](Frame& f, Result& r) { r.eax = f.ecx; }};
}
StandIn BaseDestroyBody() { return {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}}; }
StandIn GameClock() { return {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}}; }
StandIn FrameSeconds()
{
    return {0x005BEAA0, 0, 0, Ret::Float, false, 0, {Ours(&Tmr::GetSlomoFrameSeconds)},
            [](Frame&, Result& r) { r.st0 = Pick({1.0f / 60, 0.0f, 0.05f, 0.2f}); }};
}
StandIn EnsureManager() { return {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(&Mem::EnsureManager)}}; }
// Mem::Manager::Allocate: blocks from kAlloc + 0x800 on.
uint32_t g_blockNext;
StandIn Allocate()
{
    return {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)}, [](Frame& f, Result& r) {
                r.eax = reinterpret_cast<uint32_t>(ArenaPtr(g_blockNext));
                g_blockNext += (f.args[0] + 15) & ~15u;
            }};
}

// The unnamed parameters' getters (the original's assert-flag versions; ours the plain ones).
template <typename Fn>
StandIn Getter(uint32_t address, Fn ours, Type type, uint32_t size)
{
    StandIn s = {address, 12, 3, Ret::Bool, true, 0, {Ours(ours)}};
    s.action = [type, size](Frame& f, Result& r) {
        const Param* p = IsParams(f) ? Find(f.args[0], type) : nullptr;
        r.eax = p ? 1 : 0;
        if (p) {
            std::memcpy(reinterpret_cast<void*>(f.args[1]), p->value, size);
            if (type == Type::Vector)
                reinterpret_cast<float*>(f.args[1])[3] = 1.0f;
        }
    };
    return s;
}
StandIn ArrayGetter(uint32_t address)
{
    using Fn = bool (Script::CStruct::*)(uint32_t, Script::CArray**, int);
    return {address, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::GetArray))},
            [](Frame& f, Result& r) {
                r.eax = 1;
                *reinterpret_cast<void**>(f.args[1]) = ArenaPtr(kArray);
            }};
}
StandIn GetElement() { return {0x0046CC30, 4, 1, Ret::Int, true, 0, {Ours(&Script::CArray::GetElement)}}; }

// A stand-in that records what its pointer argument `arg` points at (`words` dwords).
StandIn Pointing(uint32_t address, uint8_t pop, uint8_t args, int arg, int words, Ret ret = Ret::None,
                 std::vector<uint32_t> ours = {})
{
    StandIn s = {address, pop, args, ret, true, 0, ours};
    s.action = [arg, words](Frame& f, Result&) {
        for (int i = 0; i < words; ++i)
            Record(reinterpret_cast<const uint32_t*>(f.args[arg])[i]);
    };
    return s;
}

template <typename Call>
std::vector<uint32_t> Run(uint32_t seed, Call call)
{
    g_allocNext = kAlloc;
    g_blockNext = kAlloc + 0x800;
    return RunOnArena(seed, call);
}
template <typename Setup, typename Original, typename Ours>
bool CompareRuns(const char* what, int variants, Setup setup, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        g_rng = 0x3C6EF372u * static_cast<uint32_t>(variant + 13);
        g_params.clear();
        g_components.clear();
        g_allocFails = false;
        FillMotionArena();
        setup(variant);
        SaveStart();
        uint32_t seed = Next();
        auto expected = Run(seed, [&](uint8_t*) { return original(); });
        auto actual = Run(seed, [&](uint8_t*) { return ours(); });
        if (!Compare(expected, actual, what, variant))
            return false;
    }
    return true;
}
// A script member function: original vs ours.
template <typename Setup>
bool CompareMember(const char* what, uint32_t address, bool (CParticleComponent::*ours)(Script::CStruct*, Script::CScript*),
                   int variants, Setup setup)
{
    return CompareRuns(
        what, variants, setup,
        [address] {
            return static_cast<uint32_t>(
                Original<bool(__thiscall*)(void*, void*, void*)>(address)(Particle(), Params(), TheScript()));
        },
        [ours] { return static_cast<uint32_t>((Particle()->*ours)(Params(), TheScript())); });
}

// --- the particle component ---
void RandomParticle()
{
    ArenaAt<uint8_t*>(kComp + 0x1C) = Next() % 4 ? ArenaPtr(kParticle) : nullptr;
    ArenaAt<uint8_t*>(kComp + 0x20) = Next() % 2 ? ArenaPtr(kMine + 0x1C00) : nullptr; // the suspend component
    ArenaAt<uint8_t*>(kComp + 0x24) = Next() % 2 ? ArenaPtr(kOther) : nullptr;
    ArenaAt<int>(kComp + 0x28) = static_cast<int>(Next() % 5000);
    ArenaAt<uint32_t>(kComp + 0x2C) = Next() % 3 ? 0 : Next() % 4000;
    ArenaAt<uint8_t*>(kParticle) = ArenaPtr(kParticleVtable);
    ArenaAt<uint32_t>(kParticleVtable + 7 * 4) = 0x00496510; // (slot 7: a stood in function)
    ArenaAt<uint8_t*>(kObj) = ArenaPtr(kObjVtable);
    ArenaAt<uint32_t>(kObjVtable + 6 * 4) = 0x0045D7C0;      // (slot 6: MarkAsDead)
    ArenaAt<uint8_t>(kParticleParams + 0x152) = Next() % 3 != 0;
    ArenaAt<uint8_t>(kParticleParams + 0x154) = Next() % 2;
    for (uint32_t i = 0; i < 3; ++i)
        RichVector(kParticleParams + 0xF4 + i * 0x10, 50.0f);
    RichVector(kParticleParams + 0x94, 500.0f);
    RichVector(kObj + 0x4C, 300.0f);
    ArenaAt<uint32_t>(kArray + 4) = 4;
}

void TestParticleLife()
{
    StandIns standIns({CallocCall(), BaseConstruct(), BaseDestroyBody(), ManagerFree(), GameClock(),
                       {0x00496590, 4, 1, Ret::None, true, 0}, GetComponent(),
                       {0x00494E70, 4, 1, Ret::None, true, 1}});
    bool ok = CompareRuns(
        "Particle Create", 40,
        [](int v) {
            g_allocFails = v % 4 == 3;
            GameVariable<int>(0x00696D60) = v % 2 ? 0 : static_cast<int>(Next() % 100000); // (the clock's offset)
        },
        [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x0042A000)()); },
        [] { return reinterpret_cast<uint32_t>(CParticleComponent::Create()); });
    ok = ok && CompareRuns(
                   "Particle Destroy", 40, [](int) { RandomParticle(); },
                   [] {
                       uint32_t flags = g_rng & 3;
                       return reinterpret_cast<uint32_t>(
                           Original<void*(__thiscall*)(void*, unsigned int)>(0x0042A060)(Particle(), flags));
                   },
                   [] {
                       uint32_t flags = g_rng & 3;
                       return reinterpret_cast<uint32_t>(Particle()->Destroy(flags));
                   });
    ok = ok && CompareRuns(
                   "Particle Finalize", 10,
                   [](int v) {
                       if (v % 2)
                           g_components.push_back({kObj, 0xCE0CA665, kMine + 0x1C00});
                   },
                   [] {
                       Original<void(__thiscall*)(void*)>(0x00429BF0)(Particle());
                       return 0u;
                   },
                   [] {
                       Particle()->Finalize();
                       return 0u;
                   });
    ok = ok && CompareRuns(
                   "Particle Hide", 20, [](int) { RandomParticle(); },
                   [] {
                       bool hide = g_rng & 1;
                       Original<void(__thiscall*)(void*, bool)>(0x00429960)(Particle(), hide);
                       return 0u;
                   },
                   [] {
                       bool hide = g_rng & 1;
                       Particle()->Hide(hide);
                       return 0u;
                   });
    GameVariable<int>(0x00696D60) = 0;
    Check(ok, "particle component: Create, Destroy, Finalize, Hide (original vs ours)");
}

void TestParticleMembers()
{
    using FloatFn = bool (Script::CStruct::*)(uint32_t, float*, int);
    using VectorFn = bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int);
    using ChecksumFn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    StandIns standIns(
        {Getter(0x00476D50, static_cast<FloatFn>(&Script::CStruct::GetFloat), Type::Float, 4),
         Getter(0x00477040, static_cast<VectorFn>(&Script::CStruct::GetVector), Type::Vector, 12),
         Getter(0x00476EC0, static_cast<ChecksumFn>(&Script::CStruct::GetChecksum), Type::Checksum, 4),
         GetInteger(), ArrayGetter(0x00476F10), GetElement(),
         {0x00494E20, 0, 0, Ret::None, true, 0},
         {0x00494E90, 4, 1, Ret::None, true, 0},
         {0x00494EA0, 4, 1, Ret::None, true, 0},
         {0x00494EF0, 4, 1, Ret::None, true, 0},
         {0x004076F0, 4, 1, Ret::None, true, 0, {Ours(&Obj::SetParticleValue34)}},
         {0x00494EB0, 8, 2, Ret::None, true, 0},
         Pointing(0x00496030, 8, 2, 1, 4),
         Pointing(0x00496160, 8, 2, 1, 4),
         Pointing(0x00494EC0, 8, 2, 1, 1),
         Pointing(0x00496190, 4, 1, 0, 1),
         {0x004963B0, 8, 2, Ret::None, true, 0}});
    auto setup = [](int v) {
        RandomParticle();
        // (the vector always: without it both versions pass their own stack's leftovers on)
        GiveVector(0, RichFloat(10.0f), RichFloat(10.0f), RichFloat(10.0f));
        if (v % 5 != 4) {
            Give(0, RichFloat(10.0f));
            GiveChecksum(0, Next());
            GiveInteger(0, static_cast<int>(Next() % 3));
        }
    };
    struct Member {
        const char* name;
        uint32_t address;
        bool (CParticleComponent::*ours)(Script::CStruct*, Script::CScript*);
    };
    const Member members[] = {
        {"SetEmitRate", 0x00429970, &CParticleComponent::SetEmitRate},
        {"SetLifetime", 0x004299A0, &CParticleComponent::SetLifetime},
        {"0x19AF74D1", 0x004299E0, &CParticleComponent::SetFloat19AF74D1},
        {"SetMidPointPct", 0x00429A20, &CParticleComponent::SetMidPointPct},
        {"SetStartRadius", 0x00429A60, &CParticleComponent::SetStartRadius},
        {"SetMidRadius", 0x00429AA0, &CParticleComponent::SetMidRadius},
        {"SetEndRadius", 0x00429AE0, &CParticleComponent::SetEndRadius},
        {"SetStartPos", 0x00429C10, &CParticleComponent::SetStartPos},
        {"SetMidPos", 0x00429C50, &CParticleComponent::SetMidPos},
        {"SetEndPos", 0x00429C90, &CParticleComponent::SetEndPos},
        {"SetStartBox", 0x00429CD0, &CParticleComponent::SetStartBox},
        {"SetMidBox", 0x00429D10, &CParticleComponent::SetMidBox},
        {"SetEndBox", 0x00429D50, &CParticleComponent::SetEndBox},
        {"SetEmitTarget", 0x00429D90, &CParticleComponent::SetEmitTarget},
        {"setStartColor", 0x00429DD0, &CParticleComponent::SetStartColor},
        {"setMidColor", 0x00429E50, &CParticleComponent::SetMidColor},
        {"setEndColor", 0x00429ED0, &CParticleComponent::SetEndColor},
        {"0x5446AF14", 0x00429F50, &CParticleComponent::SetFlag5446AF14},
        {"0x55835CF2", 0x00429F90, &CParticleComponent::SetColor55835CF2},
    };
    bool ok = true;
    for (const Member& m : members)
        ok = ok && CompareMember(m.name, m.address, m.ours, 30, setup);
    Check(ok, "particle component: its 19 script member functions (original vs ours)");
}

void TestParticleUpdate()
{
    StandIns standIns(
        {{0x00433E90, 0, 0, Ret::Bool, true, 0, {Ours(&Obj::CSuspendComponent::IsFar)}},
         {0x0044E4F0, 0, 0, Ret::Bool, true, 0, {Ours(&GameNet::Manager::InNetGame)}},
         // (now and then exactly the end of its lifetime)
         {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)},
          [](Frame&, Result& r) {
              r.eax = Next() % 4 ? Next() % 6000
                                 : ArenaAt<uint32_t>(kComp + 0x28) + ArenaAt<uint32_t>(kComp + 0x2C) -
                                       static_cast<uint32_t>(GameVariable<int>(0x00696D60));
          }},
         {0x0045D7C0, 0, 0, Ret::None, true, 0},
         {0x00494EE0, 4, 1, Ret::None, true, 0},
         {0x004AAD40, 0, 0, Ret::Int, true, 0, {Ours(&Obj::ParticleSystemParams)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kParticleParams)); }},
         Pointing(0x00496030, 8, 2, 1, 4),
         Pointing(0x00496060, 8, 2, 1, 16),
         {0x00496510, 0, 0, Ret::Bool, true, 0},
         {0x004172F0, 8, 2, Ret::Bool, true, 3, {Ours(&Nx::CFeeler::GetCollision)},
          [](Frame& f, Result&) {
              for (int i = 0; i < 8; ++i)
                  Record(reinterpret_cast<const uint32_t*>(f.ecx)[i]);
          }},
         {0x004174A0, 0, 0, Ret::Float, true, 0, {Ours(&Nx::CFeeler::GetHitVertexValue)}}});
    bool ok = CompareRuns(
        "Particle Update", 3000,
        [](int) {
            RandomParticle();
            GameVariable<int>(0x00696D60) = Next() % 2 ? 0 : static_cast<int>(Next() % 3000);
        },
        [] {
            Original<void(__thiscall*)(void*)>(0x0042AD90)(Particle());
            return 0u;
        },
        [] {
            Particle()->Update();
            return 0u;
        });
    ok = ok && CompareRuns(
                   "Particle CallMemberFunction", 400,
                   [](int) {
                       RandomParticle();
                       ArenaAt<uint8_t*>(kComp + 0x1C) = ArenaPtr(kParticle);
                   },
                   [] {
                       uint32_t name = Next() % 3 == 0 ? 0x2299A9F2 : Next() % 2 ? 0x96896315 : Next();
                       return static_cast<uint32_t>(Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(
                           0x0042AEE0)(Particle(), name, Params(), TheScript()));
                   },
                   [] {
                       uint32_t name = Next() % 3 == 0 ? 0x2299A9F2 : Next() % 2 ? 0x96896315 : Next();
                       return static_cast<uint32_t>(Particle()->CallMemberFunction(name, Params(), TheScript()));
                   });
    GameVariable<int>(0x00696D60) = 0;
    Check(ok, "particle component: Update (suspended, lifetimes, the model, its positions and orientations), "
              "CallMemberFunction (original vs ours)");
}

// A stand-in recording the 0x15C-byte parameter block it's given (`arg`), handing back the particle system.
StandIn ParamsGiven(uint32_t address, uint8_t pop, uint8_t args, int arg)
{
    StandIn s = {address, pop, args, Ret::Int, true, 0};
    s.action = [arg](Frame& f, Result& r) {
        for (int i = 0; i < 0x15C / 4; ++i)
            Record(reinterpret_cast<const uint32_t*>(f.args[arg])[i]);
        r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kParticle));
    };
    return s;
}

void TestParticleInit()
{
    using NamedChecksumFn = bool (Script::CStruct::*)(const char*, uint32_t*, int);
    using StructureFn = bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int);
    // (the scale: the original asks 0x00476D50, the bool-flag overload; ours the int one - one stand-in for both)
    StandIn getFloat = GetFloat();
    getFloat.ours.push_back(0x00476D50);
    StandIns standIns(
        {GetChecksum(), GetInteger(), getFloat, GetVector(), ContainsFlag(), ArrayGetter(0x00476860), GetElement(),
         {0x00476A00, 12, 0, Ret::Bool, true, 0, {Ours(static_cast<NamedChecksumFn>(&Script::CStruct::GetChecksum))},
          [](Frame& f, Result& r) {
              Record(Crc::GenerateCRCFromString(reinterpret_cast<const char*>(f.args[0])));
              r.eax = Next() % 2;
              if (r.eax)
                  *reinterpret_cast<uint32_t*>(f.args[1]) = Next();
          }},
         {0x004767C0, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<StructureFn>(&Script::CStruct::GetStructure))},
          [](Frame& f, Result& r) {
              r.eax = Next() % 2;
              if (r.eax)
                  *reinterpret_cast<void**>(f.args[1]) = ArenaPtr(kMine + 0x1E00);
          }},
         // (the parameters' constructor: every word at random)
         {0x00496240, 0, 0, Ret::Int, true, 0, {}, [](Frame& f, Result& r) {
              for (int i = 0; i < 0x15C / 4; ++i)
                  reinterpret_cast<uint32_t*>(f.ecx)[i] = Next() % 3 ? Bits(RichFloat(4.0f)) : Next();
              r.eax = f.ecx;
          }},
         ParamsGiven(0x00496510, 4, 0, 0),
         {0x00460AD0, 0, 1, Ret::Int, false, 0, {Ours(&Obj::FindObject)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kOther)); }},
         GetComponent(),
         {0x00461480, 20, 5, Ret::Int, true, 6, {Ours(&Obj::CObject::SpawnAndRunScript)}},
         {0x00496190, 4, 1, Ret::None, true, 0}});
    bool ok = CompareRuns(
        "Particle InitFromStructure", 3000,
        [](int) {
            RandomParticle();
            ArenaAt<uint32_t>(kArray + 4) = 3;
            static const uint32_t checksums[] = {0x092276DB, 0xA1DC81F9, 0x7321A8D6, 0xDD6BB3D5, 0x7D99F28D,
                                                 0x5E2A8D9F, 0x7E4813A7};
            for (uint32_t name : checksums)
                if (Next() % 3)
                    GiveChecksum(name, Next() % 4 ? Next() : 0);
            static const uint32_t integers[] = {0x5A235299, 0x9E52A20F, 0x4D83DB4C, 0x4ABA1FF1, 0x5CF83ACA,
                                                0x9F677D4E, 0x13D0BE1D, 0x4EEDFAE7, 0xD7E4AB5D, 0xE4279BA2, 0x9D5923D8};
            for (uint32_t name : integers)
                if (Next() % 3)
                    GiveInteger(name, static_cast<int>(Next() % 400) - 50);
            static const uint32_t floats[] = {0x13B9DA7B, 0x3BEE67C8, 0x8E39913D, 0x3F243A3C, 0x05400A25,
                                              0xFF7D9AA9, 0x5539FCEE, 0x35B65BD4, 0xC218CF77, 0x520682C6, 0x59463C93};
            for (uint32_t name : floats)
                if (Next() % 3)
                    Give(name, RichFloat(20.0f));
            static const uint32_t vectors[] = {0x0C4C809E, 0x3D5EA6A4, 0x7F261953, 0x004B51A2, 0x5854AB9E,
                                               0x9656F775, 0xC3CD20A2, 0x829FE7DD, 0x9D2D0915};
            for (uint32_t name : vectors) {
                // (0x0C4C809E always: it's required, and without it both versions turn by their own stack's
                // leftovers)
                if (name != 0x0C4C809E && Next() % 3 == 0)
                    continue;
                if (name == 0x0C4C809E && Next() % 3 == 0)
                    GiveVector(name, 0.0f, Pick({1.0f, 3.0f}), 0.0f); // (straight up)
                else
                    GiveVector(name, RichFloat(50.0f), RichFloat(50.0f), RichFloat(50.0f));
            }
            static const uint32_t flags[] = {0xA7A5BFFB, 0x5446AF14, 0x7045843A, 0x64F194ED, 0x454962EF,
                                             0xB7B7240D, 0xA2487CC3, 0xA0E585B0};
            for (uint32_t name : flags)
                if (Next() % 2)
                    GiveFlag(name);
            g_components.push_back({kOther, 0x286A8D26, kMine + 0x1D00});
        },
        [] {
            Original<void(__thiscall*)(void*, void*)>(0x0042A090)(Particle(), Params());
            return 0u;
        },
        [] {
            Particle()->InitFromStructure(Params());
            return 0u;
        });
    Check(ok, "particle component: InitFromStructure (the parameters made, oriented, scaled, coloured, rotated; the "
              "model, the script; original vs ours)");
}

void TestParticleGetParams()
{
    StandIns standIns(
        {{0x004AAD40, 0, 0, Ret::Int, true, 0, {Ours(&Obj::ParticleSystemParams)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kParticleParams)); }},
         {0x005BA3D0, 0, 0, Ret::Int, true, 0, {Ours(&Mem::Pool::Allocate)}, [](Frame&, Result& r) {
              r.eax = reinterpret_cast<uint32_t>(ArenaPtr(g_blockNext));
              g_blockNext += 0x10;
          }},
         Allocate(), EnsureManager(),
         {0x00476410, 0, 0, Ret::Int, true, 0, {Ours(&Script::CStruct::Init)},
          [](Frame& f, Result& r) { r.eax = f.ecx; }},
         {0x0046CAA0, 0, 0, Ret::Int, true, 0, {Ours(&Script::CArray::Init)},
          [](Frame& f, Result& r) { r.eax = f.ecx; }},
         {0x00477D40, 8, 2, Ret::None, true, 0,
          {Ours(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))}},
         {0x00477C60, 8, 2, Ret::None, true, 0,
          {Ours(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))}},
         {0x00477F70, 20, 5, Ret::None, true, 0,
          {Ours(static_cast<void (Script::CStruct::*)(uint32_t, Script::VectorValue)>(&Script::CStruct::AddVector))}},
         {0x00478B00, 8, 2, Ret::None, true, 0,
          {Ours(static_cast<void (Script::CStruct::*)(uint32_t, Script::CArray*)>(&Script::CStruct::AddArray))}},
         {0x00478670, 8, 2, Ret::None, true, 0,
          {Ours(static_cast<void (Script::CStruct::*)(uint32_t, Script::CStruct*)>(&Script::CStruct::AddStructure))}},
         {0x0046CC50, 8, 2, Ret::None, true, 0, {Ours(&Script::CArray::SetSizeAndType)}},
         {0x0046CB10, 8, 2, Ret::None, true, 0, {Ours(&Script::CArray::SetElement)}},
         {0x0046CAB0, 0, 0, Ret::None, true, 0, {Ours(&Script::CArray::Clear)}},
         {0x00477130, 0, 0, Ret::None, true, 0, {Ours(&Script::CStruct::Clear)}},
         {0x00408FD0, 0, 1, Ret::None, false, 0, {Ours(&Script::DeleteArray)}},
         {0x00409070, 0, 1, Ret::None, false, 0, {Ours(&Script::DeleteStruct)}}});
    bool ok = CompareRuns(
        "Particle GetParticleParams", 300,
        [](int) {
            RandomParticle();
            ArenaAt<uint8_t*>(kComp + 0x1C) = ArenaPtr(kParticle); // (no system: a crash in both)
        },
        [] {
            return static_cast<uint32_t>(Original<bool(__thiscall*)(void*, void*, void*)>(0x0042B040)(
                Particle(), Params(), TheScript()));
        },
        [] { return static_cast<uint32_t>(Particle()->GetParticleParams(Params(), TheScript())); });
    Check(ok, "particle component: GetParticleParams (original vs ours)");
}

// --- the proximity component ---
void RandomProximity()
{
    ArenaAt<uint8_t*>(kComp) = ArenaPtr(kCompVtable);
    ArenaAt<uint32_t>(kCompVtable + 2 * 4) = 0x005480A0; // (slot 2: InitFromStructure, stood in)
    ArenaAt<uint32_t>(kCompVtable + 7 * 4) = 0x005BAB90; // (slot 7: Suspend, stood in)
    ArenaAt<uint8_t>(kComp + 0x18) = Next() % 4 != 0;
    ArenaAt<float>(kComp + 0x20) = Pick({2.0f, 5.0f, 10.0f});
    ArenaAt<float>(kComp + 0x24) = Pick({5.0f, 8.0f, 20.0f});
    ArenaAt<int>(kComp + 0x34) = static_cast<int>(Next() % 2);
    RichVector(kObj + 0x4C, 300.0f);
    for (int i = 0; i < 3; ++i)
        ArenaAt<float>(kOther + 0x4C + 4 * i) = ArenaAt<float>(kObj + 0x4C + 4 * i) + RichFloat(15.0f);
    GameVariable<Obj::CCompositeObject*>(0x0069399C) =
        Next() % 6 ? reinterpret_cast<Obj::CCompositeObject*>(ArenaPtr(kOther)) : nullptr;
    GameVariable<float>(0x006939A8) = Pick({0.0f, 3.0f, 6.0f});
    if (Next() % 5 == 0) {
        // (the skater exactly the reach away)
        float reach = GameVariable<float>(0x006939A8) + ArenaAt<float>(kComp + 0x24);
        const float at[3] = {100.0f, 20.0f, -40.0f};
        int axis = static_cast<int>(Next() % 3);
        for (int i = 0; i < 3; ++i) {
            ArenaAt<float>(kObj + 0x4C + 4 * i) = at[i];
            ArenaAt<float>(kOther + 0x4C + 4 * i) = at[i] + (i == axis ? reach : 0.0f);
        }
    }
}

void TestProximity()
{
    auto* savedSkater = GameVariable<Obj::CCompositeObject*>(0x0069399C);
    float savedRadius = GameVariable<float>(0x006939A8);
    bool ok;
    {
        StandIns standIns({{0x0042B620, 8, 2, Ret::None, true, 0, {Ours(&CProximityComponent::RunScript)}}});
        ok = CompareRuns(
            "Proximity Update", 3000, [](int) { RandomProximity(); },
            [] {
                Original<void(__thiscall*)(void*)>(0x0042B700)(Proximity());
                return 0u;
            },
            [] {
                Proximity()->Update();
                return 0u;
            });
    }
    StandIns standIns({{0x0042B700, 0, 0, Ret::None, true, 0, {Ours(&CProximityComponent::Update)}},
                       {0x005480A0, 4, 1, Ret::None, true, 0},
                       {0x005BAB90, 4, 1, Ret::None, true, 1},
                       ContainsFlag(), GetFloat(), GameClock()});
    auto member = [](const char* what, uint32_t address,
                     bool (CProximityComponent::*ours)(Script::CStruct*, Script::CScript*)) {
        return CompareRuns(
            what, 60,
            [](int) {
                RandomProximity();
                if (Next() % 2)
                    GiveFlag(0x742CF11F);
                if (Next() % 2)
                    Give(0x9D02737D, RichFloat(5.0f));
            },
            [address] {
                return static_cast<uint32_t>(
                    Original<bool(__thiscall*)(void*, void*, void*)>(address)(Proximity(), Params(), TheScript()));
            },
            [ours] { return static_cast<uint32_t>((Proximity()->*ours)(Params(), TheScript())); });
    };
    ok = ok && member("Proximity_Init", 0x0042B850, &CProximityComponent::Proximity_Init);
    ok = ok && member("Proximity_SetActive", 0x0042B590, &CProximityComponent::Proximity_SetActive);
    ok = ok && member("Proximity_EnterProximityNotHandled", 0x0042B570,
                      &CProximityComponent::Proximity_EnterProximityNotHandled);
    ok = ok && CompareRuns(
                   "CSmtPtr Construct", 5, [](int) {},
                   [] {
                       return reinterpret_cast<uint32_t>(
                           Original<void*(__thiscall*)(void*)>(0x0042B580)(ArenaPtr(kMine + 0x100)));
                   },
                   [] {
                       return reinterpret_cast<uint32_t>(
                           reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>*>(ArenaPtr(kMine + 0x100))->Construct());
                   });
    GameVariable<Obj::CCompositeObject*>(0x0069399C) = savedSkater;
    GameVariable<float>(0x006939A8) = savedRadius;
    Check(ok, "proximity component: Update (in, out, inactive, no skater), Proximity_Init, Proximity_SetActive, "
              "Proximity_EnterProximityNotHandled; the smart pointer's constructor (original vs ours)");
}

// --- the chain component ---
CChain* ChainAt(uint32_t offset) { return reinterpret_cast<CChain*>(ArenaPtr(offset)); }

// A chain at `offset` with `links` links (lines at `lines`), its numbers at random.
void RandomChain(uint32_t offset, uint32_t lines, int links)
{
    CChain* chain = ChainAt(offset);
    chain->m_id = Next() % 2 ? 0x1111 : 0x2222;
    chain->m_links = links;
    chain->m_iterations = static_cast<int>(Pick({30.0f, 60.0f, 120.0f}));
    chain->m_damping = Pick({0.0f, 0.5f, 2.0f});
    chain->m_gravity = Pick({0.0f, 100.0f, -50.0f});
    chain->m_moveWithObject = Next() % 2 != 0;
    chain->m_collideBelow = Next() % 2 != 0;
    chain->m_collideFirst = static_cast<int>(Next() % (links + 1));
    chain->m_collideEnd = static_cast<int>(Next() % (links + 1));
    chain->m_planeOffset = RichFloat(5.0f);
    for (int i = 0; i < CChain::kMaxLinks; ++i) {
        CChainLink& link = chain->m_link[i];
        link.m_length = Pick({1.0f, 5.0f, 12.0f});
        uint8_t* line = ArenaPtr(lines + i * 0x20);
        *reinterpret_cast<uint32_t*>(line) = kLineVtable;
        link.mp_line = Next() % 8 || i + 1 < links ? line : nullptr;
    }
}
// The component: chain A and (mostly) chain B in its list.
void RandomChains()
{
    RandomChain(kChainA, kLinesA, 2 + static_cast<int>(Next() % 19));
    RandomChain(kChainB, kLinesB, 2 + static_cast<int>(Next() % 19));
    uint32_t head = kNodes, a = kNodes + 0x10, b = kNodes + 0x20;
    bool two = Next() % 3 != 0;
    auto link = [](uint32_t node, uint32_t chain, uint32_t next, uint32_t prev) {
        ArenaAt<uint8_t*>(node) = chain ? ArenaPtr(chain) : nullptr;
        ArenaAt<uint8_t*>(node + 4) = ArenaPtr(next);
        ArenaAt<uint8_t*>(node + 8) = ArenaPtr(prev);
    };
    if (two) {
        link(head, 0, a, b);
        link(a, kChainA, b, head);
        link(b, kChainB, head, a);
    } else {
        link(head, 0, a, a);
        link(a, kChainA, head, head);
    }
    ArenaAt<uint8_t*>(kComp + 0x18) = ArenaPtr(head);
    ArenaAt<int>(kComp + 0x1C) = two ? 2 : 1;
    ArenaAt<uint8_t*>(kComp + 0x24) = ArenaPtr(kMine + 0x1C00); // the skeleton component
    ArenaAt<float>(kComp + 0x2C) = Pick({1.0f / 60, 0.05f});
    float stepTime = Pick({0.0f, 0.3f, 0.99f});
    ArenaAt<float>(kComp + 0x30) = stepTime;
    // (the chains share one count of passes done: with different rates the second's due passes could go
    // negative - a count down from there that never ends, in the original as in ours)
    int iterations = ChainAt(kChainA)->m_iterations;
    ChainAt(kChainB)->m_iterations = iterations;
    ArenaAt<int>(kComp + 0x34) = static_cast<int>(stepTime * iterations) - static_cast<int>(Next() % 2);
    ArenaAt<uint8_t>(kComp + 0x44) = Next() % 4 == 0;
    ArenaAt<uint32_t>(kObj + 0x104) = Next() % 4 == 0 ? 0x4 : 0; // (the object's flags: hidden or not)
    for (uint32_t i = 0; i < 4; ++i) {
        RichVector(kObj + 0xC0 + i * 16, 1.0f);
        RichVector(kOther + 0xC0 + i * 16, 1.0f);
    }
    RichVector(kObj + 0x4C, 300.0f);
    RichVector(kOther + 0x4C, 300.0f);
    RichVector(kObj + 0xAC, 50.0f);
}

StandIn LineSlot(uint32_t address, uint8_t pop, uint8_t args) { return {address, pop, args, Ret::None, true, 0}; }
StandIn BoneOut(uint32_t address, uint32_t ours)
{
    return {address, 8, 2, Ret::Bool, true, 0, {ours}, [](Frame& f, Result&) {
                auto* out = reinterpret_cast<float*>(f.args[1]);
                for (int i = 0; i < 4; ++i)
                    out[i] = RichFloat(i == 3 ? 1.0f : 60.0f);
            }};
}

void TestChainLife()
{
    bool savedReady = GameVariable<bool>(0x007CC979);
    StandIns standIns({CallocCall(), BaseConstruct(), BaseDestroyBody(), ManagerFree(), FrameSeconds(), EnsureManager(),
                       Allocate(), GetComponent(), {0x004C5ED0, 4, 1, Ret::Int, true, 0},
                       {0x004ECC30, 12, 3, Ret::Int, true, 0}});
    bool ok = CompareRuns(
        "Chain Create", 40,
        [](int v) {
            g_allocFails = v % 4 == 3;
            GameVariable<bool>(0x007CC979) = v % 3 != 0;
        },
        [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x004298B0)()); },
        [] { return reinterpret_cast<uint32_t>(CChainComponent::Create()); });
    GameVariable<bool>(0x007CC979) = savedReady;
    ok = ok && CompareRuns(
                   "Chain Destroy", 60,
                   [](int v) {
                       RandomChains();
                       if (v % 5 == 4)
                           ArenaAt<int>(kComp + 0x1C) = 0;
                   },
                   [] {
                       uint32_t flags = g_rng & 3;
                       return reinterpret_cast<uint32_t>(
                           Original<void*(__thiscall*)(void*, unsigned int)>(0x00429910)(Chains(), flags));
                   },
                   [] {
                       uint32_t flags = g_rng & 3;
                       return reinterpret_cast<uint32_t>(Chains()->Destroy(flags));
                   });
    ok = ok && CompareRuns(
                   "ChainList Destroy", 10, [](int) { RandomChains(); },
                   [] {
                       Original<void(__thiscall*)(void*)>(0x004296B0)(ArenaPtr(kComp + 0x18));
                       return 0u;
                   },
                   [] {
                       Chains()->m_chains.Destroy();
                       return 0u;
                   });
    ok = ok && CompareRuns(
                   "Chain Finalize", 10,
                   [](int v) {
                       if (v % 2)
                           g_components.push_back({kObj, 0x222756D5, kMine + 0x1C00});
                       if (v % 3)
                           g_components.push_back({kObj, 0x286A8D26, kMine + 0x1D00});
                   },
                   [] {
                       Original<void(__thiscall*)(void*)>(0x00428370)(Chains());
                       return 0u;
                   },
                   [] {
                       Chains()->Finalize();
                       return 0u;
                   });
    ok = ok && CompareRuns(
                   "CChain Construct", 5, [](int) {},
                   [] { return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*)>(0x00428290)(ChainAt(kChainA))); },
                   [] { return reinterpret_cast<uint32_t>(ChainAt(kChainA)->Construct()); });
    ok = ok && CompareRuns(
                   "CChainLink DestroyBody", 20, [](int) { RandomChains(); },
                   [] {
                       Original<void(__thiscall*)(void*)>(0x00428250)(&ChainAt(kChainA)->m_link[g_rng % 20]);
                       return 0u;
                   },
                   [] {
                       ChainAt(kChainA)->m_link[g_rng % 20].DestroyBody();
                       return 0u;
                   });
    Check(ok, "chain component: Create, Destroy, the list's destructor, Finalize; a chain's constructor, a link's "
              "destructor (original vs ours)");
}

void TestChainMembers()
{
    StandIns standIns({GetChecksum(), GetFloat(), GetInteger(), GetVector(), ContainsFlag()});
    bool ok = CompareRuns(
        "Chain CallMemberFunction", 3000,
        [](int) {
            RandomChains();
            GiveChecksum(0x62BF8E3F, Next() % 3 ? 0x1111 : 0x2222);
            if (Next() % 2)
                Give(0x7E832F08, RichFloat(5.0f));
            if (Next() % 2)
                GiveInteger(0xE4D4B316, static_cast<int>(Next() % 5));
            if (Next() % 2)
                GiveInteger(0x98382F87, static_cast<int>(Next() % 5));
            GiveVector(0xEC13B28C, RichFloat(50.0f), RichFloat(50.0f), RichFloat(50.0f));
            if (Next() % 3 == 0)
                GiveInteger(0xD037C420, static_cast<int>(Next() % 20));
            if (Next() % 3 == 0)
                GiveFlag(0x3EA1D93A);
            if (Next() % 3 == 0)
                GiveFlag(0x5F78A252);
            if (Next() % 2)
                GiveFlag(0x5B6634D4);
        },
        [] {
            static const uint32_t names[] = {0x286BA49C, 0x317A113A, 0x982C70E5, 0x12345678};
            uint32_t name = names[Next() % 4];
            return static_cast<uint32_t>(Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x004294C0)(
                Chains(), name, Params(), TheScript()));
        },
        [] {
            static const uint32_t names[] = {0x286BA49C, 0x317A113A, 0x982C70E5, 0x12345678};
            uint32_t name = names[Next() % 4];
            return static_cast<uint32_t>(Chains()->CallMemberFunction(name, Params(), TheScript()));
        });
    Check(ok, "chain component: CallMemberFunction (collision, a link placed, hidden; original vs ours)");
}

void TestChainInit()
{
    bool savedReady = GameVariable<bool>(0x007CC979);
    StandIns standIns(
        {GetChecksum(), GetFloat(), GetInteger(), GetVector(), ContainsFlag(), EnsureManager(), Allocate(),
         {0x004767C0, 12, 3, Ret::Bool, true, 0,
          {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int)>(&Script::CStruct::GetStructure))},
          [](Frame& f, Result& r) {
              r.eax = Next() % 3 != 0;
              if (r.eax)
                  *reinterpret_cast<void**>(f.args[1]) = Params();
          }},
         ArrayGetter(0x00476860),
         {0x0046CC30, 4, 1, Ret::Int, true, 0, {Ours(&Script::CArray::GetElement)},
          [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(Params()); }},
         {0x004C5230, 4, 1, Ret::Int, true, 0, {},
          [](Frame& f, Result& r) {
              *reinterpret_cast<uint32_t*>(f.ecx) = kLineVtable;
              r.eax = f.ecx;
          }},
         {0x004A10A0, 12, 3, Ret::None, true, 0}, LineSlot(0x004C56E0, 8, 2), LineSlot(0x004C5B20, 16, 4),
         {0x0049C6C0, 0, 0, Ret::None, true, 0}});
    bool ok = CompareRuns(
        "Chain InitFromStructure", 600,
        [](int v) {
            RandomChains();
            ArenaAt<uint8_t*>(kComp + 0x18) = ArenaPtr(kNodes); // (an empty list)
            ArenaAt<uint8_t*>(kNodes + 4) = ArenaPtr(kNodes);
            ArenaAt<uint8_t*>(kNodes + 8) = ArenaPtr(kNodes);
            ArenaAt<int>(kComp + 0x1C) = 0;
            ArenaAt<uint32_t>(kArray + 4) = v % 7 == 6 ? 0 : 1 + Next() % 2;
            GameVariable<bool>(0x007CC979) = v % 3 != 0;
            GiveChecksum(0x0078395B, Next());
            GiveChecksum(0xD962E451, Next());
            GiveChecksum(0x94291927, Next());
            GiveInteger(0xD93124D6, 1 + static_cast<int>(Next() % 3));
            Give(0xFE82614D, RichFloat(40.0f));
            GiveChecksum(0x62BF8E3F, Next());
            if (Next() % 2)
                GiveFlag(0xC96BA2C1);
            if (Next() % 2)
                GiveChecksum(0xF2D1C4CA, Next());
            GiveVector(0x6C3AA2BF, RichFloat(5.0f), RichFloat(5.0f), RichFloat(5.0f));
            GiveChecksum(0x7A12D81C, Next());
            GiveChecksum(0xE5002867, Next());
            Give(0x2CCF354D, RichFloat(100.0f));
            Give(0x84750D32, RichFloat(2.0f));
            GiveInteger(0x41E56E7B, static_cast<int>(Next() % 100));
            if (Next() % 2)
                GiveFlag(0x4BFBDC28);
            if (Next() % 2)
                Give(0x5F8314B3, RichFloat(5.0f));
            if (Next() % 2)
                GiveInteger(0x07EBDE63, static_cast<int>(Next() % 4));
            if (Next() % 2)
                GiveInteger(0xB721AB4A, static_cast<int>(Next() % 4));
        },
        [] {
            Original<void(__thiscall*)(void*, void*)>(0x004283B0)(Chains(), Params());
            return 0u;
        },
        [] {
            Chains()->InitFromStructure(Params());
            return 0u;
        });
    GameVariable<bool>(0x007CC979) = savedReady;
    Check(ok, "chain component: InitFromStructure (collision_info or not, one or two chains, their links and lines; "
              "original vs ours)");
}

void TestChainUpdate()
{
    bool ok;
    {
        StandIns standIns(
            {FrameSeconds(), BoneOut(0x00432050, Ours(&Obj::CSkeletonComponent::GetBonePosition)),
             BoneOut(0x00431C60, Ours(&Obj::CSkeletonComponent::GetLocalBonePosition)),
             {0x005480F0, 0, 0, Ret::Int, true, 0, {Ours(&GameNet::FoldedGetters::Word18)}, [](Frame&, Result& r) {
                  r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kSkeleton));
              }},
             {0x004A36E0, 8, 2, Ret::Bool, true, 0, {}, [](Frame& f, Result&) {
                  auto* out = reinterpret_cast<float*>(f.args[1]);
                  for (int i = 0; i < 16; ++i)
                      out[i] = RichFloat(i >= 12 ? 20.0f : 1.0f);
              }},
             {0x00460AD0, 0, 1, Ret::Int, false, 0, {Ours(&Obj::FindObject)},
              [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kOther)); }},
             GetComponent(), LineSlot(0x004C56E0, 8, 2), LineSlot(0x004C5B20, 16, 4),
             {0x00428300, 0, 0, Ret::None, true, 0, {Ours(&CChain::UpdateLines)}}});
        ok = CompareRuns(
            "Chain Update", 3000,
            [](int) {
                RandomChains();
                for (uint32_t offset : {kChainA, kChainB})
                    for (int i = 0; i < CChain::kMaxLinks; ++i) {
                        uint32_t link = offset + 0x24 + i * 0x50;
                        RichVector(link, 100.0f);
                        for (int j = 0; j < 4; ++j)
                            ArenaAt<float>(link + 0x10 + j * 4) = ArenaAt<float>(link + j * 4) + RichFloat(2.0f);
                    }
                g_components.push_back({kOther, 0x222756D5, kMine + 0x1D00});
            },
            [] {
                Original<void(__thiscall*)(void*)>(0x004287A0)(Chains());
                return 0u;
            },
            [] {
                Chains()->Update();
                return 0u;
            });
    }
    // (UpdateLines itself, not stood in)
    StandIns standIns({LineSlot(0x004C56E0, 8, 2), LineSlot(0x004C5B20, 16, 4)});
    ok = ok && CompareRuns(
                   "CChain UpdateLines", 100, [](int) { RandomChains(); },
                   [] {
                       Original<void(__thiscall*)(void*)>(0x00428300)(ChainAt(kChainA));
                       return 0u;
                   },
                   [] {
                       ChainAt(kChainA)->UpdateLines();
                       return 0u;
                   });
    Check(ok, "chain component: Update (Verlet steps, the ends, constraint passes, the plane, lines shown or not), "
              "a chain's UpdateLines (original vs ours)");
}

} // namespace

SELFTEST(Early, TestParticleLife);
SELFTEST(Early, TestParticleMembers);
SELFTEST(Early, TestParticleUpdate);
SELFTEST(Early, TestParticleInit);
SELFTEST(Early, TestParticleGetParams);
SELFTEST(Early, TestProximity);
SELFTEST(Early, TestChainLife);
SELFTEST(Early, TestChainMembers);
SELFTEST(Early, TestChainInit);
SELFTEST(Early, TestChainUpdate);
