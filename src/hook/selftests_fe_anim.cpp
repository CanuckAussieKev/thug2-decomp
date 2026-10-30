// Self-tests for the animation controllers (src/game/gfx/anim_controller.cpp): the original and ours run the same
// calls on identical stand-in controllers, timelines, objects, skeletons and parameter structures in an arena;
// everything they call outside the batch (frame sources and frames, the timeline's pieces, the anim set, the
// memory manager and the item pools, the structure getters and adders) is a recording stand-in, so the two are
// compared call by call, then the arena word by word.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/anim_controller.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/sys/game_clock.h"
#include "game/script/cscript.h"
#include "game/sys/game_clock.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/frame_source.h"
#include "hook/fe_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;
using namespace Gfx;

// Arena layout.
constexpr uint32_t kCtrl = 0x100;       // the controller (up to 0x854 bytes)
constexpr uint32_t kTimeline = 0x1000;  // the channel's timeline (+0x64: its object)
constexpr uint32_t kPartial = 0x1100;   // a partial timeline made by the constructor (Calloc)
constexpr uint32_t kObject = 0x1200;
constexpr uint32_t kBlender = 0x1400;
constexpr uint32_t kSkelComp = 0x1500;  // the Skeleton component (+0x18: the skeleton)
constexpr uint32_t kSkeleton = 0x1600;  // +0x08 bones, +0x0C count
constexpr uint32_t kBones = 0x1700;
constexpr uint32_t kSources = 0x2000;   // frame sources, 0x300 apart
constexpr uint32_t kFrames = 0x4000;    // frames, 0x40 apart (+0x24: has an index)
constexpr uint32_t kPose = 0x6000;      // 0x840 bytes
constexpr uint32_t kParams = 0x7000;    // structures, 0x40 apart
constexpr uint32_t kScript = 0x7800;    // +0x14: its params
constexpr uint32_t kGlobal = 0x7900;
constexpr uint32_t kManager = 0x7A00;
constexpr uint32_t kPools = 0x7B00;     // two stand-in pools
constexpr uint32_t kItems = 0x7C00;
constexpr uint32_t kEnd = 0x8000;

int g_sources, g_frames, g_indexes, g_clockCalls;
float g_step;
bool g_noSkeleton;
void* g_timelineVtable[5];
uint32_t g_containsWhich; // which stand-in pool says it holds the item (0, 1, or 2 = none)

uint32_t Params(int n) { return At(kParams + 0x40 * n); }

void InstallStandIns(StandIns& s)
{
    StandInStructGetters(s);
    StandInStructAdders(s);
    s.At({0x00419AB0, AddressOf(&Obj::CAnimBlenderComponent::GetAnimSet)}, 0, [](Regs& r) {
        Rec(0x9AB0, {r.ecx});
        return 0x10000u;
    });
    AtFloat(s, {0x00419AF0, AddressOf(&Obj::CAnimBlenderComponent::GetValue)}, 4, [](Regs& r) {
        Rec(0x9AF0, {r.ecx, r.args[0]});
        return 30.5f;
    });
    AtFloat(s, {0x004814B0}, 0, [](Regs& r) {
        Rec(0x14B0, {r.ecx});
        return g_step;
    });
    s.At({0x00481E10}, 8, [](Regs& r) {
        Rec(0x1E10, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00481430}, 0x18, [](Regs& r) {
        Rec(0x1430, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0u;
    });
    s.At({0x004816B0}, 4, [](Regs& r) {
        Rec(0x16B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004813F0, AddressOf(&Gfx::CAnimTimeline::IsComplete)}, 0, [](Regs& r) {
        Rec(0x13F0, {r.ecx});
        return 0xABCD01u; // (AL = 1)
    });
    s.At({0x0048BFC0}, 0, [](Regs&) {
        Rec(0xBFC0, {});
        return At(kSources + 0x300 * (g_sources++ % 8));
    });
    s.At({0x0048BFD0}, 0, [](Regs& r) {
        Rec(0xBFD0, {r.args[0]});
        return 0u;
    });
    s.At({0x0048CFA0, AddressOf(&Nx::CFrameSource::SetDone)}, 4, [](Regs& r) {
        Rec(0xCFA0, {r.ecx, r.args[0] & 0xFF});
        return At(kFrames + 0x40 * (g_frames++ % 16));
    });
    s.At({0x0048CFD0, AddressOf(&Nx::CFrameSource::SetAnim)}, 4, [](Regs& r) {
        Rec(0xCFD0, {r.ecx, r.args[0]});
        return At(kFrames + 0x40 * (g_frames++ % 16));
    });
    s.At({0x00482BA0}, 0, [](Regs& r) {
        Rec(0x2BA0, {r.ecx});
        return static_cast<uint32_t>(7 + g_indexes++);
    });
    s.At({0x00482BD0}, 4, [](Regs& r) {
        Rec(0x2BD0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00485730, AddressOf(&Gfx::CBonedAnimFrameData::GetFrames)}, 0x18, [](Regs& r) {
        Rec(0x5730, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 1u;
    });
    s.At({0x00485250}, 0x14, [](Regs& r) {
        Rec(0x5250, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return 1u;
    });
    s.At({0x00485270}, 4, [](Regs& r) {
        Rec(0x5270, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00480660}, 0, [](Regs&) {
        Rec(0x0660, {});
        return At(kGlobal);
    });
    s.At({0x00480800}, 0, [](Regs& r) {
        Rec(0x0800, {r.ecx});
        return 0x4321u;
    });
    s.At({0x004877E0}, 0, [](Regs& r) {
        Rec(0x77E0, {r.args[0] ? 1u : 0u, r.args[1] ? 1u : 0u, r.args[2], r.args[3], r.args[4], r.args[5],
                     *reinterpret_cast<uint32_t*>(r.args[0]), *reinterpret_cast<uint32_t*>(r.args[1])});
        *reinterpret_cast<float*>(r.args[0]) = 1.5f;
        *reinterpret_cast<float*>(r.args[1]) = 9.25f;
        return 0u;
    });
    s.At({0x00487770}, 0, [](Regs& r) {
        Rec(0x7770, {r.args[1]});
        *reinterpret_cast<int*>(r.args[0]) = 2;
        return 0u;
    });
    s.At({0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, [](Regs&) {
        Rec(0xECE0, {});
        return static_cast<uint32_t>(123457 + 7919 * g_clockCalls++);
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        if (r.args[0] == 0x7149B753)
            return At(kBlender);
        return g_noSkeleton ? 0u : At(kSkelComp);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return At(kItems);
    });
    s.At({0x004E1950, AddressOf(&Mem::EnsureManager)}, 0, [](Regs&) {
        Rec(0x1950, {});
        return 0u;
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Rec(0xA3D0, {r.ecx});
        return At(kItems + 0x40);
    });
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        Rec(0xA3B0, {r.ecx, r.args[0]});
        return r.ecx == At(kPools + 0x80 * g_containsWhich) ? 1u : 0u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Rec(0xA400, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1CC0, {r.args[0]});
        return At(kPartial);
    });
    s.At({0x00481650, AddressOf(&Gfx::CAnimTimeline::Construct)}, 0, [](Regs& r) {
        Rec(0x1650, {r.ecx});
        *reinterpret_cast<void***>(r.ecx) = g_timelineVtable;
        return r.ecx;
    });
    g_timelineVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x0700, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_timelineVtable[2] = s.Thunk(0, [](Regs& r) {
        Rec(0x0702, {r.ecx, *reinterpret_cast<uint32_t*>(r.ecx + 0x18), *reinterpret_cast<uint32_t*>(r.ecx + 0x20)});
        return 0u;
    });
    g_timelineVtable[3] = s.Thunk(0x18, [](Regs& r) {
        Rec(0x0703, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0u;
    });
}

void SetUpWorld()
{
    g_sources = g_frames = g_indexes = g_clockCalls = 0;
    g_step = 3.5f;
    g_noSkeleton = false;
    g_containsWhich = 2;
    auto* t = Ptr<CAnimTimeline>(kTimeline);
    t->m_vtable = g_timelineVtable;
    t->m_start = 2.0f;
    t->m_current = 5.25f;
    t->m_end = 40.0f;
    t->m_speed = 1.0f / 60.0f;
    t->m_anim = 0x1111;
    *Ptr<uint32_t>(kTimeline + 0x64) = At(kObject);
    auto* p = Ptr<CAnimTimeline>(kPartial);
    std::memcpy(p, t, sizeof(*p));
    p->m_current = 11.0f;
    p->m_speed = 0.5f;
    p->m_direction = 0;
    *Ptr<uint32_t>(kSkelComp + 0x18) = At(kSkeleton);
    *Ptr<uint32_t>(kSkeleton + 8) = At(kBones);
    *Ptr<uint32_t>(kSkeleton + 0xC) = 33;
    for (int i = 0; i < 16; ++i)
        *Ptr<uint32_t>(kFrames + 0x40 * i + 0x24) = i % 3 == 1 ? 0 : 5;
    *Ptr<uint32_t>(kScript + 0x14) = Params(15);
    GameVariable<uint32_t>(0x007D1800) = At(kManager);
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<int>(0x00696D60) = 1000;
    for (int i = 0; i < 0x840 / 4; ++i)
        *Ptr<uint32_t>(kCtrl + 0x14 + 4 * i) = 0x3F000000u + 0x1357u * i;

    g_answers.clear();
    // 0: PlayAnim's parameters
    Answer('K', Params(0), 0x6C2BFB7F, {0x00AA0001});
    Answer('F', Params(0), 0xD16B61E6, {Bits(1.25f)});
    Answer('F', Params(0), 0xAB81BB3D, {Bits(33.5f)});
    Answer('I', Params(0), 0xCD8D7C1B, {3});
    Answer('F', Params(0), 0x8F0D24ED, {Bits(0.3f)});
    Answer('F', Params(0), 0xF0D90109, {Bits(1.75f)});
    // 1: Anim (a new frame source); the weighted animation's name, weight; the partial step / speed
    Answer('K', Params(1), 0x98549BA4, {0x00BB0002});
    Answer('K', Params(1), 0xD697A799, {0x00CC0003});
    Answer('F', Params(1), 0xC6AE16AC, {Bits(0.625f)});
    Answer('F', Params(1), 0x06C9F278, {Bits(4.5f)});
    Answer('F', Params(1), 0xF0D90109, {Bits(-2.0f)});
    // 2: AnimName only (weighted), its weight missing; the fraction below 0
    Answer('K', Params(2), 0x6C2BFB7F, {0x00DD0004});
    Answer('F', Params(2), 0x4D747FA0, {Bits(-0.5f)});
    // 3: nothing (weighted's names absent: no new frame); fraction in range; SetWobbleDetails all five
    Answer('F', Params(3), 0x4D747FA0, {Bits(0.375f)});
    Answer('F', Params(3), 0xFD266A26, {Bits(2.5f)});
    Answer('F', Params(3), 0x642F3B9C, {Bits(1.5f)});
    Answer('F', Params(3), 0x0F43FD49, {Bits(0.00037f)});
    Answer('F', Params(3), 0x964AACF3, {Bits(0.0021f)});
    Answer('F', Params(3), 0xF90B0824, {Bits(3.0f)});
    Answer('F', Params(3), 0xC6AE16AC, {Bits(0.25f)});
    // 4: fraction above 1; four of the five wobble details
    Answer('F', Params(4), 0x4D747FA0, {Bits(1.5f)});
    Answer('F', Params(4), 0xFD266A26, {Bits(9.0f)});
    Answer('F', Params(4), 0x642F3B9C, {Bits(9.0f)});
    Answer('F', Params(4), 0x0F43FD49, {Bits(9.0f)});
    Answer('F', Params(4), 0x964AACF3, {Bits(9.0f)});
    // 5: the partial controller's InitFromStructure: AnimName, Speed, Current
    Answer('K', Params(5), 0x6C2BFB7F, {0x00EE0005});
    Answer('F', Params(5), 0xF0D90109, {Bits(0.8f)});
    Answer('F', Params(5), 0x230CCBF4, {Bits(6.5f)});
    Answer('K', Params(5), 0x40C698AF, {0x1D1D});
    // 6: InitFromStructure without Speed / Current
    Answer('K', Params(6), 0x6C2BFB7F, {0x00FF0006});
}

std::vector<Saved> g_saved;
void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_saved);
}

template <typename T>
struct Same {
    using type = T;
};
template <typename R, typename T, typename... A>
R CallBoth(bool original, uint32_t address, T* self, R (T::*ours)(A...), typename Same<A>::type... args)
{
    if (original)
        return Original<R(__thiscall*)(T*, A...)>(address)(self, args...);
    return (self->*ours)(args...);
}

void TestAnimControllers()
{
    g_saved = {{0x00701430, 16, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x00696D60, 4, {}}};
    SaveAll(g_saved);
    {
        StandIns s;
        InstallStandIns(s);
        auto params = [](int n) { return reinterpret_cast<CStruct*>(Params(n)); };
        auto script = []() { return reinterpret_cast<Script::CScript*>(At(kScript)); };
        uint8_t* pose = nullptr;
        auto mark = [](uint32_t n) { Snapshot(kCtrl - 0x100, kEnd - (kCtrl - 0x100), n); };

        // --- the standard controller ---
        RunBoth("anim controllers: standard", [&](bool original) {
            BeginRun(original, kCtrl, 0x60);
            SetUpWorld();
            pose = Ptr<uint8_t>(kPose);
            auto* c = Ptr<CStandardAnimController>(kCtrl);
            auto* t = Ptr<CAnimTimeline>(kTimeline);
            Rec(0x100, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A4500, c, &CStandardAnimController::Construct, t))});
            mark(1);
            // GetPose: no index yet, the frame null... (the constructor leaves the frame null: give it one)
            for (int i = 0; i < 4; ++i) {
                c->mp_frameData = Ptr<void>(kFrames + 0x40 * (i % 3));
                c->m_frameIndex = i == 3 ? 12 : -1;
                g_noSkeleton = false;
                *Ptr<uint32_t>(kSkeleton + 8) = i == 2 ? 0 : At(kBones);
                Rec(0x101, {static_cast<uint32_t>(CallBoth(original, 0x004A4F70, c, &CStandardAnimController::GetPose, pose))});
            }
            mark(2);
            // Update: forwards, backwards (refetch), equal, NaN
            for (float step : {9.0f, 1.0f, 1.0f, std::nanf("")}) {
                g_step = step;
                CallBoth(original, 0x004A4620, c, &CStandardAnimController::Update);
            }
            mark(3);
            // Member functions
            const uint32_t names[] = {0xAF2FAE19, 0xC68365EB, 0x92267217, 0x5F495AE0, 0x12345678, 0xC68365EB};
            int n = 0;
            for (uint32_t name : names) {
                if (n == 5)
                    c->mp_frameData = nullptr, c->mp_source = nullptr;
                Rec(0x102, {static_cast<uint32_t>(CallBoth(original, 0x004A4670, c, &CStandardAnimController::CallMemberFunction,
                                                            name, params(n == 1 || n == 5 ? 1 : 0), script()))});
                ++n;
                mark(10 + n);
            }
            // The destructors: with a frame and a source, without.
            CallBoth(original, 0x004A4590, c, &CStandardAnimController::DestroyBody);
            mark(4);
            CallBoth(original, 0x004A4590, c, &CStandardAnimController::DestroyBody);
            mark(5);
            // Destroy (0 then 1: back to the pools - a pool that holds it, the other one, none)
            auto& pools = GameVariable<Mem::PoolPair>(0x00701430);
            pools.current = 1;
            pools.pools[0] = Ptr<Mem::Pool>(kPools);
            pools.pools[1] = Ptr<Mem::Pool>(kPools + 0x80);
            for (uint32_t which : {2u, 1u, 0u}) {
                g_containsWhich = which;
                c->m_vtable = nullptr;
                c->mp_frameData = Ptr<void>(kFrames);
                c->mp_source = Ptr<void>(kSources);
                Rec(0x103, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A55E0, c, &CStandardAnimController::Destroy,
                                                                static_cast<uint8_t>(which == 2 ? 0 : 1)))});
                mark(20 + which);
            }
            // Free with no pools at all; operator new from a pool, then from the manager.
            pools.pools[0] = pools.pools[1] = nullptr;
            if (original)
                Original<void(__cdecl*)(void*)>(0x004A5600)(Ptr<void>(kItems));
            else
                CStandardAnimController::operator delete(Ptr<void>(kItems));
            pools.current = 0;
            pools.pools[0] = Ptr<Mem::Pool>(kPools);
            for (int i = 0; i < 2; ++i) {
                void* item = original ? Original<void*(__cdecl*)(size_t, int)>(0x004A4F30)(0x28, 0)
                                      : CStandardAnimController::operator new(0x28, 0);
                Rec(0x104, {reinterpret_cast<uint32_t>(item)});
                pools.pools[0] = nullptr;
                GameVariable<bool>(0x007CC979) = i == 0 ? false : true;
            }
            mark(6);
        });

        // --- the weighted controller ---
        RunBoth("anim controllers: weighted", [&](bool original) {
            BeginRun(original, kCtrl, 0x60);
            SetUpWorld();
            pose = Ptr<uint8_t>(kPose);
            auto* c = Ptr<CWeightedAnimController>(kCtrl);
            auto* t = Ptr<CAnimTimeline>(kTimeline);
            auto* object = Ptr<Obj::CCompositeObject>(kObject + 0x40);
            // with a timeline (the channel's object), then without (the object given)
            Rec(0x200, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A5010, c, &CWeightedAnimController::Construct,
                                                            static_cast<CAnimTimeline*>(nullptr), object))});
            mark(1);
            Rec(0x200, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A5010, c, &CWeightedAnimController::Construct, t,
                                                            object))});
            mark(2);
            // GetPose: no frame; a weight of 0, below, NaN, above; no source; no skeleton's bones
            const float weights[] = {0.5f, 0.0f, -1.0f, std::nanf(""), 0.75f, 0.75f};
            for (int i = 0; i < 6; ++i) {
                c->mp_frameData = i == 0 ? nullptr : Ptr<void>(kFrames + 0x40 * i);
                c->mp_source = i == 4 ? nullptr : Ptr<void>(kSources + 0x300);
                c->m_weight = weights[i];
                *Ptr<uint32_t>(kSkeleton + 8) = i == 5 ? 0 : At(kBones);
                Rec(0x201, {static_cast<uint32_t>(CallBoth(original, 0x004A5660, c, &CWeightedAnimController::GetPose, pose))});
                mark(10 + i);
            }
            const uint32_t names[] = {0xAF2FAE19, 0xAF2FAE19, 0x5E3F0F17, 0x5E3F0F17, 0x5E3F0F17,
                                      0xC68365EB, 0xC68365EB, 0x5F495AE0, 0x00000001};
            const int which[] = {1, 2, 1, 2, 3, 1, 1, 0, 0};
            for (int i = 0; i < 9; ++i) {
                if (i == 6)
                    c->mp_source = nullptr;
                Rec(0x202, {static_cast<uint32_t>(CallBoth(original, 0x004A4890, c, &CWeightedAnimController::CallMemberFunction,
                                                            names[i], params(which[i]), script()))});
                mark(20 + i);
            }
            CallBoth(original, 0x004A4820, c, &CWeightedAnimController::DestroyBody);
            mark(3);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
                c->mp_source = flags ? nullptr : Ptr<void>(kSources);
                Rec(0x203, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A50B0, c, &CWeightedAnimController::Destroy, flags))});
                mark(4u + flags);
            }
        });

        // --- the wobble controller ---
        RunBoth("anim controllers: wobble", [&](bool original) {
            BeginRun(original, kCtrl, 0x60);
            SetUpWorld();
            pose = Ptr<uint8_t>(kPose);
            auto* c = Ptr<CWobbleController>(kCtrl);
            auto* t = Ptr<CAnimTimeline>(kTimeline);
            Rec(0x300, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A49F0, c, &CWobbleController::Construct, t))});
            mark(1);
            const uint32_t names[] = {0xAF2FAE19, 0xD0209498, 0xD0209498, 0xD0209498, 0xD0209498,
                                      0xEA6D0EFD, 0xEA6D0EFD, 0xEA6D0EFD, 0x5F495AE0, 0x77777777};
            const int which[] = {0, 2, 4, 1, 3, 4, 0, 3, 0, 0};
            for (int i = 0; i < 10; ++i) {
                Rec(0x302, {static_cast<uint32_t>(CallBoth(original, 0x004A4AF0, c, &CWobbleController::CallMemberFunction,
                                                            names[i], params(which[i]), script()))});
                mark(20 + i);
            }
            for (int i = 0; i < 2; ++i) {
                *Ptr<uint32_t>(kSkeleton + 8) = i ? 0 : At(kBones);
                Rec(0x301, {static_cast<uint32_t>(CallBoth(original, 0x004A5110, c, &CWobbleController::GetPose, pose))});
            }
            mark(2);
            // WobbleTime: targets near the start, the end and the middle; amplitudes that clamp; other spaz
            // factors; the timeline's current before / after.
            const float targets[] = {3.0f, 21.0f, 39.5f, 2.0f, 30.0f, 12.0f};
            const float amps[] = {2.5f, 0.5f, 6.0f, 25.0f, 1.0f, 0.0f};
            for (int i = 0; i < 6; ++i) {
                c->m_target = targets[i];
                c->m_ampA = amps[i];
                c->m_ampB = amps[5 - i] + 0.25f;
                c->m_spazFactor = 1.0f + 0.7f * i;
                c->m_k1 = 0.00037f * (i + 1);
                c->m_k2 = 0.0021f / (i + 1);
                float r = original ? Original<float(__thiscall*)(void*)>(0x004A5180)(c) : c->WobbleTime();
                Rec(0x304, {Bits(r)});
                t->m_current = i % 2 ? 50.0f : 0.0f;
                CallBoth(original, 0x004A52E0, c, &CWobbleController::Update);
                mark(30 + i);
            }
            // spazFactor 1 (amplitudes as given): a small ampA with a large ampB near the start (only the
            // full sum clamps), a negative ampB near and past the end (the result clamped to the timeline).
            const float more[][3] = {{2.5f, 0.25f, 1.5f}, {39.0f, 1.0f, -3.0f}, {41.5f, 0.5f, -6.0f},
                                     {1.0f, -2.0f, 0.5f}, {20.0f, 0.0f, -25.0f}};
            for (int i = 0; i < 5; ++i) {
                c->m_target = more[i][0];
                c->m_ampA = more[i][1];
                c->m_ampB = more[i][2];
                c->m_spazFactor = 1.0f;
                for (int k = 0; k < 4; ++k) {
                    c->m_k1 = 0.00011f * (k + 1) + 0.0003f * i;
                    c->m_k2 = 0.00173f * (k + 2);
                    float r = original ? Original<float(__thiscall*)(void*)>(0x004A5180)(c) : c->WobbleTime();
                    Rec(0x305, {Bits(r)});
                }
            }
            CallBoth(original, 0x004A4A80, c, &CWobbleController::DestroyBody);
            mark(3);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
                c->mp_source = flags ? nullptr : Ptr<void>(kSources);
                c->mp_frameData = flags ? Ptr<void>(kFrames) : nullptr;
                Rec(0x303, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A50E0, c, &CWobbleController::Destroy, flags))});
                mark(4u + flags);
            }
        });

        // --- the pose controller ---
        RunBoth("anim controllers: pose", [&](bool original) {
            BeginRun(original, kCtrl, 0x60);
            SetUpWorld();
            pose = Ptr<uint8_t>(kPose);
            auto* c = Ptr<CPoseController>(kCtrl);
            auto* t = Ptr<CAnimTimeline>(kTimeline);
            Rec(0x400, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A5730, c, &CPoseController::Construct, t))});
            mark(1);
            for (int i = 0; i < 0x840 / 4; ++i)
                *Ptr<uint32_t>(kCtrl + 0x14 + 4 * i) ^= 0x00100000u * (i & 7);
            Rec(0x401, {static_cast<uint32_t>(CallBoth(original, 0x004A4D30, c, &CPoseController::GetPose, pose))});
            mark(2);
            CallBoth(original, 0x004A4D20, c, &CPoseController::DestroyBody);
            mark(3);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
                Rec(0x402, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A5320, c, &CPoseController::Destroy, flags))});
                mark(4u + flags);
            }
        });

        // --- the partial animation controller ---
        RunBoth("anim controllers: partial", [&](bool original) {
            BeginRun(original, kCtrl, 0x60);
            SetUpWorld();
            pose = Ptr<uint8_t>(kPose);
            auto* c = Ptr<CPartialAnimController>(kCtrl);
            auto* t = Ptr<CAnimTimeline>(kTimeline);
            Rec(0x500, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A5350, c, &CPartialAnimController::Construct, t))});
            mark(1);
            // (the stand-in Calloc's timeline: the copy made by SetUpWorld, with the stand-in vtable)
            *Ptr<void**>(kPartial) = g_timelineVtable;
            CallBoth(original, 0x004A4DD0, c, &CPartialAnimController::InitFromStructure, params(5));
            mark(2);
            CallBoth(original, 0x004A4DD0, c, &CPartialAnimController::InitFromStructure, params(6));
            mark(3);
            for (int i = 0; i < 2; ++i) {
                *Ptr<uint32_t>(kSkeleton + 8) = i ? 0 : At(kBones);
                Rec(0x501, {static_cast<uint32_t>(CallBoth(original, 0x004A5430, c, &CPartialAnimController::GetPose, pose))});
            }
            mark(4);
            const uint32_t names[] = {0x6AAEB76F, 0xBD4EDD44, 0xD63A1B81, 0xF5E2B871, 0xF5E2B871, 0xD63A1B81,
                                      0x5F495AE0, 0x00000002};
            for (int i = 0; i < 8; ++i) {
                if (i == 5)
                    Ptr<CAnimTimeline>(kPartial)->m_current = 40.0f;
                Rec(0x502, {static_cast<uint32_t>(CallBoth(original, 0x004A54A0, c, &CPartialAnimController::CallMemberFunction,
                                                            names[i], params(1), script()))});
                mark(10 + i);
            }
            for (float step : {20.0f, 3.0f, std::nanf("")}) {
                g_step = step;
                CallBoth(original, 0x004A4EF0, c, &CPartialAnimController::Update);
            }
            mark(5);
            // The base InitFromStructure jump.
            if (original)
                Original<void(__thiscall*)(void*, CStruct*)>(0x004A4610)(c, params(5));
            else
                c->BaseInitFromStructure(params(5));
            mark(6);
            CallBoth(original, 0x004A4D50, c, &CPartialAnimController::DestroyBody);
            mark(7);
            CallBoth(original, 0x004A4D50, c, &CPartialAnimController::DestroyBody);
            mark(8);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
                c->mp_partial = Ptr<CAnimTimeline>(kPartial);
                c->mp_frameData = flags ? Ptr<void>(kFrames) : nullptr;
                Rec(0x503, {reinterpret_cast<uint32_t>(CallBoth(original, 0x004A5400, c, &CPartialAnimController::Destroy, flags))});
                mark(20u + flags);
            }
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestAnimControllers, Cleanup);
