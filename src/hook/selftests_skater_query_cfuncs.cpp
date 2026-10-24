// Self-tests for the script commands asking about the skaters (0x005AC5F0-0x005AD82F): each original against ours on
// stand-in skaters (their velocity, local flag, id), scores, network players and profile; the player list, the
// player's flags, the scores' totals, the components, the camera's toggle, messages sent and the structure adds are
// stood in for by recording code (over the game's function and ours alike).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/skater/skater_query_cfuncs.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;

constexpr uint32_t kPointScale = 0xB08C5AE8;
constexpr uint32_t kSkater = 0x5B8AB877;
constexpr uint32_t kPlayer = 0x67E6859A;
constexpr uint32_t kIsMale = 0x3F813177;
constexpr int kPlayers = 4;

// Stand-in skaters, their scores and the network players.
alignas(16) uint8_t g_skaters[kPlayers][0x330];
alignas(16) uint8_t g_scores[kPlayers][0x200];
alignas(16) uint8_t g_components[kPlayers][0x20];
alignas(16) uint8_t g_players[kPlayers][0x200];
alignas(16) uint8_t g_camera[0x110];
struct PlayerState {
    bool present, eliminated, local, observer, surveying;
};
PlayerState g_state[kPlayers];
int g_localSkater = -1;       // GetLocalSkater's answer (-1: none)
int g_localPlayer = -1;       // GetLocalPlayer's
bool g_teamGame, g_joining;
int g_hostMode;
int g_goals;
uint32_t g_searchIndex;       // where the stand-in player list is (one walk at a time, as the commands do it)
uint32_t g_searchIndex2;
Params g_profileInfo;
alignas(16) uint8_t g_profile[0x64];

int IndexOf(const uint8_t (*table)[0x200], uint32_t address)
{
    for (int i = 0; i < kPlayers; ++i) {
        if (address == reinterpret_cast<uint32_t>(table[i]))
            return i;
    }
    return -1;
}
int SkaterIndex(uint32_t address)
{
    for (int i = 0; i < kPlayers; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_skaters[i]))
            return i;
    }
    return -1;
}
// The next present player at or after `from` (observers only when asked), or kPlayers.
uint32_t NextPresent(uint32_t from, bool observers)
{
    while (from < kPlayers && (!g_state[from].present || (!observers && g_state[from].observer)))
        ++from;
    return from;
}

void StandInQueryCalls(StandIns& s)
{
    StandInStructAdds(s);
    // The skate module and the skaters
    s.At({0x00566520}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return g_localSkater < 0 ? 0u : reinterpret_cast<uint32_t>(g_skaters[g_localSkater]);
    });
    s.At({0x005665C0}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return r.args[0] < kPlayers && g_state[r.args[0]].present ? reinterpret_cast<uint32_t>(g_skaters[r.args[0]]) : 0u;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        int i = SkaterIndex(r.ecx);
        Record({0x0480, static_cast<uint32_t>(i)});
        return i >= 0 && i != 3 ? reinterpret_cast<uint32_t>(g_scores[i]) : 0u; // (skater 3 has no score)
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        int i = SkaterIndex(r.ecx);
        Record({0xDB90, static_cast<uint32_t>(i), r.args[0]});
        if (i >= 0)
            return reinterpret_cast<uint32_t>(g_components[i]);
        return r.ecx == reinterpret_cast<uint32_t>(g_camera) ? 0xCA3E0000u : 0u;
    });
    s.At({0x00561B40}, 0, [](Regs& r) {
        int i = IndexOf(g_scores, r.ecx);
        Record({0x1B40, static_cast<uint32_t>(i)});
        return i >= 0 ? *reinterpret_cast<uint32_t*>(g_scores[i] + 0x100) : 0u;
    });
    s.At({0x00580880, AddressOf(&Obj::CSkater::GetSkaterCamera)}, 0, [](Regs& r) {
        Record({0x0880, static_cast<uint32_t>(SkaterIndex(r.ecx))});
        return reinterpret_cast<uint32_t>(g_camera);
    });
    s.At({0x004FA7D0}, 0, [](Regs& r) {
        Record({0xA7D0, r.ecx});
        return 0u;
    });
    s.At({0x005842A0, AddressOf(&Obj::CSkaterCareer::CountTotalGoals)}, 0, [](Regs& r) {
        Record({0x42A0, r.ecx});
        return static_cast<uint32_t>(g_goals);
    });
    s.At({0x0057B640, AddressOf(&Obj::CPlayerProfileManager::GetProfile)}, 4, [](Regs& r) {
        Record({0xB640, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profile);
    });
    // The game mode and the network manager
    s.At({0x005535F0}, 0, [](Regs& r) {
        Record({0x35F0, r.ecx});
        return g_teamGame ? 1u : 0u;
    });
    s.At({0x00532AD0, AddressOf(&GameNet::Manager::GetHostMode)}, 0, [](Regs& r) {
        Record({0x2AD0, r.ecx});
        return static_cast<uint32_t>(g_hostMode);
    });
    s.At({0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, [](Regs& r) {
        Record({0x7320, r.ecx});
        return g_localPlayer < 0 ? 0u : reinterpret_cast<uint32_t>(g_players[g_localPlayer]);
    });
    s.At({0x00537070, AddressOf(&GameNet::Manager::FirstNewPlayerInfo)}, 4, [](Regs& r) {
        Record({0x7070, r.ecx, *reinterpret_cast<uint32_t*>(r.args[0])});
        return g_joining ? 0x10E30000u : 0u;
    });
    // The player lists: each search remembers its place in its node word.
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0x72C0, r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), r.args[1] & 0xFF});
        uint32_t i = NextPresent(0, (r.args[1] & 0xFF) != 0);
        reinterpret_cast<uint32_t*>(r.args[0])[1] = i;
        return i < kPlayers ? reinterpret_cast<uint32_t>(g_players[i]) : 0u;
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Record({0x7010, r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), r.args[1] & 0xFF});
        uint32_t& place = reinterpret_cast<uint32_t*>(r.args[0])[1];
        uint32_t i = place < kPlayers ? NextPresent(place + 1, (r.args[1] & 0xFF) != 0) : kPlayers;
        place = i;
        return i < kPlayers ? reinterpret_cast<uint32_t>(g_players[i]) : 0u;
    });
    // The players
    auto flag = [&s](uint32_t address, uint32_t ours, bool PlayerState::*field) {
        s.At({address, ours}, 0, [address, field](Regs& r) {
            int i = IndexOf(g_players, r.ecx);
            Record({address, static_cast<uint32_t>(i)});
            return i >= 0 && g_state[i].*field ? 1u : 0u;
        });
    };
    flag(0x00543440, AddressOf(&GameNet::PlayerInfo::IsEliminated), &PlayerState::eliminated);
    flag(0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer), &PlayerState::local);
    flag(0x00543320, AddressOf(&GameNet::PlayerInfo::IsObserver), &PlayerState::observer);
    flag(0x00543330, AddressOf(&GameNet::PlayerInfo::IsSurveying), &PlayerState::surveying);
    s.At({0x00543420, AddressOf(&GameNet::PlayerInfo::MarkEliminated)}, 0, [](Regs& r) {
        int i = IndexOf(g_players, r.ecx);
        Record({0x3420, static_cast<uint32_t>(i)});
        if (i >= 0)
            g_state[i].eliminated = true;
        return 0u;
    });
    s.At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) {
        int i = IndexOf(g_players, r.ecx);
        Record({0x34A0, static_cast<uint32_t>(i)});
        return 0x4A000u + static_cast<uint32_t>(i);
    });
    s.At({0x00543160, AddressOf(&GameNet::PlayerInfo::AddToAccumulatedScore)}, 4, [](Regs& r) {
        Record({0x3160, static_cast<uint32_t>(IndexOf(g_players, r.ecx)), r.args[0]});
        return 0u;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[1]);
        Record({0x7EB0, r.ecx, r.args[0], desc->m_id, desc->m_length, *static_cast<uint32_t*>(desc->m_data),
                static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue), desc->m_groupId,
                static_cast<uint32_t>(desc->m_delay), desc->m_forcedSequenceId, desc->m_compression, desc->m_stream,
                desc->m_singular ? 1u : 0u});
        return 0u;
    });
}

void SetScore(int i, int base, float multiplier, int last, int total)
{
    *reinterpret_cast<int*>(g_scores[i] + 0x14) = base;
    *reinterpret_cast<float*>(g_scores[i] + 0x60) = multiplier;
    *reinterpret_cast<int*>(g_scores[i] + 0x20) = last;
    *reinterpret_cast<int*>(g_scores[i] + 0x100) = total;
}
void SetVelocity(int i, float x, float y, float z)
{
    auto* skater = reinterpret_cast<Obj::CCompositeObject*>(g_skaters[i]);
    skater->m_vel.x = x;
    skater->m_vel.y = y;
    skater->m_vel.z = z;
}

void TestSkaterQueryCFuncs()
{
    Setup();
    g_saved.push_back({0x007CE4B4, 4, {}});
    g_saved.back().bytes.resize(4);
    std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(0x007CE4B4), 4);
    GameVariable<uint32_t>(0x007CE4B4) = 0x9F0F0000;
    auto* standIns = new StandIns;
    StandInQueryCalls(*standIns);
    if (!g_profileInfo.count)
        g_profileInfo.Integer(0x1234, 5).Integer(kIsMale, 1);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        std::memset(g_skaters, 0, sizeof(g_skaters));
        std::memset(g_scores, 0, sizeof(g_scores));
        std::memset(g_players, 0, sizeof(g_players));
        std::memset(g_camera, 0, sizeof(g_camera));
        std::memset(g_profile, 0, sizeof(g_profile));
        std::memcpy(g_profile + 0x5C, g_profileInfo.Struct(), 8);
        for (int i = 0; i < kPlayers; ++i) {
            auto* skater = reinterpret_cast<Obj::CCompositeObject*>(g_skaters[i]);
            skater->m_id = 0x51 + i;
            skater->m_unknownBC = i == 0 || i == 2;
            *reinterpret_cast<uint8_t**>(g_components[i] + 0x18) = g_scores[i];
            auto* player = reinterpret_cast<GameNet::PlayerInfo*>(g_players[i]);
            player->m_skater = reinterpret_cast<Obj::CSkater*>(skater);
            player->m_team = i % 2;
            g_state[i] = {true, false, i == 0, false, false};
        }
        SetScore(0, 1000, 1.5f, 250, 5000);
        SetScore(1, 333, 2.75f, 90, 7000);
        SetScore(2, -7, 3.0f, -5, 5000);
        SetScore(3, 10, 1.0f, 1, 100);
        SetVelocity(0, 3.0f, 4.0f, 12.0f); // speed 13
        g_localSkater = 0;
        g_localPlayer = 0;
        g_teamGame = g_joining = false;
        g_hostMode = 0;
        g_goals = 7;
        *reinterpret_cast<uint32_t*>(g_net + 4) = 0x0A9900A0; // (the server app: EnqueueMessage is stood in for)
        auto call = [&](uint32_t address, CFunc ours, Params& params, void* object = nullptr) {
            CallCommand(original, address, ours, params, object);
        };
        using namespace CFuncs;
        Params none;

        // The local skater's speed and scores
        {
            Params speeds[4];
            speeds[0].Float(0, 12.5f);
            speeds[1].Float(0, 13.0f);
            speeds[2].Float(0, 13.5f);
            speeds[3].Integer(0, 13);
            for (Params& p : speeds) {
                call(0x005AC5F0, CFunc_SkaterSpeedGreaterThan, p);
                call(0x005AC660, CFunc_SkaterSpeedLessThan, p);
            }
            call(0x005AC5F0, CFunc_SkaterSpeedGreaterThan, none);
            call(0x005AC660, CFunc_SkaterSpeedLessThan, none);
            Params values[6];
            values[0].Integer(0, 249);
            values[1].Integer(0, 250);
            values[2].Integer(0, 5001);
            values[3].Integer(0, 1501);
            values[4].Integer(0, 5000); // (equal to skaters 0 and 2's totals)
            values[5].Integer(0, 1500); // (equal to skater 0's pot)
            Params scaled; scaled.Integer(0, 501).Integer(kPointScale, 3);
            Params scaled2; scaled2.Integer(0, 499).Integer(kPointScale, 3);
            Params negative; negative.Integer(0, -10);
            for (int local : {0, 2, 3, -1}) {
                g_localSkater = local;
                for (Params* p : {&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &none, &negative}) {
                    call(0x005AC6D0, CFunc_SkaterLastScoreLandedGreaterThan, *p);
                    call(0x005AC720, CFunc_SkaterLastScoreLandedLessThan, *p);
                    call(0x005ACF50, CFunc_SkaterTotalScoreGreaterThan, *p);
                    call(0x005ACFA0, CFunc_SkaterTotalScoreLessThan, *p);
                    call(0x005ACFF0, CFunc_SkaterCurrentScorePotGreaterThan, *p);
                    call(0x005AD070, CFunc_SkaterCurrentScorePotLessThan, *p);
                }
                call(0x005ACFF0, CFunc_SkaterCurrentScorePotGreaterThan, scaled);
                call(0x005ACFF0, CFunc_SkaterCurrentScorePotGreaterThan, scaled2);
                call(0x005AC770, CFunc_SkaterGetLastScoreLanded, none);
                call(0x005AC5F0, CFunc_SkaterSpeedGreaterThan, speeds[0]);
                if (local >= 0 && local != 3)
                    call(0x005AD0D0, CFunc_SkaterGetScoreInfo, none);
            }
            g_localSkater = 0;
        }
        // The network players: scores accumulated, the places, who is left (teams or not)
        {
            call(0x005AC7B0, CFunc_AccumulateScores, none);
            for (bool team : {false, true}) {
                g_teamGame = team;
                call(0x005ACAA0, CFunc_OnlyOnePlayerLeft, none);
                Params at; at.Integer(0, 7000);
                Params above; above.Integer(0, 12001);
                Params team2; team2.Integer(0, 10000);
                for (Params* p : {&at, &above, &team2, &none})
                    call(0x005ACC20, CFunc_AnySkaterTotalScoreAtLeast, *p);
                call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none);
                // team 1 (players 1 and 3) out; then two players in, then one
                g_state[1].eliminated = g_state[3].eliminated = true;
                call(0x005ACAA0, CFunc_OnlyOnePlayerLeft, none);
                g_state[3].eliminated = false;
                call(0x005ACAA0, CFunc_OnlyOnePlayerLeft, none);
                g_state[0].eliminated = true;
                call(0x005ACAA0, CFunc_OnlyOnePlayerLeft, none);
                g_state[0].eliminated = g_state[1].eliminated = g_state[3].eliminated = false;
                // a player without a skater (not a team game: skipped)
                if (!team) {
                    reinterpret_cast<GameNet::PlayerInfo*>(g_players[1])->m_skater = nullptr;
                    call(0x005ACC20, CFunc_AnySkaterTotalScoreAtLeast, at);
                    call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none);
                    reinterpret_cast<GameNet::PlayerInfo*>(g_players[1])->m_skater =
                        reinterpret_cast<Obj::CSkater*>(g_skaters[1]);
                }
            }
            g_teamGame = false;
            // places for each local player, with a tie (players 0 and 2: 5000)
            for (int local : {0, 1, 2, 3}) {
                g_localPlayer = local;
                for (int i = 0; i < kPlayers; ++i)
                    g_state[i].local = i == local;
                call(0x005AD140, CFunc_InFirstPlace, none);
                call(0x005AD2D0, CFunc_InLastPlace, none);
                call(0x005AD450, CFunc_InSecondToLastPlace, none);
            }
            g_localPlayer = 1;
            g_state[3].eliminated = true; // (the lowest out already)
            call(0x005AD140, CFunc_InFirstPlace, none);
            call(0x005AD2D0, CFunc_InLastPlace, none);
            call(0x005AD450, CFunc_InSecondToLastPlace, none);
            g_state[1].surveying = true;
            call(0x005AD140, CFunc_InFirstPlace, none);
            g_state[1].surveying = false;
            g_state[1].observer = true;
            call(0x005AD2D0, CFunc_InLastPlace, none);
            g_state[1].observer = false;
            g_state[1].eliminated = true;
            call(0x005AD450, CFunc_InSecondToLastPlace, none);
            g_state[1].eliminated = false;
            g_localPlayer = -1;
            call(0x005AD140, CFunc_InFirstPlace, none);
            g_localPlayer = 1;
            // one player left in
            g_state[0].eliminated = g_state[2].eliminated = true;
            call(0x005AD140, CFunc_InFirstPlace, none);
            call(0x005ACAA0, CFunc_OnlyOnePlayerLeft, none);
            g_state[0].eliminated = g_state[2].eliminated = g_state[3].eliminated = false;
            // OnlyOneSkaterLeft: players joining, one scored, none scored on the server (host modes)
            g_joining = true;
            call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none);
            g_teamGame = true;
            call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none);
            g_teamGame = g_joining = false;
            *reinterpret_cast<int*>(g_scores[0] + 0x100) = 0;
            *reinterpret_cast<int*>(g_scores[2] + 0x100) = 0;
            *reinterpret_cast<int*>(g_scores[3] + 0x100) = -4;
            call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none); // (one scored)
            *reinterpret_cast<int*>(g_scores[1] + 0x100) = 0;
            for (int hostMode : {0, 1, 2, 3}) {
                g_hostMode = hostMode;
                for (int server : {0, 1}) {
                    g_net[0] = static_cast<uint8_t>(server);
                    call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none);
                    g_teamGame = true;
                    call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none);
                    g_teamGame = false;
                }
            }
            g_net[0] = 0;
            g_state[1].present = g_state[2].present = g_state[3].present = false; // (one skater, who scored)
            *reinterpret_cast<int*>(g_scores[0] + 0x100) = 10;
            call(0x005ACD80, CFunc_OnlyOneSkaterLeft, none);
            g_state[1].present = g_state[2].present = g_state[3].present = true;
            // EliminateLastPlacePlayer: a tie (player 3, without a score, not there), then a lowest pair, then one
            // player in
            SetScore(0, 1, 1.0f, 1, 400);
            SetScore(1, 1, 1.0f, 1, 400);
            SetScore(2, 1, 1.0f, 1, 400);
            SetScore(3, 1, 1.0f, 1, 400);
            g_state[3].present = false;
            call(0x005AC850, CFunc_EliminateLastPlacePlayer, none);
            g_state[3].present = true;
            SetScore(1, 1, 1.0f, 1, 900);
            SetScore(2, 1, 1.0f, 1, 50);
            SetScore(3, 1, 1.0f, 1, 50);
            g_state[0].observer = true; // (an observer is told too)
            call(0x005AC850, CFunc_EliminateLastPlacePlayer, none);
            call(0x005AC850, CFunc_EliminateLastPlacePlayer, none);
            g_state[0].observer = false;
            call(0x005AC850, CFunc_EliminateLastPlacePlayer, none);
            for (int i = 0; i < kPlayers; ++i)
                g_state[i].eliminated = false;
        }
        // Goals, the camera, ids, sex
        {
            Params g6; g6.Float(0, 6.0f);
            Params g7; g7.Float(0, 7.0f);
            Params g75; g75.Float(0, 7.5f);
            Params g8; g8.Integer(0, 8);
            for (Params* p : {&g6, &g7, &g75, &g8, &none}) {
                call(0x005AD5C0, CFunc_GoalsGreaterThan, *p);
                call(0x005AD610, CFunc_GoalsEqualTo, *p);
            }
            Params s1; s1.Integer(kSkater, 1);
            Params s2; s2.Integer(kSkater, 2);
            Params s0; s0.Integer(kSkater, 0);
            Params s9; s9.Integer(kSkater, 9);
            for (Params* p : {&none, &s0, &s1, &s2, &s9}) {
                if (p != &none) // (a missing skater= is an uninitialised index in the original; the scripts always give one)
                    call(0x005AD660, CFunc_ToggleSkaterCamMode, *p);
                call(0x005AD6C0, CFunc_GetSkaterID, *p);
                call(0x005AD730, CFunc_SkaterExists, *p);
            }
            g_localPlayer = -1;
            call(0x005AD660, CFunc_ToggleSkaterCamMode, s0);
            g_localSkater = -1;
            call(0x005AD6C0, CFunc_GetSkaterID, none);
            g_localSkater = 1;
            call(0x005AD6C0, CFunc_GetSkaterID, none);
            Params p1; p1.Integer(kPlayer, 2);
            call(0x005AD780, CFunc_GetSkaterSex, p1);
            call(0x005AD7E0, CFunc_GetCurrentSkaterID, none, g_skaters[2]);
            call(0x005AD7E0, CFunc_GetCurrentSkaterID, none, g_skaters[1]);
            call(0x005AD7E0, CFunc_GetCurrentSkaterID, none);
        }
        RecordState();
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 1500, "skater query commands: the original reached its calls");
    Compare(traces[0], traces[1], "skater speed, score, player, place, goal and id commands (0x005AC5F0-0x005AD7E0)");
}

} // namespace

SELFTEST(Early, TestSkaterQueryCFuncs, Restore);
