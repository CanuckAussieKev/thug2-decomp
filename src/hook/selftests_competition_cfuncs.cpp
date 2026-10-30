// Self-tests for the script commands of the game modes' runs (0x005AF090-0x005AF74F, 0x005AFB40-0x005AFD7F):
// split screen options, competitions, horse, score options, bonuses. Each original against ours on the same stand-in
// skate module, game mode, memory; the competition's, horse game's, score's, preferences' and game mode's calls are
// stood in for by recording code (over the game's function and ours alike), strings and structures come from a
// stand-in heap reset before each run.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/crc.h"
#include "game/core/string.h"
#include "game/obj/trick_objects.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/competition.h"
#include "game/skate/competition_cfuncs.h"
#include "game/skater/skater.h"
#include "game/skate/score.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;

constexpr uint32_t kTimeLimit = 0xC51BF6E3;
constexpr uint32_t kHorseTimeLimit = 0xC146142E;
constexpr uint32_t kIsHorse = 0xD1993397;
constexpr uint32_t kChecksum = 0x21902065;
constexpr uint32_t kExtraParams = 0xA61DC9A4;
constexpr uint32_t kWhichString = 0xB8D0CCC8;
constexpr uint32_t kId = 0x40C698AF;
constexpr uint32_t kFreezeScore = 0x294C84E1;
constexpr uint32_t kSkater = 0x5B8AB877;

alignas(16) uint8_t g_competition[0x200];
alignas(16) uint8_t g_horse[0x440];
alignas(16) uint8_t g_skaters[3][0x330];
alignas(16) uint8_t g_trickObjects[0x40];
Params g_gameTypePref;
uint32_t g_horseAnswer, g_competitionAnswer;
bool g_noLocalSkater;

uint32_t NameCrc(const char* name) { return Crc::GenerateCRCFromString(name); }

uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}


void StandInCompetitionCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInGameModeIsTrue(s);
    StandInMemory(s);
    // The skate module
    s.At({0x00564D80}, 0, [](Regs& r) {
        Record({0x4D80, r.ecx});
        return 0x9EF59EF5u; // (the preferences: their calls are stood in for)
    });
    s.At({0x00564D50, AddressOf(&Mdl::Skate::SetUnknown370)}, 4, [](Regs& r) {
        Record({0x4D50, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return r.args[0] < 3 ? reinterpret_cast<uint32_t>(g_skaters[r.args[0]]) : 0u;
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return g_noLocalSkater ? 0u : reinterpret_cast<uint32_t>(g_skaters[1]);
    });
    s.At({0x00566600}, 4, [](Regs& r) {
        Record({0x6600, r.ecx, r.args[0]});
        return r.args[0] == 0x51 ? reinterpret_cast<uint32_t>(g_skaters[2]) : 0u;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, r.ecx});
        return r.ecx + 0x5C0;
    });
    s.At({0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, [](Regs& r) {
        Record({0x1B40, r.ecx});
        return 4321u;
    });
    s.At({0x00561410}, 4, [](Regs& r) {
        Record({0x1410, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00561420}, 0, [](Regs& r) {
        Record({0x1420, r.ecx});
        return 0x77u;
    });
    // The preferences
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Record({0xC4A0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_gameTypePref.Struct());
    });
    s.At({0x0046C550, AddressOf(&Prefs::Preferences::GetPreferenceValue)}, 8, [](Regs& r) {
        Record({0xC550, r.ecx, r.args[0], r.args[1]});
        return r.args[0] == kTimeLimit ? 300u : r.args[0] == kHorseTimeLimit ? 45u : 0u;
    });
    s.At({0x0046C510, AddressOf(&Prefs::Preferences::GetPreferenceString)}, 8, [](Regs& r) {
        Record({0xC510, r.ecx, r.args[0], r.args[1]});
        return reinterpret_cast<uint32_t>("SKATE");
    });
    // The game mode
    s.At({0x00553AD0}, 4, [](Regs& r) {
        Record({0x3AD0, r.ecx, r.args[0]});
        return 1u;
    });
    s.At({0x00553BE0}, 4, [](Regs& r) {
        Record({0x3BE0, r.ecx, r.args[0]});
        return 1u;
    });
    for (uint32_t address : {0x00553000u, 0x00553020u, 0x00553040u, 0x00553060u}) {
        s.At({address}, 4, [address](Regs& r) {
            Record({address, r.ecx, r.args[0] & 0xFF});
            return 0u;
        });
    }
    // The competition
    s.At({0x00550010, AddressOf(&Mdl::CCompetition::EditParams)}, 4, [](Regs& r) {
        Record({0x0010, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00550020, AddressOf(&Mdl::CCompetition::StartRun)}, 0, [](Regs& r) {
        Record({0x0020, r.ecx});
        return 0u;
    });
    s.At({0x00550560}, 28, [](Regs& r) {
        Record({0x0560, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6]});
        return 0u;
    });
    s.At({0x00550EF0}, 4, [](Regs& r) {
        Record({0x0EF0, r.ecx, r.args[0]});
        return 0u;
    });
    for (uint32_t address : {0x00550B50u, 0x005501E0u}) {
        s.At({address}, 0, [address](Regs& r) {
            Record({address, r.ecx});
            return 0u;
        });
    }
    for (uint32_t address : {0x005501A0u, 0x005501C0u}) {
        s.At({address}, 4, [address](Regs& r) {
            Record({address, r.ecx, r.args[0]});
            return g_competitionAnswer;
        });
    }
    s.At({0x005501F0}, 0, [](Regs& r) {
        Record({0x01F0, r.ecx});
        return g_competitionAnswer;
    });
    // The horse game
    for (uint32_t address : {0x00560100u, 0x00560180u, 0x00560720u, 0x005608B0u}) {
        s.At({address}, 0, [address](Regs& r) {
            Record({address, r.ecx});
            return 0u;
        });
    }
    s.At({0x00560380}, 0, [](Regs& r) {
        Record({0x0380, r.ecx});
        return g_horseAnswer;
    });
    s.At({0x00560190}, 4, [](Regs& r) {
        Record({0x0190, r.ecx, r.args[0]});
        return g_horseAnswer ^ 1;
    });
    s.At({0x005601F0}, 12, [](Regs& r) {
        auto* text = reinterpret_cast<Str::String*>(&r.args[0]);
        Record({0x01F0, r.ecx, Hash(text->c_str())});
        text->~String(); // (the callee's to destroy)
        return 0u;
    });
    s.At({0x00560440}, 8, [](Regs& r) {
        Record({0x0440, r.ecx, r.args[1]});
        auto* text = new (reinterpret_cast<void*>(r.args[0])) Str::String(r.args[1] == 0x1111 ? "H-O" : "first");
        return reinterpret_cast<uint32_t>(text);
    });
    // The trick objects
    s.At({0x00589670, AddressOf(&Obj::CTrickObjectManager::GiveFreeClusters)}, 4, [](Regs& r) {
        Record({0x9670, r.ecx, r.args[0]});
        return 0u;
    });
}

void TestCompetitionCFuncs()
{
    Setup();
    auto* standIns = new StandIns;
    g_saved.push_back({0x007CE500, 4, {}});
    g_saved.push_back({0x00684FA8, 1, {}});
    for (size_t i = g_saved.size() - 2; i < g_saved.size(); ++i) {
        g_saved[i].bytes.resize(g_saved[i].size);
        std::memcpy(g_saved[i].bytes.data(), reinterpret_cast<void*>(g_saved[i].address), g_saved[i].size);
    }
    StandInCompetitionCalls(*standIns);
    GameVariable<uint8_t*>(0x007CE500) = g_trickObjects;
    if (!g_gameTypePref.count)
        g_gameTypePref.Checksum(kChecksum, 0xC0DEC0DE);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        *reinterpret_cast<uint8_t**>(g_skate + 0x360) = g_competition;
        *reinterpret_cast<uint8_t**>(g_skate + 0x364) = g_horse;
        std::memset(g_horse, 0, sizeof(g_horse));
        *reinterpret_cast<uint32_t*>(g_horse + 4) = 0x51;
        std::memset(g_skaters, 0, sizeof(g_skaters));
        g_skaters[0][0xBC] = 1;
        g_skaters[2][0xBC] = 1;
        GameVariable<bool>(0x00684FA8) = false;
        g_horseAnswer = g_competitionAnswer = 0;
        g_noLocalSkater = false;
        auto call = [&](uint32_t address, CFunc ours, Params& params) { CallCommand(original, address, ours, params); };
        using namespace CFuncs;
        Params none;

        // ApplySplitScreenOptions: not horse, horse
        call(0x005AF090, CFunc_ApplySplitScreenOptions, none);
        g_modeProperties[kIsHorse] = 1;
        call(0x005AF090, CFunc_ApplySplitScreenOptions, none);
        // Competitions
        {
            static Params extra;
            if (!extra.count)
                extra.Integer(0x4444, 5);
            Params a;
            a.Float(0x00000000, 9.0f); // (unnamed: not read)
            a.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("gold"), Bits(90.5f));
            a.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("silver"), Bits(70.25f));
            a.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("bronze"), Bits(50.0f));
            a.Add(Script::ESYMBOLTYPE_INTEGER, NameCrc("gold_score"), 30000);
            a.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("silver_score"), Bits(20000.0f));
            a.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("bronze_score"), Bits(10000.5f));
            a.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("bail"), Bits(-3.5f));
            a.Structure(kExtraParams, extra.Struct());
            call(0x005AF200, CFunc_StartCompetition, a);
            Params b;
            b.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("gold"), Bits(1.0f));
            b.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("silver"), Bits(2.0f));
            b.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("bronze"), Bits(3.0f));
            b.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("gold_score"), Bits(4.0f));
            b.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("silver_score"), Bits(5.0f));
            b.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("bronze_score"), Bits(6.0f));
            b.Add(Script::ESYMBOLTYPE_FLOAT, NameCrc("bail"), Bits(7.0f));
            call(0x005AF200, CFunc_StartCompetition, b);
            call(0x005AF320, CFunc_StartCompetitionRun, none);
            call(0x005AF340, CFunc_EndCompetitionRun, none);
            Params c; c.Integer(0, 2);
            for (uint32_t answer : {0u, 1u}) {
                g_competitionAnswer = answer;
                call(0x005AF3A0, CFunc_PlaceIs, c);
                call(0x005AF3D0, CFunc_RoundIs, c);
                call(0x005AF440, CFunc_CompetitionEnded, none);
            }
            call(0x005AF400, CFunc_EndCompetition, none);
        }
        // Horse
        {
            call(0x005AF450, CFunc_StartHorse, none);
            Params status; status.Flag(0x5555);
            for (uint32_t answer : {0u, 1u}) {
                g_horseAnswer = answer;
                call(0x005AF470, CFunc_HorseEnded, none);
                call(0x005AF480, CFunc_HorseStatusEquals, status);
            }
            call(0x005AF4A0, CFunc_StartHorseRun, none);
            call(0x005AF4C0, CFunc_EndHorseRun, none);
            call(0x005AF4E0, CFunc_SwitchHorsePlayers, none);
            Params a; a.Checksum(kWhichString, 0x1111).Checksum(kId, 0x2222).Integer(0x3333, 4);
            call(0x005AF500, CFunc_ApplyToHorsePanelString, a);
            *reinterpret_cast<uint32_t*>(g_horse + 4) = 0x52; // (no such skater)
            Params b; b.Checksum(kWhichString, 0x1112).Checksum(kId, 0x2223);
            call(0x005AF500, CFunc_ApplyToHorsePanelString, b);
            call(0x005AF660, CFunc_GetHorseString, a);
            call(0x005AF660, CFunc_GetHorseString, b);
            Params c; c.Integer(0, 0x52);
            call(0x005AF700, CFunc_IsCurrentHorseSkater, c);
            Params d; d.Checksum(0, 0x52);
            call(0x005AF700, CFunc_IsCurrentHorseSkater, d);
            Params e; e.Integer(0, 0x53);
            call(0x005AF700, CFunc_IsCurrentHorseSkater, e);
        }
        // Trick objects, the game mode's score options, bonuses, the local skater
        {
            Params a; a.Integer(0, 3);
            call(0x005AFB40, CFunc_TrickOffAllObjects, a);
            call(0x005AFB40, CFunc_TrickOffAllObjects, none);
            GameVariable<bool>(0x00684FA8) = true;
            call(0x005AFB40, CFunc_TrickOffAllObjects, a);
            GameVariable<bool>(0x00684FA8) = false;
            Params off; off.Integer(0, 0);
            Params on; on.Integer(0, 7);
            Params offFreeze; offFreeze.Integer(0, 0).Flag(kFreezeScore);
            Params onFreeze; onFreeze.Integer(0, 1).Flag(kFreezeScore);
            for (Params* p : {&none, &off, &on}) {
                call(0x005AFB80, CFunc_SetScoreDegradation, *p);
                call(0x005AFBC0, CFunc_SetTrickScoreTracking, *p);
            }
            for (Params* p : {&none, &off, &on, &offFreeze, &onFreeze})
                call(0x005AFC00, CFunc_SetScoreAccumulation, *p);
            Params b0; b0.Integer(kSkater, 0).Integer(0, 250);
            Params b1; b1.Integer(kSkater, 1).Integer(0, 250);
            Params b2; b2.Integer(kSkater, 2);
            for (Params* p : {&b0, &b1, &b2}) {
                call(0x005AFC70, CFunc_AddBonus, *p);
                call(0x005AFCF0, CFunc_GetBonusValue, *p);
            }
            call(0x005AFD60, CFunc_LocalSkaterExists, none);
            g_noLocalSkater = true;
            call(0x005AFD60, CFunc_LocalSkaterExists, none);
        }
        RecordState();
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        g_trace.push_back(GameVariable<uint32_t>(0x00690FA0));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 500, "competition commands: the original reached its calls");
    Compare(traces[0], traces[1], "split screen, competition, horse, score option and bonus commands (0x005AF090-0x005AFD60)");
}

} // namespace

SELFTEST(Early, TestCompetitionCFuncs, Restore);
