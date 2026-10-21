// Self-tests for the movable contact, node array, object hook manager and rail manager components
// (movable_contact.cpp, node_components.cpp): original vs ours on stand-in components in skater_core_testkit.h's
// arena, every callee stood in for - but the feeler's constructor and the matrix maths (Matrix::operator=,
// Invert, RotateVector), which run as they are (the original's and ours).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/nx/feeler.h"
#include "game/obj/movable_contact.h"
#include "game/obj/node_components.h"
#include "game/obj/node_objects.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
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
using Obj::CMovableContactComponent;
using Obj::CNodeArrayComponent;
using Obj::CObjectHookManagerComponent;
using Obj::CRailManagerComponent;

// Where things are (the components at kMotion; the rest as motion_testkit.h's).
constexpr uint32_t kComp = kMotion;
constexpr uint32_t kRide = kMine;           // a ride tracker (0xB0)
constexpr uint32_t kRidden = kOtherObj;     // the object ridden
constexpr uint32_t kVtableCopy = kMine + 0x200; // a stand-in vtable (16 slots)

CMovableContactComponent* Contact() { return reinterpret_cast<CMovableContactComponent*>(ArenaPtr(kComp)); }

StandIn Own(uint32_t address, uint32_t ours, uint8_t pop, uint8_t args, Ret ret = Ret::None)
{
    return {address, pop, args, ret, true, 0, {ours}};
}
StandIn CallocCall() { return Calloc(Ours(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))); }
StandIn ManagerFree() { return {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}}; }
StandIn BaseConstruct()
{
    return {0x0045D730, 0, 0, Ret::Int, true, 0, {Ours(&Obj::CBaseComponent::Construct)},
            [](Frame& f, Result& r) { r.eax = f.ecx; }};
}
StandIn BaseDestroyBody() { return {0x0045D740, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBaseComponent::DestroyBody)}}; }
StandIn ReturnThis(uint32_t address, uint32_t ours, uint8_t pop, uint8_t args)
{
    return {address, pop, args, Ret::Int, true, 0, {ours}, [](Frame& f, Result& r) { r.eax = f.ecx; }};
}

template <typename Call>
std::vector<uint32_t> Run(uint32_t seed, Call call)
{
    g_allocNext = kAlloc;
    return RunOnArena(seed, call);
}
template <typename Setup, typename Original, typename Ours>
bool CompareRuns(const char* what, int variants, Setup setup, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        g_rng = 0x3C6EF372u * static_cast<uint32_t>(variant + 9);
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

// --- the movable contact component ---
void RandomRide()
{
    ArenaAt<uint8_t*>(kComp + 0x18) = Next() % 3 ? ArenaPtr(kRide) : nullptr;
    ArenaAt<uint8_t*>(kRide) = Next() % 3 ? ArenaPtr(kRidden) : nullptr; // (the tracker's smart pointer)
    ArenaAt<uint8_t*>(kComp + 0x6C) = Next() % 3 ? ArenaPtr(kRidden + 0x8) : nullptr;
    RichVector(kComp + 0x1C, 50.0f);
    RichVector(kObj + 0x4C, 300.0f);
    RichVector(kRidden + 0x4C, 300.0f);
    RichVector(kRidden + 0x8 + 0x4C, 300.0f);
    for (uint32_t row = 0; row < 4; ++row) {
        RichVector(kRidden + 0x6C + row * 16, 1.0f);
        RichVector(kRidden + 0x8 + 0x6C + row * 16, 1.0f);
    }
}

void TestContactLife()
{
    StandIns standIns({CallocCall(), BaseConstruct(), BaseDestroyBody(), ManagerFree()});
    bool ok = CompareRuns(
        "MovableContact Create", 40, [](int v) { g_allocFails = v % 4 == 3; },
        [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x00427AB0)()); },
        [] { return reinterpret_cast<uint32_t>(CMovableContactComponent::Create()); });
    ok = ok && CompareRuns(
                   "MovableContact Destroy", 40, [](int) {},
                   [] {
                       uint32_t flags = g_rng & 3;
                       return reinterpret_cast<uint32_t>(
                           Original<void*(__thiscall*)(void*, unsigned int)>(0x004276D0)(Contact(), flags));
                   },
                   [] {
                       uint32_t flags = g_rng & 3;
                       return reinterpret_cast<uint32_t>(Contact()->Destroy(flags));
                   });
    Check(ok, "movable contact component: Create, Destroy (original vs ours)");
}

void TestContactFind()
{
    bool ok;
    {
        StandIns standIns(
            {{0x004172F0, 8, 2, Ret::Bool, true, 3, {Ours(&Nx::CFeeler::GetCollision)},
              [](Frame& f, Result& r) {
                  auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
                  for (int i = 0; i < 8; ++i)
                      Record(reinterpret_cast<const uint32_t*>(feeler)[i]);
                  r.eax = Next() % 4 != 0;
                  feeler->m_point = {RichFloat(100.0f), RichFloat(100.0f), RichFloat(100.0f), RichFloat(2.0f)};
              }},
             {0x00417210, 0, 0, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::HitObjectAlive)},
              [](Frame&, Result& r) { r.eax = Next() % 8 != 0; }},
             {0x00417240, 0, 0, Ret::Int, true, 0, {Ours(&Nx::CFeeler::GetHitObject)},
              [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kRidden)); }}});
        ok = CompareRuns(
            "MovableContact FindContact", 4000, [](int) { RichVector(kMine + 0x300, 300.0f); },
            [] {
                return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, void*, void*)>(0x00427700)(
                    Contact(), ArenaPtr(kMine + 0x300), ArenaPtr(kMine + 0x310)));
            },
            [] {
                return reinterpret_cast<uint32_t>(
                    Contact()->FindContact(reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x300)),
                                           reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x310))));
            });
    }
    {
        StandIns standIns({{0x00427700, 8, 2, Ret::Int, true, 0, {Ours(&CMovableContactComponent::FindContact)},
                            [](Frame& f, Result& r) {
                                for (int i = 0; i < 4; ++i)
                                    Record(reinterpret_cast<const uint32_t*>(f.args[0])[i]);
                                r.eax = Next() % 3 ? reinterpret_cast<uint32_t>(ArenaPtr(kRidden)) : 0;
                                auto* hit = reinterpret_cast<float*>(f.args[1]);
                                for (int i = 0; i < 4; ++i)
                                    hit[i] = RichFloat(100.0f);
                            }},
                           {0x005BEAA0, 0, 0, Ret::Float, false, 0, {Ours(&Tmr::GetSlomoFrameSeconds)}}});
        ok = ok && CompareRuns(
                       "MovableContact FindContactAhead", 3000,
                       [](int) {
                           RichVector(kMine + 0x300, 300.0f);
                           RichVector(kMine + 0x310, 300.0f);
                           RichVector(kRidden + 0xAC, 300.0f);
                       },
                       [] {
                           return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, void*, void*)>(
                               0x00427B30)(Contact(), ArenaPtr(kMine + 0x300), ArenaPtr(kMine + 0x310)));
                       },
                       [] {
                           return reinterpret_cast<uint32_t>(Contact()->FindContactAhead(
                               reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x300)),
                               reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x310))));
                       });
    }
    Check(ok, "movable contact component: FindContact (hits, blocked, gone, 9 tries), FindContactAhead");
}

void TestContactRiding()
{
    bool ok;
    {
        StandIns standIns({{0x00416DD0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CSmtPtr<Obj::CRefCounted>::Release)}},
                           ManagerFree(), {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}}});
        ok = CompareRuns(
            "MovableContact StopRiding", 200,
            [](int) {
                RandomRide();
                GameVariable<int>(0x00696D60) = Next() % 2 ? 0 : static_cast<int>(Next() % 100000); // (the clock's offset)
            },
            [] {
                Original<void(__thiscall*)(void*)>(0x00427C70)(Contact());
                return 0u;
            },
            [] {
                Contact()->StopRiding();
                return 0u;
            });
    }
    {
        StandIns standIns(
            {Own(0x00427C70, Ours(&CMovableContactComponent::StopRiding), 0, 0),
             {0x00417210, 0, 0, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::HitObjectAlive)}},
             {0x00417240, 0, 0, Ret::Int, true, 0, {Ours(&Nx::CFeeler::GetHitObject)},
              [](Frame&, Result& r) {
                  r.eax = reinterpret_cast<uint32_t>(ArenaPtr(Next() % 2 ? kRidden : kRidden + 0x40));
              }},
             {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(&Mem::EnsureManager)}},
             {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)},
              [](Frame&, Result& r) {
                  r.eax = Next() % 5 ? reinterpret_cast<uint32_t>(ArenaPtr(kAlloc)) : 0;
              }},
             ReturnThis(0x00416E70, Ours(&Obj::CRideTracker::Construct), 4, 1),
             // (Matrix::Invert stood in: ours differs from the original's in the last bit now and then - its own
             // business, not this component's; the matrix it's given is recorded and left as it is)
             {0x00405600, 0, 0, Ret::Int, true, 0, {Ours(&Mth::Matrix::Invert)}, [](Frame& f, Result& r) {
                  for (int i = 0; i < 16; ++i)
                      Record(reinterpret_cast<const uint32_t*>(f.ecx)[i]);
                  r.eax = f.ecx;
              }}});
        bool savedReady = GameVariable<bool>(0x007CC979);
        ok = ok && CompareRuns(
                       "MovableContact IsRiding", 100, [](int) { RandomRide(); },
                       [] { return static_cast<uint32_t>(Original<bool(__thiscall*)(void*)>(0x00427CB0)(Contact())); },
                       [] { return static_cast<uint32_t>(Contact()->IsRiding()); });
        ok = ok && CompareRuns(
                       "MovableContact StartRiding", 2000,
                       [](int) {
                           RandomRide();
                           GameVariable<bool>(0x007CC979) = Next() % 2 != 0;
                       },
                       [] {
                           return static_cast<uint32_t>(
                               Original<bool(__thiscall*)(void*, void*)>(0x00427CD0)(Contact(), ArenaPtr(kMine + 0x400)));
                       },
                       [] {
                           return static_cast<uint32_t>(
                               Contact()->StartRiding(reinterpret_cast<Nx::CFeeler*>(ArenaPtr(kMine + 0x400))));
                       });
        GameVariable<bool>(0x007CC979) = savedReady;
        ok = ok && CompareRuns(
                       "MovableContact UpdateContactPoint", 2000, [](int) { RandomRide(); },
                       [] {
                           Original<void(__thiscall*)(void*)>(0x00427DD0)(Contact());
                           return 0u;
                       },
                       [] {
                           Contact()->UpdateContactPoint();
                           return 0u;
                       });
        ok = ok && CompareRuns(
                       "MovableContact GetContactPoint", 1000, [](int) { RandomRide(); },
                       [] {
                           Original<void(__thiscall*)(void*, void*)>(0x00427A00)(Contact(), ArenaPtr(kMine + 0x500));
                           return 0u;
                       },
                       [] {
                           Contact()->GetContactPoint(reinterpret_cast<Mth::Vector*>(ArenaPtr(kMine + 0x500)));
                           return 0u;
                       });
    }
    GameVariable<int>(0x00696D60) = 0;
    Check(ok, "movable contact component: StopRiding, IsRiding, StartRiding, UpdateContactPoint, GetContactPoint");
}

// --- the node array, object hook manager and rail manager components ---
const char* g_model = nullptr;
StandIn GetStringBool()
{
    using Fn = bool (Script::CStruct::*)(uint32_t, const char**, int);
    return {0x00476C60, 12, 3, Ret::Bool, true, 0, {Ours(static_cast<Fn>(&Script::CStruct::GetString))},
            [](Frame& f, Result& r) {
                r.eax = g_model != nullptr;
                if (g_model)
                    *reinterpret_cast<const char**>(f.args[1]) = g_model;
            }};
}

void TestNodeComponents()
{
    bool ok;
    {
        StandIns standIns(
            {CallocCall(), BaseConstruct(), BaseDestroyBody(), ManagerFree(), GetStringBool(),
             {0x004087B0, 36, 9, Ret::Int, true, 0x70, {Ours(&Ass::CAssMan::LoadOrGetAsset)},
              [](Frame& f, Result& r) {
                  Record(Crc::GenerateCRCFromString(reinterpret_cast<const char*>(f.args[0])));
                  r.eax = Next() % 2 ? 0 : reinterpret_cast<uint32_t>(ArenaPtr(kMine + 0x600));
              }},
             Own(0x00427F20, Ours(&CNodeArrayComponent::InitFromStructure), 4, 1)});
        static const char* models[] = {"skater.mdl", "a.b.c", "nodot", "", nullptr, "x."};
        auto setup = [](int) {
            g_model = models[Next() % 6];
            ArenaAt<uint32_t>(kComp) = 0x0064628C; // (the game's vtable: slot 2 is the stand-in above)
            g_allocFails = Next() % 5 == 0;
        };
        ok = CompareRuns(
            "NodeArray Create", 40, setup, [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x00428010)()); },
            [] { return reinterpret_cast<uint32_t>(CNodeArrayComponent::Create()); });
        ok = ok && CompareRuns(
                       "NodeArray Destroy", 40, setup,
                       [] {
                           uint32_t flags = g_rng & 3;
                           return reinterpret_cast<uint32_t>(
                               Original<void*(__thiscall*)(void*, unsigned int)>(0x00427EF0)(ArenaPtr(kComp), flags));
                       },
                       [] {
                           uint32_t flags = g_rng & 3;
                           return reinterpret_cast<uint32_t>(
                               reinterpret_cast<CNodeArrayComponent*>(ArenaPtr(kComp))->Destroy(flags));
                       });
        ok = ok && CompareRuns(
                       "NodeArray RefreshFromStructure", 100, setup,
                       [] {
                           Original<void(__thiscall*)(void*, void*)>(0x00427EB0)(ArenaPtr(kComp), Params());
                           return 0u;
                       },
                       [] {
                           reinterpret_cast<CNodeArrayComponent*>(ArenaPtr(kComp))->RefreshFromStructure(Params());
                           return 0u;
                       });
    }
    {
        StandIns standIns(
            {GetStringBool(),
             {0x004087B0, 36, 9, Ret::Int, true, 0x70, {Ours(&Ass::CAssMan::LoadOrGetAsset)},
              [](Frame& f, Result& r) {
                  Record(Crc::GenerateCRCFromString(reinterpret_cast<const char*>(f.args[0])));
                  r.eax = Next() % 2 ? 0 : reinterpret_cast<uint32_t>(ArenaPtr(kMine + 0x600));
              }}});
        static const char* models[] = {"skater.mdl", "a.b.c", "nodot", "", nullptr, "x."};
        ok = ok && CompareRuns(
                       "NodeArray InitFromStructure", 200, [](int) { g_model = models[Next() % 6]; },
                       [] {
                           Original<void(__thiscall*)(void*, void*)>(0x00427F20)(ArenaPtr(kComp), Params());
                           return 0u;
                       },
                       [] {
                           reinterpret_cast<CNodeArrayComponent*>(ArenaPtr(kComp))->InitFromStructure(Params());
                           return 0u;
                       });
    }
    {
        StandIns standIns(
            {CallocCall(), BaseConstruct(), BaseDestroyBody(), ManagerFree(), GetComponent(),
             {0x004693D0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CClassNodeList::Clear)}},
             ReturnThis(0x004693C0, Ours(&Obj::CClassNodeList::Init), 0, 0),
             {0x00469430, 4, 1, Ret::None, true, 0, {Ours(&Obj::CClassNodeList::SetNodes)}},
             {0x0057C330, 0, 0, Ret::None, true, 0, {Ours(&Obj::CRailManager::Cleanup)}},
             ReturnThis(0x0057CC10, Ours(&Obj::CRailManager::Init), 0, 0),
             {0x0057E960, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRailManager::AddFromNodeArray)}},
             {0x0057C4F0, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CRailManager::SetObjectMatrix)},
              [](Frame& f, Result& r) {
                  for (int i = 0; i < 16; ++i)
                      Record(reinterpret_cast<const uint32_t*>(f.args[0])[i]);
                  r.eax = f.ecx;
              }}});
        auto setup = [](int) {
            ArenaAt<uint8_t*>(kComp + 0x18) = Next() % 2 ? ArenaPtr(kMine) : nullptr;
            // (the node array component: its vtable's slot 13 the game's getter of +0x18)
            ArenaAt<uint8_t*>(kMine + 0x700) = ArenaPtr(kVtableCopy);
            ArenaAt<uint32_t>(kVtableCopy + 13 * 4) = 0x005480F0;
            g_components.push_back({kObj, 0xC472ECC5, kMine + 0x700});
            for (uint32_t row = 0; row < 4; ++row)
                RichVector(kObj + 0x6C + row * 16, 1.0f);
            RichVector(kObj + 0x4C, 100.0f);
        };
        ok = ok && CompareRuns(
                       "ObjectHookManager Create", 20, setup,
                       [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x00428130)()); },
                       [] { return reinterpret_cast<uint32_t>(CObjectHookManagerComponent::Create()); });
        ok = ok && CompareRuns(
                       "ObjectHookManager Destroy", 40, setup,
                       [] {
                           uint32_t flags = g_rng & 3;
                           return reinterpret_cast<uint32_t>(
                               Original<void*(__thiscall*)(void*, unsigned int)>(0x00428220)(ArenaPtr(kComp), flags));
                       },
                       [] {
                           uint32_t flags = g_rng & 3;
                           return reinterpret_cast<uint32_t>(
                               reinterpret_cast<CObjectHookManagerComponent*>(ArenaPtr(kComp))->Destroy(flags));
                       });
        ok = ok && CompareRuns(
                       "ObjectHookManager InitFromStructure", 40, setup,
                       [] {
                           Original<void(__thiscall*)(void*, void*)>(0x00428090)(ArenaPtr(kComp), Params());
                           return 0u;
                       },
                       [] {
                           reinterpret_cast<CObjectHookManagerComponent*>(ArenaPtr(kComp))->InitFromStructure(Params());
                           return 0u;
                       });
        ok = ok && CompareRuns(
                       "RailManager Create", 20, setup,
                       [] { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x0042B920)()); },
                       [] { return reinterpret_cast<uint32_t>(CRailManagerComponent::Create()); });
        ok = ok && CompareRuns(
                       "RailManager Destroy", 40, setup,
                       [] {
                           uint32_t flags = g_rng & 3;
                           return reinterpret_cast<uint32_t>(
                               Original<void*(__thiscall*)(void*, unsigned int)>(0x0042BA90)(ArenaPtr(kComp), flags));
                       },
                       [] {
                           uint32_t flags = g_rng & 3;
                           return reinterpret_cast<uint32_t>(
                               reinterpret_cast<CRailManagerComponent*>(ArenaPtr(kComp))->Destroy(flags));
                       });
        ok = ok && CompareRuns(
                       "RailManager InitFromStructure", 40, setup,
                       [] {
                           Original<void(__thiscall*)(void*, void*)>(0x0042B880)(ArenaPtr(kComp), Params());
                           return 0u;
                       },
                       [] {
                           reinterpret_cast<CRailManagerComponent*>(ArenaPtr(kComp))->InitFromStructure(Params());
                           return 0u;
                       });
        ok = ok && CompareRuns(
                       "RailManager UpdateRailMatrix", 100, setup,
                       [] {
                           return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, void*)>(0x0042BA10)(
                               ArenaPtr(kComp), ArenaPtr(kMine + 0x800)));
                       },
                       [] {
                           return reinterpret_cast<uint32_t>(
                               reinterpret_cast<CRailManagerComponent*>(ArenaPtr(kComp))
                                   ->UpdateRailMatrix(reinterpret_cast<Mth::Matrix*>(ArenaPtr(kMine + 0x800))));
                       });
    }
    Check(ok, "node array, object hook manager and rail manager components (original vs ours)");
}

} // namespace

SELFTEST(Early, TestContactLife);
SELFTEST(Early, TestContactFind);
SELFTEST(Early, TestContactRiding);
SELFTEST(Early, TestNodeComponents);
