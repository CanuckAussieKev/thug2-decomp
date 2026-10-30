// Self-tests for the movie manager (src/game/obj/movie_manager.cpp): the original and ours run the same queue
// operations on an identical manager in an arena, with stand-in movies (a vtable of recording code; their flags
// scripted per movie) made by stand-ins for the three movie constructors; what the manager calls outside (the list's
// GetItem, the movie's name and calls, the asset manager, the skater's info, the camera and CObject pieces, the
// render frame, the game's camera, scripts, structures, memory, debug output) is recorded (viewer_testkit.h), and
// the calls and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/gfx/camera.h"
#include "game/obj/anim_movies.h"
#include "game/obj/cutscene.h"
#include "game/obj/movie_manager.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/sys/timer.h"
#include "game/gfx/viewport.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/goals/goal_manager.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CMovie;
using Obj::CMovieManager;

// Arena layout.
constexpr uint32_t kManager = 0x0000; // 0x12C
constexpr uint32_t kAssets = 0x0200;  // assets, 0x10 apart: vtable +0, the kind +4
constexpr uint32_t kSkaterInfo = 0x0300;
constexpr uint32_t kCameras = 0x0400; // cameras for the movies, 0xD0 apart
constexpr uint32_t kOther = 0x1000;   // a second list / objects for the copies

// The stand-in movie's own fields (after the real ones).
constexpr uint32_t kName = 0x30;
constexpr uint32_t kLoads = 0x34;
constexpr uint32_t kDone = 0x35;
constexpr uint32_t kHasCamera = 0x36;
constexpr uint32_t kFlag28 = 0x37;
constexpr uint32_t kFlag18 = 0x38;
constexpr uint32_t kFlag24 = 0x39;
constexpr uint32_t kHandles = 0x3A;

void* g_movieVtable[14];
void* g_assetVtable[1];
uint32_t g_assets[8]; // name -> asset offset, by a small table
uint64_t g_renderFrame;
int g_moviesMade;
// What the next movie made will answer.
uint8_t g_nextFlags[8];

uint8_t& Flag(uint32_t movie, uint32_t offset) { return *reinterpret_cast<uint8_t*>(movie + offset); }

uint64_t __cdecl RenderFrameStandIn()
{
    Rec(0xE8D0, {});
    return g_renderFrame;
}

// A movie made: the stand-in vtable, a camera, and the scripted flags.
uint32_t MakeMovie(uint32_t memory, uint32_t kind)
{
    auto* movie = reinterpret_cast<CMovie*>(memory);
    movie->m_node.vtable = g_movieVtable;
    movie->m_node.data = movie;
    movie->m_node.next = movie->m_node.prev = &movie->m_node;
    movie->m_camera = Ptr<Gfx::Camera>(kCameras + 0xD0 * (g_moviesMade % 6));
    movie->m_skippable = (g_moviesMade % 3) != 1;
    movie->m_playsWhilePaused = g_moviesMade % 2;
    movie->m_endScript = g_moviesMade % 4 == 2 ? 0u : 0xE5D00000u + g_moviesMade;
    movie->m_endParams = Ptr<CStruct>(0x0F00 + 8 * (g_moviesMade % 4));
    *reinterpret_cast<uint32_t*>(memory + 0x2C) = kind;
    std::memcpy(reinterpret_cast<void*>(memory + kLoads), g_nextFlags, sizeof(g_nextFlags));
    ++g_moviesMade;
    return memory;
}

void Install(StandIns& s)
{
    InstallCommon(s);
    for (uint32_t address : {0x005BE8D0u, AddressOf(&Tmr::GetRenderFrame)})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&RenderFrameStandIn)));
    s.At({0x0055D990, AddressOf(&Game::GoalListHead::NodeAt)}, 4, [](Regs& r) {
        Rec(0xD990, {r.ecx, r.args[0]});
        auto* head = reinterpret_cast<Mem::ListNode*>(r.ecx);
        int index = static_cast<int>(r.args[0]);
        for (Mem::ListNode* node = head->next; node != head; node = node->next) {
            if (index-- == 0)
                return reinterpret_cast<uint32_t>(node);
        }
        return 0u;
    });
    s.At({0x00543150, AddressOf(&Obj::MovieName)}, 0, [](Regs& r) {
        Rec(0x3150, {r.ecx});
        return *reinterpret_cast<uint32_t*>(r.ecx + kName);
    });
    s.At({0x00468F60, AddressOf(&Obj::CMovieDetails::SetValue20)}, 4, [](Regs& r) {
        Rec(0x8F60, {r.ecx, r.args[0]});
        *reinterpret_cast<uint32_t*>(r.ecx + kName) = r.args[0];
        return 0u;
    });
    for (auto [address, ours] : {std::pair<uint32_t, uint32_t>{0x00468F40u, AddressOf(&Obj::CMovieDetails::SetFlag19)},
                                 std::pair<uint32_t, uint32_t>{0x00468F50u, AddressOf(&Obj::CMovieDetails::SetFlag1B)}})
        s.At({address, ours}, 4, [address](Regs& r) {
            Rec(address & 0xFFFF, {r.ecx, r.args[0]});
            return 0u;
        });
    s.At({0x00469180, AddressOf(&Obj::CMovieDetails::GetSettings)}, 0, [](Regs& r) {
        Rec(0x9180, {r.ecx});
        return 0x700000u + (r.ecx & 0xFFF);
    });
    s.At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) {
        Rec(0x7B90, {r.ecx, r.args[0], r.args[1]});
        uint32_t asset = g_assets[r.args[0] & 7];
        return asset ? At(asset) : 0u;
    });
    s.At({0x00564FC0, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Rec(0x4FC0, {r.ecx, r.args[0]});
        return At(kSkaterInfo);
    });
    s.At({0x00466490, AddressOf(&Obj::CCutsceneDetails::Construct)}, 4, [](Regs& r) {
        const auto* context = reinterpret_cast<const uint32_t*>(r.args[0]);
        Rec(0x6490, {r.ecx, context[0], context[1], context[2] & 0xFF});
        return MakeMovie(r.ecx, 9);
    });
    s.At({0x005871B0, AddressOf(&Obj::CObjectAnimMovie::Construct)}, 0, [](Regs& r) {
        Rec(0x71B0, {r.ecx});
        return MakeMovie(r.ecx, 5);
    });
    s.At({0x005873F0, AddressOf(&Obj::CCameraAnimMovie::Construct)}, 0, [](Regs& r) {
        Rec(0x73F0, {r.ecx});
        return MakeMovie(r.ecx, 0xC0);
    });
    s.At({0x004A0970, AddressOf(Nx::SetCamera)}, 0, [](Regs& r) {
        Rec(0x0970, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00485BA0, AddressOf(&Gfx::Camera::Construct)}, 0, [](Regs& r) {
        Rec(0x5BA0, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x11, 0xC4);
        return r.ecx;
    });
    s.At({0x004859F0, AddressOf(&Gfx::Camera::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x59F0, {r.ecx});
        return 0u;
    });
    s.At({0x00460B80, AddressOf(&Obj::CObject::Construct)}, 0, [](Regs& r) {
        Rec(0x0B80, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x22, 0x4C);
        return r.ecx;
    });
    s.At({0x004615D0, AddressOf(&Obj::CObject::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x15D0, {r.ecx});
        return 0u;
    });
    s.At({0x004608F0, AddressOf(static_cast<bool (Obj::CObject::*)(uint32_t, CStruct*, Script::CScript*)>(
                          &Obj::CObject::CallMemberFunction))},
         12, [](Regs& r) {
             Rec(0x08F0, {r.ecx, r.args[0], r.args[1], r.args[2]});
             return 1u;
         });
    // The movie's vtable.
    g_movieVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0xD00, {r.ecx, r.args[0]});
        auto* node = reinterpret_cast<Mem::ListNode*>(r.ecx);
        if (!InArena(reinterpret_cast<uint32_t>(node->prev)) || !InArena(reinterpret_cast<uint32_t>(node->next))) {
            Rec(0xBAD, {reinterpret_cast<uint32_t>(node->prev), reinterpret_cast<uint32_t>(node->next)});
            Log("  movie manager: a movie deleted with bad links (%p)", node);
            return r.ecx;
        }
        node->prev->next = node->next; // (a node unlinks itself when it goes)
        node->next->prev = node->prev;
        node->next = node->prev = node;
        Flag(r.ecx, kName + 3) = 0xDE;
        return r.ecx;
    });
    struct Slot {
        int index;
        uint16_t pop;
        uint32_t flag; // the flag answered (0: none)
    };
    for (Slot slot : {Slot{1, 4, kLoads}, Slot{2, 0, 0}, Slot{3, 0, 0}, Slot{4, 12, kHandles}, Slot{5, 0, kDone},
                      Slot{6, 0, kFlag18}, Slot{7, 0, kHasCamera}, Slot{8, 0, 0}, Slot{9, 0, kFlag24},
                      Slot{10, 0, kFlag28}, Slot{11, 0, 0}, Slot{12, 4, 0}, Slot{13, 0, 0}}) {
        g_movieVtable[slot.index] = s.Thunk(slot.pop, [slot](Regs& r) {
            Rec(0xD00 + slot.index, {r.ecx, slot.pop ? r.args[0] : 0u, slot.pop > 4 ? r.args[1] : 0u,
                                     slot.pop > 8 ? r.args[2] : 0u});
            return slot.flag ? static_cast<uint32_t>(Flag(r.ecx, slot.flag)) : 0u;
        });
    }
    g_assetVtable[0] = s.Thunk(0, [](Regs& r) {
        Rec(0xA55E, {r.ecx});
        return *reinterpret_cast<uint32_t*>(r.ecx + 4);
    });
}

void PushQueue(std::vector<uint32_t>& t)
{
    auto* manager = Ptr<CMovieManager>(kManager);
    int guard = 0;
    for (Mem::ListNode* node = manager->m_movies.next; node != &manager->m_movies && guard < 32; node = node->next, ++guard)
        t.insert(t.end(), {Rel(reinterpret_cast<uint32_t>(node)), *reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t>(node) + kName)});
    t.push_back(0xE0D);
}

// Movies added of every type (assets missing, of both kinds, a movie of that name dropped first; a cutscene; a skater
// camera clearing the queue; one that fails to load), the queries by index and name, the kept camera (paused, the
// skate module's state, no camera, the frames running out), each call by name and the playing movie's, Update
// (paused or not, the view mode, a movie playing while paused, done), ending movies (skippable chains), killing
// (the playing one and another), stopping, clearing, the member function, the list helpers, the copies, and the
// destructors.
void TestMovieManager()
{
    SaveGlobals({{0x006FA7D4, 1, {}}, {0x007CE478, 4, {}}, {0x007CE4A4, 4, {}}, {0x006918DC, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_moviesMade = 0;
        g_renderFrame = 0x100;
        g_printfArgs = 2;
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CE478) = At(0x0E00); // the skate module (+0x378 its state)
        GameVariable<uint32_t>(0x006918DC) = At(0x0E80); // the asset manager
        GameVariable<bool>(0x006FA7D4) = false;
        GameVariable<int>(0x007CE4A4) = 0;
        for (int a = 0; a < 3; ++a) {
            *Ptr<void**>(kAssets + 0x10 * a) = g_assetVtable;
            *Ptr<uint32_t>(kAssets + 0x10 * a + 4) = a == 0 ? 5u : (a == 1 ? 9u : 7u);
        }
        std::memset(g_assets, 0, sizeof(g_assets));
        g_assets[1] = kAssets;        // names ending in 1: a skater camera asset
        g_assets[2] = kAssets + 0x10; // in 2: an object camera
        g_assets[3] = kAssets + 0x20; // in 3: neither (kind 7: no movie - not used)
        auto* m = Ptr<CMovieManager>(kManager);
        std::memset(m, 0xCD, sizeof(CMovieManager));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057A4B0)(m) : m->Construct())));
        auto add = [&](uint32_t name, uint32_t type, std::initializer_list<uint8_t> flags) {
            std::memset(g_nextFlags, 0, sizeof(g_nextFlags));
            std::copy(flags.begin(), flags.end(), g_nextFlags);
            CStruct* params = Ptr<CStruct>(0x0F40 + (name & 0x30));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, CStruct*, uint32_t)>(0x0057AB60)(m, name, params, type)
                                 : m->AddMovie(name, params, type));
            t.push_back(m->m_keepCamera);
            PushQueue(t);
        };
        auto count = [&]() {
            t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x0057A330)(m) : m->Count()));
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x0057B0B0)(m) : m->AnyMovies());
            t.push_back(original ? Original<uint32_t(__thiscall*)(void*)>(0x0057B030)(m) : m->GetPlayingName());
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057A530)(m) : m->GetCamera())));
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x0057B070)(m) : m->PlayingFlag24());
        };
        // Empty.
        count();
        for (uint32_t name : {0u, 0x71u}) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x0057A940)(m, name, 5)
                                 : m->MovieCall468F40(name, 5));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x0057A9B0)(m, name, 6)
                                 : m->MovieCall468F50(name, 6));
        }
        t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057AA20)(m, 0x71) : m->IsMovieGone(0x71));
        t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057AA70)(m, 0x71) : m->MovieFlag18(0x71));
        t.push_back(original ? Original<uint32_t(__thiscall*)(void*, uint32_t)>(0x0057AF60)(m, 0x71) : m->MovieValue(0x71));
        t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x0057AE60)(m) : m->Update());
        // Adding: flags {loads, done, has camera, flag 0x28, flag 0x18, flag 0x24, handles}.
        add(0x40, 0x66B7DD11, {1, 0, 1, 1, 0, 0, 1});       // a cutscene (the first: the game's camera)
        add(0x51, 0xB46E218F, {1, 0, 0, 1, 1, 1, 0});       // a skater camera asset (kind 5)
        add(0x62, 0x12743EDB, {1, 1, 1, 0, 0, 0, 1});       // an object camera asset (kind 9)
        add(0x55, 0x12743EDB, {1, 0, 1, 1, 1, 0, 0});       // missing asset? (5 -> g_assets[5] none)
        add(0x71, 0xB46E218F, {0, 0, 1, 1, 1, 1, 1});       // fails to load
        add(0x81, 0xB46E218F, {1, 0, 1, 0, 0, 1, 0});
        add(0x51, 0xB46E218F, {1, 0, 1, 1, 0, 0, 0});       // the same name again: the old one dropped
        m->m_keepCamera = true;
        add(0x99, 0x66B7DD11, {0, 0, 0, 0, 0, 0, 0});       // fails: the kept camera flag put back
        count();
        for (int i = -1; i < 7; ++i)
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, int)>(0x0057A360)(m, i)
                                                               : m->GetMovie(i))));
        for (uint32_t name : {0x40u, 0x51u, 0x62u, 0x81u, 0x99u})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, uint32_t)>(0x0057A3A0)(m, name)
                                                               : m->FindMovie(name))));
        for (uint32_t name : {0u, 0x62u, 0x99u}) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x0057A940)(m, name, 7)
                                 : m->MovieCall468F40(name, 7));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x0057A9B0)(m, name, 8)
                                 : m->MovieCall468F50(name, 8));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057AA20)(m, name) : m->IsMovieGone(name));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057AA70)(m, name) : m->MovieFlag18(name));
            t.push_back(Rel(original ? Original<uint32_t(__thiscall*)(void*, uint32_t)>(0x0057AF60)(m, name) : m->MovieValue(name)));
        }
        // The playing movie's calls.
        auto voidCall = [&](uint32_t address, void (CMovieManager::*ours)()) {
            if (original)
                Original<void(__thiscall*)(void*)>(address)(m);
            else
                (m->*ours)();
        };
        voidCall(0x0057B0E0, &CMovieManager::Playing2C);
        if (original)
            Original<void(__thiscall*)(void*, uint32_t)>(0x0057B120)(m, 0x30A1);
        else
            m->Playing30(0x30A1);
        voidCall(0x0057B160, &CMovieManager::Playing34);
        voidCall(0x0057A180, &CMovieManager::UseKeptCamera);
        for (int handles : {0, 1}) {
            Flag(reinterpret_cast<uint32_t>(m->m_movies.next), kHandles) = static_cast<uint8_t>(handles);
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, CStruct*, Script::CScript*)>(0x0057AFC0)(
                                       m, 0x3E3B3E3B, Ptr<CStruct>(0x0F00), Ptr<Script::CScript>(0x0F80))
                                 : m->CallMemberFunction(0x3E3B3E3B, Ptr<CStruct>(0x0F00), Ptr<Script::CScript>(0x0F80)));
        }
        // The kept camera.
        auto keep = [&](uint32_t movie) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t)>(0x0057A450)(m, movie);
            else
                m->KeepCamera(reinterpret_cast<CMovie*>(movie));
            t.push_back(m->m_keepCamera);
            t.push_back(m->m_keepUntil);
        };
        uint32_t first = reinterpret_cast<uint32_t>(m->m_movies.next);
        keep(0);
        GameVariable<bool>(0x006FA7D4) = true;
        keep(first);
        GameVariable<bool>(0x006FA7D4) = false;
        *Ptr<uint32_t>(0x0E00 + 0x378) = 0xE92ECAFE;
        keep(first);
        *Ptr<uint32_t>(0x0E00 + 0x378) = 0;
        Flag(first, kFlag28) = 0;
        keep(first);
        Flag(first, kFlag28) = 1;
        for (int i = 0; i < 0x30; ++i)
            Ptr<uint32_t>(kCameras)[i] = 0xCA000000u + i * 3; // the first movie's camera
        keep(first);
        count();
        // Updates.
        const struct {
            bool paused;
            int viewMode;
            uint64_t frame;
        } frames[] = {{false, 0, 0x100}, {true, 0, 0x101}, {true, 1, 0x101}, {false, 0, 0x100000101ull}, {true, 0, 0x200}};
        for (const auto& f : frames) {
            GameVariable<bool>(0x006FA7D4) = f.paused;
            GameVariable<int>(0x007CE4A4) = f.viewMode;
            g_renderFrame = f.frame;
            if (f.frame == 0x200)
                Flag(reinterpret_cast<uint32_t>(m->m_movies.next), kDone) = 1;
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x0057AE60)(m) : m->Update());
            t.push_back(m->m_keepCamera);
            PushQueue(t);
        }
        GameVariable<bool>(0x006FA7D4) = false;
        // Ending and killing.
        add(0xA1, 0xB46E218F, {1, 0, 1, 1, 0, 0, 0});
        add(0xB1, 0xB46E218F, {1, 0, 0, 1, 0, 0, 0});
        add(0xC0, 0x66B7DD11, {1, 0, 1, 1, 0, 0, 0});
        for (bool skippable : {true, false}) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, bool)>(0x0057A7E0)(m, skippable) : m->EndMovies(skippable));
            PushQueue(t);
        }
        for (uint32_t name : {0x99u, 0xC0u, 0xB1u, 0xA1u}) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057AAC0)(m, name) : m->KillMovie(name));
            PushQueue(t);
        }
        add(0xD0, 0x66B7DD11, {1, 0, 1, 1, 0, 0, 0});
        add(0xE0, 0x66B7DD11, {1, 0, 1, 1, 0, 0, 0});
        voidCall(0x0057A5A0, &CMovieManager::StopAll);
        if (original)
            Original<void(__thiscall*)(void*, void*)>(0x0057A6B0)(m, m->m_movies.prev);
        else
            m->EndMovie(reinterpret_cast<CMovie*>(m->m_movies.prev));
        PushQueue(t);
        add(0xF0, 0x66B7DD11, {1, 0, 1, 1, 0, 0, 0});
        add(0xF5, 0xF5012F52, {1, 0, 1, 1, 0, 0, 0}); // a skater camera: the queue cleared first
        add(0xF6, 0xF5012F52, {1, 0, 0, 1, 0, 0, 0});
        voidCall(0x0057A640, &CMovieManager::ClearMovies);
        PushQueue(t);
        voidCall(0x0057A640, &CMovieManager::ClearMovies); // empty
        count();
        for (bool skippable : {true, false}) // (nothing to end)
            t.push_back(original ? Original<bool(__thiscall*)(void*, bool)>(0x0057A7E0)(m, skippable) : m->EndMovies(skippable));
        // One movie: the counts; done, an update ends it and leaves none.
        add(0xF7, 0x66B7DD11, {1, 1, 1, 1, 0, 0, 0});
        count();
        t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x0057AE60)(m) : m->Update());
        count();
        // The list helpers and the copies.
        auto* head = Ptr<Mem::ListNode>(kOther);
        Mem::ListNode* nodes[3];
        head->vtable = nullptr;
        head->data = reinterpret_cast<void*>(-1);
        head->next = head->prev = head;
        for (int i = 0; i < 3; ++i) {
            nodes[i] = Ptr<Mem::ListNode>(kOther + 0x40 + 0x40 * i);
            nodes[i]->vtable = g_movieVtable;
            nodes[i]->data = nodes[i];
            nodes[i]->prev = head->prev;
            nodes[i]->next = head;
            head->prev->next = nodes[i];
            head->prev = nodes[i];
        }
        for (int pass = 0; pass < 2; ++pass) {
            if (original)
                Original<void(__thiscall*)(void*)>(0x0057A2F0)(head);
            else
                reinterpret_cast<Obj::ListHead*>(head)->DeleteAllNodes();
        }
        auto* node = Ptr<Obj::MovieListNode>(kOther + 0x100);
        auto* other = Ptr<Obj::MovieListNode>(kOther + 0x120);
        node->m_node.next = node->m_node.prev = &other->m_node;
        other->m_node.next = other->m_node.prev = &node->m_node;
        if (original)
            Original<void(__thiscall*)(void*)>(0x0057A200)(node);
        else
            node->DestroyBody();
        for (unsigned int flags : {0u, 1u})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x0057A2C0)(other, flags)
                                                                : other->Destroy(flags))));
        for (int i = 0; i < 0x31; ++i)
            Ptr<uint32_t>(kOther + 0x200)[i] = 0x5EED0000u + i * 7;
        std::memset(Ptr<void>(kOther + 0x300), 0xAB, 0xC4);
        if (original)
            Original<void*(__thiscall*)(void*, void*)>(0x0057A190)(Ptr<void>(kOther + 0x300), Ptr<void>(kOther + 0x200));
        else
            Ptr<Obj::ObjectAssignment>(kOther + 0x300)->Assign(Ptr<Obj::CObject>(kOther + 0x200));
        std::memset(Ptr<void>(kOther + 0x400), 0xAB, 0xC4);
        t.push_back(Rel(reinterpret_cast<uint32_t>(
            original ? Original<void*(__thiscall*)(void*, void*)>(0x0057A220)(Ptr<void>(kOther + 0x400), Ptr<void>(kOther + 0x200))
                     : Ptr<Obj::CameraAssignment>(kOther + 0x400)->Assign(Ptr<Gfx::Camera>(kOther + 0x200)))));
        // The destructors.
        add(0x1A0, 0x66B7DD11, {1, 0, 1, 1, 0, 0, 0});
        voidCall(0x0057B1A0, &CMovieManager::DestroyBody);
        for (unsigned int flags : {0u, 1u})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x0057B220)(m, flags)
                                                                : m->Destroy(flags))));
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "movie manager");
}

} // namespace

SELFTEST(Early, TestMovieManager, ViewerTestKit::Cleanup);
