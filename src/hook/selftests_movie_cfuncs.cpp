// Self-tests for the movie script commands (0x005AD830-0x005AE36F): each original against ours with the skate
// module's two movie managers, the skaters, objects, the park editor, the asset manager and the renderer stood in
// for by recording code (over the game's function and ours alike); the destroy list entry's constructor on a
// stand-in structure pool.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/ed/park_editor.h"
#include "game/obj/movie_cfuncs.h"
#include "game/obj/movie_manager.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Obj::CMovieManager;

constexpr uint32_t kUseGameCamera = 0x0C567FA2;
constexpr uint32_t kSkaterId = 0x5B24FAAA;
constexpr uint32_t kSkater = 0x5B8AB877;
constexpr uint32_t kStop = 0x46A9E949;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kObjectAnim = 0x12743EDB;
constexpr uint32_t kSkippable = 0x8DDE72C0;
constexpr uint32_t kShouldPause = 0xB3ECC65E;
constexpr uint32_t kCurrent = 0x230CCBF4;
constexpr uint32_t kAll = 0xC4E78E22;
constexpr uint32_t kEdit = 0x7ECA21E5;
constexpr uint32_t kToggle = 0xDA155274;

alignas(16) uint8_t g_cameraMovies[0x130];
alignas(16) uint8_t g_objectMovies[0x130];
alignas(16) uint8_t g_skaters[3][0x330];
alignas(16) uint8_t g_editor[0x80];
std::map<uint32_t, uint32_t> g_answers; // a stood-in function's answer by address
Params g_animParams;

uint32_t Answer(uint32_t address)
{
    auto it = g_answers.find(address);
    return it == g_answers.end() ? 0u : it->second;
}

void StandInMovieCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    struct Method {
        uint32_t address;
        uint32_t ours;
        uint16_t pop;
    };
    const Method methods[] = {
        {0x0057A180, AddressOf(&CMovieManager::UseKeptCamera), 0},
        {0x0057A640, AddressOf(&CMovieManager::ClearMovies), 0},
        {0x0057AB60, AddressOf(&CMovieManager::AddMovie), 12},
        {0x0057A940, AddressOf(&CMovieManager::MovieCall468F40), 8},
        {0x0057A9B0, AddressOf(&CMovieManager::MovieCall468F50), 8},
        {0x0057B030, AddressOf(&CMovieManager::GetPlayingName), 0},
        {0x0057AF60, AddressOf(&CMovieManager::MovieValue), 4},
        {0x0057AA20, AddressOf(&CMovieManager::IsMovieGone), 4},
        {0x0057AA70, AddressOf(&CMovieManager::MovieFlag18), 4},
        {0x0057A7E0, AddressOf(&CMovieManager::EndMovies), 4},
        {0x0057AAC0, AddressOf(&CMovieManager::KillMovie), 4},
        {0x0057A330, AddressOf(&CMovieManager::Count), 0},
        {0x0057B070, AddressOf(&CMovieManager::PlayingFlag24), 0},
        {0x0057B0B0, AddressOf(&CMovieManager::AnyMovies), 0},
        {0x0057B0E0, AddressOf(&CMovieManager::Playing2C), 0},
        {0x0057B120, AddressOf(&CMovieManager::Playing30), 4},
        {0x0057B160, AddressOf(&CMovieManager::Playing34), 0},
    };
    for (const Method& m : methods) {
        uint32_t address = m.address;
        uint16_t pop = m.pop;
        s.At({m.address, m.ours}, m.pop, [address, pop](Regs& r) {
            std::vector<uint32_t> call = {address, r.ecx};
            for (int i = 0; i < pop / 4; ++i) // (bytes as bytes: the bool arguments' upper bytes aren't set)
                call.push_back(address == 0x0057B120 || (i == 1 && pop == 8) || (address == 0x0057A7E0)
                                   ? r.args[i] & 0xFF
                                   : r.args[i]);
            g_trace.insert(g_trace.end(), call.begin(), call.end());
            return Answer(address);
        });
    }
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return r.args[0] == 0x52 ? reinterpret_cast<uint32_t>(g_skaters[1]) : 0u;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return r.args[0] < 3 ? reinterpret_cast<uint32_t>(g_skaters[r.args[0]]) : 0u;
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return Answer(0x00566520);
    });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x0058DD70, AddressOf(&Ed::CParkEditor::SwitchCamera)}, 0, [](Regs&) {
        Record({0xDD70});
        return 0u;
    });
    s.At({0x00407EC0, AddressOf(&Ass::CAssMan::ReloadAsset)}, 12, [](Regs& r) {
        Record({0x7EC0, r.ecx, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2]});
        return Answer(0x00407EC0);
    });
    s.At({0x0049A8E0}, 0, [](Regs& r) {
        Record({0xA8E0, r.ecx});
        return Answer(0x0049A8E0);
    });
}

void TestMovieCFuncs()
{
    Setup();
    for (uint32_t address : {0x007CE5D8u, 0x006918DCu, 0x007CEFACu, 0x006FA7C0u}) {
        g_saved.push_back({address, 4, {}});
        g_saved.back().bytes.resize(4);
        std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(address), 4);
    }
    auto* standIns = new StandIns;
    StandInMovieCalls(*standIns);
    GameVariable<uint8_t*>(0x007CE5D8) = g_editor;
    GameVariable<uint32_t>(0x006918DC) = 0xA55E0000;
    GameVariable<uint32_t>(0x006FA7C0) = 0x6FA70000;
    if (!g_animParams.count)
        g_animParams.Integer(0x1111, 3);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        *reinterpret_cast<uint8_t**>(g_skate + 0x368) = g_cameraMovies;
        *reinterpret_cast<uint8_t**>(g_skate + 0x36C) = g_objectMovies;
        std::memset(g_skaters, 0, sizeof(g_skaters));
        g_skaters[0][0xBC] = 1;
        g_skaters[1][0xBC] = 1;
        std::memset(g_editor, 0, sizeof(g_editor));
        GameVariable<uint8_t>(0x007CEFAC) = 0;
        g_answers.clear();
        auto call = [&](uint32_t address, CFunc ours, Params& params) { CallCommand(original, address, ours, params); };
        using namespace CFuncs;
        Params none;

        // The destroy list's entry
        {
            alignas(4) uint8_t entry[0xC];
            std::memset(entry, 0xCD, sizeof(entry));
            auto* result = original ? Original<CutsceneDestroyEntry*(__thiscall*)(void*)>(0x005ADF60)(entry)
                                    : reinterpret_cast<CutsceneDestroyEntry*>(entry)->Construct();
            Record({reinterpret_cast<uint32_t>(result) == reinterpret_cast<uint32_t>(entry) ? 1u : 0u,
                    reinterpret_cast<uint32_t*>(entry)[0], reinterpret_cast<uint32_t*>(entry)[1],
                    reinterpret_cast<uint32_t*>(entry)[2]});
        }
        // PlaySkaterCamAnim: the game camera, by id / index (local or not, none), stop, a name
        {
            Params a; a.Flag(kUseGameCamera).Integer(kSkater, 1);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, a);
            Params b; b.Checksum(kSkaterId, 0x52).Checksum(kName, 0xA111);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, b);
            Params c; c.Checksum(kSkaterId, 0x53).Checksum(kName, 0xA112);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, c);
            Params d; d.Integer(kSkater, 2).Checksum(kName, 0xA113);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, d);
            Params e; e.Integer(kSkater, 0).Flag(kStop);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, e);
            Params f; f.Checksum(kName, 0xA114);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, f);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, none);
            Params g; g.Integer(kSkater, 7);
            call(0x005AD830, CFunc_PlaySkaterCamAnim, g);
        }
        // Skippable / should pause / params / finished / held, for each skater
        {
            Params byIndex[4];
            byIndex[0].Checksum(kName, 0xB111).Integer(kSkippable, 1).Integer(kShouldPause, 7);
            byIndex[1].Integer(kSkater, 1).Checksum(kName, 0xB112).Integer(kSkippable, 0);
            byIndex[2].Integer(kSkater, 2).Checksum(kName, 0xB113).Integer(kShouldPause, 1);
            byIndex[3].Checksum(kSkaterId, 0x52).Integer(kSkippable, 3);
            for (uint32_t answer : {0u, 1u}) {
                g_answers[0x0057AA20] = answer;
                g_answers[0x0057AA70] = answer ^ 1;
                g_answers[0x0057AF60] = answer ? reinterpret_cast<uint32_t>(g_animParams.Struct()) : 0u;
                for (Params& p : byIndex) {
                    call(0x005AD960, CFunc_SetSkaterCamAnimSkippable, p);
                    call(0x005ADA30, CFunc_SetSkaterCamAnimShouldPause, p);
                    call(0x005ADB50, CFunc_GetSkaterCamAnimParams, p);
                    call(0x005ADC10, CFunc_SkaterCamAnimFinished, p);
                    call(0x005ADCC0, CFunc_SkaterCamAnimHeld, p);
                }
            }
            call(0x005ADB00, CFunc_GetCurrentSkaterCamAnimName, none);
            g_answers[0x0057B030] = 0xC0C0;
            call(0x005ADB00, CFunc_GetCurrentSkaterCamAnimName, none);
        }
        // KillSkaterCamAnim: Current / all / a name, local or not, the park editor's state, movies left or not
        {
            Params current; current.Flag(kCurrent);
            Params all; all.Flag(kAll).Integer(kSkater, 1);
            Params named; named.Checksum(kName, 0xD111).Checksum(kSkaterId, 0x52);
            Params remote; remote.Integer(kSkater, 2).Flag(kAll);
            Params nobody; nobody.Integer(kSkater, 9).Flag(kAll);
            g_answers[0x00566520] = reinterpret_cast<uint32_t>(g_skaters[0]);
            for (uint32_t count : {0u, 2u}) {
                g_answers[0x0057A330] = count;
                for (uint32_t state : {0u, kEdit}) {
                    *reinterpret_cast<uint32_t*>(g_editor + 0x40) = state;
                    for (uint32_t ended : {0u, 1u}) {
                        g_answers[0x0057A7E0] = ended;
                        call(0x005ADD70, CFunc_KillSkaterCamAnim, current);
                    }
                    for (Params* p : {&all, &named, &remote, &nobody})
                        call(0x005ADD70, CFunc_KillSkaterCamAnim, *p);
                }
            }
            g_answers[0x00566520] = 0;
            call(0x005ADD70, CFunc_KillSkaterCamAnim, current);
        }
        // ReloadSkaterCamAnim
        {
            Params full; full.Checksum(0, 0xE111).String(0, "anims\\cam.ska");
            Params noFile; noFile.Checksum(0, 0xE112);
            Params noName; noName.String(0, "x");
            call(0x005ADEC0, CFunc_ReloadSkaterCamAnim, full);
            GameVariable<uint8_t>(0x007CEFAC) = 1;
            for (uint32_t answer : {0u, 1u}) {
                g_answers[0x00407EC0] = answer;
                call(0x005ADEC0, CFunc_ReloadSkaterCamAnim, full);
            }
            call(0x005ADEC0, CFunc_ReloadSkaterCamAnim, noFile);
            call(0x005ADEC0, CFunc_ReloadSkaterCamAnim, noName);
        }
        // Cutscenes, the queue, pausing, object animations
        {
            for (uint32_t answer : {0u, 1u}) {
                g_answers[0x0049A8E0] = answer;
                g_answers[0x0057B070] = answer;
                g_answers[0x0057B0B0] = answer ^ 1;
                g_answers[0x0057AA20] = answer;
                g_answers[0x0057AA70] = answer ^ 1;
                g_answers[0x0057A7E0] = answer;
                call(0x005AE000, CFunc_CutsceneDestroyListActive, none);
                call(0x005AE090, CFunc_HasMovieStarted, none);
                call(0x005AE0A0, CFunc_IsMovieQueued, none);
                Params anim; anim.Checksum(kName, 0xF111);
                call(0x005AE270, CFunc_ObjectAnimFinished, anim);
                call(0x005AE2B0, CFunc_ObjectAnimHeld, anim);
                Params current; current.Flag(kCurrent);
                call(0x005AE2F0, CFunc_KillObjectAnim, current);
            }
            Params cut; cut.Integer(0x2222, 4);
            call(0x005AE010, CFunc_PlayCutscene, cut);
            Params objCut; objCut.Flag(kObjectAnim).Checksum(kName, 0xF112).Flag(kStop);
            call(0x005AE010, CFunc_PlayCutscene, objCut);
            Params p1; p1.Integer(0, 1);
            Params p0; p0.Integer(0, 0);
            Params p2; p2.Integer(0, 2);
            Params toggle; toggle.Flag(kToggle).Integer(0, 0);
            for (Params* p : {&none, &p1, &p0, &p2, &toggle})
                call(0x005AE0B0, CFunc_PauseCutscene, *p);
            call(0x005AE120, CFunc_Cutscene_FrameAdvance, none);
            Params obj; obj.Checksum(kName, 0xF113).Flag(kStop).Integer(0x3333, 1);
            call(0x005AE140, CFunc_PlayObjectAnim, obj);
            call(0x005AE140, CFunc_PlayObjectAnim, none);
            Params s1; s1.Checksum(kName, 0xF114).Integer(kSkippable, 5).Integer(kShouldPause, 0);
            Params s2; s2.Integer(kShouldPause, 9);
            for (Params* p : {&s1, &s2, &none}) {
                call(0x005AE190, CFunc_SetObjectAnimSkippable, *p);
                call(0x005AE200, CFunc_SetObjectAnimShouldPause, *p);
            }
            Params all; all.Flag(kAll);
            Params named; named.Checksum(kName, 0xF115);
            call(0x005AE2F0, CFunc_KillObjectAnim, all);
            call(0x005AE2F0, CFunc_KillObjectAnim, named);
        }
        RecordState();
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 700, "movie commands: the original reached its calls");
    Compare(traces[0], traces[1], "skater camera animation, cutscene and object animation commands (0x005AD830-0x005AE2F0)");
}

} // namespace

SELFTEST(Early, TestMovieCFuncs, Restore);
